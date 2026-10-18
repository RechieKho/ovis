#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <thread>

#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

#include <ovis/english/language_model.hpp>

#ifdef OVIS_HAS_BUILTIN_LLM
#include <llama.h>
#endif

namespace ovis::english {

namespace {

// Trailer after weights appended to an executable: offset, size, magic.
constexpr std::array<char_type, 16> embedded_magic = {
    'O', 'V', 'I', 'S', '-', 'E', 'M', 'B', 'E', 'D', 'D', 'E', 'D', '-', '1', '\0'};
constexpr std::uint64_t trailer_size = 8 + 8 + embedded_magic.size();
// Keeps the GGUF data aligned so llama.cpp can map it straight from the file.
constexpr std::uint64_t embedded_alignment = 4096;

auto write_u64(std::ostream &p_output, std::uint64_t p_value) -> void {
  for (int byte = 0; byte < 8; ++byte)
    p_output.put(static_cast<char_type>((p_value >> (8 * byte)) & 0xff));
}

auto read_u64(const char_type *p_bytes) -> std::uint64_t {
  std::uint64_t result = 0;
  for (int byte = 7; byte >= 0; --byte)
    result = (result << 8) | static_cast<unsigned char>(p_bytes[byte]);
  return result;
}

auto find_embedded(const std::filesystem::path &p_executable)
    -> std::optional<model_location> {
  std::ifstream input(p_executable, std::ios::binary | std::ios::ate);
  if (!input)
    return std::nullopt;
  const auto file_size = static_cast<std::uint64_t>(input.tellg());
  if (file_size < trailer_size)
    return std::nullopt;
  std::array<char_type, trailer_size> trailer{};
  input.seekg(static_cast<std::streamoff>(file_size - trailer_size));
  input.read(trailer.data(), trailer.size());
  if (!input || std::memcmp(trailer.data() + 16, embedded_magic.data(),
                            embedded_magic.size()) != 0)
    return std::nullopt;
  const auto offset = read_u64(trailer.data());
  const auto size = read_u64(trailer.data() + 8);
  if (offset + size + trailer_size > file_size)
    return std::nullopt;
  return model_location{p_executable, offset, size, true};
}

auto environment_path(const char_type *p_name)
    -> std::optional<std::filesystem::path> {
  const auto *const value = std::getenv(p_name);
  if (value == nullptr || *value == '\0')
    return std::nullopt;
  return std::filesystem::path(value);
}

auto file_location(const std::filesystem::path &p_path)
    -> std::optional<model_location> {
  std::error_code code;
  if (!std::filesystem::is_regular_file(p_path, code))
    return std::nullopt;
  return model_location{p_path, 0, std::filesystem::file_size(p_path), false};
}

#ifdef OVIS_HAS_BUILTIN_LLM

auto quiet_log(ggml_log_level, const char *, void *) -> void {}

auto verbose_log(ggml_log_level, const char *p_text, void *) -> void {
  std::fputs(p_text, stderr);
}

#endif

} // namespace

auto default_model_file_name() -> string_view_type {
  return "qwen2.5-coder-3b-instruct-q4_k_m.gguf";
}

auto default_model_url() -> string_view_type {
  return "https://huggingface.co/Qwen/Qwen2.5-Coder-3B-Instruct-GGUF/"
         "resolve/main/qwen2.5-coder-3b-instruct-q4_k_m.gguf";
}

auto model_cache_directory() -> std::filesystem::path {
  if (const auto cache = environment_path("XDG_CACHE_HOME"))
    return *cache / "ovis" / "models";
  if (const auto home = environment_path("HOME"))
    return *home / ".cache" / "ovis" / "models";
  return std::filesystem::temp_directory_path() / "ovis" / "models";
}

auto find_builtin_model(const std::optional<std::filesystem::path> &p_explicit,
                        const std::filesystem::path &p_executable)
    -> std::optional<model_location> {
  if (p_explicit) {
    if (auto found = file_location(*p_explicit))
      return found;
    throw error("No model file at " + p_explicit->string() + ".");
  }
  if (const auto from_environment = environment_path("OVIS_MODEL_PATH")) {
    if (auto found = file_location(*from_environment))
      return found;
    throw error("No model file at " + from_environment->string() +
                " (from OVIS_MODEL_PATH).");
  }
  if (auto embedded = find_embedded(p_executable))
    return embedded;
  const auto directory = p_executable.parent_path();
  for (const auto &candidate :
       {directory / default_model_file_name(),
        directory / ".." / "share" / "ovis" / default_model_file_name(),
        model_cache_directory() / default_model_file_name()})
    if (auto found = file_location(candidate))
      return found;
  return std::nullopt;
}

auto embed_model(const std::filesystem::path &p_executable,
                 const std::filesystem::path &p_model,
                 const std::filesystem::path &p_output) -> void {
  std::ifstream executable(p_executable, std::ios::binary);
  std::ifstream model(p_model, std::ios::binary);
  if (!executable || !model)
    throw error("Cannot read " +
                (executable ? p_model : p_executable).string() + ".");

  // Re-embedding replaces earlier weights instead of stacking them.
  auto executable_size = std::filesystem::file_size(p_executable);
  if (const auto existing = find_embedded(p_executable))
    executable_size = existing->offset;

  auto temporary = p_output;
  temporary += ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output)
      throw error("Cannot write " + temporary.string() + ".");
    std::vector<char_type> chunk(1 << 20);
    auto remaining = executable_size;
    while (remaining > 0) {
      const auto count = std::min<std::uint64_t>(remaining, chunk.size());
      executable.read(chunk.data(), static_cast<std::streamsize>(count));
      output.write(chunk.data(), static_cast<std::streamsize>(count));
      remaining -= count;
    }
    const auto offset = (executable_size + embedded_alignment - 1) /
                        embedded_alignment * embedded_alignment;
    for (auto padding = executable_size; padding < offset; ++padding)
      output.put('\0');
    output << model.rdbuf();
    const auto size = std::filesystem::file_size(p_model);
    write_u64(output, offset);
    write_u64(output, size);
    output.write(embedded_magic.data(), embedded_magic.size());
    if (!output)
      throw error("Cannot write " + temporary.string() + ".");
  }
  std::filesystem::permissions(temporary,
                               std::filesystem::status(p_executable).permissions());
  // Renaming works even when replacing the running executable.
  std::filesystem::rename(temporary, p_output);
}

