#ifndef OVIS_TOKENIZER_HPP
#define OVIS_TOKENIZER_HPP

#include <string>
#include <iterator>
#include <ranges>
#include <functional>
#include <cctype>
#include <algorithm>
#include <array>
#include <optional>

#include <ovis/def.hpp>

namespace ovis::tokenizer
{

    template <typename = void>
    struct line_metadata_t final
    {
        std::size_t m_row;

        explicit constexpr line_metadata_t(std::size_t p_row) : m_row(p_row) {}

        explicit constexpr line_metadata_t() : line_metadata_t(0) {}

        auto operator<=>(const line_metadata_t &) const -> bool = default;
    };
    using line_metadata_type = line_metadata_t<>;
    using line_content_type = std::basic_string_view<char_type>;
    using line_content_list_type = std::vector<line_content_type>;
    template <typename = void>
    struct line_t final
    {
        line_metadata_type m_metadata;
        line_content_type m_content;

        explicit constexpr line_t(line_metadata_type p_metadata, line_content_type p_content)
            : m_metadata(p_metadata), m_content(p_content) {}

        explicit constexpr line_t() : line_t(line_metadata_type(), line_content_type()) {}

        auto operator<=>(const line_t &) const -> bool = default;
    };
    using line_type = line_t<>;
    using line_list_type = std::vector<line_type>;
    using optional_line_type = std::optional<line_type>;

    using token_content_type = string_view_type;
    template <typename = void>
    struct token_metadata_t final
    {
        bool m_is_string_literal;
        bool m_is_symbol;

        explicit constexpr token_metadata_t(bool p_is_string_literal, bool p_is_symbol)
            : m_is_string_literal(p_is_string_literal), m_is_symbol(p_is_symbol) {}

        explicit constexpr token_metadata_t() : token_metadata_t(false, false) {}

        auto operator<=>(const token_metadata_t &) const -> bool = default;
    };
    using token_metadata_type = token_metadata_t<>;
    template <typename = void>
    struct token_t final
    {
        token_metadata_type m_metadata;
        token_content_type m_content;

        explicit constexpr token_t(token_metadata_type p_metadata, token_content_type p_content)
            : m_metadata(p_metadata), m_content(p_content) {}

        explicit constexpr token_t() : token_t(token_metadata_type(), token_content_type()) {}

        auto operator<=>(const token_t &) const -> bool = default;
    };
    using token_type = token_t<>;
    using token_list_type = std::vector<token_type>;
    using optional_token_type = std::optional<token_type>;

    template <typename = void>
    struct symbolic_group_t final
    {
        string_view_type m_characters;
        std::size_t m_max;
        bool m_ignored;

        explicit constexpr symbolic_group_t(string_view_type p_characters, std::size_t p_max, bool p_ignored)
            : m_characters(p_characters), m_max(p_max), m_ignored(p_ignored) {}

        auto operator<=>(const symbolic_group_t &) const -> bool = default;
    };
    using symbolic_group_type = symbolic_group_t<>;
    using symbolic_group_slice_type = std::span<symbolic_group_type>;
    using symbolic_group_list_type = std::vector<symbolic_group_type>;
    template <typename = void>
    struct string_literal_marker_t final
    {
        char_type m_character;

        explicit constexpr string_literal_marker_t(char_type p_character) : m_character(p_character) {}

        auto operator<=>(const string_literal_marker_t &) const -> bool = default;
    };
    using string_literal_marker_type = string_literal_marker_t<>;
    using string_literal_marker_slice_type = std::span<string_literal_marker_type>;
    using string_literal_marker_list_type = std::vector<string_literal_marker_type>;

    namespace implementation
    {
        template <typename = void>
        class tokenizer_context final
        {
        public:
        private:
            token_type m_current_token;
            optional_token_type m_next_token;
            optional_token_type m_previous_token;
            token_type m_last_token;
            token_type m_first_token;
            line_type m_current_line;
            optional_line_type m_next_line;
            optional_line_type m_previous_line;

