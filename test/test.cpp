#include <catch2/catch_test_macros.hpp>
#include <tokenizer.hpp>
#include <iostream>

TEST_CASE("Tokenize strings.", "[tokenizer]")
{
    using namespace std::literals::string_view_literals;
    using namespace ovis::tokenizer;
    using token_context = tokenizer_iterator::value_type;
    using validator_type = std::pair<token_type, line_type>;

    const ovis::string_view_type text = ("// This is a comment.\n"
                                         "\n"
                                         "var names = ['Richard Steward', 'Peter Griffin', 'Albert Einstein'];");

    const auto validators = std::to_array<validator_type>({
        validator_type(
            token_type("//"sv, token_metadata_type(false, true)),
            line_type(1, "// This is a comment."sv)),
        validator_type(
            token_type("This"sv, token_metadata_type(false, false)),
            line_type(1, "// This is a comment."sv)),
        validator_type(
            token_type("is"sv, token_metadata_type(false, false)),
            line_type(1, "// This is a comment."sv)),
        validator_type(
            token_type("a"sv, token_metadata_type(false, false)),
            line_type(1, "// This is a comment."sv)),
        validator_type(
            token_type("comment"sv, token_metadata_type(false, false)),
            line_type(1, "// This is a comment."sv)),
        validator_type(
            token_type("."sv, token_metadata_type(false, true)),
            line_type(1, "// This is a comment."sv)),
        validator_type(
            token_type("var"sv, token_metadata_type(false, false)),
            line_type(3, "var names = ['Richard Steward', 'Peter Griffin', 'Albert Einstein'];"sv)),
        validator_type(
            token_type("names"sv, token_metadata_type(false, false)),
            line_type(3, "var names = ['Richard Steward', 'Peter Griffin', 'Albert Einstein'];"sv)),
        validator_type(
            token_type("="sv, token_metadata_type(false, true)),
            line_type(3, "var names = ['Richard Steward', 'Peter Griffin', 'Albert Einstein'];"sv)),
        validator_type(
            token_type("["sv, token_metadata_type(false, true)),
            line_type(3, "var names = ['Richard Steward', 'Peter Griffin', 'Albert Einstein'];"sv)),
        validator_type(
            token_type("Richard Steward"sv, token_metadata_type(true, false)),
            line_type(3, "var names = ['Richard Steward', 'Peter Griffin', 'Albert Einstein'];"sv)),
        validator_type(
            token_type(","sv, token_metadata_type(false, true)),
            line_type(3, "var names = ['Richard Steward', 'Peter Griffin', 'Albert Einstein'];"sv)),
        validator_type(
            token_type("Peter Griffin"sv, token_metadata_type(true, false)),
            line_type(3, "var names = ['Richard Steward', 'Peter Griffin', 'Albert Einstein'];"sv)),
        validator_type(
            token_type(","sv, token_metadata_type(false, true)),
            line_type(3, "var names = ['Richard Steward', 'Peter Griffin', 'Albert Einstein'];"sv)),
        validator_type(
            token_type("Albert Einstein"sv, token_metadata_type(true, false)),
            line_type(3, "var names = ['Richard Steward', 'Peter Griffin', 'Albert Einstein'];"sv)),
        validator_type(
            token_type("]"sv, token_metadata_type(false, true)),
            line_type(3, "var names = ['Richard Steward', 'Peter Griffin', 'Albert Einstein'];"sv)),
        validator_type(
            token_type(";"sv, token_metadata_type(false, true)),
            line_type(3, "var names = ['Richard Steward', 'Peter Griffin', 'Albert Einstein'];"sv)),
    });

    for (auto [context, validator] : std::views::zip(std::ranges::subrange(tokenizer_iterator(text), false), validators))
    {
        REQUIRE(context.get_current_token() == validator.first);
        REQUIRE(context.get_current_line() == validator.second);
    }
}