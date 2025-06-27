#ifndef OVIS_AST_NODES_INT32_BUILDER_NODE_HPP
#define OVIS_AST_NODES_INT32_BUILDER_NODE_HPP

#include <ovis/ast/nodes/builder_node.hpp>

namespace ovis::ast
{

    namespace implementation
    {

        template <c_is_generator t_generator_type>
        class int32_builder_node : public expression_builder_node<t_generator_type>
        {
        public:
            using generator_type = t_generator_type;
            using base_type = expression_builder_node<generator_type>;
            using result_type = typename base_type::result_type;
            using expression_builder_node_box_type = typename base_type::expression_builder_node_box_type;

        private:
            int32_type m_value;

        public:
            explicit int32_builder_node(int32_type p_value)
                : m_value(p_value) {}

            ~int32_builder_node() override = default;

            auto generate() const -> result_type override
            {
                return generator_type::get_singleton().generate_int32(m_value);
            }

            auto get() const -> int32_type { return m_value; }
        };

    } // namespace implementation

} // namespace ovis::ast

#endif // OVIS_AST_NODES_INT32_BUILDER_NODE_HPP