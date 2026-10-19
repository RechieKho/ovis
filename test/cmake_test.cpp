#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <ovis/core/c_import.hpp>

// Building under CMake (proposals/cmake.md): `--object`, `--link-by-caller`,
// `@FILE`, `--locked` and `--depfile`, which let CMake link what Ovis writes.

using Catch::Matchers::ContainsSubstring;

namespace {

struct outcome {
  int status = 0;
  std::string output;
};

// Runs a shell command, with standard error in its output.
auto shell(const std::string &p_command) -> outcome {
  outcome result;
  auto *const pipe = popen((p_command + " 2>&1").c_str(), "r");
  REQUIRE(pipe != nullptr);
  char buffer[256];
  while (const auto count = std::fread(buffer, 1, sizeof(buffer), pipe))
    result.output.append(buffer, count);
  result.status = pclose(pipe);
  return result;
}

auto read_text(const std::filesystem::path &p_path) -> std::string {
  std::ifstream input(p_path);
  std::stringstream text;
  text << input.rdbuf();
  return text.str();
}

// A fresh folder for one test.
class workspace {
public:
  std::filesystem::path root;

  explicit workspace(const std::string &p_name)
      : root(std::filesystem::temp_directory_path() / ("ovis-cmake-" + p_name)) {
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
  }
  ~workspace() { std::filesystem::remove_all(root); }

  auto write(const std::string &p_name, const std::string &p_text) const
      -> std::filesystem::path {
    std::ofstream(root / p_name) << p_text;
    return root / p_name;
  }

  auto path(const std::string &p_name) const -> std::string {
    return (root / p_name).string();
  }
};

const std::string ovis_program = OVIS_EXECUTABLE;

} // namespace

TEST_CASE("Build an object file for the caller to link.", "[cmake]") {
  workspace space("object");
  space.write("hello.ovis", "show \"hi\", 1 + 2\n");
  const auto built =
      shell(ovis_program + " build " + space.path("hello.ovis") + " --object " +
            space.path("hello.o") + " --link-by-caller --locked --quiet");
  INFO(built.output);
  REQUIRE(built.status == 0);
  REQUIRE(std::filesystem::exists(space.root / "hello.o"));
  // Nothing was linked, and nothing else was written.
  REQUIRE(!std::filesystem::exists(space.root / "hello"));
  REQUIRE(!std::filesystem::exists(space.root / "hello.ovis.lock"));

  // The runtime is a C file of its own; with it the object makes a program.
  REQUIRE(shell(ovis_program + " runtime -o " + space.path("runtime.c")).status == 0);
  const auto linked = shell("cc -w -DOVIS_CHECKED " + space.path("hello.o") + " " +
                            space.path("runtime.c") + " -lm -o " +
                            space.path("hello"));
  INFO(linked.output);
  REQUIRE(linked.status == 0);
  REQUIRE(shell(space.path("hello")).output == "hi 3\n");
  // Without -o the runtime goes to standard output.
  REQUIRE_THAT(shell(ovis_program + " runtime").output, ContainsSubstring("ovis_init"));
}

TEST_CASE("Read arguments from a response file.", "[cmake]") {
  workspace space("response");
  space.write("hello.ovis", "show 7\n");
  space.write("flags.rsp", "build \"" + space.path("hello.ovis") +
                               "\"\n--object\n" + space.path("hello.o") +
                               "\n--quiet --locked\n-I/nowhere -DNAME=1\n");
  const auto built = shell(ovis_program + " @" + space.path("flags.rsp"));
  INFO(built.output);
  REQUIRE(built.status == 0);
  REQUIRE(std::filesystem::exists(space.root / "hello.o"));
}

