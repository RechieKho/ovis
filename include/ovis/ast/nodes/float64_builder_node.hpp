#ifndef OVIS_AST_NODES_FLOAT64_BUILDER_NODE_HPP
#define OVIS_AST_NODES_FLOAT64_BUILDER_NODE_HPP

#include <ovis/ast/nodes/float64_builder_node.d.hpp>

namespace ovis::ast
{

    namespace implementation
    {

        template <c_is_generator t_generator_type>
        class float64_builder_node final : public expression_builder_node<t_generator_type>
        {
        public:
            using generator_type = t_generator_type;
            using base_type = expression_builder_node<generator_type>;
            using result_type = typename base_type::result_type;
            using expression_builder_node_box_type = typename base_type::expression_builder_node_box_type;
            using optional_token_type = typename base_type::optional_token_type;

        private:
            float64_type m_value;

        public:
            explicit float64_builder_node(float64_type p_value, optional_token_type p_token = optional_token_type())
                : base_type(std::move(p_token)), m_value(p_value) {}

            ~float64_builder_node() override = default;

            auto generate() const -> result_type override
            {
                return generator_type::get_singleton().generate_float64(m_value);
            }
        };

    } // namespace implementation

} // namespace ovis::ast

#include <ovis/ast/nodes/int64_builder_node.hpp>

#endif // OVIS_AST_NODES_FLOAT64_BUILDER_NODE_HPP