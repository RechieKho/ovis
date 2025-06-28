#include <catch2/catch_test_macros.hpp>
#include <ovis/ovis.hpp>

using namespace std::literals::string_view_literals;

TEST_CASE("Tokenize strings.", "[tokenizer]")
{
    using namespace ovis::tokenizer;
    using token_context = tokenizer_iterator::value_type;
    using validator_type = token_context;

    const ovis::string_view_type text = ("// This is a comment.\n"
                                         "\n"
                                         "var names = ['Richard Steward', 'Peter Griffin', 'Albert Einstein'];");

    const auto validators = std::to_array<validator_type>({
        validator_type(
            token_type(token_metadata_type(false, true), "//"sv),
            token_type(token_metadata_type(false, false), "This"sv),
            optional_token_type(),
            token_type(token_metadata_type(false, false), "comment."sv),
            token_type(token_metadata_type(false, true), "//"sv),
            line_type(line_metadata_type(1), "// This is a comment."sv),
            line_type(line_metadata_type(3), "var names = ['Richard Steward', 'Peter Griffin', 'Albert Einstein'];"sv),
            optional_line_type()),
        validator_type(
            token_type(token_metadata_type(false, false), "This"sv),
            token_type(token_metadata_type(false, false), "is"sv),
            token_type(token_metadata_type(false, true), "//"sv),
            token_type(token_metadata_type(false, false), "comment."sv),
            token_type(token_metadata_type(false, true), "//"sv),
            line_type(line_metadata_type(1), "// This is a comment."sv),
            line_type(line_metadata_type(3), "var names = ['Richard Steward', 'Peter Griffin', 'Albert Einstein'];"sv),
            optional_line_type()),
        validator_type(
            token_type(token_metadata_type(false, false), "is"sv),
            token_type(token_metadata_type(false, false), "a"sv),
            token_type(token_metadata_type(false, false), "This"sv),
            token_type(token_metadata_type(false, false), "comment."sv),
            token_type(token_metadata_type(false, true), "//"sv),
            line_type(line_metadata_type(1), "// This is a comment."sv),
            line_type(line_metadata_type(3), "var names = ['Richard Steward', 'Peter Griffin', 'Albert Einstein'];"sv),
            optional_line_type()),
        validator_type(
            token_type(token_metadata_type(false, false), "a"sv),
            token_type(token_metadata_type(false, false), "comment."sv),
            token_type(token_metadata_type(false, false), "is"sv),
            token_type(token_metadata_type(false, false), "comment."sv),
            token_type(token_metadata_type(false, true), "//"sv),
            line_type(line_metadata_type(1), "// This is a comment."sv),
            line_type(line_metadata_type(3), "var names = ['Richard Steward', 'Peter Griffin', 'Albert Einstein'];"sv),
            optional_line_type()),
        validator_type(
            token_type(token_metadata_type(false, false), "comment."sv),
            optional_token_type(),
            token_type(token_metadata_type(false, false), "a"sv),
            token_type(token_metadata_type(false, false), "comment."sv),
            token_type(token_metadata_type(false, true), "//"sv),
            line_type(line_metadata_type(1), "// This is a comment."sv),
            line_type(line_metadata_type(3), "var names = ['Richard Steward', 'Peter Griffin', 'Albert Einstein'];"sv),
            optional_line_type()),
        validator_type(
            token_type(token_metadata_type(false, false), "var"sv),
            token_type(token_metadata_type(false, false), "names"sv),
            optional_token_type(),
            token_type(token_metadata_type(false, true), ";"sv),
            token_type(token_metadata_type(false, false), "var"sv),
            line_type(line_metadata_type(3), "var names = ['Richard Steward', 'Peter Griffin', 'Albert Einstein'];"sv),
            optional_line_type(),
            line_type(line_metadata_type(1), "// This is a comment."sv)),
        validator_type(
            token_type(token_metadata_type(false, false), "names"sv),
            token_type(token_metadata_type(false, true), "="sv),
            token_type(token_metadata_type(false, false), "var"sv),
            token_type(token_metadata_type(false, true), ";"sv),
            token_type(token_metadata_type(false, false), "var"sv),
            line_type(line_metadata_type(3), "var names = ['Richard Steward', 'Peter Griffin', 'Albert Einstein'];"sv),
            optional_line_type(),
            line_type(line_metadata_type(1), "// This is a comment."sv)),
        validator_type(
            token_type(token_metadata_type(false, true), "="sv),
            token_type(token_metadata_type(false, true), "["sv),
            token_type(token_metadata_type(false, false), "names"sv),
            token_type(token_metadata_type(false, true), ";"sv),
            token_type(token_metadata_type(false, false), "var"sv),
            line_type(line_metadata_type(3), "var names = ['Richard Steward', 'Peter Griffin', 'Albert Einstein'];"sv),
            optional_line_type(),
            line_type(line_metadata_type(1), "// This is a comment."sv)),
        validator_type(
            token_type(token_metadata_type(false, true), "["sv),
            token_type(token_metadata_type(true, false), "Richard Steward"sv),
            token_type(token_metadata_type(false, true), "="sv),
            token_type(token_metadata_type(false, true), ";"sv),
            token_type(token_metadata_type(false, false), "var"sv),
            line_type(line_metadata_type(3), "var names = ['Richard Steward', 'Peter Griffin', 'Albert Einstein'];"sv),
            optional_line_type(),
            line_type(line_metadata_type(1), "// This is a comment."sv)),
        validator_type(
            token_type(token_metadata_type(true, false), "Richard Steward"sv),
            token_type(token_metadata_type(false, true), ","sv),
            token_type(token_metadata_type(false, true), "["sv),
            token_type(token_metadata_type(false, true), ";"sv),
            token_type(token_metadata_type(false, false), "var"sv),
            line_type(line_metadata_type(3), "var names = ['Richard Steward', 'Peter Griffin', 'Albert Einstein'];"sv),
            optional_line_type(),
            line_type(line_metadata_type(1), "// This is a comment."sv)),
        validator_type(
            token_type(token_metadata_type(false, true), ","sv),
            token_type(token_metadata_type(true, false), "Peter Griffin"sv),
            token_type(token_metadata_type(true, false), "Richard Steward"sv),
            token_type(token_metadata_type(false, true), ";"sv),
            token_type(token_metadata_type(false, false), "var"sv),
            line_type(line_metadata_type(3), "var names = ['Richard Steward', 'Peter Griffin', 'Albert Einstein'];"sv),
            optional_line_type(),
            line_type(line_metadata_type(1), "// This is a comment."sv)),
        validator_type(
            token_type(token_metadata_type(true, false), "Peter Griffin"sv),
            token_type(token_metadata_type(false, true), ","sv),
            token_type(token_metadata_type(false, true), ","sv),
            token_type(token_metadata_type(false, true), ";"sv),
            token_type(token_metadata_type(false, false), "var"sv),
            line_type(line_metadata_type(3), "var names = ['Richard Steward', 'Peter Griffin', 'Albert Einstein'];"sv),
            optional_line_type(),
            line_type(line_metadata_type(1), "// This is a comment."sv)),
        validator_type(
            token_type(token_metadata_type(false, true), ","sv),
            token_type(token_metadata_type(true, false), "Albert Einstein"sv),
            token_type(token_metadata_type(true, false), "Peter Griffin"sv),
            token_type(token_metadata_type(false, true), ";"sv),
            token_type(token_metadata_type(false, false), "var"sv),
            line_type(line_metadata_type(3), "var names = ['Richard Steward', 'Peter Griffin', 'Albert Einstein'];"sv),
            optional_line_type(),
            line_type(line_metadata_type(1), "// This is a comment."sv)),
        validator_type(
            token_type(token_metadata_type(true, false), "Albert Einstein"sv),
            token_type(token_metadata_type(false, true), "]"sv),
            token_type(token_metadata_type(false, true), ","sv),
            token_type(token_metadata_type(false, true), ";"sv),
            token_type(token_metadata_type(false, false), "var"sv),
            line_type(line_metadata_type(3), "var names = ['Richard Steward', 'Peter Griffin', 'Albert Einstein'];"sv),
            optional_line_type(),
            line_type(line_metadata_type(1), "// This is a comment."sv)),
        validator_type(
            token_type(token_metadata_type(false, true), "]"sv),
            token_type(token_metadata_type(false, true), ";"sv),
            token_type(token_metadata_type(true, false), "Albert Einstein"sv),
            token_type(token_metadata_type(false, true), ";"sv),
            token_type(token_metadata_type(false, false), "var"sv),
            line_type(line_metadata_type(3), "var names = ['Richard Steward', 'Peter Griffin', 'Albert Einstein'];"sv),
            optional_line_type(),
            line_type(line_metadata_type(1), "// This is a comment."sv)),
        validator_type(
            token_type(token_metadata_type(false, true), ";"sv),
            optional_token_type(),
            token_type(token_metadata_type(false, true), "]"sv),
            token_type(token_metadata_type(false, true), ";"sv),
            token_type(token_metadata_type(false, false), "var"sv),
            line_type(line_metadata_type(3), "var names = ['Richard Steward', 'Peter Griffin', 'Albert Einstein'];"sv),
            optional_line_type(),
            line_type(line_metadata_type(1), "// This is a comment."sv)),
    });

    for (auto [context, validator] : std::views::zip(tokens(text), validators))
    {
        INFO("Token content: " << context.get_current_token().m_content);
        REQUIRE(context == validator);
    }
}

