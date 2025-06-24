#ifndef OVIS_AST_BUILDER_NODE_HPP
#define OVIS_AST_BUILDER_NODE_HPP

#include <memory>
#include <vector>

#include <ovis/ast/generators/generator.hpp>

namespace ovis::ast
{

    namespace implementation
    {

        template <c_is_generator t_generator_type>
        class builder_node
        {
        public:
            using generator_type = t_generator_type;
            using result_type = typename generator_type::result_type;
            using box_type = std::unique_ptr<builder_node>;

        private:
        public:
            virtual ~builder_node() = 0;
            virtual auto generate() const -> result_type = 0;
        };

        template <c_is_generator t_generator_type>
        builder_node<t_generator_type>::~builder_node() {}

    } // namespace implementation

} // namespace ovis::ast

#endif // OVIS_AST_BUILDER_NODE_HPP