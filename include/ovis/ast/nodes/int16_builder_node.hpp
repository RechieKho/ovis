#ifndef OVIS_AST_NODES_INT16_BUILDER_NODE_HPP
#define OVIS_AST_NODES_INT16_BUILDER_NODE_HPP

#include <ovis/ast/nodes/builder_node.hpp>

namespace ovis::ast
{

    namespace implementation
    {

        template <c_is_generator t_generator_type>
        class int16_builder_node : public expression_builder_node<t_generator_type>
        {
        public:
            using generator_type = t_generator_type;
            using base_type = expression_builder_node<generator_type>;
            using result_type = typename base_type::result_type;
            using expression_builder_node_box_type = typename base_type::expression_builder_node_box_type;
            using optional_token_type = typename base_type::optional_token_type;

        private:
            int16_type m_value;

        public:
            explicit int16_builder_node(int32_type p_value, optional_token_type p_token = optional_token_type())
                : base_type(std::move(p_token)), m_value(p_value) {}

            ~int16_builder_node() override = default;

            auto generate() const -> result_type override
            {
                return generator_type::get_singleton().generate_int16(m_value);
            }

            auto get() const -> int16_type { return m_value; }
        };

    } // namespace implementation

} // namespace ovis::ast

#endif // OVIS_AST_NODES_INT16_BUILDER_NODE_HPP