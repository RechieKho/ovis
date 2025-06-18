#include <iostream>
#include <tokenizer.hpp>
#include <string_view>

using namespace std::literals::string_view_literals;

int main()
{
    auto it = ovis::tokenizer::tokenizer_iterator("var something := 0123456789;\n\nprint('Hello world');");
    while (it)
    {
        std::cout << (*it).get_current_token().first << std::endl;
        std::cout << (*it).get_current_token().second.m_is_symbol << std::endl;
        std::cout << std::endl;
        ++it;
    }
}