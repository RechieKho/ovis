#ifndef OVIS_AST_NODES_FLOAT_SUBTRACTION_BUILDER_NODE_HPP
#define OVIS_AST_NODES_FLOAT_SUBTRACTION_BUILDER_NODE_HPP

#include <ovis/ast/nodes/float_binary_operation_builder_node.hpp>

namespace ovis::ast
{

    namespace implementation
    {

        template <c_is_generator t_generator_type>
        class float_subtraction_builder_node final : public float_binary_operation_builder_node<t_generator_type>
        {
        public:
            using generator_type = t_generator_type;
            using binary_operation_builder_node_type = binary_operation_builder_node<generator_type>;
            using base_type = float_binary_operation_builder_node<generator_type>;
            using result_type = typename base_type::result_type;
            using expression_builder_node_box_type = typename base_type::expression_builder_node_box_type;

        protected:
        private:
        public:
            explicit float_subtraction_builder_node(
                expression_builder_node_box_type p_left_hand_value,
                expression_builder_node_box_type p_right_hand_value)
                : binary_operation_builder_node_type(std::move(p_left_hand_value), std::move(p_right_hand_value)) {}

            ~float_subtraction_builder_node() override = default;

            auto generate() const -> result_type override
            {
                return generator_type::get_singleton().generate_float_subtraction(
                    binary_operation_builder_node_type::m_left_side_value->generate(),
                    binary_operation_builder_node_type::m_right_side_value->generate());
            }
        };

    } // namespace implementation

} // namespace ovis::ast

#endif // OVIS_AST_NODES_FLOAT_SUBTRACTION_BUILDER_NODE_HPP