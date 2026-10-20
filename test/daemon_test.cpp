#include <cstdlib>
#include <filesystem>
#include <thread>

#include <unistd.h>

#include <catch2/catch_test_macros.hpp>

#include <ovis/english/model_daemon.hpp>

namespace {

using ovis::english::chat_message;

// Answers from rules, so a reply shows which request reached it.
class scripted_model final : public ovis::english::language_model {
public:
  int requests = 0;

  auto name() const -> std::string override { return "scripted"; }
  auto chat(const std::vector<chat_message> &p_messages,
            std::string_view p_grammar) -> std::string override {
    ++requests;
    if (p_messages.back().content == "fail")
      throw ovis::error("the model failed");
    return p_messages.back().content + "|" + std::string(p_grammar);
  }
  auto choose(const std::vector<chat_message> &, std::size_t p_options)
      -> std::vector<double> override {
    ++requests;
    std::vector<double> result(p_options, 0.1 / double(p_options - 1));
    result[0] = 0.9;
    return result;
  }
  auto embed(const std::vector<std::string> &p_texts)
      -> std::vector<std::vector<float>> override {
    ++requests;
    std::vector<std::vector<float>> result;
    for (const auto &text : p_texts)
      result.push_back({float(text.size()), 0.5F});
    return result;
  }
};

auto test_socket(const std::string &p_name) -> std::filesystem::path {
  auto path = std::filesystem::temp_directory_path() /
              ("ovis-" + p_name + "-" + std::to_string(::getpid()) + ".sock");
  std::filesystem::remove(path);
  return path;
}

// Waits until something listens at `p_socket`.
auto wait_for(const std::filesystem::path &p_socket) -> void {
  for (int tries = 0; tries < 200 && !std::filesystem::exists(p_socket); ++tries)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

} // namespace

#ifndef _WIN32

TEST_CASE("The model daemon answers like the model it holds.", "[daemon]") {
  const auto socket = test_socket("daemon");
  scripted_model served;
  std::thread server([&] {
    ovis::english::serve_model(served, socket, std::chrono::seconds(30));
  });
  wait_for(socket);

  ovis::english::daemon_model client({.socket = socket}, "scripted", "print");
  REQUIRE(client.name() == "scripted");
  REQUIRE(client.fingerprint() == "print");
  // Text with the wire format's own characters and a newline survives.
  REQUIRE(client.chat({{"system", "s"}, {"user", "a:1\nb"}}, "g") == "a:1\nb|g");
  const auto probabilities = client.choose({{"user", "pick"}}, 3);
  REQUIRE(probabilities == served.choose({{"user", "pick"}}, 3));
  const auto vectors = client.embed({"ab", "abcd"});
  REQUIRE(vectors.size() == 2);
  REQUIRE(vectors[1] == std::vector<float>{4.0F, 0.5F});
  // The model's own errors come back as errors, not as a lost daemon.
  REQUIRE_THROWS_AS(client.chat({{"user", "fail"}}, ""), ovis::error);

  REQUIRE(ovis::english::stop_daemon(socket));
  server.join();
  REQUIRE(!std::filesystem::exists(socket));
  REQUIRE(!ovis::english::stop_daemon(socket));
}

TEST_CASE("The model daemon quits when unused.", "[daemon]") {
  const auto socket = test_socket("idle");
  scripted_model served;
  std::thread server([&] {
    ovis::english::serve_model(served, socket, std::chrono::seconds(1));
  });
  server.join();
  REQUIRE(!std::filesystem::exists(socket));
}

TEST_CASE("Without a daemon, the model loads in this process.", "[daemon]") {
  const auto socket = test_socket("missing");
  scripted_model *made = nullptr;
  std::vector<std::string> logged;
  ovis::english::daemon_model client(
      {.socket = socket,
       .fallback =
           [&] {
             auto model = std::make_unique<scripted_model>();
             made = model.get();
             return model;
           },
       .log = [&](std::string_view p_line) { logged.emplace_back(p_line); }},
      "scripted", "print");
  REQUIRE(client.chat({{"user", "hi"}}, "") == "hi|");
  REQUIRE(client.choose({{"user", "pick"}}, 2).size() == 2);
  REQUIRE(made != nullptr);
  REQUIRE(made->requests == 2);
  REQUIRE(logged.size() == 1);

  // No fallback: the build is told why.
  ovis::english::daemon_model bare({.socket = socket}, "scripted", "print");
  REQUIRE_THROWS_AS(bare.choose({{"user", "pick"}}, 2), ovis::error);
}

TEST_CASE("The daemon's socket is private and depends on the settings.",
          "[daemon]") {
  ovis::english::builtin_options options;
  options.location.file = "weights.gguf";
  const auto first = ovis::english::daemon_socket_path(options, "ovis");
  REQUIRE(first);
  REQUIRE(first == ovis::english::daemon_socket_path(options, "ovis"));
  options.use_gpu = false;
  REQUIRE(first != ovis::english::daemon_socket_path(options, "ovis"));
  options.use_gpu = true;
  options.threads = 3;
  REQUIRE(first != ovis::english::daemon_socket_path(options, "ovis"));
  const auto status = std::filesystem::status(first->parent_path());
  REQUIRE((status.permissions() & (std::filesystem::perms::group_all |
                                   std::filesystem::perms::others_all)) ==
          std::filesystem::perms::none);
}

TEST_CASE("ovis starts the model daemon and it answers as the model would.",
          "[model]") {
  const auto *const path = std::getenv("OVIS_TEST_MODEL");
  if (path == nullptr || !ovis::english::has_builtin_model_support())
    SKIP("Set OVIS_TEST_MODEL to a GGUF file to run this test.");
  const auto socket = test_socket("started");
  const auto model_path = std::filesystem::absolute(path).string();
  const ovis::english::builtin_options options{
      .location = *ovis::english::find_builtin_model(model_path, "."),
      .use_gpu = false};
  const std::vector<chat_message> question = {
      {"system", "Answer with the number of the right option."},
      {"user", "Which is a colour?\n1. red\n2. seven\n3. none of these\nAnswer:"}};

  ovis::english::daemon_model client(
      {.socket = socket,
       .executable = OVIS_EXECUTABLE,
       .serve_arguments = {"--model-path", model_path, "--cpu",
                           "--daemon-idle", "1"}},
      "tiny", "print");
  ovis::english::builtin_model local(options);
  REQUIRE(client.choose(question, 3) == local.choose(question, 3));
  REQUIRE(client.embed({"red", "seven"}) == local.embed({"red", "seven"}));
  // Started once, then reused.
  REQUIRE(client.choose(question, 3) == local.choose(question, 3));
  REQUIRE(ovis::english::stop_daemon(socket));
}

#endif
