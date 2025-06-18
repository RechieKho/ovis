#ifndef OVIS_AST_BUILDER_STATE_HPP
#define OVIS_AST_BUILDER_STATE_HPP

#include <memory>
#include <concepts>

#include <ovis/def.hpp>
#include <ovis/ast/builder.d.hpp>

namespace ovis::ast
{

    namespace implementation
    {

        template <typename = void>
        class builder_state
        {
        public:
        private:
        public:
            virtual ~builder_state() = 0;
            virtual auto on_enter_state(builder<> &p_machine) -> void = 0;
            virtual auto on_process_state(builder<> &p_machine) -> void = 0;
            virtual auto on_exit_state(builder<> &p_machine) -> void = 0;
        };

        template <>
        builder_state<>::~builder_state() {}

        template <typename t_type>
        concept c_is_builder_state = requires {
            requires std::derived_from<t_type, builder_state<>>;
            requires std::same_as<std::remove_cvref_t<decltype(t_type::name)>, string_view_type>;
        };
    } // namespace implementation

} // namespace ovis::ast

#endif // OVIS_AST_BUILDER_STATE_HPP