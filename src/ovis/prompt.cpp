#include <algorithm>
#include <cctype>
#include <optional>
#include <set>

#include <ovis/core/ast.hpp>
#include <ovis/english/prompt.hpp>

namespace ovis::english {

namespace {

constexpr const char_type *translation_system_prompt = R"prompt(You translate one paragraph of a program written in English into Ovis Core, a tiny Lisp-like language. The paragraph comes with numbered steps that spell out exactly what it means; follow the steps. Reply with Ovis Core code only: no explanations, no Markdown.

Types: int float bool str ptr, and lists int[] float[] bool[] str[]. Types can always be left out; they are worked out from how things are used.

Top-level forms:
(fn NAME (PARAM ...) STATEMENT...)       define a function; a PARAM is NAME or (NAME TYPE)
(fn NAME (PARAM ...) TYPE STATEMENT...)  the same with a stated return type (void for none)
(extern NAME (TYPE ...) TYPE)            declare another C function; C types: cint int size_t float str ptr void ...
(link "LIBRARY")                         link a C library
(use-c "HEADER.h" "LIBRARY")             use a C library: all functions and constants of its header become callable
STATEMENT                                any statement; runs when the program starts

Statements:
(let NAME VALUE)                 new variable; top-level ones are visible in every function
(set NAME VALUE)                 change a variable
(put LIST INDEX VALUE)           change a list item; the first item has index 0
(append LIST VALUE) (sort LIST) (reverse LIST)
(print VALUE ...)                print the values separated by spaces, then a new line
(if CONDITION (then STATEMENT...) (else STATEMENT...))   the else part is optional
(while CONDITION STATEMENT...)
(for NAME FIRST LAST STATEMENT...)   NAME counts up from FIRST to LAST, both included
(each NAME LIST STATEMENT...)        NAME takes every item of LIST in order
(break) (continue) (return VALUE) (return)
(pass)                           do nothing; use it when the paragraph only explains something
(NAME ARGUMENT ...)              call a function

Values:
42  -7  3.5  true  false  "text"  null  pi  NAME
(+ a b) (- a b) (* a b) (/ a b) (div a b) (% a b) (- a)    / always gives a float, div a whole number
(= a b) (!= a b) (< a b) (<= a b) (> a b) (>= a b) (and a b) (or a b) (not a)
(int x) (float x) (sqrt x) (pow x y) (floor x) (abs x) (min a b) (max a b)
(list 1 2 3) (list) (array int COUNT) (get LIST INDEX) (len LIST)
(+ "text" value ...) (substring TEXT START COUNT) (char_at TEXT INDEX) (to_str x)
(read_int) (read_float) (read_line) (random LOWEST HIGHEST)
(NAME ARGUMENT ...)              the result of a function
(ref NAME)                       the address of variable NAME, for C functions that write into it (e.g. (sqlite3_open path (ref db)))

Names from other files of the program can be used as they are; when two files define the same name, write FILE.NAME (e.g. tax.add_tax).

The C standard library can be called directly, for example (printf "%d items, %.2f each\n" n price), (strlen s), (toupper c), (sin x), (fopen path "r"), (fgets (buffer 100) 100 stdin), (time null).

The paragraph or its steps may be written partly in a Python-like pseudo-code; where the paragraph's own code and the steps disagree, follow the code. It means what it would mean in Python, though it need not be valid Python: indentation marks a block, def NAME(PARAMS): is (fn NAME (PARAMS) ...), for x in LIST: is (each x LIST ...), for i in range(A, B): is (for i A (- B 1) ...) because range leaves out B, range(N) counts from 0 to N - 1, // is div, ** is pow, x += 1 is (set x (+ x 1)), a new x = 1 is (let x 1), and print(a, b) is (print a b).

Use the names already defined by the rest of the program when the paragraph refers to them. Pick short snake_case names for new things.)prompt";

struct translation_example {
  const char_type *context;
  const char_type *english;
  const char_type *steps;
  const char_type *core;
};

constexpr translation_example translation_examples[] = {
    {"", "Ask the user for a number and call it n. Then show its square.",
     "1. Read a whole number from the user into n.\n"
     "2. Print n times n.",
     "(let n (read_int))\n(print (* n n))"},
    {"", "A number is prime when nothing but 1 and itself divides it.",
     "1. Define a function is_prime that takes a whole number n and gives "
     "back true or false.\n"
     "2. In is_prime: if n is less than 2, give back false.\n"
     "3. In is_prime: for every divisor d starting at 2, while d times d is "
     "at most n, give back false if n divided by d leaves no remainder.\n"
     "4. In is_prime: otherwise give back true.",
     "(fn is_prime (n)\n"
     "  (if (< n 2) (then (return false)))\n"
     "  (let d 2)\n"
     "  (while (<= (* d d) n)\n"
     "    (if (= (% n d) 0) (then (return false)))\n"
     "    (set d (+ d 1)))\n"
     "  (return true))"},
    {"(fn is_prime ((n int)) bool)",
     "Say which of the numbers 1 to 20 are prime.",
     "1. For every whole number i from 1 to 20: if is_prime of i is true, "
     "print i; otherwise print i followed by \"is not prime\".",
     "(for i 1 20\n"
     "  (if (is_prime i)\n"
     "    (then (print i))\n"
     "    (else (print i \"is not prime\"))))"},
    {"", "Show which version of zlib is installed.",
     "1. Use the C library z (header zlib.h).\n"
     "2. Print \"zlib\" followed by the text zlibVersion gives back.",
     "(use-c \"zlib.h\" \"z\")\n(print \"zlib\" (zlibVersion))"},
    {"", "The scores are 90, 72 and 85. Work out their average and show it.",
     "1. Make a list named scores holding the whole numbers 90, 72 and 85.\n"
     "2. Make a number named total, starting at 0, and add every score in "
     "scores to it.\n"
     "3. Print \"Average: \" followed by total divided by the number of "
     "scores, with two digits after the decimal point.",
     "(let scores (list 90 72 85))\n"
     "(let total 0)\n"
     "(each score scores (set total (+ total score)))\n"
     "(printf \"Average: %.2f\\n\" (/ total (len scores)))"},
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

auto user_message(string_view_type p_paragraph, string_view_type p_steps,
                  string_view_type p_context) -> string_type {
  auto result =
      string_type(p_context) + "Paragraph:\n" + string_type(p_paragraph);
  if (!p_steps.empty())
    result += "\n\nSteps:\n" + string_type(p_steps);
  return result;
}

} // namespace

// Prompting.

auto core_grammar() -> string_view_type {
  return R"gbnf(root   ::= ws form (ws form)* ws
form   ::= "(" ws (value (ws1 value)*)? ws ")"
value  ::= form | atom
atom   ::= number | string | symbol
number ::= "-"? [0-9]+ ("." [0-9]+)?
string ::= "\"" ([^"\\\n] | "\\" ["\\nt])* "\""
symbol ::= [a-zA-Z_+*/%<>=!?.-] [a-zA-Z0-9_+*/%<>=!?.\[\]-]*
ws     ::= [ \t\n]*
ws1    ::= [ \t\n]+
)gbnf";
}

