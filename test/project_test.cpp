#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>

#include <ovis/project/project.hpp>
#include <ovis/project/toml.hpp>

// ovis.toml (proposals/projects.md §1, packages.md §1–§4): the TOML subset,
// the project's tables, and finding the project from a folder inside it.

using Catch::Matchers::ContainsSubstring;
using namespace ovis::project;

namespace {

auto parse(const std::string &p_text) -> project {
  return parse_project(p_text, "/work/greeter/ovis.toml");
}

// The message `p_text` is refused with.
auto refusal(const std::string &p_text) -> std::string {
  try {
    parse(p_text);
  } catch (const ovis::error &p_error) {
    return p_error.what();
  }
  FAIL("accepted: " << p_text);
  return {};
}

} // namespace

TEST_CASE("The TOML ovis.toml uses.", "[project]") {
  const auto root = parse_toml(R"(# a comment
top = "x"   # after a value
[a]
text = "tab\there \"quoted\""
literal = 'C:\path'
number = -1_000
yes = true
list = [
  "one",  # a comment in a list
  "two",
]
inline = { git = "https://x", version = "1.2" }
dotted.key = 3
"https://example.com/log" = { path = "../log" }

[c.zlib]
pkg-config = "zlib"
)",
                               "ovis.toml");
  REQUIRE(root.find("top")->text == "x");
  const auto &a = *root.find("a");
  CHECK(a.find("text")->text == "tab\there \"quoted\"");
  CHECK(a.find("literal")->text == "C:\\path");
  CHECK(a.find("number")->integer == -1000);
  CHECK(a.find("yes")->boolean);
  REQUIRE(a.find("list")->items.size() == 2);
  CHECK(a.find("list")->items[1].text == "two");
  CHECK(a.find("inline")->find("version")->text == "1.2");
  CHECK(a.find("dotted")->find("key")->integer == 3);
  CHECK(a.find("https://example.com/log")->find("path")->text == "../log");
  CHECK(root.find("c")->find("zlib")->find("pkg-config")->text == "zlib");
  CHECK(root.find("c")->find("zlib")->line == 16);

  const auto refused = [](const std::string &p_text) {
    try {
      parse_toml(p_text, "ovis.toml");
    } catch (const ovis::error &p_error) {
      return std::string(p_error.what());
    }
    return std::string("accepted");
  };
  CHECK(refused("a = 1\na = 2\n") == "ovis.toml, line 2: `a` is given twice");
  CHECK(refused("[t]\n[t]\n") == "ovis.toml, line 2: [t] is given twice");
  CHECK_THAT(refused("a = \"open\n"), ContainsSubstring("line 1: a text is not closed"));
  CHECK_THAT(refused("a = hello\n"), ContainsSubstring("write texts in \"quotes\""));
  CHECK_THAT(refused("[[x]]\n"), ContainsSubstring("arrays of tables"));
  CHECK_THAT(refused("a = [1, 2\n"), ContainsSubstring("expected , or ]"));
  CHECK_THAT(refused("a = 1 2\n"), ContainsSubstring("expected the end of the line"));
  CHECK_THAT(refused("\n\na =\n"), ContainsSubstring("line 3: expected a value"));

  CHECK(toml_key("zlib") == "zlib");
  CHECK(toml_key("https://x") == "\"https://x\"");
  CHECK(toml_string("a\"b\\") == "\"a\\\"b\\\\\"");
}

TEST_CASE("A project's tables.", "[project]") {
  const auto read = parse(R"([project]
name = "greeter"
toolchain = "0.1.0"

[build]
include = ["include"]
defines = ["USE_FAST=1"]
phrases = ["phrases"]
c = ["c/*.c"]

[c.zlib]
pkg-config = "zlib >= 1.2.11"

[c.cjson]
git = "https://github.com/DaveGamble/cJSON"
tag = "v1.7.18"
targets = ["cjson"]

[c.local]
cmake = "native.cmake"
targets = ["shout"]

[dependencies]
csv = { git = "https://github.com/ovis-libs/csv", version = "1.4" }
log = { git = "https://github.com/ovis-libs/log", version = "=2.0.1" }
mine = { path = "../mine" }

[overrides]
"https://github.com/ovis-libs/log" = { path = "../log" }
"c.cjson" = { tag = "release-7" }
)");
  CHECK(read.root == "/work/greeter");
  CHECK(read.program_name() == "greeter");
  CHECK(read.toolchain == "0.1.0");
  CHECK(read.include == std::vector<std::string>{"include"});
  CHECK(read.defines == std::vector<std::string>{"USE_FAST=1"});
  CHECK(read.c_sources == std::vector<std::string>{"c/*.c"});
  REQUIRE(read.c_dependencies.size() == 3);
  CHECK(read.c_dependencies[0].kind == c_dependency::kind_type::pkg_config);
  CHECK(read.c_dependencies[0].source == "zlib >= 1.2.11");
  CHECK(read.c_dependencies[1].kind == c_dependency::kind_type::git);
  CHECK(read.c_dependencies[1].tag == "v1.7.18");
  CHECK(read.c_dependencies[1].targets == std::vector<std::string>{"cjson"});
  CHECK(read.c_dependencies[2].kind == c_dependency::kind_type::cmake);
  CHECK(read.c_dependencies[2].line == 19);

  REQUIRE(read.dependencies.size() == 3);
  CHECK(read.dependencies[0].name == "csv");
  CHECK(read.dependencies[0].minimum == version{1, 4, 0});
  CHECK_FALSE(read.dependencies[0].exact);
  CHECK(read.dependencies[1].exact);
  CHECK(read.dependencies[1].minimum == version{2, 0, 1});
  CHECK(read.dependencies[2].path == "../mine");
  REQUIRE(read.overrides.size() == 2);
  CHECK(read.overrides[0].name == "https://github.com/ovis-libs/log");
  CHECK(read.overrides[1].tag == "release-7");

  // The smallest project, named after its folder.
  const auto smallest = parse("[project]\n");
  CHECK(smallest.program_name() == "greeter");
  CHECK(smallest.c_dependencies.empty());
}

