#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <ovis/project/packages.hpp>

// Packages hosted on git (proposals/packages.md H1–H4): versions as tags,
// minimal version selection, ovis.lock, the cache, path dependencies and
// overrides, and a package's files read locked with the program. The
// repositories are made by the test and reached through file:// URLs.

using Catch::Matchers::ContainsSubstring;

namespace {

struct outcome {
  int status = 0;
  std::string output;
};

const std::string ovis_program = OVIS_EXECUTABLE;

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

// Repositories, a cache and a project, all in one folder.
class packages {
public:
  std::filesystem::path root;

  explicit packages(const std::string &p_name)
      : root(std::filesystem::temp_directory_path() / ("ovis-packages-" + p_name)) {
    remove();
    std::filesystem::create_directories(root / "app");
  }
  ~packages() { remove(); }

  auto remove() const -> void {
    // The cache's files are read-only; their folders aren't.
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }

  auto write(const std::string &p_name, const std::string &p_text) const -> void {
    std::filesystem::create_directories((root / p_name).parent_path());
    std::ofstream(root / p_name) << p_text;
  }

  auto url(const std::string &p_repository) const -> std::string {
    return "file://" + (root / p_repository).string();
  }

  auto git(const std::string &p_repository, const std::string &p_arguments) const
      -> void {
    const auto ran = shell("cd '" + (root / p_repository).string() +
                           "' && git -c user.name=ovis -c user.email=ovis@example.com "
                           "-c init.defaultBranch=main " +
                           p_arguments);
    INFO(ran.output);
    REQUIRE(ran.status == 0);
  }

  // Commits the repository's files as they are and tags them.
  auto release(const std::string &p_repository, const std::string &p_version) const
      -> void {
    if (!std::filesystem::exists(root / p_repository / ".git"))
      git(p_repository, "init -q");
    git(p_repository, "add -A");
    git(p_repository, "commit -q --allow-empty -m " + p_version);
    git(p_repository, "tag -a v" + p_version + " -m " + p_version);
  }

  // A package `p_name` at `p_version` saying `p_word` when greeting, with
  // the dependencies' lines given.
  auto package(const std::string &p_name, const std::string &p_version,
               const std::string &p_body, const std::string &p_dependencies = "") const
      -> void {
    write(p_name + "/ovis.toml", "[package]\nname = \"" + p_name + "\"\nversion = \"" +
                                     p_version + "\"\n" +
                                     (p_dependencies.empty()
                                          ? std::string()
                                          : "\n[dependencies]\n" + p_dependencies));
    write(p_name + "/" + p_name + ".ovis", p_body);
    release(p_name, p_version);
  }

  auto ovis(const std::string &p_arguments, const std::string &p_folder = "app") const
      -> outcome {
    return shell("cd '" + (root / p_folder).string() + "' && OVIS_PACKAGE_CACHE='" +
                 (root / "cache").string() + "' '" + ovis_program + "' " + p_arguments);
  }
};

} // namespace

TEST_CASE("Version tags and tree hashes.", "[packages]") {
  packages space("tags");
  space.package("greet", "1.0.0", "to greet (name):\n    show \"Hello,\", name\n");
  space.write("greet/ovis.toml", "[package]\nname = \"greet\"\nversion = \"1.2.0\"\n");
  space.release("greet", "1.2.0");
  space.git("greet", "tag not-a-version");
  space.git("greet", "tag v3");
  const auto tags = ovis::project::list_version_tags(space.url("greet"));
  REQUIRE(tags.size() == 2);
  CHECK(tags.begin()->first == ovis::project::version{1, 0, 0});
  CHECK(tags.rbegin()->first == ovis::project::version{1, 2, 0});
  CHECK(tags.begin()->second.size() == 40);

  CHECK(ovis::project::normalized_url("https://x.org/a/b.git/") == "https://x.org/a/b");
  space.write("tree/a.txt", "one");
  space.write("tree/sub/b.txt", "two");
  const auto hash = ovis::project::tree_hash(space.root / "tree");
  CHECK(hash.starts_with("sha256:"));
  CHECK(hash.size() == 7 + 64);
  space.write("tree/.git/ignored", "x");
  CHECK(ovis::project::tree_hash(space.root / "tree") == hash);
  space.write("tree/sub/b.txt", "two!");
  CHECK(ovis::project::tree_hash(space.root / "tree") != hash);
}