#ifdef OVIS_HAS_BUILTIN_LLM

auto has_builtin_model_support() -> bool { return true; }

struct builtin_model::state {
  FILE *file = nullptr;
  llama_model *model = nullptr;
  llama_context *context = nullptr;
  const llama_vocab *vocabulary = nullptr;
  string_type chat_template;
  // Prompt tokens decoded in full batches, to reuse a shared prompt prefix.
  // Generated tokens are never reused: they were decoded one at a time.
  std::vector<llama_token> cached;
  std::uint32_t batch_size = 512;

  ~state() {
    if (context != nullptr)
      llama_free(context);
    if (model != nullptr)
      llama_model_free(model);
    if (file != nullptr)
      std::fclose(file);
  }
};

builtin_model::builtin_model(builtin_options p_options)
    : options(std::move(p_options)) {}

builtin_model::~builtin_model() = default;

auto builtin_model::name() const -> string_type {
  return options.location.is_embedded
             ? "embedded (" + std::to_string(options.location.size) + " bytes)"
             : options.location.file.filename().string();
}

// Size plus a hash of the first and last MiB: cheap, and enough to tell
// different weights apart.
auto builtin_model::fingerprint() -> string_type {
  const auto &location = options.location;
  std::ifstream input(location.file, std::ios::binary);
  std::uint64_t hash = 14695981039346656037ULL;
  const auto mix = [&](std::uint64_t p_from) {
    std::vector<char_type> chunk(1 << 20);
    input.clear();
    input.seekg(static_cast<std::streamoff>(location.offset + p_from));
    input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    for (std::streamsize index = 0; index < input.gcount(); ++index)
      hash = (hash ^ static_cast<unsigned char>(chunk[index])) *
             1099511628211ULL;
  };
  mix(0);
  if (location.size > (1 << 20))
    mix(location.size - (1 << 20));
  char_type text[17];
  std::snprintf(text, sizeof(text), "%016llx",
                static_cast<unsigned long long>(hash));
  return "size " + std::to_string(location.size) + ", fnv " + text;
}

