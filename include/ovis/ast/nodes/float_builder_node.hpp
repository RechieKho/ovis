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
  using runtime_error_type = std::runtime_error;

  using result_type = typename generator_type::result_type;
  using expression_builder_node_box_type =
      typename expression_builder_node_type::box_type;
  using optional_token_type = typename builder_node_type::optional_token_type;
  using optional_exception_type =
      typename builder_node_type::optional_exception_type;

protected:
private:
  max_float_type m_data;
  expression_builder_node_type::numeric_capacity m_numeric_capacity;

public:
  explicit float_builder_node(
      max_float_type p_data,
      expression_builder_node_type::numeric_capacity p_numeric_capacity =
          expression_builder_node_type::numeric_capacity::numeric_capacity_32
              optional_token_type p_token = optional_token_type())
      : expression_builder_node_type(std::move(p_token)), m_data(p_data),
        m_numeric_capacity(p_numeric_capacity) {}

  ~float_builder_node() override = default;

  auto analyse() const -> optional_exception_type override {
    if (m_numeric_capacity != expression_builder_node_type::numeric_capacity::
                                  numeric_capacity_32 &&
        m_numeric_capacity !=
            expression_builder_node_type::numeric_capacity::numeric_capacity_64)
      return optional_exception_type(
          runtime_error_type("Fail to support such float capacity, only 32 "
                             "bits and 64 bits floats are allowed."));
    return builder_node_type::analyze();
  }

  auto get_numeric_class() const
      -> expression_builder_node_type::numeric_class override {
    return expression_builder_node_type::numeric_class::numeric_class_float;
  }

  auto get_numeric_capacity() const
      -> expression_builder_node_type::numeric_capacity override {
    return m_numeric_capacity;
  }

  auto generate() const -> result_type override {
    const auto analysis = analyse();
    if (analysis)
      throw *analysis;

    const auto self_numeric_capacity = get_numeric_capacity();
    if (self_numeric_capacity ==
        expression_builder_node_type::numeric_capacity::numeric_capacity_64)
      return generator_type::get_singleton().generate_float64(m_data);
    return generator_type::get_singleton().generate_float32(m_data);
  }
};

} // namespace implementation

} // namespace ovis::ast

#endif // OVIS_AST_NODES_FLOAT_BUILDER_NODE_HPP