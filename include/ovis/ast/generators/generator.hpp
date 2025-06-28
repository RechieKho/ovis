#ifndef OVIS_AST_GENERATORS_GENERATOR_HPP
#define OVIS_AST_GENERATORS_GENERATOR_HPP

#include <utility>
#include <concepts>

#include <ovis/def.hpp>

namespace ovis::ast
{

    template <typename t_type>
    concept c_is_generator = requires {
        requires c_is_singleton<t_type>;

        typename t_type::result_type;
        { std::declval<const t_type>().generate_float32(std::declval<float>()) } -> std::same_as<typename t_type::result_type>;
        { std::declval<const t_type>().generate_float64(std::declval<double>()) } -> std::same_as<typename t_type::result_type>;
        { std::declval<const t_type>().generate_uint8(std::declval<uint8_t>()) } -> std::same_as<typename t_type::result_type>;
        { std::declval<const t_type>().generate_int8(std::declval<int8_t>()) } -> std::same_as<typename t_type::result_type>;
        { std::declval<const t_type>().generate_uint16(std::declval<uint16_t>()) } -> std::same_as<typename t_type::result_type>;
        { std::declval<const t_type>().generate_int16(std::declval<int16_t>()) } -> std::same_as<typename t_type::result_type>;
        { std::declval<const t_type>().generate_uint32(std::declval<uint32_t>()) } -> std::same_as<typename t_type::result_type>;
        { std::declval<const t_type>().generate_int32(std::declval<int32_t>()) } -> std::same_as<typename t_type::result_type>;
        { std::declval<const t_type>().generate_uint64(std::declval<uint64_t>()) } -> std::same_as<typename t_type::result_type>;
        { std::declval<const t_type>().generate_int64(std::declval<int64_t>()) } -> std::same_as<typename t_type::result_type>;

        { std::declval<const t_type>().generate_float_addition(
            std::declval<typename t_type::result_type>(),
            std::declval<typename t_type::result_type>()) } -> std::same_as<typename t_type::result_type>;
        { std::declval<const t_type>().generate_float_subtraction(
            std::declval<typename t_type::result_type>(),
            std::declval<typename t_type::result_type>()) } -> std::same_as<typename t_type::result_type>;
        { std::declval<const t_type>().generate_float_multiplication(
            std::declval<typename t_type::result_type>(),
            std::declval<typename t_type::result_type>()) } -> std::same_as<typename t_type::result_type>;
        { std::declval<const t_type>().generate_float_division(
            std::declval<typename t_type::result_type>(),
            std::declval<typename t_type::result_type>()) } -> std::same_as<typename t_type::result_type>;

        { std::declval<const t_type>().generate_xint_addition(
            std::declval<typename t_type::result_type>(),
            std::declval<typename t_type::result_type>()) } -> std::same_as<typename t_type::result_type>;
        { std::declval<const t_type>().generate_xint_subtraction(
            std::declval<typename t_type::result_type>(),
            std::declval<typename t_type::result_type>()) } -> std::same_as<typename t_type::result_type>;
        { std::declval<const t_type>().generate_xint_multiplication(
            std::declval<typename t_type::result_type>(),
            std::declval<typename t_type::result_type>()) } -> std::same_as<typename t_type::result_type>;
        { std::declval<const t_type>().generate_int_division(
            std::declval<typename t_type::result_type>(),
            std::declval<typename t_type::result_type>()) } -> std::same_as<typename t_type::result_type>;
        { std::declval<const t_type>().generate_uint_division(
            std::declval<typename t_type::result_type>(),
            std::declval<typename t_type::result_type>()) } -> std::same_as<typename t_type::result_type>;
    };

} // namespace ovis::ast

#endif // OVIS_AST_GENERATORS_GENERATOR_HPP