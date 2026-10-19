#include <cstring>
#include <fstream>

#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <ovis/english/model_daemon.hpp>

namespace ovis::english {

namespace {

// Requests and replies are fields, each its length in decimal, a colon and
// its bytes: no escaping, and numbers travel as their bytes (the daemon is
// the same build on the same machine).
auto put(string_type &p_out, string_view_type p_field) -> void {
  p_out += std::to_string(p_field.size());
  p_out += ':';
  p_out += p_field;
}

auto take(string_view_type &p_in) -> string_type {
  const auto colon = p_in.find(':');
  if (colon == string_view_type::npos || colon == 0)
    throw error("The model daemon sent a malformed message.");
  const auto size = std::stoull(string_type(p_in.substr(0, colon)));
  if (p_in.size() - colon - 1 < size)
    throw error("The model daemon sent a malformed message.");
  string_type field(p_in.substr(colon + 1, size));
  p_in.remove_prefix(colon + 1 + size);
  return field;
}

template <typename T>
auto put_values(string_type &p_out, const std::vector<T> &p_values) -> void {
  put(p_out, string_view_type(reinterpret_cast<const char *>(p_values.data()),
                              p_values.size() * sizeof(T)));
}

template <typename T> auto take_values(string_view_type &p_in) -> std::vector<T> {
  const auto bytes = take(p_in);
  std::vector<T> values(bytes.size() / sizeof(T));
  std::memcpy(values.data(), bytes.data(), values.size() * sizeof(T));
  return values;
}

auto put_messages(string_type &p_out, const std::vector<chat_message> &p_messages)
    -> void {
  put(p_out, std::to_string(p_messages.size()));
  for (const auto &message : p_messages) {
    put(p_out, message.role);
    put(p_out, message.content);
  }
}

auto take_messages(string_view_type &p_in) -> std::vector<chat_message> {
  std::vector<chat_message> messages(std::stoull(take(p_in)));
  for (auto &message : messages) {
    message.role = take(p_in);
    message.content = take(p_in);
  }
  return messages;
}

// 64-bit FNV-1a, for names that change when what they describe does.
auto mix(std::uint64_t &p_hash, string_view_type p_text) -> void {
  for (const auto letter : p_text)
    p_hash = (p_hash ^ static_cast<unsigned char>(letter)) * 1099511628211ULL;
  p_hash = (p_hash ^ 0xff) * 1099511628211ULL;
}

} // namespace

#ifndef _WIN32

namespace {

auto unix_address(const std::filesystem::path &p_socket)
    -> std::optional<sockaddr_un> {
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  const auto text = p_socket.string();
  if (text.size() >= sizeof(address.sun_path))
    return std::nullopt;
  std::memcpy(address.sun_path, text.c_str(), text.size() + 1);
  return address;
}

// A closed peer must give an error, not kill the process.
auto send_all(int p_socket, string_view_type p_bytes) -> bool {
#ifdef MSG_NOSIGNAL
  constexpr int flags = MSG_NOSIGNAL;
#else
  constexpr int flags = 0;
#endif
  while (!p_bytes.empty()) {
    const auto sent = ::send(p_socket, p_bytes.data(), p_bytes.size(), flags);
    if (sent < 0 && errno == EINTR)
      continue;
    if (sent <= 0)
      return false;
    p_bytes.remove_prefix(static_cast<std::size_t>(sent));
  }
  return true;
}

auto receive_all(int p_socket) -> std::optional<string_type> {
  string_type bytes;
  char buffer[65536];
  while (true) {
    const auto got = ::recv(p_socket, buffer, sizeof(buffer), 0);
    if (got < 0 && errno == EINTR)
      continue;
    if (got < 0)
      return std::nullopt;
    if (got == 0)
      return bytes;
    bytes.append(buffer, static_cast<std::size_t>(got));
  }
}

auto no_sigpipe(int p_socket) -> void {
#ifdef SO_NOSIGPIPE
  const int on = 1;
  setsockopt(p_socket, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#else
  (void)p_socket;
#endif
}

// One request and its reply; nothing if no daemon answered.
auto ask_daemon(const std::filesystem::path &p_socket, string_view_type p_request)
    -> std::optional<string_type> {
  const auto address = unix_address(p_socket);
  if (!address)
    return std::nullopt;
  const int connection = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (connection < 0)
    return std::nullopt;
  no_sigpipe(connection);
  std::optional<string_type> reply;
  if (::connect(connection, reinterpret_cast<const sockaddr *>(&*address),
                sizeof(*address)) == 0 &&
      send_all(connection, p_request) && ::shutdown(connection, SHUT_WR) == 0)
    reply = receive_all(connection);
  ::close(connection);
  // A daemon that died while answering sends nothing.
  if (reply && reply->empty())
    return std::nullopt;
  return reply;
}

auto answer(language_model &p_model, string_view_type p_request, bool &p_stop)
    -> string_type {
  string_type reply;
  try {
    const auto operation = take(p_request);
    string_type payload;
    if (operation == "choose") {
      const auto messages = take_messages(p_request);
      const auto options = std::stoull(take(p_request));
      put_values(payload, p_model.choose(messages, options));
    } else if (operation == "chat") {
      const auto messages = take_messages(p_request);
      const auto grammar = take(p_request);
      put(payload, p_model.chat(messages, grammar));
    } else if (operation == "embed") {
      std::vector<string_type> texts(std::stoull(take(p_request)));
      for (auto &text : texts)
        text = take(p_request);
      const auto vectors = p_model.embed(texts);
      put(payload, std::to_string(vectors.size()));
      for (const auto &vector : vectors)
        put_values(payload, vector);
    } else if (operation == "stop") {
      p_stop = true;
    } else {
      throw error("Unknown request " + operation + " to the model daemon.");
    }
    put(reply, "ok");
    reply += payload;
  } catch (const std::exception &p_error) {
    reply.clear();
    put(reply, "error");
    put(reply, p_error.what());
  }
  return reply;
}

// The daemon's directory: private to this user, or nothing.
auto private_directory(const std::filesystem::path &p_directory)
    -> std::optional<std::filesystem::path> {
  ::mkdir(p_directory.c_str(), 0700);
  struct stat status {};
  if (::lstat(p_directory.c_str(), &status) != 0 || !S_ISDIR(status.st_mode) ||
      status.st_uid != ::getuid() || (status.st_mode & 077) != 0)
    return std::nullopt;
  return p_directory;
}

// Starts `ovis model serve` detached from this process, and waits until it
// says it is ready or why it isn't. Empty when ready.
auto start_daemon(const daemon_options &p_options) -> string_type {
  int ready[2];
  if (::pipe(ready) != 0)
    return "no pipe";
  std::vector<string_type> arguments = {p_options.executable.string(), "model",
                                        "serve"};
  arguments.insert(arguments.end(), p_options.serve_arguments.begin(),
                   p_options.serve_arguments.end());
  for (const auto &item : std::vector<string_type>{
           "--socket", p_options.socket.string(), "--ready-fd",
           std::to_string(ready[1])})
    arguments.push_back(item);
  std::vector<char *> pointers;
  for (auto &argument : arguments)
    pointers.push_back(argument.data());
  pointers.push_back(nullptr);
  const auto log = p_options.socket.string() + ".log";

  // Twice, so the daemon belongs to no build and outlives it.
  const auto child = ::fork();
  if (child == 0) {
    ::setsid();
    if (::fork() == 0) {
      ::close(ready[0]);
      const int nothing = ::open("/dev/null", O_RDWR);
      const int output = ::open(log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
      ::dup2(nothing, 0);
      ::dup2(nothing, 1);
      ::dup2(output >= 0 ? output : nothing, 2);
      ::execv(pointers[0], pointers.data());
      _exit(127);
    }
    _exit(0);
  }
  ::close(ready[1]);
  if (child < 0) {
    ::close(ready[0]);
    return "can't start a process";
  }
  int status = 0;
  while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
  }
  // Loading a large model from a cold disk cache can take minutes (7B took
  // 320 s once), so wait for ten.
  constexpr auto patience = std::chrono::seconds(600);
  string_type said;
  bool timed_out = false;
  pollfd waiting{ready[0], POLLIN, 0};
  const auto deadline = std::chrono::steady_clock::now() + patience;
  while (said.find('\n') == string_type::npos) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                          deadline - std::chrono::steady_clock::now())
                          .count();
    if (left <= 0 || ::poll(&waiting, 1, static_cast<int>(left)) <= 0) {
      timed_out = true;
      break;
    }
    char buffer[512];
    const auto got = ::read(ready[0], buffer, sizeof(buffer));
    if (got <= 0)
      break;
    said.append(buffer, static_cast<std::size_t>(got));
  }
  ::close(ready[0]);
  if (said == "ready\n")
    return {};
  if (timed_out)
    return "it was still loading the model after " +
           std::to_string(patience.count()) + " s (see " + log + ")";
  if (said.empty())
    return "it didn't start (see " + log + ")";
  return said.substr(0, said.find('\n'));
}

// libc++ counts file times in __int128, which std::to_string doesn't take.
auto write_time(const std::filesystem::path &p_file) -> std::string {
  std::error_code ignored;
  return std::to_string(static_cast<long long>(
      std::filesystem::last_write_time(p_file, ignored)
          .time_since_epoch()
          .count()));
}

} // namespace

