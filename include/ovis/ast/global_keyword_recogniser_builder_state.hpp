#ifndef GLOBAL_KEYWORD_RECOGNISER_BUILDER_STATE
#define GLOBAL_KEYWORD_RECOGNISER_BUILDER_STATE

#include <ovis/ast/builder_state.hpp>

namespace ovis::ast
{

    namespace implementation
    {

        template <c_is_generator t_generator_type>
        class global_keyword_recogniser_builder_state final : public builder_state<t_generator_type>
        {
        public:
            using generator_type = t_generator_type;
            using base_type = builder_state<generator_type>;
            using builder_type = typename base_type::builder_type;

        private:
        public:
            ~global_keyword_recogniser_builder_state() override = default;

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

} // namespace ovis::ast

#endif // GLOBAL_KEYWORD_RECOGNISER_BUILDER_STATE