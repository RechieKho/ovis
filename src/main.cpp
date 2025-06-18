#include <iostream>
#include <ovis/tokenizer.hpp>
#include <string_view>

using namespace std::literals::string_view_literals;

int main()
{
    for (auto context : ovis::tokenizer::tokens("Hello world.\n\n\nvar 'seomthing somewerwe'"))
    {
        std::cout << context.get_current_token().m_content << std::endl;
    }
}