namespace {

// The first GPU llama.cpp can use, or nothing.
auto gpu_description() -> string_type {
  for (std::size_t index = 0; index < ggml_backend_dev_count(); ++index) {
    auto *const device = ggml_backend_dev_get(index);
    const auto type = ggml_backend_dev_type(device);
    if (type == GGML_BACKEND_DEVICE_TYPE_GPU ||
        type == GGML_BACKEND_DEVICE_TYPE_IGPU)
      return string_type("the GPU (") + ggml_backend_dev_description(device) +
             ")";
  }
  return {};
}

// Performance cores only: threads on efficiency cores hold every step back
// to their pace.
auto default_threads() -> int {
#ifdef __APPLE__
  int cores = 0;
  std::size_t size = sizeof(cores);
  if (sysctlbyname("hw.perflevel0.physicalcpu", &cores, &size, nullptr, 0) ==
          0 &&
      cores > 0)
    return cores;
#endif
  return static_cast<int>(std::max(std::thread::hardware_concurrency(), 1U));
}

} // namespace

auto builtin_model::load() -> state & {
  if (!loaded) {
    llama_log_set(options.verbose ? verbose_log : quiet_log, nullptr);
    llama_backend_init();
    auto state_box = std::make_unique<state>();

    auto model_parameters = llama_model_default_params();
    // The lockfile, not the device, keeps builds reproducible: a GPU may
    // break a near-tie differently from the CPU, but only paragraphs that
    // changed are translated again.
    const auto gpu = options.use_gpu ? gpu_description() : string_type();
    model_parameters.n_gpu_layers = gpu.empty() ? 0 : -1;
    const auto &location = options.location;
    if (location.is_embedded) {
      state_box->file = std::fopen(location.file.string().c_str(), "rb");
      if (state_box->file == nullptr ||
          std::fseek(state_box->file, static_cast<long>(location.offset),
                     SEEK_SET) != 0)
        throw error("Cannot read the model embedded in " +
                    location.file.string() + ".");
      state_box->model =
          llama_model_load_from_file_ptr(state_box->file, model_parameters);
    } else {
      state_box->model = llama_model_load_from_file(
          location.file.string().c_str(), model_parameters);
    }
    if (state_box->model == nullptr)
      throw error("Cannot load the model " + name() + ".");

    const auto threads =
        options.threads > 0 ? options.threads : default_threads();
    auto context_parameters = llama_context_default_params();
    context_parameters.n_ctx = static_cast<std::uint32_t>(options.context_size);
    context_parameters.n_batch = state_box->batch_size;
    context_parameters.n_ubatch = state_box->batch_size;
    context_parameters.n_threads = threads;
    context_parameters.n_threads_batch = threads;
    context_parameters.no_perf = true;
    state_box->context =
        llama_init_from_model(state_box->model, context_parameters);
    if (state_box->context == nullptr)
      throw error("Cannot create a context for the model " + name() + ".");
    state_box->vocabulary = llama_model_get_vocab(state_box->model);
    if (const auto *const chat_template =
            llama_model_chat_template(state_box->model, nullptr))
      state_box->chat_template = chat_template;
    loaded = std::move(state_box);
    if (options.log)
      options.log("Loaded the model " + name() + " on " +
                  (gpu.empty() ? "the CPU with " + std::to_string(threads) +
                                     " threads"
                               : gpu) +
                  ".");
  }
  return *loaded;
}

