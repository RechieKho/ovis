#include <fstream>
#include <map>

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Process.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>

#include <ovis/codegen/llvm_codegen.hpp>
#include <ovis/codegen/runtime.hpp>

namespace ovis::codegen {

namespace {

using core::base_type;
using core::expression;
using core::statement;
using core::value_type;

class generator {
  struct variable {
    llvm::Value *address;
    value_type type;
  };
  struct loop_targets {
    llvm::BasicBlock *continue_block;
    llvm::BasicBlock *break_block;
  };

  const core::program &source;
  llvm::LLVMContext &context;
  std::unique_ptr<llvm::Module> module;
  llvm::IRBuilder<> builder;

  std::map<string_type, llvm::Function *> functions;
  std::map<string_type, variable> globals;
  std::vector<std::map<string_type, variable>> scopes;
  std::vector<loop_targets> loops;
  llvm::Function *current_function = nullptr;
  value_type current_return_type;
  bool in_main = false;

  // Types.

  auto type_of(const value_type &p_type) -> llvm::Type * {
    if (p_type.is_list)
      return builder.getPtrTy();
    switch (p_type.base) {
    case base_type::nothing:
      return builder.getVoidTy();
    case base_type::integer:
      return builder.getInt64Ty();
    case base_type::real:
      return builder.getDoubleTy();
    case base_type::boolean:
      return builder.getInt1Ty();
    case base_type::string:
      return builder.getPtrTy();
    }
    return nullptr;
  }

  // Booleans are stored as bytes inside lists so the C runtime can read them.
  auto element_storage_type(const value_type &p_element) -> llvm::Type * {
    return p_element.base == base_type::boolean ? builder.getInt8Ty()
                                                : type_of(p_element);
  }

  auto element_size(const value_type &p_element) -> llvm::Value * {
    return builder.getInt64(p_element.base == base_type::boolean ? 1 : 8);
  }

  auto zero_of(const value_type &p_type) -> llvm::Value * {
    if (p_type == value_type::string())
      return builder.CreateGlobalStringPtr("");
    if (p_type.is_list)
      return runtime_call("ovis_list_new", builder.getPtrTy(),
                          {element_size(p_type.element()), builder.getInt64(0)});
    return llvm::Constant::getNullValue(type_of(p_type));
  }

  auto coerce(llvm::Value *p_value, const value_type &p_from,
              const value_type &p_to) -> llvm::Value * {
    if (p_from == value_type::integer() && p_to == value_type::real())
      return builder.CreateSIToFP(p_value, builder.getDoubleTy());
    return p_value;
  }

  // Runtime and intrinsics.

  auto runtime_call(const char *p_name, llvm::Type *p_return,
                    std::initializer_list<llvm::Value *> p_arguments)
      -> llvm::Value * {
    std::vector<llvm::Type *> parameters;
    for (auto *const argument : p_arguments)
      parameters.push_back(argument->getType());
    const auto callee = module->getOrInsertFunction(
        p_name, llvm::FunctionType::get(p_return, parameters, false));
    return builder.CreateCall(callee, p_arguments);
  }

  auto intrinsic(llvm::Intrinsic::ID p_id,
                 std::initializer_list<llvm::Value *> p_arguments)
      -> llvm::Value * {
    auto *const declaration = llvm::Intrinsic::getDeclaration(
        module.get(), p_id, {builder.getDoubleTy()});
    return builder.CreateCall(declaration, p_arguments);
  }

  // Variables.

  auto create_entry_alloca(llvm::Type *p_type, const string_type &p_name)
      -> llvm::AllocaInst * {
    auto &entry = current_function->getEntryBlock();
    llvm::IRBuilder<> entry_builder(&entry, entry.begin());
    return entry_builder.CreateAlloca(p_type, nullptr, p_name);
  }

  auto lookup(const string_type &p_name) -> variable & {
    for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope)
      if (const auto found = scope->find(p_name); found != scope->end())
        return found->second;
    return globals.at(p_name);
  }

