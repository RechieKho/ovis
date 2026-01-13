#ifndef OVIS_AST_NODES_XINT_BUILDER_NODE_HPP
#define OVIS_AST_NODES_XINT_BUILDER_NODE_HPP

#include <ovis/ast/nodes/expression_builder_node.hpp>

namespace ovis::ast {

namespace implementation {

template <c_is_generator t_generator_type>
class uint_builder_node : public expression_builder_node<t_generator_type> {
public:
  using generator_type = t_generator_type;
  using expression_builder_node_type = expression_builder_node<generator_type>;
  using base_type = expression_builder_node_type;
  using result_type = typename base_type::result_type;
  using expression_builder_node_box_type =
      typename base_type::expression_builder_node_box_type;
  using optional_token_type = typename base_type::optional_token_type;

protected:
private:
  max_uint_type m_data;

public:
  explicit uint_builder_node(
      max_uint_type p_data, optional_token_type p_token = optional_token_type())
      : base_type(std::move(p_token)), m_data(p_data) {}

  ~uint_builder_node() override = default;

  auto get_numeric_class() const
      -> expression_builder_node_type::numeric_class override {
    return expression_builder_node_type::numeric_class::numeric_class_uint;
  }

  auto generate() const -> result_type override {
    return generator_type::get_singleton().generate_dummy();
  }
};

} // namespace implementation

} // namespace ovis::ast

#endif // OVIS_AST_NODES_XINT_BUILDER_NODE_HPP