auto restricted_core_grammar(const std::vector<string_type> &p_names)
    -> string_type {
  std::set<string_type> heads;
  for (const auto keyword : core::core_keywords())
    if (keyword != "fn" && keyword != "function" && keyword != "defn" &&
        keyword != "extern")
      heads.emplace(keyword);
  for (const auto name : core::builtin_names())
    heads.emplace(name);
  for (const auto &function : core::libc_functions())
    heads.insert(function.name);
  for (const auto &name : p_names)
    heads.insert(name);

  string_type alternatives;
  for (const auto &head : heads) {
    if (!alternatives.empty())
      alternatives += " | ";
    alternatives += '"';
    for (const auto character : head) {
      if (character == '"' || character == '\\')
        alternatives.push_back('\\');
      alternatives.push_back(character);
    }
    alternatives += '"';
  }

  return R"gbnf(root   ::= ws form (ws form)* ws
form   ::= fnform | extern | "(" ws head (ws1 value)* ws ")"
fnform ::= "(" ws ("fn" | "function" | "defn") ws1 symbol ws1 params (ws1 value)* ws ")"
extern ::= "(" ws "extern" ws1 symbol ws1 params ws1 value ws ")"
params ::= "(" ws (param (ws1 param)*)? ws ")"
param  ::= symbol | "(" ws symbol (ws1 value)? ws ")"
value  ::= form | atom
atom   ::= number | string | symbol
number ::= "-"? [0-9]+ ("." [0-9]+)?
string ::= "\"" ([^"\\\n] | "\\" ["\\nt])* "\""
symbol ::= [a-zA-Z_+*/%<>=!?.-] [a-zA-Z0-9_+*/%<>=!?.\[\]-]*
ws     ::= [ \t\n]*
ws1    ::= [ \t\n]+
head   ::= )gbnf" + alternatives + "\n";
}

