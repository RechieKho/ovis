#ifndef OVIS_AST_GENERATORS_LLVM_GENERATOR_HPP
#define OVIS_AST_GENERATORS_LLVM_GENERATOR_HPP

#include <ovis/def.hpp>
#include <ovis/ast/generators/generator.hpp>

namespace ovis::ast
{

    namespace implementation
    {

        template <typename = void>
        struct llvm_generator final : public proxy_type
        {
        };
        static_assert(c_is_generator<llvm_generator<>>);

    } // namespace implementation

    using llvm_generator = implementation::llvm_generator<>;

} // namespace ovis::ast

#endif // OVIS_AST_GENERATORS_LLVM_GENERATOR_HPP