auto daemon_socket_path(const builtin_options &p_options,
                        const std::filesystem::path &p_executable)
    -> std::optional<std::filesystem::path> {
  std::uint64_t hash = 14695981039346656037ULL;
  std::error_code ignored;
  const auto executable = std::filesystem::weakly_canonical(p_executable, ignored);
  mix(hash, executable.string());
  mix(hash, std::to_string(std::filesystem::file_size(executable, ignored)));
  mix(hash, write_time(executable));
  const auto &location = p_options.location;
  mix(hash, std::filesystem::weakly_canonical(location.file, ignored).string());
  mix(hash, std::to_string(location.offset));
  mix(hash, std::to_string(location.size));
  mix(hash, write_time(location.file));
  mix(hash, p_options.use_gpu ? "gpu" : "cpu");
  mix(hash, std::to_string(p_options.threads));
  mix(hash, std::to_string(p_options.context_size));
  mix(hash, std::to_string(p_options.max_tokens));
  mix(hash, p_options.cache_prompts ? "cache" : "no cache");

  // Socket paths must be short (about 100 bytes), so not the cache
  // directory.
  std::optional<std::filesystem::path> directory;
  if (const char *const runtime = std::getenv("XDG_RUNTIME_DIR");
      runtime != nullptr && *runtime != '\0')
    directory = private_directory(std::filesystem::path(runtime) / "ovis");
  if (!directory)
    directory = private_directory("/tmp/ovis-" + std::to_string(::getuid()));
  if (!directory)
    return std::nullopt;
  char name[40];
  std::snprintf(name, sizeof(name), "model-%016llx.sock",
                static_cast<unsigned long long>(hash));
  auto socket = *directory / name;
  if (!unix_address(socket))
    return std::nullopt;
  return socket;
}

