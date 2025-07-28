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
 * File:   ElaborationStep.cpp
 * Author: alain
 *
 * Created on July 12, 2017, 8:55 PM
 */

#include "Surelog/DesignCompile/ElaborationStep.h"

#include <uhdm/BaseClass.h>
#include <uhdm/expr.h>
#include <uhdm/uhdm_types.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <queue>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Surelog/CommandLine/CommandLineParser.h"
#include "Surelog/Common/Containers.h"
#include "Surelog/Common/FileSystem.h"
#include "Surelog/Common/NodeId.h"
#include "Surelog/Common/Session.h"
#include "Surelog/Design/DataType.h"
#include "Surelog/Design/DummyType.h"
#include "Surelog/Design/Enum.h"
#include "Surelog/Design/FileCNodeId.h"
#include "Surelog/Design/FileContent.h"
#include "Surelog/Design/Function.h"
#include "Surelog/Design/ModuleDefinition.h"
#include "Surelog/Design/ModuleInstance.h"
#include "Surelog/Design/Netlist.h"
#include "Surelog/Design/Parameter.h"
#include "Surelog/Design/SimpleType.h"
#include "Surelog/Design/Struct.h"
#include "Surelog/Design/TfPortItem.h"
#include "Surelog/Design/Union.h"
#include "Surelog/Design/ValuedComponentI.h"
#include "Surelog/DesignCompile/CompileDesign.h"
#include "Surelog/DesignCompile/CompileHelper.h"
#include "Surelog/ErrorReporting/Error.h"
#include "Surelog/ErrorReporting/ErrorDefinition.h"
#include "Surelog/ErrorReporting/Location.h"
#include "Surelog/Library/Library.h"
#include "Surelog/Package/Package.h"
#include "Surelog/SourceCompile/Compiler.h"
#include "Surelog/SourceCompile/SymbolTable.h"
#include "Surelog/SourceCompile/VObjectTypes.h"
#include "Surelog/Testbench/ClassDefinition.h"
#include "Surelog/Testbench/Program.h"
#include "Surelog/Testbench/Property.h"
#include "Surelog/Testbench/TypeDef.h"
#include "Surelog/Utils/StringUtils.h"

// UHDM
#include <uhdm/ElaboratorListener.h>
#include <uhdm/ExprEval.h>
#include <uhdm/clone_tree.h>
#include <uhdm/sv_vpi_user.h>
#include <uhdm/uhdm.h>
#include <uhdm/vpi_visitor.h>

