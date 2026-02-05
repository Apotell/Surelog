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
 * File:   IntegrityChecker.cpp
 * Author: hs
 *
 * Created on August 10, 2024, 00:00 AM
 */

#include <Surelog/Common/FileSystem.h>
#include <Surelog/Common/Session.h>
#include <Surelog/DesignCompile/IntegrityChecker.h>
#include <Surelog/ErrorReporting/ErrorContainer.h>
#include <Surelog/SourceCompile/SymbolTable.h>
#include <Surelog/Utils/StringUtils.h>

// uhdm
#include <uhdm/Utils.h>
#include <uhdm/uhdm.h>

namespace SURELOG {
IntegrityChecker::IntegrityChecker(Session* session)
    : m_session(session),
      m_typesWithValidName{
          uhdm::UhdmType::ClassDefn,
          uhdm::UhdmType::ClassTypespec,
          uhdm::UhdmType::ClassTypespec,
          uhdm::UhdmType::FuncCall,
          uhdm::UhdmType::Function,
          uhdm::UhdmType::FunctionDecl,
          uhdm::UhdmType::Identifier,
          uhdm::UhdmType::Interface,
          uhdm::UhdmType::IODecl,
          uhdm::UhdmType::MethodFuncCall,
          uhdm::UhdmType::MethodTaskCall,
          uhdm::UhdmType::Modport,
          uhdm::UhdmType::ModuleTypespec,
          uhdm::UhdmType::NamedEvent,
          uhdm::UhdmType::Net,
          uhdm::UhdmType::Package,
          uhdm::UhdmType::Parameter,
          uhdm::UhdmType::Port,
          uhdm::UhdmType::Program,
          uhdm::UhdmType::ProgramTypespec,
          uhdm::UhdmType::RefModule,
          uhdm::UhdmType::RefObj,
          uhdm::UhdmType::SourceFile,
          uhdm::UhdmType::SysFuncCall,
          uhdm::UhdmType::SysTaskCall,
          uhdm::UhdmType::Task,
          uhdm::UhdmType::TaskCall,
          uhdm::UhdmType::TaskDecl,
          uhdm::UhdmType::TypedefTypespec,
          uhdm::UhdmType::TypespecMember,
          // TODO(HS): Change UdpDefn::defName to UdpDefn::name
          // uhdm::UhdmType::UdpDefn,
          uhdm::UhdmType::UdpDefnTypespec,
          uhdm::UhdmType::Variable,
      },
      m_typesWithMissingFile{
          uhdm::UhdmType::Begin,
          uhdm::UhdmType::Design,
          // TODO(HS): Remove this once identifier location is fixed
          uhdm::UhdmType::Identifier,
          uhdm::UhdmType::RefTypespec,
      },
      m_typesWithMissingParent{
          uhdm::UhdmType::Design,
      },
      m_typesWithMissingLocation{
          uhdm::UhdmType::Begin,
          uhdm::UhdmType::Design,
          // TODO(HS): Remove this once identifier location is fixed
          uhdm::UhdmType::Identifier,
          uhdm::UhdmType::RefTypespec,
          uhdm::UhdmType::SourceFile,
      } {}

std::string_view IntegrityChecker::toString(LineColumnRelation relation) const {
  switch (relation) {
    case LineColumnRelation::Before: return "Before";
    case LineColumnRelation::Inside: return "Inside";
    case LineColumnRelation::After: return "After";
    case LineColumnRelation::Inconclusive: return "Inconclusive";
  }
  return "VeryBad";
}

bool IntegrityChecker::isUVMMember(const uhdm::Any* object) {
  std::string_view filepath = object->getFile();
  return (filepath.find("\\uvm_") != std::string_view::npos) || (filepath.find("/uvm_") != std::string_view::npos) ||
         (filepath.find("\\ovm_") != std::string_view::npos) || (filepath.find("/ovm_") != std::string_view::npos);
}

bool IntegrityChecker::isValidFile(const uhdm::Any* object) {
  std::string_view name = object->getFile();
  return !name.empty() && (name != uhdm::SymbolFactory::getBadSymbol());
}

bool IntegrityChecker::isValidName(const uhdm::Any* object) {
  std::string_view name = object->getName();
  return !name.empty() && (name != uhdm::SymbolFactory::getBadSymbol());
}

bool IntegrityChecker::isValidLocation(const uhdm::Any* object) {
  return (object->getStartLine() != 0) && (object->getStartColumn() != 0) && (object->getEndLine() != 0) &&
         (object->getEndColumn() != 0);
}

void IntegrityChecker::reportError(ErrorDefinition::ErrorType errorType, const uhdm::Any* object) const {
  SymbolTable* const symbolTable = m_session->getSymbolTable();
  FileSystem* const fileSystem = m_session->getFileSystem();
  ErrorContainer* const errorContainer = m_session->getErrorContainer();

  Location loc(
      fileSystem->toPathId(object->getFile(), symbolTable), object->getStartLine(), object->getStartColumn(),
      symbolTable->registerSymbol(StrCat("id:", object->getUhdmId(), ", type:", uhdm::UhdmName(object->getUhdmType()),
                                         ", name:", object->getName())));
  errorContainer->addError(errorType, loc);
}

template <typename T>
void IntegrityChecker::reportDuplicates(const uhdm::Any* object, const std::vector<T*>& collection) const {
  if (!m_reportDuplicates) return;
  if (isUVMMember(object)) return;

  const std::set<T*> unique(collection.cbegin(), collection.cend());
  if (unique.size() != collection.size()) {
    reportError(ErrorDefinition::INTEGRITY_CHECK_COLLECTION_HAS_DUPLICATES, object);
  }
}

inline IntegrityChecker::LineColumnRelation IntegrityChecker::getLineColumnRelation(uint32_t sl, uint16_t sc,
                                                                                    uint32_t el, uint16_t ec) const {
  if (sl == el) {
    if (sc < ec) return LineColumnRelation::Before;
    if (sc == ec) return LineColumnRelation::Inside;
    if (sc > ec) return LineColumnRelation::After;
  }

  return (sl < el) ? LineColumnRelation::Before : LineColumnRelation::After;
}

inline IntegrityChecker::LineColumnRelation IntegrityChecker::getLineColumnRelation(uint32_t l, uint16_t c, uint32_t sl,
                                                                                    uint16_t sc, uint32_t el,
                                                                                    uint16_t ec) const {
  if (l < sl) return LineColumnRelation::Before;
  if (l > el) return LineColumnRelation::After;

  if ((l == sl) && (c < sc)) return LineColumnRelation::Before;
  if ((l == el) && (c > ec)) return LineColumnRelation::After;

  return LineColumnRelation::Inside;
}

inline IntegrityChecker::LineColumnRelation IntegrityChecker::getLineColumnRelation(uint32_t csl, uint16_t csc,
                                                                                    uint32_t cel, uint16_t cec,
                                                                                    uint32_t psl, uint16_t psc,
                                                                                    uint32_t pel, uint16_t pec) const {
  if (cel < psl) return LineColumnRelation::Before;
  if (csl > pel) return LineColumnRelation::After;

  if ((csl == pel) && (csc >= pec)) return LineColumnRelation::After;
  if ((cel == psl) && (cec <= psc)) return LineColumnRelation::Before;

  const bool startIsInside = (csl > psl) || ((csl == psl) && (csc >= psc));
  const bool endIsInside = (cel < pel) || ((cel == pel) && (cec <= pec));
  if (startIsInside && endIsInside) return LineColumnRelation::Inside;

  return LineColumnRelation::Inconclusive;
}

std::set<const uhdm::PreprocMacroInstance*> IntegrityChecker::getMacroInstances(const uhdm::Any* object) const {
  std::pair<any_macro_instance_map_t::const_iterator, any_macro_instance_map_t::const_iterator> bounds =
      m_anyMacroInstance.equal_range(object);
  std::set<const uhdm::PreprocMacroInstance*> pmis;
  std::transform(bounds.first, bounds.second, std::inserter(pmis, pmis.end()),
                 [](any_macro_instance_map_t::const_reference& entry) { return entry.second; });
  return pmis;
}

void IntegrityChecker::reportInvalidLocation(const uhdm::Any* object) const {
  if ((object->getStartLine() == 0) && (object->getEndLine() == 0) && (object->getStartColumn() == 0) &&
      (object->getEndColumn() == 0))
    return;

  const uhdm::Any* const parent = object->getParent();
  if (parent == nullptr) return;
  if (parent->getUhdmType() == uhdm::UhdmType::Design) return;

  if ((parent->getStartLine() == 0) && (parent->getEndLine() == 0) && (parent->getStartColumn() == 0) &&
      (parent->getEndColumn() == 0))
    return;

  if (m_typesWithMissingLocation.find(object->getUhdmType()) != m_typesWithMissingLocation.cend()) return;

  // There are cases where things can be different files. e.g. PreprocTest
  if (object->getFile() != parent->getFile()) return;

  // Task body can be outside of the class definition itself!
  if ((object->getUhdmType() == uhdm::UhdmType::Task) && (parent->getUhdmType() == uhdm::UhdmType::ClassDefn)) return;

  // Function body can be outside of the class definition itself!
  if ((object->getUhdmType() == uhdm::UhdmType::Function) && (parent->getUhdmType() == uhdm::UhdmType::ClassDefn))
    return;

  // Function begin is implicit!
  if ((object->getUhdmType() == uhdm::UhdmType::Begin) && (parent->getUhdmType() == uhdm::UhdmType::Function)) return;

  // REVISIT(HS): Temporarily ignore some issues
  const uhdm::Any* p = object;
  while (p != nullptr) {
    if ((p->getUhdmType() == uhdm::UhdmType::BitSelect) || (p->getUhdmType() == uhdm::UhdmType::HierPath)) {
      return;
    }
    p = p->getParent();
  }

  const uint32_t csl = object->getStartLine();
  const uint32_t csc = object->getStartColumn();
  const uint32_t cel = object->getEndLine();
  const uint32_t cec = object->getEndColumn();

  LineColumnRelation actualRelation = getLineColumnRelation(csl, csc, cel, cec);
  if ((actualRelation != LineColumnRelation::Before) && (actualRelation != LineColumnRelation::Inside)) {
    reportError(ErrorDefinition::INTEGRITY_CHECK_INVALID_LOCATION, object);
    return;
  }

  const uint32_t psl = parent->getStartLine();
  const uint32_t psc = parent->getStartColumn();
  const uint32_t pel = parent->getEndLine();
  const uint32_t pec = parent->getEndColumn();

  actualRelation = getLineColumnRelation(psl, psc, pel, pec);
  if ((actualRelation != LineColumnRelation::Before) && (actualRelation != LineColumnRelation::Inside))
    // If parent location is known to be bad, don't bother reporting issues
    // with the child. Parent is already reported and so when the parent
    // gets fixed, the child becomes important.
    return;

  actualRelation = getLineColumnRelation(csl, csc, cel, cec, psl, psc, pel, pec);

  LineColumnRelation expectedRelation = LineColumnRelation::Inside;
  if (const uhdm::RefTypespec* const objectAsRefTypespec = object->Cast<uhdm::RefTypespec>()) {
    if (objectAsRefTypespec->getActual<uhdm::UnsupportedTypespec>() != nullptr) {
      // Ignore issues with UnsupportedTypespec.
      // There are known issues with genvar not followed with a type.
      return;
    }

    if ((parent->Cast<uhdm::Extends>() != nullptr) || (parent->Cast<uhdm::TFCall>() != nullptr)) {
      expectedRelation = LineColumnRelation::Inside;
    } else if (parent->Cast<uhdm::TypeParameter>() != nullptr) {
      expectedRelation = LineColumnRelation::After;
    } else {
      expectedRelation = LineColumnRelation::Before;
    }

    if (const uhdm::Function* const parentAsFunction = parent->Cast<uhdm::Function>()) {
      if (parentAsFunction->getReturn() == object) {
        expectedRelation = LineColumnRelation::Inside;
      }
    }

    if (const uhdm::EnumTypespec* const parentAsEnumTypespec = parent->Cast<uhdm::EnumTypespec>()) {
      if (parentAsEnumTypespec->getBaseTypespec() == object) {
        // typedef enum <base_type> { ... }
        expectedRelation = LineColumnRelation::Inside;
      }
    } else if (const uhdm::TaggedPattern* const parentAsTaggedPattern = parent->Cast<uhdm::TaggedPattern>()) {
      if (parentAsTaggedPattern->getTypespec() == object) {
        // BlahBlab: constant|operation
        expectedRelation = LineColumnRelation::Inside;
      }
    } else if (const uhdm::ArrayTypespec* const parentAsArrayTypespec = parent->Cast<uhdm::ArrayTypespec>()) {
      if ((parentAsArrayTypespec->getElemTypespec() == object) && parentAsArrayTypespec->getPacked()) {
        // elem_type [0:n] var_name
        expectedRelation = LineColumnRelation::Before;
      }
    } else if (const uhdm::Variable* const parentAsVariable = parent->Cast<uhdm::Variable>()) {
      if (parentAsVariable->getTypespec() == object) {
        // elem_type var_name[range]
        // For arrayVar/Typespec, the range is the location
        if (const uhdm::RefTypespec* const rt = parentAsVariable->getTypespec()) {
          if (const uhdm::ArrayTypespec* const t = rt->getActual<uhdm::ArrayTypespec>()) {
            if (t->getPacked()) {
              // elem_type [0:n] var_name
              expectedRelation = LineColumnRelation::Before;
            } else {
              // <var_name> <ranges> = ....
              // ArrayVar::typespec refers to the ranges
              expectedRelation = LineColumnRelation::After;
            }
            if (t->getArrayType() == vpiQueueArray) {
              // In the case of declaration, i.e. <type> <var_name[$] it is
              // 'After' In the case of assignment to empty queue, it is
              // overlapping i.e
              //    <var_name> = {}
              expectedRelation = ((csl == psl) && (csc == psc) && (cel == pel) && (cec == pec))
                                     ? LineColumnRelation::Inside
                                     : LineColumnRelation::After;
            }
          }
        }
        // if (parentAsArrayVar->getArrayType() == vpiQueueArray) {
        //   // In the case of declaration, i.e. <type> <var_name[$] it is
        //   'After'
        //   // In the case of assignment to empty queue, it is overlapping i.e
        //   //    <var_name> = {}
        //   expectedRelation =
        //       ((csl == psl) && (csc == psc) && (cel == pel) && (cec == pec))
        //           ? LineColumnRelation::Inside
        //           : LineColumnRelation::After;
        // }
        // else {
        //   // <var_name> <ranges> = ....
        //   // ArrayVar::typespec refers to the ranges
        //   expectedRelation = LineColumnRelation::After;
        // }
      }
    } else if (const uhdm::ArrayTypespec* const parentAsArrayTypespec = parent->Cast<uhdm::ArrayTypespec>()) {
      if (parentAsArrayTypespec->getIndexTypespec() == object) {
        // Since array_typspec refers to the range, index is basically the
        // range in case of associative arrays, queues, and dynamic arrays.
        expectedRelation = LineColumnRelation::After;
      } else if (parentAsArrayTypespec->getElemTypespec() == object) {
        expectedRelation = LineColumnRelation::Before;
      }
    } else if (const uhdm::IODecl* const parentAsIODecl = parent->Cast<uhdm::IODecl>()) {
      if (parentAsIODecl->getTypespec() == object) {
        // In old verilog style, the parameter declaration is different!
        // task MYHDL3_adv;
        //   input width;
        //   integer width;
        // begin: MYHDL82_RETURN
        // end
        if (actualRelation == LineColumnRelation::After) expectedRelation = LineColumnRelation::After;
      }

      if (parent->Cast<uhdm::TypedefTypespec>() != nullptr) {
        if (objectAsRefTypespec->getActual() == nullptr) {
          expectedRelation = LineColumnRelation::Inside;
        }
      }
    } else if (parent->Cast<uhdm::Variable>() != nullptr) {
      expectedRelation = LineColumnRelation::Before;
    }
  } else if (any_cast<uhdm::RefObj>(object) != nullptr) {
    if (const uhdm::BitSelect* const parentAsBitSelect = any_cast<uhdm::BitSelect>(parent)) {
      if (parentAsBitSelect->getIndex() == object) {
        expectedRelation = LineColumnRelation::After;
      }
    }
  } else if (object->getUhdmType() == uhdm::UhdmType::Range) {
    if (const uhdm::ArrayTypespec* const parentAsArrayTypespec = parent->Cast<uhdm::ArrayTypespec>()) {
      if (isValidName(parentAsArrayTypespec)) {
        // typedef int var_name[range];
        expectedRelation = LineColumnRelation::After;
      }
    } else if (parent->Cast<uhdm::IODecl>() != nullptr) {
      // (int var_name[range])
      expectedRelation = LineColumnRelation::After;
    } else if (parent->Cast<uhdm::ModuleArray>() != nullptr) {
      // (module_type var_name[range])
      expectedRelation = LineColumnRelation::After;
    } else if (const uhdm::Variable* const parentAsVariable = parent->Cast<uhdm::Variable>()) {
      if (const uhdm::RefTypespec* const rt = parentAsVariable->getTypespec()) {
        if (rt->getActual<uhdm::ArrayTypespec>() != nullptr) {
          if (rt->getActual<uhdm::ArrayTypespec>()->getPacked()) {
            // logic [range] var_name
            expectedRelation = LineColumnRelation::Before;
          } else {
            // int var_name[range]
            expectedRelation = LineColumnRelation::After;
          }
        } else if (rt->getActual<uhdm::LogicTypespec>() != nullptr) {
          // logic [range] var_name
          expectedRelation = LineColumnRelation::Before;
        }
      }
    }
  } else if (object->getUhdmType() == uhdm::UhdmType::Attribute) {
    expectedRelation = LineColumnRelation::Before;
    if (parent->Cast<uhdm::Instance>() != nullptr) {
      // (* uhdm::Attribute*) class <class_name>;
      // (* uhdm::Attribute*) module <module_name>;
      // (* uhdm::Attribute*) interface <interface_name>;
      // (* uhdm::Attribute*) package <package_name>;
      // (* uhdm::Attribute*) program <program_name>;
      expectedRelation = LineColumnRelation::Before;
    } else if (parent->Cast<uhdm::Net>() != nullptr) {
      // (* uhdm::Attribute*) input <logic_name>;
      // (* uhdm::Attribute*) output <logic_name>;
      expectedRelation = LineColumnRelation::Before;
    } else if (parent->Cast<uhdm::Port>() != nullptr) {
      // (* uhdm::Attribute*) input <port_name>
      // (* uhdm::Attribute*) output <port_name>
      expectedRelation = LineColumnRelation::Before;
    } else if (parent->Cast<uhdm::Primitive>() != nullptr) {
      // (* uhdm::Attribute*) primitive primitive_name;
      expectedRelation = LineColumnRelation::Before;
    }
  } else if (object->getUhdmType() == uhdm::UhdmType::SeqFormalDecl) {
    if (const uhdm::LetDecl* const parentAsLetDecl = parent->Cast<uhdm::LetDecl>()) {
      if (const uhdm::SeqFormalDeclCollection* const decls = parentAsLetDecl->getSeqFormalDecls()) {
        for (const uhdm::SeqFormalDecl* const decl : *decls) {
          if (decl == object) {
            // let <name>(<..., decl, ...>) = <object>
            expectedRelation = LineColumnRelation::After;
            break;
          }
        }
      }
    }
  } else if (object->getUhdmType() == uhdm::UhdmType::Port) {
    if ((parent->Cast<uhdm::RefModule>() != nullptr) || (parent->Cast<uhdm::ModuleArray>() != nullptr)) {
      // module_type module_name(..., port, ...)
      expectedRelation = LineColumnRelation::After;
    }
  }

  if (const uhdm::EventControl* parentAsEventControl = parent->Cast<uhdm::EventControl>()) {
    if (parentAsEventControl->getStmt() == object) {
      // always @(....) begin ... end
      expectedRelation = LineColumnRelation::After;
    }
  } else if (const uhdm::IODecl* const parentAsIODecl = parent->Cast<uhdm::IODecl>()) {
    if (parentAsIODecl->getExpr() == object) {
      // io_decl::expr represent the default value which is
      // on the right of the variable!
      expectedRelation = LineColumnRelation::After;
    }
  } else if (const uhdm::Variable* parentAsVariable = parent->Cast<uhdm::Variable>()) {
    if (parentAsVariable->getExpr() == object) {
      expectedRelation = LineColumnRelation::After;
    }
  } else if (const uhdm::Ports* const parentAsPorts = parent->Cast<uhdm::Port>()) {
    if (parentAsPorts->getHighConn() == object) {
      // module modname(..., .port(high_conn), ... )  <= After
      // module modname(..., port, ... )  <= Inside
      if (actualRelation == LineColumnRelation::After) {
        expectedRelation = LineColumnRelation::After;
      }
    }
  } else if (const uhdm::LetDecl* const parentAsLetDecl = parent->Cast<uhdm::LetDecl>()) {
    if (const uhdm::ExprCollection* const exprs = parentAsLetDecl->getExprs()) {
      for (const uhdm::Expr* const expr : *exprs) {
        if (expr == object) {
          // let <name>(<args>) = <object>
          expectedRelation = LineColumnRelation::After;
          break;
        }
      }
    }
  } else if (const uhdm::MethodFuncCall* const parentAsMethodFuncCall = parent->Cast<uhdm::MethodFuncCall>()) {
    if (parentAsMethodFuncCall->getPrefix() == object) {
      expectedRelation = LineColumnRelation::Before;
    }
  }

  if (actualRelation != expectedRelation) {
    bool isPackedArray = false;
    if (const uhdm::ArrayTypespec* const at = uhdm::getTypespec<uhdm::ArrayTypespec>(parent)) {
      isPackedArray = at->getPacked();
    }

    const bool isLogicNet = uhdm::getTypespec<uhdm::LogicTypespec>(parent) != nullptr;

    if ((actualRelation == LineColumnRelation::After) && (expectedRelation == LineColumnRelation::Before) &&
        (object->getUhdmType() == uhdm::UhdmType::RefTypespec) &&
        ((parent->getUhdmType() == uhdm::UhdmType::Port) || isLogicNet || isPackedArray)) {
      // typespec for uhdm::Ports*can* be inside the parent module!
      // module (port_name):
      //   input int port_name;
      // endmodule
      const uhdm::Any* const grandParent = parent->getParent();

      const uint32_t psl = grandParent->getStartLine();
      const uint32_t psc = grandParent->getStartColumn();
      const uint32_t pel = grandParent->getEndLine();
      const uint32_t pec = grandParent->getEndColumn();

      actualRelation = getLineColumnRelation(csl, csc, cel, cec, psl, psc, pel, pec);
      expectedRelation = LineColumnRelation::Inside;
    } else if ((actualRelation == LineColumnRelation::Inside) && (expectedRelation == LineColumnRelation::After) &&
               (parent->getUhdmType() == uhdm::UhdmType::Port)) {
      // unnamed port arguments for ref_module
      // module_type module_name(..., port, ...)

      if ((csl == psl) && (csc == psc) && (cel == pel) && (cec == pec)) {
        if (const uhdm::Port* const parentAsPort = parent->Cast<uhdm::Port>()) {
          if (parentAsPort->getHighConn() == object) {
            expectedRelation = LineColumnRelation::Inside;
          }
        }
      }
    }
  }

  if (actualRelation != expectedRelation) {
    const std::set<const uhdm::PreprocMacroInstance*> objectPMIs = getMacroInstances(object);
    const std::set<const uhdm::PreprocMacroInstance*> parentPMIs = getMacroInstances(parent);
    if (objectPMIs == parentPMIs) {
      reportError(ErrorDefinition::INTEGRITY_CHECK_BAD_RELATIVE_LOCATION, object);
    }
  }
}

void IntegrityChecker::reportMissingLocation(const uhdm::Any* object) const {
  if (m_reportMissingLocation) {
    if (object->getParent() && (object->getParent()->getUhdmType() == uhdm::UhdmType::ImportTypespec)) return;
    reportError(ErrorDefinition::INTEGRITY_CHECK_MISSING_LOCATION, object);
  }

  /*
    if ((object->getStartLine() != 0) && (object->getStartColumn() != 0) &&
        (object->getEndLine() != 0) && (object->getEndColumn() != 0))
      return;

    if (m_typesWithMissingLocation.find(object->getUhdmType()) !=
        m_typesWithMissingLocation.cend())
      return;

    const uhdm::Any* const parent = object->getParent();
    if (parent == nullptr) return;

    const uhdm::Any* const grandParent = parent->getParent();
    if (grandParent == nullptr) return;

    // Identifiers don't have position information yet!
    if (object->getUhdmType() == uhdm::UhdmType::Identifier) return;

    // Typespecs are handled explicitly by reportInvalidTypespecLocation
    if (any_cast<uhdm::Typespec>(object) != nullptr) return;

    // begin in function body are implicit!
    if ((object->getUhdmType() == uhdm::UhdmType::Begin) &&
        (parent->getUhdmType() == uhdm::UhdmType::Function))
      return;

    if ((object->getUhdmType() == uhdm::UhdmType::RefTypespec) &&
        (grandParent->getName() == "new") &&
        (parent->Cast<uhdm::Variables>() != nullptr) &&
        (grandParent->getUhdmType() == uhdm::UhdmType::Function)) {
      // For refTypespec associated with a class's constructor return value
      // there is no legal position because the "new" operator's return value
      // is implicit.
      const uhdm::Variables* const parentAsVariables =
          parent->Cast<uhdm::Variables>();
      const uhdm::TaskFunc* const grandParentAsTaskFunc =
          grandParent->Cast<uhdm::Function>();
      if ((grandParentAsTaskFunc->getReturn() == parent) &&
          (parentAsVariables->getTypespec() == object)) {
        return;
      }
    } else if ((object->getUhdmType() == uhdm::UhdmType::ClassTypespec) &&
               (parent->getName() == "new") &&
               (parent->getUhdmType() == uhdm::UhdmType::Function)) {
      // For typespec associated with a class's constructor return value
      // there is no legal position because the "new" operator's return value
      // is implicit.
      const uhdm::Function* const parentAsFunction =
          parent->Cast<uhdm::Function>();
      if (const uhdm::RefTypespec* const rt = parentAsFunction->getReturn()) {
        if ((rt == object) || (rt->getActual() == object)) {
          return;
        }
      }
    } else if (object->Cast<uhdm::Variables>() != nullptr) {
      // When no explicit return is specific, the function's name
      // is consdiered the return type's name.
      if (const uhdm::TaskFunc* const parentAsTaskFunc =
              parent->Cast<uhdm::TaskFunc>()) {
        if (parentAsTaskFunc->getReturn() == object) return;
      }
    } else if (const uhdm::Constant* const objectAsConstant =
                   object->Cast<uhdm::Constant>()) {
      if (const uhdm::Range* const parentAsRange =
    parent->Cast<uhdm::Range>())
    {
        // The left expression of range is allowed to be zero.
        if (parentAsRange->getLeftExpr() == object) return;

        // The right is allowed to be zero if it's associative
        if ((parentAsRange->getRightExpr() == object) &&
            (objectAsConstant->getValue() == "associative")) {
          return;
        }
      }
    }
  */
}

bool IntegrityChecker::isImplicitFunctionReturnType(const uhdm::RefTypespec* object) {
  if (const uhdm::Function* const f = object->getParent<uhdm::Function>()) {
    if ((f->getReturn() == object) && object->getName().empty()) return true;
  }
  return false;
}

std::string_view IntegrityChecker::stripDecorations(std::string_view name) {
  while (!name.empty() && name.back() == ':') name.remove_suffix(1);

  size_t pos1 = name.rfind("::");
  if (pos1 != std::string::npos) name = name.substr(pos1 + 2);

  size_t pos2 = name.rfind('.');
  if (pos2 != std::string::npos) name = name.substr(pos2 + 1);

  size_t pos3 = name.rfind('@');
  if (pos3 != std::string::npos) name = name.substr(pos3 + 1);

  return name;
}

bool IntegrityChecker::areNamedSame(const uhdm::Any* object, const uhdm::Any* actual) {
  std::string_view objectName = stripDecorations(object->getName());
  std::string_view actualName = stripDecorations(actual->getName());
  return (objectName == actualName);
}

void IntegrityChecker::reportInvalidName(const uhdm::Any* object) const {
  if (m_reportInvalidName) {
    reportError(ErrorDefinition::INTEGRITY_CHECK_INVALID_NAME, object);
  }
}

void IntegrityChecker::reportMissingName(const uhdm::Any* object) const {
  if (m_reportMissingName) {
    reportError(ErrorDefinition::INTEGRITY_CHECK_MISSING_NAME, object);
  }
  /*
    // Implicit function return type are unnammed.
    if (isImplicitFunctionReturnType(object)) return;

    bool shouldReport = false;

    if (const uhdm::RefObj* const objectAsRefObj =
    object->Cast<uhdm::RefObj>()) { shouldReport = object->getName().empty();
    shouldReport = shouldReport ||
    (object->getName().find(SymbolTable::getBadSymbol()) !=
                           std::string_view::npos);

      if (const uhdm::Any* const actual = objectAsRefObj->getActual()) {
        shouldReport = shouldReport || !areNamedSame(object, actual);
        shouldReport = shouldReport && !isImplicitFunctionReturnType(actual);
      }

      shouldReport = shouldReport && (object->getName() != "super");
      shouldReport = shouldReport && (object->getName() != "this");
    } else if (const uhdm::RefTypespec* const objectAsRefTypespec =
                   object->Cast<const uhdm::RefTypespec*>()) {
      shouldReport = (object->getName().find(SymbolTable::getBadSymbol()) !=
                      std::string_view::npos);
      if (const uhdm::TypedefTypespec* const parent =
              object->getParent<uhdm::TypedefTypespec>()) {
        if (parent->getTypedefAlias() != nullptr) {
          shouldReport = false;
        }
      } else if (const uhdm::Any* actual = objectAsRefTypespec->getActual()) {
        if ((actual->getUhdmType() == uhdm::UhdmType::ArrayTypespec) ||
            (actual->getUhdmType() == uhdm::UhdmType::BitTypespec) ||
            (actual->getUhdmType() == uhdm::UhdmType::ByteTypespec) ||
            (actual->getUhdmType() == uhdm::UhdmType::ChandleTypespec) ||
            (actual->getUhdmType() == uhdm::UhdmType::IntTypespec) ||
            (actual->getUhdmType() == uhdm::UhdmType::IntegerTypespec) ||
            (actual->getUhdmType() == uhdm::UhdmType::LogicTypespec) ||
            (actual->getUhdmType() == uhdm::UhdmType::LongIntTypespec) ||
            (actual->getUhdmType() == uhdm::UhdmType::PackedArrayTypespec) ||
            (actual->getUhdmType() == uhdm::UhdmType::RealTypespec) ||
            (actual->getUhdmType() == uhdm::UhdmType::ShortIntTypespec) ||
            (actual->getUhdmType() == uhdm::UhdmType::ShortRealTypespec) ||
            (actual->getUhdmType() == uhdm::UhdmType::StringTypespec) ||
            (actual->getUhdmType() == uhdm::UhdmType::TimeTypespec) ||
            (actual->getUhdmType() == uhdm::UhdmType::VoidTypespec)) {
          shouldReport = false;
        } else if ((actual->getUhdmType() == uhdm::UhdmType::EnumTypespec) ||
                   (actual->getUhdmType() == uhdm::UhdmType::StructTypespec)
    || (actual->getUhdmType() == uhdm::UhdmType::UnionTypespec)) {
          shouldReport = false;
        } else if ((actual->getUhdmType() == uhdm::UhdmType::ClassTypespec) ||
                   (actual->getUhdmType() ==
    uhdm::UhdmType::InterfaceTypespec)
    || (actual->getUhdmType() == uhdm::UhdmType::ModuleTypespec) ||
                   (actual->getUhdmType() ==
                    uhdm::UhdmType::UnsupportedTypespec)) {
          shouldReport = shouldReport || object->getName().empty();
          if (object->getName() != "item") {
            shouldReport = shouldReport || !areNamedSame(object, actual);
          }
        }
      } else {
        shouldReport = shouldReport || object->getName().empty();
      }
    }

    if (shouldReport) {
      reportError(ErrorDefinition::INTEGRITY_CHECK_MISSING_NAME, object);
    }
  */
}

void IntegrityChecker::reportMissingFile(const uhdm::Any* object) const {
  if (m_reportMissingFile) {
    if (object->getParent() && (object->getParent()->getUhdmType() == uhdm::UhdmType::ImportTypespec)) return;
    reportError(ErrorDefinition::INTEGRITY_CHECK_MISSING_FILE, object);
  }
}

void IntegrityChecker::reportMissingParent(const uhdm::Any* object) const {
  if (m_reportMissingParent) {
    reportError(ErrorDefinition::INTEGRITY_CHECK_MISSING_PARENT, object);
  }
}

void IntegrityChecker::reportNullActual(const uhdm::Any* object) const {
  if (m_reportNullActual) {
    reportError(ErrorDefinition::INTEGRITY_CHECK_NULL_ACTUAL, object);
  }
  /*
  if (isBuiltInPackageOnStack(object)) return;

  bool shouldReport = false;

  if (const uhdm::RefObj* const objectAsRefObj = object->Cast<uhdm::RefObj>())
  { shouldReport = objectAsRefObj->getActual() == nullptr;
    // Special case for $root and few others
    if (const uhdm::Any* const parent = object->getParent()) {
      shouldReport =
          shouldReport &&
          !(((object->getName() == "$root") || (object->getName() == "size")
  || (object->getName() == "delete")) && (parent->getUhdmType() ==
  uhdm::UhdmType::HierPath)); shouldReport = shouldReport &&
                     !((parent->getUhdmType() == uhdm::UhdmType::SysFuncCall)
  && (parent->getName() == "$bits")); shouldReport = shouldReport &&
  (object->getName() != "default");
    }
  } else if (const uhdm::RefTypespec* const objectAsRefTypespec =
                 object->Cast<const uhdm::RefTypespec*>()) {
    if (const uhdm::TypedefTypespec* const parent =
            object->getParent<uhdm::TypedefTypespec>()) {
      if (parent->getTypedefAlias() != nullptr) {
        shouldReport = false;
      }
    } else {
      shouldReport = objectAsRefTypespec->getActual() == nullptr;
    }
  } else if (const uhdm::RefModule* const objectAsRefModule =
                 object->Cast<const uhdm::RefModule*>()) {
    shouldReport = objectAsRefModule->getActual() == nullptr;
  } else if (const uhdm::ChandleVar* const objectAsChandleVar =
                 object->Cast<const uhdm::ChandleVar*>()) {
    shouldReport = objectAsChandleVar->getActual() == nullptr;
  } else if (const uhdm::TaskFunc* const parentAsTaskFunc =
                 object->getParent<uhdm::TaskFunc>()) {
    if ((parentAsTaskFunc->getReturn() == object) &&
        (parentAsTaskFunc->getAccessType() == vpiDPIImportAcc)) {
      // Imported functions cannot be bound!
      shouldReport = false;
    }
  }
*/
}

void IntegrityChecker::reportNullTypespec(const uhdm::Any* object) const {
  if (m_reportNullTypespec) {
    reportError(ErrorDefinition::INTEGRITY_CHECK_NULL_TYPESPEC, object);
  }
}

void IntegrityChecker::reportUnsupportedTypespec(const uhdm::Any* object) const {
  if (m_reportUnsupportedTypespec) {
    reportError(ErrorDefinition::INTEGRITY_CHECK_UNSUPPORTED_TYPESPEC, object);
  }
}

void IntegrityChecker::reportInvalidForeachVariable(const uhdm::Any* object) const {
  if (m_reportInvalidForeachVariable) {
    reportError(ErrorDefinition::INTEGRITY_CHECK_INVALID_FOREACH_VARIABLE, object);
  }
}

void IntegrityChecker::reportMissingConstantTypespec(const uhdm::Any* object) const {
  if (m_reportMissingConstantTypespec) {
    reportError(ErrorDefinition::INTEGRITY_CHECK_INVALID_CONST_TPS, object);
  }
}

void IntegrityChecker::reportInvalidTypespecLocation(const uhdm::Any* object) {
  if (const uhdm::Typespec* const t = any_cast<uhdm::Typespec>(object)) {
    bool shouldReport = false;
    if ((any_cast<uhdm::ChandleTypespec>(t) != nullptr) || (any_cast<uhdm::EnumTypespec>(t) != nullptr) ||
        (any_cast<uhdm::ImportTypespec>(t) != nullptr) || (any_cast<uhdm::InterfaceTypespec>(t) != nullptr) ||
        (any_cast<uhdm::StructTypespec>(t) != nullptr) || (any_cast<uhdm::TypedefTypespec>(t) != nullptr) ||
        (any_cast<uhdm::TypeParameter>(t) != nullptr) || (any_cast<uhdm::UnionTypespec>(t) != nullptr)) {
      if ((t->getStartLine() == 0) || (t->getEndLine() == 0) || (t->getStartColumn() == 0) ||
          (t->getEndColumn() == 0)) {
        shouldReport = true;
      }
    } else if ((t->getStartLine() != 0) || (t->getEndLine() != 0) || (t->getStartColumn() != 0) ||
               (t->getEndColumn() != 0)) {
      shouldReport = true;
    }
    if (shouldReport) {
      reportError(ErrorDefinition::INTEGRITY_CHECK_INVALID_LOCATION, object);
    }
  }
}

void IntegrityChecker::visitAny2(const uhdm::Any* object) {
  if (m_visited.find(object) != m_visited.cend()) return;
  if (isUVMMember(object)) return;

  reportNullActual(object);
  reportInvalidTypespecLocation(object);

  // Known Issues!
  if (const uhdm::IntTypespec* const t = any_cast<uhdm::IntTypespec>(object)) {
    if (const uhdm::Expr* const e = t->getExpr()) {
      m_visited.emplace(e);
    }
  } else if (const uhdm::Operation* const op = any_cast<uhdm::Operation>(object)) {
    if (op->getOpType() == vpiCastOp) {
      if (const uhdm::RefTypespec* const rt = op->getTypespec()) {
        if (const uhdm::IntTypespec* const t = rt->getActual<uhdm::IntTypespec>()) {
          if (const uhdm::Expr* const e = t->getExpr()) {
            m_visited.emplace(e);
          }
        }
      }
    }
  }

  reportMissingLocation(object);
  reportMissingName(object);
  reportMissingFile(object);

  const uhdm::Any* const parent = object->getParent();
  if ((object->getUhdmType() != uhdm::UhdmType::Design) && (parent == nullptr)) {
    reportError(ErrorDefinition::INTEGRITY_CHECK_MISSING_PARENT, object);
    return;
  }

  reportInvalidLocation(object);

  const uhdm::Scope* const parentAsScope = any_cast<uhdm::Scope>(parent);
  const uhdm::Design* const parentAsDesign = any_cast<uhdm::Design>(parent);
  const uhdm::UdpDefn* const parentAsUdpDefn = any_cast<uhdm::UdpDefn>(parent);

  const std::set<uhdm::UhdmType> allowedScopeChildren{
      uhdm::UhdmType::ArrayTypespec,
      uhdm::UhdmType::Assert,
      uhdm::UhdmType::Assume,
      uhdm::UhdmType::BitTypespec,
      uhdm::UhdmType::ByteTypespec,
      uhdm::UhdmType::ChandleTypespec,
      uhdm::UhdmType::ClassTypespec,
      uhdm::UhdmType::ConcurrentAssertions,
      uhdm::UhdmType::Cover,
      uhdm::UhdmType::EnumTypespec,
      uhdm::UhdmType::EventTypespec,
      uhdm::UhdmType::ImportTypespec,
      uhdm::UhdmType::IntTypespec,
      uhdm::UhdmType::IntegerTypespec,
      uhdm::UhdmType::InterfaceTypespec,
      uhdm::UhdmType::LetDecl,
      uhdm::UhdmType::LogicTypespec,
      uhdm::UhdmType::LongIntTypespec,
      uhdm::UhdmType::ModuleTypespec,
      uhdm::UhdmType::NamedEvent,
      uhdm::UhdmType::NamedEventArray,
      uhdm::UhdmType::Net,
      uhdm::UhdmType::ParamAssign,
      uhdm::UhdmType::Parameter,
      uhdm::UhdmType::PropertyDecl,
      uhdm::UhdmType::PropertyTypespec,
      uhdm::UhdmType::RealTypespec,
      uhdm::UhdmType::Restrict,
      uhdm::UhdmType::SequenceDecl,
      uhdm::UhdmType::SequenceTypespec,
      uhdm::UhdmType::ShortIntTypespec,
      uhdm::UhdmType::ShortRealTypespec,
      uhdm::UhdmType::StringTypespec,
      uhdm::UhdmType::StructTypespec,
      uhdm::UhdmType::TimeTypespec,
      uhdm::UhdmType::TypeParameter,
      uhdm::UhdmType::UnionTypespec,
      uhdm::UhdmType::UnsupportedTypespec,
      uhdm::UhdmType::Variable,
      uhdm::UhdmType::VoidTypespec,
  };

  bool expectScope = (allowedScopeChildren.find(object->getUhdmType()) != allowedScopeChildren.cend());
  if (any_cast<uhdm::Begin>(object) != nullptr) {
    expectScope = false;
  }

  const std::set<uhdm::UhdmType> allowedDesignChildren{
      uhdm::UhdmType::Package,  uhdm::UhdmType::Module,    uhdm::UhdmType::ClassDefn,
      uhdm::UhdmType::Typespec, uhdm::UhdmType::LetDecl,   uhdm::UhdmType::Function,
      uhdm::UhdmType::Task,     uhdm::UhdmType::Parameter, uhdm::UhdmType::ParamAssign};
  bool expectDesign = (allowedDesignChildren.find(object->getUhdmType()) != allowedDesignChildren.cend());

  if (any_cast<uhdm::ParamAssign>(object) != nullptr) {
    if (any_cast<uhdm::ClassTypespec>(parent) != nullptr) {
      expectScope = expectDesign = false;
    }
  }

  const std::set<uhdm::UhdmType> allowedUdpChildren{uhdm::UhdmType::Net, uhdm::UhdmType::IODecl,
                                                    uhdm::UhdmType::TableEntry};
  bool expectUdpDefn = (allowedUdpChildren.find(object->getUhdmType()) != allowedUdpChildren.cend());

  // if ((object->getUhdmType() != uhdm::UhdmType::Design) &&
  //     (m_callstack.back() != object->getParent()) &&
  //     (m_callstack.back()->getUhdmType() !=
  //      uhdm::UhdmType::PreprocMacroInstance) &&
  //     ((object->getUhdmType() == uhdm::UhdmType::RefObj) ||
  //      (object->getUhdmType() == uhdm::UhdmType::RefTypespec))) {
  //   Location loc(
  //       fileSystem->toPathId(object->getFile(), symbolTable),
  //       object->getStartLine(), object->getStartColumn(),
  //       symbolTable->registerSymbol(std::to_string(object->getUhdmId())));
  //   errorContainer->addError(ErrorDefinition::INTEGRITY_CHECK_INVALID_REFPARENT,
  //                            loc);
  // }

  if ((any_cast<uhdm::IODecl>(object) != nullptr) && (any_cast<uhdm::Modport>(parent) != nullptr)) {
    expectScope = expectDesign = expectUdpDefn = false;
  }

  if ((parentAsScope == nullptr) && (parentAsDesign == nullptr) && (parentAsUdpDefn == nullptr) &&
      (expectScope || expectDesign || expectUdpDefn)) {
    reportError(ErrorDefinition::INTEGRITY_CHECK_PARENT_IS_NEITHER_SCOPE_NOR_DESIGN, object);
  }
}

void IntegrityChecker::visitAny(const uhdm::Any* object) {
  if (m_visited.find(object) != m_visited.cend()) return;

  const uhdm::Any* const parent = object->getParent();
  if ((parent == nullptr) && (object->getUhdmType() != uhdm::UhdmType::Design)) {
    reportMissingParent(object);
    return;
  }

  const uhdm::Any* parentPackage = object;
  while (parentPackage != nullptr) {
    if ((parentPackage->getUhdmType() == uhdm::UhdmType::Package) && (parentPackage->getName() == "builtin")) {
      m_visited.emplace(object);
      return;
    }
    parentPackage = parentPackage->getParent();
  }

  uhdm::UhdmType uhdmType = object->getUhdmType();

  if (!isValidName(object) && (m_typesWithValidName.find(uhdmType) != m_typesWithValidName.cend())) {
    reportMissingName(object);
  }

  if (!isValidFile(object) && (m_typesWithMissingFile.find(uhdmType) == m_typesWithMissingFile.cend())) {
    reportMissingFile(object);
  }

  if ((object->getParent() == nullptr) &&
      (m_typesWithMissingParent.find(uhdmType) == m_typesWithMissingParent.cend())) {
    reportMissingParent(object);
  }

  if (!isValidLocation(object) && (m_typesWithMissingLocation.find(uhdmType) == m_typesWithMissingLocation.cend())) {
    reportMissingLocation(object);
  }

  const uhdm::Scope* const parentAsScope = object->getParent<uhdm::Scope>();
  const uhdm::Design* const parentAsDesign = object->getParent<uhdm::Design>();
  if ((parentAsScope == nullptr) && (parentAsDesign == nullptr)) {
    if ((any_cast<uhdm::Typespec>(object) != nullptr) || (any_cast<uhdm::Variable>(object) != nullptr)) {
      reportError(ErrorDefinition::INTEGRITY_CHECK_PARENT_IS_NEITHER_SCOPE_NOR_DESIGN, object);
    }
  }
}
void IntegrityChecker::visitAlias(const uhdm::Alias* object) {}
void IntegrityChecker::visitAlways(const uhdm::Always* object) {}
void IntegrityChecker::visitAnyPattern(const uhdm::AnyPattern* object) {}
void IntegrityChecker::visitArrayExpr(const uhdm::ArrayExpr* object) {}
void IntegrityChecker::visitArrayTypespec(const uhdm::ArrayTypespec* object) {}
void IntegrityChecker::visitAssert(const uhdm::Assert* object) {}
void IntegrityChecker::visitAssignStmt(const uhdm::AssignStmt* object) {}
void IntegrityChecker::visitAssignment(const uhdm::Assignment* object) {}
void IntegrityChecker::visitAssume(const uhdm::Assume* object) {}
void IntegrityChecker::visitAttribute(const uhdm::Attribute* object) {}
void IntegrityChecker::visitBegin(const uhdm::Begin* object) {
  const uhdm::Any* const parent = object->getParent();
  if (parent == nullptr) return;

  if (any_cast<uhdm::TaskFunc>(parent) == nullptr) {
    if (!isValidLocation(object)) {
      reportMissingLocation(object);
    }
    if (!isValidFile(object)) {
      reportMissingFile(object);
    }
  }
}
void IntegrityChecker::visitBitSelect(const uhdm::BitSelect* object) {
  if (object->getActual() == nullptr) reportNullActual(object);
}
void IntegrityChecker::visitBitTypespec(const uhdm::BitTypespec* object) {}
void IntegrityChecker::visitBreakStmt(const uhdm::BreakStmt* object) {}
void IntegrityChecker::visitByteTypespec(const uhdm::ByteTypespec* object) {}
void IntegrityChecker::visitCaseItem(const uhdm::CaseItem* object) {}
void IntegrityChecker::visitCaseProperty(const uhdm::CaseProperty* object) {}
void IntegrityChecker::visitCasePropertyItem(const uhdm::CasePropertyItem* object) {}
void IntegrityChecker::visitCaseStmt(const uhdm::CaseStmt* object) {}
void IntegrityChecker::visitChandleTypespec(const uhdm::ChandleTypespec* object) {}
void IntegrityChecker::visitCheckerDecl(const uhdm::CheckerDecl* object) {}
void IntegrityChecker::visitCheckerInst(const uhdm::CheckerInst* object) {}
void IntegrityChecker::visitCheckerInstPort(const uhdm::CheckerInstPort* object) {}
void IntegrityChecker::visitCheckerPort(const uhdm::CheckerPort* object) {}
void IntegrityChecker::visitClassDefn(const uhdm::ClassDefn* object) {}
void IntegrityChecker::visitClassObj(const uhdm::ClassObj* object) {}
void IntegrityChecker::visitClassTypespec(const uhdm::ClassTypespec* object) {}
void IntegrityChecker::visitClockedProperty(const uhdm::ClockedProperty* object) {}
void IntegrityChecker::visitClockedSeq(const uhdm::ClockedSeq* object) {}
void IntegrityChecker::visitClockingBlock(const uhdm::ClockingBlock* object) {}
void IntegrityChecker::visitClockingIODecl(const uhdm::ClockingIODecl* object) {}
void IntegrityChecker::visitConstant(const uhdm::Constant* object) {
  bool report = true;
  if (const uhdm::RefTypespec* const rt = object->getTypespec()) {
    if (rt->getActual() != nullptr) {
      report = false;
    }
  }
  if (report) {
    reportMissingConstantTypespec(object);
  }
}
void IntegrityChecker::visitConstrForeach(const uhdm::ConstrForeach* object) {}
void IntegrityChecker::visitConstrIf(const uhdm::ConstrIf* object) {}
void IntegrityChecker::visitConstrIfElse(const uhdm::ConstrIfElse* object) {}
void IntegrityChecker::visitConstraint(const uhdm::Constraint* object) {}
void IntegrityChecker::visitConstraintOrdering(const uhdm::ConstraintOrdering* object) {}
void IntegrityChecker::visitContAssign(const uhdm::ContAssign* object) {}
void IntegrityChecker::visitContAssignBit(const uhdm::ContAssignBit* object) {}
void IntegrityChecker::visitContinueStmt(const uhdm::ContinueStmt* object) {}
void IntegrityChecker::visitCover(const uhdm::Cover* object) {}
void IntegrityChecker::visitDeassign(const uhdm::Deassign* object) {}
void IntegrityChecker::visitDefParam(const uhdm::DefParam* object) {}
void IntegrityChecker::visitDelayControl(const uhdm::DelayControl* object) {}
void IntegrityChecker::visitDelayTerm(const uhdm::DelayTerm* object) {}
void IntegrityChecker::visitDesign(const uhdm::Design* object) {}
void IntegrityChecker::visitDisable(const uhdm::Disable* object) {}
void IntegrityChecker::visitDisableFork(const uhdm::DisableFork* object) {}
void IntegrityChecker::visitDistItem(const uhdm::DistItem* object) {}
void IntegrityChecker::visitDistribution(const uhdm::Distribution* object) {}
void IntegrityChecker::visitDoWhile(const uhdm::DoWhile* object) {}
void IntegrityChecker::visitEnumConst(const uhdm::EnumConst* object) {}
void IntegrityChecker::visitEnumTypespec(const uhdm::EnumTypespec* object) {}
void IntegrityChecker::visitEventControl(const uhdm::EventControl* object) {}
void IntegrityChecker::visitEventStmt(const uhdm::EventStmt* object) {}
void IntegrityChecker::visitEventTypespec(const uhdm::EventTypespec* object) {}
void IntegrityChecker::visitExpectStmt(const uhdm::ExpectStmt* object) {}
void IntegrityChecker::visitExtends(const uhdm::Extends* object) {}
void IntegrityChecker::visitFinalStmt(const uhdm::FinalStmt* object) {}
void IntegrityChecker::visitForStmt(const uhdm::ForStmt* object) {}
void IntegrityChecker::visitForce(const uhdm::Force* object) {}
void IntegrityChecker::visitForeachStmt(const uhdm::ForeachStmt* object) {
  if (const uhdm::Any* const variable = object->getVariable()) {
    if (const uhdm::Any* const actual = uhdm::getActual(variable)) {
      if (actual->getUhdmType() == uhdm::UhdmType::IODecl) {
        if (const uhdm::Typespec* const typespec = uhdm::getTypespec(actual)) {
          if ((typespec->getUhdmType() != uhdm::UhdmType::ArrayTypespec) &&
              (typespec->getUhdmType() != uhdm::UhdmType::StringTypespec)) {
            reportInvalidForeachVariable(object);
          }
        }
      }
    }
  }
}
void IntegrityChecker::visitForeverStmt(const uhdm::ForeverStmt* object) {}
void IntegrityChecker::visitForkStmt(const uhdm::ForkStmt* object) {}
void IntegrityChecker::visitFuncCall(const uhdm::FuncCall* object) {}
void IntegrityChecker::visitFunction(const uhdm::Function* object) {}
void IntegrityChecker::visitFunctionDecl(const uhdm::FunctionDecl* object) {}
void IntegrityChecker::visitGate(const uhdm::Gate* object) {}
void IntegrityChecker::visitGateArray(const uhdm::GateArray* object) {}
void IntegrityChecker::visitGenCase(const uhdm::GenCase* object) {}
void IntegrityChecker::visitGenFor(const uhdm::GenFor* object) {}
void IntegrityChecker::visitGenIf(const uhdm::GenIf* object) {}
void IntegrityChecker::visitGenIfElse(const uhdm::GenIfElse* object) {}
void IntegrityChecker::visitGenRegion(const uhdm::GenRegion* object) {}
void IntegrityChecker::visitGenScope(const uhdm::GenScope* object) {}
void IntegrityChecker::visitGenScopeArray(const uhdm::GenScopeArray* object) {}
void IntegrityChecker::visitHierPath(const uhdm::HierPath* object) {}
void IntegrityChecker::visitIODecl(const uhdm::IODecl* object) {}
void IntegrityChecker::visitIdentifier(const uhdm::Identifier* object) {}
void IntegrityChecker::visitIfElse(const uhdm::IfElse* object) {}
void IntegrityChecker::visitIfStmt(const uhdm::IfStmt* object) {}
void IntegrityChecker::visitImmediateAssert(const uhdm::ImmediateAssert* object) {}
void IntegrityChecker::visitImmediateAssume(const uhdm::ImmediateAssume* object) {}
void IntegrityChecker::visitImmediateCover(const uhdm::ImmediateCover* object) {}
void IntegrityChecker::visitImplication(const uhdm::Implication* object) {}
void IntegrityChecker::visitImportTypespec(const uhdm::ImportTypespec* object) {}
void IntegrityChecker::visitIndexedPartSelect(const uhdm::IndexedPartSelect* object) {}
void IntegrityChecker::visitInitial(const uhdm::Initial* object) {}
void IntegrityChecker::visitIntTypespec(const uhdm::IntTypespec* object) {}
void IntegrityChecker::visitIntegerTypespec(const uhdm::IntegerTypespec* object) {}
void IntegrityChecker::visitInterface(const uhdm::Interface* object) {}
void IntegrityChecker::visitInterfaceArray(const uhdm::InterfaceArray* object) {}
void IntegrityChecker::visitInterfaceTFDecl(const uhdm::InterfaceTFDecl* object) {}
void IntegrityChecker::visitInterfaceTypespec(const uhdm::InterfaceTypespec* object) {}
void IntegrityChecker::visitLetDecl(const uhdm::LetDecl* object) {}
void IntegrityChecker::visitLetExpr(const uhdm::LetExpr* object) {}
void IntegrityChecker::visitLogicTypespec(const uhdm::LogicTypespec* object) {}
void IntegrityChecker::visitLongIntTypespec(const uhdm::LongIntTypespec* object) {}
void IntegrityChecker::visitMethodFuncCall(const uhdm::MethodFuncCall* object) {}
void IntegrityChecker::visitMethodTaskCall(const uhdm::MethodTaskCall* object) {}
void IntegrityChecker::visitModPath(const uhdm::ModPath* object) {}
void IntegrityChecker::visitModport(const uhdm::Modport* object) {}
void IntegrityChecker::visitModule(const uhdm::Module* object) {}
void IntegrityChecker::visitModuleArray(const uhdm::ModuleArray* object) {}
void IntegrityChecker::visitModuleTypespec(const uhdm::ModuleTypespec* object) {}
void IntegrityChecker::visitMulticlockSequenceExpr(const uhdm::MulticlockSequenceExpr* object) {}
void IntegrityChecker::visitNamedEvent(const uhdm::NamedEvent* object) {}
void IntegrityChecker::visitNamedEventArray(const uhdm::NamedEventArray* object) {}
void IntegrityChecker::visitNet(const uhdm::Net* object) {}
void IntegrityChecker::visitNullStmt(const uhdm::NullStmt* object) {}
void IntegrityChecker::visitOperation(const uhdm::Operation* object) {}
void IntegrityChecker::visitOrderedWait(const uhdm::OrderedWait* object) {}
void IntegrityChecker::visitPackage(const uhdm::Package* object) {}
void IntegrityChecker::visitParamAssign(const uhdm::ParamAssign* object) {}
void IntegrityChecker::visitParameter(const uhdm::Parameter* object) {}
void IntegrityChecker::visitPartSelect(const uhdm::PartSelect* object) {
  if (object->getActual() == nullptr) reportNullActual(object);
}
void IntegrityChecker::visitPathTerm(const uhdm::PathTerm* object) {}
void IntegrityChecker::visitPort(const uhdm::Port* object) {}
void IntegrityChecker::visitPortBit(const uhdm::PortBit* object) {}
void IntegrityChecker::visitPreprocMacroDefinition(const uhdm::PreprocMacroDefinition* object) {}
void IntegrityChecker::visitPreprocMacroInstance(const uhdm::PreprocMacroInstance* object) {}
void IntegrityChecker::visitPrimTerm(const uhdm::PrimTerm* object) {}
void IntegrityChecker::visitProgram(const uhdm::Program* object) {}
void IntegrityChecker::visitProgramArray(const uhdm::ProgramArray* object) {}
void IntegrityChecker::visitProgramTypespec(const uhdm::ProgramTypespec* object) {}
void IntegrityChecker::visitPropFormalDecl(const uhdm::PropFormalDecl* object) {}
void IntegrityChecker::visitPropertyDecl(const uhdm::PropertyDecl* object) {}
void IntegrityChecker::visitPropertyInst(const uhdm::PropertyInst* object) {}
void IntegrityChecker::visitPropertySpec(const uhdm::PropertySpec* object) {}
void IntegrityChecker::visitPropertyTypespec(const uhdm::PropertyTypespec* object) {}
void IntegrityChecker::visitRange(const uhdm::Range* object) {}
void IntegrityChecker::visitRealTypespec(const uhdm::RealTypespec* object) {}
void IntegrityChecker::visitRefModule(const uhdm::RefModule* object) {
  if (object->getActual() == nullptr) reportNullActual(object);
}
void IntegrityChecker::visitRefObj(const uhdm::RefObj* object) {
  if (object->getName() == "triggered") {
    if (const uhdm::HierPath* const hp = object->getParent<uhdm::HierPath>()) {
      if (const uhdm::AnyCollection* const pe = hp->getPathElems()) {
        uhdm::AnyCollection::const_iterator it = std::find(pe->cbegin(), pe->cend(), object);
        if ((it != pe->begin()) && (it != pe->cend())) {
          if (any_cast<uhdm::NamedEvent>(*--it) != nullptr) {
            // event.triggered is a property and can't be bound to anything.
            // SL doesn' model properties, yet!
            return;
          }
        }
      }
    }
  }

  if (!isValidName(object)) {
    reportMissingName(object);
  }
  std::string_view name = object->getName();
  if (const uhdm::Any* const actual = object->getActual()) {
    if ((name != "this") && (name != "super") && (name != "$root") && !areNamedSame(object, actual)) {
      reportInvalidName(object);
    }
  } else if (name != "$root") {
    reportNullActual(object);
  }
}
void IntegrityChecker::visitRefTypespec(const uhdm::RefTypespec* object) {
  const uhdm::Any* const parent = object->getParent();
  if (parent == nullptr) return;

  const uhdm::Variable* const parentAsVariable = object->getParent<uhdm::Variable>();

  if (!isImplicitFunctionReturnType(object)) {
    if (!isValidLocation(object) && (parentAsVariable == nullptr)) {
      reportMissingLocation(object);
    }
    if (!isValidFile(object)) {
      reportMissingFile(object);
    }
  }

  if (parentAsVariable != nullptr) {
    bool isRefVar = false;
    if (const uhdm::ForeachStmt* const grandParentAsForeachStmt = parentAsVariable->getParent<uhdm::ForeachStmt>()) {
      if (const uhdm::AnyCollection* const loopVars = grandParentAsForeachStmt->getLoopVars()) {
        isRefVar = std::find(loopVars->cbegin(), loopVars->cend(), object) != loopVars->cend();
      }
    }

    if (isRefVar && isValidName(object)) {
      reportInvalidName(object);
    }
  }
  if (const uhdm::Any* const actual = object->getActual()) {
    if (actual->getUhdmType() == uhdm::UhdmType::UnsupportedTypespec) {
      reportUnsupportedTypespec(object);
    }

    switch (actual->getUhdmType()) {
      case uhdm::UhdmType::ClassTypespec:
      case uhdm::UhdmType::ImportTypespec:
      case uhdm::UhdmType::InterfaceTypespec:
      case uhdm::UhdmType::ModuleTypespec:
      case uhdm::UhdmType::ProgramTypespec:
      case uhdm::UhdmType::TypeParameter:
      case uhdm::UhdmType::TypedefTypespec:
      case uhdm::UhdmType::UdpDefnTypespec:
      case uhdm::UhdmType::UnsupportedTypespec: {
        if (!isValidName(object)) {
          bool shouldReport = true;
          if (const uhdm::TaskFunc* const tf = any_cast<uhdm::TaskFunc>(parent)) {
            shouldReport = (tf->getName() != "new");
          }
          if (shouldReport) reportMissingName(object);
        } else if (!areNamedSame(object, actual)) {
          reportInvalidName(object);
        }
      } break;

      default: {
        if (isValidName(object)) {
          if (const uhdm::TypedefTypespec* const parentAsTypedefTypespec = any_cast<uhdm::TypedefTypespec>(parent)) {
            if ((parentAsTypedefTypespec->getTypedefAlias() != object)) {
              reportInvalidName(object);
            }
          } else if (parent->getUhdmType() != uhdm::UhdmType::TaggedPattern) {
            reportInvalidName(object);
          }
        }
      } break;
    }
  } else {
    reportNullTypespec(object);
    if (!isValidLocation(object)) {
      reportMissingLocation(object);
    }
    if (!isValidFile(object)) {
      reportMissingFile(object);
    }
  }
}
void IntegrityChecker::visitReg(const uhdm::Reg* object) {}
void IntegrityChecker::visitRegArray(const uhdm::RegArray* object) {}
void IntegrityChecker::visitRelease(const uhdm::Release* object) {}
void IntegrityChecker::visitRepeat(const uhdm::Repeat* object) {}
void IntegrityChecker::visitRepeatControl(const uhdm::RepeatControl* object) {}
void IntegrityChecker::visitRestrict(const uhdm::Restrict* object) {}
void IntegrityChecker::visitReturnStmt(const uhdm::ReturnStmt* object) {}
void IntegrityChecker::visitSeqFormalDecl(const uhdm::SeqFormalDecl* object) {}
void IntegrityChecker::visitSequenceDecl(const uhdm::SequenceDecl* object) {}
void IntegrityChecker::visitSequenceInst(const uhdm::SequenceInst* object) {}
void IntegrityChecker::visitSequenceTypespec(const uhdm::SequenceTypespec* object) {}
void IntegrityChecker::visitShortIntTypespec(const uhdm::ShortIntTypespec* object) {}
void IntegrityChecker::visitShortRealTypespec(const uhdm::ShortRealTypespec* object) {}
void IntegrityChecker::visitSoftDisable(const uhdm::SoftDisable* object) {}
void IntegrityChecker::visitSourceFile(const uhdm::SourceFile* object) {}
void IntegrityChecker::visitSpecParam(const uhdm::SpecParam* object) {}
void IntegrityChecker::visitStringTypespec(const uhdm::StringTypespec* object) {}
void IntegrityChecker::visitStructPattern(const uhdm::StructPattern* object) {}
void IntegrityChecker::visitStructTypespec(const uhdm::StructTypespec* object) {}
void IntegrityChecker::visitSwitchArray(const uhdm::SwitchArray* object) {}
void IntegrityChecker::visitSwitchTran(const uhdm::SwitchTran* object) {}
void IntegrityChecker::visitSysFuncCall(const uhdm::SysFuncCall* object) {}
void IntegrityChecker::visitSysTaskCall(const uhdm::SysTaskCall* object) {}
void IntegrityChecker::visitTableEntry(const uhdm::TableEntry* object) {}
void IntegrityChecker::visitTaggedPattern(const uhdm::TaggedPattern* object) {}
void IntegrityChecker::visitTask(const uhdm::Task* object) {}
void IntegrityChecker::visitTaskCall(const uhdm::TaskCall* object) {}
void IntegrityChecker::visitTaskDecl(const uhdm::TaskDecl* object) {}
void IntegrityChecker::visitTchk(const uhdm::Tchk* object) {}
void IntegrityChecker::visitTchkTerm(const uhdm::TchkTerm* object) {}
void IntegrityChecker::visitThread(const uhdm::Thread* object) {}
void IntegrityChecker::visitTimeTypespec(const uhdm::TimeTypespec* object) {}
void IntegrityChecker::visitTypeParameter(const uhdm::TypeParameter* object) {}
void IntegrityChecker::visitTypedefTypespec(const uhdm::TypedefTypespec* object) {}
void IntegrityChecker::visitTypespecMember(const uhdm::TypespecMember* object) {}
void IntegrityChecker::visitUdp(const uhdm::Udp* object) {}
void IntegrityChecker::visitUdpArray(const uhdm::UdpArray* object) {}
void IntegrityChecker::visitUdpDefn(const uhdm::UdpDefn* object) {}
void IntegrityChecker::visitUdpDefnTypespec(const uhdm::UdpDefnTypespec* object) {}
void IntegrityChecker::visitUnionTypespec(const uhdm::UnionTypespec* object) {}
void IntegrityChecker::visitUnsupportedExpr(const uhdm::UnsupportedExpr* object) {}
void IntegrityChecker::visitUnsupportedStmt(const uhdm::UnsupportedStmt* object) {}
void IntegrityChecker::visitUnsupportedTypespec(const uhdm::UnsupportedTypespec* object) {}
void IntegrityChecker::visitUserSystf(const uhdm::UserSystf* object) {}
void IntegrityChecker::visitVarSelect(const uhdm::VarSelect* object) {}
void IntegrityChecker::visitVariable(const uhdm::Variable* object) {}
void IntegrityChecker::visitVoidTypespec(const uhdm::VoidTypespec* object) {}
void IntegrityChecker::visitWaitFork(const uhdm::WaitFork* object) {}
void IntegrityChecker::visitWaitStmt(const uhdm::WaitStmt* object) {}
void IntegrityChecker::visitWhileStmt(const uhdm::WhileStmt* object) {}

// clang-format off
void IntegrityChecker::visitAliasCollection(const uhdm::Any* object, const uhdm::AliasCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitAlwaysCollection(const uhdm::Any* object, const uhdm::AlwaysCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitAnyCollection(const uhdm::Any* object, const uhdm::AnyCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitAnyPatternCollection(const uhdm::Any* object, const uhdm::AnyPatternCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitArrayExprCollection(const uhdm::Any* object, const uhdm::ArrayExprCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitArrayTypespecCollection(const uhdm::Any* object, const uhdm::ArrayTypespecCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitAssertCollection(const uhdm::Any* object, const uhdm::AssertCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitAssignStmtCollection(const uhdm::Any* object, const uhdm::AssignStmtCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitAssignmentCollection(const uhdm::Any* object, const uhdm::AssignmentCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitAssumeCollection(const uhdm::Any* object, const uhdm::AssumeCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitAtomicStmtCollection(const uhdm::Any* object, const uhdm::AtomicStmtCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitAttributeCollection(const uhdm::Any* object, const uhdm::AttributeCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitBeginCollection(const uhdm::Any* object, const uhdm::BeginCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitBitSelectCollection(const uhdm::Any* object, const uhdm::BitSelectCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitBitTypespecCollection(const uhdm::Any* object, const uhdm::BitTypespecCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitBreakStmtCollection(const uhdm::Any* object, const uhdm::BreakStmtCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitByteTypespecCollection(const uhdm::Any* object, const uhdm::ByteTypespecCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitCaseItemCollection(const uhdm::Any* object, const uhdm::CaseItemCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitCasePropertyCollection(const uhdm::Any* object, const uhdm::CasePropertyCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitCasePropertyItemCollection(const uhdm::Any* object, const uhdm::CasePropertyItemCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitCaseStmtCollection(const uhdm::Any* object, const uhdm::CaseStmtCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitChandleTypespecCollection(const uhdm::Any* object, const uhdm::ChandleTypespecCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitCheckerDeclCollection(const uhdm::Any* object, const uhdm::CheckerDeclCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitCheckerInstCollection(const uhdm::Any* object, const uhdm::CheckerInstCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitCheckerInstPortCollection(const uhdm::Any* object, const uhdm::CheckerInstPortCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitCheckerPortCollection(const uhdm::Any* object, const uhdm::CheckerPortCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitClassDefnCollection(const uhdm::Any* object, const uhdm::ClassDefnCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitClassObjCollection(const uhdm::Any* object, const uhdm::ClassObjCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitClassTypespecCollection(const uhdm::Any* object, const uhdm::ClassTypespecCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitClockedPropertyCollection(const uhdm::Any* object, const uhdm::ClockedPropertyCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitClockedSeqCollection(const uhdm::Any* object, const uhdm::ClockedSeqCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitClockingBlockCollection(const uhdm::Any* object, const uhdm::ClockingBlockCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitClockingIODeclCollection(const uhdm::Any* object, const uhdm::ClockingIODeclCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitConcurrentAssertionsCollection(const uhdm::Any* object, const uhdm::ConcurrentAssertionsCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitConstantCollection(const uhdm::Any* object, const uhdm::ConstantCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitConstrForeachCollection(const uhdm::Any* object, const uhdm::ConstrForeachCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitConstrIfCollection(const uhdm::Any* object, const uhdm::ConstrIfCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitConstrIfElseCollection(const uhdm::Any* object, const uhdm::ConstrIfElseCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitConstraintCollection(const uhdm::Any* object, const uhdm::ConstraintCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitConstraintExprCollection(const uhdm::Any* object, const uhdm::ConstraintExprCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitConstraintOrderingCollection(const uhdm::Any* object, const uhdm::ConstraintOrderingCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitContAssignCollection(const uhdm::Any* object, const uhdm::ContAssignCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitContAssignBitCollection(const uhdm::Any* object, const uhdm::ContAssignBitCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitContinueStmtCollection(const uhdm::Any* object, const uhdm::ContinueStmtCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitCoverCollection(const uhdm::Any* object, const uhdm::CoverCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitDeassignCollection(const uhdm::Any* object, const uhdm::DeassignCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitDefParamCollection(const uhdm::Any* object, const uhdm::DefParamCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitDelayControlCollection(const uhdm::Any* object, const uhdm::DelayControlCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitDelayTermCollection(const uhdm::Any* object, const uhdm::DelayTermCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitDesignCollection(const uhdm::Any* object, const uhdm::DesignCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitDisableCollection(const uhdm::Any* object, const uhdm::DisableCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitDisableForkCollection(const uhdm::Any* object, const uhdm::DisableForkCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitDisablesCollection(const uhdm::Any* object, const uhdm::DisablesCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitDistItemCollection(const uhdm::Any* object, const uhdm::DistItemCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitDistributionCollection(const uhdm::Any* object, const uhdm::DistributionCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitDoWhileCollection(const uhdm::Any* object, const uhdm::DoWhileCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitEnumConstCollection(const uhdm::Any* object, const uhdm::EnumConstCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitEnumTypespecCollection(const uhdm::Any* object, const uhdm::EnumTypespecCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitEventControlCollection(const uhdm::Any* object, const uhdm::EventControlCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitEventStmtCollection(const uhdm::Any* object, const uhdm::EventStmtCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitEventTypespecCollection(const uhdm::Any* object, const uhdm::EventTypespecCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitExpectStmtCollection(const uhdm::Any* object, const uhdm::ExpectStmtCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitExprCollection(const uhdm::Any* object, const uhdm::ExprCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitExtendsCollection(const uhdm::Any* object, const uhdm::ExtendsCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitFinalStmtCollection(const uhdm::Any* object, const uhdm::FinalStmtCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitForStmtCollection(const uhdm::Any* object, const uhdm::ForStmtCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitForceCollection(const uhdm::Any* object, const uhdm::ForceCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitForeachStmtCollection(const uhdm::Any* object, const uhdm::ForeachStmtCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitForeverStmtCollection(const uhdm::Any* object, const uhdm::ForeverStmtCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitForkStmtCollection(const uhdm::Any* object, const uhdm::ForkStmtCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitFuncCallCollection(const uhdm::Any* object, const uhdm::FuncCallCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitFunctionCollection(const uhdm::Any* object, const uhdm::FunctionCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitFunctionDeclCollection(const uhdm::Any* object, const uhdm::FunctionDeclCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitGateCollection(const uhdm::Any* object, const uhdm::GateCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitGateArrayCollection(const uhdm::Any* object, const uhdm::GateArrayCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitGenCaseCollection(const uhdm::Any* object, const uhdm::GenCaseCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitGenForCollection(const uhdm::Any* object, const uhdm::GenForCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitGenIfCollection(const uhdm::Any* object, const uhdm::GenIfCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitGenIfElseCollection(const uhdm::Any* object, const uhdm::GenIfElseCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitGenRegionCollection(const uhdm::Any* object, const uhdm::GenRegionCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitGenScopeCollection(const uhdm::Any* object, const uhdm::GenScopeCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitGenScopeArrayCollection(const uhdm::Any* object, const uhdm::GenScopeArrayCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitGenStmtCollection(const uhdm::Any* object, const uhdm::GenStmtCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitHierPathCollection(const uhdm::Any* object, const uhdm::HierPathCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitIODeclCollection(const uhdm::Any* object, const uhdm::IODeclCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitIdentifierCollection(const uhdm::Any* object, const uhdm::IdentifierCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitIfElseCollection(const uhdm::Any* object, const uhdm::IfElseCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitIfStmtCollection(const uhdm::Any* object, const uhdm::IfStmtCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitImmediateAssertCollection(const uhdm::Any* object, const uhdm::ImmediateAssertCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitImmediateAssumeCollection(const uhdm::Any* object, const uhdm::ImmediateAssumeCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitImmediateCoverCollection(const uhdm::Any* object, const uhdm::ImmediateCoverCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitImplicationCollection(const uhdm::Any* object, const uhdm::ImplicationCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitImportTypespecCollection(const uhdm::Any* object, const uhdm::ImportTypespecCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitIndexedPartSelectCollection(const uhdm::Any* object, const uhdm::IndexedPartSelectCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitInitialCollection(const uhdm::Any* object, const uhdm::InitialCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitInstanceCollection(const uhdm::Any* object, const uhdm::InstanceCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitInstanceArrayCollection(const uhdm::Any* object, const uhdm::InstanceArrayCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitIntTypespecCollection(const uhdm::Any* object, const uhdm::IntTypespecCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitIntegerTypespecCollection(const uhdm::Any* object, const uhdm::IntegerTypespecCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitInterfaceCollection(const uhdm::Any* object, const uhdm::InterfaceCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitInterfaceArrayCollection(const uhdm::Any* object, const uhdm::InterfaceArrayCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitInterfaceTFDeclCollection(const uhdm::Any* object, const uhdm::InterfaceTFDeclCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitInterfaceTypespecCollection(const uhdm::Any* object, const uhdm::InterfaceTypespecCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitLetDeclCollection(const uhdm::Any* object, const uhdm::LetDeclCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitLetExprCollection(const uhdm::Any* object, const uhdm::LetExprCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitLogicTypespecCollection(const uhdm::Any* object, const uhdm::LogicTypespecCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitLongIntTypespecCollection(const uhdm::Any* object, const uhdm::LongIntTypespecCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitMethodFuncCallCollection(const uhdm::Any* object, const uhdm::MethodFuncCallCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitMethodTaskCallCollection(const uhdm::Any* object, const uhdm::MethodTaskCallCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitModPathCollection(const uhdm::Any* object, const uhdm::ModPathCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitModportCollection(const uhdm::Any* object, const uhdm::ModportCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitModuleCollection(const uhdm::Any* object, const uhdm::ModuleCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitModuleArrayCollection(const uhdm::Any* object, const uhdm::ModuleArrayCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitModuleTypespecCollection(const uhdm::Any* object, const uhdm::ModuleTypespecCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitMulticlockSequenceExprCollection(const uhdm::Any* object, const uhdm::MulticlockSequenceExprCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitNamedEventCollection(const uhdm::Any* object, const uhdm::NamedEventCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitNamedEventArrayCollection(const uhdm::Any* object, const uhdm::NamedEventArrayCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitNetCollection(const uhdm::Any* object, const uhdm::NetCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitNetDriversCollection(const uhdm::Any* object, const uhdm::NetDriversCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitNetLoadsCollection(const uhdm::Any* object, const uhdm::NetLoadsCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitNullStmtCollection(const uhdm::Any* object, const uhdm::NullStmtCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitOperationCollection(const uhdm::Any* object, const uhdm::OperationCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitOrderedWaitCollection(const uhdm::Any* object, const uhdm::OrderedWaitCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitPackageCollection(const uhdm::Any* object, const uhdm::PackageCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitParamAssignCollection(const uhdm::Any* object, const uhdm::ParamAssignCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitParameterCollection(const uhdm::Any* object, const uhdm::ParameterCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitPartSelectCollection(const uhdm::Any* object, const uhdm::PartSelectCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitPathTermCollection(const uhdm::Any* object, const uhdm::PathTermCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitPortCollection(const uhdm::Any* object, const uhdm::PortCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitPortBitCollection(const uhdm::Any* object, const uhdm::PortBitCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitPortsCollection(const uhdm::Any* object, const uhdm::PortsCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitPreprocMacroDefinitionCollection(const uhdm::Any* object, const uhdm::PreprocMacroDefinitionCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitPreprocMacroInstanceCollection(const uhdm::Any* object, const uhdm::PreprocMacroInstanceCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitPrimTermCollection(const uhdm::Any* object, const uhdm::PrimTermCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitPrimitiveCollection(const uhdm::Any* object, const uhdm::PrimitiveCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitPrimitiveArrayCollection(const uhdm::Any* object, const uhdm::PrimitiveArrayCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitProcessCollection(const uhdm::Any* object, const uhdm::ProcessCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitProgramCollection(const uhdm::Any* object, const uhdm::ProgramCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitProgramArrayCollection(const uhdm::Any* object, const uhdm::ProgramArrayCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitProgramTypespecCollection(const uhdm::Any* object, const uhdm::ProgramTypespecCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitPropFormalDeclCollection(const uhdm::Any* object, const uhdm::PropFormalDeclCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitPropertyDeclCollection(const uhdm::Any* object, const uhdm::PropertyDeclCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitPropertyInstCollection(const uhdm::Any* object, const uhdm::PropertyInstCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitPropertySpecCollection(const uhdm::Any* object, const uhdm::PropertySpecCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitPropertyTypespecCollection(const uhdm::Any* object, const uhdm::PropertyTypespecCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitRangeCollection(const uhdm::Any* object, const uhdm::RangeCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitRealTypespecCollection(const uhdm::Any* object, const uhdm::RealTypespecCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitRefModuleCollection(const uhdm::Any* object, const uhdm::RefModuleCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitRefObjCollection(const uhdm::Any* object, const uhdm::RefObjCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitRefTypespecCollection(const uhdm::Any* object, const uhdm::RefTypespecCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitRegCollection(const uhdm::Any* object, const uhdm::RegCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitRegArrayCollection(const uhdm::Any* object, const uhdm::RegArrayCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitReleaseCollection(const uhdm::Any* object, const uhdm::ReleaseCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitRepeatCollection(const uhdm::Any* object, const uhdm::RepeatCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitRepeatControlCollection(const uhdm::Any* object, const uhdm::RepeatControlCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitRestrictCollection(const uhdm::Any* object, const uhdm::RestrictCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitReturnStmtCollection(const uhdm::Any* object, const uhdm::ReturnStmtCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitScopeCollection(const uhdm::Any* object, const uhdm::ScopeCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitSeqFormalDeclCollection(const uhdm::Any* object, const uhdm::SeqFormalDeclCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitSequenceDeclCollection(const uhdm::Any* object, const uhdm::SequenceDeclCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitSequenceInstCollection(const uhdm::Any* object, const uhdm::SequenceInstCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitSequenceTypespecCollection(const uhdm::Any* object, const uhdm::SequenceTypespecCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitShortIntTypespecCollection(const uhdm::Any* object, const uhdm::ShortIntTypespecCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitShortRealTypespecCollection(const uhdm::Any* object, const uhdm::ShortRealTypespecCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitSimpleExprCollection(const uhdm::Any* object, const uhdm::SimpleExprCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitSoftDisableCollection(const uhdm::Any* object, const uhdm::SoftDisableCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitSourceFileCollection(const uhdm::Any* object, const uhdm::SourceFileCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitSpecParamCollection(const uhdm::Any* object, const uhdm::SpecParamCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitStringTypespecCollection(const uhdm::Any* object, const uhdm::StringTypespecCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitStructPatternCollection(const uhdm::Any* object, const uhdm::StructPatternCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitStructTypespecCollection(const uhdm::Any* object, const uhdm::StructTypespecCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitSwitchArrayCollection(const uhdm::Any* object, const uhdm::SwitchArrayCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitSwitchTranCollection(const uhdm::Any* object, const uhdm::SwitchTranCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitSysFuncCallCollection(const uhdm::Any* object, const uhdm::SysFuncCallCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitSysTaskCallCollection(const uhdm::Any* object, const uhdm::SysTaskCallCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitTFCallCollection(const uhdm::Any* object, const uhdm::TFCallCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitTableEntryCollection(const uhdm::Any* object, const uhdm::TableEntryCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitTaggedPatternCollection(const uhdm::Any* object, const uhdm::TaggedPatternCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitTaskCollection(const uhdm::Any* object, const uhdm::TaskCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitTaskCallCollection(const uhdm::Any* object, const uhdm::TaskCallCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitTaskDeclCollection(const uhdm::Any* object, const uhdm::TaskDeclCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitTaskFuncCollection(const uhdm::Any* object, const uhdm::TaskFuncCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitTaskFuncDeclCollection(const uhdm::Any* object, const uhdm::TaskFuncDeclCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitTchkCollection(const uhdm::Any* object, const uhdm::TchkCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitTchkTermCollection(const uhdm::Any* object, const uhdm::TchkTermCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitThreadCollection(const uhdm::Any* object, const uhdm::ThreadCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitTimeTypespecCollection(const uhdm::Any* object, const uhdm::TimeTypespecCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitTypeParameterCollection(const uhdm::Any* object, const uhdm::TypeParameterCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitTypedefTypespecCollection(const uhdm::Any* object, const uhdm::TypedefTypespecCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitTypespecCollection(const uhdm::Any* object, const uhdm::TypespecCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitTypespecMemberCollection(const uhdm::Any* object, const uhdm::TypespecMemberCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitUdpCollection(const uhdm::Any* object, const uhdm::UdpCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitUdpArrayCollection(const uhdm::Any* object, const uhdm::UdpArrayCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitUdpDefnCollection(const uhdm::Any* object, const uhdm::UdpDefnCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitUdpDefnTypespecCollection(const uhdm::Any* object, const uhdm::UdpDefnTypespecCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitUnionTypespecCollection(const uhdm::Any* object, const uhdm::UnionTypespecCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitUnsupportedExprCollection(const uhdm::Any* object, const uhdm::UnsupportedExprCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitUnsupportedStmtCollection(const uhdm::Any* object, const uhdm::UnsupportedStmtCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitUnsupportedTypespecCollection(const uhdm::Any* object, const uhdm::UnsupportedTypespecCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitUserSystfCollection(const uhdm::Any* object, const uhdm::UserSystfCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitVarSelectCollection(const uhdm::Any* object, const uhdm::VarSelectCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitVariableCollection(const uhdm::Any* object, const uhdm::VariableCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitVoidTypespecCollection(const uhdm::Any* object, const uhdm::VoidTypespecCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitWaitForkCollection(const uhdm::Any* object, const uhdm::WaitForkCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitWaitStmtCollection(const uhdm::Any* object, const uhdm::WaitStmtCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitWaitsCollection(const uhdm::Any* object, const uhdm::WaitsCollection& objects) { reportDuplicates(object, objects); }
void IntegrityChecker::visitWhileStmtCollection(const uhdm::Any* object, const uhdm::WhileStmtCollection& objects) { reportDuplicates(object, objects); }
// clang-format on

void IntegrityChecker::populateAnyMacroInstanceCache(const uhdm::PreprocMacroInstance* pmi) {
  if (const uhdm::AnyCollection* const objects = pmi->getObjects()) {
    for (const uhdm::Any* any : *objects) {
      m_anyMacroInstance.emplace(any, pmi);
    }
  }

  if (const uhdm::PreprocMacroInstanceCollection* children = pmi->getPreprocMacroInstances()) {
    for (const uhdm::PreprocMacroInstance* child : *children) {
      populateAnyMacroInstanceCache(child);
    }
  }
}

void IntegrityChecker::populateAnyMacroInstanceCache() {
  if (const uhdm::SourceFileCollection* const sourceFiles = m_design->getSourceFiles()) {
    for (const uhdm::SourceFile* sourceFile : *sourceFiles) {
      if (const uhdm::PreprocMacroInstanceCollection* const macroInstances = sourceFile->getPreprocMacroInstances()) {
        for (const uhdm::PreprocMacroInstance* pmi : *macroInstances) {
          populateAnyMacroInstanceCache(pmi);
        }
      }
    }
  }
}

void IntegrityChecker::check(const uhdm::Design* object) {
  m_design = object;
  populateAnyMacroInstanceCache();
  visit(object);
  m_design = nullptr;
}

void IntegrityChecker::check(const std::vector<const uhdm::Design*>& objects) {
  for (const uhdm::Design* d : objects) {
    check(d);
  }
}
}  // namespace SURELOG