auto serve_model(language_model &p_model, const std::filesystem::path &p_socket,
                 std::chrono::seconds p_idle,
                 const std::function<void()> &p_ready) -> void {
  const auto address = unix_address(p_socket);
  if (!address)
    throw error("The socket path " + p_socket.string() + " is too long.");
  const int listener = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (listener < 0)
    throw error("Cannot make a socket for the model daemon.");
  ::unlink(p_socket.c_str());
  if (::bind(listener, reinterpret_cast<const sockaddr *>(&*address),
             sizeof(*address)) != 0 ||
      ::listen(listener, 16) != 0) {
    ::close(listener);
    throw error("Cannot listen on " + p_socket.string() + ".");
  }
  ::chmod(p_socket.c_str(), 0600);
  ::signal(SIGPIPE, SIG_IGN);
  if (p_ready)
    p_ready();

  bool stop = false;
  while (!stop) {
    pollfd waiting{listener, POLLIN, 0};
    const auto ready = ::poll(
        &waiting, 1,
        static_cast<int>(std::chrono::milliseconds(p_idle).count()));
    if (ready < 0 && errno == EINTR)
      continue;
    if (ready <= 0)
      break;
    const int connection = ::accept(listener, nullptr, nullptr);
    if (connection < 0)
      continue;
    no_sigpipe(connection);
    if (const auto request = receive_all(connection))
      send_all(connection, answer(p_model, *request, stop));
    ::close(connection);
  }
  ::close(listener);
  // A newer daemon may have taken the path over; only remove our own.
  struct stat status {};
  if (::stat(p_socket.c_str(), &status) == 0 && S_ISSOCK(status.st_mode))
    ::unlink(p_socket.c_str());
}

