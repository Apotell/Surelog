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
#include "Surelog/Common/Session.h"
#include "Surelog/Common/SymbolId.h"
#include "Surelog/Design/FileCNodeId.h"
#include "Surelog/Design/FileContent.h"
#include "Surelog/Design/Signal.h"
#include "Surelog/Design/VObject.h"
#include "Surelog/DesignCompile/CompileDesign.h"
#include "Surelog/DesignCompile/CompileHelper.h"
#include "Surelog/ErrorReporting/Error.h"
#include "Surelog/ErrorReporting/ErrorContainer.h"
#include "Surelog/ErrorReporting/ErrorDefinition.h"
#include "Surelog/ErrorReporting/Location.h"
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
#include <uhdm/Utils.h>
#include <uhdm/uhdm.h>

#include <cstdint>
#include <stack>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace SURELOG {
int32_t FunctorCompileClass::operator()() const {
  CompileClass* instance =
      new CompileClass(m_session, m_compileDesign, m_class, m_design);
  instance->compile();
  delete instance;
  return true;
}

bool CompileClass::compile() {
  if (m_class->m_fileContents.empty()) return true;

  const FileContent* fC = m_class->m_fileContents[0];
  if (fC == nullptr) return true;
  NodeId nodeId = m_class->m_nodeIds[0];

  std::vector<std::string_view> names;
  ClassDefinition* parent = m_class;
  DesignComponent* container = nullptr;
  while (parent != nullptr) {
    names.emplace_back(parent->getName());
    container = parent->getContainer();
    parent = parent->m_parent;
  }

  std::string fullName;
  if (container != nullptr) {
    fullName.append(container->getName()).append("::");
  }

  fullName.append(names.back());
  names.pop_back();
  while (!names.empty()) {
    fullName.append("::").append(names.back());
    names.pop_back();
  }

  uhdm::ClassDefn* const defn = m_class->getUhdmModel<uhdm::ClassDefn>();
  const uhdm::ScopedScope scopedScope(defn);

  if (defn->getFullName().empty()) defn->setFullName(fullName);
  SymbolTable* const symbols = m_session->getSymbolTable();
  CommandLineParser* const clp = m_session->getCommandLineParser();

  if (ErrorContainer* errors = new ErrorContainer(m_session)) {
    Location loc(fC->getFileId(nodeId), fC->Line(nodeId), fC->Column(nodeId),
                 symbols->registerSymbol(fullName));
    Error err1(ErrorDefinition::COMP_COMPILE_CLASS, loc);
    errors->printMessage(err1, clp->muteStdout());
    delete errors;
  }
  if (fC->getSize() == 0) return true;

  NodeId classId = m_class->m_nodeIds[0];

  do {
    classId = fC->Parent(classId);
  } while (classId && !((fC->Type(classId) == VObjectType::paDescription) ||
                        (fC->Type(classId) == VObjectType::paClass_item)));
  if (classId) {
    classId = fC->Child(classId);
    if (fC->Type(classId) == VObjectType::paAttribute_instance) {
      if (uhdm::AttributeCollection* attributes =
              m_helper.compileAttributes(m_class, fC, classId, defn)) {
        m_class->setAttributes(attributes);
      }
    }
  }

  // Package imports
  std::vector<FileCNodeId> pack_imports;
  // - Local file imports
  for (auto& import : fC->getObjects(VObjectType::paPackage_import_item)) {
    pack_imports.emplace_back(import);
  }
  // - Parent container imports
  if (DesignComponent* container = m_class->getContainer()) {
    // FileCNodeId itself(container->getFileContents ()[0],
    // container->getNodeIds ()[0]); pack_imports.emplace_back(itself);
    for (auto& import :
         container->getObjects(VObjectType::paPackage_import_item)) {
      pack_imports.emplace_back(import);
    }
  }

  for (const auto& pack_import : pack_imports) {
    const FileContent* pack_fC = pack_import.fC;
    NodeId pack_id = pack_import.nodeId;
    m_helper.importPackage(m_class, m_design, pack_fC, pack_id);
  }

  if (fC->sl_collect(nodeId, VObjectType::VIRTUAL)) {
    defn->setVirtual(true);
  }

  // This
  DataType* thisdt =
      new DataType(fC, nodeId, "this", VObjectType::paClass_declaration);
  thisdt->setDefinition(m_class);
  Property* prop = new Property(thisdt, fC, nodeId, InvalidNodeId, "this",
                                false, false, false, false, false);
  m_class->insertProperty(prop);

  std::stack<NodeId> stack;
  stack.emplace(nodeId);
  bool inFunction_body_declaration = false;
  bool inTask_body_declaration = false;
  while (!stack.empty()) {
    bool skipChildren = false;
    const NodeId id = stack.top();
    stack.pop();

    VObjectType type = fC->Type(id);
    switch (type) {
      case VObjectType::paPackage_import_item: {
        m_helper.importPackage(m_class, m_design, fC, id);
        m_helper.compileImportDeclaration(m_class, fC, id);
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
      case VObjectType::ENDFUNCTION: {
        inFunction_body_declaration = false;
        break;
      }
      case VObjectType::paTask_body_declaration: {
        inTask_body_declaration = true;
        break;
      }
      case VObjectType::ENDTASK: {
        inTask_body_declaration = false;
        break;
      }
      case VObjectType::paClass_property:
        if (inFunction_body_declaration || inTask_body_declaration) break;
        compileProperty(fC, id);
        break;
      case VObjectType::paClass_constraint:
        compileConstraint(fC, id);
        break;
      case VObjectType::paClass_declaration:
        if (id != nodeId) {
          compileDeclaration(fC, id);
          if (const NodeId siblingId = fC->Sibling(id)) {
            stack.emplace(siblingId);
          }
          continue;
        }
        break;
      case VObjectType::paCovergroup_declaration:
        compileCovergroupDeclaration(fC, id);
        break;
      case VObjectType::paParameter_port_list:
        compileParameterPortList(fC, id);
        skipChildren = true;
        break;
      case VObjectType::paParameter_declaration:
        compileParameterDeclaration(fC, id);
        skipChildren = true;
        break;
      case VObjectType::paLocal_parameter_declaration:
        compileLocalParameterDeclaration(fC, id);
        skipChildren = true;
        break;
      case VObjectType::paClass_method:
        compileMethod(fC, id);
        skipChildren = true;
        break;
      case VObjectType::EXTENDS:
        compileExtends(fC, id);
        skipChildren = true;
        break;
      case VObjectType::paLet_declaration: {
        m_helper.compileLetDeclaration(m_class, fC, id);
        break;
      }
      case VObjectType::STRING_CONST: {
        if (NodeId siblingId = fC->Sibling(id)) break;
        if (fC->Type(fC->Parent(id)) != VObjectType::paClass_declaration) break;

        const std::string_view endLabel = fC->SymName(id);
        m_class->setEndLabel(endLabel);
        std::string_view moduleName =
            StringUtils::ltrim_until(m_class->getName(), '@');
        if (endLabel != moduleName) {
          Location loc(fC->getFileId(m_class->getNodeIds()[0]),
                       fC->Line(m_class->getNodeIds()[0]),
                       fC->Column(m_class->getNodeIds()[0]),
                       symbols->registerSymbol(moduleName));
          Location loc2(fC->getFileId(id), fC->Line(id), fC->Column(id),
                        symbols->registerSymbol(endLabel));
          Error err(ErrorDefinition::COMP_UNMATCHED_LABEL, loc, loc2);
          m_session->getErrorContainer()->addError(err);
        }
        break;
      }
      default:
        break;
    }

    if (const NodeId siblingId = fC->Sibling(id)) stack.emplace(siblingId);
    if (!skipChildren) {
      if (const NodeId childId = fC->Child(id)) stack.emplace(childId);
    }
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

  compile_properties();
  return true;
}

bool CompileClass::compile_properties() {
  uhdm::ClassDefn* defn = m_class->getUhdmModel<uhdm::ClassDefn>();
  const uhdm::ScopedScope scopedScope(defn);
  for (Signal* sig : m_class->getSignals()) {
    const FileContent* fC = sig->getFileContent();
    NodeId id = sig->getNodeId();
    // Assignment to a default value
    uhdm::Expr* exp =
        m_helper.exprFromAssign(m_class, fC, id, sig->getUnpackedDimension());
    if (uhdm::Any* obj = m_helper.compileSignals(m_class, sig)) {
      fC->populateCoreMembers(sig->getNameId(), sig->getNameId(), obj);
      obj->setParent(defn);
      if (exp != nullptr) {
        exp->setParent(obj, true);
        if (uhdm::Variable* const var = any_cast<uhdm::Variable>(obj)) {
          var->setExpr(exp);
        }
      }
    }
  }
  return true;
}

bool CompileClass::compileProperty(const FileContent* fC, NodeId id) {
  SymbolTable* const symbols = m_session->getSymbolTable();
  ErrorContainer* const errors = m_session->getErrorContainer();

  NodeId data_declaration = fC->Child(id);
  m_helper.compileDataDeclaration(m_class, fC, data_declaration, false,
                                  m_attributes);

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
      if (the_type == VObjectType::STRING_CONST) {
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
                        symbols->registerSymbol(varName));
          const FileContent* prevFile = previous->getFileContent();
          NodeId prevNode = previous->getNodeId();
          Location loc2(prevFile->getFileId(prevNode), prevFile->Line(prevNode),
                        prevFile->Column(prevNode),
                        symbols->registerSymbol(varName));
          Error err(ErrorDefinition::COMP_MULTIPLY_DEFINED_PROPERTY, loc1,
                    loc2);
          errors->addError(err);
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

bool CompileClass::compileMethod(const FileContent* fC, NodeId id) {
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
  SymbolTable* const symbols = m_session->getSymbolTable();
  ErrorContainer* const errors = m_session->getErrorContainer();

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
    if (the_type == VObjectType::VIRTUAL) {
      type = fC->Sibling(type);
      the_type = fC->Type(type);
    }
    std::string typeName;
    if (the_type == VObjectType::STRING_CONST) {
      typeName = fC->SymName(type);
    } else {
      typeName = VObject::getTypeName(the_type);
    }
    returnType->init(fC, type, typeName, fC->Type(type));
    NodeId function_name = fC->Sibling(function_data_type_or_implicit);
    if (function_name) {
      funcName = fC->SymName(function_name);
      if (m_builtins.find(funcName) != m_builtins.end()) {
        Location loc(fC->getFileId(), fC->Line(function_name),
                     fC->Column(function_name),
                     symbols->registerSymbol(funcName));
        Error err(ErrorDefinition::COMP_CANNOT_REDEFINE_BUILTIN_METHOD, loc);
        errors->addError(err);
      }
    }
    m_helper.compileFunction(m_class, fC, id, nullptr, true);
    m_helper.compileFunction(m_class, fC, id, nullptr, true);

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
    if (fC->Type(task_name) == VObjectType::STRING_CONST)
      taskName = fC->SymName(task_name);
    else if (fC->Type(task_name) == VObjectType::paClass_scope) {
      NodeId Class_type = fC->Child(task_name);
      taskName.assign(fC->SymName(fC->Child(Class_type)))
          .append("::")
          .append(fC->SymName(fC->Sibling(task_name)));
    }

    m_helper.compileTask(m_class, fC, id, nullptr, true);
    m_helper.compileTask(m_class, fC, id, nullptr, true);

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
    uhdm::Serializer& s = m_compileDesign->getSerializer();
    if (fC->Type(func_prototype) == VObjectType::paTask_prototype) {
      NodeId task_decl =
          m_helper.setFuncTaskQualifiers(fC, fC->Child(id), nullptr);
      NodeId Task_body_declaration;
      if (fC->Type(task_decl) == VObjectType::paTask_body_declaration)
        Task_body_declaration = task_decl;
      else
        Task_body_declaration = fC->Child(task_decl);
      NodeId task_name = fC->Child(Task_body_declaration);
      if (fC->Type(task_name) == VObjectType::STRING_CONST)
        taskName = fC->SymName(task_name);
      else if (fC->Type(task_name) == VObjectType::paClass_scope) {
        NodeId Class_type = fC->Child(task_name);
        taskName.assign(fC->SymName(fC->Child(Class_type)))
            .append("::")
            .append(fC->SymName(fC->Sibling(task_name)));
      }

      m_helper.compileTask(m_class, fC, id, nullptr, true);
      m_helper.compileTask(m_class, fC, id, nullptr, true);

      std::vector<uhdm::TaskFuncDecl*>* task_func_decls =
          m_class->getTaskFuncDecls();
      if (task_func_decls == nullptr) {
        m_class->setTaskFuncDecls(s.makeCollection<uhdm::TaskFuncDecl>());
        task_func_decls = m_class->getTaskFuncDecls();
      }

      uhdm::TaskDecl* td = nullptr;
      for (uhdm::TaskFuncDecl* tfd : *m_class->getTaskFuncDecls()) {
        if (tfd->getName() == taskName) {
          td = any_cast<uhdm::TaskDecl>(tfd);
          break;
        }
      }

      if (td == nullptr) {
        td = s.make<uhdm::TaskDecl>();
        td->setName(taskName);
        td->setParent(m_class->getUhdmModel());
        fC->populateCoreMembers(id, id, td);
        task_func_decls->emplace_back(td);
      }

      for (uhdm::TaskFunc* tf : *m_class->getTaskFuncs()) {
        if (tf->getName() == taskName) {
          td->setTaskFunc(tf);
          break;
        }
      }
    } else {
      NodeId function_data_type = fC->Child(func_prototype);
      NodeId data_type = fC->Child(function_data_type);
      NodeId type = fC->Child(data_type);
      VObjectType the_type = fC->Type(type);
      std::string typeName;
      if (the_type == VObjectType::STRING_CONST) {
        typeName = fC->SymName(type);
      } else {
        typeName = VObject::getTypeName(the_type);
      }
      returnType->init(fC, type, typeName, fC->Type(type));
      NodeId function_name = fC->Sibling(function_data_type);
      funcName = fC->SymName(function_name);

      m_helper.compileFunction(m_class, fC, id, nullptr, true);
      m_helper.compileFunction(m_class, fC, id, nullptr, true);

      std::vector<uhdm::TaskFuncDecl*>* task_func_decls =
          m_class->getTaskFuncDecls();
      if (task_func_decls == nullptr) {
        m_class->setTaskFuncDecls(s.makeCollection<uhdm::TaskFuncDecl>());
        task_func_decls = m_class->getTaskFuncDecls();
      }

      uhdm::FunctionDecl* fd = nullptr;
      for (uhdm::TaskFuncDecl* tfd : *m_class->getTaskFuncDecls()) {
        if (tfd->getName() == funcName) {
          fd = any_cast<uhdm::FunctionDecl>(tfd);
          break;
        }
      }

      if (fd == nullptr) {
        fd = s.make<uhdm::FunctionDecl>();
        fd->setName(funcName);
        fd->setParent(m_class->getUhdmModel());
        fC->populateCoreMembers(id, id, fd);
        task_func_decls->emplace_back(fd);
      }

      for (uhdm::TaskFunc* tf : *m_class->getTaskFuncs()) {
        if (tf->getName() == funcName) {
          fd->setTaskFunc(tf);
          break;
        }
      }
    }
    is_extern = true;
  } else if (func_type == VObjectType::paClass_constructor_declaration) {
    funcName = "new";
    returnType->init(fC, InvalidNodeId, "void", VObjectType::NO_TYPE);

    m_helper.compileClassConstructorDeclaration(m_class, fC, fC->Child(id));

  } else if (func_type == VObjectType::paClass_constructor_prototype) {
    funcName = "new";

    m_helper.compileFunction(m_class, fC, id, nullptr, true);
    m_helper.compileFunction(m_class, fC, id, nullptr, true);
  } else {
    funcName = "UNRECOGNIZED_METHOD_TYPE";
  }
  if (!taskName.empty()) {
    TaskMethod* method = new TaskMethod(m_class, fC, id, taskName, is_extern);
    method->compile(m_helper);
    TaskMethod* prevDef = m_class->getTask(taskName);
    if (prevDef) {
      Location loc1(fC->getFileId(id), fC->Line(id), fC->Column(id),
                    symbols->registerSymbol(taskName));
      const FileContent* prevFile = prevDef->getFileContent();
      NodeId prevNode = prevDef->getNodeId();
      Location loc2(prevFile->getFileId(prevNode), prevFile->Line(prevNode),
                    prevFile->Column(prevNode),
                    symbols->registerSymbol(taskName));
      Error err(ErrorDefinition::COMP_MULTIPLY_DEFINED_TASK, loc1, loc2);
      errors->addError(err);
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
      SymbolId funcSymbol = symbols->registerSymbol(funcName);
      Location loc1(fC->getFileId(id), fC->Line(id), fC->Column(id),
                    funcSymbol);
      const FileContent* prevFile = prevDef->getFileContent();
      NodeId prevNode = prevDef->getNodeId();
      Location loc2(prevFile->getFileId(prevNode), prevFile->Line(prevNode),
                    prevFile->Column(prevNode), funcSymbol);
      if (funcSymbol) {
        Error err(ErrorDefinition::COMP_MULTIPLY_DEFINED_FUNCTION, loc1, loc2);
        errors->addError(err);
      }
    }
    m_class->insertFunction(method);
  }
  return true;
}

bool CompileClass::compileConstraint(const FileContent* fC,
                                     NodeId class_constraint) {
  SymbolTable* const symbols = m_session->getSymbolTable();
  ErrorContainer* const errors = m_session->getErrorContainer();

  NodeId constraint_prototype = fC->Child(class_constraint);
  NodeId constraint_name = fC->Child(constraint_prototype);
  const std::string_view constName = fC->SymName(constraint_name);
  Constraint* prevDef = m_class->getConstraint(constName);
  if (prevDef) {
    Location loc1(fC->getFileId(class_constraint), fC->Line(class_constraint),
                  fC->Column(class_constraint),
                  symbols->registerSymbol(constName));
    const FileContent* prevFile = prevDef->getFileContent();
    NodeId prevNode = prevDef->getNodeId();
    Location loc2(prevFile->getFileId(prevNode), prevFile->Line(prevNode),
                  prevFile->Column(prevNode),
                  symbols->registerSymbol(constName));
    Error err(ErrorDefinition::COMP_MULTIPLY_DEFINED_CONSTRAINT, loc1, loc2);
    errors->addError(err);
  }
  Constraint* constraint = new Constraint(fC, class_constraint, constName);
  m_class->insertConstraint(constraint);

  uhdm::ClassDefn* const pscope = m_class->getUhdmModel<uhdm::ClassDefn>();
  m_helper.compileConstraintBlock(m_class, fC, class_constraint, pscope);
  return true;
}

bool CompileClass::compileDeclaration(const FileContent* fC, NodeId id) {
  SymbolTable* const symbols = m_session->getSymbolTable();
  ErrorContainer* const errors = m_session->getErrorContainer();

  uhdm::Serializer& s = m_compileDesign->getSerializer();
  const bool virtualClass = fC->sl_collect(id, VObjectType::VIRTUAL);
  const NodeId class_name_id = fC->sl_collect(id, VObjectType::STRING_CONST);
  const std::string_view class_name = fC->SymName(class_name_id);
  std::string full_class_name =
      StrCat(m_class->getUhdmModel<uhdm::ClassDefn>()->getFullName(),
             "::", class_name);
  ClassDefinition* prevDef = m_class->getClass(class_name);
  if (prevDef) {
    Location loc1(fC->getFileId(class_name_id), fC->Line(class_name_id),
                  fC->Column(class_name_id),
                  symbols->registerSymbol(class_name));
    const FileContent* prevFile = prevDef->getFileContent();
    NodeId prevNode =
        prevFile->sl_collect(prevDef->getNodeId(), VObjectType::STRING_CONST);
    Location loc2(prevFile->getFileId(prevNode), prevFile->Line(prevNode),
                  prevFile->Column(prevNode),
                  symbols->registerSymbol(class_name));
    Error err(ErrorDefinition::COMP_MULTIPLY_DEFINED_INNER_CLASS, loc1, loc2);
    errors->addError(err);
  }
  ClassDefinition* the_class =
      new ClassDefinition(m_session, class_name, m_class->getLibrary(),
                          m_class->getContainer(), fC, id, m_class, s);
  uhdm::ClassDefn* defn = the_class->getUhdmModel<uhdm::ClassDefn>();
  defn->setVirtual(virtualClass);
  defn->setFullName(full_class_name);
  m_class->insertClass(the_class);
  uhdm::ClassDefn* parent = m_class->getUhdmModel<uhdm::ClassDefn>();
  defn->setParent(parent);

  FunctorCompileClass(m_session, m_compileDesign, the_class, m_design)();
  return true;
}

bool CompileClass::compileCovergroupDeclaration(const FileContent* fC,
                                                NodeId id) {
  SymbolTable* const symbols = m_session->getSymbolTable();
  ErrorContainer* const errors = m_session->getErrorContainer();

  NodeId covergroup_name = fC->Child(id);
  const std::string_view covergroupName = fC->SymName(covergroup_name);
  CoverGroupDefinition* prevDef = m_class->getCoverGroup(covergroupName);
  if (prevDef) {
    Location loc1(fC->getFileId(covergroup_name), fC->Line(covergroup_name),
                  fC->Column(covergroup_name),
                  symbols->registerSymbol(covergroupName));
    const FileContent* prevFile = prevDef->getFileContent();
    NodeId prevNode = prevDef->getNodeId();
    Location loc2(prevFile->getFileId(prevNode), prevFile->Line(prevNode),
                  prevFile->Column(prevNode),
                  symbols->registerSymbol(covergroupName));
    Error err(ErrorDefinition::COMP_MULTIPLY_DEFINED_COVERGROUP, loc1, loc2);
    errors->addError(err);
  }
  CoverGroupDefinition* covergroup =
      new CoverGroupDefinition(fC, id, covergroupName);
  m_class->insertCoverGroup(covergroup);

  return true;
}

bool CompileClass::compileLocalParameterDeclaration(const FileContent* fC,
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
  SymbolTable* const symbols = m_session->getSymbolTable();
  ErrorContainer* const errors = m_session->getErrorContainer();

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
                    symbols->registerSymbol(name));
      const FileContent* prevFile = prevDef->first.fC;
      NodeId prevNode = prevDef->first.nodeId;
      Location loc2(prevFile->getFileId(prevNode), prevFile->Line(prevNode),
                    prevFile->Column(prevNode), symbols->registerSymbol(name));
      Error err(ErrorDefinition::COMP_MULTIPLY_DEFINED_PARAMETER, loc1, loc2);
      errors->addError(err);
    }

    FileCNodeId fnid(fC, id);
    m_class->addObject(VObjectType::paLocal_parameter_declaration, fnid);
    m_class->addNamedObject(name, fnid, nullptr);

    param_assignment = fC->Sibling(param_assignment);
  }
  return m_helper.compileParameterDeclaration(m_class, fC, id, nullptr, false,
                                              false);
}