  auto declare_local(const string_type &p_name, const value_type &p_type)
      -> variable & {
    auto *const address = create_entry_alloca(type_of(p_type), p_name);
    return scopes.back()[p_name] = variable{address, p_type};
  }

  // Expressions.

  auto generate_arithmetic(const expression &p_call) -> llvm::Value * {
    const auto &type = p_call.type;
    const bool is_real = type == value_type::real();
    const auto &name = p_call.text;

    auto operand = [&](std::size_t p_index) {
      const auto &argument = p_call.arguments[p_index];
      return coerce(generate(argument), argument.type, type);
    };

    if (name == "-" && p_call.arguments.size() == 1)
      return is_real ? builder.CreateFNeg(operand(0))
                     : builder.CreateNeg(operand(0));

    auto *result = operand(0);
    for (std::size_t index = 1; index < p_call.arguments.size(); ++index) {
      auto *const right = operand(index);
      if (name == "+") {
        result = is_real ? builder.CreateFAdd(result, right)
                         : builder.CreateAdd(result, right);
      } else if (name == "-") {
        result = is_real ? builder.CreateFSub(result, right)
                         : builder.CreateSub(result, right);
      } else if (name == "*") {
        result = is_real ? builder.CreateFMul(result, right)
                         : builder.CreateMul(result, right);
      } else if (name == "/" || name == "%") {
        if (is_real) {
          result = name == "/" ? builder.CreateFDiv(result, right)
                               : builder.CreateFRem(result, right);
        } else {
          runtime_call("ovis_check_divisor", builder.getVoidTy(), {right});
          result = name == "/" ? builder.CreateSDiv(result, right)
                               : builder.CreateSRem(result, right);
        }
      } else {
        // min / max
        auto *const right_wins =
            is_real ? (name == "min" ? builder.CreateFCmpOLT(right, result)
                                     : builder.CreateFCmpOGT(right, result))
                    : (name == "min" ? builder.CreateICmpSLT(right, result)
                                     : builder.CreateICmpSGT(right, result));
        result = builder.CreateSelect(right_wins, right, result);
      }
    }
    return result;
  }

  auto generate_comparison(const expression &p_call) -> llvm::Value * {
    const auto &left_expression = p_call.arguments[0];
    const auto &right_expression = p_call.arguments[1];
    const auto &name = p_call.text;
    auto *left = generate(left_expression);
    auto *right = generate(right_expression);

    if (left_expression.type == value_type::string()) {
      auto *const equal = builder.CreateICmpNE(
          runtime_call("ovis_str_eq", builder.getInt32Ty(), {left, right}),
          builder.getInt32(0));
      return name == "=" ? equal : builder.CreateNot(equal);
    }

    if (left_expression.type.is_numeric()) {
      const auto common =
          left_expression.type == value_type::real() ||
                  right_expression.type == value_type::real()
              ? value_type::real()
              : value_type::integer();
      left = coerce(left, left_expression.type, common);
      right = coerce(right, right_expression.type, common);
      if (common == value_type::real()) {
        if (name == "=")
          return builder.CreateFCmpOEQ(left, right);
        if (name == "!=")
          return builder.CreateFCmpUNE(left, right);
        if (name == "<")
          return builder.CreateFCmpOLT(left, right);
        if (name == "<=")
          return builder.CreateFCmpOLE(left, right);
        if (name == ">")
          return builder.CreateFCmpOGT(left, right);
        return builder.CreateFCmpOGE(left, right);
      }
    }
    if (name == "=")
      return builder.CreateICmpEQ(left, right);
    if (name == "!=")
      return builder.CreateICmpNE(left, right);
    if (name == "<")
      return builder.CreateICmpSLT(left, right);
    if (name == "<=")
      return builder.CreateICmpSLE(left, right);
    if (name == ">")
      return builder.CreateICmpSGT(left, right);
    return builder.CreateICmpSGE(left, right);
  }

