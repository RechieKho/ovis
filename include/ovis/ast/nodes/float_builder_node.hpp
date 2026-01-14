#ifndef OVIS_AST_NODES_FLOAT_BUILDER_NODE_HPP
#define OVIS_AST_NODES_FLOAT_BUILDER_NODE_HPP

#include <ovis/ast/nodes/expression_builder_node.hpp>

namespace ovis::ast {

namespace implementation {

template <c_is_generator t_generator_type>
class float_builder_node : public expression_builder_node<t_generator_type> {
public:
  using generator_type = t_generator_type;

  using builder_node_type = builder_node<generator_type>;
  using expression_builder_node_type = expression_builder_node<generator_type>;

  using result_type = typename generator_type::result_type;
  using expression_builder_node_box_type =
      typename expression_builder_node_type::box_type;
  using optional_token_type = typename builder_node_type::optional_token_type;

protected:
private:
  max_float_type m_data;

public:
  explicit float_builder_node(
      max_float_type p_data,
      optional_token_type p_token = optional_token_type())
      : expression_builder_node_type(std::move(p_token)), m_data(p_data) {}

  ~float_builder_node() override = default;
};

} // namespace implementation

} // namespace ovis::ast

#endif // OVIS_AST_NODES_FLOAT_BUILDER_NODE_HPP