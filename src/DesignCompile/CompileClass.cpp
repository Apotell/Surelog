/*
 Copyright 2019 Alain Dargelas

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

 http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
 */

/*
 * File:   CompileClass.cpp
 * Author: alain
 *
 * Created on June 7, 2018, 10:26 PM
 */

#include "Surelog/DesignCompile/CompileClass.h"

#include "Surelog/CommandLine/CommandLineParser.h"
#include "Surelog/Common/FileSystem.h"
#include "Surelog/Common/NodeId.h"
#include "Surelog/Common/SymbolId.h"
#include "Surelog/Design/FileCNodeId.h"
#include "Surelog/Design/FileContent.h"
#include "Surelog/Design/VObject.h"
#include "Surelog/DesignCompile/CompileDesign.h"
#include "Surelog/DesignCompile/CompileHelper.h"
#include "Surelog/ErrorReporting/Error.h"
#include "Surelog/ErrorReporting/ErrorContainer.h"
#include "Surelog/ErrorReporting/ErrorDefinition.h"
#include "Surelog/ErrorReporting/Location.h"
#include "Surelog/SourceCompile/Compiler.h"
#include "Surelog/SourceCompile/SymbolTable.h"
#include "Surelog/SourceCompile/VObjectTypes.h"
#include "Surelog/Testbench/ClassDefinition.h"
#include "Surelog/Testbench/Constraint.h"
#include "Surelog/Testbench/CoverGroupDefinition.h"
#include "Surelog/Testbench/FunctionMethod.h"
#include "Surelog/Testbench/Property.h"
#include "Surelog/Utils/StringUtils.h"

// UHDM
#include <uhdm/Serializer.h>
#include <uhdm/class_defn.h>
#include <uhdm/containers.h>

#include <cstdint>
#include <stack>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace SURELOG {
int32_t FunctorCompileClass::operator()() const {
  CompileClass* instance =
      new CompileClass(m_compileDesign, m_class, m_design, m_symbols, m_errors);
  instance->compile(Elaborate::No, Reduce::No);
  delete instance;
  return true;
}

