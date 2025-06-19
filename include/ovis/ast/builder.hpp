#ifndef OVIS_AST_BUILDER_HPP
#define OVIS_AST_BUILDER_HPP

#include <map>
#include <memory>
#include <optional>

#include <ovis/def.hpp>
#include <ovis/ast/builder.d.hpp>
#include <ovis/ast/states/builder_state.hpp>
#include <ovis/ast/states/global_command_recogniser_builder_state.hpp>

namespace ovis::ast
{

    namespace implementation
    {

        template <c_is_generator t_generator_type>
        class builder final
        {
        public:
            using generator_type = t_generator_type;
            using builder_state_type = builder_state<generator_type>;
            using builder_state_box_type = std::unique_ptr<builder_state_type>;
            using builder_state_map_type = std::map<type_info_type, builder_state_box_type>;
            using active_builder_state_type = builder_state_map_type::value_type;
            using optional_active_builder_state_type = std::optional<active_builder_state_type>;

        private:
            builder_state_map_type builder_states;
            optional_active_builder_state_type current_builder_state;
            generator_type generator;

        public:
            static auto use_default() -> builder
            {
                builder default_builder;
                default_builder.to<global_command_recogniser_builder_state<generator_type>>();
            }

            explicit builder()
                : builder_states(), current_builder_state(), generator()
            {
            }

            template <c_is_builder_state t_builder_state_type>
            auto add_builder_state() -> void
            {
                builder_states.insert(typeid(t_builder_state_type), builder_state_box_type(t_builder_state_type()));
            }

            auto process() -> void
            {
                if (!current_builder_state.has_value())
                    return;

                current_builder_state->second->on_process_state(*this);
            }

            template <c_is_builder_state t_builder_state_type>
            auto to() -> void
            {
                if (!builder_states.contains(typeid(t_builder_state_type)))
                    add_builder_state<t_builder_state_type>();

                current_builder_state->second->on_exit_state(*this);
                current_builder_state = std::move(builder_states[typeid(t_builder_state_type)]);
                current_builder_state->second->on_enter_state(*this);
            }
        };

    } // namespace implementation

    template <typename t_generator_type>
    using builder = implementation::builder<t_generator_type>;

} // namespace ovis::ast

#endif // OVIS_AST_BUILDER_HPP