#ifndef OVIS_AST_NODES_FLOAT_BUILDER_NODE_HPP
#define OVIS_AST_NODES_FLOAT_BUILDER_NODE_HPP

#include <ovis/ast/nodes/expression_builder_node.hpp>

namespace ovis::ast
{

    namespace implementation
    {

        template <c_is_generator t_generator_type>
        class float_builder_node : public expression_builder_node<t_generator_type>
        {
        public:
            using generator_type = t_generator_type;
            using expression_builder_node_type = expression_builder_node<generator_type>;
            using base_type = expression_builder_node_type;
            using result_type = typename base_type::result_type;
            using expression_builder_node_box_type = typename base_type::expression_builder_node_box_type;
            using optional_token_type = typename base_type::optional_token_type;

        protected:
            explicit float_builder_node(optional_token_type p_token = optional_token_type())
                : base_type(std::move(p_token)) {}

        private:
        public:
        };

    } // namespace implementation

} // namespace ovis::ast

#endif // OVIS_AST_NODES_FLOAT_BUILDER_NODE_HPP