#ifndef OVIS_AST_NODES_EXPRESSION_BUILDER_NODE_HPP
#define OVIS_AST_NODES_EXPRESSION_BUILDER_NODE_HPP

#include <memory>
#include <charconv>
#include <fast_float/fast_float.h>

#include <ovis/ast/nodes/builder_node.hpp>

namespace ovis::ast
{

    namespace implementation
    {

        template <c_is_generator t_generator_type>
        class expression_builder_node : public builder_node<t_generator_type>
        {
        public:
            using generator_type = t_generator_type;
            using base_type = builder_node<generator_type>;
            using result_type = typename base_type::result_type;
            using expression_builder_node_box_type = std::unique_ptr<expression_builder_node>;
            using optional_token_type = typename base_type::optional_token_type;
            using optional_uint_type = std::optional<max_uint_type>;
            using optional_float_type = std::optional<max_float_type>;

            static constexpr const string_view_type uint_binary_prefix = "0b"sv;
            static constexpr const string_view_type uint_octal_prefix = "0o"sv;
            static constexpr const string_view_type uint_decimal_prefix = "0d"sv;
            static constexpr const string_view_type uint_hexadecimal_prefix = "0x"sv;

        protected:
            explicit expression_builder_node(optional_token_type p_token = optional_token_type())
                : base_type(std::move(p_token)) {}

        public:
            static auto parse_uint(string_view_type p_text) -> optional_uint_type
            {
                uint_type result;

                if (p_text.size() > 2)
                {
                    if (p_text.starts_with(uint_binary_prefix))
                    {
                        p_text.remove_prefix(2);
                        std::from_chars_result status = std::from_chars(p_text.cbegin(), p_text.cend(), result, 2);
                        if (status.ptr != p_text.cend() || status.ec != std::errc{})
                            return optional_uint_type();
                        else
                            return optional_uint_type(result);
                    }
                    if (p_text.starts_with(uint_octal_prefix))
                    {
                        p_text.remove_prefix(2);
                        std::from_chars_result status = std::from_chars(p_text.cbegin(), p_text.cend(), result, 8);
                        if (status.ptr != p_text.cend() || status.ec != std::errc{})
                            return optional_uint_type();
                        else
                            return optional_uint_type(result);
                    }
                    if (p_text.starts_with(uint_decimal_prefix))
                    {
                        p_text.remove_prefix(2);
                        std::from_chars_result status = std::from_chars(p_text.cbegin(), p_text.cend(), result, 10);
                        if (status.ptr != p_text.cend() || status.ec != std::errc{})
                            return optional_uint_type();
                        else
                            return optional_uint_type(result);
                    }
                    if (p_text.starts_with(uint_hexadecimal_prefix))
                    {
                        p_text.remove_prefix(2);
                        std::from_chars_result status = std::from_chars(p_text.cbegin(), p_text.cend(), result, 16);
                        if (status.ptr != p_text.cend() || status.ec != std::errc{})
                            return optional_uint_type();
                        else
                            return optional_uint_type(result);
                    }
                }

                std::from_chars_result status = std::from_chars(p_text.cbegin(), p_text.cend(), result, 10);
                if (status.ptr != p_text.cend() || status.ec != std::errc{})
                    return optional_uint_type();
                else
                    return optional_uint_type(result);
            }

            static auto parse_float(string_view_type p_text) -> optional_float_type
            {
                float_type result;
                fast_float::from_chars_result status = fast_float::from_chars(p_text.cbegin(), p_text.cend(), result, fast_float::chars_format::general);
                if (status.ptr != p_text.cend() || status.ec != std::errc{})
                    return optional_float_type();
                else
                    return optional_float_type(result);
            }

            static auto add(
                const expression_builder_node_box_type &p_node_a,
                const expression_builder_node_box_type &p_node_b) -> expression_builder_node_box_type
            {
                auto result = p_node_a.add(p_node_b);
                if (result)
                    return result;
                else
                    return p_node_b.add(p_node_a, true);
            }

            static auto subtract(
                const expression_builder_node_box_type &p_node_a,
                const expression_builder_node_box_type &p_node_b) -> expression_builder_node_box_type
            {
                auto result = p_node_a.substract(p_node_b);
                if (result)
                    return result;
                else
                    return p_node_b.substract(p_node_a, true);
            }

            static auto multiply(
                const expression_builder_node_box_type &p_node_a,
                const expression_builder_node_box_type &p_node_b) -> expression_builder_node_box_type
            {
                auto result = p_node_a.multiply(p_node_b);
                if (result)
                    return result;
                else
                    return p_node_b.multiply(p_node_a, true);
            }

            static auto divide(
                const expression_builder_node_box_type &p_node_a,
                const expression_builder_node_box_type &p_node_b) -> expression_builder_node_box_type
            {
                auto result = p_node_a.divide(p_node_b);
                if (result)
                    return result;
                else
                    return p_node_b.divide(p_node_a, true);
            }

            virtual auto add(const expression_builder_node_box_type &p_node, bool p_reversed = false) const -> expression_builder_node_box_type { return expression_builder_node_box_type(); }
            virtual auto subtract(const expression_builder_node_box_type &p_node, bool p_reversed = false) const -> expression_builder_node_box_type { return expression_builder_node_box_type(); }
            virtual auto multiply(const expression_builder_node_box_type &p_node, bool p_reversed = false) const -> expression_builder_node_box_type { return expression_builder_node_box_type(); }
            virtual auto divide(const expression_builder_node_box_type &p_node, bool p_reversed = false) const -> expression_builder_node_box_type { return expression_builder_node_box_type(); }
        };

    } // namespace implementation

} // namespace ovis::ast

#endif // OVIS_AST_NODES_EXPRESSION_BUILDER_NODE_HPP