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

    using int_type = std::int32_t;
    using uint_type = std::uint32_t;
    using char_type = char;

    using string_type = std::basic_string<char_type>;
    using string_view_type = std::basic_string_view<char_type>;

    using fault_type = std::runtime_error;
    using type_info_type = std::type_info;

} // namespace ovis

#endif // OVIS_DEF_HPP