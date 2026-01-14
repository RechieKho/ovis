#ifndef OVIS_AST_NODES_FLOAT_BINARY_OPERATION_BUILDER_NODE_HPP
#define OVIS_AST_NODES_FLOAT_BINARY_OPERATION_BUILDER_NODE_HPP

#include <ovis/ast/nodes/binary_operation_builder_node.hpp>

namespace ovis::ast {

namespace implementation {

template <c_is_generator t_generator_type>
class float_binary_operation_builder_node
    : public binary_operation_builder_node<t_generator_type> {
public:
  using generator_type = t_generator_type;

  using builder_node_type = builder_node<generator_type>;
  using expression_builder_node_type = expression_builder_node<generator_type>;
  using binary_operation_builder_node_type =
      binary_operation_builder_node<generator_type>;

  using result_type = typename generator_type::result_type;
  using expression_builder_node_box_type =
      typename expression_builder_node_type::box_type;
  using optional_token_type = typename builder_node_type::optional_token_type;

protected:
  explicit float_binary_operation_builder_node(
      expression_builder_node_box_type p_left_hand_value,
      expression_builder_node_box_type p_right_hand_value,
      optional_token_type p_token = optional_token_type())
      : binary_operation_builder_node_type(std::move(p_left_hand_value),
                                           std::move(p_right_hand_value),
                                           std::move(p_token)) {}

private:
public:
};

} // namespace implementation

} // namespace ovis::ast

#endif // OVIS_AST_NODES_FLOAT_BINARY_OPERATION_BUILDER_NODE_HPP