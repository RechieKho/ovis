#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include <llvm/IR/LLVMContext.h>

#include <ovis/codegen/llvm_codegen.hpp>
#include <ovis/core/checker.hpp>
#include <ovis/core/elaborate.hpp>
#include <ovis/core/modules.hpp>
#include <ovis/core/c_import.hpp>
#include <ovis/english/language_model.hpp>

#include "support.hpp"

using namespace std::literals::string_literals;
using namespace test_support;

TEST_CASE("Find weights embedded in an executable.", "[model]") {
  const auto directory = std::filesystem::temp_directory_path();
  const auto program = directory / "ovis-test-program-file";
  const auto weights = directory / "ovis-test-weights.gguf";
  const auto combined = directory / "ovis-test-combined";
  std::ofstream(program, std::ios::binary) << "pretend executable";
  std::ofstream(weights, std::ios::binary) << "pretend weights";

  const auto plain = ovis::english::find_builtin_model(std::nullopt, program);
  REQUIRE((!plain || !plain->is_embedded));
  ovis::english::embed_model(program, weights, combined);
  const auto found = ovis::english::find_builtin_model(std::nullopt, combined);
  REQUIRE(found);
  REQUIRE(found->is_embedded);
  REQUIRE(found->offset % 4096 == 0);
  REQUIRE(found->size == std::filesystem::file_size(weights));
  std::ifstream input(combined, std::ios::binary);
  input.seekg(static_cast<std::streamoff>(found->offset));
  std::string text(found->size, '\0');
  input.read(text.data(), static_cast<std::streamsize>(text.size()));
  REQUIRE(text == "pretend weights");

  // Embedding again replaces the weights instead of stacking them.
  ovis::english::embed_model(combined, weights, combined);
  REQUIRE(std::filesystem::file_size(combined) ==
          found->offset + found->size + 32);

  REQUIRE_THROWS_AS(
      ovis::english::find_builtin_model(directory / "missing.gguf", program),
      ovis::error);
  for (const auto &path : {program, weights, combined})
    std::filesystem::remove(path);
}

TEST_CASE("Prefer the larger default model where a GPU runs it.", "[model]") {
  const auto directory =
      std::filesystem::temp_directory_path() / "ovis-test-defaults";
  std::filesystem::create_directories(directory);
  const auto program = directory / "ovis";
  // No model from this machine's own cache.
  const auto *const cache = std::getenv("XDG_CACHE_HOME");
  const std::optional<std::string> saved =
      cache != nullptr ? std::optional<std::string>(cache) : std::nullopt;
  ::setenv("XDG_CACHE_HOME", (directory / "cache").c_str(), 1);
  const auto *const model_path = std::getenv("OVIS_MODEL_PATH");
  const std::optional<std::string> saved_path =
      model_path != nullptr ? std::optional<std::string>(model_path)
                            : std::nullopt;
  ::unsetenv("OVIS_MODEL_PATH");
  const auto &small = ovis::english::default_model_for(false);
  const auto &large = ovis::english::default_model_for(true);
  REQUIRE(small.size == "3b");
  REQUIRE(large.size == "7b");
  std::ofstream(directory / large.file_name) << "pretend 7B";
  // Only the larger one: used either way.
  REQUIRE(ovis::english::find_builtin_model(std::nullopt, program, false)->file ==
          directory / large.file_name);
  std::ofstream(directory / small.file_name) << "pretend 3B";
  REQUIRE(ovis::english::find_builtin_model(std::nullopt, program, true)->file ==
          directory / large.file_name);
  REQUIRE(ovis::english::find_builtin_model(std::nullopt, program, false)->file ==
          directory / small.file_name);
  std::filesystem::remove_all(directory);
  if (saved)
    ::setenv("XDG_CACHE_HOME", saved->c_str(), 1);
  else
    ::unsetenv("XDG_CACHE_HOME");
  if (saved_path)
    ::setenv("OVIS_MODEL_PATH", saved_path->c_str(), 1);
}

TEST_CASE("The built-in model repeats itself exactly.", "[model]") {
  const auto *const path = std::getenv("OVIS_TEST_MODEL");
  if (path == nullptr || !ovis::english::has_builtin_model_support())
    SKIP("Set OVIS_TEST_MODEL to a GGUF file to run this test.");

  const auto options = ovis::english::builtin_options{
      .location = *ovis::english::find_builtin_model(std::string(path), "."),
      .max_tokens = 48};
  const std::vector<ovis::english::chat_message> request = {
      {"system", "You answer in a few lower case words."},
      {"user", "Name three colours."}};
  const std::string grammar = "root ::= [a-z ]+\n";

  ovis::english::builtin_model first(options);
  const auto reply = first.chat(request, grammar);
  // A different request in between must not change the answer, even though
  // part of the prompt is reused.
  first.chat({{"system", "You answer in a few lower case words."},
              {"user", "Say hi."}},
             ovis::english::choice_grammar(3));
  REQUIRE(first.chat(request, grammar) == reply);
  ovis::english::builtin_model second(options);
  REQUIRE(second.chat(request, grammar) == reply);
  REQUIRE(!first.fingerprint().empty());

  // A system message longer than a batch is saved once read whole, and a
  // new model loads it: the reply is the same as reading it again.
  std::string long_system = "You answer in a few lower case words.";
  for (int index = 0; index < 120; ++index)
    long_system += " Colours are words.";
  const std::vector<ovis::english::chat_message> long_request = {
      {"system", long_system}, {"user", "Name three colours."}};
  const auto long_reply = first.chat(long_request, grammar);
  first.chat(request, grammar);
  REQUIRE(first.chat(long_request, grammar) == long_reply);
  ovis::english::builtin_model third(options);
  REQUIRE(third.chat(long_request, grammar) == long_reply);

  // The output follows the grammar.
  REQUIRE(!reply.empty());
  REQUIRE(reply.find_first_not_of("abcdefghijklmnopqrstuvwxyz ") ==
          std::string::npos);
}