TEST_CASE("One version per line: the least that satisfies everyone.", "[packages]") {
  packages space("mvs");
  space.package("greet", "1.0.0", "to greet (name):\n    show \"Hello,\", name\n");
  space.package("greet", "1.1.0", "to greet (name):\n    show \"Hello there,\", name\n");
  space.package("greet", "1.2.0", "to greet (name):\n    show \"Hi,\", name\n");
  space.package("shout", "1.0.0", "to shout at (name):\n    greet name\n    show \"!!!\"\n",
                "greet = { git = \"" + space.url("greet") + "\", version = \"1.1\" }\n");
  space.write("app/ovis.toml", "[project]\nname = \"app\"\n\n[dependencies]\n"
                               "greet = { git = \"" + space.url("greet") +
                                   "\", version = \"1.0\" }\n"
                                   "shout = { git = \"" + space.url("shout") +
                                   "\", version = \"1.0\" }\n");
  space.write("app/main.ovis", "greet \"Ana\"\nshout at \"Bo\"\n");

  // app asks greet 1.0, shout 1.1: one greet, 1.1.0, not the newest 1.2.0.
  auto ran = space.ovis("run --offline --quiet");
  INFO(ran.output);
  REQUIRE(ran.status == 0);
  CHECK(ran.output == "Hello there, Ana\nHello there, Bo\n!!!\n");
  const auto lock = read_text(space.root / "app" / "ovis.lock");
  CHECK_THAT(lock, ContainsSubstring("\"version\": \"1.1.0\""));
  CHECK_THAT(lock, ContainsSubstring("\"dependencies\": [\n        \"greet@1\"\n"));
  CHECK_THAT(lock, ContainsSubstring("\"tree\": \"sha256:"));

  const auto tree = space.ovis("tree");
  CHECK_THAT(tree.output, ContainsSubstring("greet 1.1.0"));
  CHECK_THAT(tree.output, ContainsSubstring("shout 1.0.0"));
  CHECK_THAT(tree.output, ContainsSubstring("  greet 1.1.0"));

  // With the lock and the cache, the repositories aren't needed.
  std::filesystem::rename(space.root / "greet", space.root / "greet-moved");
  std::filesystem::rename(space.root / "shout", space.root / "shout-moved");
  ran = space.ovis("run --offline --quiet");
  INFO(ran.output);
  CHECK(ran.output == "Hello there, Ana\nHello there, Bo\n!!!\n");
  CHECK(read_text(space.root / "app" / "ovis.lock") == lock);
  std::filesystem::rename(space.root / "greet-moved", space.root / "greet");
  std::filesystem::rename(space.root / "shout-moved", space.root / "shout");

  // update moves to the newest version on the line, and says so.
  const auto updated = space.ovis("update greet");
  INFO(updated.output);
  CHECK(updated.status == 0);
  CHECK_THAT(updated.output, ContainsSubstring("greet 1.1.0 -> 1.2.0"));
  CHECK(space.ovis("run --offline --quiet").output == "Hi, Ana\nHi, Bo\n!!!\n");
  CHECK_THAT(space.ovis("update").output,
             ContainsSubstring("every dependency is where ovis.lock has it"));
  CHECK_THAT(space.ovis("update nothing").output,
             ContainsSubstring("no package nothing is in the program"));

  // A build that would change ovis.lock fails under --locked.
  std::filesystem::remove(space.root / "app" / "ovis.lock");
  CHECK_THAT(space.ovis("build --offline --quiet --locked").output,
             ContainsSubstring("ovis.lock would change"));
}