bool CompileClass::compile(Elaborate elaborate, Reduce reduce) {
  if (m_class->m_fileContents.empty()) return true;

  m_helper.setElaborate(elaborate);
  m_helper.setReduce(reduce);

  const FileContent* fC = m_class->m_fileContents[0];
  if (fC == nullptr) return true;
  NodeId nodeId = m_class->m_nodeIds[0];

  std::vector<std::string_view> names;
  ClassDefinition* parent = m_class;
  DesignComponent* tmp_container = nullptr;
  while (parent) {
    tmp_container = parent->getContainer();
    names.push_back(parent->getName());
    parent = parent->m_parent;
  }

  std::string fullName;
  if (tmp_container) {
    fullName.assign(tmp_container->getName()).append("::");
  }
  if (!names.empty()) {
    uint32_t index = names.size() - 1;
    while (true) {
      fullName += names[index];
      if (index > 0) fullName += "::";
      if (index == 0) break;
      index--;
    }
  }

  if (m_class->m_uhdm_definition->VpiFullName().empty())
    m_class->m_uhdm_definition->VpiFullName(fullName);
  Location loc(fC->getFileId(nodeId), fC->Line(nodeId), fC->Column(nodeId),
               m_symbols->registerSymbol(fullName));

  Error err1(ErrorDefinition::COMP_COMPILE_CLASS, loc);
  ErrorContainer* errors =
      new ErrorContainer(m_symbols, m_errors->getLogListener());
  errors->registerCmdLine(
      m_compileDesign->getCompiler()->getCommandLineParser());
  errors->addError(err1);
  errors->printMessage(
      err1,
      m_compileDesign->getCompiler()->getCommandLineParser()->muteStdout());
  delete errors;
  if (fC->getSize() == 0) return true;

  NodeId classId = m_class->m_nodeIds[0];

  do {
    VObject current = fC->Object(classId);
    classId = current.m_parent;
  } while (classId && !((fC->Type(classId) == VObjectType::paDescription) ||
                        (fC->Type(classId) == VObjectType::paClass_item)));
  if (classId) {
    VObject current = fC->Object(classId);
    classId = current.m_child;
    if (fC->Type(classId) == VObjectType::paAttribute_instance) {
      if (UHDM::VectorOfattribute* attributes = m_helper.compileAttributes(
              m_class, fC, classId, m_compileDesign, nullptr)) {
        m_class->Attributes(attributes);
      }
    }
  }

  // Package imports
  std::vector<FileCNodeId> pack_imports;
  // - Local file imports
  for (auto import : fC->getObjects(VObjectType::paPackage_import_item)) {
    pack_imports.push_back(import);
  }
  // - Parent container imports
  DesignComponent* container = m_class->getContainer();
  if (container) {
    // FileCNodeId itself(container->getFileContents ()[0],
    // container->getNodeIds ()[0]); pack_imports.push_back(itself);
    for (auto import :
         container->getObjects(VObjectType::paPackage_import_item)) {
      pack_imports.push_back(import);
    }
  }

  for (const auto& pack_import : pack_imports) {
    const FileContent* pack_fC = pack_import.fC;
    NodeId pack_id = pack_import.nodeId;
    m_helper.importPackage(m_class, m_design, pack_fC, pack_id,
                           m_compileDesign);
  }

  compile_class_parameters_(fC, nodeId);

  // This
  DataType* thisdt =
      new DataType(fC, nodeId, "this", VObjectType::paClass_declaration);
  thisdt->setDefinition(m_class);
  Property* prop = new Property(thisdt, fC, nodeId, InvalidNodeId, "this",
                                false, false, false, false, false);
  m_class->insertProperty(prop);

  NodeId id = nodeId;
  if (!id) return false;
  std::stack<NodeId> stack;
  stack.push(id);
  bool inFunction_body_declaration = false;
  bool inTask_body_declaration = false;
  while (!stack.empty()) {
    bool skipGuts = false;
    id = stack.top();
    stack.pop();
    const VObject& current = fC->Object(id);
    VObjectType type = fC->Type(id);
    switch (type) {
      case VObjectType::paPackage_import_item: {
        m_helper.importPackage(m_class, m_design, fC, id, m_compileDesign);
        m_helper.compileImportDeclaration(m_class, fC, id, m_compileDesign);
        break;
      }
      case VObjectType::paClass_constructor_declaration: {
        inFunction_body_declaration = true;
        break;
      }
      case VObjectType::paFunction_body_declaration: {
        inFunction_body_declaration = true;
        break;
      }
      case VObjectType::paENDFUNCTION: {
        inFunction_body_declaration = false;
        break;
      }
      case VObjectType::paTask_body_declaration: {
        inTask_body_declaration = true;
        break;
      }
      case VObjectType::paENDTASK: {
        inTask_body_declaration = false;
        break;
      }
      case VObjectType::paClass_property:
        if (inFunction_body_declaration || inTask_body_declaration) break;
        compile_class_property_(fC, id);
        break;
      case VObjectType::paClass_constraint:
        compile_class_constraint_(fC, id);
        break;
      case VObjectType::paClass_declaration:
        if (id != nodeId) {
          compile_class_declaration_(fC, id);
          if (current.m_sibling) stack.push(current.m_sibling);
          continue;
        }
        break;
      case VObjectType::paCovergroup_declaration:
        compile_covergroup_declaration_(fC, id);
        break;
      case VObjectType::paLocal_parameter_declaration:
        compile_local_parameter_declaration_(fC, id);
        break;
      case VObjectType::paParameter_declaration:
        compile_parameter_declaration_(fC, id);
        break;
      case VObjectType::paClass_method:
        compile_class_method_(fC, id);
        skipGuts = true;
        break;
      case VObjectType::paClass_type:
        compile_class_type_(fC, id);
        break;
      case VObjectType::paLet_declaration: {
        m_helper.compileLetDeclaration(m_class, fC, id, m_compileDesign);
        break;
      }
      case VObjectType::slStringConst: {
        NodeId sibling = fC->Sibling(id);
        if (!sibling) {
          if (fC->Type(fC->Parent(id)) != VObjectType::paClass_declaration)
            break;
          const std::string_view endLabel = fC->SymName(id);
          m_class->setEndLabel(endLabel);
          std::string_view moduleName =
              StringUtils::ltrim_until(m_class->getName(), '@');
          if (endLabel != moduleName) {
            Location loc(fC->getFileId(m_class->getNodeIds()[0]),
                         fC->Line(m_class->getNodeIds()[0]),
                         fC->Column(m_class->getNodeIds()[0]),
                         m_compileDesign->getCompiler()
                             ->getSymbolTable()
                             ->registerSymbol(moduleName));
            Location loc2(fC->getFileId(id), fC->Line(id), fC->Column(id),
                          m_compileDesign->getCompiler()
                              ->getSymbolTable()
                              ->registerSymbol(endLabel));
            Error err(ErrorDefinition::COMP_UNMATCHED_LABEL, loc, loc2);
            m_compileDesign->getCompiler()->getErrorContainer()->addError(err);
          }
        }
        break;
      }
      default:
        break;
    }

    if (current.m_sibling) stack.push(current.m_sibling);
    if (!skipGuts)
      if (current.m_child) stack.push(current.m_child);
  }

  // Default constructor
  DataType* returnType = new DataType();
  FunctionMethod* method =
      new FunctionMethod(m_class, fC, nodeId, "new", returnType, false, false,
                         false, false, false, false);
  method->compile(m_helper);
  Function* prevDef = m_class->getFunction("new");
  if (!prevDef) {
    m_class->insertFunction(method);
  }

  return true;
}