  // `and` / `or` only evaluate as many operands as needed.
  auto generate_logical(const expression &p_call) -> llvm::Value * {
    const bool is_and = p_call.text == "and";
    auto *const end_block = llvm::BasicBlock::Create(context, "logic.end");
    std::vector<std::pair<llvm::Value *, llvm::BasicBlock *>> incoming;

    for (std::size_t index = 0; index < p_call.arguments.size(); ++index) {
      auto *const value = generate(p_call.arguments[index]);
      if (index + 1 == p_call.arguments.size()) {
        incoming.emplace_back(value, builder.GetInsertBlock());
        builder.CreateBr(end_block);
        break;
      }
      auto *const next_block = llvm::BasicBlock::Create(
          context, "logic.next", current_function);
      incoming.emplace_back(builder.getInt1(!is_and), builder.GetInsertBlock());
      if (is_and)
        builder.CreateCondBr(value, next_block, end_block);
      else
        builder.CreateCondBr(value, end_block, next_block);
      builder.SetInsertPoint(next_block);
    }

    end_block->insertInto(current_function);
    builder.SetInsertPoint(end_block);
    auto *const phi = builder.CreatePHI(builder.getInt1Ty(), incoming.size());
    for (const auto &[value, block] : incoming)
      phi->addIncoming(value, block);
    return phi;
  }

  auto element_address(llvm::Value *p_list, llvm::Value *p_index,
                       const value_type &p_element) -> llvm::Value * {
    return runtime_call("ovis_list_at", builder.getPtrTy(),
                        {p_list, p_index, element_size(p_element)});
  }

  auto store_element(llvm::Value *p_address, llvm::Value *p_value,
                     const value_type &p_element) -> void {
    if (p_element.base == base_type::boolean)
      p_value = builder.CreateZExt(p_value, builder.getInt8Ty());
    builder.CreateStore(p_value, p_address);
  }

  auto load_element(llvm::Value *p_address, const value_type &p_element)
      -> llvm::Value * {
    auto *const value =
        builder.CreateLoad(element_storage_type(p_element), p_address);
    if (p_element.base == base_type::boolean)
      return builder.CreateICmpNE(value, builder.getInt8(0));
    return value;
  }

