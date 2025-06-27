#ifndef OVIS_AST_NODES_EXPRESSION_BUILDER_NODE_HPP
#define OVIS_AST_NODES_EXPRESSION_BUILDER_NODE_HPP

#include <memory>

#include <ovis/ast/nodes/builder_node.hpp>

namespace ovis::ast
{

    namespace implementation
    {

        template <c_is_generator t_generator_type>
        class expression_builder_node : public builder_node<t_generator_type>
        {
        public:
            using generator_type = t_generator_type;
            using base_type = builder_node<generator_type>;
            using result_type = typename base_type::result_type;
            using expression_builder_node_box_type = std::unique_ptr<expression_builder_node>;
            using optional_token_type = typename base_type::optional_token_type;

        protected:
            explicit expression_builder_node(optional_token_type p_token = optional_token_type())
                : base_type(std::move(p_token)) {}

        public:
            static auto add(
                const expression_builder_node_box_type &p_node_a,
                const expression_builder_node_box_type &p_node_b) -> expression_builder_node_box_type
            {
                auto result = p_node_a.add(p_node_b);
                if (result)
                    return result;
                else
                    return p_node_b.add(p_node_a, true);
            }

            static auto subtract(
                const expression_builder_node_box_type &p_node_a,
                const expression_builder_node_box_type &p_node_b) -> expression_builder_node_box_type
            {
                auto result = p_node_a.substract(p_node_b);
                if (result)
                    return result;
                else
                    return p_node_b.substract(p_node_a, true);
            }

            static auto multiply(
                const expression_builder_node_box_type &p_node_a,
                const expression_builder_node_box_type &p_node_b) -> expression_builder_node_box_type
            {
                auto result = p_node_a.multiply(p_node_b);
                if (result)
                    return result;
                else
                    return p_node_b.multiply(p_node_a, true);
            }

            static auto divide(
                const expression_builder_node_box_type &p_node_a,
                const expression_builder_node_box_type &p_node_b) -> expression_builder_node_box_type
            {
                auto result = p_node_a.divide(p_node_b);
                if (result)
                    return result;
                else
                    return p_node_b.divide(p_node_a, true);
            }

            virtual auto add(const expression_builder_node_box_type &p_node, bool p_reversed = false) const -> expression_builder_node_box_type { return expression_builder_node_box_type(); }
            virtual auto subtract(const expression_builder_node_box_type &p_node, bool p_reversed = false) const -> expression_builder_node_box_type { return expression_builder_node_box_type(); }
            virtual auto multiply(const expression_builder_node_box_type &p_node, bool p_reversed = false) const -> expression_builder_node_box_type { return expression_builder_node_box_type(); }
            virtual auto divide(const expression_builder_node_box_type &p_node, bool p_reversed = false) const -> expression_builder_node_box_type { return expression_builder_node_box_type(); }
        };

    } // namespace implementation

} // namespace ovis::ast

#endif // OVIS_AST_NODES_EXPRESSION_BUILDER_NODE_HPP