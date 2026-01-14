#ifndef OVIS_AST_NODES_XINT_BINARY_OPERATION_BUILDER_NODE_HPP
#define OVIS_AST_NODES_XINT_BINARY_OPERATION_BUILDER_NODE_HPP

#include <ovis/ast/nodes/binary_operation_builder_node.hpp>

namespace ovis::ast {

namespace implementation {

template <c_is_generator t_generator_type>
class xint_binary_operation_builder_node
    : public binary_operation_builder_node<t_generator_type> {
public:
  using generator_type = t_generator_type;
  using base_type = binary_operation_builder_node<generator_type>;
  using feature_base_type = xint_feature<>;
  using result_type = typename base_type::result_type;
  using expression_builder_node_box_type =
      typename base_type::expression_builder_node_box_type;
  using optional_token_type = typename base_type::optional_token_type;

protected:
  explicit xint_binary_operation_builder_node(
      expression_builder_node_box_type p_left_hand_value,
      expression_builder_node_box_type p_right_hand_value, bool p_is_signed,
      optional_token_type p_token = optional_token_type())
      : base_type(std::move(p_left_hand_value), std::move(p_right_hand_value),
                  std::move(p_token)),
        feature_base_type(p_is_signed) {}

private:
public:
  auto get_numeric_class() const
      -> expression_builder_node_type::numeric_class override {
    const auto left_side_value_numeric_class =
        binary_operation_builder_node_type::m_left_side_value
            ->get_numeric_class();
    const auto rigth_side_value_numeric_class =
        binary_operation_builder_node_type::m_right_side_value
            ->get_numeric_class();
    if (left_side_value_numeric_class !=
            expression_builder_node_type::numeric_class::numeric_class_uint ||
        left_side_value_numeric_class !=
            expression_builder_node_type::numeric_Class::numeric_class_sint)
      return expression_builder_node_type::numeric_class::
          numeric_class_undefined;
    if (right_side_value_numeric_class !=
            expression_builder_node_type::numeric_class::numeric_class_uint ||
        right_side_value_numeric_class !=
            expression_builder_node_type::numeric_Class::numeric_class_sint)
      return expression_builder_node_type::numeric_class::
          numeric_class_undefined;
    if (left_side_value_numeric_class ==
            expression_builder_node_type::numeric_class::numeric_class_uint ||
        right_side_value_numeric_class ==
            expression_builder_node_type::numeric_Class::numeric_class_uint)
      return expression_builder_node_type::numeric_class::numeric_class_uint;
    return expression_builder_node_type::numeric_class::numeric_class_sint;
  }

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

#endif // OVIS_AST_NODES_XINT_BINARY_OPERATION_BUILDER_NODE_HPP