  auto generate_call(const expression &p_call) -> llvm::Value * {
    const auto &name = p_call.text;
    const auto &arguments = p_call.arguments;

    if (name == "+" || name == "-" || name == "*" || name == "/" ||
        name == "%" || name == "min" || name == "max")
      return generate_arithmetic(p_call);
    if (name == "=" || name == "!=" || name == "<" || name == "<=" ||
        name == ">" || name == ">=")
      return generate_comparison(p_call);
    if (name == "and" || name == "or")
      return generate_logical(p_call);
    if (name == "not")
      return builder.CreateNot(generate(arguments[0]));

    if (name == "int") {
      auto *const value = generate(arguments[0]);
      if (arguments[0].type == value_type::real())
        return builder.CreateFPToSI(value, builder.getInt64Ty());
      if (arguments[0].type == value_type::boolean())
        return builder.CreateZExt(value, builder.getInt64Ty());
      return value;
    }

    auto real_argument = [&](std::size_t p_index) {
      return coerce(generate(arguments[p_index]), arguments[p_index].type,
                    value_type::real());
    };
    if (name == "float")
      return real_argument(0);
    if (name == "sqrt")
      return intrinsic(llvm::Intrinsic::sqrt, {real_argument(0)});
    if (name == "floor")
      return intrinsic(llvm::Intrinsic::floor, {real_argument(0)});
    if (name == "pow")
      return intrinsic(llvm::Intrinsic::pow,
                       {real_argument(0), real_argument(1)});
    if (name == "abs") {
      auto *const value = generate(arguments[0]);
      if (p_call.type == value_type::real())
        return intrinsic(llvm::Intrinsic::fabs, {value});
      return builder.CreateSelect(
          builder.CreateICmpSLT(value, builder.getInt64(0)),
          builder.CreateNeg(value), value);
    }

    if (name == "len")
      return runtime_call("ovis_list_len", builder.getInt64Ty(),
                          {generate(arguments[0])});
    if (name == "get") {
      auto *const list = generate(arguments[0]);
      auto *const index = generate(arguments[1]);
      return load_element(element_address(list, index, p_call.type),
                          p_call.type);
    }
    if (name == "list" || name == "array") {
      const auto element = p_call.type.element();
      const bool is_literal = name == "list";
      auto *const count = is_literal ? builder.getInt64(arguments.size())
                                     : generate(arguments[0]);
      auto *const list = runtime_call("ovis_list_new", builder.getPtrTy(),
                                      {element_size(element), count});
      if (is_literal) {
        for (std::size_t index = 0; index < arguments.size(); ++index) {
          auto *const value = coerce(generate(arguments[index]),
                                     arguments[index].type, element);
          store_element(
              element_address(list, builder.getInt64(index), element), value,
              element);
        }
      } else if (element == value_type::string()) {
        // Zeroed memory is a null pointer; make string arrays hold "".
        for_range(builder.getInt64(0),
                  builder.CreateSub(count, builder.getInt64(1)),
                  [&](llvm::Value *p_index) {
                    store_element(element_address(list, p_index, element),
                                  zero_of(element), element);
                  });
      }
      return list;
    }
    if (name == "append") {
      const auto element = arguments[0].type.element();
      auto *const list = generate(arguments[0]);
      auto *const value =
          coerce(generate(arguments[1]), arguments[1].type, element);
      auto *const address = runtime_call("ovis_list_push", builder.getPtrTy(),
                                         {list, element_size(element)});
      store_element(address, value, element);
      return nullptr;
    }
    if (name == "read_int")
      return runtime_call("ovis_read_int", builder.getInt64Ty(), {});
    if (name == "read_float")
      return runtime_call("ovis_read_float", builder.getDoubleTy(), {});
    if (name == "random")
      return runtime_call("ovis_random", builder.getInt64Ty(),
                          {generate(arguments[0]), generate(arguments[1])});

    auto *const callee = functions.at(name);
    const auto &declaration = *find_function(name);
    std::vector<llvm::Value *> values;
    for (std::size_t index = 0; index < arguments.size(); ++index)
      values.push_back(coerce(generate(arguments[index]),
                              arguments[index].type,
                              declaration.parameters[index].type));
    return builder.CreateCall(callee, values);
  }

  auto generate(const expression &p_expression) -> llvm::Value * {
    using kind = expression::kind_type;
    switch (p_expression.kind) {
    case kind::integer:
      return builder.getInt64(p_expression.integer);
    case kind::real:
      return llvm::ConstantFP::get(builder.getDoubleTy(), p_expression.real);
    case kind::boolean:
      return builder.getInt1(p_expression.boolean);
    case kind::string:
      return builder.CreateGlobalStringPtr(p_expression.text);
    case kind::variable: {
      const auto &found = lookup(p_expression.text);
      return builder.CreateLoad(type_of(found.type), found.address,
                                p_expression.text);
    }
    case kind::call:
      return generate_call(p_expression);
    }
    return nullptr;
  }

  // Statements.

  // Calls `p_body` for each index from `p_first` to `p_last` inclusive.
  template <typename t_body_type>
  auto for_range(llvm::Value *p_first, llvm::Value *p_last,
                 t_body_type p_body) -> void {
    auto *const counter =
        create_entry_alloca(builder.getInt64Ty(), "range.index");
    builder.CreateStore(p_first, counter);
    auto *const condition_block =
        llvm::BasicBlock::Create(context, "range.condition", current_function);
    auto *const body_block =
        llvm::BasicBlock::Create(context, "range.body", current_function);
    auto *const end_block =
        llvm::BasicBlock::Create(context, "range.end", current_function);
    builder.CreateBr(condition_block);
    builder.SetInsertPoint(condition_block);
    auto *const index = builder.CreateLoad(builder.getInt64Ty(), counter);
    builder.CreateCondBr(builder.CreateICmpSLE(index, p_last), body_block,
                         end_block);
    builder.SetInsertPoint(body_block);
    p_body(index);
    builder.CreateStore(builder.CreateAdd(index, builder.getInt64(1)), counter);
    builder.CreateBr(condition_block);
    builder.SetInsertPoint(end_block);
  }