TEST_CASE("Parse pure uint.", "[ast]")
{
    using expression_builder_node_type = ovis::ast::default_expression_builder_node_type;

    {
        expression_builder_node_type::optional_pure_uint_type result = expression_builder_node_type::parse_pure_uint("010"sv);
        REQUIRE(result.has_value());
        REQUIRE(result.value() == 10);
    }

    {
        expression_builder_node_type::optional_pure_uint_type result = expression_builder_node_type::parse_pure_uint("0b10"sv);
        REQUIRE(result.has_value());
        REQUIRE(result.value() == 2);
    }

    {
        expression_builder_node_type::optional_pure_uint_type result = expression_builder_node_type::parse_pure_uint("0o10"sv);
        REQUIRE(result.has_value());
        REQUIRE(result.value() == 8);
    }

    {
        expression_builder_node_type::optional_pure_uint_type result = expression_builder_node_type::parse_pure_uint("0d10"sv);
        REQUIRE(result.has_value());
        REQUIRE(result.value() == 10);
    }

    {
        expression_builder_node_type::optional_pure_uint_type result = expression_builder_node_type::parse_pure_uint("0x10"sv);
        REQUIRE(result.has_value());
        REQUIRE(result.value() == 16);
    }

    {
        expression_builder_node_type::optional_pure_uint_type result = expression_builder_node_type::parse_pure_uint("-10"sv);
        REQUIRE(!result.has_value());
    }

    {
        expression_builder_node_type::optional_pure_uint_type result = expression_builder_node_type::parse_pure_uint("1e0"sv);
        REQUIRE(!result.has_value());
    }
}