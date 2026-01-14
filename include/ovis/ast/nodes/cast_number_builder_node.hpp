#ifndef OVIS_AST_NODES_CAST_INT_TO_FLOAT32_BUILDER_NODE_HPP
#define OVIS_AST_NODES_CAST_INT_TO_FLOAT32_BUILDER_NODE_HPP

#include <ovis/ast/generators/generator.hpp>
#include <ovis/ast/nodes/unary_operation_builder_node.hpp>

namespace ovis::ast {

namespace implementation {

template <c_is_generator t_generator_type>
class cast_number_builder_node final
    : public unary_operation_builder_node<t_generator_type> {
public:
  using generator_type = t_generator_type;

  using builder_node_type = builder_node<generator_type>;
  using expression_builder_node_type = expression_builder_node<generator_type>;
  using unary_operation_builder_node_type =
      unary_operation_builder_node<generator_type>;

  using result_type = typename generator_type::result_type;
  using expression_builder_node_box_type =
      typename expression_builder_node_type::box_type;
  using optional_token_type = typename builder_node_type::optional_token_type;

private:
  expression_builder_node_type::numeric_class m_target_numeric_class;

public:
  explicit cast_number_builder_node(
      expression_builder_node_box_type p_value,
      optional_token_type p_token = optional_token_type())
      : unary_operation_builder_node_type(std::move(p_value),
                                          std::move(p_token)) {}

  ~cast_number_builder_node() override = default;

  auto get_target_numeric_class() const
      -> expression_builder_node_type::numeric_class {
    return m_target_numeric_class;
  }

  auto generate() const -> result_type override {
    return generator_type::get_singleton().generate_int_to_float_cast(
        unary_operation_builder_node_type::m_value->generate());
  }
};

} // namespace implementation

} // namespace ovis::ast

#endif // OVIS_AST_NODES_CAST_INT_TO_FLOAT32_BUILDER_NODE_HPP
