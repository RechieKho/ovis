#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

// Building a project (proposals/projects.md J2–J4): `ovis new`/`init`, the
// commands with no program, and a project's own C files and pkg-config
// dependencies built with cc alone.

using Catch::Matchers::ContainsSubstring;

namespace {

struct outcome {
  int status = 0;
  std::string output;
};

const std::string ovis_program = OVIS_EXECUTABLE;

// Runs `ovis ARGUMENTS` in `p_directory`, standard error included.
auto ovis(const std::filesystem::path &p_directory, const std::string &p_arguments)
    -> outcome {
  outcome result;
  const auto command = "cd '" + p_directory.string() + "' && '" + ovis_program +
                       "' " + p_arguments + " 2>&1";
  auto *const pipe = popen(command.c_str(), "r");
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

class workspace {
public:
  std::filesystem::path root;

  explicit workspace(const std::string &p_name)
      : root(std::filesystem::temp_directory_path() / ("ovis-project-" + p_name)) {
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
  }
  ~workspace() { std::filesystem::remove_all(root); }

  auto write(const std::string &p_name, const std::string &p_text) const -> void {
    std::filesystem::create_directories((root / p_name).parent_path());
    std::ofstream(root / p_name) << p_text;
  }
};

} // namespace

TEST_CASE("ovis new makes a project that runs with no program named.",
          "[project]") {
  workspace space("new");
  const auto made = ovis(space.root, "new greeter --quiet");
  INFO(made.output);
  REQUIRE(made.status == 0);
  const auto project = space.root / "greeter";
  CHECK(read_text(project / "ovis.toml") == "[project]\nname = \"greeter\"\n");
  CHECK(read_text(project / ".gitignore") == "/build/\n");
  CHECK(std::filesystem::exists(project / "main.ovis"));

  auto ran = ovis(project, "run --quiet --offline");
  INFO(ran.output);
  CHECK(ran.status == 0);
  CHECK(ran.output == "Hello, world!\n");
  // From a folder inside the project, and naming the project's folder.
  std::filesystem::create_directories(project / "notes" / "deep");
  CHECK(ovis(project / "notes" / "deep", "run --quiet --offline").output ==
        "Hello, world!\n");
  CHECK(ovis(space.root, "run greeter --quiet --offline").output ==
        "Hello, world!\n");

  // build writes build/NAME, unless -o says otherwise.
  REQUIRE(ovis(project, "build --quiet --offline").status == 0);
  REQUIRE(std::filesystem::exists(project / "build" / "greeter"));
  REQUIRE(ovis(project, "build --quiet --offline -o other").status == 0);
  CHECK(std::filesystem::exists(project / "other"));

  // check and fmt work on the project too, and check writes nothing.
  CHECK(ovis(project, "check").status == 0);
  CHECK(ovis(project, "fmt --check").status == 0);

  // new refuses a folder with files; init keeps the files there.
  CHECK_THAT(ovis(space.root, "new greeter").output,
             ContainsSubstring("already exists; use `ovis init`"));
  space.write("old/main.ovis", "show \"mine\"\n");
  REQUIRE(ovis(space.root / "old", "init --quiet").status == 0);
  CHECK(read_text(space.root / "old" / "main.ovis") == "show \"mine\"\n");
  CHECK(read_text(space.root / "old" / "ovis.toml") == "[project]\nname = \"old\"\n");
  CHECK_THAT(ovis(space.root / "old", "init").output,
             ContainsSubstring("ovis.toml already exists"));
}

TEST_CASE("Without a project, a program must be named.", "[project]") {
  workspace space("none");
  const auto ran = ovis(space.root, "run");
  CHECK(ran.status != 0);
  CHECK_THAT(ran.output, ContainsSubstring("no ovis.toml here or in a folder above"));
  space.write("ovis.toml", "[project]\n");
  CHECK_THAT(ovis(space.root, "run").output, ContainsSubstring("has no main.ovis"));
  space.write("ovis.toml", "[project]\nnmae = \"x\"\n");
  space.write("main.ovis", "show 1\n");
  CHECK_THAT(ovis(space.root, "run").output,
             ContainsSubstring("ovis.toml, line 2: unknown key `nmae`"));
  // A single file named builds as before, whatever ovis.toml says.
  space.write("one.ovis", "show 2\n");
  CHECK(ovis(space.root, "run one.ovis --quiet --offline").output == "2\n");
}

TEST_CASE("A project's settings and its own C files, built without CMake.",
          "[project]") {
  workspace space("native");
  space.write("ovis.toml", R"([project]
name = "calc"

[build]
include = ["include"]
defines = ["CALC_BASE=40"]
phrases = ["words"]
c = ["c/**/*.c"]
)");
  space.write("include/calc.h", "#define CALC_STEP 2\nint add_base(int n);\nint twice(int n);\n");
  space.write("c/base.c", "#include \"calc.h\"\nint add_base(int n) { return CALC_BASE + n; }\n");
  space.write("c/more/twice.c", "#include \"calc.h\"\nint twice(int n) { return CALC_STEP * n; }\n");
  space.write("words/calc.phr", "expr  the lucky number : int => 7\n");
  space.write("main.ovis", "use \"calc.h\"\nshow add_base(CALC_STEP), twice(5)\n"
                           "show the lucky number\n");

  auto ran = ovis(space.root, "run --quiet --offline");
  INFO(ran.output);
  REQUIRE(ran.status == 0);
  CHECK(ran.output == "42 10\n7\n");
  const auto object = space.root / "build" / "c" / "c_base.c.o";
  REQUIRE(std::filesystem::exists(object));
  REQUIRE(std::filesystem::exists(space.root / "build" / "c" / "c_more_twice.c.o"));

  // Built again only when the file, a header it includes, or the flags
  // change.
  const auto first = std::filesystem::last_write_time(object);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  REQUIRE(ovis(space.root, "run --quiet --offline").status == 0);
  CHECK((std::filesystem::last_write_time(object) == first));
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  space.write("include/calc.h", "#define CALC_STEP 3\nint add_base(int n);\nint twice(int n);\n");
  ran = ovis(space.root, "run --quiet --offline");
  INFO(ran.output);
  CHECK_THAT(ran.output, ContainsSubstring("calc.h changed since the last build"));
  CHECK_THAT(ran.output, ContainsSubstring("43 15\n7\n"));
  CHECK((std::filesystem::last_write_time(object) != first));

  // check reads the headers with the project's flags but compiles nothing.
  std::filesystem::remove_all(space.root / "build");
  CHECK(ovis(space.root, "check").status == 0);
  CHECK_FALSE(std::filesystem::exists(space.root / "build"));

  // A C file that doesn't compile is named, with cc's message.
  space.write("c/base.c", "int add_base(int n) { return n +; }\n");
  const auto broken = ovis(space.root, "run --quiet --offline");
  CHECK(broken.status != 0);
  CHECK_THAT(broken.output, ContainsSubstring("Compiling c/base.c failed"));

  // A pattern that matches nothing is a mistake, not an empty list.
  space.write("ovis.toml", "[project]\n[build]\nc = [\"src/*.c\"]\n");
  CHECK_THAT(ovis(space.root, "run --quiet --offline").output,
             ContainsSubstring("`c` pattern src/*.c matches no file"));
}

TEST_CASE("pkg-config dependencies, and the ones CMake builds named wrongly.",
          "[project]") {
  workspace space("pkg");
  space.write("main.ovis", "show 1\n");
  space.write("ovis.toml", "[project]\n[c.nothing]\npkg-config = \"no-such-library-ovis\"\n");
  const auto missing = ovis(space.root, "run --quiet --offline");
  CHECK(missing.status != 0);
  CHECK_THAT(missing.output,
             ContainsSubstring("[c.nothing]: pkg-config can't find `no-such-library-ovis`"));
  space.write("ovis.toml", "[project]\n[c.engine]\npath = \"native\"\ntargets = [\"engine\"]\n");
  CHECK_THAT(ovis(space.root, "run --quiet --offline").output,
             ContainsSubstring("ovis.toml, line 2: [c.engine] names native, which is "
                               "not a folder with a CMakeLists.txt"));
  space.write("ovis.toml", "[project]\n[c.mine]\ncmake = \"mine.cmake\"\ntargets = [\"m\"]\n");
  CHECK_THAT(ovis(space.root, "run --quiet --offline").output,
             ContainsSubstring("names mine.cmake, which is not a file"));
}

TEST_CASE("examples/project builds with zlib from pkg-config.", "[project]") {
  if (std::system("pkg-config --exists zlib") != 0)
    SKIP("zlib's pkg-config file is not installed");
  workspace space("example");
  std::filesystem::copy(OVIS_EXAMPLES_DIR "/project", space.root / "packer",
                        std::filesystem::copy_options::recursive);
  std::filesystem::remove_all(space.root / "packer" / "build");
  const auto ran = ovis(space.root / "packer", "run --quiet --offline");
  INFO(ran.output);
  REQUIRE(ran.status == 0);
  CHECK(ran.output == "Packed 3 shouts into 16 bytes\nPacked 100 into 20\n");
}
