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

  using builder_node_type = builder_node<generator_type>;
  using expression_builder_node_type =
      expression_builder_node_type<generator_type>;
  using binary_operation_builder_node_type =
      binary_operation_builder_node<generator_type>;
  using logic_error_type = std::logic_error;

  using result_type = typename generator_type::result_type;
  using expression_builder_node_box_type =
      typename expression_builder_node_type::box_type;
  using optional_token_type = typename builder_node_type::optional_token_type;
  using optional_exception_type =
      typename builder_node_type::optional_exception_type;

protected:
  explicit xint_binary_operation_builder_node(
      expression_builder_node_box_type p_left_hand_value,
      expression_builder_node_box_type p_right_hand_value, bool p_is_signed,
      optional_token_type p_token = optional_token_type())
      : binary_operation_builder_node_type(std::move(p_left_hand_value),
                                           std::move(p_right_hand_value),
                                           std::move(p_token)) {}

private:
public:
  auto analyse() const -> optional_exception_type override {
    const auto self_numeric_class = get_numeric_class();
    if (self_numeric_class !=
            expression_builder_node_type::numeric_class::numeric_class_sint &&
        self_numeric_class !=
            expression_builder_node_type::numeric_class::numeric_class_uint)
      return optional_exception_type(
          logic_error_type("Fail to generate integer binary operation from "
                           "non-integer operand."));
    return builder_node_type::analyse();
  }

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
};

} // namespace implementation

} // namespace ovis::ast

#endif // OVIS_AST_NODES_XINT_BINARY_OPERATION_BUILDER_NODE_HPP