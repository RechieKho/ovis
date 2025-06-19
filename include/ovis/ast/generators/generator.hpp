#ifndef OVIS_AST_GENERATORS_GENERATOR_HPP
#define OVIS_AST_GENERATORS_GENERATOR_HPP

#include <utility>
#include <concepts>

#include <ovis/def.hpp>

namespace ovis::ast
{

    template <typename t_type>
    concept c_is_generator = requires {
        requires c_is_proxy<t_type>;

        typename t_type::result_type;
    };

} // namespace ovis::ast

#endif // OVIS_AST_GENERATORS_GENERATOR_HPP