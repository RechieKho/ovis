#ifndef OVIS_AST_STATES_GLOBAL_COMMAND_RECOGNISER_BUILDER_STATE_HPP
#define OVIS_AST_STATES_GLOBAL_COMMAND_RECOGNISER_BUILDER_STATE_HPP

#include <ovis/ast/states/global_command_recogniser_builder_state.d.hpp>

namespace ovis::ast
{

    namespace implementation
    {

        template <c_is_generator t_generator_type>
        class global_command_recogniser_builder_state final : public builder_state<t_generator_type>
        {
        public:
            using generator_type = t_generator_type;
            using base_type = builder_state<generator_type>;
            using builder_type = typename base_type::builder_type;

        private:
        public:
            ~global_command_recogniser_builder_state() override = default;

            auto on_enter_state(builder_type &p_machine) -> void override
            {
                // TODO: Implementation.
            }

            auto on_process_state(builder_type &p_machine) -> void override
            {
                // TODO: Implementation.
            }

            auto on_exit_state(builder_type &p_machine) -> void override
            {
                // TODO: Implementation.
            }
        };

    } // namespace implementation

    template <typename t_generator_type>
    using global_command_recogniser_builder_state = implementation::global_command_recogniser_builder_state<t_generator_type>;

} // namespace ovis::ast

#endif // OVIS_AST_STATES_GLOBAL_COMMAND_RECOGNISER_BUILDER_STATE_HPP