namespace SURELOG {

ElaborationStep::ElaborationStep(Session* session, CompileDesign* compileDesign)
    : m_session(session),
      m_compileDesign(compileDesign),
      m_exprBuilder(session),
      m_helper(session) {
  m_exprBuilder.setDesign(m_compileDesign->getCompiler()->getDesign());
}

bool ElaborationStep::bindTypedefs_() {
  Compiler* const compiler = m_compileDesign->getCompiler();
  SymbolTable* const symbols = m_session->getSymbolTable();
  FileSystem* const fileSystem = m_session->getFileSystem();
  ErrorContainer* const errors = m_session->getErrorContainer();
  Design* design = compiler->getDesign();
  uhdm::Serializer& s = m_compileDesign->getSerializer();
  std::vector<std::pair<TypeDef*, DesignComponent*>> defs;
  std::map<std::string, uhdm::Typespec*, std::less<>> specs;
  for (const auto& file : design->getAllFileContents()) {
    FileContent* fC = file.second;
    for (const auto& typed : fC->getTypeDefMap()) {
      TypeDef* typd = typed.second;
      defs.emplace_back(typd, fC);
    }
  }

  for (const auto& package : design->getPackageDefinitions()) {
    Package* pack = package.second;
    for (const auto& typed : pack->getTypeDefMap()) {
      TypeDef* typd = typed.second;
      defs.emplace_back(typd, pack);
    }
  }

  for (const auto& module : design->getModuleDefinitions()) {
    ModuleDefinition* mod = module.second;
    for (const auto& typed : mod->getTypeDefMap()) {
      TypeDef* typd = typed.second;
      defs.emplace_back(typd, mod);
    }
    mod = mod->getUnelabMmodule();
    for (const auto& typed : mod->getTypeDefMap()) {
      TypeDef* typd = typed.second;
      defs.emplace_back(typd, mod);
    }
  }

  for (const auto& program_def : design->getProgramDefinitions()) {
    Program* program = program_def.second;
    for (const auto& typed : program->getTypeDefMap()) {
      TypeDef* typd = typed.second;
      defs.emplace_back(typd, program);
    }
  }

  for (const auto& class_def : design->getClassDefinitions()) {
    ClassDefinition* classp = class_def.second;
    for (const auto& typed : classp->getTypeDefMap()) {
      TypeDef* typd = typed.second;
      defs.emplace_back(typd, classp);
    }
  }

  for (auto& defTuple : defs) {
    TypeDef* typd = defTuple.first;
    DesignComponent* comp = defTuple.second;
    const DataType* prevDef = typd->getDefinition();
    bool noTypespec = false;
    if (prevDef) {
      prevDef = prevDef->getActual();
      if (prevDef->getTypespec() == nullptr)
        noTypespec = true;
      else {
        specs.emplace(prevDef->getTypespec()->getName(),
                      prevDef->getTypespec());
        if (Package* pack = valuedcomponenti_cast<Package>(comp)) {
          std::string name =
              StrCat(pack->getName(), "::", prevDef->getTypespec()->getName());
          specs.emplace(name, prevDef->getTypespec());
        }
        if (ClassDefinition* pack =
                valuedcomponenti_cast<ClassDefinition*>(comp)) {
          std::string name =
              StrCat(pack->getName(), "::", prevDef->getTypespec()->getName());
          specs.emplace(name, prevDef->getTypespec());
        }
      }
    }

    if (noTypespec == true) {
      if (prevDef && prevDef->getCategory() == DataType::Category::DUMMY) {
        const DataType* def =
            bindTypeDef_(typd, comp, ErrorDefinition::NO_ERROR_MESSAGE);
        if (def && (typd != def)) {
          typd->setDefinition(def);
          typd->setDataType((DataType*)def);
          NodeId id = typd->getDefinitionNode();
          const FileContent* fC = typd->getFileContent();
          NodeId Packed_dimension = fC->Sibling(id);
          uhdm::Typespec* tpclone = nullptr;
          if (Packed_dimension &&
              fC->Type(Packed_dimension) == VObjectType::paPacked_dimension) {
            tpclone = m_helper.compileTypespec(
                defTuple.second, typd->getFileContent(),
                typd->getDefinitionNode(), InvalidNodeId, m_compileDesign,
                Reduce::Yes, nullptr, nullptr, false);
          } else if (uhdm::Typespec* tps = def->getTypespec()) {
            uhdm::ElaboratorContext elaboratorContext(&s, false, true);
            tpclone = (uhdm::Typespec*)uhdm::clone_tree((uhdm::Any*)tps,
                                                        &elaboratorContext);
            if (uhdm::TypedefTypespec* tt =
                    any_cast<uhdm::TypedefTypespec>(tpclone)) {
              uhdm::RefTypespec* rt = s.make<uhdm::RefTypespec>();
              rt->setParent(tpclone);
              rt->setActual(tps);
              tt->setTypedefAlias(rt);
            }
          }
          if (uhdm::Typespec* unpacked = prevDef->getUnpackedTypespec()) {
            uhdm::ElaboratorContext elaboratorContext(&s, false, true);
            uhdm::ArrayTypespec* unpacked_clone =
                (uhdm::ArrayTypespec*)uhdm::clone_tree((uhdm::Any*)unpacked,
                                                       &elaboratorContext);
            if (tpclone != nullptr) {
              uhdm::RefTypespec* rt = s.make<uhdm::RefTypespec>();
              rt->setParent(unpacked_clone);
              rt->setActual(tpclone);
              unpacked_clone->setElemTypespec(rt);
            }
            tpclone = unpacked_clone;
          }

          if (tpclone) {
            typd->setTypespec(tpclone);
            if (uhdm::TypedefTypespec* tt =
                    any_cast<uhdm::TypedefTypespec>(tpclone)) {
              tt->setName(typd->getName());
            }
            specs.emplace(typd->getName(), tpclone);
            if (Package* pack = valuedcomponenti_cast<Package>(comp)) {
              std::string name = StrCat(pack->getName(), "::", typd->getName());
              specs.emplace(name, tpclone);
            }
          }
        }
      }
      if (typd->getTypespec() == nullptr) {
        const FileContent* typeF = typd->getFileContent();
        NodeId typeId = typd->getDefinitionNode();
        uhdm::Typespec* ts = m_helper.compileTypespec(
            defTuple.second, typeF, typeId, InvalidNodeId, m_compileDesign,
            Reduce::Yes, nullptr, nullptr, false);
        if (ts) {
          if (uhdm::TypedefTypespec* tt = any_cast<uhdm::TypedefTypespec>(ts)) {
            tt->setName(ts->getName());
          }
          std::string name;
          if (typeF->Type(typeId) == VObjectType::STRING_CONST) {
            name = typeF->SymName(typeId);
          } else {
            name = typd->getName();
          }
          specs.emplace(typd->getName(), ts);
          if (Package* pack = valuedcomponenti_cast<Package>(comp)) {
            std::string name = StrCat(pack->getName(), "::", typd->getName());
            specs.emplace(name, ts);
          }
          if (ClassDefinition* pack =
                  valuedcomponenti_cast<ClassDefinition>(comp)) {
            std::string name = StrCat(pack->getName(), "::", typd->getName());
            specs.emplace(name, ts);
          }
          if (ts->getUhdmType() == uhdm::UhdmType::UnsupportedTypespec) {
            Location loc1(fileSystem->toPathId(ts->getFile(), symbols),
                          ts->getStartLine(), ts->getStartColumn(),
                          symbols->registerSymbol(name));
            Error err1(ErrorDefinition::COMP_UNDEFINED_TYPE, loc1);
            errors->addError(err1);
          }
        }
        typd->setTypespec(ts);
        if (DataType* dt = (DataType*)typd->getDataType()) {
          if (dt->getTypespec() == nullptr) {
            dt->setTypespec(ts);
          }
        }
      }
    } else if (prevDef == nullptr) {
      const DataType* def =
          bindTypeDef_(typd, comp, ErrorDefinition::NO_ERROR_MESSAGE);
      if (def && (typd != def)) {
        typd->setDefinition(def);
        typd->setDataType((DataType*)def);
        typd->setTypespec(nullptr);
        uhdm::Typespec* ts = m_helper.compileTypespec(
            defTuple.second, typd->getFileContent(), typd->getDefinitionNode(),
            InvalidNodeId, m_compileDesign, Reduce::Yes, nullptr, nullptr,
            false);
        if (ts) {
          specs.emplace(typd->getName(), ts);
          if (uhdm::TypedefTypespec* tt = any_cast<uhdm::TypedefTypespec>(ts)) {
            tt->setName(typd->getName());
          }
          if (Package* pack = valuedcomponenti_cast<Package>(comp)) {
            std::string name = StrCat(pack->getName(), "::", typd->getName());
            specs.emplace(name, ts);
          }
        }
        typd->setTypespec(ts);
      } else {
        if (prevDef == nullptr) {
          const FileContent* fC = typd->getFileContent();
          NodeId id = typd->getNodeId();
          std::string definition_string;
          NodeId defNode = typd->getDefinitionNode();
          VObjectType defType = fC->Type(defNode);
          if (defType == VObjectType::STRING_CONST) {
            definition_string = fC->SymName(defNode);
          }
          Location loc1(fC->getFileId(id), fC->Line(id), fC->Column(id),
                        symbols->registerSymbol(definition_string));
          Error err1(ErrorDefinition::COMP_UNDEFINED_TYPE, loc1);
          errors->addError(err1);
        }
      }
    }
  }

  swapTypespecPointersInUhdm(s, m_compileDesign->getSwapedObjects());
  swapTypespecPointersInTypedef(design, m_compileDesign->getSwapedObjects());

  return true;
}

static uhdm::Typespec* replace(
    const uhdm::Typespec* orig,
    std::map<const uhdm::Typespec*, const uhdm::Typespec*>& typespecSwapMap) {
  if (orig && (orig->getUhdmType() == uhdm::UhdmType::UnsupportedTypespec)) {
    auto itr = typespecSwapMap.find(orig);
    if (itr != typespecSwapMap.end()) {
      return const_cast<uhdm::Typespec*>(itr->second);
    }
  }
  return const_cast<uhdm::Typespec*>(orig);
}

static void replace(
    const uhdm::RefTypespec* orig_rt,
    std::map<const uhdm::Typespec*, const uhdm::Typespec*>& typespecSwapMap) {
  if (orig_rt) {
    if (const uhdm::UnsupportedTypespec* orig_ut =
            orig_rt->getActual<uhdm::UnsupportedTypespec>()) {
      auto itr = typespecSwapMap.find(orig_ut);
      if (itr != typespecSwapMap.end()) {
        const_cast<uhdm::RefTypespec*>(orig_rt)->setActual(
            const_cast<uhdm::Typespec*>(itr->second));
      }
    }
  }
}

void ElaborationStep::swapTypespecPointersInTypedef(
    Design* design,
    std::map<const uhdm::Typespec*, const uhdm::Typespec*>& typespecSwapMap) {
  for (const auto& file : design->getAllFileContents()) {
    FileContent* fC = file.second;
    for (const auto& typed : fC->getDataTypeMap()) {
      const DataType* dt = typed.second;
      while (dt) {
        ((DataType*)dt)
            ->setTypespec(replace(dt->getTypespec(), typespecSwapMap));
        dt = dt->getDefinition();
      }
    }
    for (const auto& typed : fC->getTypeDefMap()) {
      TypeDef* typd = typed.second;
      const DataType* dt = typd;
      while (dt) {
        ((DataType*)dt)
            ->setTypespec(replace(dt->getTypespec(), typespecSwapMap));
        dt = dt->getDefinition();
      }
    }
  }
  for (const auto& package : design->getPackageDefinitions()) {
    Package* pack = package.second;
    for (const auto& typed : pack->getDataTypeMap()) {
      const DataType* dt = typed.second;
      while (dt) {
        ((DataType*)dt)
            ->setTypespec(replace(dt->getTypespec(), typespecSwapMap));
        dt = dt->getDefinition();
      }
    }
    for (const auto& typed : pack->getTypeDefMap()) {
      TypeDef* typd = typed.second;
      const DataType* dt = typd;
      while (dt) {
        ((DataType*)dt)
            ->setTypespec(replace(dt->getTypespec(), typespecSwapMap));
        dt = dt->getDefinition();
      }
    }
  }
  for (const auto& module : design->getModuleDefinitions()) {
    ModuleDefinition* mod = module.second;
    for (const auto& typed : mod->getDataTypeMap()) {
      const DataType* dt = typed.second;
      while (dt) {
        ((DataType*)dt)
            ->setTypespec(replace(dt->getTypespec(), typespecSwapMap));
        dt = dt->getDefinition();
      }
    }
    for (const auto& typed : mod->getTypeDefMap()) {
      TypeDef* typd = typed.second;
      const DataType* dt = typd;
      while (dt) {
        ((DataType*)dt)
            ->setTypespec(replace(dt->getTypespec(), typespecSwapMap));
        dt = dt->getDefinition();
      }
    }
  }
  for (const auto& program_def : design->getProgramDefinitions()) {
    Program* program = program_def.second;
    for (const auto& typed : program->getDataTypeMap()) {
      const DataType* dt = typed.second;
      while (dt) {
        ((DataType*)dt)
            ->setTypespec(replace(dt->getTypespec(), typespecSwapMap));
        dt = dt->getDefinition();
      }
    }
    for (const auto& typed : program->getTypeDefMap()) {
      TypeDef* typd = typed.second;
      const DataType* dt = typd;
      while (dt) {
        ((DataType*)dt)
            ->setTypespec(replace(dt->getTypespec(), typespecSwapMap));
        dt = dt->getDefinition();
      }
    }
  }

  for (const auto& class_def : design->getClassDefinitions()) {
    ClassDefinition* classp = class_def.second;
    for (const auto& typed : classp->getDataTypeMap()) {
      const DataType* dt = typed.second;
      while (dt) {
        ((DataType*)dt)
            ->setTypespec(replace(dt->getTypespec(), typespecSwapMap));
        dt = dt->getDefinition();
      }
    }
    for (const auto& typed : classp->getTypeDefMap()) {
      TypeDef* typd = typed.second;
      const DataType* dt = typd;
      while (dt) {
        ((DataType*)dt)
            ->setTypespec(replace(dt->getTypespec(), typespecSwapMap));
        dt = dt->getDefinition();
      }
    }
  }
}

void ElaborationStep::swapTypespecPointersInUhdm(
    uhdm::Serializer& s,
    std::map<const uhdm::Typespec*, const uhdm::Typespec*>& typespecSwapMap) {
  // Replace all references of obsolete typespecs
  for (auto o : s.getAllObjects()) {
    uhdm::Any* var = (uhdm::Any*)o.first;
    if (uhdm::TypespecMember* ex = any_cast<uhdm::TypespecMember>(var)) {
      replace(ex->getTypespec(), typespecSwapMap);
    } else if (uhdm::Parameter* ex = any_cast<uhdm::Parameter>(var)) {
      replace(ex->getTypespec(), typespecSwapMap);
    } else if (uhdm::TypeParameter* ex = any_cast<uhdm::TypeParameter>(var)) {
      replace(ex->getTypespec(), typespecSwapMap);
      replace(ex->getExpr(), typespecSwapMap);
    } else if (uhdm::IODecl* ex = any_cast<uhdm::IODecl>(var)) {
      replace(ex->getTypespec(), typespecSwapMap);
    } else if (uhdm::ClassTypespec* ex = any_cast<uhdm::ClassTypespec>(var)) {
      replace(ex->getExtends(), typespecSwapMap);
    } else if (uhdm::ClassDefn* ex = any_cast<uhdm::ClassDefn>(var)) {
      if (ex->getTypespecs()) {
        for (auto& tps : *ex->getTypespecs()) {
          tps = replace(tps, typespecSwapMap);
        }
      }
    } else if (uhdm::Ports* ex = any_cast<uhdm::Ports>(var)) {
      replace(ex->getTypespec(), typespecSwapMap);
    } else if (uhdm::PropFormalDecl* ex = any_cast<uhdm::PropFormalDecl>(var)) {
      replace(ex->getTypespec(), typespecSwapMap);
    } else if (uhdm::ClassObj* ex = any_cast<uhdm::ClassObj>(var)) {
      if (ex->getTypespecs()) {
        for (auto& tps : *ex->getTypespecs()) {
          tps = replace(tps, typespecSwapMap);
        }
      }
      replace(ex->getClassTypespec(), typespecSwapMap);
    } else if (uhdm::Design* ex = any_cast<uhdm::Design>(var)) {
      if (ex->getTypespecs()) {
        for (auto& tps : *ex->getTypespecs()) {
          tps = replace(tps, typespecSwapMap);
        }
      }
    } else if (uhdm::Extends* ex = any_cast<uhdm::Extends>(var)) {
      replace(ex->getClassTypespec(), typespecSwapMap);
    } else if (uhdm::LogicTypespec* ex = any_cast<uhdm::LogicTypespec>(var)) {
      replace(ex->getElemTypespec(), typespecSwapMap);
      replace(ex->getIndexTypespec(), typespecSwapMap);
    } else if (uhdm::TaggedPattern* ex = any_cast<uhdm::TaggedPattern>(var)) {
      replace(ex->getTypespec(), typespecSwapMap);
    } else if (uhdm::ArrayTypespec* ex = any_cast<uhdm::ArrayTypespec>(var)) {
      replace(ex->getElemTypespec(), typespecSwapMap);
      replace(ex->getIndexTypespec(), typespecSwapMap);
    } else if (uhdm::PackedArrayTypespec* ex =
                   any_cast<uhdm::PackedArrayTypespec>(var)) {
      replace(ex->getElemTypespec(), typespecSwapMap);
      replace(ex->getIndexTypespec(), typespecSwapMap);
    } else if (uhdm::BitTypespec* ex = any_cast<uhdm::BitTypespec>(var)) {
      replace(ex->getElemTypespec(), typespecSwapMap);
      replace(ex->getIndexTypespec(), typespecSwapMap);
    } else if (uhdm::EnumTypespec* ex = any_cast<uhdm::EnumTypespec>(var)) {
      replace(ex->getBaseTypespec(), typespecSwapMap);
    } else if (uhdm::SeqFormalDecl* ex = any_cast<uhdm::SeqFormalDecl>(var)) {
      replace(ex->getTypespec(), typespecSwapMap);
    } else if (uhdm::InstanceArray* ex = any_cast<uhdm::InstanceArray>(var)) {
      replace(ex->getElemTypespec(), typespecSwapMap);
    } else if (uhdm::NamedEvent* ex = any_cast<uhdm::NamedEvent>(var)) {
      replace(ex->getTypespec(), typespecSwapMap);
    } else if (uhdm::ParamAssign* ex = any_cast<uhdm::ParamAssign>(var)) {
      if (uhdm::Typespec* rhs = ex->getRhs<uhdm::Typespec>()) {
        ex->setRhs(replace(rhs, typespecSwapMap));
      }
    }
    // common pointers
    if (uhdm::Scope* ex = any_cast<uhdm::Scope>(var)) {
      if (ex->getTypespecs()) {
        for (auto& tps : *ex->getTypespecs()) {
          tps = replace(tps, typespecSwapMap);
        }
      }
    }
    if (uhdm::Expr* ex = any_cast<uhdm::Expr>(var)) {
      replace(ex->getTypespec(), typespecSwapMap);
    }
    if (uhdm::TypedefTypespec* ex = any_cast<uhdm::TypedefTypespec>(var)) {
      replace(ex->getTypedefAlias(), typespecSwapMap);
    }
  }
}

bool ElaborationStep::bindTypedefsPostElab_() {
  Compiler* compiler = m_compileDesign->getCompiler();
  Design* design = compiler->getDesign();
  uhdm::Serializer& s = m_compileDesign->getSerializer();
  std::queue<ModuleInstance*> queue;
  for (auto instance : design->getTopLevelModuleInstances()) {
    queue.push(instance);
  }

  while (!queue.empty()) {
    ModuleInstance* current = queue.front();
    queue.pop();
    if (current == nullptr) continue;
    for (uint32_t i = 0; i < current->getNbChildren(); i++) {
      queue.push(current->getChildren(i));
    }
    if (auto comp = current->getDefinition()) {
      for (const auto& typed : comp->getDataTypeMap()) {
        const DataType* dt = typed.second;
        while (dt) {
          ((DataType*)dt)
              ->setTypespec(replace(dt->getTypespec(),
                                    m_compileDesign->getSwapedObjects()));
          dt = dt->getDefinition();
        }
      }
      for (const auto& typed : comp->getTypeDefMap()) {
        TypeDef* typd = typed.second;
        const DataType* dt = typd;
        while (dt) {
          ((DataType*)dt)
              ->setTypespec(replace(dt->getTypespec(),
                                    m_compileDesign->getSwapedObjects()));
          dt = dt->getDefinition();
        }
      }
    }
  }

  swapTypespecPointersInUhdm(s, m_compileDesign->getSwapedObjects());
  swapTypespecPointersInTypedef(design, m_compileDesign->getSwapedObjects());
  return true;
}

const DataType* ElaborationStep::bindTypeDef_(
    TypeDef* typd, const DesignComponent* parent,
    ErrorDefinition::ErrorType errtype) {
  SymbolTable* symbols = m_session->getSymbolTable();
  NodeId defNode = typd->getDefinitionNode();
  const FileContent* fC = typd->getFileContent();
  VObjectType defType = fC->Type(defNode);
  std::string objName;
  if (defType == VObjectType::STRING_CONST) {
    objName = fC->SymName(defNode);
  } else if (defType == VObjectType::paClass_scope) {
    NodeId class_type = fC->Child(defNode);
    NodeId nameId = fC->Child(class_type);
    objName.assign(fC->SymName(nameId))
        .append("::")
        .append(fC->SymName(fC->Sibling(defNode)));
  } else {
    objName = "NOT_A_VALID_TYPE_NAME";
    symbols->registerSymbol(objName);
  }

  const DataType* result = bindDataType_(objName, fC, defNode, parent, errtype);
  if (result != typd)
    return result;
  else
    return nullptr;
}

const DataType* ElaborationStep::bindDataType_(
    std::string_view type_name, const FileContent* fC, NodeId id,
    const DesignComponent* parent, ErrorDefinition::ErrorType errtype) {
  Compiler* compiler = m_compileDesign->getCompiler();
  ErrorContainer* errors = m_session->getErrorContainer();
  SymbolTable* symbols = m_session->getSymbolTable();
  Design* design = compiler->getDesign();
  std::string libName = "work";
  if (!parent->getFileContents().empty()) {
    libName = parent->getFileContents()[0]->getLibrary()->getName();
  }
  ClassNameClassDefinitionMultiMap classes = design->getClassDefinitions();
  bool found = false;
  bool classFound = false;
  std::string class_in_lib = StrCat(libName, "@", type_name);
  ClassNameClassDefinitionMultiMap::iterator itr1 = classes.end();

  if (type_name == "signed") {
    return new DataType(fC, id, type_name, VObjectType::paSigning_Signed);
  } else if (type_name == "unsigned") {
    return new DataType(fC, id, type_name, VObjectType::paSigning_Unsigned);
  } else if (type_name == "logic") {
    return new DataType(fC, id, type_name, VObjectType::paIntVec_TypeLogic);
  } else if (type_name == "bit") {
    return new DataType(fC, id, type_name, VObjectType::paIntVec_TypeBit);
  } else if (type_name == "byte") {
    return new DataType(fC, id, type_name, VObjectType::paIntegerAtomType_Byte);
  }

  const DataType* result = nullptr;
  if ((result = parent->getDataType(design, type_name))) {
    found = true;
  }
  if (found == false) {
    itr1 = classes.find(class_in_lib);

    if (itr1 != classes.end()) {
      found = true;
      classFound = true;
    }
  }
  if (found == false) {
    itr1 = classes.find(type_name);

    if (itr1 != classes.end()) {
      found = true;
      classFound = true;
    }
  }
  if (found == false) {
    std::string class_in_class = StrCat(parent->getName(), "::", type_name);
    itr1 = classes.find(class_in_class);

    if (itr1 != classes.end()) {
      found = true;
      classFound = true;
    }
  }
  if (found == false) {
    if (parent->getParentScope()) {
      std::string class_in_own_package =
          StrCat(((DesignComponent*)parent->getParentScope())->getName(),
                 "::", type_name);
      itr1 = classes.find(class_in_own_package);
      if (itr1 != classes.end()) {
        found = true;
        classFound = true;
      }
    }
  }
  if (found == false) {
    for (const auto& package : parent->getAccessPackages()) {
      std::string class_in_package =
          StrCat(package->getName(), "::", type_name);
      itr1 = classes.find(class_in_package);
      if (itr1 != classes.end()) {
        found = true;
        classFound = true;
        break;
      }
      const DataType* dtype = package->getDataType(design, type_name);
      if (dtype) {
        found = true;
        result = dtype;
        break;
      }
    }
  }
  if (found == false) {
    const ClassDefinition* classDefinition =
        valuedcomponenti_cast<const ClassDefinition*>(parent);
    if (classDefinition) {
      if (classDefinition->getName() == type_name) {
        result = classDefinition;
        found = true;
      }
      if (found == false) {
        Parameter* param = classDefinition->getParameter(type_name);
        if (param) {
          found = true;
          result = param;
        }
      }
      if (found == false) {
        if ((result = classDefinition->getBaseDataType(type_name))) {
          found = true;
        }
      }
      if (found == false) {
        if (classDefinition->getContainer()) {
          const DataType* dtype =
              classDefinition->getContainer()->getDataType(design, type_name);
          if (dtype) {
            found = true;
            result = dtype;
          }
        }
      }
    }
  }
  if (found == false) {
    const TypeDef* def = parent->getTypeDef(type_name);
    if (def) {
      found = true;
      result = def;
    }
  }

  if (found == false) {
    auto res = parent->getNamedObject(type_name);
    if (res) {
      DesignComponent* comp = res->second;
      result = valuedcomponenti_cast<ClassDefinition*>(comp);
      if (result) found = true;
    }
  }
  if (found == false) {
    auto res = parent->getNamedObject(StrCat(libName, "@", type_name));
    if (res) {
      DesignComponent* comp = res->second;
      result = valuedcomponenti_cast<ClassDefinition*>(comp);
      if (result) found = true;
    }
  }
  if (found == false) {
    if (type_name.find("::") != std::string::npos) {
      std::vector<std::string_view> args;
      StringUtils::tokenizeMulti(type_name, "::", args);
      std::string_view classOrPackageName = args[0];
      std::string_view the_type_name = args[1];
      itr1 = classes.find(StrCat(libName, "@", classOrPackageName));
      if (itr1 == classes.end()) {
        if (parent->getParentScope()) {
          std::string class_in_own_package =
              StrCat(((DesignComponent*)parent->getParentScope())->getName(),
                     "::", classOrPackageName);
          itr1 = classes.find(class_in_own_package);
        }
      }
      if (itr1 != classes.end()) {
        const DataType* dtype =
            itr1->second->getDataType(design, the_type_name);
        if (dtype) {
          result = dtype;
          found = true;
        }
      }
      if (found == false) {
        Package* pack = design->getPackage(classOrPackageName);
        if (pack) {
          const DataType* dtype = pack->getDataType(design, the_type_name);
          if (dtype) {
            result = dtype;
            found = true;
          }
          if (found == false) {
            dtype = pack->getDataType(design, type_name);
            if (dtype) {
              result = dtype;
              found = true;
            }
          }
          if (found == false) {
            dtype = pack->getClassDefinition(type_name);
            if (dtype) {
              result = dtype;
              found = true;
            }
          }
        }
      }
    }
  }

  if ((found == false) && (errtype != ErrorDefinition::NO_ERROR_MESSAGE)) {
    Location loc1(fC->getFileId(id), fC->Line(id), fC->Column(id),
                  symbols->registerSymbol(type_name));
    Location loc2(symbols->registerSymbol(parent->getName()));
    Error err1(errtype, loc1, loc2);
    errors->addError(err1);
  } else {
    if (classFound == true) {
      // Binding
      ClassDefinition* def = itr1->second;
      result = def;
    }
  }
  while (result && result->getDefinition()) {
    result = result->getDefinition();
  }

  return result;
}

Variable* ElaborationStep::bindVariable_(std::string_view var_name,
                                         Scope* scope, const FileContent* fC,
                                         NodeId id,
                                         const DesignComponent* parent,
                                         ErrorDefinition::ErrorType errtype,
                                         bool returnClassParam) {
  SymbolTable* const symbols = m_session->getSymbolTable();
  ErrorContainer* const errors = m_session->getErrorContainer();
  Variable* result = nullptr;

  const ClassDefinition* classDefinition =
      valuedcomponenti_cast<const ClassDefinition*>(parent);
  if (classDefinition) result = classDefinition->getProperty(var_name);

  if (result == nullptr) {
    if (scope) {
      result = scope->getVariable(var_name);
    }
  }
  if ((result == nullptr) && scope) {
    Scope* itr_scope = scope;
    while (itr_scope) {
      Procedure* proc = scope_cast<Procedure*>(itr_scope);
      if (proc) {
        for (auto param : proc->getParams()) {
          if (param->getName() == var_name) {
            result = param;
            break;
          }
        }
      }
      if (result) break;
      itr_scope = itr_scope->getParentScope();
    }
  }

  if (result == nullptr && parent) {
    for (auto package : parent->getAccessPackages()) {
      Value* val = package->getValue(var_name);
      if (val) {
        break;
      }
    }
  }

  if ((result == nullptr) && (errtype != ErrorDefinition::NO_ERROR_MESSAGE)) {
    Location loc1(fC->getFileId(id), fC->Line(id), fC->Column(id),
                  symbols->registerSymbol(var_name));
    Location loc2(symbols->registerSymbol(parent->getName()));
    Error err1(errtype, loc1, loc2);
    errors->addError(err1);
  }

  if (!returnClassParam) {
    // Class parameters datatype have no definition and are strings
    if (result) {
      const DataType* dtype = result->getDataType();
      if (dtype && !dtype->getDefinition()) {
        if (dtype->getType() == VObjectType::STRING_CONST) {
          result = nullptr;
        }
      }
    }
  }

  return result;
}

Variable* ElaborationStep::locateVariable_(
    const std::vector<std::string_view>& var_chain, const FileContent* fC,
    NodeId id, Scope* scope, DesignComponent* parentComponent,
    ErrorDefinition::ErrorType errtype) {
  Variable* the_obj = nullptr;
  const DesignComponent* currentComponent = parentComponent;
  for (auto var : var_chain) {
    if (var == "this") {
    } else if (var == "super") {
      const ClassDefinition* classDefinition =
          valuedcomponenti_cast<const ClassDefinition*>(currentComponent);
      if (classDefinition) {
        currentComponent = nullptr;
        const auto& baseClassMap = classDefinition->getBaseClassMap();
        if (!baseClassMap.empty()) {
          currentComponent = datatype_cast<const ClassDefinition*>(
              baseClassMap.begin()->second);
          var = "this";
        }
        if (currentComponent == nullptr) {
          var = "super";
          currentComponent = parentComponent;
        }
      }
    }

    the_obj =
        bindVariable_(var, scope, fC, id, currentComponent, errtype, false);
    if (the_obj) {
      const DataType* dtype = the_obj->getDataType();
      while (dtype && dtype->getDefinition()) {
        dtype = dtype->getDefinition();
      }
      const ClassDefinition* tmpClass =
          datatype_cast<const ClassDefinition*>(dtype);
      if (tmpClass) {
        currentComponent = tmpClass;
      }
    }
  }
  return the_obj;
}

Variable* ElaborationStep::locateStaticVariable_(
    const std::vector<std::string_view>& var_chain, const FileContent* fC,
    NodeId id, Scope* scope, DesignComponent* parentComponent,
    ErrorDefinition::ErrorType errtype) {
  std::string name;
  for (uint32_t i = 0; i < var_chain.size(); i++) {
    name += var_chain[i];
    if (i < var_chain.size() - 1) name += "::";
  }
  std::map<std::string, Variable*>::iterator itr = m_staticVariables.find(name);
  if (itr != m_staticVariables.end()) return itr->second;
  Variable* result = nullptr;
  Design* design = m_compileDesign->getCompiler()->getDesign();
  if (!var_chain.empty()) {
    Package* package = design->getPackage(var_chain[0]);
    if (package) {
      if (var_chain.size() > 1) {
        ClassDefinition* classDefinition =
            package->getClassDefinition(var_chain[1]);
        if (classDefinition) {
          if (var_chain.size() == 2) {
            result =
                new Variable(classDefinition, classDefinition->getFileContent(),
                             classDefinition->getNodeId(), InvalidNodeId,
                             classDefinition->getName());
          }
          if (var_chain.size() == 3) {
            std::vector<std::string_view> tmp;
            tmp.emplace_back(var_chain[2]);
            result =
                locateVariable_(tmp, fC, id, scope, classDefinition, errtype);
          }
        }
      }
    }

    if (result == nullptr) {
      ClassDefinition* classDefinition =
          design->getClassDefinition(var_chain[0]);
      if (classDefinition == nullptr) {
        if (parentComponent && parentComponent->getParentScope()) {
          const std::string name = StrCat(
              ((DesignComponent*)parentComponent->getParentScope())->getName(),
              "::", var_chain[0]);
          classDefinition = design->getClassDefinition(name);
        }
      }
      if (classDefinition) {
        if (var_chain.size() == 1)
          result =
              new Variable(classDefinition, classDefinition->getFileContent(),
                           classDefinition->getNodeId(), InvalidNodeId,
                           classDefinition->getName());
        if (var_chain.size() == 2) {
          std::vector<std::string_view> tmp;
          tmp.emplace_back(var_chain[1]);

          const DataType* dtype =
              bindDataType_(var_chain[1], fC, id, classDefinition,
                            ErrorDefinition::NO_ERROR_MESSAGE);
          if (dtype) {
            result =
                new Variable(dtype, dtype->getFileContent(), dtype->getNodeId(),
                             InvalidNodeId, dtype->getName());
          } else
            result =
                locateVariable_(tmp, fC, id, scope, classDefinition, errtype);
        }
      }
    }
  }
  if (result == nullptr) {
    if (!var_chain.empty()) {
      const DataType* dtype =
          bindDataType_(var_chain[0], fC, id, parentComponent, errtype);
      if (dtype) {
        result =
            new Variable(dtype, dtype->getFileContent(), dtype->getNodeId(),
                         InvalidNodeId, dtype->getName());
      }
    }
  }
  m_staticVariables.emplace(name, result);
  return result;
}

void checkIfBuiltInTypeOrErrorOut(DesignComponent* def, const FileContent* fC,
                                  NodeId id, const DataType* type,
                                  std::string_view interfName,
                                  ErrorContainer* errors,
                                  SymbolTable* symbols) {
  if (def == nullptr && type == nullptr && (interfName != "logic") &&
      (interfName != "byte") && (interfName != "bit") &&
      (interfName != "new") && (interfName != "expect") &&
      (interfName != "var") && (interfName != "signed") &&
      (interfName != "unsigned") && (interfName != "do") &&
      (interfName != "final") && (interfName != "global") &&
      (interfName != "soft")) {
    Location loc(fC->getFileId(id), fC->Line(id), fC->Column(id),
                 symbols->registerSymbol(interfName));
    Error err(ErrorDefinition::COMP_UNDEFINED_TYPE, loc);
    errors->addError(err);
  }
}

bool bindStructInPackage(Design* design, Signal* signal,
                         std::string_view packageName,
                         std::string_view structName) {
  Package* p = design->getPackage(packageName);
  if (p) {
    const DataType* dtype = p->getDataType(design, structName);
    if (dtype) {
      signal->setDataType(dtype);
      const DataType* actual = dtype->getActual();
      if (actual->getCategory() == DataType::Category::STRUCT) {
        Struct* st = (Struct*)actual;
        if (st->isNet()) {
          signal->setType(VObjectType::paNetType_Wire);
        }
      }
      return true;
    } else {
      const DataType* dtype = p->getClassDefinition(structName);
      if (dtype) {
        signal->setDataType(dtype);
        return true;
      }
    }
  }
  return false;
}

bool ElaborationStep::bindPortType_(Signal* signal, const FileContent* fC,
                                    NodeId id, Scope* scope,
                                    ModuleInstance* instance,
                                    DesignComponent* parentComponent,
                                    ErrorDefinition::ErrorType errtype) {
  if (signal->getDataType() || signal->getInterfaceDef() ||
      signal->getModport())
    return true;
  Compiler* const compiler = m_compileDesign->getCompiler();
  SymbolTable* const symbols = m_session->getSymbolTable();
  ErrorContainer* const errors = m_session->getErrorContainer();
  CommandLineParser* const clp = m_session->getCommandLineParser();
  Design* design = compiler->getDesign();
  const std::string_view libName = fC->getLibrary()->getName();
  VObjectType type = fC->Type(id);
  switch (type) {
    case VObjectType::paPort:
      /*
        n<mem_if> u<3> t<StringConst> p<6> s<5> l<1>
        n<> u<4> t<Constant_bit_select> p<5> l<1>
        n<> u<5> t<Constant_select> p<6> c<4> l<1>
        n<> u<6> t<Port_reference> p<11> c<3> s<10> l<1>
        n<mif> u<7> t<StringConst> p<10> s<9> l<1>
        n<> u<8> t<Constant_bit_select> p<9> l<1>
        n<> u<9> t<Constant_select> p<10> c<8> l<1>
        n<> u<10> t<Port_reference> p<11> c<7> l<1>
        n<> u<11> t<Port_expression> p<12> c<6> l<1>
        n<> u<12> t<uhdm::Port> p<13> c<11> l<1>
       */
      {
        NodeId Port_expression = fC->Child(id);
        if (Port_expression &&
            (fC->Type(Port_expression) == VObjectType::paPort_expression)) {
          NodeId if_type = fC->Child(Port_expression);
          if (fC->Type(if_type) == VObjectType::paPort_reference) {
            NodeId if_type_name_s = fC->Child(if_type);
            NodeId if_name = fC->Sibling(if_type);
            if (if_name) {
              std::string interfaceName =
                  StrCat(libName, "@", fC->SymName(if_type_name_s));
              ModuleDefinition* interface =
                  design->getModuleDefinition(interfaceName);
              if (interface) {
                signal->setInterfaceDef(interface);
              } else {
                Location loc(fC->getFileId(if_type_name_s),
                             fC->Line(if_type_name_s),
                             fC->Column(if_type_name_s),
                             symbols->registerSymbol(interfaceName));
                Error err(ErrorDefinition::COMP_UNDEFINED_INTERFACE, loc);
                errors->addError(err);
              }
            }
          }
        }
        break;
      }
    case VObjectType::paInput_declaration:
    case VObjectType::paOutput_declaration:
    case VObjectType::paInout_declaration: {
      break;
    }
    case VObjectType::paPort_declaration: {
      /*
       n<Configuration> u<21> t<StringConst> p<22> l<7>
       n<> u<22> t<Interface_identifier> p<26> c<21> s<25> l<7>
       n<cfg> u<23> t<StringConst> p<24> l<7>
       n<> u<24> t<Interface_identifier> p<25> c<23> l<7>
       n<> u<25> t<List_of_interface_identifiers> p<26> c<24> l<7>
       n<> u<26> t<Interface_port_declaration> p<27> c<22> l<7>
       n<> u<27> t<Port_declaration> p<28> c<26> l<7>
       */
      NodeId subNode = fC->Child(id);
      VObjectType subType = fC->Type(subNode);
      switch (subType) {
        case VObjectType::paInterface_port_declaration: {
          NodeId interface_identifier = fC->Child(subNode);
          NodeId interfIdName = fC->Child(interface_identifier);
          const std::string_view interfName = fC->SymName(interfIdName);

          DesignComponent* def = nullptr;
          const DataType* type = nullptr;

          const std::pair<FileCNodeId, DesignComponent*>* datatype =
              parentComponent->getNamedObject(interfName);
          if (!datatype) {
            def = design->getClassDefinition(
                StrCat(parentComponent->getName(), "::", interfName));
          }
          if (datatype) {
            def = datatype->second;
          }
          if (def == nullptr) {
            def = design->getComponentDefinition(
                StrCat(libName, "@", interfName));
          }
          if (def == nullptr) {
            type = parentComponent->getDataType(design, interfName);
          }
          checkIfBuiltInTypeOrErrorOut(def, fC, id, type, interfName, errors,
                                       symbols);
          break;
        }
        case VObjectType::paInput_declaration:
        case VObjectType::paOutput_declaration:
        case VObjectType::paInout_declaration: {
          break;
        }
        default:
          break;
      }
      break;
    }
    case VObjectType::STRING_CONST: {
      std::string interfName;
      if (signal->getInterfaceTypeNameId()) {
        interfName = signal->getInterfaceTypeName();
      } else {
        if (NodeId typespecId = signal->getTypespecId()) {
          if (fC->Type(typespecId) == VObjectType::paClass_scope) {
            NodeId Class_type = fC->Child(typespecId);
            NodeId Class_type_name = fC->Child(Class_type);
            NodeId Class_scope_name = fC->Sibling(typespecId);
            if (bindStructInPackage(design, signal,
                                    fC->SymName(Class_type_name),
                                    fC->SymName(Class_scope_name)))
              return true;
          } else if (fC->Type(typespecId) == VObjectType::STRING_CONST) {
            interfName = fC->SymName(typespecId);
          }
        }
      }
      std::string baseName = interfName;
      std::string modPort;
      if (interfName.find('.') != std::string::npos) {
        modPort = interfName;
        modPort = StringUtils::ltrim_until(modPort, '.');
        baseName = StringUtils::rtrim_until(baseName, '.');
      } else if (interfName.find("::") != std::string::npos) {
        std::vector<std::string_view> result;
        StringUtils::tokenizeMulti(interfName, "::", result);
        if (result.size() > 1) {
          const std::string_view packName = result[0];
          const std::string_view structName = result[1];
          if (bindStructInPackage(design, signal, packName, structName))
            return true;
        }
      }

      DesignComponent* def = nullptr;
      const DataType* type = nullptr;

      const std::pair<FileCNodeId, DesignComponent*>* datatype =
          parentComponent->getNamedObject(interfName);
      if (datatype) {
        def = datatype->second;
        DataType* dt = valuedcomponenti_cast<ClassDefinition*>(def);
        if (dt) {
          signal->setDataType(dt);
        }
      } else {
        std::string name = StrCat(parentComponent->getName(), "::", interfName);
        def = design->getClassDefinition(name);
        DataType* dt = valuedcomponenti_cast<ClassDefinition*>(def);
        if (dt) {
          signal->setDataType(dt);
        }
      }
      if (def == nullptr) {
        def = design->getComponentDefinition(StrCat(libName, "@", baseName));
        if (def) {
          ModuleDefinition* module =
              valuedcomponenti_cast<ModuleDefinition*>(def);
          ClassDefinition* cl = valuedcomponenti_cast<ClassDefinition*>(def);
          if (module) {
            signal->setInterfaceDef(module);
          } else if (cl) {
            signal->setDataType(cl);
            return true;
          } else {
            def = nullptr;
          }
          if (!modPort.empty()) {
            if (module) {
              if (Modport* modport = module->getModport(modPort)) {
                signal->setModport(modport);
              } else {
                def = nullptr;
              }
            }
          }
        }
      }
      if (def == nullptr) {
        def = design->getComponentDefinition(StrCat(libName, "@", baseName));
        ClassDefinition* c = valuedcomponenti_cast<ClassDefinition*>(def);
        if (c) {
          Variable* var = new Variable(c, fC, signal->getNodeId(),
                                       InvalidNodeId, signal->getName());
          parentComponent->addVariable(var);
          return false;
        } else {
          def = nullptr;
        }
      }
      if (def == nullptr) {
        type = parentComponent->getDataType(design, interfName);
        if (type == nullptr) {
          if (!clp->fileUnit()) {
            for (const auto& fC : m_compileDesign->getCompiler()
                                      ->getDesign()
                                      ->getAllFileContents()) {
              if (const DataType* dt1 =
                      fC.second->getDataType(design, interfName)) {
                type = dt1;
                break;
              }
            }
          }
        }

        if (type) {
          const DataType* def = type->getActual();
          DataType::Category cat = def->getCategory();
          if (cat == DataType::Category::SIMPLE_TYPEDEF) {
            VObjectType t = def->getType();
            if (t == VObjectType::paIntVec_TypeLogic) {
              // Make "net types" explicit (vs variable types) for elab.
              signal->setType(VObjectType::paIntVec_TypeLogic);
            } else if (t == VObjectType::paIntVec_TypeReg) {
              signal->setType(VObjectType::paIntVec_TypeReg);
            } else if (t == VObjectType::paNetType_Wire) {
              signal->setType(VObjectType::paNetType_Wire);
            }
          } else if (cat == DataType::Category::REF) {
            // Should not arrive here, there should always be an actual
            // definition
          }
          signal->setDataType(type);
        }
      }
      if (def == nullptr) {
        if (parentComponent->getParameters()) {
          for (auto param : *parentComponent->getParameters()) {
            if (param->getUhdmType() == uhdm::UhdmType::TypeParameter) {
              if (param->getName() == interfName) {
                Parameter* p = parentComponent->getParameter(interfName);
                type = p;
                signal->setDataType(type);
                return true;
              }
            }
          }
        }
      }
      if (signal->getType() != VObjectType::NO_TYPE) {
        return true;
      }
      if (def == nullptr) {
        while (instance) {
          for (Parameter* p : instance->getTypeParams()) {
            if (p->getName() == interfName) {
              type = p;
              signal->setDataType(type);
              return true;
            }
          }

          DesignComponent* component = instance->getDefinition();
          if (component) {
            if (component->getParameters()) {
              for (auto param : *component->getParameters()) {
                if (param->getUhdmType() == uhdm::UhdmType::TypeParameter) {
                  if (param->getName() == interfName) {
                    Parameter* p = component->getParameter(interfName);
                    type = p;
                    signal->setDataType(type);
                    return true;
                  }
                }
              }
            }
          }
          instance = instance->getParent();
        }
      }
      checkIfBuiltInTypeOrErrorOut(def, fC, id, type, interfName, errors,
                                   symbols);
      break;
    }
    default:
      break;
  }
  return true;
}

uhdm::Expr* ElaborationStep::exprFromAssign_(DesignComponent* component,
                                             const FileContent* fC, NodeId id,
                                             NodeId unpackedDimension,
                                             ModuleInstance* instance) {
  // Assignment section
  NodeId assignment;
  NodeId Assign = fC->Sibling(id);
  if (Assign && (fC->Type(Assign) == VObjectType::paExpression)) {
    assignment = Assign;
  }
  if (unpackedDimension) {
    NodeId tmp = unpackedDimension;
    while ((fC->Type(tmp) == VObjectType::paUnpacked_dimension) ||
           (fC->Type(tmp) == VObjectType::paVariable_dimension)) {
      tmp = fC->Sibling(tmp);
    }
    if (tmp && (fC->Type(tmp) != VObjectType::paUnpacked_dimension) &&
        (fC->Type(tmp) != VObjectType::paVariable_dimension)) {
      assignment = tmp;
    }
  }

  NodeId expression;
  if (assignment) {
    if (fC->Type(assignment) == VObjectType::paClass_new) {
      expression = assignment;
    } else {
      NodeId Primary = fC->Child(assignment);
      if (fC->Type(assignment) == VObjectType::paExpression) {
        Primary = assignment;
      }
      expression = Primary;
    }
  } else {
    expression = fC->Sibling(id);
    if ((fC->Type(expression) != VObjectType::paExpression) &&
        (fC->Type(expression) != VObjectType::paConstant_expression))
      expression = InvalidNodeId;
  }

  uhdm::Expr* exp = nullptr;
  if (expression) {
    exp = (uhdm::Expr*)m_helper.compileExpression(component, fC, expression,
                                                  m_compileDesign, Reduce::No,
                                                  nullptr, instance);
  }
  return exp;
}

uhdm::Typespec* ElaborationStep::elabTypeParameter_(DesignComponent* component,
                                                    Parameter* sit,
                                                    ModuleInstance* instance) {
  uhdm::Serializer& s = m_compileDesign->getSerializer();
  uhdm::Any* uparam = sit->getUhdmParam();
  uhdm::Typespec* spec = nullptr;
  bool type_param = false;
  if (uparam->getUhdmType() == uhdm::UhdmType::TypeParameter) {
    if (uhdm::RefTypespec* rt = ((uhdm::TypeParameter*)uparam)->getTypespec())
      spec = rt->getActual();
    type_param = true;
  } else {
    if (uhdm::RefTypespec* rt = ((uhdm::Parameter*)uparam)->getTypespec())
      spec = rt->getActual();
  }

  const std::string_view pname = sit->getName();
  for (Parameter* param : instance->getTypeParams()) {
    // Param override
    if (param->getName() == pname) {
      uhdm::Any* uparam = param->getUhdmParam();
      uhdm::Typespec* override_spec = nullptr;
      if (uparam == nullptr) {
        if (type_param) {
          uhdm::TypeParameter* tp = s.make<uhdm::TypeParameter>();
          tp->setName(pname);
          param->setUhdmParam(tp);
        } else {
          uhdm::Parameter* tp = s.make<uhdm::Parameter>();
          tp->setName(pname);
          param->setUhdmParam(tp);
        }
        uparam = param->getUhdmParam();
      }

      if (type_param) {
        if (uhdm::RefTypespec* rt =
                ((uhdm::TypeParameter*)uparam)->getTypespec()) {
          override_spec = rt->getActual();
        }
      } else {
        if (uhdm::RefTypespec* rt = ((uhdm::Parameter*)uparam)->getTypespec()) {
          override_spec = rt->getActual();
        }
      }

      if (override_spec == nullptr) {
        ModuleInstance* parent = instance;
        if (ModuleInstance* pinst = instance->getParent()) parent = pinst;
        override_spec = m_helper.compileTypespec(
            component, param->getFileContent(), param->getNodeType(),
            InvalidNodeId, m_compileDesign, Reduce::Yes, nullptr, parent,
            false);
      }

      if (override_spec) {
        if (type_param) {
          uhdm::TypeParameter* tparam = (uhdm::TypeParameter*)uparam;
          if (tparam->getTypespec() == nullptr) {
            uhdm::RefTypespec* override_specRef = s.make<uhdm::RefTypespec>();
            override_specRef->setParent(tparam);
            tparam->setTypespec(override_specRef);
          }
          tparam->getTypespec()->setActual(override_spec);
        } else {
          uhdm::Parameter* tparam = (uhdm::Parameter*)uparam;
          if (tparam->getTypespec() == nullptr) {
            uhdm::RefTypespec* override_specRef = s.make<uhdm::RefTypespec>();
            override_specRef->setParent(tparam);
            tparam->setTypespec(override_specRef);
          }
          tparam->getTypespec()->setActual(override_spec);
        }
        spec = override_spec;
        spec->setParent(uparam);
      }
      break;
    }
  }
  return spec;
}

uhdm::Any* ElaborationStep::makeVar_(
    DesignComponent* component, Signal* sig,
    std::vector<uhdm::Range*>* packedDimensions, int32_t packedSize,
    std::vector<uhdm::Range*>* unpackedDimensions, int32_t unpackedSize,
    ModuleInstance* instance, uhdm::VariablesCollection* vars,
    uhdm::Expr* assignExp, uhdm::Typespec* tps) {
  uhdm::Variables* obj = (uhdm::Variables*)m_helper.compileSignals(
      component, m_compileDesign, sig);
  if (assignExp) {
    assignExp->setParent(obj);
    obj->setExpr(assignExp);

    if (assignExp->getUhdmType() == uhdm::UhdmType::Constant) {
      m_helper.adjustSize(tps, component, m_compileDesign, instance,
                          (uhdm::Constant*)assignExp);
    } else if (assignExp->getUhdmType() == uhdm::UhdmType::Operation) {
      uhdm::Operation* op = (uhdm::Operation*)assignExp;
      int32_t opType = op->getOpType();
      const uhdm::Typespec* tp = tps;
      if (opType == vpiAssignmentPatternOp) {
        if (tp->getUhdmType() == uhdm::UhdmType::PackedArrayTypespec) {
          uhdm::PackedArrayTypespec* ptp = (uhdm::PackedArrayTypespec*)tp;
          if (const uhdm::RefTypespec* ert = ptp->getElemTypespec()) {
            tp = ert->getActual();
          }
          if (tp == nullptr) tp = tps;
        }
      }
      for (auto oper : *op->getOperands()) {
        if (oper->getUhdmType() == uhdm::UhdmType::Constant)
          m_helper.adjustSize(tp, component, m_compileDesign, instance,
                              (uhdm::Constant*)oper, false, true);
      }
    }
  }

  return obj;
}
}  // namespace SURELOG