auto translation_request(string_view_type p_paragraph,
                         string_view_type p_steps,
                         const std::vector<string_type> &p_context)
    -> std::vector<chat_message> {
  std::vector<chat_message> result = {{"system", translation_system_prompt}};
  for (const auto &item : translation_examples) {
    std::vector<string_type> context;
    if (*item.context != '\0')
      context.push_back(item.context);
    result.push_back(
        {"user", user_message(item.english, item.steps,
                              describe_context(context))});
    result.push_back({"assistant", item.core});
  }
  result.push_back({"user", user_message(p_paragraph, p_steps,
                                         describe_context(p_context))});
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


// Detail pass.

namespace {

constexpr const char_type *detail_system_prompt = R"prompt(You read one paragraph of a program that its author described in English, and spell out exactly what it asks for as concrete numbered steps, so that someone could carry them out without guessing. You do not write code.

Reply in exactly this form:
STATUS: ok
STEPS:
1. one concrete action
2. the next action
ASSUMPTIONS:
- a detail the paragraph did not state, which you had to decide
Leave out ASSUMPTIONS when you decided nothing yourself.

To use a C library, write a step "Use the C library LIBRARY (header HEADER.h)." and then call its functions by their C names.

The author may write some or all of a paragraph in a Python-like pseudo-code, mixed with English. Read it as Python: indentation marks a block, def defines a function, range(A, B) leaves out B. It need not be valid Python. Code is already exact: give each line of it one step that says the same thing in words, with the same names and numbers, and fill in only what its English parts leave open.

Do only what the paragraph asks. Never add printing, examples or other work it doesn't mention.

Each step does one thing: make a named value or list, read input, calculate, repeat steps, choose between steps, or print (say exactly what text and in what format). A reusable operation starts with "Define a function NAME that takes ... and gives back ...", and its later steps start with "In NAME:". Use the names the rest of the program already uses. Fill in small details yourself (whole number or decimal, starting values, output format) and list each one under ASSUMPTIONS.

A paragraph that only gives values (numbers, rates, names, lists) is ok: its steps make named values holding them. A short command such as "Say goodbye." is ok too, even if it is a single sentence.

If the paragraph only describes or introduces the program as a whole, gives no values and asks for nothing to happen, reply:
STATUS: note

If the paragraph asks for something but is too abstract or ambiguous to turn into steps without guessing what the author wants, reply:
STATUS: too_vague
REASON: what is missing, in one sentence
SUGGESTION: a rewrite of the paragraph that would be clear enough

If the paragraph is not about what the program does at all, reply:
STATUS: irrelevant
REASON: why, in one sentence
SUGGESTION: what to write instead, or to remove it)prompt";

struct detail_example {
  const char_type *english;
  const char_type *reply;
};

constexpr detail_example detail_examples[] = {
    {"The scores are 90, 72 and 85. Work out their average and show it.",
     "STATUS: ok\n"
     "STEPS:\n"
     "1. Make a list named scores holding the whole numbers 90, 72 and 85.\n"
     "2. Make a number named total, starting at 0, and add every score in "
     "scores to it.\n"
     "3. Print \"Average: \" followed by total divided by the number of "
     "scores, with two digits after the decimal point.\n"
     "ASSUMPTIONS:\n"
     "- \"Show\" means print it on the screen.\n"
     "- The average is shown with two decimals.\n"},
    {"A number is prime when nothing but 1 and itself divides it.",
     "STATUS: ok\n"
     "STEPS:\n"
     "1. Define a function is_prime that takes a whole number n and gives "
     "back true or false.\n"
     "2. In is_prime: if n is less than 2, give back false.\n"
     "3. In is_prime: for every divisor d starting at 2, while d times d is "
     "at most n, give back false if n divided by d leaves no remainder.\n"
     "4. In is_prime: otherwise give back true.\n"
     "ASSUMPTIONS:\n"
     "- Numbers below 2 are not prime.\n"
     "- Divisors only need checking up to the square root of n.\n"},
    {"def average(numbers):\n"
     "    add them all up and divide by how many there are\n"
     "\n"
     "print the average of 4, 8 and 15",
     "STATUS: ok\n"
     "STEPS:\n"
     "1. Define a function average that takes a list of numbers named "
     "numbers and gives back a decimal number.\n"
     "2. In average: make a number named total, starting at 0, and add "
     "every number in numbers to it.\n"
     "3. In average: give back total divided by the number of items in "
     "numbers.\n"
     "4. Print what average gives back for a list holding the whole numbers "
     "4, 8 and 15.\n"
     "ASSUMPTIONS:\n"
     "- The average is a decimal number, printed as it is.\n"},
    {"Say goodbye to the user.",
     "STATUS: ok\n"
     "STEPS:\n"
     "1. Print \"Goodbye!\".\n"
     "ASSUMPTIONS:\n"
     "- The exact words are \"Goodbye!\".\n"},
    {"Shipping costs 4.50, and orders over 50 ship for free.",
     "STATUS: ok\n"
     "STEPS:\n"
     "1. Make a decimal number named shipping_cost holding 4.50.\n"
     "2. Make a decimal number named free_shipping_limit holding 50.\n"
     "ASSUMPTIONS:\n"
     "- Both amounts are decimal numbers, since prices have cents.\n"},
    {"This program helps a teacher keep track of grades.",
     "STATUS: note\n"},
    {"Tidy up the output.",
     "STATUS: too_vague\n"
     "REASON: It doesn't say which output or what is untidy about it.\n"
     "SUGGESTION: Say exactly what to print and how, for example \"Print "
     "one result per line, with a label before each.\"\n"},
    {"Make the report look professional.",
     "STATUS: too_vague\n"
     "REASON: It doesn't say which report or what should change about how it "
     "looks.\n"
     "SUGGESTION: Say what to print and how, for example \"Print each "
     "student's name and grade on its own line, with the grades lined up in "
     "one column.\"\n"},
    {"I wrote this on a rainy Sunday afternoon.",
     "STATUS: irrelevant\n"
     "REASON: It is about the author, not about what the program should do.\n"
     "SUGGESTION: Remove this paragraph.\n"},
};

auto describe_other_paragraphs(const std::vector<string_type> &p_others)
    -> string_type {
  if (p_others.empty())
    return "The rest of the program: nothing yet.\n\n";
  string_type result = "The rest of the program already does this:\n";
  for (const auto &other : p_others)
    result += other + "\n";
  return result + "\n";
}

auto value_after(string_view_type p_line, string_view_type p_label)
    -> std::optional<string_type> {
  if (!p_line.starts_with(p_label))
    return std::nullopt;
  p_line.remove_prefix(p_label.size());
  while (!p_line.empty() && p_line.front() == ' ')
    p_line.remove_prefix(1);
  while (!p_line.empty() && (p_line.back() == ' ' || p_line.back() == '\r'))
    p_line.remove_suffix(1);
  return string_type(p_line);
}

auto lines_of(string_view_type p_text) -> std::vector<string_view_type> {
  std::vector<string_view_type> result;
  std::size_t position = 0;
  while (position < p_text.size()) {
    auto end = p_text.find('\n', position);
    if (end == string_view_type::npos)
      end = p_text.size();
    result.push_back(p_text.substr(position, end - position));
    position = end + 1;
  }
  return result;
}

} // namespace

