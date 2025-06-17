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

#include "def.hpp"

namespace ovis::tokenizer
{

    struct symbolic_group
    {
        string_view_type characters;
        std::size_t max;
        bool ignored;

        explicit constexpr symbolic_group(string_view_type p_characters, std::size_t p_max, bool p_ignored)
            : characters(p_characters), max(p_max), ignored(p_ignored) {}
    };

    struct string_literal_marker
    {
        char_type character;

        explicit constexpr string_literal_marker(char_type p_character) : character(p_character) {}
    };

    constexpr std::array<symbolic_group, 18> symbolic_groups = {
        symbolic_group("(", 1, false),
        symbolic_group(")", 1, false),
        symbolic_group(":", 1, false),
        symbolic_group("!", 1, false),
        symbolic_group("@", 1, false),
        symbolic_group("#", 1, false),
        symbolic_group("$", 1, false),
        symbolic_group("%", 1, false),
        symbolic_group("^", 1, false),
        symbolic_group("{", 1, false),
        symbolic_group("}", 1, false),
        symbolic_group(";", 1, false),
        symbolic_group(",", 1, false),
        symbolic_group("~", 1, false),
        symbolic_group(".", 1, false),
        symbolic_group("+-*/=><?", 2, false),
        symbolic_group("&", 2, false),
        symbolic_group("|", 2, false),
    };

    constexpr std::array<string_literal_marker, 3> string_literal_markers = {
        string_literal_marker('\''),
        string_literal_marker('\"'),
        string_literal_marker('`'),
    };

    template <typename t_type>
    concept c_is_tokenizer_context = requires {
        { std::declval<const t_type>().is_first_token() } -> std::same_as<bool>;
        { std::declval<const t_type>().is_last_token() } -> std::same_as<bool>;
        { std::declval<const t_type>().is_first_line() } -> std::same_as<bool>;
        { std::declval<const t_type>().is_last_line() } -> std::same_as<bool>;
        { std::declval<const t_type>().get_row() } -> std::same_as<std::size_t>;
        { std::declval<const t_type>().get_current_token() } -> std::same_as<string_view_type>;
        { std::declval<const t_type>().get_next_token() } -> std::same_as<string_view_type>;
        { std::declval<const t_type>().get_previous_token() } -> std::same_as<string_view_type>;
        { std::declval<const t_type>().get_first_token() } -> std::same_as<string_view_type>;
        { std::declval<const t_type>().get_last_token() } -> std::same_as<string_view_type>;
        { std::declval<const t_type>().get_current_line() } -> std::same_as<string_view_type>;
        { std::declval<const t_type>().get_previous_line() } -> std::same_as<string_view_type>;
        { std::declval<const t_type>().get_next_line() } -> std::same_as<string_view_type>;
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
            bool m_is_first_token;
            bool m_is_last_token;
            bool m_is_first_line;
            bool m_is_last_line;
            std::size_t m_row;
            string_view_type m_current_token;
            string_view_type m_next_token;
            string_view_type m_previous_token;
            string_view_type m_first_token;
            string_view_type m_last_token;
            string_view_type m_current_line;
            string_view_type m_next_line;
            string_view_type m_previous_line;

        public:
            explicit tokenizer_context(
                bool p_is_first_token,
                bool p_is_last_token,
                bool p_is_first_line,
                bool p_is_last_line,
                std::size_t p_row,
                string_view_type p_current_token,
                string_view_type p_next_token,
                string_view_type p_previous_token,
                string_view_type p_first_token,
                string_view_type p_last_token,
                string_view_type p_current_line,
                string_view_type p_next_line,
                string_view_type p_previous_line)
                : m_is_first_token(p_is_first_token),
                  m_is_last_token(p_is_last_token),
                  m_is_first_line(p_is_first_line),
                  m_is_last_line(p_is_last_line),
                  m_row(p_row),
                  m_current_token(p_current_token),
                  m_next_token(p_next_token),
                  m_previous_token(p_previous_token),
                  m_first_token(p_first_token),
                  m_last_token(p_last_token),
                  m_current_line(p_current_line),
                  m_next_line(p_next_line),
                  m_previous_line(p_previous_line)
            {
            }

            constexpr explicit tokenizer_context()
                : tokenizer_context(
                      false,
                      false,
                      false,
                      false,
                      0,
                      string_view_type(),
                      string_view_type(),
                      string_view_type(),
                      string_view_type(),
                      string_view_type(),
                      string_view_type(),
                      string_view_type(),
                      string_view_type())
            {
            }

            auto is_first_token() const -> bool
            {
                return m_is_first_token;
            }

            auto is_last_token() const -> bool
            {
                return m_is_last_token;
            }

            auto is_first_line() const -> bool
            {
                return m_is_first_line;
            }

            auto is_last_line() const -> bool
            {
                return m_is_last_line;
            }

            auto get_row() const -> std::size_t
            {
                return m_row;
            }

            auto get_current_token() const -> string_view_type
            {
                return m_current_token;
            }

            auto get_previous_token() const -> string_view_type
            {
                return m_previous_token;
            }

            auto get_next_token() const -> string_view_type
            {
                return m_next_token;
            }

            auto get_first_token() const -> string_view_type
            {
                return m_first_token;
            }

            auto get_last_token() const -> string_view_type
            {
                return m_last_token;
            }

            auto get_current_line() const -> string_view_type
            {
                return m_current_line;
            }

            auto get_previous_line() const -> string_view_type
            {
                return m_previous_line;
            }

            auto get_next_line() const -> string_view_type
            {
                return m_next_line;
            }
        };
        static_assert(c_is_tokenizer_context<tokenizer_context<>>);