TEST_CASE("Exact versions, missing ones, and a tag that disagrees.", "[packages]") {
  packages space("exact");
  space.package("greet", "1.0.0", "to greet (name):\n    show \"Hello,\", name\n");
  space.package("greet", "1.1.0", "to greet (name):\n    show \"Hello there,\", name\n");
  space.package("shout", "1.0.0", "to shout at (name):\n    greet name\n",
                "greet = { git = \"" + space.url("greet") + "\", version = \"1.1\" }\n");
  space.write("app/main.ovis", "greet \"Ana\"\n");
  const auto project = [&](const std::string &p_greet, const std::string &p_more = "") {
    space.write("app/ovis.toml", "[project]\n[dependencies]\ngreet = { git = \"" +
                                     space.url("greet") + "\", " + p_greet + " }\n" + p_more);
    std::filesystem::remove(space.root / "app" / "ovis.lock");
  };

  project("version = \"=1.0.0\"");
  CHECK(space.ovis("run --offline --quiet").output == "Hello, Ana\n");
  // An exact version older than another package needs is said plainly.
  project("version = \"=1.0.0\"",
          "shout = { git = \"" + space.url("shout") + "\", version = \"1\" }\n");
  const auto older = space.ovis("run --offline --quiet");
  CHECK(older.status != 0);
  CHECK_THAT(older.output, ContainsSubstring("ovis.toml asks =1.0.0"));
  CHECK_THAT(older.output, ContainsSubstring("shout 1.0.0 asks 1.1.0"));
  project("version = \"1.5\"");
  CHECK_THAT(space.ovis("run --offline --quiet").output,
             ContainsSubstring("has no version 1.5.0 or later on its 1 line"));

  // A tag whose ovis.toml says another version is refused.
  space.write("greet/ovis.toml", "[package]\nname = \"greet\"\nversion = \"1.9.0\"\n");
  space.release("greet", "1.3.0");
  project("version = \"1.3\"");
  CHECK_THAT(space.ovis("run --offline --quiet").output,
             ContainsSubstring("says it is version 1.9.0"));
  // Something that isn't a package.
  space.write("plain/readme.txt", "no ovis.toml here");
  space.release("plain", "1.0.0");
  space.write("app/ovis.toml", "[project]\n[dependencies]\nplain = { git = \"" +
                                   space.url("plain") + "\", version = \"1\" }\n");
  CHECK_THAT(space.ovis("run --offline --quiet").output,
             ContainsSubstring("has no ovis.toml, so it is not an Ovis package"));
}

TEST_CASE("Path dependencies, and overrides in the root.", "[packages]") {
  packages space("paths");
  space.package("greet", "1.0.0", "to greet (name):\n    show \"Hello,\", name\n");
  space.write("local/greet/ovis.toml", "[package]\nname = \"greet\"\nversion = \"1.4.0\"\n");
  space.write("local/greet/greet.ovis", "to greet (name):\n    show \"Local hello,\", name\n");
  space.write("app/main.ovis", "greet \"Ana\"\n");

  space.write("app/ovis.toml", "[project]\n[dependencies]\n"
                               "greet = { path = \"../local/greet\" }\n");
  CHECK(space.ovis("run --offline --quiet").output == "Local hello, Ana\n");
  CHECK_THAT(read_text(space.root / "app" / "ovis.lock"), ContainsSubstring("\"path\": \""));

  // An override replaces a package everywhere: by its name, or its URL.
  space.write("app/ovis.toml", "[project]\n[dependencies]\ngreet = { git = \"" +
                                   space.url("greet") + "\", version = \"1.0\" }\n\n"
                                   "[overrides]\ngreet = { path = \"../local/greet\" }\n");
  CHECK(space.ovis("run --offline --quiet").output == "Local hello, Ana\n");
  space.write("app/ovis.toml", "[project]\n[dependencies]\ngreet = { git = \"" +
                                   space.url("greet") + "\", version = \"1.0\" }\n\n"
                                   "[overrides]\n\"" + space.url("greet") +
                                   ".git\" = { path = \"../local/greet\" }\n");
  CHECK(space.ovis("run --offline --quiet").output == "Local hello, Ana\n");
}

TEST_CASE("A package's lines are its author's to settle.", "[packages]") {
  packages space("locked");
  space.package("vague", "1.0.0", "to tell about (x):\n    show square root of x + 7\n");
  space.write("app/ovis.toml", "[project]\n[dependencies]\nvague = { git = \"" +
                                   space.url("vague") + "\", version = \"1\" }\n");
  space.write("app/main.ovis", "tell about 9\n");
  const auto ran = space.ovis("run --quiet --endpoint http://127.0.0.1:9/v1");
  CHECK(ran.status != 0);
  CHECK_THAT(ran.output, ContainsSubstring("vague@1/vague.ovis, line 2"));
  CHECK_THAT(ran.output, ContainsSubstring("in the package vague@1, whose author must "
                                           "settle it"));

  // Settled in the package (a meaning above the line), it builds, and the
  // cached files are never written.
  space.write("vague/vague.ovis", "to tell about (x):\n"
                                  "    @[meaning: show (square root of x) + 7]\n"
                                  "    show square root of x + 7\n");
  space.write("vague/ovis.toml", "[package]\nname = \"vague\"\nversion = \"1.0.1\"\n");
  space.release("vague", "1.0.1");
  space.ovis("update");
  const auto settled = space.ovis("run --quiet --offline");
  INFO(settled.output);
  CHECK(settled.status == 0);
  CHECK(settled.output == "10.0\n");
}