auto builtin_model::read_prompt(const std::vector<chat_message> &p_messages,
                                std::size_t p_reply_tokens) -> state & {
  auto &current = load();

  // A prompt in the model's own chat format (ChatML if it has none that
  // llama.cpp knows).
  const auto *const chat_template =
      current.chat_template.empty() ? "chatml" : current.chat_template.c_str();
  const auto format = [&](std::size_t p_count, bool p_reply) {
    std::vector<llama_chat_message> messages;
    for (std::size_t index = 0; index < p_count; ++index)
      messages.push_back({p_messages[index].role.c_str(),
                          p_messages[index].content.c_str()});
    string_type prompt;
    const auto length = llama_chat_apply_template(
        chat_template, messages.data(), messages.size(), p_reply, nullptr, 0);
    if (length < 0) {
      for (std::size_t index = 0; index < p_count; ++index)
        prompt += "<|im_start|>" + p_messages[index].role + "\n" +
                  p_messages[index].content + "<|im_end|>\n";
      if (p_reply)
        prompt += "<|im_start|>assistant\n";
    } else {
      prompt.resize(static_cast<std::size_t>(length));
      llama_chat_apply_template(chat_template, messages.data(), messages.size(),
                                p_reply, prompt.data(), length);
    }
    return prompt;
  };
  const auto tokenize = [&](const string_type &p_text) {
    std::vector<llama_token> tokens(p_text.size() + 16);
    const auto count = llama_tokenize(
        current.vocabulary, p_text.data(), static_cast<int32_t>(p_text.size()),
        tokens.data(), static_cast<int32_t>(tokens.size()), true, true);
    if (count < 0)
      throw error("Cannot tokenize the prompt.");
    tokens.resize(static_cast<std::size_t>(count));
    return tokens;
  };

  const auto prompt = format(p_messages.size(), true);
  auto tokens = tokenize(prompt);
  if (tokens.size() + p_reply_tokens >
      static_cast<std::size_t>(options.context_size))
    throw error("The paragraph and its context are too long for the model's "
                "context of " + std::to_string(options.context_size) +
                " tokens; split the paragraph.");

  // Batches start only at fixed places: multiples of the batch size, and the
  // end of the system message. A reused batch then holds exactly what
  // computing it again would give, so the reply doesn't depend on what was
  // asked before. The second place lets requests sharing a system message
  // shorter than a batch (every question of the phrase front end) reuse it.
  std::vector<std::size_t> starts;
  for (std::size_t start = 0; start < tokens.size();
       start += current.batch_size)
    starts.push_back(start);
  if (p_messages.size() > 1 && p_messages.front().role == "system") {
    const auto system = format(1, false);
    const auto system_tokens = tokenize(system);
    if (prompt.starts_with(system) && system_tokens.size() < tokens.size() &&
        std::equal(system_tokens.begin(), system_tokens.end(), tokens.begin()))
      starts.push_back(system_tokens.size());
  }
  std::ranges::sort(starts);
  const auto [last, end] = std::ranges::unique(starts);
  starts.erase(last, end);

  // Reuse up to the last start the previous prompt shares, keeping at least
  // one token to decode, since its logits are the ones needed.
  std::size_t common = 0;
  while (common < tokens.size() && common < current.cached.size() &&
         tokens[common] == current.cached[common])
    ++common;
  std::size_t shared = 0;
  for (const auto start : starts)
    if (start <= common && start < tokens.size())
      shared = start;
  auto *const memory = llama_get_memory(current.context);
  llama_memory_seq_rm(memory, 0, static_cast<llama_pos>(shared), -1);
  current.cached.assign(tokens.begin(),
                        tokens.begin() + static_cast<std::ptrdiff_t>(shared));

  for (auto start = std::ranges::find(starts, shared); start != starts.end();
       ++start) {
    const auto begin = *start;
    const auto finish = std::next(start) == starts.end() ? tokens.size()
                                                         : *std::next(start);
    if (llama_decode(current.context,
                     llama_batch_get_one(tokens.data() + begin,
                                         static_cast<int32_t>(finish - begin))) != 0)
      throw error("The model failed to read the prompt.");
    current.cached.insert(current.cached.end(), tokens.begin() + begin,
                          tokens.begin() + finish);
  }
  return current;
}