  auto is_terminated() -> bool {
    return builder.GetInsertBlock()->getTerminator() != nullptr;
  }

  // Code after break, continue or return still needs somewhere to go.
  auto start_unreachable_block() -> void {
    builder.SetInsertPoint(
        llvm::BasicBlock::Create(context, "unreachable", current_function));
  }

  auto generate_block(const std::vector<statement> &p_body) -> void {
    scopes.emplace_back();
    for (const auto &statement : p_body)
      generate(statement);
    scopes.pop_back();
  }

  auto generate_loop(llvm::BasicBlock *p_condition_block,
                     llvm::BasicBlock *p_continue_block,
                     llvm::BasicBlock *p_end_block,
                     const std::vector<statement> &p_body) -> void {
    loops.push_back({p_continue_block, p_end_block});
    generate_block(p_body);
    loops.pop_back();
    if (!is_terminated())
      builder.CreateBr(p_continue_block);
    if (p_continue_block != p_condition_block) {
      builder.SetInsertPoint(p_continue_block);
    }
  }

  auto print_value(llvm::Value *p_value, const value_type &p_type) -> void {
    if (p_type.is_list) {
      runtime_call("ovis_print_list", builder.getVoidTy(),
                   {p_value, builder.getInt32(static_cast<int>(p_type.base) -
                                              static_cast<int>(
                                                  base_type::integer))});
      return;
    }
    switch (p_type.base) {
    case base_type::integer:
      runtime_call("ovis_print_int", builder.getVoidTy(), {p_value});
      break;
    case base_type::real:
      runtime_call("ovis_print_float", builder.getVoidTy(), {p_value});
      break;
    case base_type::boolean:
      runtime_call("ovis_print_bool", builder.getVoidTy(),
                   {builder.CreateZExt(p_value, builder.getInt32Ty())});
      break;
    case base_type::string:
      runtime_call("ovis_print_str", builder.getVoidTy(), {p_value});
      break;
    case base_type::nothing:
      break;
    }
  }

