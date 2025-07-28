#ifndef OVIS_AST_NODES_FLOAT_UNARY_OPERATION_BUILDER_NODE_HPP
#define OVIS_AST_NODES_FLOAT_UNARY_OPERATION_BUILDER_NODE_HPP

#include <ovis/ast/nodes/unary_operation_builder_node.hpp>
#include <ovis/ast/nodes/float_feature.hpp>

namespace ovis::ast
{

    namespace implementation
    {

        template <c_is_generator t_generator_type>
        class float_unary_operation_builder_node : public unary_operation_builder_node<t_generator_type>, public float_feature<>
        {
        public:
            using generator_type = t_generator_type;
            using base_type = unary_operation_builder_node<generator_type>;
            using feature_base_type = float_feature<>;
            using result_type = typename base_type::result_type;
            using expression_builder_node_box_type = typename base_type::expression_builder_node_box_type;
            using optional_token_type = typename base_type::optional_token_type;

        protected:
            explicit float_unary_operation_builder_node(
                expression_builder_node_box_type p_value,
                optional_token_type p_token = optional_token_type())
                : base_type(std::move(p_value), std::move(p_token)), feature_base_type() {}

        private:
        public:
        };

    } // namespace implementation

} // namespace ovis::ast

#endif // OVIS_AST_NODES_FLOAT_UNARY_OPERATION_BUILDER_NODE_HPP