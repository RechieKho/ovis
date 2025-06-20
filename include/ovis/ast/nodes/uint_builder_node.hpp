#ifndef OVIS_AST_NODES_UINT_BUILDER_NODE_HPP
#define OVIS_AST_NODES_UINT_BUILDER_NODE_HPP

#include <ovis/ast/nodes/builder_node.hpp>

namespace ovis::ast
{

    namespace implementation
    {

        template <c_is_generator t_generator_type>
        class uint_builder_node : public builder_node<t_generator_type>
        {
        public:
            using generator_type = t_generator_type;
            using base_type = builder_node<generator_type>;
            using result_type = typename base_type::result_type;

        private:
            max_uint_type m_value;

        public:
            explicit uint_builder_node(max_uint_type p_value)
                : m_value(p_value) {}

            ~uint_builder_node() override = default;

            auto generate() const -> result_type override
            {
                return generator_type::get_singleton().generate_uint(m_value);
            }
        };

    } // namespace implementation

} // namespace ovis::ast

#endif // OVIS_AST_NODES_UINT_BUILDER_NODE_HPP