bool CompileClass::compile_class_property_(const FileContent* fC, NodeId id) {
  NodeId data_declaration = fC->Child(id);
  m_helper.compileDataDeclaration(m_class, fC, data_declaration, false,
                                  m_compileDesign, Reduce::No, m_attributes);

  NodeId var_decl = fC->Child(data_declaration);
  VObjectType type = fC->Type(data_declaration);
  bool is_local = false;
  bool is_static = false;
  bool is_protected = false;
  bool is_rand = false;
  bool is_randc = false;
  while ((type == VObjectType::paPropQualifier_ClassItem) ||
         (type == VObjectType::paPropQualifier_Rand) ||
         (type == VObjectType::paPropQualifier_Randc)) {
    NodeId qualifier = fC->Child(data_declaration);
    VObjectType qualType = fC->Type(qualifier);
    if (qualType == VObjectType::paClassItemQualifier_Protected)
      is_protected = true;
    if (qualType == VObjectType::paClassItemQualifier_Static) is_static = true;
    if (qualType == VObjectType::paClassItemQualifier_Local) is_local = true;
    if (type == VObjectType::paPropQualifier_Rand) is_rand = true;
    if (type == VObjectType::paPropQualifier_Randc) is_randc = true;
    data_declaration = fC->Sibling(data_declaration);
    type = fC->Type(data_declaration);
    var_decl = fC->Child(data_declaration);
  }

  if (type == VObjectType::paData_declaration) {
    /*
   n<A> u<3> t<StringConst> p<37> s<12> l<5>
   n<> u<4> t<IntegerAtomType_Int> p<5> l<6>
   n<> u<5> t<Data_type> p<9> c<4> s<8> l<6>
   n<size> u<6> t<StringConst> p<7> l<6>
   n<> u<7> t<Variable_decl_assignment> p<8> c<6> l<6>
   n<> u<8> t<List_of_variable_decl_assignments> p<9> c<7> l<6>
   n<> u<9> t<Variable_declaration> p<10> c<5> l<6>
   n<> u<10> t<Data_declaration> p<11> c<9> l<6>
   n<> u<11> t<Class_property> p<12> c<10> l<6>
   n<> u<12> t<Class_item> p<37> c<11> s<35> l<6>
     */
    VObjectType var_type = fC->Type(var_decl);
    if (var_type == VObjectType::paVariable_declaration) {
      NodeId data_type = fC->Child(var_decl);
      NodeId node_type = fC->Child(data_type);

      VObjectType the_type = fC->Type(node_type);
      std::string typeName;
      if (the_type == VObjectType::slStringConst) {
        typeName = fC->SymName(node_type);
      } else if (the_type == VObjectType::paClass_scope) {
        NodeId class_type = fC->Child(node_type);
        NodeId class_name = fC->Child(class_type);
        typeName = fC->SymName(class_name);
        typeName += "::";
        NodeId symb_id = fC->Sibling(node_type);
        typeName += fC->SymName(symb_id);
      } else {
        typeName = VObject::getTypeName(the_type);
      }
      DataType* datatype = m_class->getUsedDataType(typeName);
      if (!datatype) {
        DataType* type =
            new DataType(fC, node_type, typeName, fC->Type(node_type));
        m_class->insertUsedDataType(typeName, type);
        datatype = m_class->getUsedDataType(typeName);
      }

      NodeId list_of_variable_decl_assignments = fC->Sibling(data_type);
      NodeId variable_decl_assignment =
          fC->Child(list_of_variable_decl_assignments);
      while (variable_decl_assignment) {
        NodeId var = fC->Child(variable_decl_assignment);
        NodeId range = fC->Sibling(var);
        const std::string_view varName = fC->SymName(var);

        Property* previous = m_class->getProperty(varName);
        if (previous) {
          Location loc1(fC->getFileId(var), fC->Line(var), fC->Column(var),
                        m_symbols->registerSymbol(varName));
          const FileContent* prevFile = previous->getFileContent();
          NodeId prevNode = previous->getNodeId();
          Location loc2(prevFile->getFileId(prevNode), prevFile->Line(prevNode),
                        prevFile->Column(prevNode),
                        m_symbols->registerSymbol(varName));
          Error err(ErrorDefinition::COMP_MULTIPLY_DEFINED_PROPERTY, loc1,
                    loc2);
          m_errors->addError(err);
        }

        Property* prop =
            new Property(datatype, fC, var, range, varName, is_local, is_static,
                         is_protected, is_rand, is_randc);
        m_class->insertProperty(prop);

        variable_decl_assignment = fC->Sibling(variable_decl_assignment);
      }
    }
  }

  return true;
}

