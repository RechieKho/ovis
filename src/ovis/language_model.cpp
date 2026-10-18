#include <algorithm>
#include <cmath>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <ovis/english/language_model.hpp>

namespace ovis::english {

auto choice_grammar(std::size_t p_options) -> string_type {
  if (p_options < 1 || p_options > max_choice_options)
    throw error("Internal compiler error: a choice needs 1 to " +
                std::to_string(max_choice_options) + " options.");
  return "root ::= [1-" + std::to_string(p_options) + "]\n";
}

auto softmax(const std::vector<double> &p_scores) -> std::vector<double> {
  const auto highest = std::ranges::max(p_scores);
  if (highest == -INFINITY)
    throw error("The model gave no option any probability.");
  std::vector<double> result;
  double total = 0;
  // Subtracting the highest score keeps exp from overflowing.
  for (const auto score : p_scores)
    total += result.emplace_back(std::exp(score - highest));
  for (auto &probability : result)
    probability /= total;
  return result;
}

auto language_model::choose(const std::vector<chat_message> &p_messages,
                            std::size_t p_options) -> std::vector<double> {
  const auto reply = chat(p_messages, choice_grammar(p_options));
  std::vector<double> result(p_options, 0);
  const auto option = reply.empty() ? 0 : reply.front() - '0';
  if (option < 1 || static_cast<std::size_t>(option) > p_options)
    throw error("The model answered \"" + reply + "\" instead of an option.");
  result[static_cast<std::size_t>(option - 1)] = 1;
  return result;
}

auto endpoint_model::post(const string_type &p_request) -> string_type {
  auto base = options.endpoint;
  while (!base.empty() && base.back() == '/')
    base.pop_back();
  const auto scheme_end = base.find("://");
  const auto path_begin =
      base.find('/', scheme_end == string_type::npos ? 0 : scheme_end + 3);
  const auto host = base.substr(0, path_begin);
  const auto path =
      (path_begin == string_type::npos ? string_type() : base.substr(path_begin)) +
      "/chat/completions";

  httplib::Client client(host);
  client.set_connection_timeout(10);
  client.set_read_timeout(options.timeout_seconds);
  httplib::Headers headers;
  if (!options.api_key.empty())
    headers.emplace("Authorization", "Bearer " + options.api_key);

  const auto response = client.Post(path, headers, p_request, "application/json");
  if (!response)
    throw error("Cannot reach the language model at " + options.endpoint +
                " (" + httplib::to_string(response.error()) +
                "). Start a local server such as `llama-server`, pass "
                "--endpoint, or use --offline to build from the lockfile "
                "only.");
  if (response->status != 200)
    throw error("The language model at " + options.endpoint +
                " answered HTTP " + std::to_string(response->status) + ": " +
                response->body.substr(0, 500));
  return response->body;
}

namespace {

auto chat_request(const endpoint_options &p_options,
                  const std::vector<chat_message> &p_messages, int p_max_tokens)
    -> nlohmann::json {
  nlohmann::json request = {
      {"model", p_options.model},
      {"temperature", 0},
      {"max_tokens", p_max_tokens},
      {"messages", nlohmann::json::array()},
  };
  for (const auto &message : p_messages)
    request["messages"].push_back(
        {{"role", message.role}, {"content", message.content}});
  return request;
}

} // namespace

auto endpoint_model::chat(const std::vector<chat_message> &p_messages,
                          string_view_type p_grammar) -> string_type {
  auto request = chat_request(options, p_messages, options.max_tokens);
  if (options.use_grammar && !p_grammar.empty())
    request["grammar"] = p_grammar;
  const auto body = post(request.dump());
  try {
    const auto reply = nlohmann::json::parse(body);
    return reply.at("choices").at(0).at("message").at("content");
  } catch (const nlohmann::json::exception &p_error) {
    throw error("Unexpected reply from the language model: " +
                string_type(p_error.what()));
  }
}

auto endpoint_model::choose(const std::vector<chat_message> &p_messages,
                            std::size_t p_options) -> std::vector<double> {
  choice_grammar(p_options); // checks the number of options
  // No grammar: it would change which tokens the server reports. The
  // options are found among the most likely first tokens instead.
  auto request = chat_request(options, p_messages, 1);
  request["logprobs"] = true;
  request["top_logprobs"] = 20;
  std::vector<double> scores(p_options, -INFINITY);
  try {
    const auto reply = nlohmann::json::parse(post(request.dump()));
    const auto &choice = reply.at("choices").at(0);
    if (choice.contains("logprobs") && choice["logprobs"].is_object())
      for (const auto &entry :
           choice["logprobs"].at("content").at(0).at("top_logprobs")) {
        auto token = entry.at("token").get<string_type>();
        std::erase_if(token, [](char_type p_character) {
          return std::isspace(static_cast<unsigned char>(p_character)) != 0;
        });
        if (token.size() == 1 && token[0] >= '1' &&
            static_cast<std::size_t>(token[0] - '0') <= p_options) {
          auto &score = scores[static_cast<std::size_t>(token[0] - '1')];
          score = std::max(score, entry.at("logprob").get<double>());
        }
      }
  } catch (const nlohmann::json::exception &) {
    // A reply without the expected logprobs: use the fallback below.
  }
  if (std::ranges::max(scores) == -INFINITY)
    return language_model::choose(p_messages, p_options);
  return softmax(scores);
}

} // namespace ovis::english
