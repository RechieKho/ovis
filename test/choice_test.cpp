#include <cmath>
#include <cstdlib>
#include <deque>
#include <numeric>
#include <thread>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <ovis/english/language_model.hpp>

namespace {

using ovis::english::chat_message;

// Answers `chat` from a queue and remembers the grammars it was given.
class queued_model final : public ovis::english::language_model {
public:
  std::deque<std::string> replies;
  std::vector<std::string> grammars;

  auto name() const -> std::string override { return "queued"; }
  auto chat(const std::vector<chat_message> &, std::string_view p_grammar)
      -> std::string override {
    grammars.emplace_back(p_grammar);
    REQUIRE(!replies.empty());
    auto reply = replies.front();
    replies.pop_front();
    return reply;
  }
};

auto question() -> std::vector<chat_message> {
  return {{"system", "Answer with the number of the most likely reading."},
          {"user", "Line: average = mean of numbers\n"
                   "\"mean of numbers\" most likely means:\n"
                   "1. average of numbers\n2. smallest of numbers\n"
                   "3. largest of numbers\nAnswer:"}};
}

auto total(const std::vector<double> &p_probabilities) -> double {
  return std::accumulate(p_probabilities.begin(), p_probabilities.end(), 0.0);
}

// An OpenAI-compatible server on a free local port, answering every
// `/v1/chat/completions` request with `reply(request)`.
class fake_server {
  httplib::Server server;
  std::thread thread;

public:
  std::vector<nlohmann::json> requests;
  std::function<nlohmann::json(const nlohmann::json &)> reply;
  int port = 0;

  fake_server() {
    server.Post("/v1/chat/completions",
                [this](const httplib::Request &p_request,
                       httplib::Response &p_response) {
                  const auto request = nlohmann::json::parse(p_request.body);
                  requests.push_back(request);
                  p_response.set_content(reply(request).dump(),
                                         "application/json");
                });
    port = server.bind_to_any_port("127.0.0.1");
    thread = std::thread([this] { server.listen_after_bind(); });
    server.wait_until_ready();
  }
  ~fake_server() {
    server.stop();
    thread.join();
  }

  auto model() const -> ovis::english::endpoint_model {
    return ovis::english::endpoint_model(
        {.endpoint = "http://127.0.0.1:" + std::to_string(port) + "/v1"});
  }
};

auto message_reply(const std::string &p_content) -> nlohmann::json {
  return {{"choices", {{{"message", {{"content", p_content}}}}}}};
}

} // namespace

TEST_CASE("Turn scores into probabilities.", "[choice]") {
  const auto probabilities = ovis::english::softmax({0.0, std::log(3.0)});
  REQUIRE(probabilities[0] == Catch::Approx(0.25));
  REQUIRE(probabilities[1] == Catch::Approx(0.75));
  // Large scores must not overflow.
  REQUIRE(total(ovis::english::softmax({1000.0, 999.0})) == Catch::Approx(1));
  REQUIRE(ovis::english::softmax({-INFINITY, 2.0})[0] == 0);
  REQUIRE_THROWS_AS(ovis::english::softmax({-INFINITY}), ovis::error);

  REQUIRE(ovis::english::choice_grammar(3) == "root ::= [1-3]\n");
  REQUIRE_THROWS_AS(ovis::english::choice_grammar(0), ovis::error);
  REQUIRE_THROWS_AS(
      ovis::english::choice_grammar(ovis::english::max_choice_options + 1),
      ovis::error);
}

TEST_CASE("Choose with a model that only chats.", "[choice]") {
  queued_model model;
  model.replies = {"2", "7"};
  REQUIRE(model.choose(question(), 3) == std::vector<double>{0, 1, 0});
  REQUIRE(model.grammars.back() == "root ::= [1-3]\n");
  // A reply outside the options is the model's fault, reported as such.
  REQUIRE_THROWS_AS(model.choose(question(), 3), ovis::error);
}

TEST_CASE("Choose with a server's logprobs.", "[choice]") {
  fake_server server;
  server.reply = [](const nlohmann::json &) {
    const auto logprob = [](std::string p_token, double p_value) {
      return nlohmann::json{{"token", p_token}, {"logprob", p_value}};
    };
    return nlohmann::json{
        {"choices",
         {{{"message", {{"content", "1"}}},
           {"logprobs",
            {{"content",
              {{{"token", "1"},
                {"logprob", std::log(0.5)},
                // Other tokens, an option out of range and a spaced digit,
                // which counts as its digit.
                {"top_logprobs",
                 {logprob("1", std::log(0.5)), logprob("The", std::log(0.2)),
                  logprob(" 3", std::log(0.1)),
                  logprob("9", std::log(0.1))}}}}}}}}}}};
  };
  auto model = server.model();
  const auto probabilities = model.choose(question(), 3);
  REQUIRE(probabilities[0] == Catch::Approx(0.5 / 0.6));
  REQUIRE(probabilities[1] == 0);
  REQUIRE(probabilities[2] == Catch::Approx(0.1 / 0.6));

  const auto &request = server.requests.back();
  REQUIRE(request["max_tokens"] == 1);
  REQUIRE(request["logprobs"] == true);
  REQUIRE(!request.contains("grammar"));
}

TEST_CASE("Choose with a server that gives no logprobs.", "[choice]") {
  fake_server server;
  server.reply = [](const nlohmann::json &p_request) {
    // The fallback asks for an option under a grammar.
    return message_reply(p_request.contains("grammar") ? "3" : "Hello");
  };
  auto model = server.model();
  REQUIRE(model.choose(question(), 3) == std::vector<double>{0, 0, 1});
  REQUIRE(server.requests.size() == 2);
  REQUIRE(server.requests.back()["grammar"] == "root ::= [1-3]\n");
}

TEST_CASE("The built-in model chooses the same way every time.",
          "[model][choice]") {
  const auto *const path = std::getenv("OVIS_TEST_MODEL");
  if (path == nullptr || !ovis::english::has_builtin_model_support())
    SKIP("Set OVIS_TEST_MODEL to a GGUF file to run this test.");

  const auto options = ovis::english::builtin_options{
      .location = *ovis::english::find_builtin_model(std::string(path), ".")};
  ovis::english::builtin_model first(options);
  const auto probabilities = first.choose(question(), 3);
  REQUIRE(probabilities.size() == 3);
  REQUIRE(total(probabilities) == Catch::Approx(1));

  // Another question with the same system message reuses its batch, which
  // must not change the answer; nor may a fresh instance or a chat.
  auto other = question();
  other[1].content = "Line: show it\n\"it\" most likely means:\n1. total\n"
                     "2. numbers\nAnswer:";
  const auto other_probabilities = first.choose(other, 2);
  REQUIRE(first.choose(question(), 3) == probabilities);
  first.chat({{"user", "Say hi."}}, "root ::= \"hi\"\n");
  REQUIRE(first.choose(question(), 3) == probabilities);
  ovis::english::builtin_model second(options);
  REQUIRE(second.choose(question(), 3) == probabilities);
  REQUIRE(second.choose(other, 2) == other_probabilities);
}