bool CompileClass::compile_class_method_(const FileContent* fC, NodeId id) {
  /*
    n<> u<8> t<MethodQualifier_Virtual> p<21> s<20> l<12>
    n<> u<9> t<Function_data_type> p<10> l<12>
    n<> u<10> t<Function_data_type_or_implicit> p<19> c<9> s<11> l<12>
    n<print_tree1> u<11> t<StringConst> p<19> s<17> l<12>
    n<> u<12> t<IntegerAtomType_Int> p<13> l<12>
    n<> u<13> t<Data_type> p<14> c<12> l<12>
    n<> u<14> t<Data_type_or_implicit> p<16> c<13> s<15> l<12>
    n<a> u<15> t<StringConst> p<16> l<12>
    n<> u<16> t<Tf_port_item> p<17> c<14> l<12>
    n<> u<17> t<Tf_port_list> p<19> c<16> s<18> l<12>
    n<> u<18> t<Endfunction> p<19> l<14>
    n<> u<19> t<Function_body_declaration> p<20> c<10> l<12>
    n<> u<20> t<Function_declaration> p<21> c<19> l<12>
    n<> u<21> t<Class_method> p<22> c<8> l<12>
   */
  NodeId func_decl = fC->Child(id);
  VObjectType func_type = fC->Type(func_decl);
  std::string funcName;
  std::string taskName;
  bool is_virtual = false;
  bool is_extern = false;
  bool is_static = false;
  bool is_local = false;
  bool is_protected = false;
  bool is_pure = false;
  DataType* returnType = new DataType();
  while ((func_type == VObjectType::paMethodQualifier_Virtual) ||
         (func_type == VObjectType::paMethodQualifier_ClassItem) ||
         (func_type == VObjectType::paPure_virtual_qualifier) ||
         (func_type == VObjectType::paExtern_qualifier) ||
         (func_type == VObjectType::paClassItemQualifier_Protected)) {
    if (func_type == VObjectType::paMethodQualifier_Virtual) {
      is_virtual = true;
      func_decl = fC->Sibling(func_decl);
      func_type = fC->Type(func_decl);
    }
    if (func_type == VObjectType::paClassItemQualifier_Protected) {
      is_protected = true;
      func_decl = fC->Sibling(func_decl);
      func_type = fC->Type(func_decl);
    }
    if (func_type == VObjectType::paPure_virtual_qualifier) {
      is_virtual = true;
      is_pure = true;
      func_decl = fC->Sibling(func_decl);
      func_type = fC->Type(func_decl);
    }
    if (func_type == VObjectType::paExtern_qualifier) {
      is_extern = true;
      func_decl = fC->Sibling(func_decl);
      func_type = fC->Type(func_decl);
    }
    if (func_type == VObjectType::paMethodQualifier_ClassItem) {
      NodeId qualifier = fC->Child(func_decl);
      VObjectType type = fC->Type(qualifier);
      if (type == VObjectType::paClassItemQualifier_Static) is_static = true;
      if (type == VObjectType::paClassItemQualifier_Local) is_local = true;
      if (type == VObjectType::paClassItemQualifier_Protected)
        is_protected = true;
      func_decl = fC->Sibling(func_decl);
      func_type = fC->Type(func_decl);
    }
  }
  if (func_type == VObjectType::paFunction_declaration) {
    NodeId func_body_decl = fC->Child(func_decl);
    NodeId function_data_type_or_implicit = fC->Child(func_body_decl);
    NodeId function_data_type = fC->Child(function_data_type_or_implicit);
    NodeId data_type = fC->Child(function_data_type);
    NodeId type = fC->Child(data_type);
    VObjectType the_type = fC->Type(type);
    if (the_type == VObjectType::paVIRTUAL) {
      type = fC->Sibling(type);
      the_type = fC->Type(type);
    }
    std::string typeName;
    if (the_type == VObjectType::slStringConst) {
      typeName = fC->SymName(type);
    } else {
      typeName = VObject::getTypeName(the_type);
    }
    returnType->init(fC, type, typeName, fC->Type(type));
    NodeId function_name = fC->Sibling(function_data_type_or_implicit);
    if (function_name) {
      funcName = fC->SymName(function_name);
      if (builtins_.find(funcName) != builtins_.end()) {
        Location loc(fC->getFileId(), fC->Line(function_name),
                     fC->Column(function_name),
                     m_symbols->registerSymbol(funcName));
        Error err(ErrorDefinition::COMP_CANNOT_REDEFINE_BUILTIN_METHOD, loc);
        m_errors->addError(err);
      }
    }
    m_helper.compileFunction(m_class, fC, fC->Child(id), m_compileDesign,
                             Reduce::No, nullptr, true);
    m_helper.compileFunction(m_class, fC, fC->Child(id), m_compileDesign,
                             Reduce::No, nullptr, true);

  } else if (func_type == VObjectType::paTask_declaration) {
    /*
     n<cfg_dut> u<143> t<StringConst> p<146> s<144> l<37>
     n<> u<144> t<Endtask> p<146> s<145> l<39>
     n<cfg_dut> u<145> t<StringConst> p<146> l<39>
     n<> u<146> t<Task_body_declaration> p<147> c<143> l<37>
     n<> u<147> t<Task_declaration> p<148> c<146> l<37>
     n<> u<148> t<Class_method> p<149> c<147> l<37>
     */

    NodeId task_decl =
        m_helper.setFuncTaskQualifiers(fC, fC->Child(id), nullptr);
    NodeId Task_body_declaration;
    if (fC->Type(task_decl) == VObjectType::paTask_body_declaration)
      Task_body_declaration = task_decl;
    else
      Task_body_declaration = fC->Child(task_decl);
    NodeId task_name = fC->Child(Task_body_declaration);
    if (fC->Type(task_name) == VObjectType::slStringConst)
      taskName = fC->SymName(task_name);
    else if (fC->Type(task_name) == VObjectType::paClass_scope) {
      NodeId Class_type = fC->Child(task_name);
      taskName.assign(fC->SymName(fC->Child(Class_type)))
          .append("::")
          .append(fC->SymName(fC->Sibling(task_name)));
    }

    m_helper.compileTask(m_class, fC, fC->Child(id), m_compileDesign,
                         Reduce::No, nullptr, true);
    m_helper.compileTask(m_class, fC, fC->Child(id), m_compileDesign,
                         Reduce::No, nullptr, true);

  } else if (func_type == VObjectType::paMethod_prototype) {
    /*
     n<> u<65> t<IntVec_TypeBit> p<66> l<37>
     n<> u<66> t<Data_type> p<67> c<65> l<37>
     n<> u<67> t<Function_data_type> p<69> c<66> s<68> l<37>
     n<is_active> u<68> t<StringConst> p<69> l<37>
     n<> u<69> t<Function_prototype> p<70> c<67> l<37>
     n<> u<70> t<Method_prototype> p<71> c<69> l<37>
     n<> u<71> t<Class_method> p<72> c<70> l<37>
     */
    NodeId func_prototype = fC->Child(func_decl);
    if (fC->Type(func_prototype) == VObjectType::paTask_prototype) {
      NodeId task_decl =
          m_helper.setFuncTaskQualifiers(fC, fC->Child(id), nullptr);
      NodeId Task_body_declaration;
      if (fC->Type(task_decl) == VObjectType::paTask_body_declaration)
        Task_body_declaration = task_decl;
      else
        Task_body_declaration = fC->Child(task_decl);
      NodeId task_name = fC->Child(Task_body_declaration);
      if (fC->Type(task_name) == VObjectType::slStringConst)
        taskName = fC->SymName(task_name);
      else if (fC->Type(task_name) == VObjectType::paClass_scope) {
        NodeId Class_type = fC->Child(task_name);
        taskName.assign(fC->SymName(fC->Child(Class_type)))
            .append("::")
            .append(fC->SymName(fC->Sibling(task_name)));
      }

      m_helper.compileTask(m_class, fC, fC->Child(id), m_compileDesign,
                           Reduce::No, nullptr, true);
      m_helper.compileTask(m_class, fC, fC->Child(id), m_compileDesign,
                           Reduce::No, nullptr, true);

    } else {
      NodeId function_data_type = fC->Child(func_prototype);
      NodeId data_type = fC->Child(function_data_type);
      NodeId type = fC->Child(data_type);
      VObjectType the_type = fC->Type(type);
      std::string typeName;
      if (the_type == VObjectType::slStringConst) {
        typeName = fC->SymName(type);
      } else {
        typeName = VObject::getTypeName(the_type);
      }
      returnType->init(fC, type, typeName, fC->Type(type));
      NodeId function_name = fC->Sibling(function_data_type);
      funcName = fC->SymName(function_name);

      m_helper.compileFunction(m_class, fC, fC->Child(id), m_compileDesign,
                               Reduce::No, nullptr, true);
      m_helper.compileFunction(m_class, fC, fC->Child(id), m_compileDesign,
                               Reduce::No, nullptr, true);
    }
    is_extern = true;
  } else if (func_type == VObjectType::paClass_constructor_declaration) {
    funcName = "new";
    returnType->init(fC, InvalidNodeId, "void", VObjectType::slNoType);

    m_helper.compileClassConstructorDeclaration(m_class, fC, fC->Child(id),
                                                m_compileDesign);

  } else if (func_type == VObjectType::paClass_constructor_prototype) {
    funcName = "new";

    m_helper.compileFunction(m_class, fC, fC->Child(id), m_compileDesign,
                             Reduce::No, nullptr, true);
    m_helper.compileFunction(m_class, fC, fC->Child(id), m_compileDesign,
                             Reduce::No, nullptr, true);

  } else {
    funcName = "UNRECOGNIZED_METHOD_TYPE";
  }
  if (!taskName.empty()) {
    TaskMethod* method = new TaskMethod(m_class, fC, id, taskName, is_extern);
    method->compile(m_helper);
    TaskMethod* prevDef = m_class->getTask(taskName);
    if (prevDef) {
      Location loc1(fC->getFileId(id), fC->Line(id), fC->Column(id),
                    m_symbols->registerSymbol(taskName));
      const FileContent* prevFile = prevDef->getFileContent();
      NodeId prevNode = prevDef->getNodeId();
      Location loc2(prevFile->getFileId(prevNode), prevFile->Line(prevNode),
                    prevFile->Column(prevNode),
                    m_symbols->registerSymbol(taskName));
      Error err(ErrorDefinition::COMP_MULTIPLY_DEFINED_TASK, loc1, loc2);
      m_errors->addError(err);
    }
    m_class->insertTask(method);
  } else {
    FunctionMethod* method = new FunctionMethod(
        m_class, fC, id, funcName, returnType, is_virtual, is_extern, is_static,
        is_local, is_protected, is_pure);
    Variable* variable =
        new Variable(returnType, fC, id, InvalidNodeId, funcName);
    method->addVariable(variable);
    method->compile(m_helper);
    Function* prevDef = m_class->getFunction(funcName);
    if (prevDef) {
      SymbolId funcSymbol = m_symbols->registerSymbol(funcName);
      Location loc1(fC->getFileId(id), fC->Line(id), fC->Column(id),
                    funcSymbol);
      const FileContent* prevFile = prevDef->getFileContent();
      NodeId prevNode = prevDef->getNodeId();
      Location loc2(prevFile->getFileId(prevNode), prevFile->Line(prevNode),
                    prevFile->Column(prevNode), funcSymbol);
      if (funcSymbol) {
        Error err(ErrorDefinition::COMP_MULTIPLY_DEFINED_FUNCTION, loc1, loc2);
        m_errors->addError(err);
      }
    }
    m_class->insertFunction(method);
  }
  return true;
}

