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

TEST_CASE("Keep tracking documents under 500 lines.", "[docs]") {
  // Inactive information belongs in docs/dream/ (see the dream skill).
  const std::filesystem::path root = OVIS_SOURCE_DIR;
  std::vector<std::filesystem::path> documents = {
      root / "CLAUDE.md", root / "docs" / "state.md",
      root / "docs" / "remaining-tasks.md",
      root / "docs" / "architecture-spec.md",
      root / "docs" / "model-backends.md", root / "docs" / "c-libraries.md",
      root / "docs" / "ovis-core.md",
      root / "docs" / "questions.md"};
  for (const auto &entry :
       std::filesystem::directory_iterator(root / "docs" / "dream"))
    if (entry.is_directory())
      documents.push_back(entry.path() / "INDEX.md");

  for (const auto &document : documents) {
    INFO(document.string());
    REQUIRE(std::filesystem::exists(document));
    const auto text = read_file(document);
    REQUIRE(std::ranges::count(text, '\n') < 500);

    // Relative links must point at files that exist.
    for (std::size_t position = text.find("]("); position != std::string::npos;
         position = text.find("](", position + 2)) {
      const auto end = text.find(')', position);
      const auto target = text.substr(position + 2, end - position - 2);
      if (target.starts_with("http") || target.starts_with("#"))
        continue;
      INFO(target);
      REQUIRE(std::filesystem::exists(document.parent_path() /
                                      target.substr(0, target.find('#'))));
    }
  }
}

