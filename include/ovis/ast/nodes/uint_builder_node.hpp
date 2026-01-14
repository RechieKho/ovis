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

  auto get_numeric_capacity() const
      -> expression_builder_node_type::numeric_capacity override {
    if ((m_data & UINT8_MAX) == m_data)
      return expression_builder_node_type::numeric_capacity::numeric_capacity_8;
    if ((m_data & UINT16_MAX) == m_data)
      return expression_builder_node_type::numeric_capacity::
          numeric_capacity_16;
    if ((m_data & UINT32_MAX) == m_data)
      return expression_builder_node_type::numeric_capacity::
          numeric_capacity_32;
    if ((m_data & UINT64_MAX) == m_data)
      return expression_builder_node_type::numeric_capacity::
          numeric_capacity_64;
    return expression_builder_node_type::get_numeric_capacity();
  }

  auto generate() const -> result_type override {
    switch (get_numeric_capacity()) {
    case expression_builder_node_type::numeric_capacity::numeric_capacity_8:
      return generator_type::get_singleton().generate_uint8(
          static_cast<uint8_t>(m_data));
    case expression_builder_node_type::numeric_capacity::numeric_capacity_16:
      return generator_type::get_singleton().generate_uint16(
          static_cast<uint16_t>(m_data));
    case expression_builder_node_type::numeric_capacity::numeric_capacity_32:
      return generator_type::get_singleton().generate_uint32(
          static_cast<uint32_t>(m_data));
    default:
      return generator_type::get_singleton().generate_uint64(
          static_cast<uint64_t>(m_data));
    }
  }
};

} // namespace implementation

} // namespace ovis::ast

#endif // OVIS_AST_NODES_XINT_BUILDER_NODE_HPP