// Every line of text is capped: a small model otherwise sometimes rambles
// on in one line until it runs out of tokens.
constexpr const char_type *detail_rules = R"gbnf(
ok          ::= "STATUS: ok\nSTEPS:\n" step+ assumptions?
step        ::= [1-9] [0-9]? ". " text "\n"
assumptions ::= "ASSUMPTIONS:\n" ("- " text "\n"){1,8}
note        ::= "STATUS: note\n"
refusal     ::= "STATUS: " ("too_vague" | "irrelevant") "\nREASON: " text "\nSUGGESTION: " text "\n"
text        ::= [^\n]{1,240}
)gbnf";

auto detail_grammar(bool p_allow_note) -> string_view_type {
  static const auto with_note =
      string_type("root        ::= ok | note | refusal") + detail_rules;
  static const auto without_note =
      string_type("root        ::= ok | refusal") + detail_rules;
  return p_allow_note ? with_note : without_note;
}

auto looks_like_code(string_view_type p_paragraph) -> bool {
  static const std::vector<string_view_type> block_keywords = {
      "def ", "for ", "while ", "if ", "elif ", "else", "with ", "try"};
  std::size_t position = 0;
  while (position <= p_paragraph.size()) {
    auto end = p_paragraph.find('\n', position);
    if (end == string_view_type::npos)
      end = p_paragraph.size();
    auto line = p_paragraph.substr(position, end - position);
    position = end + 1;
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
      line.remove_prefix(1);
    while (!line.empty() &&
           std::isspace(static_cast<unsigned char>(line.back())))
      line.remove_suffix(1);
    if (line.empty())
      continue;
    // A block header: "def f(x):", "for x in xs:", "else:".
    if (line.back() == ':' &&
        std::any_of(block_keywords.begin(), block_keywords.end(),
                    [&](auto p_keyword) { return line.starts_with(p_keyword); }))
      return true;
    // A call written without a space, "print(x)", or an assignment "x = 1".
    for (std::size_t index = 1; index < line.size(); ++index)
      if (line[index] == '(' &&
          (std::isalnum(static_cast<unsigned char>(line[index - 1])) ||
           line[index - 1] == '_'))
        return true;
    for (const auto *const operator_text : {" = ", " += ", " -= ", " == "})
      if (line.find(operator_text) != string_view_type::npos)
        return true;
  }
  return false;
}

