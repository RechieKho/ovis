#ifndef OVIS_ENGLISH_MODEL_DAEMON_HPP
#define OVIS_ENGLISH_MODEL_DAEMON_HPP

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

#include <ovis/english/language_model.hpp>

// Keeps the built-in model loaded between builds. Loading takes seconds
// (5 s for 3B) and a build asks a question or two, so `ovis` starts a copy
// of itself in the background (`ovis model serve`) that holds the model and
// answers over a local socket, and quits after a while unused. Unix only;
// elsewhere, and whenever the daemon can't be reached, the model loads in
// the build's own process as before.
namespace ovis::english {

// Where the daemon for these options listens: the name hashes everything
// its answers depend on (ovis itself, the weights, the device, the
// threads), so different settings never share a daemon. Nothing when the
// directory for it can't be made private to this user.
auto daemon_socket_path(const builtin_options &p_options,
                        const std::filesystem::path &p_executable)
    -> std::optional<std::filesystem::path>;

// Answers requests for `p_model` on `p_socket`, one at a time, until a
// `stop` request or `p_idle` without one. `p_ready`, once listening.
// Throws `error` if it can't listen.
auto serve_model(language_model &p_model, const std::filesystem::path &p_socket,
                 std::chrono::seconds p_idle,
                 const std::function<void()> &p_ready = {}) -> void;

// Asks the daemon at `p_socket` to quit. False if none answered.
auto stop_daemon(const std::filesystem::path &p_socket) -> bool;

struct daemon_options {
  std::filesystem::path socket;
  // Starts the daemon when none answers: runs `executable model serve` with
  // `serve_arguments`, and waits for it to say it is ready. Empty: only
  // connects (for tests).
  std::filesystem::path executable;
  std::vector<string_type> serve_arguments;
  // What to use when the daemon can't be started or reached: the model
  // loaded in this process.
  std::function<std::unique_ptr<language_model>()> fallback;
  std::function<void(string_view_type)> log;
};

// A model answered by the daemon, started on first use.
class daemon_model final : public language_model {
  daemon_options options;
  string_type model_name;
  string_type model_fingerprint;
  std::unique_ptr<language_model> local;
  bool started = false;

  // The model to use once the daemon has failed: `local`, made on demand.
  auto fallback(const string_type &p_why) -> language_model &;
  // Sends a request and returns the reply's payload; nothing if the daemon
  // can't be reached. Throws `error` when the model itself failed.
  auto request(const string_type &p_request) -> std::optional<string_type>;

public:
  // `p_name` and `p_fingerprint` describe the weights without loading them.
  daemon_model(daemon_options p_options, string_type p_name,
               string_type p_fingerprint);
  ~daemon_model() override;

  auto name() const -> string_type override { return model_name; }
  auto fingerprint() -> string_type override { return model_fingerprint; }
  auto chat(const std::vector<chat_message> &p_messages,
            string_view_type p_grammar) -> string_type override;
  auto choose(const std::vector<chat_message> &p_messages,
              std::size_t p_options) -> std::vector<double> override;
  auto embed(const std::vector<string_type> &p_texts)
      -> std::vector<std::vector<float>> override;
};

} // namespace ovis::english

#endif // OVIS_ENGLISH_MODEL_DAEMON_HPP
