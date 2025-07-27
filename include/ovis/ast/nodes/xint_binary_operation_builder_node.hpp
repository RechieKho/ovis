#ifndef OVIS_AST_NODES_XINT_BINARY_OPERATION_BUILDER_NODE_HPP
#define OVIS_AST_NODES_XINT_BINARY_OPERATION_BUILDER_NODE_HPP

#include <ovis/ast/nodes/binary_operation_builder_node.hpp>
#include <ovis/ast/nodes/xint_feature.hpp>

namespace ovis::ast
{

    namespace implementation
    {

        template <c_is_generator t_generator_type>
        class xint_binary_operation_builder_node : public binary_operation_builder_node<t_generator_type>, public xint_feature<>
        {
        public:
            using generator_type = t_generator_type;
            using base_type = binary_operation_builder_node<generator_type>;
            using feature_base_type = xint_feature<>;
            using result_type = typename base_type::result_type;
            using expression_builder_node_box_type = typename base_type::expression_builder_node_box_type;
            using optional_token_type = typename base_type::optional_token_type;

        protected:
            explicit xint_binary_operation_builder_node(
                expression_builder_node_box_type p_left_hand_value,
                expression_builder_node_box_type p_right_hand_value,
                bool p_is_signed,
                optional_token_type p_token = optional_token_type())
                : base_type(std::move(p_left_hand_value), std::move(p_right_hand_value), std::move(p_token)), feature_base_type(p_is_signed) {}

        private:
        public:
        };

    } // namespace implementation

} // namespace ovis::ast

#endif // OVIS_AST_NODES_XINT_BINARY_OPERATION_BUILDER_NODE_HPP