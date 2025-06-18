#ifndef OVIS_TOKENIZER_HPP
#define OVIS_TOKENIZER_HPP

#include <concepts>
#include <string>
#include <iterator>
#include <ranges>
#include <functional>
#include <cctype>
#include <algorithm>
#include <array>
#include <optional>

#include "def.hpp"

namespace ovis::tokenizer
{

    using line_row_type = std::size_t;
    using line_content_type = std::basic_string_view<char_type>;
    using line_content_list_type = std::vector<line_content_type>;
    using line_type = std::pair<line_row_type, line_content_type>;
    using line_list_type = std::vector<line_type>;

    using token_content_type = string_view_type;
    using token_metadata_type = struct token_metadata_t
    {
        bool m_is_string_literal;
        bool m_is_symbol;

        explicit constexpr token_metadata_t(bool p_is_string_literal, bool p_is_symbol)
            : m_is_string_literal(p_is_string_literal), m_is_symbol(p_is_symbol) {}

        explicit constexpr token_metadata_t() : token_metadata_t(false, false) {}

        auto operator<=>(const token_metadata_t &) const -> bool = default;
    };
    using token_type = std::pair<token_content_type, token_metadata_type>;
    using token_list_type = std::vector<token_type>;

    using symbolic_group_type = struct symbolic_group_t
    {
        string_view_type m_characters;
        std::size_t m_max;
        bool m_ignored;

        explicit constexpr symbolic_group_t(string_view_type p_characters, std::size_t p_max, bool p_ignored)
            : m_characters(p_characters), m_max(p_max), m_ignored(p_ignored) {}

        auto operator<=>(const symbolic_group_t &) const -> bool = default;
    };
    using string_literal_marker_type = struct string_literal_marker_t
    {
        char_type m_character;

        explicit constexpr string_literal_marker_t(char_type p_character) : m_character(p_character) {}

        auto operator<=>(const string_literal_marker_t &) const -> bool = default;
    };

    constexpr std::array<symbolic_group_type, 20> symbolic_groups = {
        symbolic_group_type("(", 1, false),
        symbolic_group_type(")", 1, false),
        symbolic_group_type(":", 1, false),
        symbolic_group_type("!", 1, false),
        symbolic_group_type("@", 1, false),
        symbolic_group_type("#", 1, false),
        symbolic_group_type("$", 1, false),
        symbolic_group_type("%", 1, false),
        symbolic_group_type("^", 1, false),
        symbolic_group_type("{", 1, false),
        symbolic_group_type("}", 1, false),
        symbolic_group_type("[", 1, false),
        symbolic_group_type("]", 1, false),
        symbolic_group_type(";", 1, false),
        symbolic_group_type(",", 1, false),
        symbolic_group_type("~", 1, false),
        symbolic_group_type(".", 1, false),
        symbolic_group_type("+-*/=><?", 2, false),
        symbolic_group_type("&", 2, false),
        symbolic_group_type("|", 2, false),
    };

    constexpr std::array<string_literal_marker_type, 3> string_literal_markers = {
        string_literal_marker_type('\''),
        string_literal_marker_type('\"'),
        string_literal_marker_type('`'),
    };

    template <typename t_type>
    concept c_is_tokenizer_context = requires {
        { std::declval<const t_type>().get_current_token() } -> std::same_as<token_type>;
        { std::declval<const t_type>().get_next_token() } -> std::same_as<std::optional<token_type>>;
        { std::declval<const t_type>().get_previous_token() } -> std::same_as<std::optional<token_type>>;
        { std::declval<const t_type>().get_last_token() } -> std::same_as<token_type>;
        { std::declval<const t_type>().get_first_token() } -> std::same_as<token_type>;
        { std::declval<const t_type>().get_current_line() } -> std::same_as<line_type>;
        { std::declval<const t_type>().get_next_line() } -> std::same_as<std::optional<line_type>>;
        { std::declval<const t_type>().get_previous_line() } -> std::same_as<std::optional<line_type>>;
    };

    template <typename t_type>
    concept c_is_tokenizer_iterator = std::input_iterator<t_type> && std::sentinel_for<bool, t_type> && c_is_tokenizer_context<std::iter_value_t<t_type>>;

    namespace implementation
    {
        template <typename = void>
        class tokenizer_context
        {
        public:
        private:
            token_type m_current_token;
            std::optional<token_type> m_next_token;
            std::optional<token_type> m_previous_token;
            token_type m_last_token;
            token_type m_first_token;
            line_type m_current_line;
            std::optional<line_type> m_next_line;
            std::optional<line_type> m_previous_line;

