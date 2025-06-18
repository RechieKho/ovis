#ifndef OVIS_AST_BUILDER_HPP
#define OVIS_AST_BUILDER_HPP

#include <map>
#include <memory>
#include <optional>

#include <ovis/def.hpp>
#include <ovis/ast/builder.d.hpp>
#include <ovis/ast/builder_state.hpp>
#include <ovis/ast/global_keyword_recogniser_builder_state.hpp>

namespace ovis::ast
{

    namespace implementation
    {

        template <typename>
        class builder final
        {
        public:
            using builder_state_box_type = std::unique_ptr<builder_state<>>;
            using builder_state_map_type = std::map<string_type, builder_state_box_type>;
            using active_builder_state_type = builder_state_map_type::value_type;
            using optional_active_builder_state_type = std::optional<active_builder_state_type>;

        private:
            builder_state_map_type builder_states;
            optional_active_builder_state_type current_builder_state;

            template <c_is_builder_state t_builder_state_type>
            auto add_builder_state() -> void
            {
                builder_states.insert(t_builder_state_type::name, builder_state_box_type(t_builder_state_type()));
            }

        public:
            explicit builder()
                : builder_states(), current_builder_state()
            {
                add_builder_state<global_keyword_recogniser_builder_state>();
            }

            auto process() -> void
            {
                if (!current_builder_state.has_value())
                    return;

                current_builder_state->second->on_process_state(*this);
            }

            auto to(string_view_type p_key) -> void
            {
                if (!builder_states.contains(p_key))
                    throw fault_type("Requested builder_state does not exists.");

                current_builder_state->second->on_exit_state(*this);
                current_builder_state = std::move(builder_states[p_key]);
                current_builder_state->second->on_enter_state(*this);
            }
        };

    } // namespace implementation

    using builder = implementation::builder<>;

} // namespace ovis::ast

#endif // OVIS_AST_BUILDER_HPP