bool CompileClass::compile_class_constraint_(const FileContent* fC,
                                             NodeId class_constraint) {
  NodeId constraint_prototype = fC->Child(class_constraint);
  NodeId constraint_name = fC->Child(constraint_prototype);
  const std::string_view constName = fC->SymName(constraint_name);
  Constraint* prevDef = m_class->getConstraint(constName);
  if (prevDef) {
    Location loc1(fC->getFileId(class_constraint), fC->Line(class_constraint),
                  fC->Column(class_constraint),
                  m_symbols->registerSymbol(constName));
    const FileContent* prevFile = prevDef->getFileContent();
    NodeId prevNode = prevDef->getNodeId();
    Location loc2(prevFile->getFileId(prevNode), prevFile->Line(prevNode),
                  prevFile->Column(prevNode),
                  m_symbols->registerSymbol(constName));
    Error err(ErrorDefinition::COMP_MULTIPLY_DEFINED_CONSTRAINT, loc1, loc2);
    m_errors->addError(err);
  }
  Constraint* constraint = new Constraint(fC, class_constraint, constName);
  m_class->insertConstraint(constraint);

  return true;
}

bool CompileClass::compile_class_declaration_(const FileContent* fC,
                                              NodeId id) {
  UHDM::Serializer& s = m_compileDesign->getSerializer();
  const bool virtualClass = fC->sl_collect(id, VObjectType::paVIRTUAL);
  const NodeId class_name_id = fC->sl_collect(id, VObjectType::slStringConst);
  const std::string_view class_name = fC->SymName(class_name_id);
  std::string full_class_name =
      StrCat(m_class->m_uhdm_definition->VpiFullName(), "::", class_name);
  ClassDefinition* prevDef = m_class->getClass(class_name);
  if (prevDef) {
    Location loc1(fC->getFileId(class_name_id), fC->Line(class_name_id),
                  fC->Column(class_name_id),
                  m_symbols->registerSymbol(class_name));
    const FileContent* prevFile = prevDef->getFileContent();
    NodeId prevNode =
        prevFile->sl_collect(prevDef->getNodeId(), VObjectType::slStringConst);
    Location loc2(prevFile->getFileId(prevNode), prevFile->Line(prevNode),
                  prevFile->Column(prevNode),
                  m_symbols->registerSymbol(class_name));
    Error err(ErrorDefinition::COMP_MULTIPLY_DEFINED_INNER_CLASS, loc1, loc2);
    m_errors->addError(err);
  }
  UHDM::class_defn* defn = s.MakeClass_defn();
  defn->VpiVirtual(virtualClass);
  defn->VpiName(class_name);
  defn->VpiFullName(full_class_name);
  ClassDefinition* the_class =
      new ClassDefinition(class_name, m_class->getLibrary(),
                          m_class->getContainer(), fC, id, m_class, defn);
  m_class->insertClass(the_class);
  UHDM::class_defn* parent = m_class->getUhdmDefinition();
  defn->VpiParent(parent);
  UHDM::VectorOfscope* scopes = parent->Scopes();
  if (scopes == nullptr) {
    parent->Scopes(s.MakeScopeVec());
    scopes = parent->Scopes();
  }
  scopes->push_back(defn);

  CompileClass* instance = new CompileClass(m_compileDesign, the_class,
                                            m_design, m_symbols, m_errors);
  instance->compile(m_helper.getElaborate(), m_helper.getReduce());
  delete instance;

  return true;
}