bool CompileClass::compileParameterDeclaration(const FileContent* fC,
                                               NodeId id) {
  SymbolTable* const symbols = m_session->getSymbolTable();
  ErrorContainer* const errors = m_session->getErrorContainer();

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
                    symbols->registerSymbol(name));
      const FileContent* prevFile = prevDef->first.fC;
      NodeId prevNode = prevDef->first.nodeId;
      Location loc2(prevFile->getFileId(prevNode), prevFile->Line(prevNode),
                    prevFile->Column(prevNode), symbols->registerSymbol(name));
      Error err(ErrorDefinition::COMP_MULTIPLY_DEFINED_PARAMETER, loc1, loc2);
      errors->addError(err);
    }

    FileCNodeId fnid(fC, id);
    m_class->addObject(VObjectType::paParameter_declaration, fnid);
    m_class->addNamedObject(name, fnid, nullptr);

    param_assignment = fC->Sibling(param_assignment);
  }

  return m_helper.compileParameterDeclaration(m_class, fC, id, nullptr, false,
                                              false);
}

bool CompileClass::compileExtends(const FileContent* fC, NodeId id) {
  if (fC->Type(id) != VObjectType::EXTENDS) return false;

  uhdm::Serializer& s = m_compileDesign->getSerializer();

  const NodeId Class_type = fC->Sibling(id);
  NodeId String_const = fC->Child(Class_type);
  std::string baseClassName(fC->SymName(String_const));
  String_const = fC->Sibling(String_const);
  while (String_const &&
         (fC->Type(String_const) == VObjectType::STRING_CONST)) {
    baseClassName.append("::").append(fC->SymName(String_const));
    String_const = fC->Sibling(String_const);
  }

  uhdm::Extends* const extends = s.make<uhdm::Extends>();
  extends->setParent(m_class->getUhdmModel());
  fC->populateCoreMembers(Class_type, Class_type, extends);
  m_class->getUhdmModel<uhdm::ClassDefn>()->setExtends(extends);

  uhdm::RefTypespec* rt = s.make<uhdm::RefTypespec>();
  rt->setParent(extends);
  rt->setName(baseClassName);
  fC->populateCoreMembers(Class_type, Class_type, rt);
  extends->setClassTypespec(rt);

  if (uhdm::UnsupportedTypespec* const ts = m_helper.compileUnsupportedTypespec(
          m_class, fC, Class_type, extends, baseClassName)) {
    rt->setActual(ts);
  }

  return true;
}

bool CompileClass::compileParameterPortList(const FileContent* fC, NodeId id) {
  if (const NodeId Parameter_port_list =
          fC->sl_collect(id, VObjectType::paParameter_port_list)) {
    return m_helper.compileParameterPortList(m_class, fC, Parameter_port_list,
                                             nullptr, false);
  }

  return true;
}

}  // namespace SURELOG