auto stop_daemon(const std::filesystem::path &p_socket) -> bool {
  string_type request;
  put(request, "stop");
  return ask_daemon(p_socket, request).has_value();
}

daemon_model::daemon_model(daemon_options p_options, string_type p_name,
                           string_type p_fingerprint)
    : options(std::move(p_options)), model_name(std::move(p_name)),
      model_fingerprint(std::move(p_fingerprint)) {}

daemon_model::~daemon_model() = default;

auto daemon_model::request(const string_type &p_request)
    -> std::optional<string_type> {
  if (local)
    return std::nullopt;
  auto reply = ask_daemon(options.socket, p_request);
  if (!reply && !started && !options.executable.empty()) {
    started = true;
    // One build starts the daemon; others wait for it, then connect.
    const auto lock_path = options.socket.string() + ".lock";
    const int lock = ::open(lock_path.c_str(), O_RDWR | O_CREAT, 0600);
    if (lock >= 0)
      while (::flock(lock, LOCK_EX) != 0 && errno == EINTR) {
      }
    reply = ask_daemon(options.socket, p_request);
    if (!reply) {
      if (options.log)
        options.log("Starting the model daemon (it keeps the model loaded "
                    "between builds).");
      const auto problem = start_daemon(options);
      if (problem.empty())
        reply = ask_daemon(options.socket, p_request);
      else
        fallback("the model daemon didn't start: " + problem);
    }
    if (lock >= 0)
      ::close(lock);
  }
  if (!reply)
    return std::nullopt;
  string_view_type rest = *reply;
  const auto status = take(rest);
  if (status == "error")
    throw error(take(rest));
  return string_type(rest);
}

#else

auto daemon_socket_path(const builtin_options &, const std::filesystem::path &)
    -> std::optional<std::filesystem::path> {
  return std::nullopt;
}

auto serve_model(language_model &, const std::filesystem::path &,
                 std::chrono::seconds, const std::function<void()> &) -> void {
  throw error("The model daemon needs Unix sockets.");
}

auto stop_daemon(const std::filesystem::path &) -> bool { return false; }

daemon_model::daemon_model(daemon_options p_options, string_type p_name,
                           string_type p_fingerprint)
    : options(std::move(p_options)), model_name(std::move(p_name)),
      model_fingerprint(std::move(p_fingerprint)) {}

daemon_model::~daemon_model() = default;

auto daemon_model::request(const string_type &) -> std::optional<string_type> {
  return std::nullopt;
}

#endif

auto daemon_model::fallback(const string_type &p_why) -> language_model & {
  if (!local) {
    if (!options.fallback)
      throw error("The model daemon isn't available: " + p_why + ".");
    if (options.log)
      options.log("Loading the model in this process: " + p_why + ".");
    local = options.fallback();
  }
  return *local;
}

auto daemon_model::chat(const std::vector<chat_message> &p_messages,
                        string_view_type p_grammar) -> string_type {
  string_type request_text;
  put(request_text, "chat");
  put_messages(request_text, p_messages);
  put(request_text, p_grammar);
  if (const auto reply = request(request_text)) {
    string_view_type rest = *reply;
    return take(rest);
  }
  return fallback("no model daemon answered").chat(p_messages, p_grammar);
}

auto daemon_model::choose(const std::vector<chat_message> &p_messages,
                          std::size_t p_options) -> std::vector<double> {
  string_type request_text;
  put(request_text, "choose");
  put_messages(request_text, p_messages);
  put(request_text, std::to_string(p_options));
  if (const auto reply = request(request_text)) {
    string_view_type rest = *reply;
    return take_values<double>(rest);
  }
  return fallback("no model daemon answered").choose(p_messages, p_options);
}

auto daemon_model::embed(const std::vector<string_type> &p_texts)
    -> std::vector<std::vector<float>> {
  string_type request_text;
  put(request_text, "embed");
  put(request_text, std::to_string(p_texts.size()));
  for (const auto &text : p_texts)
    put(request_text, text);
  if (const auto reply = request(request_text)) {
    string_view_type rest = *reply;
    std::vector<std::vector<float>> vectors(std::stoull(take(rest)));
    for (auto &vector : vectors)
      vector = take_values<float>(rest);
    return vectors;
  }
  return fallback("no model daemon answered").embed(p_texts);
}

} // namespace ovis::english