bool CompileClass::compile_covergroup_declaration_(const FileContent* fC,
                                                   NodeId id) {
  NodeId covergroup_name = fC->Child(id);
  const std::string_view covergroupName = fC->SymName(covergroup_name);
  CoverGroupDefinition* prevDef = m_class->getCoverGroup(covergroupName);
  if (prevDef) {
    Location loc1(fC->getFileId(covergroup_name), fC->Line(covergroup_name),
                  fC->Column(covergroup_name),
                  m_symbols->registerSymbol(covergroupName));
    const FileContent* prevFile = prevDef->getFileContent();
    NodeId prevNode = prevDef->getNodeId();
    Location loc2(prevFile->getFileId(prevNode), prevFile->Line(prevNode),
                  prevFile->Column(prevNode),
                  m_symbols->registerSymbol(covergroupName));
    Error err(ErrorDefinition::COMP_MULTIPLY_DEFINED_COVERGROUP, loc1, loc2);
    m_errors->addError(err);
  }
  CoverGroupDefinition* covergroup =
      new CoverGroupDefinition(fC, id, covergroupName);
  m_class->insertCoverGroup(covergroup);

  return true;
}

bool CompileClass::compile_local_parameter_declaration_(const FileContent* fC,
                                                        NodeId id) {
  /*
   n<> u<8> t<IntegerAtomType_Int> p<9> l<3>
   n<> u<9> t<Data_type> p<10> c<8> l<3>
   n<> u<10> t<Data_type_or_implicit> p<20> c<9> s<19> l<3>
   n<FOO> u<11> t<StringConst> p<18> s<17> l<3>
   n<3> u<12> t<IntConst> p<13> l<3>
   n<> u<13> t<Primary_literal> p<14> c<12> l<3>
   n<> u<14> t<Constant_primary> p<15> c<13> l<3>
   n<> u<15> t<Constant_expression> p<16> c<14> l<3>
   n<> u<16> t<Constant_mintypmax_expression> p<17> c<15> l<3>
   n<> u<17> t<Constant_param_expression> p<18> c<16> l<3>
   n<> u<18> t<Param_assignment> p<19> c<11> l<3>
   n<> u<19> t<List_of_param_assignments> p<20> c<18> l<3>
   n<> u<20> t<Local_parameter_declaration> p<21> c<10> l<3>
  */
  NodeId list_of_type_assignments = fC->Child(id);
  if (fC->Type(list_of_type_assignments) ==
          VObjectType::paList_of_type_assignments ||
      fC->Type(list_of_type_assignments) == VObjectType::paTYPE) {
    // Type param
    m_helper.compileParameterDeclaration(m_class, fC, list_of_type_assignments,
                                         m_compileDesign, Reduce::No, true,
                                         nullptr, false, false);

  } else {
    m_helper.compileParameterDeclaration(m_class, fC, id, m_compileDesign,
                                         Reduce::No, true, nullptr, false,
                                         false);
  }
  NodeId data_type_or_implicit = fC->Child(id);
  NodeId list_of_param_assignments = fC->Sibling(data_type_or_implicit);
  NodeId param_assignment = fC->Child(list_of_param_assignments);
  while (param_assignment) {
    NodeId var = fC->Child(param_assignment);
    const std::string_view name = fC->SymName(var);
    const std::pair<FileCNodeId, DesignComponent*>* prevDef =
        m_class->getNamedObject(name);
    if (prevDef) {
      Location loc1(fC->getFileId(var), fC->Line(var), fC->Column(var),
                    m_symbols->registerSymbol(name));
      const FileContent* prevFile = prevDef->first.fC;
      NodeId prevNode = prevDef->first.nodeId;
      Location loc2(prevFile->getFileId(prevNode), prevFile->Line(prevNode),
                    prevFile->Column(prevNode),
                    m_symbols->registerSymbol(name));
      Error err(ErrorDefinition::COMP_MULTIPLY_DEFINED_PARAMETER, loc1, loc2);
      m_errors->addError(err);
    }

    FileCNodeId fnid(fC, id);
    m_class->addObject(VObjectType::paLocal_parameter_declaration, fnid);
    m_class->addNamedObject(name, fnid, nullptr);

    param_assignment = fC->Sibling(param_assignment);
  }
  return true;
}

