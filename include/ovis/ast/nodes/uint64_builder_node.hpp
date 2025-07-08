#ifndef OVIS_AST_NODES_UINT64_BUILDER_NODE_HPP
#define OVIS_AST_NODES_UINT64_BUILDER_NODE_HPP

#include <ovis/ast/nodes/uint64_builder_node.d.hpp>
#include <ovis/ast/nodes/uint32_builder_node.d.hpp>
#include <ovis/ast/nodes/uint16_builder_node.d.hpp>
#include <ovis/ast/nodes/uint8_builder_node.d.hpp>
#include <ovis/ast/nodes/xint_addition_builder_node.hpp>
#include <ovis/ast/nodes/xint_subtraction_builder_node.hpp>
#include <ovis/ast/nodes/xint_multiplication_builder_node.hpp>
#include <ovis/ast/nodes/uint_division_builder_node.hpp>
#include <ovis/ast/nodes/builder_node.hpp>

namespace ovis::ast
{

    namespace implementation
    {

        template <c_is_generator t_generator_type>
        class uint64_builder_node : public expression_builder_node<t_generator_type>
        {
        public:
            using generator_type = t_generator_type;
            using expression_builder_node_type = expression_builder_node<generator_type>;
            using base_type = expression_builder_node_type;
            using result_type = typename base_type::result_type;
            using expression_builder_node_box_type = typename base_type::expression_builder_node_box_type;
            using optional_token_type = typename base_type::optional_token_type;

        private:
            using xint_addition_builder_node_type = xint_addition_builder_node<generator_type>;
            using xint_subtraction_builder_node_type = xint_subtraction_builder_node<generator_type>;
            using xint_multiplication_builder_node_type = xint_multiplication_builder_node<generator_type>;
            using uint_division_builder_node_type = uint_division_builder_node<generator_type>;
            using uint32_builder_node_type = uint32_builder_node<generator_type>;
            using uint16_builder_node_type = uint16_builder_node<generator_type>;
            using uint8_builder_node_type = uint8_builder_node<generator_type>;

            uint64_type m_value;

        public:
            explicit uint64_builder_node(uint32_type p_value, optional_token_type p_token = optional_token_type())
                : base_type(std::move(p_token)), m_value(p_value) {}

            ~uint64_builder_node() override = default;

            auto generate() const -> result_type override
            {
                return generator_type::get_singleton().generate_uint64(m_value);
            }

            auto add(const expression_builder_node_box_type &p_node, bool p_reverse) const -> expression_builder_node_box_type override
            {
                ATTEMPT_RETURN_UNIFORM_BINARY_OPERATION(
                    expression_builder_node_type,
                    xint_addition_builder_node_type,
                    uint64_builder_node,
                    uint64_builder_node,
                    *this,
                    p_node,
                    p_reverse);

                ATTEMPT_RETURN_UNIFORM_BINARY_OPERATION(
                    expression_builder_node_type,
                    xint_addition_builder_node_type,
                    uint64_builder_node,
                    uint32_builder_node_type,
                    *this,
                    p_node,
                    p_reverse);

                ATTEMPT_RETURN_UNIFORM_BINARY_OPERATION(
                    expression_builder_node_type,
                    xint_addition_builder_node_type,
                    uint64_builder_node,
                    uint16_builder_node_type,
                    *this,
                    p_node,
                    p_reverse);

                ATTEMPT_RETURN_UNIFORM_BINARY_OPERATION(
                    expression_builder_node_type,
                    xint_addition_builder_node_type,
                    uint64_builder_node,
                    uint8_builder_node_type,
                    *this,
                    p_node,
                    p_reverse);

                return base_type::add(p_node, p_reverse);
            }