TEST_CASE("Mistakes in ovis.toml name the line.", "[project]") {
  CHECK(refusal("[project]\nnmae = \"x\"\n") ==
        "ovis.toml, line 2: unknown key `nmae` in [project] (it takes name, "
        "toolchain)");
  CHECK_THAT(refusal("[biuld]\n"), ContainsSubstring("line 1: unknown table [biuld]"));
  CHECK(refusal("[build]\ninclude = \"include\"\n") ==
        "ovis.toml, line 2: `include` must be a list, not a text");
  CHECK_THAT(refusal("[build]\ninclude = [1]\n"),
             ContainsSubstring("each of `include` must be a text"));
  CHECK_THAT(refusal("[c.z]\n"),
             ContainsSubstring("line 1: [c.z] needs exactly one of pkg-config"));
  CHECK_THAT(refusal("[c.z]\npkg-config = \"z\"\npath = \"p\"\n"),
             ContainsSubstring("exactly one"));
  CHECK_THAT(refusal("[c.j]\ngit = \"https://x\"\ntargets = [\"j\"]\n"),
             ContainsSubstring("give its `tag`"));
  CHECK_THAT(refusal("[c.j]\npath = \"native\"\n"), ContainsSubstring("needs `targets`"));
  CHECK_THAT(refusal("[c.z]\npkg-config = \"z\"\ntargets = [\"z\"]\n"),
             ContainsSubstring("no `targets`"));
  CHECK_THAT(refusal("[dependencies]\nlog = \"1.2\"\n"),
             ContainsSubstring("must be a table"));
  CHECK_THAT(refusal("[dependencies]\nlog = { git = \"https://x\" }\n"),
             ContainsSubstring("needs a `version`"));
  CHECK_THAT(refusal("[dependencies]\nlog = { git = \"x\", version = \"one\" }\n"),
             ContainsSubstring("`version` must be a version"));
  CHECK_THAT(refusal("[dependencies]\nlog = { version = \"1\" }\n"),
             ContainsSubstring("needs one of `git` or `path`"));
  CHECK_THAT(refusal("[package]\nname = \"csv\"\n"),
             ContainsSubstring("needs a `name` and a `version`"));

  // A package names the oldest version it works with, never an exact one.
  const std::string package = "[package]\nname = \"csv\"\nversion = \"1.4.0\"\n";
  CHECK_THAT(refusal(package + "[dependencies]\nlog = { git = \"x\", version = "
                               "\"=1.2.0\" }\n"),
             ContainsSubstring("line 5: a package can't pin `log`"));
  CHECK_THAT(refusal(package + "[dependencies]\nlog = { git = \"x\", commit = "
                               "\"abc\" }\n"),
             ContainsSubstring("can't pin"));
  CHECK_THAT(refusal(package + "[overrides]\nlog = { path = \"../log\" }\n"),
             ContainsSubstring("[overrides] are for the project at the top"));
  const auto read = parse(package + "ovis = \"0.2\"\n");
  CHECK(read.package->package_version == version{1, 4, 0});
  CHECK(read.package->ovis == version{0, 2, 0});
}

TEST_CASE("Versions and their lines.", "[project]") {
  CHECK(parse_version("1.4.0") == version{1, 4, 0});
  CHECK(parse_version("v2") == version{2, 0, 0});
  CHECK(parse_version("0.3") == version{0, 3, 0});
  CHECK_FALSE(parse_version("1.4.0-beta"));
  CHECK_FALSE(parse_version("1..2"));
  CHECK_FALSE(parse_version(""));
  CHECK(version{1, 4, 2}.line() == "1");
  CHECK(version{0, 3, 9}.line() == "0.3");
  CHECK(version{1, 4, 0} < version{1, 10, 0});
  CHECK(version{2, 0, 1}.text() == "2.0.1");
}

TEST_CASE("The project is found from a folder inside it.", "[project]") {
  const auto root = std::filesystem::temp_directory_path() / "ovis-project-find";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "app" / "src" / "deep");
  std::ofstream(root / "app" / "ovis.toml") << "[project]\nname = \"app\"\n";
  CHECK(find_project(root / "app" / "src" / "deep") == root / "app" / "ovis.toml");
  CHECK(find_project(root / "app" / "src" / "deep" / "x.ovis") ==
        root / "app" / "ovis.toml");
  CHECK(find_project(root / "app") == root / "app" / "ovis.toml");
  const auto loaded = load_project(root / "app" / "ovis.toml");
  CHECK(loaded.program_name() == "app");
  CHECK(loaded.root == root / "app");
  std::filesystem::remove_all(root);
}
