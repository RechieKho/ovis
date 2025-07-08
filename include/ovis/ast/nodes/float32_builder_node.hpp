#ifndef OVIS_AST_NODES_FLOAT32_BUILDER_NODE_HPP
#define OVIS_AST_NODES_FLOAT32_BUILDER_NODE_HPP

#include <ovis/ast/nodes/float32_builder_node.d.hpp>
#include <ovis/ast/nodes/float_addition_builder_node.hpp>
#include <ovis/ast/nodes/float_subtraction_builder_node.hpp>
#include <ovis/ast/nodes/float_multiplication_builder_node.hpp>
#include <ovis/ast/nodes/float_division_builder_node.hpp>

namespace ovis::ast
{

    namespace implementation
    {

        template <c_is_generator t_generator_type>
        class float32_builder_node final : public expression_builder_node<t_generator_type>
        {
        public:
            using generator_type = t_generator_type;
            using expression_builder_node_type = expression_builder_node<generator_type>;
            using base_type = expression_builder_node_type;
            using result_type = typename base_type::result_type;
            using expression_builder_node_box_type = typename base_type::expression_builder_node_box_type;
            using optional_token_type = typename base_type::optional_token_type;

        private:
            using float_addition_builder_node_type = float_addition_builder_node<generator_type>;
            using float_subtraction_builder_node_type = float_subtraction_builder_node<generator_type>;
            using float_multiplication_builder_node_type = float_multiplication_builder_node<generator_type>;
            using float_division_builder_node_type = float_division_builder_node<generator_type>;

            float32_type m_value;

        public:
            explicit float32_builder_node(float32_type p_value, optional_token_type p_token = optional_token_type())
                : base_type(std::move(p_token)), m_value(p_value) {}

            ~float32_builder_node() override = default;

            auto generate() const -> result_type override
            {
                return generator_type::get_singleton().generate_float32(m_value);
            }

            auto add(const expression_builder_node_box_type &p_node, bool p_reverse) const -> expression_builder_node_box_type override
            {
                ATTEMPT_RETURN_UNIFORM_BINARY_OPERATION(
                    expression_builder_node_type,
                    float_addition_builder_node_type,
                    float32_builder_node,
                    float32_builder_node,
                    *this,
                    p_node,
                    p_reverse);

                return base_type::add(p_node, p_reverse);
            }

            auto subtract(const expression_builder_node_box_type &p_node, bool p_reverse) const -> expression_builder_node_box_type override
            {
                ATTEMPT_RETURN_UNIFORM_BINARY_OPERATION(
                    expression_builder_node_type,
                    float_subtraction_builder_node,
                    float32_builder_node,
                    float32_builder_node,
                    *this,
                    p_node,
                    p_reverse);

                return base_type::subtract(p_node, p_reverse);
            }

            auto multiply(const expression_builder_node_box_type &p_node, bool p_reverse) const -> expression_builder_node_box_type override
            {
                ATTEMPT_RETURN_UNIFORM_BINARY_OPERATION(
                    expression_builder_node_type,
                    float_multiplication_builder_node_type,
                    float32_builder_node,
                    float32_builder_node,
                    *this,
                    p_node,
                    p_reverse);

                return base_type::multiply(p_node, p_reverse);
            }

            auto divide(const expression_builder_node_box_type &p_node, bool p_reverse) const -> expression_builder_node_box_type override
            {
                ATTEMPT_RETURN_UNIFORM_BINARY_OPERATION(
                    expression_builder_node_type,
                    float_division_builder_node_type,
                    float32_builder_node,
                    float32_builder_node,
                    *this,
                    p_node,
                    p_reverse);

                return base_type::divide(p_node, p_reverse);
            }

            auto get() const -> float32_type { return m_value; }
        };

    } // namespace implementation

} // namespace ovis::ast

#include <ovis/ast/nodes/int32_builder_node.hpp>

#endif // OVIS_AST_NODES_FLOAT32_BUILDER_NODE_HPP