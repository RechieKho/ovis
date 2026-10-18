#ifndef OVIS_ENGLISH_LANGUAGE_MODEL_HPP
#define OVIS_ENGLISH_LANGUAGE_MODEL_HPP

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <ovis/def.hpp>

namespace ovis::english {

struct chat_message {
  string_type role; // "system", "user" or "assistant"
  string_type content;
};

class language_model {
public:
  virtual ~language_model() = default;
  virtual auto name() const -> string_type = 0;
  // Identifies the exact weights, if known; recorded in the lockfile.
  virtual auto fingerprint() -> string_type { return {}; }
  // Returns the assistant's reply. When `p_grammar` (GBNF) is given, the
  // reply must follow it. Throws `error`.
  virtual auto chat(const std::vector<chat_message> &p_messages,
                    string_view_type p_grammar) -> string_type = 0;
  // How likely the reply is to start with each of the options `1` to
  // `p_options` (at most `max_choice_options`): one probability per option,
  // summing to 1. Nothing is generated or sampled, so it repeats exactly.
  // This default asks `chat` for one option under a grammar and is certain
  // of it; models that see their own probabilities override it. Throws
  // `error`.
  virtual auto choose(const std::vector<chat_message> &p_messages,
                      std::size_t p_options) -> std::vector<double>;
};

// One digit per option, so every option is a single token.
inline constexpr std::size_t max_choice_options = 8;

// The grammar of a reply that is one of the options `1` to `p_options`.
auto choice_grammar(std::size_t p_options) -> string_type;

// Probabilities from log-scores: exp, normalised to sum to 1. A score of
// -infinity gets 0. Throws `error` when every score is -infinity.
auto softmax(const std::vector<double> &p_scores) -> std::vector<double>;

struct endpoint_options {
  // Base URL of an OpenAI-compatible API, e.g. llama.cpp's `llama-server`
  // (http://127.0.0.1:8080/v1) or Ollama (http://127.0.0.1:11434/v1).
  string_type endpoint = "http://127.0.0.1:8080/v1";
  string_type model = "local";
  string_type api_key;
  // Sends the grammar (a llama.cpp extension). Servers that don't know it
  // ignore it.
  bool use_grammar = true;
  int max_tokens = 1024;
  int timeout_seconds = 600;
};

// Talks to `/chat/completions` of an OpenAI-compatible server.
class endpoint_model final : public language_model {
  endpoint_options options;

public:
  explicit endpoint_model(endpoint_options p_options)
      : options(std::move(p_options)) {}

  auto name() const -> string_type override { return options.model; }
  auto chat(const std::vector<chat_message> &p_messages,
            string_view_type p_grammar) -> string_type override;
  // Uses the server's `logprobs` for the first token. Servers that give none
  // fall back to `language_model::choose`.
  auto choose(const std::vector<chat_message> &p_messages,
              std::size_t p_options) -> std::vector<double> override;

private:
  // Posts a request body to `/chat/completions` and returns the reply body.
  auto post(const string_type &p_request) -> string_type;
};

// Where the built-in model's weights are: a GGUF file, or a GGUF appended to
// a file (the `ovis` executable itself) at `offset`.
struct model_location {
  std::filesystem::path file;
  std::uint64_t offset = 0;
  std::uint64_t size = 0;
  bool is_embedded = false;
};

struct builtin_options {
  model_location location;
  // Runs every layer on the GPU when llama.cpp has one (Metal on a Mac,
  // CUDA, Vulkan), else on the CPU.
  bool use_gpu = true;
  // 0 picks the number of performance cores. Fixed for a machine, so
  // translations repeat exactly.
  int threads = 0;
  int context_size = 8192;
  int max_tokens = 1024;
  bool verbose = false;
  // Told where the model runs once it is loaded.
  std::function<void(string_view_type)> log;
};

// Runs a GGUF model in-process with llama.cpp: greedy decoding, so the same
// request with the same weights on the same machine and device gives the
// same reply. The model is loaded
// on first use, so builds that only need the lockfile never load it.
class builtin_model final : public language_model {
  struct state;
  builtin_options options;
  std::unique_ptr<state> loaded;

  auto load() -> state &;
  // Decodes the prompt of `p_messages`, reusing what the last prompt shares
  // with it, and leaves the logits of the next token in the context.
  auto read_prompt(const std::vector<chat_message> &p_messages,
                   std::size_t p_reply_tokens) -> state &;

public:
  explicit builtin_model(builtin_options p_options);
  ~builtin_model() override;

  auto name() const -> string_type override;
  auto fingerprint() -> string_type override;
  auto chat(const std::vector<chat_message> &p_messages,
            string_view_type p_grammar) -> string_type override;
  // A softmax over the logits of the tokens `1` to `p_options`, after one
  // decode of the prompt.
  auto choose(const std::vector<chat_message> &p_messages,
              std::size_t p_options) -> std::vector<double> override;
};

// Whether this build of Ovis contains llama.cpp.
auto has_builtin_model_support() -> bool;

auto default_model_file_name() -> string_view_type;
auto default_model_url() -> string_view_type;

// Directory `ovis model pull` downloads into: $XDG_CACHE_HOME/ovis/models or
// ~/.cache/ovis/models.
auto model_cache_directory() -> std::filesystem::path;

// Finds the built-in model: `p_explicit` (--model-path), $OVIS_MODEL_PATH,
// weights embedded in the executable, the default model next to the
// executable or in ../share/ovis, then the cache directory.
auto find_builtin_model(const std::optional<std::filesystem::path> &p_explicit,
                        const std::filesystem::path &p_executable)
    -> std::optional<model_location>;

// Writes a copy of `p_executable` with the GGUF at `p_model` appended, so the
// result needs no separate model file.
auto embed_model(const std::filesystem::path &p_executable,
                 const std::filesystem::path &p_model,
                 const std::filesystem::path &p_output) -> void;

} // namespace ovis::english

#endif // OVIS_ENGLISH_LANGUAGE_MODEL_HPP
