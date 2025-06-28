#ifndef OVIS_AST_GENERATORS_LLVM_GENERATOR_HPP
#define OVIS_AST_GENERATORS_LLVM_GENERATOR_HPP

#include <expected>

#include <llvm/Support/Error.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>
#include <llvm/ADT/APFloat.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/ADT/APSInt.h>
#include <llvm/ADT/APInt.h>

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
            using ir_builder_type = llvm::IRBuilder<>;
            using ir_builder_box_type = std::unique_ptr<ir_builder_type>;

        private:
            context_box_type context;
            ir_builder_box_type ir_builder;

            explicit llvm_generator()
                : context(std::make_unique<context_type>()), ir_builder(std::make_unique<ir_builder_type>(*context)) {}

        public:
            static auto get_singleton() -> llvm_generator &
            {
                static box_type singleton;
                if (!singleton)
                    singleton = box_type(new llvm_generator());
                return *singleton;
            }

            auto generate_float32(float p_value) const -> result_type
            {
                return llvm::ConstantFP::get(*context, llvm::APFloat(p_value));
            }

            auto generate_float64(double p_value) const -> result_type
            {
                return llvm::ConstantFP::get(*context, llvm::APFloat(p_value));
            }

            auto generate_uint8(uint8_t p_value) const -> result_type
            {
                return llvm::ConstantInt::get(*context, llvm::APInt(8, p_value, false, true));
            }

            auto generate_int8(int8_t p_value) const -> result_type
            {
                return llvm::ConstantInt::get(*context, llvm::APInt(8, p_value, true, true));
            }

            auto generate_uint16(uint16_t p_value) const -> result_type
            {
                return llvm::ConstantInt::get(*context, llvm::APInt(16, p_value, false, true));
            }

            auto generate_int16(int16_t p_value) const -> result_type
            {
                return llvm::ConstantInt::get(*context, llvm::APInt(16, p_value, true, true));
            }

            auto generate_uint32(uint32_t p_value) const -> result_type
            {
                return llvm::ConstantInt::get(*context, llvm::APInt(32, p_value, false, true));
            }

            auto generate_int32(int32_t p_value) const -> result_type
            {
                return llvm::ConstantInt::get(*context, llvm::APInt(32, p_value, true, true));
            }

            auto generate_uint64(uint64_t p_value) const -> result_type
            {
                return llvm::ConstantInt::get(*context, llvm::APInt(64, p_value, false, true));
            }

            auto generate_int64(int64_t p_value) const -> result_type
            {
                return llvm::ConstantInt::get(*context, llvm::APInt(64, p_value, true, true));
            }

            auto generate_float_addition(result_type p_left_side_value, result_type p_right_side_value) const -> result_type
            {
                return ir_builder->CreateFAdd(p_left_side_value, p_right_side_value, "float_addition_result");
            }

            auto generate_float_subtraction(result_type p_left_side_value, result_type p_right_side_value) const -> result_type
            {
                return ir_builder->CreateFSub(p_left_side_value, p_right_side_value, "float_subtraction_result");
            }

            auto generate_float_multiplication(result_type p_left_side_value, result_type p_right_side_value) const -> result_type
            {
                return ir_builder->CreateFMul(p_left_side_value, p_right_side_value, "float_multiplication_result");
            }

            auto generate_float_division(result_type p_left_side_value, result_type p_right_side_value) const -> result_type
            {
                return ir_builder->CreateFDiv(p_left_side_value, p_right_side_value, "float_division_result");
            }

            auto generate_xint_addition(result_type p_left_side_value, result_type p_right_side_value) const -> result_type
            {
                return ir_builder->CreateAdd(p_left_side_value, p_right_side_value, "xint_addition_result");
            }

            auto generate_xint_subtraction(result_type p_left_side_value, result_type p_right_side_value) const -> result_type
            {
                return ir_builder->CreateSub(p_left_side_value, p_right_side_value, "xint_subtraction_result");
            }

            auto generate_xint_multiplication(result_type p_left_side_value, result_type p_right_side_value) const -> result_type
            {
                return ir_builder->CreateMul(p_left_side_value, p_right_side_value, "xint_multiplication_result");
            }

            auto generate_int_division(result_type p_left_side_value, result_type p_right_side_value) const -> result_type
            {
                return ir_builder->CreateSDiv(p_left_side_value, p_right_side_value, "int_division_result");
            }

            auto generate_uint_division(result_type p_left_side_value, result_type p_right_side_value) const -> result_type
            {
                return ir_builder->CreateUDiv(p_left_side_value, p_right_side_value, "uint_division_result");
            }
        };
        static_assert(c_is_generator<llvm_generator<>>);

    } // namespace implementation

    using llvm_generator = implementation::llvm_generator<>;

} // namespace ovis::ast

#endif // OVIS_AST_GENERATORS_LLVM_GENERATOR_HPP