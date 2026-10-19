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
};

auto is_in_header(const import_state &p_state, CXCursor p_cursor) -> bool {
  CXFile file = nullptr;
  clang_getSpellingLocation(clang_getCursorLocation(p_cursor), &file, nullptr,
                            nullptr, nullptr);
  return file != nullptr && clang_File_isEqual(file, p_state.header);
}

auto import_function(import_state &p_state, CXCursor p_cursor) -> void {
  auto name = to_string(clang_getCursorSpelling(p_cursor));
  if (name.starts_with("_") || !p_state.seen.insert(name).second)
    return;
  c_function function{.name = name};
  const auto result = map_type(clang_getCursorResultType(p_cursor));
  if (!result) {
    p_state.library->skipped.push_back(
        name + ": returns " +
        describe_c_type(clang_getCursorResultType(p_cursor)) +
        ", which Ovis can't use yet");
    return;
  }
  function.result = result->type;
  const auto count = clang_Cursor_getNumArguments(p_cursor);
  for (int index = 0; index < count; ++index) {
    const auto type = clang_getCursorType(
        clang_Cursor_getArgument(p_cursor, static_cast<unsigned>(index)));
    const auto mapped = map_type(type);
    if (!mapped) {
      p_state.library->skipped.push_back(
          name + ": takes " + describe_c_type(type) +
          ", which Ovis can't pass yet");
      return;
    }
    function.parameters.push_back(mapped->type);
    function.pointees.push_back(mapped->pointee);
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

auto import_macro(import_state &p_state, CXCursor p_cursor) -> void {
  if (clang_Cursor_isMacroFunctionLike(p_cursor))
    return;
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

  import_state state{unit, clang_getFile(unit, header.c_str()), &p_library, {}};
  clang_visitChildren(clang_getTranslationUnitCursor(unit), visit, &state);
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

auto import_c_library(string_view_type p_header, string_view_type p_library,
                      const c_import_options &p_options) -> const c_library & {
  static std::map<string_type, c_library> cache;
  string_type key = string_type(p_header) + "|" + string_type(p_library);
  for (const auto &directory : p_options.include_directories)
    key += "|I" + directory.string();
  for (const auto &directory : p_options.library_directories)
    key += "|L" + directory.string();
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
  for (const auto &package : packages) {
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
  parse_header(library, include_flags);

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
          function.library = request.header;
          imported.push_back(std::move(function));
        }
        unit.constants.insert(unit.constants.end(), library.constants.begin(),
                              library.constants.end());
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
