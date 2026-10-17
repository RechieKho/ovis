#include <cctype>
#include <fstream>
#include <sstream>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <ovis/english/language_model.hpp>
#include <ovis/english/lockfile.hpp>
#include <ovis/english/paragraph.hpp>
#include <ovis/english/prompt.hpp>

namespace ovis::english {

namespace {

auto is_blank(string_view_type p_line) -> bool {
  for (const auto character : p_line)
    if (!std::isspace(static_cast<unsigned char>(character)))
      return false;
  return true;
}

constexpr const char_type *system_prompt = R"prompt(You translate one paragraph of a program written in English into Ovis Core, a tiny Lisp-like language. Reply with Ovis Core code only: no explanations, no Markdown.

Types: int, float, bool, str and lists of them: int[] float[] bool[] str[].

Top-level forms:
(fn NAME ((PARAM TYPE) ...) RETURN-TYPE STATEMENT...)  define a function; RETURN-TYPE can be void
STATEMENT                                             any statement; runs when the program starts

Statements:
(let NAME VALUE)                 new variable; top-level ones are visible in every function
(set NAME VALUE)                 change a variable
(put LIST INDEX VALUE)           change a list item; the first item has index 0
(append LIST VALUE)              add an item to the end of a list
(print VALUE ...)                print the values separated by spaces, then a new line
(if CONDITION (then STATEMENT...) (else STATEMENT...))   the else part is optional
(while CONDITION STATEMENT...)
(for NAME FIRST LAST STATEMENT...)   NAME counts up from FIRST to LAST, both included
(each NAME LIST STATEMENT...)        NAME takes every item of LIST in order
(break) (continue) (return VALUE) (return)
(pass)                           do nothing; use it when the paragraph only explains something
(NAME ARGUMENT ...)              call a function

Values:
42  -7  3.5  true  false  "text"  NAME
(+ a b) (- a b) (* a b) (/ a b) (% a b) (- a)     int divided by int gives a whole number
(= a b) (!= a b) (< a b) (<= a b) (> a b) (>= a b) (and a b) (or a b) (not a)
(int x) (float x) (sqrt x) (pow x y) (floor x) (abs x) (min a b) (max a b)
(list 1 2 3) (array int COUNT) (get LIST INDEX) (len LIST)
(read_int) (read_float) (random LOWEST HIGHEST)
(NAME ARGUMENT ...)              the result of a function

Use the names already defined by the rest of the program when the paragraph refers to them. Pick short snake_case names for new things.)prompt";

struct example {
  const char_type *context;
  const char_type *english;
  const char_type *core;
};

constexpr example examples[] = {
    {"",
     "Ask the user for a number and call it n. Then show the square of n.",
     "(let n (read_int))\n(print (* n n))"},
    {"",
     "To check whether a number is prime: numbers below 2 are not prime. "
     "Otherwise try every divisor starting from 2 while its square is not "
     "bigger than the number; if one of them divides the number evenly it "
     "isn't prime. If none does, it is prime.",
     "(fn is_prime ((n int)) bool\n"
     "  (if (< n 2) (then (return false)))\n"
     "  (let divisor 2)\n"
     "  (while (<= (* divisor divisor) n)\n"
     "    (if (= (% n divisor) 0) (then (return false)))\n"
     "    (set divisor (+ divisor 1)))\n"
     "  (return true))"},
    {"(fn is_prime ((n int)) bool)",
     "Go through the numbers 1 to 20 and print the ones that are prime, "
     "otherwise say the number is not prime.",
     "(for i 1 20\n"
     "  (if (is_prime i)\n"
     "    (then (print i))\n"
     "    (else (print i \"is not prime\"))))"},
    {"",
     "The scores are 90, 72 and 85. Work out their average and show it as "
     "\"Average: ...\".",
     "(let scores (list 90 72 85))\n"
     "(let total 0)\n"
     "(each score scores (set total (+ total score)))\n"
     "(print \"Average:\" (/ (float total) (len scores)))"},
};

auto describe_context(const std::vector<string_type> &p_context)
    -> string_type {
  if (p_context.empty())
    return "Already defined by the rest of the program: nothing yet.\n\n";
  string_type result = "Already defined by the rest of the program:\n";
  for (const auto &declaration : p_context)
    result += declaration + "\n";
  return result + "\n";
}

auto user_message(string_view_type p_paragraph, string_view_type p_context)
    -> string_type {
  return string_type(p_context) + "Paragraph:\n" + string_type(p_paragraph);
}

} // namespace

// Paragraphs.

auto split_paragraphs(string_view_type p_source) -> std::vector<paragraph> {
  std::vector<paragraph> result;
  std::optional<paragraph> current;
  std::size_t line_number = 0;
  std::size_t position = 0;
  while (position <= p_source.size()) {
    auto end = p_source.find('\n', position);
    if (end == string_view_type::npos)
      end = p_source.size();
    const auto line = p_source.substr(position, end - position);
    ++line_number;
    position = end + 1;

    if (is_blank(line)) {
      if (current)
        result.push_back(std::move(*current));
      current.reset();
      continue;
    }
    if (!current)
      current = paragraph{line_number, {}};
    else
      current->text.push_back('\n');
    current->text += line;
    if (!current->text.empty() && current->text.back() == '\r')
      current->text.pop_back();
  }
  if (current)
    result.push_back(std::move(*current));
  return result;
}

