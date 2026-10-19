#include <algorithm>
#include <fstream>
#include <map>
#include <set>

#include <llvm/Config/llvm-config.h>
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
    // How many cleanup frames were open outside the loop body.
    std::size_t frame_depth;
  };

  const core::program &source;
  llvm::LLVMContext &context;
  std::unique_ptr<llvm::Module> module;
  llvm::IRBuilder<> builder;

  std::map<string_type, llvm::Function *> functions;
  std::map<string_type, const core::c_function *> c_functions;
  // Each record is a struct of its fields, always reached through a pointer.
  std::map<string_type, const core::record *> records;
  std::map<string_type, llvm::StructType *> record_types;
  // Every record starts with a number saying which record it is (its place
  // in `records`), so a value known only by a trait can find its methods.
  std::map<string_type, unsigned> record_tags;
  std::map<string_type, const core::trait *> traits;
  std::map<string_type, const core::trait *> method_owner;
  std::set<std::pair<string_type, string_type>> implemented;
  // A trait method → one function per record, by tag; null where the record
  // doesn't implement the trait.
  std::map<string_type, llvm::GlobalVariable *> dispatch_tables;
  std::map<string_type, llvm::Function *> record_printers;
  std::map<string_type, variable> globals;
  std::vector<std::map<string_type, variable>> scopes;
  std::vector<loop_targets> loops;
  // One frame per open block (a function's body, `main`, an `if` branch, a
  // loop body): the `defer` statements reached so far, in order. They are
  // copied to every exit of the block.
  std::vector<std::vector<const statement *>> frames;
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
    case base_type::pointer:
    case base_type::record:
      return builder.getPtrTy();
    case base_type::unknown:
      break;
    }
    throw error("Internal compiler error: a type was not worked out.");
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
      return builder.CreateGlobalString("");
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
    // LLVM 20 renamed getDeclaration; keep building against LLVM 18 too.
#if LLVM_VERSION_MAJOR >= 20
    auto *const declaration = llvm::Intrinsic::getOrInsertDeclaration(
        module.get(), p_id, {builder.getDoubleTy()});
#else
    auto *const declaration = llvm::Intrinsic::getDeclaration(
        module.get(), p_id, {builder.getDoubleTy()});
