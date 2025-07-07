#ifndef OVIS_AST_NODES_UINT_DIVISION_BUILDER_NODE_HPP
#define OVIS_AST_NODES_UINT_DIVISION_BUILDER_NODE_HPP

#include <ovis/ast/nodes/xint_binary_operation_builder_node.hpp>

namespace ovis::ast
{

    namespace implementation
    {

        template <c_is_generator t_generator_type>
        class uint_division_builder_node final : public xint_binary_operation_builder_node<t_generator_type>
        {
        public:
            using generator_type = t_generator_type;
            using binary_operation_builder_node_type = binary_operation_builder_node<generator_type>;
            using base_type = xint_binary_operation_builder_node<generator_type>;
            using result_type = typename base_type::result_type;
            using expression_builder_node_box_type = typename base_type::expression_builder_node_box_type;
            using optional_token_type = typename base_type::optional_token_type;

        protected:
        private:
        public:
            explicit uint_division_builder_node(
                expression_builder_node_box_type p_left_hand_value,
                expression_builder_node_box_type p_right_hand_value,
                optional_token_type p_token = optional_token_type())
                : base_type(std::move(p_left_hand_value), std::move(p_right_hand_value), std::move(p_token)) {}

            ~uint_division_builder_node() override = default;

            auto generate() const -> result_type override
            {
                return generator_type::get_singleton().generate_uint_division(
                    binary_operation_builder_node_type::m_left_side_value->generate(),
                    binary_operation_builder_node_type::m_right_side_value->generate());
            }
        };

    } // namespace implementation

} // namespace ovis::ast

#endif // OVIS_AST_NODES_UINT_DIVISION_BUILDER_NODE_HPP