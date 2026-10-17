#ifndef OVIS_CODEGEN_RUNTIME_HPP
#define OVIS_CODEGEN_RUNTIME_HPP

#include <ovis/def.hpp>

namespace ovis::codegen {

// C source of the runtime support library (lists, printing, input).
auto runtime_source() -> string_view_type;

} // namespace ovis::codegen

#endif // OVIS_CODEGEN_RUNTIME_HPP