auto normalize(string_view_type p_text) -> string_type {
  string_type result;
  bool in_space = false;
  for (const auto character : p_text) {
    if (std::isspace(static_cast<unsigned char>(character))) {
      in_space = true;
      continue;
    }
    if (in_space && !result.empty())
      result.push_back(' ');
    in_space = false;
    result.push_back(character);
  }
  return result;
}

// Lockfile.

auto lockfile::path_for(const std::filesystem::path &p_source)
    -> std::filesystem::path {
  auto result = p_source;
  result += ".lock";
  return result;
}

auto lockfile::load(const std::filesystem::path &p_path) -> lockfile {
  lockfile result;
  std::ifstream input(p_path);
  if (!input)
    return result;
  try {
    const auto document = nlohmann::json::parse(input);
    result.model = document.value("model", "");
    for (const auto &item : document.at("paragraphs"))
      result.entries.push_back(
          {item.at("english").get<string_type>(),
           item.at("core").get<string_type>()});
  } catch (const nlohmann::json::exception &p_error) {
    throw error("Cannot read " + p_path.string() + ": " + p_error.what());
  }
  return result;
}

auto lockfile::save(const std::filesystem::path &p_path) const -> void {
  nlohmann::ordered_json document;
  document["ovis_lock_version"] = 1;
  document["model"] = model;
  document["paragraphs"] = nlohmann::ordered_json::array();
  for (const auto &item : entries)
    document["paragraphs"].push_back(
        {{"english", item.english}, {"core", item.core}});
  std::ofstream output(p_path);
  if (!output)
    throw error("Cannot write " + p_path.string() + ".");
  output << document.dump(2) << '\n';
}

auto lockfile::find(string_view_type p_english) const
    -> std::optional<string_type> {
  const auto key = normalize(p_english);
  for (const auto &item : entries)
    if (normalize(item.english) == key)
      return item.core;
  return std::nullopt;
}

// Prompting.

auto core_grammar() -> string_view_type {
  return R"gbnf(root   ::= ws form (ws form)* ws
form   ::= "(" ws (value (ws1 value)*)? ws ")"
value  ::= form | atom
atom   ::= number | string | symbol
number ::= "-"? [0-9]+ ("." [0-9]+)?
string ::= "\"" ([^"\\\n] | "\\" ["\\nt])* "\""
symbol ::= [a-zA-Z_+*/%<>=!-] [a-zA-Z0-9_+*/%<>=!?\[\]-]*
ws     ::= [ \t\n]*
ws1    ::= [ \t\n]+
)gbnf";
}

auto translation_request(string_view_type p_paragraph,
                         const std::vector<string_type> &p_context)
    -> std::vector<chat_message> {
  std::vector<chat_message> result = {{"system", system_prompt}};
  for (const auto &item : examples) {
    std::vector<string_type> context;
    if (*item.context != '\0')
      context.push_back(item.context);
    result.push_back({"user", user_message(item.english,
                                           describe_context(context))});
    result.push_back({"assistant", item.core});
  }
  result.push_back(
      {"user", user_message(p_paragraph, describe_context(p_context))});
  return result;
}

auto repair_request(std::vector<chat_message> p_request,
                    string_view_type p_previous_core,
                    const std::vector<string_type> &p_problems)
    -> std::vector<chat_message> {
  string_type feedback = "That Ovis Core code has problems:\n";
  for (const auto &problem : p_problems)
    feedback += "- " + problem + "\n";
  feedback += "Reply with the corrected Ovis Core code for the whole "
              "paragraph, nothing else.";
  p_request.push_back({"assistant", string_type(p_previous_core)});
  p_request.push_back({"user", std::move(feedback)});
  return p_request;
}

auto extract_core(string_view_type p_reply) -> string_type {
  string_view_type text = p_reply;
  if (const auto fence = text.find("```"); fence != string_view_type::npos) {
    text.remove_prefix(fence + 3);
    // Drop the language tag after the opening fence.
    if (const auto newline = text.find('\n'); newline != string_view_type::npos)
      text.remove_prefix(newline + 1);
    if (const auto closing = text.find("```"); closing != string_view_type::npos)
      text = text.substr(0, closing);
  }
  const auto first = text.find('(');
  const auto last = text.rfind(')');
  if (first == string_view_type::npos || last == string_view_type::npos ||
      last < first)
    return string_type(text);
  return string_type(text.substr(first, last - first + 1));
}

// Endpoint model.

auto endpoint_model::chat(const std::vector<chat_message> &p_messages)
    -> string_type {
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

  nlohmann::json request = {
      {"model", options.model},
      {"temperature", 0},
      {"max_tokens", options.max_tokens},
      {"messages", nlohmann::json::array()},
  };
  for (const auto &message : p_messages)
    request["messages"].push_back(
        {{"role", message.role}, {"content", message.content}});
  if (options.use_grammar)
    request["grammar"] = core_grammar();

  httplib::Client client(host);
  client.set_connection_timeout(10);
  client.set_read_timeout(options.timeout_seconds);
  httplib::Headers headers;
  if (!options.api_key.empty())
    headers.emplace("Authorization", "Bearer " + options.api_key);

  const auto response =
      client.Post(path, headers, request.dump(), "application/json");
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
  try {
    const auto reply = nlohmann::json::parse(response->body);
    return reply.at("choices").at(0).at("message").at("content");
  } catch (const nlohmann::json::exception &p_error) {
    throw error("Unexpected reply from the language model: " +
                string_type(p_error.what()));
  }
}

} // namespace ovis::english
