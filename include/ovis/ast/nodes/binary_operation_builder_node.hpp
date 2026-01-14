#ifndef OVIS_AST_NODES_BINARY_OPERATION_BUILDER_NODE_HPP
#define OVIS_AST_NODES_BINARY_OPERATION_BUILDER_NODE_HPP

#include <ovis/ast/nodes/expression_builder_node.hpp>

namespace ovis::ast {

namespace implementation {

template <c_is_generator t_generator_type>
class binary_operation_builder_node
    : public expression_builder_node<t_generator_type> {
public:
  using generator_type = t_generator_type;

  using builder_node_type = builder_node<generator_type>;
  using expression_builder_node_type = expression_builder_node<generator_type>;

  using result_type = typename generator_type::result_type;
  using expression_builder_node_box_type =
      typename expression_builder_node_type::box_type;
  using optional_token_type = typename builder_node_type::optional_token_type;

protected:
  expression_builder_node_box_type m_left_side_value;
  expression_builder_node_box_type m_right_side_value;

  explicit binary_operation_builder_node(
      expression_builder_node_box_type p_left_side_value,
      expression_builder_node_box_type p_right_side_value,
      optional_token_type p_token = optional_token_type())
      : expression_builder_node_type(std::move(p_token)),
        m_left_side_value(std::move(p_left_side_value)),
        m_right_side_value(std::move(p_right_side_value)) {}

private:
public:
  auto get_numeric_capacity() const
      -> expression_builder_node_type::numeric_capacity override {
    const auto left_side_value_numeric_capacity =
        binary_operation_builder_node_type::m_left_side_value
            ->get_numeric_capacity();
    const auto right_side_value_numeric_capacity =
        binary_operation_builder_node_type::m_right_side_value
            ->get_numeric_capacity();
    if (left_side_value_numeric_capacity > right_side_value_numeric_capacity)
      return left_side_value_numeric_capacity;
    else
      return right_side_value_numeric_capacity;
  }
};

} // namespace implementation

} // namespace ovis::ast

#endif // OVIS_AST_NODES_BINARY_OPERATION_BUILDER_NODE_HPP