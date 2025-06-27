#ifndef OVIS_DEF_HPP
#define OVIS_DEF_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <concepts>
#include <stdexcept>
#include <typeinfo>

namespace ovis
{
    using namespace std::literals::string_view_literals;

    using char_type = char;

    using int8_type = std::int8_t;
    using int16_type = std::int16_t;
    using int32_type = std::int32_t;
    using int64_type = std::int64_t;

    using uint8_type = std::uint8_t;
    using uint16_type = std::uint16_t;
    using uint32_type = std::uint32_t;
    using uint64_type = std::uint64_t;

    using float32_type = float;
    using float64_type = double;

    using int_type = int32_type;
    using uint_type = uint32_type;
    using float_type = float32_type;

    using max_int_type = int64_type;
    using max_uint_type = uint64_type;
    using max_float_type = float64_type;

    using string_type = std::basic_string<char_type>;
    using string_view_type = std::basic_string_view<char_type>;

    using fault_type = std::runtime_error;
    using type_info_type = std::type_info;

    template <typename t_type>
    concept c_is_singleton = requires {
        { t_type::get_singleton() } -> std::same_as<t_type &>;
    };

} // namespace ovis

#endif // OVIS_DEF_HPP