auto may_be_note(string_view_type p_paragraph) -> bool {
  return !starts_with_command(p_paragraph) && !looks_like_code(p_paragraph);
}

auto starts_with_command(string_view_type p_paragraph) -> bool {
  // Words that may come before the command: "Finally tell ...".
  static const std::vector<string_view_type> lead_ins = {
      "finally", "then", "first", "next", "also", "now", "lastly",
      "last", "afterwards", "and", "so", "please", "second", "third"};
  static const std::vector<string_view_type> commands = {
      "add", "ask", "calculate", "check", "choose", "collect", "compare",
      "compress", "compute", "convert", "count", "create", "decide",
      "define", "display", "divide", "do", "draw", "find", "generate",
      "give", "greet", "handle", "keep", "list", "load", "make", "mention",
      "multiply", "open", "output", "pick", "print", "put", "read",
      "remember", "repeat", "report", "round", "save", "say", "send",
      "show", "sort", "store", "subtract", "sum", "tell", "use", "work",
      "write"};
  bool at_sentence_start = true;
  std::size_t position = 0;
  while (position < p_paragraph.size()) {
    const auto character = p_paragraph[position];
    if (!std::isalpha(static_cast<unsigned char>(character))) {
      if (character == '.' || character == '!' || character == '?' ||
          character == ':' || character == ';')
        at_sentence_start = true;
      ++position;
      continue;
    }
    string_type word;
    while (position < p_paragraph.size() &&
           std::isalpha(static_cast<unsigned char>(p_paragraph[position])))
      word += static_cast<char_type>(
          std::tolower(static_cast<unsigned char>(p_paragraph[position++])));
    if (!at_sentence_start)
      continue;
    if (std::find(commands.begin(), commands.end(), word) != commands.end())
      return true;
    at_sentence_start = std::find(lead_ins.begin(), lead_ins.end(), word) !=
                        lead_ins.end();
  }
  return false;
}

