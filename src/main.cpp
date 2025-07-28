#include <iostream>
#include <ovis/ovis.hpp>
#include <string_view>

using namespace std::literals::string_view_literals;

int main()
{
    auto something = ovis::ast::implementation::expression_builder_node<ovis::ast::llvm_generator>::expression_builder_node_box_type(new ovis::ast::implementation::int8_builder_node<ovis::ast::llvm_generator>(1));
    auto another_thing = ovis::ast::implementation::cast_int_to_float32_builder_node<ovis::ast::llvm_generator>(std::move(something));

    auto float_result = ovis::ast::llvm_generator::get_singleton().generate_uint16(1234);
    auto casted = llvm::cast<llvm::ConstantInt>(float_result);
    casted->getValue().print(llvm::outs(), false);
}