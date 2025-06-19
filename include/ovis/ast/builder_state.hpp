#ifndef OVIS_AST_BUILDER_STATE_HPP
#define OVIS_AST_BUILDER_STATE_HPP

#include <memory>
#include <concepts>

#include <ovis/def.hpp>
#include <ovis/ast/builder.d.hpp>
#include <ovis/ast/generator.hpp>

namespace ovis::ast
{

    namespace implementation
    {

        template <c_is_generator t_generator_type>
        class builder_state
        {
        public:
            using generator_type = t_generator_type;
            using builder_type = builder<generator_type>;

        private:
        public:
            virtual ~builder_state() = 0;
            virtual auto on_enter_state(builder_type &p_machine) -> void = 0;
            virtual auto on_process_state(builder_type &p_machine) -> void = 0;
            virtual auto on_exit_state(builder_type &p_machine) -> void = 0;
        };

        template <c_is_generator t_generator_type>
        builder_state<t_generator_type>::~builder_state() {}

        template <typename t_type>
        concept c_is_builder_state = requires {
            typename t_type::generator_type;
            requires std::derived_from<t_type, builder_state<typename t_type::generator_type>>;
        };
    } // namespace implementation

} // namespace ovis::ast

#endif // OVIS_AST_BUILDER_STATE_HPP