auto detail_request(string_view_type p_paragraph,
                    const std::vector<string_type> &p_others,
                    const std::vector<string_type> &p_previous_steps,
                    string_view_type p_notes) -> std::vector<chat_message> {
  std::vector<chat_message> result = {{"system", detail_system_prompt}};
  for (const auto &item : detail_examples) {
    result.push_back({"user", describe_other_paragraphs({}) +
                                  "Paragraph:\n" + item.english});
    result.push_back({"assistant", item.reply});
  }
  auto message = describe_other_paragraphs(p_others) + string_type(p_notes) +
                 "Paragraph:\n" + string_type(p_paragraph);
  if (!p_previous_steps.empty()) {
    message += "\n\nThe author changed this paragraph. These steps were "
               "written for the earlier version, and the author may have "
               "edited them:\n";
    for (std::size_t index = 0; index < p_previous_steps.size(); ++index)
      message += std::to_string(index + 1) + ". " + p_previous_steps[index] +
                 "\n";
    message += "Keep the steps that still fit the paragraph as it is now, "
               "with the author's wording, and drop every step and "
               "assumption for something the paragraph no longer asks for.";
  }
  result.push_back({"user", std::move(message)});
  return result;
}

// Review of edited steps.

namespace {

constexpr const char_type *review_system_prompt = R"prompt(The author of a program written in English wrote or edited the numbered steps that spell out one of its paragraphs. Check that the steps, taken together, still do what the paragraph asks. Steps may add detail the paragraph leaves open; that is fine. They must not leave out something the paragraph asks for, and must not do something the paragraph has nothing to do with.

If the steps are fine, reply with the details they decide that the paragraph does not state, in exactly this form:
STATUS: ok
ASSUMPTIONS:
- one detail
Leave out ASSUMPTIONS if there are none.

Otherwise reply:
STATUS: mismatch
REASON: what the steps leave out or add, in one sentence
SUGGESTION: how to change the steps or the paragraph so they agree)prompt";

} // namespace

auto review_grammar() -> string_view_type {
  return R"gbnf(root        ::= ok | mismatch
ok          ::= "STATUS: ok\n" assumptions?
assumptions ::= "ASSUMPTIONS:\n" ("- " text "\n"){1,8}
mismatch    ::= "STATUS: mismatch\nREASON: " text "\nSUGGESTION: " text "\n"
text        ::= [^\n]{1,240}
)gbnf";
}

auto review_request(string_view_type p_paragraph,
                    const std::vector<string_type> &p_steps,
                    const std::vector<string_type> &p_others)
    -> std::vector<chat_message> {
  string_type message = describe_other_paragraphs(p_others) +
                        "Paragraph:\n" + string_type(p_paragraph) +
                        "\n\nSteps:\n";
  for (std::size_t index = 0; index < p_steps.size(); ++index)
    message += std::to_string(index + 1) + ". " + p_steps[index] + "\n";
  return {
      {"system", review_system_prompt},
      {"user", "Paragraph:\nRead the price and print it with two decimals."
               "\n\nSteps:\n1. Read a decimal number from the user into "
               "price.\n2. Print price with two digits after the decimal "
               "point, followed by \" EUR\".\n"},
      {"assistant", "STATUS: ok\nASSUMPTIONS:\n- The price is shown in "
                    "euros.\n"},
      {"user", "Paragraph:\nRead the scores, sort them and print them.\n\n"
               "Steps:\n1. Read whole numbers from the user into scores.\n"
               "2. Print scores.\n"},
      {"assistant", "STATUS: mismatch\nREASON: The paragraph asks for the "
                    "scores to be sorted, but no step sorts them.\n"
                    "SUGGESTION: Add a step \"Sort scores from smallest to "
                    "largest.\" before printing, or remove \"sort them\" "
                    "from the paragraph.\n"},
      {"user", std::move(message)}};
}