auto builtin_model::chat(const std::vector<chat_message> &p_messages,
                         string_view_type p_grammar) -> string_type {
  auto &current =
      read_prompt(p_messages, static_cast<std::size_t>(options.max_tokens));

  llama_sampler *grammar = nullptr;
  if (!p_grammar.empty()) {
    const string_type text(p_grammar);
    grammar = llama_sampler_init_grammar(current.vocabulary, text.c_str(),
                                         "root");
    if (grammar == nullptr)
      throw error("Internal compiler error: the translation grammar is "
                  "invalid.");
  }

  const auto vocabulary_size = llama_vocab_n_tokens(current.vocabulary);
  std::vector<llama_token_data> candidates(
      static_cast<std::size_t>(vocabulary_size));
  // Greedy decoding under the grammar: the most likely token the grammar
  // allows. Checking only the top token first gives the same answer as
  // checking the whole vocabulary, which is only needed when it is refused.
  const auto next_token = [&]() -> llama_token {
    const auto *const logits = llama_get_logits_ith(current.context, -1);
    const auto *const best = std::max_element(logits, logits + vocabulary_size);
    const auto token = static_cast<llama_token>(best - logits);
    if (grammar == nullptr)
      return token;
    llama_token_data top{token, *best, 0};
    llama_token_data_array single{&top, 1, -1, false};
    llama_sampler_apply(grammar, &single);
    if (top.logit != -INFINITY)
      return token;
    for (llama_token index = 0; index < vocabulary_size; ++index)
      candidates[static_cast<std::size_t>(index)] = {index, logits[index], 0};
    llama_token_data_array all{candidates.data(), candidates.size(), -1, false};
    llama_sampler_apply(grammar, &all);
    const auto allowed = std::max_element(
        candidates.begin(), candidates.end(),
        [](const auto &p_left, const auto &p_right) {
          return p_left.logit < p_right.logit;
        });
    if (allowed->logit == -INFINITY)
      throw error("the grammar allows no further token");
    return allowed->id;
  };

  string_type reply;
  try {
    for (int generated = 0; generated < options.max_tokens; ++generated) {
      auto token = next_token();
      if (grammar != nullptr)
        llama_sampler_accept(grammar, token);
      if (llama_vocab_is_eog(current.vocabulary, token))
        break;
      char_type piece[256];
      const auto piece_length = llama_token_to_piece(
          current.vocabulary, token, piece, sizeof(piece), 0, false);
      if (piece_length > 0)
        reply.append(piece, static_cast<std::size_t>(piece_length));
      if (llama_decode(current.context, llama_batch_get_one(&token, 1)) != 0)
        throw error("The model failed while writing its reply.");
    }
  } catch (const std::exception &p_error) {
    if (grammar != nullptr)
      llama_sampler_free(grammar);
    throw error(string_type("The model stopped: ") + p_error.what());
  }
  if (grammar != nullptr)
    llama_sampler_free(grammar);
  return reply;
}

auto builtin_model::choose(const std::vector<chat_message> &p_messages,
                           std::size_t p_options) -> std::vector<double> {
  choice_grammar(p_options); // checks the number of options
  auto &current = read_prompt(p_messages, 1);
  const auto *const logits = llama_get_logits_ith(current.context, -1);
  std::vector<double> scores;
  for (std::size_t option = 1; option <= p_options; ++option) {
    const auto digit = std::to_string(option);
    llama_token token = 0;
    if (llama_tokenize(current.vocabulary, digit.data(),
                       static_cast<int32_t>(digit.size()), &token, 1, false,
                       false) != 1)
      throw error("The model " + name() + " has no single token for the "
                  "option " + digit + ".");
    scores.push_back(logits[token]);
  }
  return softmax(scores);
}

#else

auto has_builtin_model_support() -> bool { return false; }

struct builtin_model::state {};

builtin_model::builtin_model(builtin_options p_options)
    : options(std::move(p_options)) {}

builtin_model::~builtin_model() = default;

auto builtin_model::name() const -> string_type { return "unavailable"; }

auto builtin_model::fingerprint() -> string_type { return {}; }

auto builtin_model::chat(const std::vector<chat_message> &, string_view_type)
    -> string_type {
  throw error("This build of Ovis has no built-in model (it was configured "
              "with -DOVIS_BUILTIN_LLM=OFF). Use --endpoint or --offline.");
}

auto builtin_model::choose(const std::vector<chat_message> &p_messages,
                           std::size_t p_options) -> std::vector<double> {
  return language_model::choose(p_messages, p_options);
}

#endif

} // namespace ovis::english
