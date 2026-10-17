#ifndef OVIS_ENGLISH_LANGUAGE_MODEL_HPP
#define OVIS_ENGLISH_LANGUAGE_MODEL_HPP

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
  // Returns the assistant's reply. Throws `error`.
  virtual auto chat(const std::vector<chat_message> &p_messages)
      -> string_type = 0;
};

struct endpoint_options {
  // Base URL of an OpenAI-compatible API, e.g. llama.cpp's `llama-server`
  // (http://127.0.0.1:8080/v1) or Ollama (http://127.0.0.1:11434/v1).
  string_type endpoint = "http://127.0.0.1:8080/v1";
  string_type model = "local";
  string_type api_key;
  // Sends a GBNF grammar (llama.cpp extension) so the model can only produce
  // well-formed S-expressions. Servers that don't know it ignore it.
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
  auto chat(const std::vector<chat_message> &p_messages)
      -> string_type override;
};

} // namespace ovis::english

#endif // OVIS_ENGLISH_LANGUAGE_MODEL_HPP
