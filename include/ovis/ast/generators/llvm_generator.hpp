#ifndef OVIS_AST_GENERATORS_LLVM_GENERATOR_HPP
#define OVIS_AST_GENERATORS_LLVM_GENERATOR_HPP

#include <llvm/ADT/APFloat.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/LLVMContext.h>

#include <ovis/def.hpp>
#include <ovis/ast/generators/generator.hpp>

namespace ovis::ast
{

    namespace implementation
    {

        template <typename = void>
        class llvm_generator final
        {
        public:
            using result_type = llvm::Value *;
            using box_type = std::unique_ptr<llvm_generator>;
            using context_type = llvm::LLVMContext;
            using context_box_type = std::unique_ptr<context_type>;

        private:
            context_box_type context;

            static box_type singleton;

        public:
            static auto get_singleton() -> llvm_generator &
            {
                if (!singleton)
                    singleton = std::make_unique<context_type>();
                return *singleton;
            }

            explicit llvm_generator()
                : context(std::make_unique<context_type>()) {}

            auto generate_float(float_type p_float) const -> result_type
            {
                return llvm::ConstantFP::get(context.get(), llvm::APFloat(p_float));
            }
        };
        static_assert(c_is_generator<llvm_generator<>>);

    } // namespace implementation

    using llvm_generator = implementation::llvm_generator<>;

} // namespace ovis::ast

#endif // OVIS_AST_GENERATORS_LLVM_GENERATOR_HPP