  auto generate(const statement &p_statement) -> void {
    using kind = statement::kind_type;
    const auto &values = p_statement.values;
    switch (p_statement.kind) {
    case kind::let: {
      const auto type = *p_statement.declared_type;
      auto *const value =
          values.empty()
              ? zero_of(type)
              : coerce(generate(values[0]), values[0].type, type);
      if (in_main && scopes.size() == 1) {
        builder.CreateStore(value, globals.at(p_statement.name).address);
      } else {
        builder.CreateStore(value,
                            declare_local(p_statement.name, type).address);
      }
      break;
    }
    case kind::set: {
      const auto &target = lookup(p_statement.name);
      builder.CreateStore(coerce(generate(values[0]), values[0].type,
                                 target.type),
                          target.address);
      break;
    }
    case kind::put: {
      const auto &target = lookup(p_statement.name);
      const auto element = target.type.element();
      auto *const list =
          builder.CreateLoad(builder.getPtrTy(), target.address);
      auto *const address =
          element_address(list, generate(values[0]), element);
      store_element(address,
                    coerce(generate(values[1]), values[1].type, element),
                    element);
      break;
    }
    case kind::print:
      for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0)
          runtime_call("ovis_print_space", builder.getVoidTy(), {});
        print_value(generate(values[index]), values[index].type);
      }
      runtime_call("ovis_print_newline", builder.getVoidTy(), {});
      break;
    case kind::if_: {
      auto *const then_block =
          llvm::BasicBlock::Create(context, "if.then", current_function);
      auto *const else_block =
          llvm::BasicBlock::Create(context, "if.else", current_function);
      auto *const end_block =
          llvm::BasicBlock::Create(context, "if.end", current_function);
      builder.CreateCondBr(generate(values[0]), then_block, else_block);
      builder.SetInsertPoint(then_block);
      generate_block(p_statement.body);
      if (!is_terminated())
        builder.CreateBr(end_block);
      builder.SetInsertPoint(else_block);
      generate_block(p_statement.else_body);
      if (!is_terminated())
        builder.CreateBr(end_block);
      builder.SetInsertPoint(end_block);
      break;
    }
    case kind::while_: {
      auto *const condition_block =
          llvm::BasicBlock::Create(context, "while.condition", current_function);
      auto *const body_block =
          llvm::BasicBlock::Create(context, "while.body", current_function);
      auto *const end_block =
          llvm::BasicBlock::Create(context, "while.end", current_function);
      builder.CreateBr(condition_block);
      builder.SetInsertPoint(condition_block);
      builder.CreateCondBr(generate(values[0]), body_block, end_block);
      builder.SetInsertPoint(body_block);
      generate_loop(condition_block, condition_block, end_block,
                    p_statement.body);
      builder.SetInsertPoint(end_block);
      break;
    }
    case kind::for_:
    case kind::each: {
      const bool is_each = p_statement.kind == kind::each;
      scopes.emplace_back();
      auto *const counter =
          create_entry_alloca(builder.getInt64Ty(), p_statement.name + ".index");
      llvm::Value *list = nullptr;
      llvm::Value *last = nullptr;
      if (is_each) {
        list = generate(values[0]);
        builder.CreateStore(builder.getInt64(0), counter);
      } else {
        builder.CreateStore(generate(values[0]), counter);
        last = generate(values[1]);
      }

      auto *const condition_block =
          llvm::BasicBlock::Create(context, "for.condition", current_function);
      auto *const body_block =
          llvm::BasicBlock::Create(context, "for.body", current_function);
      auto *const step_block =
          llvm::BasicBlock::Create(context, "for.step", current_function);
      auto *const end_block =
          llvm::BasicBlock::Create(context, "for.end", current_function);
      builder.CreateBr(condition_block);

      builder.SetInsertPoint(condition_block);
      auto *const index = builder.CreateLoad(builder.getInt64Ty(), counter);
      auto *const keep_going =
          is_each ? builder.CreateICmpSLT(
                        index, runtime_call("ovis_list_len",
                                            builder.getInt64Ty(), {list}))
                  : builder.CreateICmpSLE(index, last);
      builder.CreateCondBr(keep_going, body_block, end_block);

      builder.SetInsertPoint(body_block);
      if (is_each) {
        const auto element = values[0].type.element();
        auto &item = declare_local(p_statement.name, element);
        builder.CreateStore(
            load_element(element_address(list, index, element), element),
            item.address);
      } else {
        // The loop variable is a copy, so assigning to it in the body does
        // not change how often the loop runs.
        auto &item = declare_local(p_statement.name, value_type::integer());
        builder.CreateStore(index, item.address);
      }
      generate_loop(condition_block, step_block, end_block, p_statement.body);
      builder.CreateStore(
          builder.CreateAdd(builder.CreateLoad(builder.getInt64Ty(), counter),
                            builder.getInt64(1)),
          counter);
      builder.CreateBr(condition_block);
      builder.SetInsertPoint(end_block);
      scopes.pop_back();
      break;
    }
    case kind::break_:
    case kind::continue_:
      builder.CreateBr(p_statement.kind == kind::break_
                           ? loops.back().break_block
                           : loops.back().continue_block);
      start_unreachable_block();
      break;
    case kind::return_:
      generate_return(values.empty() ? nullptr : &values[0]);
      start_unreachable_block();
      break;
    case kind::evaluate:
      generate(values[0]);
      break;
    case kind::pass:
      break;
    }
  }

  auto generate_return(const expression *p_value) -> void {
    if (in_main) {
      builder.CreateRet(
          p_value == nullptr
              ? builder.getInt32(0)
              : builder.CreateTrunc(generate(*p_value), builder.getInt32Ty()));
      return;
    }
    if (current_return_type == value_type::nothing()) {
      builder.CreateRetVoid();
      return;
    }
    builder.CreateRet(p_value == nullptr
                          ? zero_of(current_return_type)
                          : coerce(generate(*p_value), p_value->type,
                                   current_return_type));
  }

  auto find_function(const string_type &p_name) const -> const core::function * {
    for (const auto &unit : source.units)
      for (const auto &function : unit.functions)
        if (function.name == p_name)
          return &function;
    return nullptr;
  }

  auto declare_functions() -> void {
    for (const auto &unit : source.units) {
      for (const auto &function : unit.functions) {
        std::vector<llvm::Type *> parameters;
        for (const auto &parameter : function.parameters)
          parameters.push_back(type_of(parameter.type));
        auto *const type = llvm::FunctionType::get(
            type_of(function.return_type), parameters, false);
        // Prefixed so user functions never clash with C library symbols.
        auto *const declaration =
            llvm::Function::Create(type, llvm::Function::InternalLinkage,
                                   "ovis." + function.name, module.get());
        for (std::size_t index = 0; index < parameters.size(); ++index)
          declaration->getArg(index)->setName(function.parameters[index].name);
        functions[function.name] = declaration;
      }
    }
  }

  auto declare_globals() -> void {
    for (const auto &unit : source.units) {
      for (const auto &statement : unit.statements) {
        if (statement.kind != statement::kind_type::let ||
            globals.contains(statement.name))
          continue;
        auto *const type = type_of(*statement.declared_type);
        auto *const global = new llvm::GlobalVariable(
            *module, type, false, llvm::GlobalValue::InternalLinkage,
            llvm::Constant::getNullValue(type), statement.name);
        globals[statement.name] = {global, *statement.declared_type};
      }
    }
  }

  auto finish_function() -> void {
    if (is_terminated())
      return;
    generate_return(nullptr);
  }

  auto define_main() -> void {
    auto *const main = llvm::Function::Create(
        llvm::FunctionType::get(builder.getInt32Ty(), false),
        llvm::Function::ExternalLinkage, "main", module.get());
    current_function = main;
    in_main = true;
    builder.SetInsertPoint(llvm::BasicBlock::Create(context, "entry", main));
    runtime_call("ovis_init", builder.getVoidTy(), {});
    scopes.emplace_back();
    // Globals whose type has a non-zero default (strings, lists) need it set
    // before any function can observe them.
    for (const auto &[name, global] : globals)
      if (global.type == value_type::string() || global.type.is_list)
        builder.CreateStore(zero_of(global.type), global.address);
    for (const auto &unit : source.units)
      for (const auto &statement : unit.statements)
        generate(statement);
    scopes.pop_back();
    finish_function();
    in_main = false;
  }

  auto define_functions() -> void {
    for (const auto &unit : source.units) {
      for (const auto &function : unit.functions) {
        current_function = functions.at(function.name);
        current_return_type = function.return_type;
        builder.SetInsertPoint(
            llvm::BasicBlock::Create(context, "entry", current_function));
        scopes.emplace_back();
        for (std::size_t index = 0; index < function.parameters.size();
             ++index) {
          const auto &parameter = function.parameters[index];
          builder.CreateStore(current_function->getArg(index),
                              declare_local(parameter.name, parameter.type)
                                  .address);
        }
        for (const auto &statement : function.body)
          generate(statement);
        finish_function();
        scopes.pop_back();
      }
    }
  }