TEST_CASE("A locked build changes nothing and says what to run.", "[cmake]") {
  workspace space("locked");
  // `mean of` is no phrase: reading it takes a decision, which a locked
  // build can't make.
  const auto source = "numbers = [1, 2, 3]\nx = mean of numbers\nshow x\n";
  const auto program = space.write("unclear.ovis", source);
  const auto built =
      shell(ovis_program + " build " + program.string() + " --object " +
            space.path("unclear.o") + " --locked --quiet");
  REQUIRE(built.status != 0);
  REQUIRE_THAT(built.output, ContainsSubstring("--locked"));
  REQUIRE_THAT(built.output, ContainsSubstring("ovis run"));
  REQUIRE(read_text(program) == source);
  REQUIRE(!std::filesystem::exists(space.root / "unclear.o"));
  REQUIRE(!std::filesystem::exists(space.root / "unclear.ovis.lock"));

  // Settled by the author, and the lockfile that records the meaning
  // written, the same program builds.
  space.write("unclear.ovis",
              "numbers = [1, 2, 3]\n@[meaning: x = average of numbers]\n"
              "x = mean of numbers\nshow x\n");
  const auto lock_missing =
      shell(ovis_program + " build " + program.string() + " --object " +
            space.path("unclear.o") + " --locked --quiet");
  REQUIRE(lock_missing.status != 0);
  REQUIRE_THAT(lock_missing.output, ContainsSubstring("lockfile would change"));
  REQUIRE(shell(ovis_program + " build " + program.string() + " --object " +
                space.path("unclear.o") + " --offline --quiet")
              .status == 0);
  const auto settled =
      shell(ovis_program + " build " + program.string() + " --object " +
            space.path("unclear.o") + " --locked --quiet");
  INFO(settled.output);
  REQUIRE(settled.status == 0);
}

TEST_CASE("Write a dependency file with the headers that were read.", "[cmake]") {
  if (!ovis::core::has_c_import_support())
    SKIP("Built without libclang.");
  workspace space("depfile");
  std::filesystem::create_directories(space.root / "include");
  space.write("include/small.h", "int small_twice(int x);\n");
  space.write("use.ovis",
              "use \"small.h\"\nshow small_twice(21)\n");
  space.write("small.c", "int small_twice(int x) { return 2 * x; }\n");
  // The header's hash goes in the lockfile once, by an unlocked build.
  const auto flags = " --object " + space.path("use.o") +
                     " --link-by-caller --offline --quiet -I" +
                     space.path("include");
  REQUIRE(shell(ovis_program + " build " + space.path("use.ovis") + flags)
              .status == 0);
  const auto built = shell(ovis_program + " build " + space.path("use.ovis") +
                           flags + " --locked --depfile " + space.path("use.d"));
  INFO(built.output);
  REQUIRE(built.status == 0);
  const auto depfile = read_text(space.root / "use.d");
  REQUIRE_THAT(depfile, ContainsSubstring(space.path("use.o") + ":"));
  REQUIRE_THAT(depfile, ContainsSubstring(space.path("use.ovis")));
  REQUIRE_THAT(depfile, ContainsSubstring(space.path("include/small.h")));

  // The C library is left to the caller.
  REQUIRE(shell(ovis_program + " runtime -o " + space.path("runtime.c")).status == 0);
  const auto linked = shell("cc -w -DOVIS_CHECKED " + space.path("use.o") + " " +
                            space.path("small.c") + " " +
                            space.path("runtime.c") + " -lm -o " +
                            space.path("use"));
  INFO(linked.output);
  REQUIRE(linked.status == 0);
  REQUIRE(shell(space.path("use")).output == "42\n");
}

TEST_CASE("Build an Ovis program with CMake from an installed Ovis.", "[cmake]") {
  if (shell("cmake --version").status != 0 || shell("cc --version").status != 0)
    SKIP("Needs cmake and a C compiler.");
  if (!ovis::core::has_c_import_support())
    SKIP("Built without libclang.");
  workspace space("find-package");
  const auto prefix = space.path("prefix");
  const auto install = shell(std::string("cmake --install ") + OVIS_BUILD_DIR +
                             " --component ovis --prefix " + prefix);
  INFO(install.output);
  REQUIRE(install.status == 0);

  // The example is copied, so building it leaves the tree as it was.
  std::filesystem::copy(std::string(OVIS_SOURCE_DIR) + "/examples/cmake",
                        space.root / "example",
                        std::filesystem::copy_options::recursive);
  const auto configured =
      shell("cmake -S " + space.path("example") + " -B " + space.path("build") +
            " -DCMAKE_PREFIX_PATH=" + prefix);
  INFO(configured.output);
  REQUIRE(configured.status == 0);
  const auto built = shell("cmake --build " + space.path("build"));
  INFO(built.output);
  REQUIRE(built.status == 0);
  REQUIRE(shell(space.path("build/greeter")).output == "Twice 3 is 6\n");

  // Changing the program builds it again; nothing in the tree is written.
  std::ofstream(space.root / "example/greeter.ovis", std::ios::app)
      << "show \"again\"\n";
  const auto rebuilt = shell("cmake --build " + space.path("build"));
  INFO(rebuilt.output);
  REQUIRE(rebuilt.status == 0);
  REQUIRE(shell(space.path("build/greeter")).output ==
          "Twice 3 is 6\nagain\n");
  REQUIRE(!std::filesystem::exists(space.root / "example/greeter"));
}