#endif
    return builder.CreateCall(declaration, p_arguments);
  }

  // Variables.

  auto create_entry_alloca(llvm::Type *p_type, const string_type &p_name)
      -> llvm::AllocaInst * {
    auto &entry = current_function->getEntryBlock();
    llvm::IRBuilder<> entry_builder(&entry, entry.begin());
    return entry_builder.CreateAlloca(p_type, nullptr, p_name);
  }

  auto find_variable(const string_type &p_name) -> variable * {
    for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope)
      if (const auto found = scope->find(p_name); found != scope->end())
        return &found->second;
    if (const auto found = globals.find(p_name); found != globals.end())
      return &found->second;
    return nullptr;
  }

  auto lookup(const string_type &p_name) -> variable & {
    return *find_variable(p_name);
  }

  auto generate_constant(const core::c_constant &p_constant)
      -> llvm::Value * {
    using kind = core::c_constant::kind_type;
    switch (p_constant.kind) {
    case kind::integer:
      return builder.getInt64(p_constant.integer);
    case kind::real:
      return llvm::ConstantFP::get(builder.getDoubleTy(), p_constant.real);
    case kind::null:
      return llvm::ConstantPointerNull::get(builder.getPtrTy());
    case kind::runtime:
      return runtime_call(p_constant.getter.c_str(),
                          p_constant.name.starts_with("std")
                              ? static_cast<llvm::Type *>(builder.getPtrTy())
                              : builder.getInt64Ty(),
                          {});
    }
    return nullptr;
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
      } else if (name == "/" || name == "%" || name == "div") {
        if (is_real) {
          result = name == "%" ? builder.CreateFRem(result, right)
                               : builder.CreateFDiv(result, right);
        } else {
          runtime_call("ovis_check_divisor", builder.getVoidTy(), {right});
          result = name == "%" ? builder.CreateSRem(result, right)
                               : builder.CreateSDiv(result, right);
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

    if (left_expression.type == value_type::pointer() ||
        right_expression.type == value_type::pointer())
      return name == "=" ? builder.CreateICmpEQ(left, right)
                         : builder.CreateICmpNE(left, right);
    if (left_expression.type == value_type::string()) {
      auto *const equal = builder.CreateICmpNE(
          runtime_call("ovis_str_eq", builder.getInt32Ty(), {left, right}),
          builder.getInt32(0));
      if (name == "=" || name == "!=")
        return name == "=" ? equal : builder.CreateNot(equal);
      // Ordered text compares the sign of strcmp with 0.
      left = runtime_call("ovis_str_cmp", builder.getInt32Ty(), {left, right});
      right = builder.getInt32(0);
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

  auto to_text(llvm::Value *p_value, const value_type &p_type)
      -> llvm::Value * {
    switch (p_type.base) {
    case base_type::integer:
      return runtime_call("ovis_int_to_str", builder.getPtrTy(), {p_value});
    case base_type::real:
      return runtime_call("ovis_float_to_str", builder.getPtrTy(), {p_value});
    case base_type::boolean:
      return runtime_call("ovis_bool_to_str", builder.getPtrTy(),
                          {builder.CreateZExt(p_value, builder.getInt32Ty())});
    default:
      return p_value;
    }
  }

  // Records.

  auto field_index(const string_type &p_record, const string_type &p_field)
      -> unsigned {
    const auto &fields = records.at(p_record)->fields;
    for (unsigned index = 0; index < fields.size(); ++index)
      if (fields[index].name == p_field)
        return index + 1; // After the tag.
    throw error("Internal compiler error: no field " + p_field + ".");
  }

  // The address of a field, after making sure the record exists: a variable
  // declared before it is given a record holds null.
  auto field_address(llvm::Value *p_record, const string_type &p_name,
                     const string_type &p_field) -> llvm::Value * {
    runtime_call("ovis_record_check", builder.getVoidTy(), {p_record});
    return builder.CreateStructGEP(record_types.at(p_name), p_record,
                                   field_index(p_name, p_field));
  }

  auto generate_new(const expression &p_call) -> llvm::Value * {
    const auto &name = p_call.type_argument->record;
    auto *const type = record_types.at(name);
    auto *const record =
        runtime_call("ovis_record_new", builder.getPtrTy(),
                     {llvm::ConstantExpr::getSizeOf(type)});
    builder.CreateStore(builder.getInt64(record_tags.at(name)),
                        builder.CreateStructGEP(type, record, 0));
    for (std::size_t index = 0; index < p_call.arguments.size(); ++index) {
      const auto &field_name = p_call.fields[index];
      const auto &field = *records.at(name)->find(field_name);
      builder.CreateStore(coerce(generate(p_call.arguments[index]),
                                 p_call.arguments[index].type, field.type),
                          builder.CreateStructGEP(
                              type, record, field_index(name, field_name)));
    }
    return record;
  }

  // A method called on a value known only as a trait: the function is found
  // in the method's table by the tag at the start of the record.
  auto generate_dispatch(const expression &p_call) -> llvm::Value * {
    const auto &signature = *method_owner.at(p_call.text)->find(p_call.text);
    auto *const self = generate(p_call.arguments[0]);
    runtime_call("ovis_record_check", builder.getVoidTy(), {self});
    auto *const tag = builder.CreateLoad(builder.getInt64Ty(), self);
    auto *const table = dispatch_tables.at(p_call.text);
    auto *const entry = builder.CreateInBoundsGEP(
        table->getValueType(), table, {builder.getInt64(0), tag});
    auto *const callee = builder.CreateLoad(builder.getPtrTy(), entry);
    runtime_call("ovis_method_check", builder.getVoidTy(), {callee});
    std::vector<llvm::Type *> parameters;
    std::vector<llvm::Value *> values{self};
    for (std::size_t index = 0; index < signature.parameters.size(); ++index) {
      parameters.push_back(type_of(signature.parameters[index].type));
      if (index > 0)
        values.push_back(coerce(generate(p_call.arguments[index]),
                                p_call.arguments[index].type,
                                signature.parameters[index].type));
    }
    return builder.CreateCall(
        llvm::FunctionType::get(type_of(signature.return_type), parameters,
                                false),
        callee, values);
  }

  auto generate_concat(const expression &p_call) -> llvm::Value * {
    llvm::Value *result = nullptr;
    for (const auto &argument : p_call.arguments) {
      auto *const text = to_text(generate(argument), argument.type);
      result = result == nullptr
                   ? text
                   : runtime_call("ovis_concat", builder.getPtrTy(),
                                  {result, text});
    }
    return result;
  }

  static auto c_llvm_type(llvm::IRBuilder<> &p_builder, core::c_type p_type)
      -> llvm::Type * {
    using enum core::c_type;
    switch (p_type) {
    case void_type:
      return p_builder.getVoidTy();
    case int8_type:
    case uint8_type:
      return p_builder.getInt8Ty();
    case int16_type:
    case uint16_type:
      return p_builder.getInt16Ty();
    case int_type:
    case uint_type:
      return p_builder.getInt32Ty();
    case long_type:
    case size_type:
      return p_builder.getInt64Ty();
    case double_type:
      return p_builder.getDoubleTy();
    case float_type:
      return p_builder.getFloatTy();
    case string_type:
    case pointer_type:
      return p_builder.getPtrTy();
    }
    return nullptr;
  }

  // Converts an Ovis value to a C type.
  auto to_c(llvm::Value *p_value, const value_type &p_from, core::c_type p_to)
      -> llvm::Value * {
    p_value = coerce(p_value, p_from, core::ovis_type_of(p_to));
    auto *const type = c_llvm_type(builder, p_to);
    if (p_to == core::c_type::float_type)
      return builder.CreateFPTrunc(p_value, type);
    if (!type->isIntegerTy() || type->getIntegerBitWidth() == 64)
      return p_value;
    return p_from == value_type::boolean()
               ? builder.CreateZExt(p_value, type)
               : builder.CreateTrunc(p_value, type);
  }

  // Converts a C value to its Ovis type.
  auto from_c(llvm::Value *p_value, core::c_type p_type) -> llvm::Value * {
    if (p_type == core::c_type::float_type)
      return builder.CreateFPExt(p_value, builder.getDoubleTy());
    if (!p_value->getType()->isIntegerTy() ||
        p_value->getType()->getIntegerBitWidth() == 64)
      return p_value;
    return core::is_unsigned(p_type)
               ? builder.CreateZExt(p_value, builder.getInt64Ty())
               : builder.CreateSExt(p_value, builder.getInt64Ty());
  }

  // A variadic argument with C's default promotions.
  auto to_c_variadic(llvm::Value *p_value, const value_type &p_type)
      -> llvm::Value * {
    if (p_type == value_type::boolean())
      return builder.CreateZExt(p_value, builder.getInt32Ty());
    return p_value;
  }

  // Calls a printf-style function whose format is written out. Integer
  // conversions get the `ll` length so every Ovis int is passed as 64 bits,
  // and values are converted to what each conversion shows.
  auto generate_format_arguments(const expression &p_call,
                                 const core::c_function &p_callee,
                                 std::vector<llvm::Value *> &p_values)
      -> void {
    const auto &format = p_call.arguments[*p_callee.format_index];
    const auto conversions = core::parse_format(format.text);
    string_type rewritten;
    std::size_t copied = 0;
    auto argument = p_callee.parameters.size();
    for (const auto &conversion : conversions) {
      const auto letter = conversion.conversion;
      auto spec_end = conversion.end - 1;
      if (string_view_type("diouxX").contains(letter)) {
        while (string_view_type("hlLqjzt").contains(format.text[spec_end - 1]))
          --spec_end;
        rewritten += format.text.substr(copied, spec_end - copied) + "ll";
        rewritten.push_back(letter);
      } else {
        rewritten += format.text.substr(copied, conversion.end - copied);
      }
      copied = conversion.end;

      if (conversion.takes_star_width) {
        const auto &width = p_call.arguments[argument++];
        p_values.push_back(
            builder.CreateTrunc(generate(width), builder.getInt32Ty()));
      }
      const auto &value_expression = p_call.arguments[argument++];
      auto *value = generate(value_expression);
      const auto &type = value_expression.type;
      if (string_view_type("diouxX").contains(letter)) {
        if (type == value_type::real())
          value = builder.CreateFPToSI(value, builder.getInt64Ty());
        else if (type == value_type::boolean())
          value = builder.CreateZExt(value, builder.getInt64Ty());
      } else if (letter == 'c') {
        value = type == value_type::real()
                    ? builder.CreateFPToSI(value, builder.getInt32Ty())
                : type == value_type::boolean()
                    ? builder.CreateZExt(value, builder.getInt32Ty())
                    : builder.CreateTrunc(value, builder.getInt32Ty());
      } else if (type == value_type::integer() &&
                 !string_view_type("sp").contains(letter)) {
        value = builder.CreateSIToFP(value, builder.getDoubleTy());
      }
      p_values.push_back(value);
    }
    rewritten += format.text.substr(copied);
    p_values[*p_callee.format_index] =
        builder.CreateGlobalString(rewritten);
  }

  auto generate_c_call(const expression &p_call,
                       const core::c_function &p_callee) -> llvm::Value * {
    std::vector<llvm::Type *> parameter_types;
    for (const auto parameter : p_callee.parameters)
      parameter_types.push_back(c_llvm_type(builder, parameter));
    const auto declaration = module->getOrInsertFunction(
        p_callee.name,
        llvm::FunctionType::get(c_llvm_type(builder, p_callee.result),
                                parameter_types, p_callee.is_variadic));

    const auto &arguments = p_call.arguments;
    std::vector<llvm::Value *> values;
    // (ref x) through a pointer to a narrower C type (`int *`, `float *`)
    // goes through a temporary of that type, copied back after the call.
    struct write_back {
      llvm::Value *temporary;
      core::c_type type;
      variable *target;
    };
    std::vector<write_back> write_backs;
    for (std::size_t index = 0; index < p_callee.parameters.size(); ++index) {
      const auto &argument = arguments[index];
      const auto pointee = index < p_callee.pointees.size()
                               ? p_callee.pointees[index]
                               : std::nullopt;
      if (argument.kind == expression::kind_type::call &&
          argument.text == "ref" && pointee) {
        auto *const target = find_variable(argument.arguments[0].text);
        auto *const type = c_llvm_type(builder, *pointee);
        if (type == type_of(target->type)) {
          values.push_back(target->address);
          continue;
        }
        auto *const temporary = create_entry_alloca(type, "ref");
        builder.CreateStore(
            to_c(builder.CreateLoad(type_of(target->type), target->address),
                 target->type, *pointee),
            temporary);
        values.push_back(temporary);
        write_backs.push_back({temporary, *pointee, target});
        continue;
      }
      values.push_back(to_c(generate(argument), argument.type,
                            p_callee.parameters[index]));
    }
    if (p_callee.format_index &&
        arguments[*p_callee.format_index].kind ==
            expression::kind_type::string) {
      generate_format_arguments(p_call, p_callee, values);
    } else {
      for (std::size_t index = p_callee.parameters.size();
           index < arguments.size(); ++index)
        values.push_back(
            to_c_variadic(generate(arguments[index]), arguments[index].type));
    }
    auto *const result = builder.CreateCall(declaration, values);
    for (const auto &item : write_backs)
      builder.CreateStore(
          from_c(builder.CreateLoad(c_llvm_type(builder, item.type),
                                    item.temporary),
                 item.type),
          item.target->address);
    return p_callee.result == core::c_type::void_type
               ? nullptr
               : from_c(result, p_callee.result);
  }

  auto generate_call(const expression &p_call) -> llvm::Value * {
    const auto &name = p_call.text;
    const auto &arguments = p_call.arguments;

    if ((name == "+" && p_call.type == value_type::string()) ||
        name == "concat")
      return generate_concat(p_call);
    if (name == "to_str")
      return to_text(generate(arguments[0]), arguments[0].type);
    if (name == "ref")
      return find_variable(arguments[0].text)->address;
    if (name == "new")
      return generate_new(p_call);
    if (name == "field") {
      const auto &record = arguments[0].type.record;
      return builder.CreateLoad(
          type_of(p_call.type),
          field_address(generate(arguments[0]), record, p_call.fields[0]));
    }
    if (name == "+" || name == "-" || name == "*" || name == "/" ||
        name == "%" || name == "div" || name == "min" || name == "max")
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
    if (name == "read_line")
      return runtime_call("ovis_read_line", builder.getPtrTy(), {});
    if (name == "substring")
      return runtime_call("ovis_substring", builder.getPtrTy(),
                          {generate(arguments[0]), generate(arguments[1]),
                           generate(arguments[2])});
    if (name == "char_at")
      return runtime_call("ovis_char_at", builder.getInt64Ty(),
                          {generate(arguments[0]), generate(arguments[1])});
    if (name == "chr" || name == "buffer")
      return runtime_call(name == "chr" ? "ovis_chr" : "ovis_buffer",
                          builder.getPtrTy(), {generate(arguments[0])});
    if (name == "sort") {
      runtime_call("ovis_list_sort", builder.getVoidTy(),
                   {generate(arguments[0]), list_kind(arguments[0].type)});
      return nullptr;
    }
    if (name == "remove-at") {
      runtime_call("ovis_list_remove", builder.getVoidTy(),
                   {generate(arguments[0]), generate(arguments[1]),
                    element_size(arguments[0].type.element())});
      return nullptr;
    }
    if (name == "reverse") {
      runtime_call("ovis_list_reverse", builder.getVoidTy(),
                   {generate(arguments[0]),
                    element_size(arguments[0].type.element())});
      return nullptr;
    }

    if (method_owner.contains(name) && arguments[0].type.is_record() &&
        traits.contains(arguments[0].type.record))
      return generate_dispatch(p_call);
    if (!functions.contains(name))
      return generate_c_call(p_call, *c_functions.at(name));
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
      return builder.CreateGlobalString(p_expression.text);
    case kind::variable: {
      const auto *const found = find_variable(p_expression.text);
      if (found == nullptr)
        return generate_constant(*core::find_constant(source, p_expression.text));
      return builder.CreateLoad(type_of(found->type), found->address,
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

  // Not getTerminator(): from LLVM 23 it assumes the block has one and
  // gives back its last instruction, so every non-empty block looked done.
  auto is_terminated() -> bool {
    const auto *const block = builder.GetInsertBlock();
    return !block->empty() && block->back().isTerminator();
  }

  // Code after break, continue or return still needs somewhere to go.
  auto start_unreachable_block() -> void {
    builder.SetInsertPoint(
        llvm::BasicBlock::Create(context, "unreachable", current_function));
  }

  // Emits the deferred statements of the frames from the top down to
  // `p_depth`, last first, for an exit that leaves them. The frames are cut
  // below each one while its statements are emitted, so a `defer` inside a
  // deferred statement belongs to that statement and nothing runs twice.
  auto emit_frames(std::size_t p_depth) -> void {
    const auto saved = frames;
    for (auto index = saved.size(); index > p_depth; --index) {
      frames.resize(index - 1);
      const auto &deferred = saved[index - 1];
      for (auto statement = deferred.rbegin(); statement != deferred.rend();
           ++statement)
        generate_block((*statement)->body);
    }
    frames = saved;
  }

  auto generate_block(const std::vector<statement> &p_body) -> void {
    scopes.emplace_back();
    frames.emplace_back();
    for (const auto &statement : p_body)
      generate(statement);
    if (!is_terminated())
      emit_frames(frames.size() - 1);
    frames.pop_back();
    scopes.pop_back();
  }

  auto generate_loop(llvm::BasicBlock *p_condition_block,
                     llvm::BasicBlock *p_continue_block,
                     llvm::BasicBlock *p_end_block,
                     const std::vector<statement> &p_body) -> void {
    loops.push_back({p_continue_block, p_end_block, frames.size()});
    generate_block(p_body);
    loops.pop_back();
    if (!is_terminated())
      builder.CreateBr(p_continue_block);
    if (p_continue_block != p_condition_block) {
      builder.SetInsertPoint(p_continue_block);
    }
  }

  // How the runtime tells list element types apart.
  auto list_kind(const value_type &p_type) -> llvm::Value * {
    return builder.getInt32(static_cast<int>(p_type.base) -
                            static_cast<int>(base_type::integer));
  }

  auto print_text(string_view_type p_text) -> void {
    runtime_call("ovis_print_str", builder.getVoidTy(),
                 {builder.CreateGlobalString(
                     llvm::StringRef(p_text.data(), p_text.size()))});
  }

  // A record inside another value is bracketed, so its commas stay apart
  // from the outer value's: `[(a book with title "Dune", pages 412)]`.
  auto print_inner(llvm::Value *p_value, const value_type &p_type) -> void {
    if (p_type.is_record()) {
      builder.CreateCall(record_printer(p_type.record),
                         {p_value, builder.getInt1(true)});
    } else if (p_type == value_type::string()) {
      runtime_call("ovis_print_quoted", builder.getVoidTy(), {p_value});
    } else {
      print_value(p_value, p_type);
    }
  }

  // `ovis.print.NAME(record, is_inner)`: prints a record as it is written,
  // `a book with title "Dune", pages 412`, or `nothing`. A function, so a
  // record holding its own kind of record compiles.
  auto record_printer(const string_type &p_name) -> llvm::Function * {
    if (const auto found = record_printers.find(p_name);
        found != record_printers.end())
      return found->second;
    auto *const printer = llvm::Function::Create(
        llvm::FunctionType::get(builder.getVoidTy(),
                                {builder.getPtrTy(), builder.getInt1Ty()},
                                false),
        llvm::Function::InternalLinkage, "ovis.print." + p_name, module.get());
    record_printers[p_name] = printer;

    const llvm::IRBuilderBase::InsertPointGuard guard(builder);
    auto *const outer_function = current_function;
    current_function = printer;
    auto *const record = printer->getArg(0);
    auto *const is_inner = printer->getArg(1);
    if (traits.contains(p_name)) {
      // Known only as a trait: print the record it turns out to be.
      auto *const entry = llvm::BasicBlock::Create(context, "entry", printer);
      auto *const empty_block =
          llvm::BasicBlock::Create(context, "empty", printer);
      auto *const done_block = llvm::BasicBlock::Create(context, "done", printer);
      builder.SetInsertPoint(entry);
      auto *const next_block = llvm::BasicBlock::Create(context, "next", printer);
      builder.CreateCondBr(builder.CreateIsNull(record), empty_block, next_block);
      builder.SetInsertPoint(empty_block);
      print_text("nothing");
      builder.CreateBr(done_block);
      builder.SetInsertPoint(next_block);
      auto *const tag = builder.CreateLoad(builder.getInt64Ty(), record);
      for (const auto &[record_name, declaration] : records) {
        if (!implemented.contains({p_name, record_name}))
          continue;
        auto *const hit = llvm::BasicBlock::Create(context, "hit", printer);
        auto *const miss = llvm::BasicBlock::Create(context, "miss", printer);
        builder.CreateCondBr(
            builder.CreateICmpEQ(tag, builder.getInt64(record_tags.at(record_name))),
            hit, miss);
        builder.SetInsertPoint(hit);
        builder.CreateCall(record_printer(record_name), {record, is_inner});
        builder.CreateBr(done_block);
        builder.SetInsertPoint(miss);
      }
      builder.CreateBr(done_block);
      builder.SetInsertPoint(done_block);
      builder.CreateRetVoid();
      current_function = outer_function;
      return printer;
    }
    auto *const entry = llvm::BasicBlock::Create(context, "entry", printer);
    auto *const empty_block = llvm::BasicBlock::Create(context, "empty", printer);
    auto *const record_block =
        llvm::BasicBlock::Create(context, "record", printer);
    auto *const open_block = llvm::BasicBlock::Create(context, "open", printer);
    auto *const fields_block =
        llvm::BasicBlock::Create(context, "fields", printer);
    auto *const end_block = llvm::BasicBlock::Create(context, "end", printer);
    builder.SetInsertPoint(entry);
    builder.CreateCondBr(builder.CreateIsNull(record), empty_block,
                         record_block);
    builder.SetInsertPoint(empty_block);
    print_text("nothing");
    builder.CreateRetVoid();

    builder.SetInsertPoint(record_block);
    builder.CreateCondBr(is_inner, open_block, fields_block);
    builder.SetInsertPoint(open_block);
    print_text("(");
    builder.CreateBr(fields_block);

    builder.SetInsertPoint(fields_block);
    const auto &declaration = *records.at(p_name);
    print_text(core::describe_in_english(value_type::record_of(p_name)));
    for (std::size_t index = 0; index < declaration.fields.size(); ++index) {
      const auto &field = declaration.fields[index];
      // A field named in several words is `number_of_wheels` in Ovis Core,
      // and printed as written.
      auto spoken = field.name;
      std::ranges::replace(spoken, '_', ' ');
      print_text((index == 0 ? " with " : ", ") + spoken + " ");
      auto *const value = builder.CreateLoad(
          type_of(field.type),
          builder.CreateStructGEP(record_types.at(p_name), record,
                                  static_cast<unsigned>(index) + 1));
      print_inner(value, field.type);
    }
    auto *const close_block =
        llvm::BasicBlock::Create(context, "close", printer);
    builder.CreateCondBr(is_inner, close_block, end_block);
    builder.SetInsertPoint(close_block);
    print_text(")");
    builder.CreateBr(end_block);
    builder.SetInsertPoint(end_block);
    builder.CreateRetVoid();
    current_function = outer_function;
    return printer;
  }

  auto print_record_list(llvm::Value *p_list, const value_type &p_type)
      -> void {
    const auto element = p_type.element();
    print_text("[");
    for_range(builder.getInt64(0),
              builder.CreateSub(runtime_call("ovis_list_len",
                                             builder.getInt64Ty(), {p_list}),
                                builder.getInt64(1)),
              [&](llvm::Value *p_index) {
                auto *const comma_block = llvm::BasicBlock::Create(
                    context, "list.comma", current_function);
                auto *const item_block = llvm::BasicBlock::Create(
                    context, "list.item", current_function);
                builder.CreateCondBr(
                    builder.CreateICmpSGT(p_index, builder.getInt64(0)),
                    comma_block, item_block);
                builder.SetInsertPoint(comma_block);
                print_text(", ");
                builder.CreateBr(item_block);
                builder.SetInsertPoint(item_block);
                print_inner(load_element(element_address(p_list, p_index,
                                                         element),
                                         element),
                            element);
              });
    print_text("]");
  }

  auto print_value(llvm::Value *p_value, const value_type &p_type) -> void {
    if (p_type.is_list && p_type.base == base_type::record) {
      print_record_list(p_value, p_type);
      return;
    }
    if (p_type.is_list) {
      runtime_call("ovis_print_list", builder.getVoidTy(),
                   {p_value, list_kind(p_type)});
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
    case base_type::record:
      builder.CreateCall(record_printer(p_type.record),
                         {p_value, builder.getInt1(false)});
      break;
    case base_type::nothing:
    case base_type::pointer:
    case base_type::unknown:
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
    case kind::defer:
      frames.back().push_back(&p_statement);
      break;
    case kind::break_:
    case kind::continue_:
      emit_frames(loops.back().frame_depth);
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
    case kind::set_field: {
      const auto &record = values[0].type.record;
      const auto &field = *records.at(record)->find(p_statement.name);
      auto *const address =
          field_address(generate(values[0]), record, p_statement.name);
      builder.CreateStore(
          coerce(generate(values[1]), values[1].type, field.type), address);
      break;
    }
    case kind::pass:
      break;
    }
  }

  // The value is worked out before the deferred statements run, so they
  // can't change what is given back.
  auto generate_return(const expression *p_value) -> void {
    if (in_main) {
      auto *const value =
          p_value == nullptr
              ? builder.getInt32(0)
              : builder.CreateTrunc(generate(*p_value), builder.getInt32Ty());
      emit_frames(0);
      builder.CreateRet(value);
      return;
    }
    if (current_return_type == value_type::nothing()) {
      emit_frames(0);
      builder.CreateRetVoid();
      return;
    }
    auto *const value = p_value == nullptr
                            ? zero_of(current_return_type)
                            : coerce(generate(*p_value), p_value->type,
                                     current_return_type);
    emit_frames(0);
    builder.CreateRet(value);
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

  // One table per trait method, with the function each record answers it
  // with: its impl's, else the trait's default.
  auto declare_dispatch() -> void {
    for (const auto &unit : source.units) {
      for (const auto &trait : unit.traits)
        traits.emplace(trait.name, &trait);
      for (const auto &impl : unit.impls)
        implemented.emplace(impl.trait, impl.type);
    }
    for (const auto &[trait_name, trait] : traits) {
      for (const auto &method : trait->methods) {
        method_owner.emplace(method.name, trait);
        std::vector<llvm::Constant *> entries(
            records.size(),
            llvm::Constant::getNullValue(builder.getPtrTy()));
        for (const auto &[record_name, record] : records) {
          if (!implemented.contains({trait_name, record_name}))
            continue;
          const auto written =
              functions.find(core::impl_function_name(method.name, record_name));
          if (written != functions.end())
            entries[record_tags.at(record_name)] = written->second;
          else if (method.has_default)
            entries[record_tags.at(record_name)] = functions.at(method.name);
        }
        auto *const type =
            llvm::ArrayType::get(builder.getPtrTy(), records.size());
        dispatch_tables[method.name] = new llvm::GlobalVariable(
            *module, type, true, llvm::GlobalValue::InternalLinkage,
            llvm::ConstantArray::get(type, entries),
            "ovis.dispatch." + method.name);
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
    frames.emplace_back();
    for (const auto &unit : source.units)
      for (const auto &statement : unit.statements)
        generate(statement);
    finish_function();
    frames.pop_back();
    scopes.pop_back();
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
        frames.emplace_back();
        for (const auto &statement : function.body)
          generate(statement);
        finish_function();
        frames.pop_back();
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
        builder(p_context) {
    for (const auto &function : core::libc_functions())
      c_functions[function.name] = &function;
    for (const auto &unit : p_program.units)
      for (const auto &function : unit.externs)
        c_functions[function.name] = &function;
  }

  auto declare_records() -> void {
    for (const auto &unit : source.units)
      for (const auto &record : unit.records)
        records[record.name] = &record;
    for (const auto &[name, record] : records) {
      record_tags[name] = static_cast<unsigned>(record_tags.size());
      std::vector<llvm::Type *> fields{builder.getInt64Ty()};
      for (const auto &field : record->fields)
        fields.push_back(type_of(field.type));
      record_types[name] =
          llvm::StructType::create(context, fields, "ovis.record." + name);
    }
  }

  auto run() -> std::unique_ptr<llvm::Module> {
    declare_records();
    declare_functions();
    declare_dispatch();
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
  // LLVM 21 takes a Triple where LLVM 18 took a string.
#if LLVM_VERSION_MAJOR >= 21
  const llvm::Triple triple(llvm::sys::getDefaultTargetTriple());
#else
  const auto triple = llvm::sys::getDefaultTargetTriple();
#endif
  string_type problem;
  const auto *const target = llvm::TargetRegistry::lookupTarget(triple, problem);
  if (target == nullptr)
    throw error("No LLVM target for " + llvm::Triple(triple).str() + ": " +
                problem);
  return std::unique_ptr<llvm::TargetMachine>(target->createTargetMachine(
      triple, "generic", "", llvm::TargetOptions(), llvm::Reloc::PIC_));
}

auto set_target(llvm::Module &p_module, const llvm::TargetMachine &p_machine)
    -> void {
#if LLVM_VERSION_MAJOR >= 21
  p_module.setTargetTriple(p_machine.getTargetTriple());
#else
  p_module.setTargetTriple(p_machine.getTargetTriple().str());
#endif
  p_module.setDataLayout(p_machine.createDataLayout());
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
  set_target(p_module, *machine);

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

auto libraries_of(const core::program &p_program) -> std::vector<string_type> {
  std::vector<string_type> result = {"-lm"};
  const auto add = [&](string_type p_flag) {
    if (std::ranges::find(result, p_flag) == result.end())
      result.push_back(std::move(p_flag));
  };
  for (const auto &unit : p_program.units) {
    for (const auto &flag : unit.link_flags)
      add(flag);
    for (const auto &library : unit.links)
      add("-l" + library);
  }
  return result;
}

auto build_executable(llvm::Module &p_module,
                      const std::filesystem::path &p_output,
                      const std::vector<string_type> &p_link_flags) -> void {
  const auto machine = host_target_machine();
  set_target(p_module, *machine);

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
  std::vector<llvm::StringRef> arguments = {compiler, "-O2", "-w",  object,
                                            runtime,  "-o",  output};
  for (const auto &flag : p_link_flags)
    arguments.push_back(flag);
  string_type message;
  const auto status =
      llvm::sys::ExecuteAndWait(compiler, arguments, std::nullopt, {}, 0, 0,
                                &message);
  std::filesystem::remove_all(work_directory);
  if (status != 0)
    throw error("Linking failed (" + compiler + "): " + message);
}

} // namespace ovis::codegen