            auto subtract(const expression_builder_node_box_type &p_node, bool p_reverse) const -> expression_builder_node_box_type override
            {
                ATTEMPT_RETURN_UNIFORM_BINARY_OPERATION(
                    expression_builder_node_type,
                    xint_subtraction_builder_node_type,
                    uint64_builder_node,
                    uint64_builder_node,
                    *this,
                    p_node,
                    p_reverse);

                ATTEMPT_RETURN_UNIFORM_BINARY_OPERATION(
                    expression_builder_node_type,
                    xint_subtraction_builder_node_type,
                    uint64_builder_node,
                    uint32_builder_node_type,
                    *this,
                    p_node,
                    p_reverse);

                ATTEMPT_RETURN_UNIFORM_BINARY_OPERATION(
                    expression_builder_node_type,
                    xint_subtraction_builder_node_type,
                    uint64_builder_node,
                    uint16_builder_node_type,
                    *this,
                    p_node,
                    p_reverse);

                ATTEMPT_RETURN_UNIFORM_BINARY_OPERATION(
                    expression_builder_node_type,
                    xint_subtraction_builder_node_type,
                    uint64_builder_node,
                    uint8_builder_node_type,
                    *this,
                    p_node,
                    p_reverse);

                return base_type::subtract(p_node, p_reverse);
            }

            auto multiply(const expression_builder_node_box_type &p_node, bool p_reverse) const -> expression_builder_node_box_type override
            {
                ATTEMPT_RETURN_UNIFORM_BINARY_OPERATION(
                    expression_builder_node_type,
                    xint_multiplication_builder_node_type,
                    uint64_builder_node,
                    uint64_builder_node,
                    *this,
                    p_node,
                    p_reverse);

                ATTEMPT_RETURN_UNIFORM_BINARY_OPERATION(
                    expression_builder_node_type,
                    xint_multiplication_builder_node_type,
                    uint64_builder_node,
                    uint32_builder_node_type,
                    *this,
                    p_node,
                    p_reverse);

                ATTEMPT_RETURN_UNIFORM_BINARY_OPERATION(
                    expression_builder_node_type,
                    xint_multiplication_builder_node_type,
                    uint64_builder_node,
                    uint16_builder_node_type,
                    *this,
                    p_node,
                    p_reverse);

                ATTEMPT_RETURN_UNIFORM_BINARY_OPERATION(
                    expression_builder_node_type,
                    xint_multiplication_builder_node_type,
                    uint64_builder_node,
                    uint8_builder_node_type,
                    *this,
                    p_node,
                    p_reverse);

                return base_type::multiply(p_node, p_reverse);
            }

            auto divide(const expression_builder_node_box_type &p_node, bool p_reverse) const -> expression_builder_node_box_type override
            {
                ATTEMPT_RETURN_UNIFORM_BINARY_OPERATION(
                    expression_builder_node_type,
                    uint_division_builder_node_type,
                    uint64_builder_node,
                    uint64_builder_node,
                    *this,
                    p_node,
                    p_reverse);

                ATTEMPT_RETURN_UNIFORM_BINARY_OPERATION(
                    expression_builder_node_type,
                    uint_division_builder_node_type,
                    uint64_builder_node,
                    uint32_builder_node_type,
                    *this,
                    p_node,
                    p_reverse);

                ATTEMPT_RETURN_UNIFORM_BINARY_OPERATION(
                    expression_builder_node_type,
                    uint_division_builder_node_type,
                    uint64_builder_node,
                    uint16_builder_node_type,
                    *this,
                    p_node,
                    p_reverse);

                ATTEMPT_RETURN_UNIFORM_BINARY_OPERATION(
                    expression_builder_node_type,
                    uint_division_builder_node_type,
                    uint64_builder_node,
                    uint8_builder_node_type,
                    *this,
                    p_node,
                    p_reverse);

                return base_type::divide(p_node, p_reverse);
            }

            auto get() const -> uint64_type { return m_value; }
        };

    } // namespace implementation

} // namespace ovis::ast

#include <ovis/ast/nodes/uint32_builder_node.hpp>
#include <ovis/ast/nodes/uint16_builder_node.hpp>
#include <ovis/ast/nodes/uint8_builder_node.hpp>

#endif // OVIS_AST_NODES_UINT64_BUILDER_NODE_HPP