bool CompileClass::compile_parameter_declaration_(const FileContent* fC,
                                                  NodeId id) {
  NodeId list_of_type_assignments = fC->Child(id);
  if (fC->Type(list_of_type_assignments) ==
          VObjectType::paList_of_type_assignments ||
      fC->Type(list_of_type_assignments) == VObjectType::paTYPE) {
    // Type param
    m_helper.compileParameterDeclaration(m_class, fC, list_of_type_assignments,
                                         m_compileDesign, Reduce::No, false,
                                         nullptr, false, false);

  } else {
    m_helper.compileParameterDeclaration(m_class, fC, id, m_compileDesign,
                                         Reduce::No, false, nullptr, false,
                                         false);
  }

  NodeId data_type_or_implicit = fC->Child(id);
  NodeId list_of_param_assignments = fC->Sibling(data_type_or_implicit);
  NodeId param_assignment = fC->Child(list_of_param_assignments);
  while (param_assignment) {
    NodeId var = fC->Child(param_assignment);
    const std::string_view name = fC->SymName(var);
    const std::pair<FileCNodeId, DesignComponent*>* prevDef =
        m_class->getNamedObject(name);
    if (prevDef) {
      Location loc1(fC->getFileId(var), fC->Line(var), fC->Column(var),
                    m_symbols->registerSymbol(name));
      const FileContent* prevFile = prevDef->first.fC;
      NodeId prevNode = prevDef->first.nodeId;
      Location loc2(prevFile->getFileId(prevNode), prevFile->Line(prevNode),
                    prevFile->Column(prevNode),
                    m_symbols->registerSymbol(name));
      Error err(ErrorDefinition::COMP_MULTIPLY_DEFINED_PARAMETER, loc1, loc2);
      m_errors->addError(err);
    }

    FileCNodeId fnid(fC, id);
    m_class->addObject(VObjectType::paLocal_parameter_declaration, fnid);
    m_class->addNamedObject(name, fnid, nullptr);

    param_assignment = fC->Sibling(param_assignment);
  }
  return true;
}