        public:
            explicit tokenizer_context(
                token_type p_current_token,
                optional_token_type p_next_token,
                optional_token_type p_previous_token,
                token_type p_last_token,
                token_type p_first_token,
                line_type p_current_line,
                optional_line_type p_next_line,
                optional_line_type p_previous_line)
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
                      token_type(),
                      optional_token_type(),
                      optional_token_type(),
                      token_type(),
                      token_type(),
                      line_type(),
                      optional_line_type(),
                      optional_line_type())
            {
            }

            auto operator<=>(const tokenizer_context &) const -> bool = default;

            auto get_current_token() const -> token_type { return m_current_token; }

            auto get_next_token() const -> optional_token_type { return m_next_token; }

            auto get_previous_token() const -> optional_token_type { return m_previous_token; }

            auto get_last_token() const -> token_type { return m_last_token; }

            auto get_first_token() const -> token_type { return m_first_token; }

            auto get_current_line() const -> line_type { return m_current_line; }

            auto get_next_line() const -> optional_line_type { return m_next_line; }

            auto get_previous_line() const -> optional_line_type { return m_previous_line; }
        };

        template <typename = void>
        auto get_corresponding_symbolic_group_index(char_type p_character, symbolic_group_slice_type p_symbolic_groups) -> std::size_t
        {
            return std::distance(p_symbolic_groups.begin(), std::find_if(p_symbolic_groups.begin(), p_symbolic_groups.end(), [p_character](const symbolic_group_type &p_group)
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
                lines.push_back(line_type(line_metadata_type(row), line_content_type(content)));
            }

            return lines;
        }

        template <typename = void>
        auto to_tokens(line_content_type p_line_content, symbolic_group_slice_type p_symbolic_groups, string_literal_marker_slice_type p_string_literal_markers) -> token_list_type
        {
            token_list_type tokens;
            std::size_t current_symbolic_group_index = p_symbolic_groups.size();
            char_type current_string_literal_character = '\0';

            std::size_t token_begin_index = 0;
            std::size_t token_end_index = 0;

            const std::function<bool()> is_reading_string_literal = [&current_string_literal_character]()
            {
                return current_string_literal_character != '\0';
            };

            const std::function<bool()> is_reading_symbol = [&current_symbolic_group_index, &p_symbolic_groups]()
            {
                return current_symbolic_group_index < p_symbolic_groups.size();
            };

            const std::function<void(std::size_t, bool)>
                push_token = [&](std::size_t new_token_begin_index, bool p_ignore_token)
            {
                if (token_begin_index != token_end_index && !p_ignore_token)
                    tokens.push_back(
                        token_type(
                            token_metadata_type(is_reading_string_literal(), is_reading_symbol()),
                            token_content_type(&p_line_content[token_begin_index], &p_line_content[token_end_index])));
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

                if (std::any_of(p_string_literal_markers.begin(), p_string_literal_markers.end(), [character](string_literal_marker_type p_marker)
                                { return p_marker.m_character == character; }))
                {
                    push_token(i + 1, false);
                    current_string_literal_character = character;
                    current_symbolic_group_index = p_symbolic_groups.size();
                    continue;
                }

                if (isspace(character))
                {
                    push_token(i + 1, false);
                    continue;
                }

                const std::size_t new_group_index = get_corresponding_symbolic_group_index(character, p_symbolic_groups);
                if (is_reading_symbol())
                {
                    const symbolic_group_type group = p_symbolic_groups[current_symbolic_group_index];
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
        class tokenizer_iterator final
        {
        public:
            using difference_type = std::ptrdiff_t;
            using value_type = tokenizer_context<>;
            using pointer = const value_type *;
            using reference_type = const value_type &;
            using iterator_category = std::input_iterator_tag;

            static constexpr const std::array<symbolic_group_type, 18> default_symbolic_groups = {
                symbolic_group_type("(", 1, false),
                symbolic_group_type(")", 1, false),
                symbolic_group_type(":", 2, false),
                symbolic_group_type("!", 1, false),
                symbolic_group_type("@", 1, false),
                symbolic_group_type("#", 1, false),
                symbolic_group_type("$", 1, false),
                symbolic_group_type("%", 1, false),
                symbolic_group_type("{", 1, false),
                symbolic_group_type("}", 1, false),
                symbolic_group_type("[", 1, false),
                symbolic_group_type("]", 1, false),
                symbolic_group_type(";", 1, false),
                symbolic_group_type(",", 1, false),
                symbolic_group_type("~", 1, false),
                symbolic_group_type("+-*/=><?", 2, false),
                symbolic_group_type("&", 2, false),
                symbolic_group_type("|", 2, false),
            };

            static constexpr const std::array<string_literal_marker_type, 3> default_string_literal_markers = {
                string_literal_marker_type('\''),
                string_literal_marker_type('\"'),
                string_literal_marker_type('`'),
            };

        private:
            line_list_type m_lines;
            std::size_t m_line_index;

            token_list_type m_tokens;
            std::size_t m_token_index;

            symbolic_group_list_type m_symbolic_groups;
            string_literal_marker_list_type m_string_literal_markers;

        public:
            static auto use_default(string_view_type p_text) -> tokenizer_iterator
            {
                return tokenizer_iterator(p_text, symbolic_group_list_type(default_symbolic_groups.cbegin(), default_symbolic_groups.cend()), string_literal_marker_list_type(default_string_literal_markers.cbegin(), default_string_literal_markers.cend()));
            }

            explicit tokenizer_iterator(string_view_type p_text, symbolic_group_list_type p_symbolic_groups, string_literal_marker_list_type p_string_literal_markers)
                : m_lines(to_lines(p_text)), m_line_index(0), m_tokens(), m_token_index(0), m_symbolic_groups(std::move(p_symbolic_groups)), m_string_literal_markers(std::move(p_string_literal_markers))
            {
                if (!m_lines.empty())
                    m_tokens = to_tokens(m_lines[0].m_content, m_symbolic_groups, m_string_literal_markers);
            }

            operator bool() const
            {
                return m_line_index < m_lines.size();
            }

            auto operator==(bool p_sentinel) const -> bool
            {
                return bool(*this) == p_sentinel;
            }

            auto operator*() const -> value_type
            {
                const bool is_first_token = m_token_index == 0;
                const bool is_last_token = m_token_index == (m_tokens.size() - 1);
                const bool is_first_line = m_line_index == 0;
                const bool is_last_line = m_line_index == (m_lines.size() - 1);
                return value_type(
                    m_tokens[m_token_index],
                    is_last_token ? optional_token_type() : m_tokens[m_token_index + 1],
                    is_first_token ? optional_token_type() : m_tokens[m_token_index - 1],
                    m_tokens.back(),
                    m_tokens.front(),
                    m_lines[m_line_index],
                    is_last_line ? optional_line_type() : m_lines[m_line_index + 1],
                    is_first_line ? optional_line_type() : m_lines[m_line_index - 1]);
            }

            auto operator++() -> tokenizer_iterator &
            {
                if (!*this)
                    return *this;

                const bool is_last_token = m_token_index == (m_tokens.size() - 1);
                if (is_last_token)
                {
                    m_token_index = 0;
                    ++m_line_index;
                    if (m_line_index < m_lines.size())
                        m_tokens = to_tokens(m_lines[m_line_index].m_content, m_symbolic_groups, m_string_literal_markers);
                }
                else
                    ++m_token_index;

                return *this;
            }

            auto operator++(int) -> tokenizer_iterator
            {
                tokenizer_iterator old = *this;
                ++(*this);
                return old;
            }
        };

    } // namespace implementation

    using tokenizer_iterator = implementation::tokenizer_iterator<>;

    auto tokens(string_view_type p_text)
    {
        return std::ranges::subrange(tokenizer_iterator::use_default(p_text), false);
    }

} // namespace ovis::tokenizer

#endif // OVIS_TOKENIZER_HPP