#ifndef OVIS_AST_NODES_FLOAT_MULTIPLICATION_BUILDER_NODE_HPP
#define OVIS_AST_NODES_FLOAT_MULTIPLICATION_BUILDER_NODE_HPP

#include <ovis/ast/nodes/float_binary_operation_builder_node.hpp>

namespace ovis::ast {

namespace implementation {

template <c_is_generator t_generator_type>
class float_multiplication_builder_node final
    : public float_binary_operation_builder_node<t_generator_type> {
public:
  using generator_type = t_generator_type;

  using builder_node_type = builder_node<generator_type>;
  using expression_builder_node_type = expression_builder_node<generator_type>;
  using binary_operation_builder_node_type =
      binary_operation_builder_node<generator_type>;
  using float_binary_operation_builder_node_type =
      float_binary_operation_builder_node<generator_type>;

  using result_type = typename generator_type::result_type;
  using expression_builder_node_box_type =
      typename expression_builder_node_type::box_type;
  using optional_token_type = typename builder_node_type::optional_token_type;

protected:
private:
public:
  explicit float_multiplication_builder_node(
      expression_builder_node_box_type p_left_hand_value,
      expression_builder_node_box_type p_right_hand_value,
      optional_token_type p_token = optional_token_type())
      : float_binary_operation_builder_node_type(std::move(p_left_hand_value),
                                                 std::move(p_right_hand_value),
                                                 std::move(p_token)) {}

  ~float_multiplication_builder_node() override = default;

  auto generate() const -> result_type override {
    const auto analysis = analyse();
    if (analysis)
      throw *analysis;

    return generator_type::get_singleton().generate_float_multiplication(
        binary_operation_builder_node_type::m_left_side_value->generate(),
        binary_operation_builder_node_type::m_right_side_value->generate());
  }
};

} // namespace implementation

} // namespace ovis::ast

#endif // OVIS_AST_NODES_FLOAT_MULTIPLICATION_BUILDER_NODE_HPP