public:
  generator(const core::program &p_program, llvm::LLVMContext &p_context,
            string_view_type p_module_name)
      : source(p_program), context(p_context),
        module(std::make_unique<llvm::Module>(
            llvm::StringRef(p_module_name.data(), p_module_name.size()),
            p_context)),
        builder(p_context) {}

  auto run() -> std::unique_ptr<llvm::Module> {
    declare_functions();
    declare_globals();
    define_main();
    define_functions();

    string_type problems;
    llvm::raw_string_ostream stream(problems);
    if (llvm::verifyModule(*module, &stream))
      throw error("Internal compiler error, generated invalid LLVM IR:\n" +
                  problems);
    return std::move(module);
  }
};

auto host_target_machine() -> std::unique_ptr<llvm::TargetMachine> {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  const auto triple = llvm::sys::getDefaultTargetTriple();
  string_type problem;
  const auto *const target = llvm::TargetRegistry::lookupTarget(triple, problem);
  if (target == nullptr)
    throw error("No LLVM target for " + triple + ": " + problem);
  return std::unique_ptr<llvm::TargetMachine>(target->createTargetMachine(
      triple, "generic", "", llvm::TargetOptions(), llvm::Reloc::PIC_));
}

auto find_c_compiler() -> string_type {
  for (const auto *const name : {"cc", "clang", "gcc"})
    if (auto found = llvm::sys::findProgramByName(name))
      return *found;
  throw error("A C compiler (cc, clang or gcc) is needed to link programs.");
}

} // namespace

