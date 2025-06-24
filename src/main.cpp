#include <iostream>
#include <ovis/ovis.hpp>
#include <string_view>

using namespace std::literals::string_view_literals;

int main()
{
    auto float_result = ovis::ast::llvm_generator::get_singleton().generate_uint16(1234);
    auto casted = llvm::cast<llvm::ConstantInt>(float_result);
    casted->getValue().print(llvm::outs(), false);
}