auto parse_review(string_view_type p_reply) -> detail_result {
  detail_result result;
  bool has_status = false;
  for (auto line : lines_of(p_reply)) {
    while (!line.empty() && line.front() == ' ')
      line.remove_prefix(1);
    if (const auto status = value_after(line, "STATUS:")) {
      has_status = true;
      if (*status == "mismatch")
        result.status = detail_result::status_type::mismatch;
    } else if (const auto reason = value_after(line, "REASON:")) {
      result.reason = *reason;
    } else if (const auto suggestion = value_after(line, "SUGGESTION:")) {
      result.suggestion = *suggestion;
    } else if (line.starts_with("-")) {
      result.assumptions.push_back(*value_after(line, "-"));
    }
  }
  if (!has_status)
    throw error("The model's review of the steps could not be read:\n" +
                string_type(p_reply));
  if (result.is_refusal() && result.reason.empty())
    result.reason = "The model could not say why.";
  return result;
}

auto parse_detail(string_view_type p_reply) -> detail_result {
  detail_result result;
  bool has_status = false;
  bool in_assumptions = false;
  for (auto line : lines_of(p_reply)) {
    while (!line.empty() && line.front() == ' ')
      line.remove_prefix(1);
    if (const auto status = value_after(line, "STATUS:")) {
      has_status = true;
      if (*status == "note")
        result.status = detail_result::status_type::note;
      else if (*status == "too_vague")
        result.status = detail_result::status_type::too_vague;
      else if (*status == "irrelevant")
        result.status = detail_result::status_type::irrelevant;
      continue;
    }
    if (line.starts_with("STEPS:")) {
      in_assumptions = false;
      continue;
    }
    if (line.starts_with("ASSUMPTIONS:")) {
      in_assumptions = true;
      continue;
    }
    if (const auto reason = value_after(line, "REASON:")) {
      result.reason = *reason;
      continue;
    }
    if (const auto suggestion = value_after(line, "SUGGESTION:")) {
      result.suggestion = *suggestion;
      continue;
    }
    if (in_assumptions) {
      if (const auto assumption = value_after(line, "-"))
        result.assumptions.push_back(*assumption);
      continue;
    }
    std::size_t digits = 0;
    while (digits < line.size() &&
           std::isdigit(static_cast<unsigned char>(line[digits])))
      ++digits;
    if (digits > 0 && digits < line.size() &&
        (line[digits] == '.' || line[digits] == ')'))
      result.steps.push_back(*value_after(line.substr(digits + 1), ""));
  }

  if (!has_status && !result.steps.empty())
    has_status = true;
  if (!has_status ||
      (result.status == detail_result::status_type::ok && result.steps.empty()))
    throw error("The model's reply to the detail pass could not be read:\n" +
                string_type(p_reply));
  if (result.is_refusal() && result.reason.empty())
    result.reason = "The model could not say why.";
  return result;
}

// Explaining failures.

namespace {

constexpr const char_type *explanation_system_prompt = R"prompt(A paragraph of a program written in English could not be turned into working code. Explain to its author why, in plain English and without mentioning code, and suggest a rewrite of the paragraph that would work. Reply in exactly this form:
REASON: one or two sentences
SUGGESTION: the rewritten paragraph)prompt";

} // namespace

auto explanation_grammar() -> string_view_type {
  return R"gbnf(root ::= "REASON: " text "\nSUGGESTION: " text "\n"
text ::= [^\n]{1,600}
)gbnf";
}

auto explanation_request(string_view_type p_paragraph,
                         string_view_type p_steps, string_view_type p_core,
                         const std::vector<string_type> &p_problems)
    -> std::vector<chat_message> {
  string_type message = "Paragraph:\n" + string_type(p_paragraph);
  if (!p_steps.empty())
    message += "\n\nSteps:\n" + string_type(p_steps);
  message += "\n\nLast attempt at code:\n" + string_type(p_core) +
             "\n\nWhat was wrong with it:\n";
  for (const auto &problem : p_problems)
    message += "- " + problem + "\n";
  return {{"system", explanation_system_prompt}, {"user", std::move(message)}};
}

auto parse_explanation(string_view_type p_reply)
    -> std::pair<string_type, string_type> {
  std::pair<string_type, string_type> result;
  for (const auto line : lines_of(p_reply)) {
    if (const auto reason = value_after(line, "REASON:"))
      result.first = *reason;
    else if (const auto suggestion = value_after(line, "SUGGESTION:"))
      result.second = *suggestion;
  }
  if (result.first.empty())
    result.first = string_type(p_reply);
  return result;
}

} // namespace ovis::english