auto generate_module(const core::program &p_program,
                     llvm::LLVMContext &p_context,
                     string_view_type p_module_name)
    -> std::unique_ptr<llvm::Module> {
  return generator(p_program, p_context, p_module_name).run();
}

auto optimize(llvm::Module &p_module) -> void {
  const auto machine = host_target_machine();
  p_module.setTargetTriple(machine->getTargetTriple().str());
  p_module.setDataLayout(machine->createDataLayout());

  llvm::LoopAnalysisManager loop_analysis;
  llvm::FunctionAnalysisManager function_analysis;
  llvm::CGSCCAnalysisManager cgscc_analysis;
  llvm::ModuleAnalysisManager module_analysis;
  llvm::PassBuilder passes(machine.get());
  passes.registerModuleAnalyses(module_analysis);
  passes.registerCGSCCAnalyses(cgscc_analysis);
  passes.registerFunctionAnalyses(function_analysis);
  passes.registerLoopAnalyses(loop_analysis);
  passes.crossRegisterProxies(loop_analysis, function_analysis, cgscc_analysis,
                              module_analysis);
  passes.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2)
      .run(p_module, module_analysis);
}

auto to_ir_text(const llvm::Module &p_module) -> string_type {
  string_type result;
  llvm::raw_string_ostream stream(result);
  p_module.print(stream, nullptr);
  return result;
}

auto build_executable(llvm::Module &p_module,
                      const std::filesystem::path &p_output) -> void {
  const auto machine = host_target_machine();
  p_module.setTargetTriple(machine->getTargetTriple().str());
  p_module.setDataLayout(machine->createDataLayout());

  const auto work_directory =
      std::filesystem::temp_directory_path() /
      ("ovis-" + std::to_string(llvm::sys::Process::getProcessId()));
  std::filesystem::create_directories(work_directory);
  const auto object_path = work_directory / "program.o";
  const auto runtime_path = work_directory / "runtime.c";

  {
    std::error_code code;
    llvm::raw_fd_ostream object(object_path.string(), code,
                                llvm::sys::fs::OF_None);
    if (code)
      throw error("Cannot write " + object_path.string() + ": " +
                  code.message());
    llvm::legacy::PassManager emitter;
    if (machine->addPassesToEmitFile(emitter, object, nullptr,
                                     llvm::CodeGenFileType::ObjectFile))
      throw error("LLVM cannot emit object files for this target.");
    emitter.run(p_module);
  }
  std::ofstream(runtime_path) << runtime_source();

  const auto compiler = find_c_compiler();
  const auto output = p_output.string();
  const auto object = object_path.string();
  const auto runtime = runtime_path.string();
  const std::vector<llvm::StringRef> arguments = {
      compiler, "-O2", "-w", object, runtime, "-o", output, "-lm"};
  string_type message;
  const auto status =
      llvm::sys::ExecuteAndWait(compiler, arguments, std::nullopt, {}, 0, 0,
                                &message);
  std::filesystem::remove_all(work_directory);
  if (status != 0)
    throw error("Linking failed (" + compiler + "): " + message);
}

} // namespace ovis::codegen