TEST_CASE("Two lines of one package are two packages.", "[packages]") {
  packages space("lines");
  space.package("greet", "1.0.0", "to greet (name):\n    show \"Hello,\", name\n");
  space.package("greet", "2.0.0", "to greet (name):\n    show \"Greetings,\", name\n");
  space.package("shout", "1.0.0", "to shout at (name):\n    greet name\n    show \"!!!\"\n",
                "greet = { git = \"" + space.url("greet") + "\", version = \"1\" }\n");
  space.write("app/ovis.toml", "[project]\n[dependencies]\n"
                               "greet = { git = \"" + space.url("greet") +
                                   "\", version = \"2\" }\n"
                                   "shout = { git = \"" + space.url("shout") +
                                   "\", version = \"1\" }\n");
  // Each file reads with its own dependencies' phrases: the program's
  // greet is 2.0.0's, shout's is 1.0.0's.
  space.write("app/main.ovis", "greet \"Ana\"\nshout at \"Bo\"\n");
  const auto ran = space.ovis("run --offline --quiet");
  INFO(ran.output);
  REQUIRE(ran.status == 0);
  CHECK(ran.output == "Greetings, Ana\nHello, Bo\n!!!\n");
  CHECK_THAT(space.ovis("tree").output, ContainsSubstring("(2 copies: 1.0.0 2.0.0)"));

  // A phrase of a package the program doesn't use directly isn't there.
  space.write("app/ovis.toml", "[project]\n[dependencies]\nshout = { git = \"" +
                                   space.url("shout") + "\", version = \"1\" }\n");
  space.write("app/main.ovis", "shout at \"Bo\"\ngreet \"Ana\"\n");
  const auto hidden = space.ovis("run --offline --quiet");
  CHECK(hidden.status != 0);
  CHECK_THAT(hidden.output, ContainsSubstring("main.ovis, line 2"));
}

TEST_CASE("Two lines' records are different types, said with their versions.",
          "[packages]") {
  packages space("records");
  const auto log = [](const std::string &p_fields) {
    return "an entry has:\n    words (text)\n" + p_fields +
           "\nto note (t: text):\n    e = an entry with words t\n    give back e\n";
  };
  space.package("log", "1.0.0", log(""));
  space.package("log", "2.0.0", log("    level (whole number, 1)\n"));
  space.package("csv", "1.0.0", "to read the row (t: text):\n    give back note t\n",
                "log = { git = \"" + space.url("log") + "\", version = \"1\" }\n");
  space.package("report", "1.0.0",
                "to report (e: entry):\n    show \"Report:\", e's words, e's level\n",
                "log = { git = \"" + space.url("log") + "\", version = \"2\" }\n");
  space.write("app/ovis.toml", "[project]\n[dependencies]\n"
                               "csv = { git = \"" + space.url("csv") + "\", version = \"1\" }\n"
                               "report = { git = \"" + space.url("report") +
                                   "\", version = \"1\" }\n");
  space.write("app/main.ovis", "row = read the row \"a,b\"\nshow row\nreport row\n");
  const auto mixed = space.ovis("run --offline --quiet");
  INFO(mixed.output);
  CHECK(mixed.status != 0);
  CHECK_THAT(mixed.output,
             ContainsSubstring("main.ovis, line 3: Argument 'e' of 'report (e: entry)' must "
                               "be a log 2.0.0 entry but is a log 1.0.0 entry."));
  CHECK_THAT(mixed.output, ContainsSubstring("the program has 2 versions of log"));
  CHECK_THAT(mixed.output, ContainsSubstring("1.0.0 for csv"));
  CHECK_THAT(mixed.output, ContainsSubstring("2.0.0 for report"));

  // Each kept to its own side, both build; a record prints as written.
  space.write("app/main.ovis", "row = read the row \"a,b\"\nshow row\n");
  const auto apart = space.ovis("run --offline --quiet");
  INFO(apart.output);
  CHECK(apart.status == 0);
  CHECK(apart.output == "an entry with words \"a,b\"\n");
}
