#ifndef OVIS_AST_GENERATOR_HPP
#define OVIS_AST_GENERATOR_HPP

#include <utility>
#include <concepts>

#include <ovis/def.hpp>

namespace ovis::ast
{

    template <typename t_type>
    concept c_is_generator = requires {
        requires std::constructible_from<t_type>;
    };

} // namespace ovis::ast

#endif // OVIS_AST_GENERATOR_HPP