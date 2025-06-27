#ifndef OVIS_AST_BUILDER_NODE_HPP
#define OVIS_AST_BUILDER_NODE_HPP

#include <memory>
#include <optional>

#include <ovis/ast/generators/generator.hpp>
#include <ovis/tokenizer.hpp>

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
            using optional_token_type = std::optional<ovis::tokenizer::token_type>;

        protected:
            optional_token_type m_token;

            explicit builder_node(optional_token_type p_token = optional_token_type())
                : m_token(std::move(p_token)) {}

        private:
        public:
            virtual ~builder_node() = 0;
            virtual auto generate() const -> result_type = 0;

            auto get_token() -> optional_token_type
            {
                return m_token;
            }
        };

        template <c_is_generator t_generator_type>
        builder_node<t_generator_type>::~builder_node() {}

    } // namespace implementation

} // namespace ovis::ast

#endif // OVIS_AST_BUILDER_NODE_HPP