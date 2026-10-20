#ifndef OVIS_CORE_MONOMORPHISE_HPP
#define OVIS_CORE_MONOMORPHISE_HPP

#include <map>
#include <vector>

#include <ovis/core/ast.hpp>
#include <ovis/core/checker.hpp>

// Generics are copied for each use with concrete types (monomorphised,
// docs/proposals/generics.md §6): `push__int` for `push` with whole numbers,
// the record `stack__int` for `(record stack int)`. Codegen and the runtime
// never see a type variable.
namespace ovis::core {

// What each type variable stands for.
using type_bindings = std::map<string_type, value_type>;

// The type with its type variables replaced by what they stand for.
auto substitute(const value_type &p_type, const type_bindings &p_bindings)
    -> value_type;
// Every type written in the function (parameters, result, variables,
// expressions' types and type arguments) with its type variables replaced.
auto substitute(function &p_function, const type_bindings &p_bindings) -> void;

// Fills `p_bindings` with what `p_generic`'s type variables are where
// `p_given` goes: `(list (var item))` given `(list int)` binds `item` to
// `int`. Returns false where they can't meet.
auto match_type(const value_type &p_generic, const value_type &p_given,
                type_bindings &p_bindings) -> bool;

// A type as a part of a name: `int`, `str`, `record_book`, `list_int`.
auto mangle(const value_type &p_type) -> string_type;
// A copy's name: `push__int`, `pair__int__str`.
auto copy_name(string_view_type p_name, const std::vector<value_type> &p_types)
    -> string_type;

// Makes each concrete use of a generic record (`(record stack int)`) a
// record of its own (`stack__int`, with its tag and fields), and each impl
// for every use of one an impl of that record, everywhere in the program;
// removes the generic records, impls and functions, which codegen never
// sees. Run by `check` once everything is checked.
auto make_record_copies(program &p_program) -> std::vector<diagnostic>;

} // namespace ovis::core

#endif // OVIS_CORE_MONOMORPHISE_HPP