        public:
            explicit tokenizer_context(
                token_type p_current_token,
                std::optional<token_type> p_next_token,
                std::optional<token_type> p_previous_token,
                token_type p_last_token,
                token_type p_first_token,
                line_type p_current_line,
                std::optional<line_type> p_next_line,
                std::optional<line_type> p_previous_line)
                : m_current_token(p_current_token),
                  m_next_token(p_next_token),
                  m_previous_token(p_previous_token),
                  m_last_token(p_last_token),
                  m_first_token(p_first_token),
                  m_current_line(p_current_line),
                  m_next_line(p_next_line),
                  m_previous_line(p_previous_line)
            {
            }

            constexpr explicit tokenizer_context()
                : tokenizer_context(
                      token_type(token_content_type(), token_metadata_type()),
                      std::optional<token_type>(),
                      std::optional<token_type>(),
                      token_type(token_content_type(), token_metadata_type()),
                      token_type(token_content_type(), token_metadata_type()),
                      line_type(line_row_type(), line_content_type()),
                      std::optional<line_type>(),
                      std::optional<line_type>())
            {
            }

            auto get_current_token() const -> token_type { return m_current_token; }
            auto get_next_token() const -> std::optional<token_type> { return m_next_token; }
            auto get_previous_token() const -> std::optional<token_type> { return m_previous_token; }
            auto get_last_token() const -> token_type { return m_last_token; }
            auto get_first_token() const -> token_type { return m_first_token; }
            auto get_current_line() const -> line_type { return m_current_line; }
            auto get_next_line() const -> std::optional<line_type> { return m_next_line; }
            auto get_previous_line() const -> std::optional<line_type> { return m_previous_line; }
        };
        static_assert(c_is_tokenizer_context<tokenizer_context<>>);

        template <typename = void>
        auto get_corresponding_symbolic_group_index(char_type p_character) -> std::size_t
        {
            return std::distance(symbolic_groups.cbegin(), std::find_if(symbolic_groups.cbegin(), symbolic_groups.cend(), [p_character](const symbolic_group_type &p_group)
                                                                        { return std::any_of(p_group.m_characters.cbegin(), p_group.m_characters.cend(), [p_character](char_type p_symbol)
                                                                                             { return p_symbol == p_character; }); }));
        }

        template <typename = void>
        auto to_lines(string_view_type p_text) -> line_list_type
        {
            line_list_type lines;
            auto content_subrange_range = p_text | std::views::split("\n"sv);

            for (auto [row, content_subrange] : std::views::zip(std::views::iota(1, std::ranges::distance(content_subrange_range) + 1), content_subrange_range))
            {
                line_content_type content = line_content_type(content_subrange.begin(), content_subrange.end());
                const std::function<bool(const char_type)> is_whitespace_predicate = [](const char_type p_character)
                { return std::isspace(p_character); };
                const uint_type whitespace_prefix_count = std::min(static_cast<std::size_t>(std::distance(std::ranges::find_if_not(
                                                                                                              content.cbegin(), content.cend(), is_whitespace_predicate),
                                                                                                          content.cbegin())),
                                                                   content.size());
                content.remove_prefix(whitespace_prefix_count);
                const uint_type whitespace_suffix_count = std::min(static_cast<std::size_t>(std::distance(std::ranges::find_if_not(
                                                                                                              content.crbegin(), content.crend(), is_whitespace_predicate),
                                                                                                          content.crbegin())),
                                                                   content.size());
                content.remove_suffix(whitespace_suffix_count);
                if (content.empty())
                    continue;
                lines.push_back(line_type(row, content));
            }

            return lines;
        }