        template <typename = void>
        auto get_corresponding_symbolic_group_index(char_type p_character) -> std::size_t
        {
            return std::distance(symbolic_groups.cbegin(), std::find_if(symbolic_groups.cbegin(), symbolic_groups.cend(), [p_character](const symbolic_group &p_group)
                                                                        { return std::any_of(p_group.characters.cbegin(), p_group.characters.cend(), [p_character](char_type p_symbol)
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

            const std::function<void(std::size_t, bool)> push_token = [&](std::size_t new_token_begin_index, bool p_ignore_token)
            {
                if (token_begin_index != token_end_index && !p_ignore_token)
                    tokens.push_back(token_type(&p_line_content[token_begin_index], &p_line_content[token_end_index]));
                token_begin_index = new_token_begin_index;
                token_end_index = new_token_begin_index;
            };

            for (auto [i, character] : std::views::zip(std::views::iota(static_cast<std::size_t>(0), p_line_content.size()), p_line_content))
            {
                if (current_string_literal_character != '\0')
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

                if (std::any_of(string_literal_markers.cbegin(), string_literal_markers.cend(), [character](string_literal_marker p_marker)
                                { return p_marker.character == character; }))
                {
                    push_token(i + 1, false);
                    current_string_literal_character = character;
                    continue;
                }

                if (isspace(character))
                {
                    push_token(i + 1, false);
                    continue;
                }

                const std::size_t new_group_index = get_corresponding_symbolic_group_index(character);
                if (current_symbolic_group_index < symbolic_groups.size())
                {
                    const symbolic_group group = symbolic_groups[current_symbolic_group_index];
                    if (new_group_index != current_symbolic_group_index || (token_end_index - token_begin_index) >= group.max)
                    {
                        push_token(i, group.ignored);
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
                    is_first_token,
                    is_last_token,
                    is_first_line,
                    is_last_line,
                    lines[line_index].first,
                    tokens[token_index],
                    is_last_token ? ""sv : tokens[token_index + 1],
                    is_first_token ? ""sv : tokens[token_index - 1],
                    tokens.front(),
                    tokens.back(),
                    lines[line_index].second,
                    is_last_line ? ""sv : lines[line_index + 1].second,
                    is_first_line ? ""sv : lines[line_index - 1].second);
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