#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Program.h>

#include <ovis/core/c_import.hpp>

#ifdef OVIS_HAS_C_IMPORT
#include <clang-c/Index.h>
#endif

namespace ovis::core {

namespace {

auto read_whole_file(const std::filesystem::path &p_path) -> string_type {
  std::ifstream input(p_path, std::ios::binary);
  std::stringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

auto fnv_hash(string_view_type p_text) -> string_type {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const auto character : p_text)
    hash = (hash ^ static_cast<unsigned char>(character)) * 1099511628211ULL;
  char_type text[17];
  std::snprintf(text, sizeof(text), "%016llx",
                static_cast<unsigned long long>(hash));
  return text;
}

auto split_words(string_view_type p_text) -> std::vector<string_type> {
  std::vector<string_type> result;
  std::istringstream input{string_type(p_text)};
  for (string_type word; input >> word;)
    result.push_back(word);
  return result;
}

// Runs a program and returns what it printed, or nothing if it failed.
auto run_and_capture(const string_type &p_program,
                     const std::vector<string_type> &p_arguments)
    -> std::optional<string_type> {
  const auto found = llvm::sys::findProgramByName(p_program);
  if (!found)
    return std::nullopt;
  llvm::SmallString<128> output_path;
  if (llvm::sys::fs::createTemporaryFile("ovis-capture", "txt", output_path))
    return std::nullopt;
  std::vector<llvm::StringRef> arguments = {*found};
  for (const auto &argument : p_arguments)
    arguments.push_back(argument);
  const std::optional<llvm::StringRef> redirects[] = {
      llvm::StringRef(), llvm::StringRef(output_path), llvm::StringRef()};
  const auto status =
      llvm::sys::ExecuteAndWait(*found, arguments, std::nullopt, redirects);
  auto text = read_whole_file(output_path.str().str());
  llvm::sys::fs::remove(output_path);
  if (status != 0)
    return std::nullopt;
  return text;
}

auto c_type_name(c_type p_type) -> string_view_type {
  switch (p_type) {
  case c_type::void_type:
    return "void";
  case c_type::int8_type:
    return "int8";
  case c_type::uint8_type:
    return "uint8";
  case c_type::int16_type:
    return "int16";
  case c_type::uint16_type:
    return "uint16";
  case c_type::int_type:
    return "cint";
  case c_type::uint_type:
    return "uint";
  case c_type::long_type:
    return "int";
  case c_type::size_type:
    return "size_t";
  case c_type::double_type:
    return "float";
  case c_type::float_type:
    return "cfloat";
  case c_type::string_type:
    return "str";
  case c_type::pointer_type:
    return "ptr";
  }
  return "?";
}

// Lowercase words in a name: `sqlite3_open_v2` → sqlite3 open v2,
// `zlibVersion` → zlib version.
auto name_words(string_view_type p_name) -> std::vector<string_type> {
  std::vector<string_type> result;
  string_type word;
  const auto flush = [&]() {
    if (!word.empty())
      result.push_back(word);
    word.clear();
  };
  for (std::size_t index = 0; index < p_name.size(); ++index) {
    const auto character = static_cast<unsigned char>(p_name[index]);
    if (!std::isalnum(character)) {
      flush();
      continue;
    }
    if (std::isupper(character) && index > 0 &&
        std::islower(static_cast<unsigned char>(p_name[index - 1])))
      flush();
    word.push_back(static_cast<char_type>(std::tolower(character)));
  }
  flush();
  return result;
}

auto text_words(string_view_type p_text) -> std::set<string_type> {
  std::set<string_type> result;
  for (const auto &word : name_words(p_text))
    if (word.size() >= 3)
      result.insert(word);
  return result;
}

#ifdef OVIS_HAS_C_IMPORT

auto to_string(CXString p_text) -> string_type {
  const auto *const characters = clang_getCString(p_text);
  string_type result = characters != nullptr ? characters : "";
  clang_disposeString(p_text);
  return result;
}

auto scalar_type(CXType p_type) -> std::optional<c_type> {
  switch (clang_getCanonicalType(p_type).kind) {
  case CXType_Void:
    return c_type::void_type;
  case CXType_Bool:
  case CXType_UChar:
  case CXType_Char_U:
    return c_type::uint8_type;
  case CXType_SChar:
  case CXType_Char_S:
    return c_type::int8_type;
  case CXType_Short:
    return c_type::int16_type;
  case CXType_UShort:
    return c_type::uint16_type;
  case CXType_Int:
    return c_type::int_type;
  case CXType_UInt:
    return c_type::uint_type;
  case CXType_Long:
  case CXType_LongLong:
  case CXType_ULong:
  case CXType_ULongLong:
    return clang_Type_getSizeOf(p_type) == 8 ? c_type::long_type
                                              : c_type::int_type;
  case CXType_Float:
    return c_type::float_type;
  case CXType_Double:
    return c_type::double_type;
  case CXType_Enum:
    return clang_Type_getSizeOf(p_type) == 8 ? c_type::long_type
                                              : c_type::int_type;
  default:
    return std::nullopt;
  }
}

struct mapped_type {
  c_type type;
  std::optional<c_type> pointee;
};

auto is_character(CXTypeKind p_kind) -> bool {
  return p_kind == CXType_Char_S || p_kind == CXType_Char_U ||
         p_kind == CXType_SChar || p_kind == CXType_UChar;
}

auto map_type(CXType p_type) -> std::optional<mapped_type> {
  const auto canonical = clang_getCanonicalType(p_type);
  if (canonical.kind == CXType_Pointer ||
      canonical.kind == CXType_ConstantArray ||
      canonical.kind == CXType_IncompleteArray) {
    const auto pointee = clang_getCanonicalType(
        canonical.kind == CXType_Pointer ? clang_getPointeeType(canonical)
                                         : clang_getArrayElementType(canonical));
    if (is_character(pointee.kind))
      return mapped_type{c_type::string_type, std::nullopt};
    if (pointee.kind == CXType_Pointer) {
      const auto inner =
          clang_getCanonicalType(clang_getPointeeType(pointee)).kind;
      return mapped_type{c_type::pointer_type,
                         is_character(inner) ? c_type::string_type
                                             : c_type::pointer_type};
    }
    const auto scalar = scalar_type(pointee);
    if (scalar && *scalar != c_type::void_type)
      return mapped_type{c_type::pointer_type, scalar};
    // void *, struct handles, callbacks: only a pointer or null can be passed.
    return mapped_type{c_type::pointer_type, std::nullopt};
  }
  if (const auto scalar = scalar_type(canonical))
    return mapped_type{*scalar, std::nullopt};
  return std::nullopt;
}

auto describe_c_type(CXType p_type) -> string_type {
  return to_string(clang_getTypeSpelling(p_type));
}

auto first_sentence(string_type p_comment) -> string_type {
  // Strip comment markers and collapse whitespace.
  string_type text;
  for (const auto character : p_comment)
    text.push_back(character == '*' || character == '/' ? ' ' : character);
  auto words = split_words(text);
  string_type result;
  for (const auto &word : words) {
    if (result.size() + word.size() > 120)
      break;
    if (!result.empty())
      result.push_back(' ');
    result += word;
    if (word.ends_with('.'))
      break;
  }
  return result;
}

auto parse_number(const std::vector<string_type> &p_tokens)
    -> std::optional<std::pair<bool, double>> {
  // Accepts N, -N, (N) and (-N); returns (is_real, value).
  std::vector<string_type> tokens = p_tokens;
  if (tokens.size() >= 2 && tokens.front() == "(" && tokens.back() == ")")
    tokens = {tokens.begin() + 1, tokens.end() - 1};
  bool negative = false;
  if (tokens.size() == 2 && tokens[0] == "-") {
    negative = true;
    tokens.erase(tokens.begin());
  }
  if (tokens.size() != 1)
    return std::nullopt;
  auto literal = tokens[0];
  if (literal.empty() ||
      !(std::isdigit(static_cast<unsigned char>(literal[0])) ||
        literal[0] == '.'))
    return std::nullopt;
  const bool is_hex = literal.starts_with("0x") || literal.starts_with("0X");
  const bool is_real =
      !is_hex && literal.find_first_of(".eE") != string_type::npos;
  while (!literal.empty() && string_view_type(is_real ? "fFlL" : "uUlL")
                                     .contains(literal.back()))
    literal.pop_back();
  char_type *end = nullptr;
  double value = is_real
                     ? std::strtod(literal.c_str(), &end)
                     : static_cast<double>(std::strtoull(literal.c_str(), &end, 0));
  if (end == nullptr || *end != '\0')
    return std::nullopt;
  return std::pair{is_real, negative ? -value : value};
}

struct import_state {
  CXTranslationUnit unit;
  CXFile header;
  c_library *library;
  std::set<string_type> seen;
  // The complete structs and unions the header defines, by their
  // declaration's USR: the name Ovis calls each (its tag, or the typedef's
  // for an unnamed one).
  std::map<string_type, string_type> record_names;
  // Structs declared elsewhere that a function or field reaches (`struct
  // tm` for `localtime`), to import after the header's own.
  std::vector<CXCursor> reached;
  // The directory of the library's own headers, if it isn't the system's.
  std::optional<std::filesystem::path> beside;
  // Function-like macros, read once the functions they call are known.
  std::vector<std::vector<string_type>> macros;
};

// The C record a type is: a struct the header defines, or one it reaches
// from another header (named, and imported later).
auto record_name(import_state &p_state, CXType p_type)
    -> std::optional<string_type> {
  const auto canonical = clang_getCanonicalType(p_type);
  if (canonical.kind != CXType_Record)
    return std::nullopt;
  const auto declaration = clang_getTypeDeclaration(canonical);
  const auto usr = to_string(clang_getCursorUSR(declaration));
  const auto found = p_state.record_names.find(usr);
  if (found != p_state.record_names.end())
    return found->second;
  const auto definition = clang_getCursorDefinition(declaration);
  if (clang_Cursor_isNull(definition))
    return std::nullopt; // incomplete: a handle
  auto name = to_string(clang_getCursorSpelling(definition));
  if (clang_Cursor_isAnonymous(definition) || name.empty()) {
    // `typedef struct { … } div_t;`: the typedef that names it.
    if (clang_getTypeDeclaration(p_type).kind == CXCursor_TypedefDecl)
      name = to_string(clang_getCursorSpelling(clang_getTypeDeclaration(p_type)));
    else
      return std::nullopt;
  }
  if (name.empty() || name.find(' ') != string_type::npos)
    return std::nullopt;
  p_state.record_names.emplace(usr, name);
  p_state.reached.push_back(definition);
  return name;
}

// A pointer to a function: its C phrase signature (`(i32 ptr) i32`), when
// every type in it is one Ovis passes.
auto c_phrase_of(CXType p_type) -> std::optional<string_type> {
  const auto canonical = clang_getCanonicalType(p_type);
  if (canonical.kind != CXType_Pointer)
    return std::nullopt;
  const auto function = clang_getCanonicalType(clang_getPointeeType(canonical));
  if (function.kind != CXType_FunctionProto &&
      function.kind != CXType_FunctionNoProto)
    return std::nullopt;
  const auto result = map_type(clang_getResultType(function));
  if (!result || clang_isFunctionTypeVariadic(function))
    return std::nullopt;
  std::vector<c_type> parameters;
  const auto count = clang_getNumArgTypes(function);
  for (int index = 0; index < count; ++index) {
    const auto mapped =
        map_type(clang_getArgType(function, static_cast<unsigned>(index)));
    if (!mapped)
      return std::nullopt;
    parameters.push_back(mapped->type);
  }
  return c_phrase_type(parameters, result->type).pointee;
}

// A pointer to a struct the header defines.
auto pointed_record(import_state &p_state, CXType p_type)
    -> std::optional<string_type> {
  const auto canonical = clang_getCanonicalType(p_type);
  if (canonical.kind != CXType_Pointer)
    return std::nullopt;
  return record_name(p_state, clang_getPointeeType(canonical));
}

// The header itself, or one of the library's own headers beside it (not
// the system's: those would bring all of libc).
auto is_in_header(const import_state &p_state, CXCursor p_cursor) -> bool {
  CXFile file = nullptr;
  clang_getSpellingLocation(clang_getCursorLocation(p_cursor), &file, nullptr,
                            nullptr, nullptr);
  if (file == nullptr)
    return false;
  if (clang_File_isEqual(file, p_state.header))
    return true;
  if (!p_state.beside)
    return false;
  std::error_code ignored;
  const auto path = std::filesystem::weakly_canonical(
      to_string(clang_getFileName(file)), ignored);
  return path.parent_path() == *p_state.beside;
}

auto import_function(import_state &p_state, CXCursor p_cursor) -> void {
  auto name = to_string(clang_getCursorSpelling(p_cursor));
  if (name.starts_with("_") || !p_state.seen.insert(name).second)
    return;
  c_function function{.name = name};
  auto result = map_type(clang_getCursorResultType(p_cursor));
  // A struct given back by value: copied into a new record by a wrapper.
  if (!result)
    if (const auto by_value =
            record_name(p_state, clang_getCursorResultType(p_cursor))) {
      result = mapped_type{c_type::pointer_type, std::nullopt};
      function.result_by_value = true;
      function.result_record = *by_value;
    }
  if (!result) {
    p_state.library->skipped.push_back(
        name + ": returns " +
        describe_c_type(clang_getCursorResultType(p_cursor)) +
        ", which Ovis can't use yet");
    return;
  }
  function.result = result->type;
  if (!function.result_by_value)
    function.result_record =
        pointed_record(p_state, clang_getCursorResultType(p_cursor)).value_or("");
  function.result_phrase =
      c_phrase_of(clang_getCursorResultType(p_cursor)).value_or("");
  function.result_spelling =
      describe_c_type(clang_getCursorResultType(p_cursor));
  const auto count = clang_Cursor_getNumArguments(p_cursor);
  for (int index = 0; index < count; ++index) {
    const auto type = clang_getCursorType(
        clang_Cursor_getArgument(p_cursor, static_cast<unsigned>(index)));
    auto mapped = map_type(type);
    const auto by_value = mapped ? std::nullopt : record_name(p_state, type);
    if (by_value)
      mapped = mapped_type{c_type::pointer_type, std::nullopt};
    if (!mapped) {
      p_state.library->skipped.push_back(
          name + ": takes " + describe_c_type(type) +
          ", which Ovis can't pass yet");
      return;
    }
    function.parameters.push_back(mapped->type);
    function.pointees.push_back(mapped->pointee);
    function.records.push_back(
        by_value ? *by_value : pointed_record(p_state, type).value_or(""));
    function.by_value.push_back(by_value.has_value());
    function.phrases.push_back(c_phrase_of(type).value_or(""));
    function.spellings.push_back(describe_c_type(type));
  }
  function.is_variadic =
      clang_isFunctionTypeVariadic(clang_getCursorType(p_cursor)) != 0;
  if (function.is_variadic && name.find("printf") != string_type::npos &&
      !function.parameters.empty() &&
      function.parameters.back() == c_type::string_type)
    function.format_index = function.parameters.size() - 1;
  function.comment =
      first_sentence(to_string(clang_Cursor_getRawCommentText(p_cursor)));
  p_state.library->functions.push_back(std::move(function));
}

// `#define deflateInit(strm, level) deflateInit_((strm), (level), …)`: a
// macro that is one call of a function the header has, each of its
// parameters given as one of the call's arguments, becomes a function of
// those arguments' types, called through a wrapper that expands it.
auto import_function_macro(import_state &p_state,
                           const std::vector<string_type> &p_tokens) -> void {
  // NAME ( P , P ) CALLEE ( ARGS )
  if (p_tokens.size() < 6 || p_tokens[1] != "(")
    return;
  const auto &name = p_tokens[0];
  std::vector<string_type> parameters;
  std::size_t at = 2;
  for (; at < p_tokens.size() && p_tokens[at] != ")"; ++at)
    if (p_tokens[at] != ",")
      parameters.push_back(p_tokens[at]);
  ++at;
  if (at + 2 >= p_tokens.size() || p_tokens[at + 1] != "(" ||
      p_tokens.back() != ")" || name.starts_with("_") ||
      p_state.seen.contains(name))
    return;
  const auto callee = std::ranges::find(p_state.library->functions,
                                        p_tokens[at], &c_function::name);
  if (callee == p_state.library->functions.end() || callee->is_variadic)
    return;
  // The call's arguments, split at commas outside brackets.
  std::vector<std::vector<string_type>> arguments(1);
  int depth = 0;
  for (auto index = at + 2; index + 1 < p_tokens.size(); ++index) {
    const auto &item = p_tokens[index];
    depth += item == "(" ? 1 : item == ")" ? -1 : 0;
    if (item == "," && depth == 0)
      arguments.emplace_back();
    else
      arguments.back().push_back(item);
  }
  if (depth != 0 || arguments.size() != callee->parameters.size())
    return;
  // Each parameter is an argument by itself (in brackets, maybe cast).
  const auto argument_of = [&](const string_type &p_parameter)
      -> std::optional<std::size_t> {
    for (std::size_t index = 0; index < arguments.size(); ++index) {
      auto words = arguments[index];
      std::erase(words, "(");
      std::erase(words, ")");
      if (!words.empty() && words.back() == p_parameter &&
          std::ranges::count(arguments[index], p_parameter) == 1)
        return index;
    }
    return std::nullopt;
  };
  c_function function{.name = name,
                      .result = callee->result,
                      .comment = callee->comment,
                      .result_record = callee->result_record,
                      .result_phrase = callee->result_phrase,
                      .result_spelling = callee->result_spelling,
                      .is_macro = true};
  for (const auto &parameter : parameters) {
    const auto index = argument_of(parameter);
    if (!index || callee->by_value[*index])
      return;
    function.parameters.push_back(callee->parameters[*index]);
    function.pointees.push_back(callee->pointees[*index]);
    function.records.push_back(callee->records[*index]);
    function.phrases.push_back(callee->phrases[*index]);
    function.by_value.push_back(false);
    function.spellings.push_back(callee->spellings[*index]);
  }
  p_state.seen.insert(name);
  p_state.library->functions.push_back(std::move(function));
}

auto import_macro(import_state &p_state, CXCursor p_cursor) -> void {
  if (clang_Cursor_isMacroFunctionLike(p_cursor)) {
    CXToken *tokens = nullptr;
    unsigned count = 0;
    clang_tokenize(p_state.unit, clang_getCursorExtent(p_cursor), &tokens,
                   &count);
    std::vector<string_type> spellings;
    for (unsigned index = 0; index < count; ++index)
      spellings.push_back(
          to_string(clang_getTokenSpelling(p_state.unit, tokens[index])));
    clang_disposeTokens(p_state.unit, tokens, count);
    p_state.macros.push_back(std::move(spellings));
    return;
  }
  CXToken *tokens = nullptr;
  unsigned count = 0;
  clang_tokenize(p_state.unit, clang_getCursorExtent(p_cursor), &tokens,
                 &count);
  std::vector<string_type> spellings;
  for (unsigned index = 0; index < count; ++index)
    spellings.push_back(
        to_string(clang_getTokenSpelling(p_state.unit, tokens[index])));
  clang_disposeTokens(p_state.unit, tokens, count);
  if (spellings.size() < 2 || spellings[0].starts_with("_") ||
      !p_state.seen.insert(spellings[0]).second)
    return;
  const auto number =
      parse_number({spellings.begin() + 1, spellings.end()});
  if (!number)
    return;
  c_constant constant{.name = spellings[0]};
  if (number->first) {
    constant.kind = c_constant::kind_type::real;
    constant.real = number->second;
  } else {
    constant.integer = static_cast<max_int_type>(number->second);
  }
  p_state.library->constants.push_back(std::move(constant));
}

// A struct or union's fields as a C record's. Fields Ovis can't read
// (bitfields, arrays other than of characters) are left out and listed.
auto import_record(import_state &p_state, CXCursor p_cursor,
                   const string_type &p_name) -> void {
  if (!p_state.seen.insert("struct " + p_name).second)
    return;
  const auto type = clang_getCursorType(p_cursor);
  record result{.name = p_name,
                .is_c = true,
                .size = static_cast<std::uint64_t>(clang_Type_getSizeOf(type))};
  struct fields_state {
    import_state *state;
    record *result;
  } walk{&p_state, &result};
  clang_visitChildren(
      p_cursor,
      [](CXCursor p_field, CXCursor, CXClientData p_data) -> CXChildVisitResult {
        auto &walk = *static_cast<fields_state *>(p_data);
        if (clang_getCursorKind(p_field) != CXCursor_FieldDecl)
          return CXChildVisit_Continue;
        const auto name = to_string(clang_getCursorSpelling(p_field));
        const auto field_type = clang_getCursorType(p_field);
        const auto skip = [&](const string_type &p_why) {
          walk.state->library->skipped.push_back(walk.result->name + "." + name +
                                                 ": " + p_why);
          return CXChildVisit_Continue;
        };
        if (name.empty())
          return skip("an unnamed member");
        if (clang_Cursor_isBitField(p_field))
          return skip("a bitfield, which Ovis can't read yet");
        record_field field{
            .name = name,
            .offset = static_cast<std::uint64_t>(
                clang_Cursor_getOffsetOfField(p_field) / 8)};
        const auto canonical = clang_getCanonicalType(field_type);
        if (canonical.kind == CXType_ConstantArray &&
            is_character(clang_getCanonicalType(
                             clang_getArrayElementType(canonical)).kind)) {
          field.type = value_type::string();
          field.text_length =
              static_cast<std::uint64_t>(clang_getNumElements(canonical));
        } else if (const auto inner = record_name(*walk.state, field_type)) {
          field.type = value_type::record_of(*inner);
          field.inline_record = true;
        } else if (const auto pointed = pointed_record(*walk.state, field_type)) {
          field.type = value_type::record_of(*pointed);
          field.storage = c_type::pointer_type;
        } else if (const auto mapped = map_type(field_type);
                   mapped && canonical.kind != CXType_ConstantArray &&
                   canonical.kind != CXType_IncompleteArray) {
          field.type = ovis_type_of(mapped->type);
          field.storage = mapped->type;
        } else {
          return skip("a " + describe_c_type(field_type) +
                      ", which Ovis can't read yet");
        }
        walk.result->fields.push_back(std::move(field));
        return CXChildVisit_Continue;
      },
      &walk);
  p_state.library->records.push_back(std::move(result));
}

// First, the names of the structs the header defines, so a field or a
// function may name one declared further down.
auto name_records(CXCursor p_cursor, CXCursor, CXClientData p_data)
    -> CXChildVisitResult {
  auto &state = *static_cast<import_state *>(p_data);
  if (!is_in_header(state, p_cursor))
    return CXChildVisit_Continue;
  const auto kind = clang_getCursorKind(p_cursor);
  if ((kind == CXCursor_StructDecl || kind == CXCursor_UnionDecl) &&
      clang_isCursorDefinition(p_cursor) && !clang_Cursor_isAnonymous(p_cursor)) {
    const auto name = to_string(clang_getCursorSpelling(p_cursor));
    if (!name.empty() && !name.starts_with("_"))
      state.record_names.try_emplace(to_string(clang_getCursorUSR(p_cursor)),
                                     name);
  }
  if (kind == CXCursor_TypedefDecl) {
    // `typedef struct { … } Vector2;`: the typedef names it.
    const auto underlying = clang_getCanonicalType(
        clang_getTypedefDeclUnderlyingType(p_cursor));
    const auto declaration = clang_getTypeDeclaration(underlying);
    // `typedef struct z_stream_s { … } z_stream;`: the name C code uses.
    const auto definition = clang_getCursorDefinition(declaration);
    if (underlying.kind == CXType_Record && !clang_Cursor_isNull(definition) &&
        is_in_header(state, definition))
      state.record_names.insert_or_assign(
          to_string(clang_getCursorUSR(definition)),
          to_string(clang_getCursorSpelling(p_cursor)));
  }
  return CXChildVisit_Continue;
}

auto visit(CXCursor p_cursor, CXCursor, CXClientData p_data)
    -> CXChildVisitResult {
  auto &state = *static_cast<import_state *>(p_data);
  if (!is_in_header(state, p_cursor))
    return CXChildVisit_Continue;
  switch (clang_getCursorKind(p_cursor)) {
  case CXCursor_FunctionDecl:
    import_function(state, p_cursor);
    break;
  case CXCursor_MacroDefinition:
    import_macro(state, p_cursor);
    break;
  case CXCursor_EnumDecl:
    return CXChildVisit_Recurse;
  case CXCursor_StructDecl:
  case CXCursor_UnionDecl:
  case CXCursor_TypedefDecl: {
    const auto declaration =
        clang_getCursorKind(p_cursor) == CXCursor_TypedefDecl
            ? clang_getTypeDeclaration(clang_getCanonicalType(
                  clang_getTypedefDeclUnderlyingType(p_cursor)))
            : p_cursor;
    if (!clang_isCursorDefinition(declaration))
      break;
    const auto found = state.record_names.find(
        to_string(clang_getCursorUSR(declaration)));
    if (found != state.record_names.end())
      import_record(state, declaration, found->second);
    break;
  }
  case CXCursor_EnumConstantDecl: {
    auto name = to_string(clang_getCursorSpelling(p_cursor));
    if (state.seen.insert(name).second)
      state.library->constants.push_back(
          {.name = name,
           .integer = clang_getEnumConstantDeclValue(p_cursor)});
    break;
  }
  default:
    break;
  }
  return CXChildVisit_Continue;
}

// What libclang needs to find the compiler's own headers (`stddef.h`) and
// the system's. Homebrew's libclang on macOS knows neither: it gets the
// resource directory of the clang beside it (or the one on the PATH) and
// the SDK (`xcrun --show-sdk-path`). Worked out once.
auto platform_flags() -> const std::vector<string_type> & {
  static const std::vector<string_type> flags = [] {
    std::vector<string_type> result;
#ifdef __APPLE__
#ifdef OVIS_CLANG_RESOURCE_DIR
    if (std::error_code ignored;
        std::filesystem::exists(OVIS_CLANG_RESOURCE_DIR "/include", ignored))
      result.emplace_back("-resource-dir=" OVIS_CLANG_RESOURCE_DIR);
#endif
    const auto trimmed = [](string_type p_text) {
      while (!p_text.empty() && (p_text.back() == '\n' || p_text.back() == ' '))
        p_text.pop_back();
      return p_text;
    };
    if (result.empty())
      if (const auto directory = run_and_capture("clang", {"-print-resource-dir"}))
        if (const auto path = trimmed(*directory); !path.empty())
          result.push_back("-resource-dir=" + path);
    if (const auto sdk = run_and_capture("xcrun", {"--show-sdk-path"}))
      if (const auto path = trimmed(*sdk); !path.empty()) {
        result.emplace_back("-isysroot");
        result.push_back(path);
      }
#endif
    return result;
  }();
  return flags;
}

auto parse_header(c_library &p_library,
                  const std::vector<string_type> &p_include_flags) -> void {
  const auto header = p_library.header_path.string();
  const auto source = "#include \"" + header + "\"\n";
  CXUnsavedFile unsaved{"ovis_import.c", source.c_str(),
                        static_cast<unsigned long>(source.size())};
  std::vector<const char *> arguments = {"-x", "c", "-std=gnu11",
                                         "-fparse-all-comments"};
  for (const auto &flag : p_include_flags)
    arguments.push_back(flag.c_str());
  for (const auto &flag : platform_flags())
    arguments.push_back(flag.c_str());

  const auto index = clang_createIndex(0, 0);
  CXTranslationUnit unit = nullptr;
  const auto code = clang_parseTranslationUnit2(
      index, "ovis_import.c", arguments.data(),
      static_cast<int>(arguments.size()), &unsaved, 1,
      CXTranslationUnit_DetailedPreprocessingRecord |
          CXTranslationUnit_SkipFunctionBodies | CXTranslationUnit_Incomplete,
      &unit);
  if (code != CXError_Success || unit == nullptr) {
    clang_disposeIndex(index);
    throw error("Cannot read the C header " + header + ".");
  }
  for (unsigned diagnostic = 0; diagnostic < clang_getNumDiagnostics(unit);
       ++diagnostic) {
    const auto item = clang_getDiagnostic(unit, diagnostic);
    const auto severity = clang_getDiagnosticSeverity(item);
    const auto text = to_string(clang_formatDiagnostic(
        item, clang_defaultDiagnosticDisplayOptions()));
    clang_disposeDiagnostic(item);
    if (severity >= CXDiagnostic_Error) {
      clang_disposeTranslationUnit(unit);
      clang_disposeIndex(index);
      throw error("The C header " + header + " has errors: " + text);
    }
  }

  // The files read, for a build's dependency file; the system's are left out.
  struct reading {
    c_library *library;
    std::vector<std::filesystem::path> system;
  } inclusions{&p_library, system_include_directories()};
  clang_getInclusions(
      unit,
      [](CXFile p_file, CXSourceLocation *, unsigned, CXClientData p_data) {
        auto &state = *static_cast<reading *>(p_data);
        std::error_code ignored;
        const std::filesystem::path path = std::filesystem::weakly_canonical(
            to_string(clang_getFileName(p_file)), ignored);
        if (path.empty() || path.filename() == "ovis_import.c")
          return;
        for (const auto &directory : state.system) {
          const auto [end, unused] = std::mismatch(
              directory.begin(), directory.end(), path.begin(), path.end());
          if (end == directory.end())
            return;
        }
        state.library->dependencies.push_back(path);
      },
      &inclusions);

  import_state state{unit, clang_getFile(unit, header.c_str()), &p_library,
                     {}, {}, {}, {}};
  const auto directory = p_library.header_path.parent_path();
  const auto system = system_include_directories();
  if (std::ranges::find(system, directory) == system.end())
    state.beside = directory;
  clang_visitChildren(clang_getTranslationUnitCursor(unit), name_records, &state);
  clang_visitChildren(clang_getTranslationUnitCursor(unit), visit, &state);
  for (const auto &macro : state.macros)
    import_function_macro(state, macro);
  // The structs the functions reach, and those their fields reach.
  for (std::size_t index = 0; index < state.reached.size(); ++index) {
    const auto cursor = state.reached[index];
    import_record(state, cursor,
                  state.record_names.at(to_string(clang_getCursorUSR(cursor))));
  }
  clang_disposeTranslationUnit(unit);
  clang_disposeIndex(index);
}

#endif

auto find_header(string_view_type p_header,
                 const std::vector<std::filesystem::path> &p_directories)
    -> std::optional<std::filesystem::path> {
  const std::filesystem::path header{string_type(p_header)};
  std::error_code code;
  if (header.is_absolute())
    return std::filesystem::exists(header, code)
               ? std::optional(std::filesystem::canonical(header))
               : std::nullopt;
  for (const auto &directory : p_directories)
    if (std::filesystem::exists(directory / header, code))
      return std::filesystem::canonical(directory / header);
  return std::nullopt;
}

} // namespace

auto system_include_directories() -> std::vector<std::filesystem::path> {
  std::vector<std::filesystem::path> result;
  for (const auto *const directory :
       {"/usr/local/include", "/usr/include/x86_64-linux-gnu",
        "/usr/include/aarch64-linux-gnu", "/usr/include",
        "/opt/homebrew/include"})
    result.emplace_back(directory);
#ifdef __APPLE__
  // Headers live in the SDK, not /usr/include.
  if (const auto sdk = run_and_capture("xcrun", {"--show-sdk-path"})) {
    auto path = *sdk;
    while (!path.empty() && (path.back() == '\n' || path.back() == ' '))
      path.pop_back();
    if (!path.empty())
      result.emplace_back(std::filesystem::path(path) / "usr" / "include");
  }
#endif
  return result;
}

auto has_c_import_support() -> bool {
#ifdef OVIS_HAS_C_IMPORT
  return true;
#else
  return false;
#endif
}

namespace {

// Libraries read so far, by header, library and the flags they were read with.
auto import_cache() -> std::map<string_type, c_library> & {
  static std::map<string_type, c_library> cache;
  return cache;
}

} // namespace

auto imported_dependencies() -> std::vector<std::filesystem::path> {
  std::set<std::filesystem::path> unique;
  for (const auto &[key, library] : import_cache())
    unique.insert(library.dependencies.begin(), library.dependencies.end());
  return {unique.begin(), unique.end()};
}

auto import_c_library(string_view_type p_header, string_view_type p_library,
                      const c_import_options &p_options) -> const c_library & {
  auto &cache = import_cache();
  string_type key = string_type(p_header) + "|" + string_type(p_library);
  for (const auto &directory : p_options.include_directories)
    key += "|I" + directory.string();
  for (const auto &directory : p_options.library_directories)
    key += "|L" + directory.string();
  for (const auto &flag : p_options.compiler_flags)
    key += "|F" + flag;
  key += p_options.link_by_caller ? "|caller" : "|self";
  if (const auto found = cache.find(key); found != cache.end())
    return found->second;

#ifndef OVIS_HAS_C_IMPORT
  throw error("This build of Ovis can't read C headers (it was built without "
              "libclang); declare the functions with (extern ...) instead.");
#else
  c_library library;

  // pkg-config knows include and link flags for most libraries. Try the
  // library name, then the header's name (zlib.h → zlib).
  std::vector<string_type> packages;
  if (!p_library.empty())
    packages.emplace_back(p_library);
  const auto stem =
      std::filesystem::path(string_type(p_header)).stem().string();
  if (stem != p_library)
    packages.push_back(stem);
  std::vector<string_type> include_flags;
  std::optional<string_type> library_flags;
  // Under CMake the target's flags come with the call and its library is
  // linked by CMake: pkg-config has nothing to add.
  for (const auto &package : p_options.link_by_caller ? std::vector<string_type>()
                                                      : packages) {
    library_flags = run_and_capture("pkg-config", {"--libs", package});
    if (!library_flags)
      continue;
    for (auto &flag :
         split_words(run_and_capture("pkg-config", {"--cflags", package})
                         .value_or("")))
      include_flags.push_back(std::move(flag));
    break;
  }

  std::vector<std::filesystem::path> directories =
      p_options.include_directories;
  for (const auto &flag : include_flags)
    if (flag.starts_with("-I"))
      directories.emplace_back(flag.substr(2));
  for (auto &directory : system_include_directories())
    directories.push_back(std::move(directory));
  const auto header_path = find_header(p_header, directories);
  if (!header_path)
    throw error("Cannot find the C header " + string_type(p_header) +
                ". Install the library's development files, or pass -I with "
                "the directory that holds it.");
  library.header_path = *header_path;
  library.header_hash = fnv_hash(read_whole_file(*header_path));

  for (const auto &directory : p_options.include_directories)
    include_flags.push_back("-I" + directory.string());
  for (const auto &flag : p_options.compiler_flags)
    include_flags.push_back(flag);
  parse_header(library, include_flags);
  // What a wrapper for a struct passed by value is compiled with.
  for (auto &function : library.functions) {
    function.header_path = library.header_path.string();
    function.compile_flags = include_flags;
  }

  if (p_options.link_by_caller)
    return cache.emplace(key, std::move(library)).first->second;
  for (const auto &directory : p_options.library_directories)
    library.link_flags.push_back("-L" + directory.string());
  if (library_flags) {
    for (auto &flag : split_words(*library_flags))
      library.link_flags.push_back(std::move(flag));
  } else if (!p_library.empty()) {
    library.link_flags.push_back("-l" + string_type(p_library));
  }
  return cache.emplace(key, std::move(library)).first->second;
#endif
}

auto resolve_c_imports(program &p_program, const c_import_options &p_options)
    -> std::vector<diagnostic> {
  std::vector<diagnostic> result;
  for (std::size_t index = 0; index < p_program.units.size(); ++index) {
    auto &unit = p_program.units[index];
    std::vector<c_function> imported;
    for (const auto &request : unit.c_imports) {
      try {
        const auto &library =
            import_c_library(request.header, request.library, p_options);
        for (auto function : library.functions) {
          // Ovis's own `free`, `abs`… do what C's would (`free` frees
          // either kind of memory).
          if (is_builtin(function.name))
            continue;
          function.library = request.header;
          imported.push_back(std::move(function));
        }
        unit.constants.insert(unit.constants.end(), library.constants.begin(),
                              library.constants.end());
        // A struct is one type in the whole program, however many files
        // use its header.
        for (const auto &record : library.records)
          if (!find_record(p_program, record.name)) {
            auto added = record;
            added.location = request.location;
            unit.records.push_back(std::move(added));
          }
        unit.link_flags.insert(unit.link_flags.end(),
                               library.link_flags.begin(),
                               library.link_flags.end());
      } catch (const error &p_error) {
        result.push_back({index, request.location, p_error.what()});
      }
    }
    // The paragraph's own (extern ...) declarations win over imported ones.
    imported.insert(imported.end(), unit.externs.begin(), unit.externs.end());
    unit.externs = std::move(imported);
  }
  return result;
}

} // namespace ovis::core