        template <typename = void>
        auto to_tokens(line_content_type p_line_content) -> token_list_type
        {
            token_list_type tokens;
            std::size_t current_symbolic_group_index = symbolic_groups.size();
            char_type current_string_literal_character = '\0';

            std::size_t token_begin_index = 0;
            std::size_t token_end_index = 0;

            const std::function<bool()> is_reading_string_literal = [&current_string_literal_character]()
            {
                return current_string_literal_character != '\0';
            };

            const std::function<bool()> is_reading_symbol = [&current_symbolic_group_index]()
            {
                return current_symbolic_group_index < symbolic_groups.size();
            };

            const std::function<void(std::size_t, bool)>
                push_token = [&](std::size_t new_token_begin_index, bool p_ignore_token)
            {
                if (token_begin_index != token_end_index && !p_ignore_token)
                    tokens.push_back(
                        token_type(
                            token_content_type(&p_line_content[token_begin_index], &p_line_content[token_end_index]),
                            token_metadata_type(is_reading_string_literal(), is_reading_symbol())));
                token_begin_index = new_token_begin_index;
                token_end_index = new_token_begin_index;
            };

            for (auto [i, character] : std::views::zip(std::views::iota(static_cast<std::size_t>(0), p_line_content.size()), p_line_content))
            {
                if (is_reading_string_literal())
                {
                    if (character == current_string_literal_character)
                    {
                        push_token(i + 1, false);
                        current_string_literal_character = '\0';
                        continue;
                    }

                    ++token_end_index;
                    continue;
                }

                if (std::any_of(string_literal_markers.cbegin(), string_literal_markers.cend(), [character](string_literal_marker_type p_marker)
                                { return p_marker.m_character == character; }))
                {
                    push_token(i + 1, false);
                    current_string_literal_character = character;
                    current_symbolic_group_index = symbolic_groups.size();
                    continue;
                }

                if (isspace(character))
                {
                    push_token(i + 1, false);
                    continue;
                }

                const std::size_t new_group_index = get_corresponding_symbolic_group_index(character);
                if (is_reading_symbol())
                {
                    const symbolic_group_type group = symbolic_groups[current_symbolic_group_index];
                    if (new_group_index != current_symbolic_group_index || (token_end_index - token_begin_index) >= group.m_max)
                    {
                        push_token(i, group.m_ignored);
                        current_symbolic_group_index = new_group_index;
                    }
                }
                else if (new_group_index != current_symbolic_group_index)
                {
                    push_token(i, false);
                    current_symbolic_group_index = new_group_index;
                }

                ++token_end_index;
            }

            push_token(p_line_content.size(), false);

            return tokens;
        }

        template <typename = void>
        class tokenizer_iterator
        {
        public:
            using difference_type = std::ptrdiff_t;
            using value_type = tokenizer_context<>;
            using pointer = const value_type *;
            using reference_type = const value_type &;
            using iterator_category = std::input_iterator_tag;

        private:
            line_list_type lines;
            std::size_t line_index;

            token_list_type tokens;
            std::size_t token_index;

        public:
            explicit tokenizer_iterator(string_view_type p_text)
                : lines(to_lines(p_text)), line_index(0), tokens(), token_index(0)
            {
                if (!lines.empty())
                    tokens = to_tokens(lines[0].second);
            }

            operator bool() const
            {
                return line_index < lines.size();
            }

            auto operator==(bool p_sentinel) const -> bool
            {
                return bool(*this) == p_sentinel;
            }

            auto operator*() const -> value_type
            {
                const bool is_first_token = token_index == 0;
                const bool is_last_token = token_index == (tokens.size() - 1);
                const bool is_first_line = line_index == 0;
                const bool is_last_line = line_index == (lines.size() - 1);
                return value_type(
                    tokens[token_index],
                    is_last_token ? std::optional<token_type>() : tokens[token_index + 1],
                    is_first_token ? std::optional<token_type>() : tokens[token_index - 1],
                    tokens.back(),
                    tokens.front(),
                    lines[line_index],
                    is_last_line ? std::optional<line_type>() : lines[line_index + 1],
                    is_first_line ? std::optional<line_type>() : lines[line_index - 1]);
            }

            auto operator++() -> tokenizer_iterator &
            {
                if (!*this)
                    return *this;

                const bool is_last_token = token_index == (tokens.size() - 1);
                if (is_last_token)
                {
                    token_index = 0;
                    ++line_index;
                    if (line_index < lines.size())
                        tokens = to_tokens(lines[line_index].second);
                }
                else
                    ++token_index;

                return *this;
            }

            auto operator++(int) -> tokenizer_iterator
            {
                tokenizer_iterator old = *this;
                ++(*this);
                return old;
            }
        };
        static_assert(c_is_tokenizer_iterator<tokenizer_iterator<>>);

    } // namespace implementation

    using tokenizer_iterator = implementation::tokenizer_iterator<>;

} // namespace ovis::tokenizer

#endif // OVIS_TOKENIZER_HPP