bool CompileClass::compile_class_type_(const FileContent* fC, NodeId id) {
  UHDM::Serializer& s = m_compileDesign->getSerializer();
  NodeId parent = fC->Parent(id);
  VObjectType ptype = fC->Type(parent);
  if (ptype != VObjectType::paClass_declaration) return true;
  NodeId base_class_id = fC->Child(id);
  std::string base_class_name(fC->SymName(base_class_id));
  while (fC->Sibling(base_class_id) &&
         (fC->Type(fC->Sibling(base_class_id)) == VObjectType::slStringConst)) {
    base_class_id = fC->Sibling(base_class_id);
    base_class_name.append("::").append(fC->SymName(base_class_id));
  }
  // Insert base class placeholder
  // Will be bound in UVMElaboration step
  ClassDefinition* base_class = new ClassDefinition(
      base_class_name, m_class->getLibrary(), m_class->getContainer(), fC,
      base_class_id, nullptr, s.MakeClass_defn());
  m_class->insertBaseClass(base_class);

  return true;
}

bool CompileClass::compile_class_parameters_(const FileContent* fC, NodeId id) {
  /*
  n<all_c> u<1> t<StringConst> p<16> s<14> l<3>
  n<uvm_port_base> u<2> t<StringConst> p<12> s<8> l<7>
  n<IF> u<3> t<StringConst> p<6> s<5> l<7>
  n<uvm_void> u<4> t<StringConst> p<5> l<7>
  n<> u<5> t<Data_type> p<6> c<4> l<7>
  n<> u<6> t<List_of_type_assignments> p<7> c<3> l<7>
  n<> u<7> t<Parameter_port_declaration> p<8> c<6> l<7>
  n<> u<8> t<Parameter_port_list> p<12> c<7> s<10> l<7>
  n<IF> u<9> t<StringConst> p<10> l<7>
  n<> u<10> t<Class_type> p<12> c<9> s<11> l<7>
  n<> u<11> t<Endclass> p<12> l<8>
  n<> u<12> t<Class_declaration> p<13> c<2> l<7>

  or

  n<T1> u<3> t<StringConst> p<9> s<5> l<18>
  n<> u<4> t<IntegerAtomType_Int> p<5> l<18>
  n<> u<5> t<Data_type> p<9> c<4> s<6> l<18>
  n<T2> u<6> t<StringConst> p<9> s<8> l<18>
  n<T1> u<7> t<StringConst> p<8> l<18>
  n<> u<8> t<Data_type> p<9> c<7> l<18>
  n<> u<9> t<List_of_type_assignments> p<10> c<3> l<18>
  n<> u<10> t<Parameter_port_declaration> p<11> c<9> l<18>
  n<> u<11> t<Parameter_port_list> p<31> c<10> s<20> l<18>

  */
  UHDM::class_defn* defn = m_class->getUhdmDefinition();

  if (fC->sl_collect(id, VObjectType::paVIRTUAL)) {
    defn->VpiVirtual(true);
  }

  NodeId paramList = fC->sl_collect(id, VObjectType::paParameter_port_list);
  if (paramList) {
    NodeId parameter_port_declaration = fC->Child(paramList);
    while (parameter_port_declaration) {
      NodeId list_of_type_assignments = fC->Child(parameter_port_declaration);
      NodeId type = fC->Child(list_of_type_assignments);
      if (fC->Type(list_of_type_assignments) ==
              VObjectType::paList_of_type_assignments ||
          fC->Type(list_of_type_assignments) == VObjectType::paTYPE) {
        // Type param
        m_helper.compileParameterDeclaration(
            m_class, fC, list_of_type_assignments, m_compileDesign, Reduce::No,
            false, nullptr, false, false);
      } else if (fC->Type(type) == VObjectType::paTYPE) {
        // Handled in compile_parameter_declaration_
      } else {
        // Regular param
        m_helper.compileParameterDeclaration(
            m_class, fC, parameter_port_declaration, m_compileDesign,
            Reduce::No, false, nullptr, false, false);
      }
      parameter_port_declaration = fC->Sibling(parameter_port_declaration);
    }
  }
  return true;
}

}  // namespace SURELOG
