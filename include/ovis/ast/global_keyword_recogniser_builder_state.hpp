#ifndef GLOBAL_KEYWORD_RECOGNISER_BUILDER_STATE
#define GLOBAL_KEYWORD_RECOGNISER_BUILDER_STATE

#include <ovis/ast/builder_state.hpp>

namespace ovis::ast
{

    namespace implementation
    {

        template <typename = void>
        class global_keyword_recogniser_builder_state final : public builder_state<>
        {
        public:
            static constexpr const string_view_type name = "global_keyword_recogniser"sv;

        private:
        public:
            ~global_keyword_recogniser_builder_state() override = default;

            auto on_enter_state(builder<> &p_machine) -> void override
            {
                // TODO: Implementation.
            }

            auto on_process_state(builder<> &p_machine) -> void override
            {
                // TODO: Implementation.
            }

            auto on_exit_state(builder<> &p_machine) -> void override
            {
                // TODO: Implementation.
            }
        };
        static_assert(c_is_builder_state<global_keyword_recogniser_builder_state<>>);

    } // namespace implementation

} // namespace ovis::ast

#endif // GLOBAL_KEYWORD_RECOGNISER_BUILDER_STATE