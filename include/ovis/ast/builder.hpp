#ifndef OVIS_AST_BUILDER_HPP
#define OVIS_AST_BUILDER_HPP

#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <ovis/def.hpp>
#include <ovis/ast/builder.d.hpp>
#include <ovis/ast/nodes/builder_node.hpp>
#include <ovis/ast/states/builder_state.hpp>
#include <ovis/ast/states/global_command_recogniser_builder_state.hpp>
#include <ovis/ast/generators/llvm_generator.hpp>
#include <ovis/ast/nodes/expression_builder_node.hpp>
#include <ovis/ast/nodes/float32_builder_node.hpp>
#include <ovis/ast/nodes/float64_builder_node.hpp>
#include <ovis/ast/nodes/int8_builder_node.hpp>
#include <ovis/ast/nodes/int16_builder_node.hpp>
#include <ovis/ast/nodes/int32_builder_node.hpp>
#include <ovis/ast/nodes/int64_builder_node.hpp>
#include <ovis/ast/nodes/uint8_builder_node.hpp>
#include <ovis/ast/nodes/uint16_builder_node.hpp>
#include <ovis/ast/nodes/uint32_builder_node.hpp>
#include <ovis/ast/nodes/uint64_builder_node.hpp>

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
            using builder_node_type = builder_node<generator_type>;
            using builder_node_box_type = std::unique_ptr<builder_node_type>;
            using builder_node_box_list_type = std::vector<builder_node_box_type>;

        private:
            builder_state_map_type states;
            optional_active_builder_state_type current_state;
            builder_node_box_list_type nodes;

        public:
            static auto use_default() -> builder
            {
                builder default_builder;
                default_builder.to<global_command_recogniser_builder_state<generator_type>>();
            }

            explicit builder()
                : states(), current_state(), nodes()
            {
            }

            template <c_is_builder_state t_builder_state_type>
            auto add_builder_state() -> void
            {
                states.insert(typeid(t_builder_state_type), builder_state_box_type(t_builder_state_type()));
            }

            auto process() -> void
            {
                if (!current_state.has_value())
                    return;

                current_state->second->on_process_state(*this);
            }

            auto build() -> builder_node_box_list_type &&
            {
                return std::move(nodes);
            }

            template <c_is_builder_state t_builder_state_type>
            auto to() -> void
            {
                if (!states.contains(typeid(t_builder_state_type)))
                    add_builder_state<t_builder_state_type>();

                current_state->second->on_exit_state(*this);
                current_state = std::move(states[typeid(t_builder_state_type)]);
                current_state->second->on_enter_state(*this);
            }
        };

    } // namespace implementation

    template <typename t_generator_type>
    using builder = implementation::builder<t_generator_type>;

    using llvm_builder_type = builder<llvm_generator>;
    using default_builder_type = llvm_builder_type;

    template <typename t_generator_type>
    using expression_builder_node = implementation::expression_builder_node<t_generator_type>;
    using default_expression_builder_node_type = expression_builder_node<llvm_generator>;

} // namespace ovis::ast

#endif // OVIS_AST_BUILDER_HPP