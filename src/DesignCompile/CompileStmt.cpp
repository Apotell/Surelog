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
 * File:   CompileStmt.cpp
 * Author: alain
 *
 * Created on May 14, 2019, 8:03 PM
 */

#include "Surelog/Common/FileSystem.h"
#include "Surelog/Common/NodeId.h"
#include "Surelog/Common/Session.h"
#include "Surelog/Design/BindStmt.h"
#include "Surelog/Design/Design.h"
#include "Surelog/Design/Enum.h"
#include "Surelog/Design/FileContent.h"
#include "Surelog/Design/Function.h"
#include "Surelog/Design/ModuleDefinition.h"
#include "Surelog/Design/ModuleInstance.h"
#include "Surelog/Design/Task.h"
#include "Surelog/Design/VObject.h"
#include "Surelog/Design/ValuedComponentI.h"
#include "Surelog/DesignCompile/CompileDesign.h"
#include "Surelog/DesignCompile/CompileHelper.h"
#include "Surelog/DesignCompile/UhdmWriter.h"
#include "Surelog/ErrorReporting/ErrorContainer.h"
#include "Surelog/ErrorReporting/ErrorDefinition.h"
#include "Surelog/ErrorReporting/Location.h"
#include "Surelog/Library/Library.h"
#include "Surelog/SourceCompile/Compiler.h"
#include "Surelog/SourceCompile/SymbolTable.h"
#include "Surelog/SourceCompile/VObjectTypes.h"
#include "Surelog/Testbench/ClassDefinition.h"
#include "Surelog/Testbench/Property.h"
#include "Surelog/Utils/StringUtils.h"

// UHDM
#include <uhdm/BaseClass.h>
#include <uhdm/expr.h>
#include <uhdm/sv_vpi_user.h>
#include <uhdm/uhdm.h>
#include <uhdm/uhdm_types.h>
#include <uhdm/vpi_user.h>

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace SURELOG {

using namespace uhdm;  // NOLINT (using a bunch of these)

uhdm::Any* CompileHelper::searchObjectName(std::string_view name,
                                           DesignComponent* component,
                                           uhdm::Any* stmt) {
  uhdm::Any* object = nullptr;
  auto [func, actual_comp] = getTaskFunc(name, component, nullptr, stmt);
  if (func) {
    object = func;
  }
  if (object == nullptr) {
    if (stmt) {
      if (stmt->getName() == name) {
        object = stmt;
      }
      if (object == nullptr) {
        if (uhdm::Scope* sc = any_cast<uhdm::Scope*>(stmt)) {
          if (sc->getInternalScopes()) {
            for (uhdm::Scope* s : *sc->getInternalScopes()) {
              if (s->getName() == name) {
                object = s;
                break;
              }
            }
          }
        }
      }
      if (object == nullptr) {
        if (stmt->getParent()) {
          if (uhdm::Any* tmp = searchObjectName(
                  name, component, (uhdm::Any*)stmt->getParent())) {
            object = tmp;
          }
        }
      }
    }
  }
  return object;
}

uhdm::AnyCollection* CompileHelper::compileStmt(
    DesignComponent* component, const FileContent* fC, NodeId the_stmt,
    uhdm::Any* pstmt, ValuedComponentI* instance, bool muteErrors) {
  SymbolTable* const symbols = m_session->getSymbolTable();
  FileSystem* const fileSystem = m_session->getFileSystem();
  ErrorContainer* const errors = m_session->getErrorContainer();

  uhdm::AnyCollection* results = nullptr;
  uhdm::Serializer& s = m_compileDesign->getSerializer();
  VObjectType type = fC->Type(the_stmt);
  uhdm::AttributeCollection* attributes = nullptr;
  if (type == VObjectType::paAttribute_instance) {
    attributes = compileAttributes(component, fC, the_stmt, nullptr);
    while (fC->Type(the_stmt) == VObjectType::paAttribute_instance)
      the_stmt = fC->Sibling(the_stmt);
  }
  type = fC->Type(the_stmt);
  uhdm::Any* stmt = nullptr;
  switch (type) {
    case VObjectType::paModule_common_item:
    case VObjectType::paModule_or_generate_item:
    case VObjectType::paModule_or_generate_item_declaration:
    case VObjectType::paPackage_or_generate_item_declaration:
    case VObjectType::paGenerate_item: {
      if (instance) break;
      NodeId child = fC->Child(the_stmt);
      if (!child) {
        // That is the null statement (no statement)
        return nullptr;
      }
      results = compileStmt(component, fC, child, pstmt, instance, muteErrors);
      break;
    }
    case VObjectType::paGenvar_declaration: {
      results = compileGenVars(component, fC, the_stmt, pstmt);
      break;
    }
    case VObjectType::paGenerate_region:
    case VObjectType::paLoop_generate_construct:
    case VObjectType::paGenerate_interface_loop_statement:
    case VObjectType::paGenerate_interface_conditional_statement:
    case VObjectType::paConditional_generate_construct: {
      NodeId child = fC->Child(the_stmt);
      if (!child) {
        // That is the null statement (no statement)
        return nullptr;
      }
      if (uhdm::AnyCollection* sts = compileGenStmt(
              valuedcomponenti_cast<ModuleDefinition*>(component), fC, the_stmt,
              pstmt)) {
        if (!sts->empty()) stmt = sts->front();
      }
      break;
    }
    case VObjectType::paGenerate_begin_end_block: {
      if (instance) break;
      NodeId child = fC->Child(the_stmt);
      if (!child) {
        // That is the null statement (no statement)
        return nullptr;
      }
      if (fC->Sibling(child)) {
        results = s.makeCollection<Any>();
        NodeId tmp = child;
        while (tmp) {
          if (fC->Type(tmp) != VObjectType::STRING_CONST) {
            if (uhdm::AnyCollection* stmts = compileStmt(
                    component, fC, tmp, pstmt, instance, muteErrors)) {
              for (uhdm::Any* st : *stmts) {
                results->emplace_back(st);
              }
            }
          }
          tmp = fC->Sibling(tmp);
        }
      } else {
        results =
            compileStmt(component, fC, child, pstmt, instance, muteErrors);
      }
      break;
    }
    case VObjectType::paStatement_or_null: {
      NodeId child = fC->Child(the_stmt);
      if (!child) {
        // That is the null statement (no statement)
        return nullptr;
      }
      results = compileStmt(component, fC, child, pstmt, instance, muteErrors);
      break;
    }
    case VObjectType::paBlock_item_declaration:
    case VObjectType::paTf_item_declaration:
    case VObjectType::paStatement:
    case VObjectType::paJump_statement:
    case VObjectType::paStatement_item:
    case VObjectType::paImmediate_assertion_statement:
    case VObjectType::paProcedural_assertion_statement:
    case VObjectType::paLoop_statement: {
      results = compileStmt(component, fC, fC->Child(the_stmt), pstmt, instance,
                            muteErrors);
      break;
    }
    case VObjectType::paInc_or_dec_expression: {
      stmt = compileExpression(component, fC, the_stmt, pstmt, instance);
      break;
    }
    case VObjectType::paProcedural_timing_control_statement: {
      uhdm::AtomicStmt* dc = compileProceduralTimingControlStmt(
          component, fC, fC->Child(the_stmt), pstmt, instance);
      stmt = dc;
      break;
    }
    case VObjectType::paNonblocking_assignment: {
      NodeId Operator_assignment = the_stmt;
      uhdm::Assignment* assign = compileBlockingAssignment(
          component, fC, Operator_assignment, false, pstmt, instance);
      stmt = assign;
      break;
    }
    case VObjectType::paBlocking_assignment:
    case VObjectType::paOperator_assignment: {
      NodeId Operator_assignment = fC->Child(the_stmt);
      uhdm::Assignment* assign = compileBlockingAssignment(
          component, fC, Operator_assignment, true, pstmt, instance);
      stmt = assign;
      break;
    }
    case VObjectType::paSubroutine_call_statement: {
      NodeId Subroutine_call = fC->Child(the_stmt);
      stmt = compileTfCall(component, fC, Subroutine_call, pstmt);
      break;
    }
    case VObjectType::paSystem_task: {
      stmt = compileTfCall(component, fC, the_stmt, pstmt);
      break;
    }
    case VObjectType::paConditional_statement: {
      NodeId Conditional_statement = the_stmt;
      NodeId Cond_predicate = fC->Child(Conditional_statement);
      uhdm::AtomicStmt* cstmt = compileConditionalStmt(
          component, fC, Cond_predicate, pstmt, instance);
      stmt = cstmt;
      break;
    }
    case VObjectType::paCond_predicate: {
      NodeId Cond_predicate = the_stmt;
      uhdm::AtomicStmt* cstmt = compileConditionalStmt(
          component, fC, Cond_predicate, pstmt, instance);
      stmt = cstmt;
      break;
    }
    case VObjectType::paRandcase_statement: {
      NodeId Case_statement = the_stmt;
      uhdm::AtomicStmt* cstmt =
          compileRandcaseStmt(component, fC, Case_statement, pstmt, instance);
      stmt = cstmt;
      break;
    }
    case VObjectType::paCase_statement: {
      NodeId Case_statement = the_stmt;
      uhdm::AtomicStmt* cstmt =
          compileCaseStmt(component, fC, Case_statement, pstmt, instance);
      stmt = cstmt;
      break;
    }
    case VObjectType::paSeq_block: {
      NodeId item = fC->Child(the_stmt);
      uhdm::Begin* begin = s.make<uhdm::Begin>();
      uhdm::AnyCollection* stmts = begin->getStmts(true);
      uhdm::Scope* scope = nullptr;
      std::string label, endLabel;
      NodeId labelId, endLabelId;
      if (fC->Type(item) == VObjectType::STRING_CONST) {
        labelId = item;
        item = fC->Sibling(item);
      } else {
        NodeId Statement_item = fC->Parent(the_stmt);
        NodeId Statement = fC->Parent(Statement_item);
        NodeId Label = fC->Child(Statement);
        if (fC->Sibling(Label) == Statement_item) {
          if (fC->Type(Label) == VObjectType::STRING_CONST) labelId = Label;
        }
      }
      NodeId tempItem = item;
      while (tempItem) {
        if (fC->Type(tempItem) == VObjectType::END) {
          if ((endLabelId = fC->Sibling(tempItem))) {
            break;
          }
        }
        tempItem = fC->Sibling(tempItem);
      }
      if (labelId) {
        label = fC->SymName(labelId);
        begin->setName(label);
      }
      if (endLabelId) {
        endLabel = fC->SymName(endLabelId);
        begin->setEndLabel(endLabel);
      }
      stmt = begin;
      scope = begin;
      if (!labelId && !endLabelId) labelId = the_stmt;

      const uhdm::ScopedScope scopedScope(scope);
      scope->setParent(pstmt);
      fC->populateCoreMembers(the_stmt, the_stmt, scope);
      if (labelId && endLabelId && (label != endLabel)) {
        Location loc(fC->getFileId(), fC->Line(labelId), fC->Column(labelId),
                     symbols->registerSymbol(label));
        Location loc2(fC->getFileId(), fC->Line(endLabelId),
                      fC->Column(endLabelId),
                      symbols->registerSymbol(endLabel));
        Error err(ErrorDefinition::COMP_UNMATCHED_LABEL, loc, loc2);
        errors->addError(err);
      }
      while (item && (fC->Type(item) != VObjectType::END)) {
        if (uhdm::AnyCollection* cstmts =
                compileStmt(component, fC, item, stmt, instance, muteErrors)) {
          for (uhdm::Any* cstmt : *cstmts) {
            bool isDecl = false;
            if (cstmt->getUhdmType() == uhdm::UhdmType::Assignment) {
              uhdm::Assignment* assign = (uhdm::Assignment*)cstmt;
              if (assign->getRhs() == nullptr) {
                isDecl = true;
                if (assign->getLhs()) {
                  assign->getLhs()->setParent(stmt);
                }
              }
            } else if (cstmt->getUhdmType() == uhdm::UhdmType::SequenceDecl) {
              isDecl = true;
            }
            if (!isDecl) {
              stmts->emplace_back(cstmt);
              cstmt->setParent(stmt);
            }
          }
        }
        item = fC->Sibling(item);
      }
      break;
    }
    case VObjectType::paPar_block: {
      NodeId item = fC->Child(the_stmt);
      uhdm::ForkStmt* fork = s.make<uhdm::ForkStmt>();
      uhdm::AnyCollection* stmts = fork->getStmts(true);
      uhdm::Scope* scope = nullptr;
      std::string label, endLabel;
      NodeId labelId, endLabelId;
      if (fC->Type(item) == VObjectType::STRING_CONST) {
        labelId = item;
        item = fC->Sibling(item);
      } else {
        NodeId Statement_item = fC->Parent(the_stmt);
        NodeId Statement = fC->Parent(Statement_item);
        NodeId Label = fC->Child(Statement);
        if (fC->Sibling(Label) == Statement_item) {
          if (fC->Type(Label) == VObjectType::STRING_CONST) labelId = Label;
        }
      }
      // Get join label if any
      NodeId tempItem = fC->Sibling(item);
      while (tempItem) {
        VObjectType type = fC->Type(tempItem);
        if ((type == VObjectType::paJoin_keyword) ||
            (type == VObjectType::paJoin_any_keyword) ||
            (type == VObjectType::paJoin_none_keyword)) {
          if ((endLabelId = fC->Sibling(tempItem))) {
            break;
          }
        }
        tempItem = fC->Sibling(tempItem);
      }

      if (labelId) {
        label = fC->SymName(labelId);
        fork->setName(label);
      }
      if (endLabelId) {
        endLabel = fC->SymName(endLabelId);
        fork->setEndLabel(endLabel);
      }
      stmt = fork;
      scope = fork;
      if (!labelId && !endLabelId) labelId = the_stmt;

      scope->setParent(pstmt);
      while (item) {
        if (uhdm::AnyCollection* cstmts =
                compileStmt(component, fC, item, stmt, instance, muteErrors)) {
          for (uhdm::Any* cstmt : *cstmts) {
            bool isDecl = false;
            if (cstmt->getUhdmType() == uhdm::UhdmType::Assignment) {
              uhdm::Assignment* assign = (uhdm::Assignment*)cstmt;
              if (assign->getRhs() == nullptr) {
                if (scope->getVariables() == nullptr) {
                  isDecl = true;
                }
                if (assign->getLhs()) {
                  assign->getLhs()->setParent(stmt);
                }
              }
            }
            if (!isDecl) {
              stmts->emplace_back(cstmt);
              cstmt->setParent(stmt);
            }
          }
        }
        item = fC->Sibling(item);
        if (item) {
          VObjectType jointype = fC->Type(item);
          int32_t vpijointype = 0;
          if (jointype == VObjectType::paJoin_keyword ||
              jointype == VObjectType::paJoin_any_keyword ||
              jointype == VObjectType::paJoin_none_keyword) {
            if (NodeId endLabel = fC->Sibling(item)) {
              const std::string_view endlabel = fC->SymName(endLabel);
              if (endlabel != label) {
                Location loc(fC->getFileId(), fC->Line(labelId),
                             fC->Column(labelId),
                             symbols->registerSymbol(label));
                Location loc2(fC->getFileId(), fC->Line(endLabel),
                              fC->Column(endLabel),
                              symbols->registerSymbol(endlabel));
                Error err(ErrorDefinition::COMP_UNMATCHED_LABEL, loc, loc2);
                errors->addError(err);
              }
            }
          }

          if (jointype == VObjectType::paJoin_keyword) {
            vpijointype = vpiJoin;
            ((uhdm::ForkStmt*)stmt)->setJoinType(vpijointype);
            break;
          } else if (jointype == VObjectType::paJoin_any_keyword) {
            vpijointype = vpiJoinAny;
            ((uhdm::ForkStmt*)stmt)->setJoinType(vpijointype);
            break;
          } else if (jointype == VObjectType::paJoin_none_keyword) {
            vpijointype = vpiJoinNone;
            ((uhdm::ForkStmt*)stmt)->setJoinType(vpijointype);
            break;
          }
        }
      }
      break;
    }
    case VObjectType::FOREVER: {
      uhdm::ForeverStmt* forever = s.make<uhdm::ForeverStmt>();
      NodeId item = fC->Sibling(the_stmt);
      if (uhdm::AnyCollection* forev =
              compileStmt(component, fC, item, forever, instance, muteErrors)) {
        uhdm::Any* stmt = forev->front();
        stmt->setParent(forever);
        forever->setStmt(stmt);
        forever->setEndLine(stmt->getEndLine());
        forever->setEndColumn(stmt->getEndColumn());
      }
      stmt = forever;
      break;
    }
    case VObjectType::FOREACH: {
      uhdm::ForeachStmt* for_each = s.make<uhdm::ForeachStmt>();
      NodeId Ps_or_hierarchical_array_identifier = fC->Sibling(the_stmt);
      NodeId Loop_variables = fC->Sibling(Ps_or_hierarchical_array_identifier);
      NodeId loopVarId = fC->Child(Loop_variables);
      NodeId Statement = Loop_variables;
      uhdm::AnyCollection* loop_vars = for_each->getLoopVars(true);
      while (fC->Type(Statement) == VObjectType::STRING_CONST ||
             fC->Type(Statement) == VObjectType::paLoop_variables ||
             fC->Type(Statement) == VObjectType::paComma) {
        if (fC->Type(Statement) == VObjectType::STRING_CONST) {
          loopVarId = Statement;
        } else if (fC->Type(Statement) == VObjectType::paComma) {
          uhdm::Operation* op = s.make<uhdm::Operation>();
          fC->populateCoreMembers(Statement, Statement, op);
          op->setOpType(vpiNullOp);
          loop_vars->emplace_back(op);
        } else {
          loopVarId = fC->Child(Statement);
        }
        while (loopVarId) {
          uhdm::Variable* ref = s.make<uhdm::Variable>();
          ref->setParent(for_each);
          ref->setName(fC->SymName(loopVarId));
          fC->populateCoreMembers(loopVarId, loopVarId, ref);

          uhdm::UnsupportedTypespec* tps = s.make<uhdm::UnsupportedTypespec>();
          tps->setParent(ref);
          tps->setFile(ref->getFile());

          uhdm::RefTypespec* tpsRef = s.make<uhdm::RefTypespec>();
          tpsRef->setParent(ref);
          tpsRef->setActual(tps);
          tpsRef->setFile(ref->getFile());

          ref->setTypespec(tpsRef);
          loop_vars->emplace_back(ref);

          loopVarId = fC->Sibling(loopVarId);
          while (fC->Type(loopVarId) == VObjectType::paComma) {
            NodeId lookahead = fC->Sibling(loopVarId);
            if (fC->Type(lookahead) == VObjectType::paComma) {
              uhdm::Operation* op = s.make<uhdm::Operation>();
              op->setParent(for_each);
              fC->populateCoreMembers(loopVarId, loopVarId, op);
              op->setOpType(vpiNullOp);
              loop_vars->emplace_back(op);
              loopVarId = fC->Sibling(loopVarId);
            } else {
              break;
            }
          }
          if ((fC->Type(loopVarId) != VObjectType::STRING_CONST) &&
              (fC->Type(loopVarId) != VObjectType::paComma)) {
            break;
          }
          if (fC->Type(loopVarId) == VObjectType::paComma) {
            loopVarId = fC->Sibling(loopVarId);
          }
        }
        Statement = fC->Sibling(Statement);
      }

      if (uhdm::AnyCollection* forev = compileStmt(
              component, fC, Statement, for_each, instance, muteErrors)) {
        uhdm::Any* stmt = forev->front();
        stmt->setParent(for_each);
        for_each->setStmt(stmt);
        for_each->setEndLine(stmt->getEndLine());
        for_each->setEndColumn(stmt->getEndColumn());
      }

      if (uhdm::Any* const var = compilePsOrHierarchicalArrayIdentifier(
              component, fC, Ps_or_hierarchical_array_identifier, for_each)) {
        for_each->setVariable(var);
      }
      stmt = for_each;
      break;
    }
    case VObjectType::paProcedural_continuous_assignment: {
      uhdm::Any* conta =
          compileProceduralContinuousAssign(component, fC, the_stmt);
      stmt = conta;
      break;
    }
    case VObjectType::paParameter_declaration:
    case VObjectType::paLocal_parameter_declaration: {
      NodeId Data_type_or_implicit = fC->Child(the_stmt);
      NodeId Data_type = fC->Child(Data_type_or_implicit);
      NodeId List_of_param_assignments = fC->Sibling(Data_type_or_implicit);
      NodeId Param_assignment = fC->Child(List_of_param_assignments);
      NodeId unpackedDimId = fC->Sibling(fC->Child(Param_assignment));
      uhdm::Typespec* ts =
          compileTypespec(component, fC, fC->Child(Data_type_or_implicit),
                          unpackedDimId, nullptr, nullptr, false);
      uhdm::AnyCollection* param_assigns = s.makeCollection<Any>();
      while (Param_assignment) {
        NodeId name = fC->Child(Param_assignment);
        NodeId value = fC->Sibling(name);
        uhdm::Parameter* param = s.make<uhdm::Parameter>();
        // Unpacked dimensions
        if (fC->Type(value) == VObjectType::paUnpacked_dimension) {
          int32_t unpackedSize;
          std::vector<uhdm::Range*>* unpackedDimensions = compileRanges(
              component, fC, value, param, nullptr, unpackedSize, false);
          param->setRanges(unpackedDimensions);
          param->setSize(unpackedSize);
          while (fC->Type(value) == VObjectType::paUnpacked_dimension) {
            value = fC->Sibling(value);
          }
        }
        param->setLocalParam(true);
        fC->populateCoreMembers(name, name, param);

        uhdm::ParamAssign* param_assign = s.make<uhdm::ParamAssign>();
        fC->populateCoreMembers(Param_assignment, Param_assignment,
                                param_assign);
        param_assigns->emplace_back(param_assign);
        param->setParent(pstmt);
        param->setName(fC->SymName(name));
        if (ts != nullptr) {
          if (param->getTypespec() == nullptr) {
            uhdm::RefTypespec* tsRef = s.make<uhdm::RefTypespec>();
            setRefTypespecName(tsRef, ts, fC->SymName(Data_type));
            fC->populateCoreMembers(Data_type_or_implicit,
                                    Data_type_or_implicit, tsRef);
            tsRef->setParent(param);
            param->setTypespec(tsRef);
          }
          param->getTypespec()->setActual(ts);
        }
        param_assign->setLhs(param);
        if (uhdm::Any* const rhs = compileExpression(component, fC, value,
                                                     param_assign, nullptr)) {
          if (uhdm::Typespec* const typespec = any_cast<uhdm::Typespec>(rhs)) {
            uhdm::RefTypespec* rt = s.make<uhdm::RefTypespec>();
            setRefTypespecName(rt, typespec, typespec->getName());
            fC->populateCoreMembers(value, value, rt);
            rt->setParent(param_assign);
            param_assign->setRhs(rt);
          } else if (uhdm::TaggedPattern* const tp =
                         any_cast<uhdm::TaggedPattern>(rhs)) {
            param_assign->setRhs(tp);
          } else if (uhdm::Expr* const e = any_cast<uhdm::Expr>(rhs)) {
            param_assign->setRhs(e);
          }
        }

        Param_assignment = fC->Sibling(Param_assignment);
      }
      results = param_assigns;
      break;
    }
    case VObjectType::REPEAT: {
      NodeId cond = fC->Sibling(the_stmt);
      NodeId rstmt = fC->Sibling(cond);
      uhdm::Repeat* repeat = s.make<uhdm::Repeat>();
      if (uhdm::Any* cond_exp =
              compileExpression(component, fC, cond, repeat)) {
        repeat->setCondition((uhdm::Expr*)cond_exp);
      }
      if (uhdm::AnyCollection* repeat_stmts =
              compileStmt(component, fC, rstmt, repeat, instance, muteErrors)) {
        uhdm::Any* stmt = repeat_stmts->front();
        repeat->setStmt(stmt);
        stmt->setParent(repeat);
        repeat->setEndLine(stmt->getEndLine());
        repeat->setEndColumn(stmt->getEndColumn());
      }
      stmt = repeat;
      break;
    }
    case VObjectType::WHILE: {
      NodeId cond = fC->Sibling(the_stmt);
      NodeId rstmt = fC->Sibling(cond);
      uhdm::WhileStmt* while_st = s.make<uhdm::WhileStmt>();
      if (uhdm::Any* cond_exp =
              compileExpression(component, fC, cond, while_st)) {
        while_st->setCondition((uhdm::Expr*)cond_exp);
      }
      if (uhdm::AnyCollection* while_stmts = compileStmt(
              component, fC, rstmt, while_st, instance, muteErrors)) {
        uhdm::Any* stmt = while_stmts->front();
        while_st->setStmt(stmt);
        stmt->setParent(while_st);
        while_st->setEndLine(stmt->getEndLine());
        while_st->setEndColumn(stmt->getEndColumn());
      }
      stmt = while_st;
      break;
    }
    case VObjectType::DO: {
      NodeId Statement_or_null = fC->Sibling(the_stmt);
      NodeId Condition = fC->Sibling(Statement_or_null);
      uhdm::DoWhile* do_while = s.make<uhdm::DoWhile>();
      if (NodeId Statement = fC->Child(Statement_or_null)) {
        uhdm::AnyCollection* while_stmts = compileStmt(
            component, fC, Statement, do_while, instance, muteErrors);
        if (while_stmts && !while_stmts->empty()) {
          uhdm::Any* stmt = while_stmts->front();
          do_while->setStmt(stmt);
          stmt->setParent(do_while);
        }
      }
      if (uhdm::Any* cond_exp =
              compileExpression(component, fC, Condition, do_while)) {
        do_while->setCondition((uhdm::Expr*)cond_exp);
      }
      fC->populateCoreMembers(fC->Parent(Condition), fC->Parent(Condition),
                              do_while);
      stmt = do_while;
      break;
    }
    case VObjectType::paWait_statement: {
      NodeId Expression = fC->Child(the_stmt);
      if (!Expression) {
        // wait fork
        uhdm::WaitFork* waitst = s.make<uhdm::WaitFork>();
        stmt = waitst;
      } else if (fC->Type(Expression) == VObjectType::paExpression) {
        // wait
        NodeId Statement_or_null = fC->Sibling(Expression);
        uhdm::WaitStmt* waitst = s.make<uhdm::WaitStmt>();
        if (NodeId Statement = fC->Child(Statement_or_null)) {
          uhdm::AnyCollection* while_stmts = compileStmt(
              component, fC, Statement, waitst, instance, muteErrors);
          if (while_stmts && !while_stmts->empty()) {
            uhdm::Any* stmt = while_stmts->front();
            waitst->setStmt(stmt);
            stmt->setParent(waitst);
            waitst->setEndLine(stmt->getEndLine());
            waitst->setEndColumn(stmt->getEndColumn());
          }
        }
        if (uhdm::Any* cond_exp =
                compileExpression(component, fC, Expression, waitst)) {
          waitst->setCondition((uhdm::Expr*)cond_exp);
        }
        stmt = waitst;
      } else {
        // wait order
        uhdm::OrderedWait* waitst = s.make<uhdm::OrderedWait>();
        stmt = waitst;
        uhdm::AnyCollection* conditions = waitst->getConditions(true);
        NodeId Hierarchical_identifier = Expression;
        while (Hierarchical_identifier &&
               (fC->Type(Hierarchical_identifier) ==
                VObjectType::paHierarchical_identifier)) {
          if (uhdm::Any* cond_exp = compileExpression(
                  component, fC, Hierarchical_identifier, waitst)) {
            conditions->emplace_back(cond_exp);
          }
          Hierarchical_identifier = fC->Sibling(Hierarchical_identifier);
        }
        NodeId Action_block = Hierarchical_identifier;
        NodeId Stmt = fC->Child(Action_block);
        if (fC->Type(Stmt) == VObjectType::paStatement_or_null) {
          // If only
          if (uhdm::AnyCollection* if_stmts = compileStmt(
                  component, fC, Stmt, waitst, instance, muteErrors)) {
            if (!if_stmts->empty()) {
              uhdm::Any* stmt = if_stmts->front();
              waitst->setStmt(stmt);
              stmt->setParent(waitst);
              waitst->setEndLine(stmt->getEndLine());
              waitst->setEndColumn(stmt->getEndColumn());
            }
          }
        } else if (fC->Type(Stmt) == VObjectType::ELSE) {
          // Else Only
          Stmt = fC->Sibling(Stmt);
          if (uhdm::AnyCollection* if_stmts = compileStmt(
                  component, fC, Stmt, waitst, instance, muteErrors)) {
            if (!if_stmts->empty()) {
              uhdm::Any* stmt = if_stmts->front();
              waitst->setElseStmt(stmt);
              stmt->setParent(waitst);
              waitst->setEndLine(stmt->getEndLine());
              waitst->setEndColumn(stmt->getEndColumn());
            }
          }
        } else {
          // if else
          if (uhdm::AnyCollection* if_stmts = compileStmt(
                  component, fC, Stmt, waitst, instance, muteErrors)) {
            if (!if_stmts->empty()) {
              uhdm::Any* stmt = if_stmts->front();
              waitst->setStmt(stmt);
              stmt->setParent(waitst);
              waitst->setEndLine(stmt->getEndLine());
              waitst->setEndColumn(stmt->getEndColumn());
            }
          }
          NodeId Else = fC->Sibling(Stmt);
          Stmt = fC->Sibling(Else);
          if (uhdm::AnyCollection* else_stmts = compileStmt(
                  component, fC, Stmt, waitst, instance, muteErrors)) {
            if (!else_stmts->empty()) {
              uhdm::Any* stmt = else_stmts->front();
              waitst->setElseStmt(stmt);
              stmt->setParent(waitst);
              waitst->setEndLine(stmt->getEndLine());
              waitst->setEndColumn(stmt->getEndColumn());
            }
          }
        }
      }
      break;
    }
    case VObjectType::paEvent_trigger: {
      uhdm::EventStmt* estmt = s.make<uhdm::EventStmt>();
      NodeId Trigger_type = fC->Child(the_stmt);
      if (fC->Type(Trigger_type) != VObjectType::paNonBlockingTriggerEvent) {
        estmt->setBlocking(true);
      }
      stmt = estmt;
      NodeId Hierarchical_identifier = fC->Sibling(Trigger_type);
      if (uhdm::Expr* exp = (uhdm::Expr*)compileExpression(
              component, fC, Hierarchical_identifier, estmt)) {
        estmt->setLabel(exp->getName());
      }
      break;
    }
    case VObjectType::FOR: {
      stmt = compileForLoop(component, fC, the_stmt, true);
      break;
    }
    case VObjectType::RETURN: {
      uhdm::ReturnStmt* return_stmt = s.make<uhdm::ReturnStmt>();
      fC->populateCoreMembers(the_stmt, the_stmt, return_stmt);
      if (NodeId cond = fC->Sibling(the_stmt)) {
        if (uhdm::Expr* exp = (uhdm::Expr*)compileExpression(
                component, fC, cond, return_stmt, instance, true)) {
          exp->setParent(return_stmt);
          return_stmt->setCondition(exp);
          return_stmt->setEndLine(exp->getEndLine());
          return_stmt->setEndColumn(exp->getEndColumn());
        }
      }
      stmt = return_stmt;
      break;
    }
    case VObjectType::BREAK: {
      uhdm::BreakStmt* bstmt = s.make<uhdm::BreakStmt>();
      stmt = bstmt;
      break;
    }
    case VObjectType::paDisable_statement: {
      NodeId exp = fC->Child(the_stmt);
      if (exp) {
        uhdm::Disable* disable = s.make<uhdm::Disable>();
        if (uhdm::Expr* expc =
                (uhdm::Expr*)compileExpression(component, fC, exp, disable)) {
          if (expc->getUhdmType() == uhdm::UhdmType::RefObj) {
            const std::string_view name = expc->getName();
            if (uhdm::Any* object = searchObjectName(name, component, pstmt)) {
              disable->setExpr(object);
            }
          }
        }
        stmt = disable;
      } else {
        uhdm::DisableFork* disable = s.make<uhdm::DisableFork>();
        stmt = disable;
      }
      break;
    }
    case VObjectType::CONTINUE: {
      uhdm::ContinueStmt* cstmt = s.make<uhdm::ContinueStmt>();
      stmt = cstmt;
      break;
    }
    case VObjectType::paRandsequence_statement: {
      uhdm::SequenceDecl* seqdecl = s.make<uhdm::SequenceDecl>();
      if (NodeId Name = fC->Child(the_stmt)) {
        const std::string_view name = fC->SymName(Name);
        seqdecl->setName(name);
      }
      stmt = seqdecl;
      break;
    }
    case VObjectType::paChecker_instantiation: {
      stmt = compileCheckerInstantiation(component, fC, fC->Child(the_stmt),
                                         pstmt, nullptr);
      break;
    }
    case VObjectType::paSimple_immediate_assertion_statement: {
      stmt = compileSimpleImmediateAssertion(component, fC, fC->Child(the_stmt),
                                             pstmt, nullptr);
      break;
    }
    case VObjectType::paDeferred_immediate_assertion_statement: {
      stmt = compileDeferredImmediateAssertion(
          component, fC, fC->Child(the_stmt), pstmt, nullptr);
      break;
    }
    case VObjectType::paProperty_declaration: {
      stmt =
          compilePropertyDeclaration(component, fC, the_stmt, pstmt, nullptr);
      break;
    }
    case VObjectType::paConcurrent_assertion_statement: {
      stmt = compileConcurrentAssertion(component, fC, fC->Child(the_stmt),
                                        pstmt, nullptr);
      break;
    }
    case VObjectType::paData_declaration: {
      results = compileDataDeclaration(component, fC, fC->Child(the_stmt),
                                       pstmt, instance);
      break;
    }
    case VObjectType::STRING_CONST: {
      if (uhdm::AnyCollection* stmts =
              compileStmt(component, fC, fC->Sibling(the_stmt), pstmt, instance,
                          muteErrors)) {
        const std::string_view label = fC->SymName(the_stmt);
        for (uhdm::Any* st : *stmts) {
          if (uhdm::AtomicStmt* stm = any_cast<uhdm::AtomicStmt>(st)) {
            stm->setLabel(label);
            fC->populateCoreMembers(the_stmt, InvalidNodeId, stm);
          } else if (uhdm::ConcurrentAssertions* stm =
                         any_cast<uhdm::ConcurrentAssertions>(st)) {
            stm->setName(label);
            fC->populateCoreMembers(the_stmt, InvalidNodeId, stm);
          }
        }
        results = stmts;
      }
      break;
    }
    case VObjectType::paExpect_property_statement: {
      uhdm::ExpectStmt* expect = s.make<uhdm::ExpectStmt>();
      NodeId Property_expr = fC->Child(the_stmt);
      NodeId If_block = fC->Sibling(Property_expr);
      uhdm::Any* if_stmt = nullptr;
      uhdm::Any* else_stmt = nullptr;
      if (fC->Type(If_block) == VObjectType::paAction_block) {
        NodeId if_stmt_id = fC->Child(If_block);
        NodeId else_stmt_id;
        if (fC->Type(if_stmt_id) == VObjectType::ELSE) {
          else_stmt_id = fC->Sibling(if_stmt_id);
          if_stmt_id = InvalidNodeId;
        } else {
          NodeId else_keyword = fC->Sibling(if_stmt_id);
          if (else_keyword) else_stmt_id = fC->Sibling(else_keyword);
        }
        if (if_stmt_id) {
          if (uhdm::AnyCollection* if_stmts = compileStmt(
                  component, fC, if_stmt_id, expect, instance, muteErrors)) {
            if_stmt = if_stmts->front();
          }
        }
        if (else_stmt_id) {
          if (uhdm::AnyCollection* else_stmts = compileStmt(
                  component, fC, else_stmt_id, expect, instance, muteErrors)) {
            else_stmt = else_stmts->front();
          }
        }
      }
      uhdm::PropertySpec* prop_spec = s.make<uhdm::PropertySpec>();
      NodeId Clocking_event = fC->Child(Property_expr);

      uhdm::Expr* clocking_event = nullptr;
      if (fC->Type(Clocking_event) == VObjectType::paClocking_event) {
        clocking_event = (uhdm::Expr*)compileExpression(
            component, fC, Clocking_event, prop_spec, instance);
        Clocking_event = fC->Sibling(Clocking_event);
      }
      prop_spec->setClockingEvent(clocking_event);
      if (uhdm::Expr* property_expr = any_cast<uhdm::Expr>(compileExpression(
              component, fC, Clocking_event, prop_spec, instance))) {
        prop_spec->setPropertyExpr(property_expr);
      }
      fC->populateCoreMembers(Property_expr, Property_expr, prop_spec);
      prop_spec->setParent(expect);
      expect->setPropertySpec(prop_spec);
      expect->setStmt(if_stmt);
      expect->setElseStmt(else_stmt);
      stmt = expect;
      break;
    }
    case VObjectType::paContinuous_assign: {
      // Non-elab model
      auto assigns = compileContinuousAssignment(
          component, fC, fC->Child(the_stmt), pstmt, nullptr);
      uhdm::AnyCollection* stmts = s.makeCollection<Any>();
      for (auto assign : assigns) {
        assign->setParent(pstmt);
        stmts->emplace_back(assign);
      }
      results = stmts;
      break;
    }
    case VObjectType::paInterface_instantiation:
    case VObjectType::paModule_instantiation:
    case VObjectType::paProgram_instantiation: {
      // Non-elab model
      ModuleDefinition* mod =
          valuedcomponenti_cast<ModuleDefinition*>(component);
      std::pair<std::vector<uhdm::ModuleArray*>, std::vector<uhdm::RefModule*>>
          result = compileInstantiation(mod, fC, pstmt, the_stmt, nullptr);
      if (!result.first.empty()) {
        uhdm::AnyCollection* stmts = s.makeCollection<Any>();
        for (auto mod : result.first) {
          mod->setParent(pstmt);
          stmts->emplace_back(mod);
        }
        results = stmts;
      }
      if (!result.second.empty()) {
        uhdm::AnyCollection* stmts = s.makeCollection<Any>();
        for (auto mod : result.second) {
          mod->setParent(pstmt);
          stmts->emplace_back(mod);
        }
        results = stmts;
      }
      break;
    }
    case VObjectType::paAlways_construct: {
      // Non-elab model
      ModuleDefinition* mod =
          valuedcomponenti_cast<ModuleDefinition*>(component);
      uhdm::Always* always =
          compileAlwaysBlock(mod, fC, the_stmt, pstmt, nullptr);
      stmt = always;
      break;
    }
    case VObjectType::paInitial_construct: {
      // Non-elab model
      ModuleDefinition* mod =
          valuedcomponenti_cast<ModuleDefinition*>(component);
      uhdm::Initial* init = compileInitialBlock(mod, fC, the_stmt, pstmt);
      stmt = init;
      break;
    }
    case VObjectType::paFinal_construct: {
      // Non-elab model
      ModuleDefinition* mod =
          valuedcomponenti_cast<ModuleDefinition*>(component);
      uhdm::FinalStmt* final = compileFinalBlock(mod, fC, the_stmt, pstmt);
      stmt = final;
      break;
    }
    case VObjectType::paNet_declaration: {
      compileNetDeclaration(component, fC, the_stmt, false, nullptr);
      break;
    }
    default:
      break;
  }
  if (stmt) {
    if (attributes) {
      // Only attach attributes to following stmt
      if (uhdm::AtomicStmt* stm = any_cast<uhdm::AtomicStmt>(stmt)) {
        if (attributes != nullptr) {
          stm->setAttributes(attributes);
          for (auto a : *attributes) a->setParent(stm, true);
        }
      }
    }

    fC->populateCoreMembers(the_stmt, the_stmt, stmt);
    stmt->setParent(pstmt);
    results = s.makeCollection<Any>();
    results->emplace_back(stmt);
  } else if (results) {
    for (uhdm::Any* st : *results) {
      st->setParent(pstmt);
      if (uhdm::AtomicStmt* stm = any_cast<uhdm::AtomicStmt>(st)) {
        if (attributes != nullptr) {
          stm->setAttributes(attributes);
          for (auto a : *attributes) a->setParent(stm, true);
        }
      }
    }
  } else {
    VObjectType stmttype = fC->Type(the_stmt);
    if ((muteErrors == false) && (stmttype != VObjectType::END) &&
        (stmttype != VObjectType::paJoin_keyword) &&
        (stmttype != VObjectType::paJoin_any_keyword) &&
        (stmttype != VObjectType::paJoin_none_keyword)) {
      uhdm::UnsupportedStmt* ustmt = s.make<uhdm::UnsupportedStmt>();
      std::string lineText;
      fileSystem->readLine(fC->getFileId(), fC->Line(the_stmt), lineText);
      Location loc(fC->getFileId(the_stmt), fC->Line(the_stmt),
                   fC->Column(the_stmt),
                   symbols->registerSymbol(
                       StrCat("<", fC->printObject(the_stmt), "> ", lineText)));
      Error err(ErrorDefinition::UHDM_UNSUPPORTED_STMT, loc);
      errors->addError(err);
      ustmt->setValue(StrCat("STRING:", lineText));
      fC->populateCoreMembers(the_stmt, the_stmt, ustmt);
      ustmt->setParent(pstmt);
      stmt = ustmt;  // NOLINT
      // std::cout << "UNSUPPORTED STATEMENT: " << fC->getFileName(the_stmt)
      // << ":" << fC->Line(the_stmt) << ":" << std::endl; std::cout << " -> "
      // << fC->printObject(the_stmt) << std::endl;
    }
  }
  return results;
}

uhdm::AnyCollection* CompileHelper::compileDataDeclaration(
    DesignComponent* component, const FileContent* fC, NodeId nodeId,
    uhdm::Any* pstmt, ValuedComponentI* instance) {
  uhdm::Serializer& s = m_compileDesign->getSerializer();
  uhdm::AnyCollection* results = nullptr;
  VObjectType type = fC->Type(nodeId);
  bool automatic_status = false;
  bool const_status = false;
  if (type == VObjectType::paLifetime_Automatic) {
    nodeId = fC->Sibling(nodeId);
    automatic_status = true;
    type = fC->Type(nodeId);
  }
  if (type == VObjectType::paLifetime_Static) {
    nodeId = fC->Sibling(nodeId);
    automatic_status = false;
    type = fC->Type(nodeId);
  }
  if (type == VObjectType::paConst_type) {
    nodeId = fC->Sibling(nodeId);
    const_status = true;
    type = fC->Type(nodeId);
  }
  switch (type) {
    case VObjectType::paAssertion_variable_declaration:
    case VObjectType::paVariable_declaration: {
      NodeId Data_type = fC->Child(nodeId);
      // uhdm::Typespec* ts = compileTypespec(component, fC, Data_type,
      // compileDesign);
      NodeId List_of_variable_decl_assignments = fC->Sibling(Data_type);
      if (fC->Type(List_of_variable_decl_assignments) ==
          VObjectType::paPacked_dimension) {
        List_of_variable_decl_assignments =
            fC->Sibling(List_of_variable_decl_assignments);
      }
      NodeId Variable_decl_assignment =
          fC->Child(List_of_variable_decl_assignments);
      while (Variable_decl_assignment) {
        NodeId Var = Variable_decl_assignment;
        if (fC->Child(Var)) Var = fC->Child(Var);

        uhdm::Assignment* assign_stmt = s.make<uhdm::Assignment>();
        assign_stmt->setParent(pstmt);
        assign_stmt->setOpType(vpiAssignmentOp);
        assign_stmt->setBlocking(true);
        fC->populateCoreMembers(Variable_decl_assignment,
                                Variable_decl_assignment, assign_stmt);

        NodeId UnpackedDimensionsStartId = fC->Sibling(Var);
        if (uhdm::Any* var = compileVariable(component, fC, Data_type, Var,
                                             UnpackedDimensionsStartId,
                                             assign_stmt, instance, false)) {
          fC->populateCoreMembers(Var, Var, var);
          if (uhdm::Expr* const expr = any_cast<uhdm::Expr>(var)) {
            assign_stmt->setLhs(expr);
          }
          if (uhdm::Variable* const v = any_cast<uhdm::Variable>(var)) {
            v->setConstantVariable(const_status);
            v->setAutomatic(automatic_status);
            v->setName(fC->SymName(Var));
          } else if (uhdm::RefObj* const ro = any_cast<uhdm::RefObj>(var)) {
            ro->setName(fC->SymName(Var));
          }
        }

        NodeId Expression = UnpackedDimensionsStartId;
        while (Expression &&
               (fC->Type(Expression) != VObjectType::paExpression)) {
          Expression = fC->Sibling(Expression);
        }
        if (Expression) {
          if (uhdm::Expr* rhs = (uhdm::Expr*)compileExpression(
                  component, fC, Expression, assign_stmt, instance)) {
            assign_stmt->setRhs(rhs);
          }
        }

        if (results == nullptr) results = s.makeCollection<Any>();
        results->emplace_back(assign_stmt);

        Variable_decl_assignment = fC->Sibling(Variable_decl_assignment);
      }
      break;
    }
    case VObjectType::paType_declaration: {
      /* const DataType* dt = */ compileTypeDef(component, fC,
                                                fC->Parent(nodeId), pstmt);
      if (results == nullptr) {
        results = s.makeCollection<Any>();
        // Return an empty list of statements.
        // Type declaration are not executable statements.
      }
      break;
    }
    default:
      break;
  }
  return results;
}

uhdm::AtomicStmt* CompileHelper::compileConditionalStmt(
    DesignComponent* component, const FileContent* fC, NodeId Cond_predicate,
    uhdm::Any* pstmt, ValuedComponentI* instance, bool muteErrors) {
  uhdm::Serializer& s = m_compileDesign->getSerializer();
  int32_t qualifier = 0;
  if (fC->Type(Cond_predicate) == VObjectType::paUnique_priority) {
    NodeId Qualifier = fC->Child(Cond_predicate);
    if (fC->Type(Qualifier) == VObjectType::UNIQUE) {
      qualifier = vpiUniqueQualifier;
    } else if (fC->Type(Qualifier) == VObjectType::PRIORITY) {
      qualifier = vpiPriorityQualifier;
    } else if (fC->Type(Qualifier) == VObjectType::UNIQUE0) {
      qualifier = vpiUniqueQualifier;
    }
    Cond_predicate = fC->Sibling(Cond_predicate);
  }
  NodeId If_branch_stmt = fC->Sibling(Cond_predicate);
  NodeId Else_branch_stmt = fC->Sibling(If_branch_stmt);
  uhdm::AtomicStmt* result_stmt = nullptr;
  if (Else_branch_stmt) {
    uhdm::IfElse* cond_stmt = s.make<uhdm::IfElse>();
    cond_stmt->setQualifier(qualifier);
    if (uhdm::Any* cond_exp = compileExpression(component, fC, Cond_predicate,
                                                cond_stmt, instance)) {
      cond_stmt->setCondition((uhdm::Expr*)cond_exp);
    }
    if (uhdm::AnyCollection* if_stmts = compileStmt(
            component, fC, If_branch_stmt, cond_stmt, instance, muteErrors)) {
      cond_stmt->setStmt(if_stmts->front());
    }
    if (uhdm::AnyCollection* else_stmts = compileStmt(
            component, fC, Else_branch_stmt, cond_stmt, instance, muteErrors)) {
      cond_stmt->setElseStmt(else_stmts->front());
    }
    result_stmt = cond_stmt;
  } else {
    uhdm::IfStmt* cond_stmt = s.make<uhdm::IfStmt>();
    cond_stmt->setQualifier(qualifier);
    if (uhdm::Any* cond_exp = compileExpression(component, fC, Cond_predicate,
                                                cond_stmt, instance)) {
      cond_stmt->setCondition((uhdm::Expr*)cond_exp);
    }
    if (uhdm::AnyCollection* if_stmts = compileStmt(
            component, fC, If_branch_stmt, cond_stmt, instance, muteErrors)) {
      cond_stmt->setStmt(if_stmts->front());
    }
    result_stmt = cond_stmt;
  }
  return result_stmt;
}

uhdm::AtomicStmt* CompileHelper::compileEventControlStmt(
    DesignComponent* component, const FileContent* fC,
    NodeId Procedural_timing_control, uhdm::Any* pstmt,
    ValuedComponentI* instance, bool muteErrors) {
  uhdm::Serializer& s = m_compileDesign->getSerializer();
  /*
  n<#100> u<70> t<IntConst> p<71> l<7>
  n<> u<71> t<Delay_control> p<72> c<70> l<7>
  n<> u<72> t<Procedural_timing_control> p<88> c<71> s<87> l<7>
  */
  NodeId Event_control = fC->Child(Procedural_timing_control);

  NodeId Event_expression = fC->Child(Event_control);
  uhdm::EventControl* event = s.make<uhdm::EventControl>();
  fC->populateCoreMembers(Event_control, Event_control, event);
  event->setParent(pstmt);

  if (Event_expression) {
    if (uhdm::Any* exp =
            compileExpression(component, fC, Event_expression, event)) {
      event->setCondition(exp);
    }
  }  // else @(*) : no event expression
  NodeId Statement_or_null = fC->Sibling(Procedural_timing_control);
  if (uhdm::AnyCollection* stmts = compileStmt(component, fC, Statement_or_null,
                                               event, instance, muteErrors)) {
    if (!stmts->empty()) event->setStmt(stmts->front());
  }
  return event;
}

uhdm::AtomicStmt* CompileHelper::compileRandcaseStmt(
    DesignComponent* component, const FileContent* fC, NodeId nodeId,
    uhdm::Any* pstmt, ValuedComponentI* instance, bool muteErrors) {
  uhdm::Serializer& s = m_compileDesign->getSerializer();
  uhdm::AtomicStmt* result = nullptr;
  NodeId RandCase = fC->Child(nodeId);
  uhdm::CaseStmt* case_stmt = s.make<uhdm::CaseStmt>();
  case_stmt->setRandType(vpiRand);
  uhdm::CaseItemCollection* case_items = case_stmt->getCaseItems(true);
  result = case_stmt;
  while (RandCase) {
    NodeId Expression = fC->Child(RandCase);
    uhdm::CaseItem* case_item = s.make<uhdm::CaseItem>();
    fC->populateCoreMembers(RandCase, RandCase, case_item);
    case_items->emplace_back(case_item);
    uhdm::AnyCollection* exprs = case_item->getExprs(true);
    case_item->setParent(case_stmt);
    if (uhdm::Any* item_exp =
            compileExpression(component, fC, Expression, case_item, instance)) {
      item_exp->setParent(case_item);
      exprs->emplace_back(item_exp);
    }
    NodeId stmt = fC->Sibling(Expression);
    if (uhdm::AnyCollection* stmts =
            compileStmt(component, fC, stmt, case_item, instance, muteErrors)) {
      case_item->setStmt(stmts->front());
    }
    RandCase = fC->Sibling(RandCase);
    if (fC->Type(RandCase) == VObjectType::ENDCASE) {
      break;
    }
  }
  return result;
}

uhdm::AtomicStmt* CompileHelper::compileCaseStmt(
    DesignComponent* component, const FileContent* fC, NodeId nodeId,
    uhdm::Any* pstmt, ValuedComponentI* instance, bool muteErrors) {
  uhdm::Serializer& s = m_compileDesign->getSerializer();
  uhdm::AtomicStmt* result = nullptr;
  NodeId Case_keyword = fC->Child(nodeId);
  NodeId Unique;
  if (fC->Type(Case_keyword) == VObjectType::paUnique_priority) {
    Unique = fC->Child(Case_keyword);
    Case_keyword = fC->Sibling(Case_keyword);
  }
  NodeId Case_type = fC->Child(Case_keyword);
  NodeId Condition = fC->Sibling(Case_keyword);
  uhdm::CaseStmt* case_stmt = s.make<uhdm::CaseStmt>();
  uhdm::Any* cond_exp =
      compileExpression(component, fC, Condition, case_stmt, instance);
  NodeId Case_item = fC->Sibling(Condition);
  uhdm::CaseItemCollection* case_items = case_stmt->getCaseItems(true);
  result = case_stmt;
  case_stmt->setCondition((uhdm::Expr*)cond_exp);
  cond_exp->setParent(case_stmt);
  VObjectType CaseType = fC->Type(Case_type);
  switch (CaseType) {
    case VObjectType::paCase_inside_item:
    case VObjectType::CASE:
      case_stmt->setCaseType(vpiCaseExact);
      break;
    case VObjectType::CASEX:
      case_stmt->setCaseType(vpiCaseX);
      break;
    case VObjectType::CASEZ:
      case_stmt->setCaseType(vpiCaseZ);
      break;
    default:
      break;
  }
  if (Unique) {
    VObjectType UniqueType = fC->Type(Unique);
    switch (UniqueType) {
      case VObjectType::UNIQUE:
        case_stmt->setQualifier(vpiUniqueQualifier);
        break;
      case VObjectType::UNIQUE0:
        case_stmt->setQualifier(vpiNoQualifier);
        break;
      case VObjectType::PRIORITY:
        case_stmt->setQualifier(vpiPriorityQualifier);
        break;
      default:
        break;
    }
  }
  while (Case_item) {
    uhdm::CaseItem* case_item = nullptr;
    if (fC->Type(Case_item) == VObjectType::paCase_item ||
        fC->Type(Case_item) == VObjectType::paCase_inside_item) {
      case_item = s.make<uhdm::CaseItem>();
      case_items->emplace_back(case_item);
      fC->populateCoreMembers(Case_item, Case_item, case_item);
      case_item->setParent(case_stmt);
    }
    bool isDefault = false;
    NodeId Expression;
    if (fC->Type(Case_item) == VObjectType::paCase_item) {
      Expression = fC->Child(Case_item);
      if (fC->Type(Expression) == VObjectType::paExpression) {
        uhdm::AnyCollection* exprs = case_item->getExprs(true);
        while (Expression) {
          if (fC->Type(Expression) == VObjectType::paExpression) {
            // Expr
            if (uhdm::Any* item_exp = compileExpression(
                    component, fC, Expression, case_item, instance)) {
              item_exp->setParent(case_item);
              exprs->emplace_back(item_exp);
            }
          } else {
            // Stmt
            if (uhdm::AnyCollection* stmts =
                    compileStmt(component, fC, Expression, case_item, instance,
                                muteErrors)) {
              case_item->setStmt(stmts->front());
            }
          }
          Expression = fC->Sibling(Expression);
        }
      } else {
        isDefault = true;
      }
    } else if (fC->Type(Case_item) == VObjectType::paCase_inside_item) {
      NodeId Open_range_list = fC->Child(Case_item);
      if (fC->Type(Open_range_list) == VObjectType::paStatement_or_null) {
        isDefault = true;
        Expression = Open_range_list;
      } else {
        uhdm::Operation* insideOp = s.make<uhdm::Operation>();
        insideOp->setOpType(vpiInsideOp);
        uhdm::AnyCollection* operands = insideOp->getOperands(true);
        fC->populateCoreMembers(Open_range_list, Open_range_list, insideOp);
        NodeId Value_range = fC->Child(Open_range_list);
        uhdm::AnyCollection* exprs = case_item->getExprs(true);
        exprs->emplace_back(insideOp);
        insideOp->setParent(case_item);

        while (Value_range) {
          if (uhdm::Expr* item_exp = (uhdm::Expr*)compileExpression(
                  component, fC, Value_range, insideOp)) {
            operands->emplace_back(item_exp);
          }
          Value_range = fC->Sibling(Value_range);
        }
        NodeId Statement_or_null = fC->Sibling(Open_range_list);
        // Stmt
        if (uhdm::AnyCollection* stmts = compileStmt(
                component, fC, Statement_or_null, case_item, instance)) {
          case_item->setStmt(stmts->front());
        }
      }
    }

    if (isDefault) {
      // Default
      if (Expression) {
        if (uhdm::AnyCollection* stmts =
                compileStmt(component, fC, Expression, case_item, instance)) {
          case_item->setStmt(stmts->front());
        }
      }
    }

    Case_item = fC->Sibling(Case_item);
  }

  return result;
}

template <typename T>
std::pair<uhdm::IODeclCollection*, uhdm::VariableCollection*>
CompileHelper::compileTfPortDecl(DesignComponent* component, T* parent,
                                 const FileContent* fC, NodeId tf_item_decl) {
  uhdm::Serializer& s = m_compileDesign->getSerializer();
  std::vector<uhdm::IODecl*>* ios = parent->getIODecls(true);

  /*
n<> u<137> t<TfPortDir_Inp> p<141> s<138> l<28>
n<> u<138> t<Data_type_or_implicit> p<141> s<140> l<28>
n<req_1> u<139> t<StringConst> p<140> l<28>
n<> u<140> t<List_of_tf_variable_identifiers> p<141> c<139> l<28>
n<> u<141> t<Tf_port_declaration> p<142> c<137> l<28>
n<> u<142> t<Tf_item_declaration> p<386> c<141> s<384> l<28>
  */
  std::map<std::string, uhdm::IODecl*, std::less<>> ioMap;

  while (tf_item_decl) {
    if (fC->Type(tf_item_decl) == VObjectType::paTf_item_declaration) {
      NodeId Tf_port_declaration = fC->Child(tf_item_decl);
      if (fC->Type(Tf_port_declaration) == VObjectType::paTf_port_declaration) {
        NodeId TfPortDir = fC->Child(Tf_port_declaration);
        VObjectType tf_port_direction_type = fC->Type(TfPortDir);
        NodeId Data_type_or_implicit = fC->Sibling(TfPortDir);
        NodeId Data_type = fC->Child(Data_type_or_implicit);
        uhdm::Typespec* ts = nullptr;
        if (fC->Type(Data_type) == VObjectType::paData_type) {
          ts = compileTypespec(component, fC, Data_type, InvalidNodeId, parent,
                               nullptr, false);
        } else if (fC->Type(Data_type) == VObjectType::paPacked_dimension) {
          // Implicit type
          uhdm::LogicTypespec* pts = s.make<uhdm::LogicTypespec>();
          pts->setParent(parent);
          fC->populateCoreMembers(Data_type, Data_type, pts);
          int32_t size;
          if (uhdm::RangeCollection* ranges = compileRanges(
                  component, fC, Data_type, pts, nullptr, size, false)) {
            pts->setRanges(ranges);
          }
          ts = pts;
        }

        NodeId List_of_tf_variable_identifiers =
            fC->Sibling(Data_type_or_implicit);
        NodeId nameId = fC->Child(List_of_tf_variable_identifiers);
        while (nameId) {
          uhdm::RangeCollection* ranges = nullptr;
          NodeId Variable_dimension = fC->Sibling(nameId);
          if (fC->Type(Variable_dimension) ==
              VObjectType::paVariable_dimension) {
            int32_t size;
            ranges = compileRanges(component, fC, Variable_dimension, parent,
                                   nullptr, size, false);
          }
          const std::string_view name = fC->SymName(nameId);
          uhdm::IODecl* decl = s.make<uhdm::IODecl>();
          ios->emplace_back(decl);
          decl->setParent(parent);
          decl->setDirection(
              UhdmWriter::getVpiDirection(tf_port_direction_type));
          decl->setName(name);
          ioMap.emplace(name, decl);
          fC->populateCoreMembers(nameId, nameId, decl);
          if (ts != nullptr) {
            if (decl->getTypespec() == nullptr) {
              uhdm::RefTypespec* tsRef = s.make<uhdm::RefTypespec>();
              tsRef->setParent(decl);
              setRefTypespecName(tsRef, ts, fC->SymName(Data_type));
              decl->setTypespec(tsRef);
              fC->populateCoreMembers(Data_type, Data_type, tsRef);
            }
            decl->getTypespec()->setActual(ts);
          }
          decl->setRanges(ranges);
          if (fC->Type(Variable_dimension) ==
              VObjectType::paVariable_dimension) {
            nameId = fC->Sibling(nameId);
          }
          nameId = fC->Sibling(nameId);
        }
      } else if (fC->Type(Tf_port_declaration) ==
                 VObjectType::paBlock_item_declaration) {
        NodeId Data_declaration = fC->Child(Tf_port_declaration);
        if (fC->Type(Data_declaration) == VObjectType::paData_declaration) {
          NodeId Variable_declaration = fC->Child(Data_declaration);
          bool is_static = false;
          if (fC->Type(Variable_declaration) ==
              VObjectType::paLifetime_Automatic) {
            Variable_declaration = fC->Sibling(Variable_declaration);
          } else if (fC->Type(Variable_declaration) ==
                     VObjectType::paLifetime_Static) {
            is_static = true;
            Variable_declaration = fC->Sibling(Variable_declaration);
          }
          NodeId Data_type = fC->Child(Variable_declaration);
          uhdm::Typespec* ts = compileTypespec(
              component, fC, Data_type, InvalidNodeId, parent, nullptr, false);
          NodeId List_of_variable_decl_assignments = fC->Sibling(Data_type);
          NodeId Variable_decl_assignment =
              fC->Child(List_of_variable_decl_assignments);
          while (Variable_decl_assignment) {
            NodeId nameId = fC->Child(Variable_decl_assignment);
            const std::string_view name = fC->SymName(nameId);
            std::map<std::string, uhdm::IODecl*>::iterator itr =
                ioMap.find(name);
            if (itr == ioMap.end()) {
              if (uhdm::Any* var =
                      compileVariable(component, fC, Data_type, nameId,
                                      InvalidNodeId, parent, nullptr, false)) {
                // if (uhdm::Any* var = compileVariable(component, fC,
                // nameId, VObjectType::NO_TYPE, Data_type,
                // ts)) {
                if (uhdm::Variable* const v = any_cast<uhdm::Variable>(var)) {
                  v->setAutomatic(!is_static);
                  v->setName(name);
                }
                if (ts != nullptr) {
                  if (uhdm::Expr* e = any_cast<uhdm::Expr>(var)) {
                    if (e->getTypespec() == nullptr) {
                      uhdm::RefTypespec* tsRef = s.make<uhdm::RefTypespec>();
                      tsRef->setParent(var);
                      fC->populateCoreMembers(Data_type, Data_type, tsRef);
                      e->setTypespec(tsRef);
                    }
                    e->getTypespec()->setActual(ts);
                  }
                }
              }
            } else if (ts != nullptr) {
              if (itr->second->getTypespec() == nullptr) {
                uhdm::RefTypespec* tsRef = s.make<uhdm::RefTypespec>();
                tsRef->setParent(itr->second);
                fC->populateCoreMembers(Data_type, Data_type, tsRef);
                itr->second->setTypespec(tsRef);
              }
              itr->second->getTypespec()->setActual(ts);
            }
            Variable_decl_assignment = fC->Sibling(Variable_decl_assignment);
          }
        }
      }
    }
    tf_item_decl = fC->Sibling(tf_item_decl);
  }
  // std::vector<uhdm::IODecl>* ios = parent->Io_decls(true);
  // std::vector<uhdm::Variables>* vars = parent->Variables(true);
  return std::make_pair(ios, nullptr);
}

std::vector<uhdm::IODecl*>* CompileHelper::compileTfPortList(
    DesignComponent* component, uhdm::Any* parent, const FileContent* fC,
    NodeId tf_port_list) {
  if ((!tf_port_list) ||
      ((fC->Type(tf_port_list) != VObjectType::paTf_port_item_list) &&
       (fC->Type(tf_port_list) != VObjectType::paLet_port_list))) {
    return nullptr;
  }

  uhdm::Serializer& s = m_compileDesign->getSerializer();
  std::vector<uhdm::IODecl*>* ios = s.makeCollection<uhdm::IODecl>();
  /*
   n<c1> u<7> t<StringConst> p<29> s<27> l<10>
   n<> u<8> t<IntegerAtomType_Int> p<9> l<12>
   n<> u<9> t<Data_type> p<10> c<8> l<12>
   n<> u<10> t<Function_data_type> p<11> c<9> l<12>
   n<> u<11> t<Function_data_type_or_implicit> p<24> c<10> s<12> l<12>
   n<try_get> u<12> t<StringConst> p<24> s<22> l<12>
   n<> u<13> t<IntegerAtomType_Int> p<14> l<12>
   n<> u<14> t<Data_type> p<15> c<13> l<12>
   n<> u<15> t<Data_type_or_implicit> p<21> c<14> s<16> l<12>
   n<keyCount> u<16> t<StringConst> p<21> s<20> l<12>
   n<1> u<17> t<IntConst> p<18> l<12>
   n<> u<18> t<Primary_literal> p<19> c<17> l<12>
   n<> u<19> t<Primary> p<20> c<18> l<12>
   n<> u<20> t<Expression> p<21> c<19> l<12>
   n<> u<21> t<Tf_port_item> p<22> c<15> l<12>
   n<> u<22> t<Tf_port_list> p<24> c<21> s<23> l<12>
   n<> u<23> t<Endfunction> p<24> l<13>
   n<> u<24> t<Function_body_declaration> p<25> c<11> l<12>
   n<> u<25> t<Function_declaration> p<26> c<24> l<12>
   n<> u<26> t<Class_method> p<27> c<25> l<12>
  */
  /*
   Or
   n<get> u<47> t<StringConst> p<55> s<53> l<18>
   n<> u<48> t<PortDir_Ref> p<49> l<18>
   n<> u<49> t<Tf_port_direction> p<52> c<48> s<50> l<18>
   n<> u<50> t<Data_type_or_implicit> p<52> s<51> l<18>
   n<message> u<51> t<StringConst> p<52> l<18>
   n<> u<52> t<Tf_port_item> p<53> c<49> l<18>
  */
  // Compile arguments
  NodeId tf_port_item = fC->Child(tf_port_list);
  int32_t previousDirection = vpiInput;
  NodeId prevType = InvalidNodeId;
  uhdm::Typespec* ts = nullptr;
  while (tf_port_item) {
    uhdm::IODecl* decl = s.make<uhdm::IODecl>();
    ios->emplace_back(decl);
    NodeId tf_data_type_or_implicit = fC->Child(tf_port_item);
    NodeId tf_data_type = fC->Child(tf_data_type_or_implicit);
    VObjectType tf_port_direction_type = fC->Type(tf_data_type_or_implicit);
    if (tf_port_direction_type != VObjectType::paData_type_or_implicit)
      previousDirection = UhdmWriter::getVpiDirection(tf_port_direction_type);
    decl->setDirection(previousDirection);
    NodeId tf_param_name = fC->Sibling(tf_data_type_or_implicit);
    if (tf_port_direction_type == VObjectType::paTfPortDir_Ref ||
        tf_port_direction_type == VObjectType::paTfPortDir_ConstRef ||
        tf_port_direction_type == VObjectType::paTfPortDir_Inp ||
        tf_port_direction_type == VObjectType::paTfPortDir_Out ||
        tf_port_direction_type == VObjectType::paTfPortDir_Inout) {
      tf_data_type = fC->Sibling(tf_data_type_or_implicit);
      tf_param_name = fC->Sibling(tf_data_type);
    }
    NodeId type = fC->sl_collect(tf_data_type, VObjectType::paData_type);
    if (fC->Type(type) == VObjectType::VIRTUAL) type = fC->Sibling(type);
    if (prevType == InvalidNodeId) prevType = type;

    NodeId unpackedDimension =
        fC->Sibling(fC->Sibling(fC->Child(tf_port_item)));
    if (unpackedDimension &&
        (fC->Type(unpackedDimension) != VObjectType::paVariable_dimension))
      unpackedDimension = fC->Sibling(unpackedDimension);
    int32_t size;
    if (std::vector<uhdm::Range*>* unpackedDimensions = compileRanges(
            component, fC, unpackedDimension, decl, nullptr, size, false)) {
      decl->setRanges(unpackedDimensions);
    }
    fC->populateCoreMembers(tf_param_name, tf_param_name, decl);
    if (uhdm::Typespec* tempts = compileTypespec(
            component, fC, type, unpackedDimension, decl, nullptr, true)) {
      ts = tempts;
    }
    decl->setParent(parent);

    if (ts != nullptr) {
      if (decl->getTypespec() == nullptr) {
        uhdm::RefTypespec* tsRef = s.make<uhdm::RefTypespec>();
        tsRef->setParent(decl);
        NodeId refName = (type == InvalidNodeId) ? prevType : type;
        std::string_view name = ts->getName();
        if (name.empty() || (name == SymbolTable::getBadSymbol())) {
          name = fC->SymName(refName);
        }
        setRefTypespecName(tsRef, ts, name);
        decl->setTypespec(tsRef);
        fC->populateCoreMembers(refName, refName, tsRef);
      }
      decl->getTypespec()->setActual(ts);
    }

    const std::string_view name = fC->SymName(tf_param_name);
    decl->setName(name);

    NodeId expression = fC->Sibling(tf_param_name);
    if (expression &&
        (fC->Type(expression) != VObjectType::paVariable_dimension) &&
        (fC->Type(type) != VObjectType::STRING_CONST)) {
      if (uhdm::Any* defvalue =
              compileExpression(component, fC, expression, decl, nullptr)) {
        decl->setExpr(defvalue);
      }
    }

    tf_port_item = fC->Sibling(tf_port_item);
  }

  return ios;
}

NodeId CompileHelper::setFuncTaskQualifiers(const FileContent* fC,
                                            NodeId nodeId, uhdm::TaskFunc* tf) {
  NodeId func_decl = nodeId;
  VObjectType func_type = fC->Type(nodeId);

  if (func_type == VObjectType::paFunction_declaration) {
    func_decl = fC->Child(nodeId);
    func_type = fC->Type(func_decl);
  }
  if (func_type == VObjectType::paTask_declaration) {
    func_decl = fC->Child(nodeId);
    func_type = fC->Type(func_decl);
  }

  bool is_local = false;
  bool is_protected = false;
  while ((func_type == VObjectType::paMethodQualifier_Virtual) ||
         (func_type == VObjectType::paMethodQualifier_ClassItem) ||
         (func_type == VObjectType::paPure_virtual_qualifier) ||
         (func_type == VObjectType::paExtern_qualifier) ||
         (func_type == VObjectType::paClassItemQualifier_Protected) ||
         (func_type == VObjectType::paLifetime_Automatic) ||
         (func_type == VObjectType::paLifetime_Static) ||
         (func_type == VObjectType::paDpi_import_export) ||
         (func_type == VObjectType::paPure_keyword) ||
         (func_type == VObjectType::IMPORT) ||
         (func_type == VObjectType::EXPORT) ||
         (func_type == VObjectType::paContext_keyword) ||
         (func_type == VObjectType::STRING_CONST)) {
    if (func_type == VObjectType::paDpi_import_export) {
      func_decl = fC->Child(func_decl);
      func_type = fC->Type(func_decl);
    }
    if (func_type == VObjectType::paPure_keyword) {
      func_decl = fC->Sibling(func_decl);
      func_type = fC->Type(func_decl);
      if (tf) tf->setDPIPure(true);
    }
    if (func_type == VObjectType::EXPORT) {
      func_decl = fC->Sibling(func_decl);
      func_type = fC->Type(func_decl);
      if (tf) tf->setAccessType(vpiDPIExportAcc);
    }
    if (func_type == VObjectType::IMPORT) {
      func_decl = fC->Sibling(func_decl);
      func_type = fC->Type(func_decl);
      if (tf) tf->setAccessType(vpiDPIImportAcc);
    }
    if (func_type == VObjectType::STRING_LITERAL) {
      std::string_view ctype = StringUtils::unquoted(fC->SymName(func_decl));
      if (ctype == "DPI-C") {
        if (tf) tf->setDPICStr(vpiDPIC);
      } else if (ctype == "DPI") {
        if (tf) tf->setDPICStr(vpiDPI);
      }
      func_decl = fC->Sibling(func_decl);
      func_type = fC->Type(func_decl);
    }
    if (func_type == VObjectType::paContext_keyword) {
      func_decl = fC->Sibling(func_decl);
      func_type = fC->Type(func_decl);
      if (tf) tf->setDPIContext(true);
    }
    if (func_type == VObjectType::paMethodQualifier_Virtual) {
      func_decl = fC->Sibling(func_decl);
      func_type = fC->Type(func_decl);
      if (tf) tf->setVirtual(true);
    }
    if (func_type == VObjectType::paLifetime_Automatic) {
      func_decl = fC->Sibling(func_decl);
      func_type = fC->Type(func_decl);
      if (tf) tf->setAutomatic(true);
    }
    if (func_type == VObjectType::paLifetime_Static) {
      func_decl = fC->Sibling(func_decl);
      func_type = fC->Type(func_decl);
    }
    if (func_type == VObjectType::paClassItemQualifier_Protected) {
      is_protected = true;
      if (tf) tf->setVisibility(vpiProtectedVis);
      func_decl = fC->Sibling(func_decl);
      func_type = fC->Type(func_decl);
    }
    if (func_type == VObjectType::paPure_virtual_qualifier) {
      if (tf) tf->setDPIPure(true);
      if (tf) tf->setVirtual(true);
      func_decl = fC->Sibling(func_decl);
      func_type = fC->Type(func_decl);
    }
    if (func_type == VObjectType::paExtern_qualifier) {
      func_decl = fC->Sibling(func_decl);
      func_type = fC->Type(func_decl);
      if (tf) tf->setAccessType(vpiExternAcc);
    }
    if (func_type == VObjectType::paMethodQualifier_ClassItem) {
      NodeId qualifier = fC->Child(func_decl);
      VObjectType type = fC->Type(qualifier);
      if (type == VObjectType::paClassItemQualifier_Static) {
        // TODO: No VPI attribute for static!
      }
      if (type == VObjectType::paClassItemQualifier_Local) {
        if (tf) tf->setVisibility(vpiLocalVis);
        is_local = true;
      }
      if (type == VObjectType::paClassItemQualifier_Protected) {
        is_protected = true;
        if (tf) tf->setVisibility(vpiProtectedVis);
      }
      func_decl = fC->Sibling(func_decl);
      func_type = fC->Type(func_decl);
    }
  }
  if ((!is_local) && (!is_protected)) {
    if (tf) tf->setVisibility(vpiPublicVis);
  }
  return func_decl;
}

bool CompileHelper::compileTask(DesignComponent* component,
                                const FileContent* fC, NodeId id,
                                ValuedComponentI* instance, bool isMethod) {
  uhdm::Serializer& s = m_compileDesign->getSerializer();
  std::vector<uhdm::TaskFunc*>* task_funcs = component->getTaskFuncs();
  if (task_funcs == nullptr) {
    component->setTaskFuncs(s.makeCollection<uhdm::TaskFunc>());
    task_funcs = component->getTaskFuncs();
  }
  uhdm::Any* pscope = component->getUhdmModel();
  if (pscope == nullptr)
    pscope = m_compileDesign->getCompiler()->getDesign()->getUhdmDesign();
  NodeId nodeId =
      (fC->Type(id) == VObjectType::paTask_declaration) ? id : fC->Child(id);
  std::string name;
  NodeId task_decl = setFuncTaskQualifiers(fC, nodeId, nullptr);
  NodeId Task_body_declaration;
  if (fC->Type(task_decl) == VObjectType::paTask_body_declaration)
    Task_body_declaration = task_decl;
  else
    Task_body_declaration = fC->Child(task_decl);
  NodeId task_name = fC->Child(Task_body_declaration);
  if (fC->Type(task_name) == VObjectType::STRING_CONST)
    name = fC->SymName(task_name);
  else if (fC->Type(task_name) == VObjectType::paClass_scope) {
    NodeId Class_type = fC->Child(task_name);
    name.assign(fC->SymName(fC->Child(Class_type)))
        .append("::")
        .append(fC->SymName(fC->Sibling(task_name)));
    task_name = fC->Sibling(task_name);
  }

  uhdm::Task* task = nullptr;
  for (auto f : *component->getTaskFuncs()) {
    if (f->getName() == name) {
      task = reinterpret_cast<uhdm::Task*>(f);
      break;
    }
  }
  if (task == nullptr) {
    // make placeholder first
    task = s.make<uhdm::Task>();
    task->setName(name);
    task->setParent(pscope);
    fC->populateCoreMembers(id, id, task);
    task_funcs->emplace_back(task);
    return true;
  }
  if (task->getIODecls() || task->getVariables() || task->getStmt())
    return true;
  setFuncTaskQualifiers(fC, nodeId, task);
  task->setMethod(isMethod);
  fC->populateCoreMembers(nodeId, task_decl, task);
  const uhdm::ScopedScope scopedScope(task);
  NodeId Tf_port_list = fC->Sibling(task_name);
  NodeId Statement_or_null;
  if (fC->Type(Tf_port_list) == VObjectType::paTf_port_item_list) {
    Statement_or_null = fC->Sibling(Tf_port_list);
    task->setIODecls(compileTfPortList(component, task, fC, Tf_port_list));
  } else if (fC->Type(Tf_port_list) ==
             VObjectType::paTf_item_declaration_list) {
    Statement_or_null = fC->Sibling(Tf_port_list);
    Tf_port_list = fC->Child(Tf_port_list);
    compileTfPortDecl(component, task, fC, Tf_port_list);
    while (Tf_port_list &&
           (fC->Type(Tf_port_list) == VObjectType::paTf_item_declaration)) {
      NodeId Block_item_declaration = fC->Child(Tf_port_list);
      NodeId Parameter_declaration = fC->Child(Block_item_declaration);
      if (Parameter_declaration && (fC->Type(Parameter_declaration) ==
                                    VObjectType::paParameter_declaration)) {
        compileStmt(component, fC, Parameter_declaration, task, instance);
      }
      Tf_port_list = fC->Sibling(Tf_port_list);
    }
  } else if (fC->Type(Tf_port_list) == VObjectType::paBlock_item_declaration) {
    Statement_or_null = Tf_port_list;
  } else if (fC->Type(Tf_port_list) == VObjectType::paStatement_or_null) {
    Statement_or_null = Tf_port_list;
  }

  NodeId MoreStatement_or_null = fC->Sibling(Statement_or_null);
  if (MoreStatement_or_null &&
      (fC->Type(MoreStatement_or_null) == VObjectType::ENDTASK)) {
    MoreStatement_or_null = InvalidNodeId;
  }
  if (MoreStatement_or_null) {
    // Page 983, 2017 Standard: More than 1 Stmts
    uhdm::Begin* begin = s.make<uhdm::Begin>();
    task->setStmt(begin);
    begin->setParent(task);
    const uhdm::ScopedScope scopedScope(begin);
    uhdm::AnyCollection* stmts = begin->getStmts(true);
    const NodeId firstStatementId = Statement_or_null;
    NodeId lastStatementId = Statement_or_null;
    while (Statement_or_null &&
           (fC->Type(Statement_or_null) != VObjectType::ENDTASK)) {
      if (uhdm::AnyCollection* sts =
              compileStmt(component, fC, Statement_or_null, begin, instance)) {
        for (uhdm::Any* st : *sts) {
          uhdm::UhdmType stmt_type = st->getUhdmType();
          if (stmt_type == uhdm::UhdmType::ParamAssign) {
            if (uhdm::ParamAssign* pst = any_cast<uhdm::ParamAssign>(st))
              task->getParamAssigns(true)->emplace_back(pst);
          }
          if (stmt_type == uhdm::UhdmType::Assignment) {
            uhdm::Assignment* stmt = (uhdm::Assignment*)st;
            if (stmt->getRhs() == nullptr ||
                (uhdm::Expr*)stmt->getLhs<uhdm::Variable>()) {
              // Declaration
              if (stmt->getRhs() != nullptr) {
                stmts->emplace_back(st);
              } else {
                if (uhdm::Variable* var = stmt->getLhs<uhdm::Variable>())
                  var->setParent(begin);
                // s.Erase(stmt);
              }
            } else {
              stmts->emplace_back(st);
            }
          } else {
            stmts->emplace_back(st);
          }
          st->setParent(begin);
        }
      }
      lastStatementId = Statement_or_null;
      Statement_or_null = fC->Sibling(Statement_or_null);
    }
    fC->populateCoreMembers(firstStatementId, lastStatementId, begin);
  } else {
    // Page 983, 2017 Standard: 0 or 1 Stmt
    if (Statement_or_null &&
        (fC->Type(Statement_or_null) != VObjectType::ENDTASK)) {
      if (uhdm::AnyCollection* stmts =
              compileStmt(component, fC, Statement_or_null, task, instance)) {
        if (!stmts->empty()) {
          task->setStmt(stmts->at(0));
        }
      }
    }
  }
  return true;
}

bool CompileHelper::compileClassConstructorDeclaration(
    DesignComponent* component, const FileContent* fC, NodeId nodeId) {
  uhdm::Serializer& s = m_compileDesign->getSerializer();
  std::vector<uhdm::TaskFunc*>* task_funcs = component->getTaskFuncs();
  if (task_funcs == nullptr) {
    component->setTaskFuncs(s.makeCollection<uhdm::TaskFunc>());
    task_funcs = component->getTaskFuncs();
  }
  uhdm::Function* func = s.make<uhdm::Function>();
  func->setParent(component->getUhdmModel());
  func->setMethod(true);
  task_funcs->emplace_back(func);
  fC->populateCoreMembers(nodeId, nodeId, func);
  const uhdm::ScopedScope scopedScope(func);
  std::string name = "new";
  std::string className;
  NodeId Tf_port_list;
  Tf_port_list = fC->Child(nodeId);
  if (fC->Type(Tf_port_list) == VObjectType::paClass_scope) {
    NodeId Class_scope = Tf_port_list;
    NodeId Class_type = fC->Child(Class_scope);
    NodeId Class_name = fC->Child(Class_type);
    className = fC->SymName(Class_name);
    name = className + "::new";
    Tf_port_list = fC->Sibling(Tf_port_list);
  }

  func->setName(name);
  func->setIODecls(compileTfPortList(component, func, fC, Tf_port_list));

  NodeId Stmt;
  if (fC->Type(Tf_port_list) == VObjectType::paFunction_statement_or_null) {
    Stmt = Tf_port_list;
  } else {
    Stmt = fC->Sibling(Tf_port_list);
  }
  int32_t nbStmts = 0;
  NodeId StmtTmp = Stmt;
  while (StmtTmp) {
    if (fC->Type(StmtTmp) == VObjectType::paBlock_item_declaration) {
      nbStmts++;
    } else if (fC->Type(StmtTmp) == VObjectType::paSuper_dot_new) {
      nbStmts++;
      NodeId lookAhead = fC->Sibling(StmtTmp);
      if (fC->Type(lookAhead) == VObjectType::paArgument_list) {
        StmtTmp = fC->Sibling(StmtTmp);
      }
    } else if (fC->Type(StmtTmp) == VObjectType::paFunction_statement_or_null) {
      nbStmts++;
    }
    StmtTmp = fC->Sibling(StmtTmp);
  }

  if (nbStmts == 1) {
    if (fC->Type(Stmt) == VObjectType::paSuper_dot_new) {
      uhdm::MethodFuncCall* mcall = s.make<uhdm::MethodFuncCall>();
      mcall->setParent(func);
      mcall->setName("super.new");
      NodeId Args = fC->Sibling(Stmt);
      if (fC->Type(Args) == VObjectType::paArgument_list) {
        if (uhdm::AnyCollection* arguments = compileTfCallArguments(
                component, fC, Args, mcall, nullptr, false)) {
          mcall->setArguments(arguments);
        }
        Stmt = fC->Sibling(Stmt);  // NOLINT(*.DeadStores)
      }
      func->setStmt(mcall);
      fC->populateCoreMembers(Stmt, Args, mcall);
    } else {
      if (NodeId Statement = fC->Child(Stmt)) {
        if (uhdm::AnyCollection* sts =
                compileStmt(component, fC, Statement, func)) {
          uhdm::Any* st = sts->front();
          st->setParent(func);
          func->setStmt(st);
          func->setEndLine(st->getEndLine());
          func->setEndColumn(st->getEndColumn());
        }
      }
    }
  } else if (nbStmts > 1) {
    uhdm::Begin* begin = s.make<uhdm::Begin>();
    func->setStmt(begin);
    begin->setParent(func);
    const uhdm::ScopedScope scopedScope(begin);
    uhdm::AnyCollection* stmts = begin->getStmts(true);
    Stmt = fC->Sibling(Tf_port_list);
    const NodeId firstStmtId = Stmt;
    NodeId lastStmtId = Stmt;
    while (Stmt) {
      if (fC->Type(Stmt) == VObjectType::paSuper_dot_new) {
        uhdm::MethodFuncCall* mcall = s.make<uhdm::MethodFuncCall>();
        mcall->setParent(begin);
        mcall->setName("super.new");
        NodeId Args = fC->Sibling(Stmt);
        if (fC->Type(Args) == VObjectType::paArgument_list) {
          if (uhdm::AnyCollection* arguments = compileTfCallArguments(
                  component, fC, Args, mcall, nullptr, false)) {
            mcall->setArguments(arguments);
          }
          Stmt = fC->Sibling(Stmt);
        }
        fC->populateCoreMembers(Stmt, Args, mcall);
        stmts->emplace_back(mcall);
        mcall->setParent(begin);
      } else {
        if (NodeId Statement = fC->Child(Stmt)) {
          if (uhdm::AnyCollection* sts =
                  compileStmt(component, fC, Statement, begin)) {
            for (uhdm::Any* st : *sts) {
              stmts->emplace_back(st);
              st->setParent(begin);
            }
          }
        }
      }
      lastStmtId = Stmt;
      Stmt = fC->Sibling(Stmt);
    }
    fC->populateCoreMembers(firstStmtId, lastStmtId, begin);
  }

  return true;
}

bool CompileHelper::compileFunction(DesignComponent* component,
                                    const FileContent* fC, NodeId id,
                                    ValuedComponentI* instance, bool isMethod) {
  uhdm::Serializer& s = m_compileDesign->getSerializer();
  std::vector<uhdm::TaskFunc*>* task_funcs = component->getTaskFuncs();
  if (task_funcs == nullptr) {
    component->setTaskFuncs(s.makeCollection<uhdm::TaskFunc>());
    task_funcs = component->getTaskFuncs();
  }
  NodeId nodeId = (fC->Type(id) == VObjectType::paFunction_declaration)
                      ? id
                      : fC->Child(id);
  uhdm::Any* pscope = component->getUhdmModel();
  if (pscope == nullptr)
    pscope = m_compileDesign->getCompiler()->getDesign()->getUhdmDesign();
  std::string name;
  std::string className;
  NodeId func_decl = setFuncTaskQualifiers(fC, nodeId, nullptr);
  VObjectType func_decl_type = fC->Type(func_decl);
  bool constructor = false;
  if (func_decl_type == VObjectType::paClass_constructor_declaration ||
      func_decl_type == VObjectType::paClass_constructor_prototype) {
    constructor = true;
  }
  NodeId Tf_port_list;
  NodeId nameId;
  if (constructor) {
    Tf_port_list = fC->Child(func_decl);
    name = "new";
  } else {
    NodeId Function_body_declaration;
    if (fC->Type(func_decl) == VObjectType::paFunction_body_declaration)
      Function_body_declaration = func_decl;
    else
      Function_body_declaration = fC->Child(func_decl);
    NodeId Function_data_type_or_implicit =
        fC->Child(Function_body_declaration);
    nameId = fC->Sibling(Function_data_type_or_implicit);
    if (fC->Type(nameId) == VObjectType::STRING_CONST) {
      name = fC->SymName(nameId);
      Tf_port_list = fC->Sibling(nameId);
    } else if (fC->Type(nameId) == VObjectType::paClass_scope) {
      NodeId Class_type = fC->Child(nameId);
      NodeId suffixname = fC->Sibling(nameId);
      className = fC->SymName(fC->Child(Class_type));
      name.assign(className).append("::").append(fC->SymName(suffixname));
      Tf_port_list = fC->Sibling(suffixname);
    } else {
      nameId = InvalidNodeId;
    }
  }
  uhdm::Function* func = nullptr;
  for (auto f : *component->getTaskFuncs()) {
    if (f->getName() == name) {
      func = any_cast<uhdm::Function*>(f);
      break;
    }
  }
  if (func == nullptr) {
    // make placeholder first
    func = s.make<uhdm::Function>();
    func->setName(name);
    if (className.empty()) {
      func->setParent(pscope);
    } else if (ClassDefinition* const cd =
                   m_compileDesign->getCompiler()
                       ->getDesign()
                       ->getClassDefinition(StrCat(fC->getLibrary()->getName(),
                                                   "@", className))) {
      func->setParent(cd->getUhdmModel());
    } else {
      func->setParent(pscope);
    }
    fC->populateCoreMembers(id, id, func);
    task_funcs->emplace_back(func);
    return true;
  }
  if (func->getIODecls() || func->getVariables() || func->getStmt())
    return true;
  const uhdm::ScopedScope scopedScope(func);
  setFuncTaskQualifiers(fC, nodeId, func);
  func->setMethod(isMethod);
  if (constructor) {
    uhdm::RefTypespec* tpsRef = s.make<uhdm::RefTypespec>();
    tpsRef->setParent(func);
    tpsRef->setActual(component->getUhdmTypespecModel());
    fC->populateCoreMembers(InvalidNodeId, InvalidNodeId, tpsRef);
    func->setReturn(tpsRef);
  } else {
    NodeId Function_body_declaration;
    if (fC->Type(func_decl) == VObjectType::paFunction_body_declaration)
      Function_body_declaration = func_decl;
    else
      Function_body_declaration = fC->Child(func_decl);
    NodeId Function_data_type_or_implicit =
        fC->Child(Function_body_declaration);
    NodeId Function_data_type = fC->Child(Function_data_type_or_implicit);
    NodeId Return_data_type = fC->Child(Function_data_type);
    if (fC->Type(Function_data_type) == VObjectType::paPacked_dimension) {
      Return_data_type = Function_data_type;
    }
    if ((!Return_data_type) &&
        ((fC->Type(Function_data_type) == VObjectType::paSigning_Unsigned) ||
         (fC->Type(Function_data_type) == VObjectType::paSigning_Signed))) {
      Return_data_type = Function_data_type;
    }

    const uhdm::ScopedScope scopedScope(func);
    uhdm::Typespec* ts = nullptr;
    if (Return_data_type) {
      ts = compileTypespec(component, fC, Return_data_type, InvalidNodeId, func,
                           instance, true);
    } else if (!Function_data_type) {
      // Implicit return type
      ts = s.make<uhdm::LogicTypespec>();
      ts->setParent(func);
      fC->populateCoreMembers(InvalidNodeId, InvalidNodeId, ts);
    } else {  // else void return type
      ts = s.make<uhdm::VoidTypespec>();
      ts->setParent(func);
      fC->populateCoreMembers(Function_data_type, Function_data_type, ts);
    }

    if (ts != nullptr) {
      uhdm::RefTypespec* tsRef = s.make<uhdm::RefTypespec>();
      fC->populateCoreMembers(Return_data_type, Return_data_type, tsRef);
      setRefTypespecName(tsRef, ts, ts->getName());
      tsRef->setParent(func);
      tsRef->setActual(ts);
      func->setReturn(tsRef);
    }
  }

  NodeId Function_statement_or_null = Tf_port_list;
  if (fC->Type(Tf_port_list) == VObjectType::paTf_port_item_list) {
    func->setIODecls(compileTfPortList(component, func, fC, Tf_port_list));
    Function_statement_or_null = fC->Sibling(Tf_port_list);
  } else if (fC->Type(Tf_port_list) ==
             VObjectType::paTf_item_declaration_list) {
    Function_statement_or_null = fC->Sibling(Tf_port_list);
    Tf_port_list = fC->Child(Tf_port_list);
    auto results = compileTfPortDecl(component, func, fC, Tf_port_list);
    func->setIODecls(results.first);
    func->setVariables(results.second);
    while (fC->Type(Tf_port_list) == VObjectType::paTf_item_declaration) {
      NodeId Block_item_declaration = fC->Child(Tf_port_list);
      NodeId Parameter_declaration = fC->Child(Block_item_declaration);
      if ((fC->Type(Parameter_declaration) ==
           VObjectType::paParameter_declaration) ||
          (fC->Type(Parameter_declaration) ==
           VObjectType::paLocal_parameter_declaration)) {
        DesignComponent* tmp =
            new ModuleDefinition(m_session, "fake", fC, InvalidNodeId, s);
        compileParameterDeclaration(tmp, fC, Parameter_declaration, nullptr,
                                    false, false);
        if (uhdm::AnyCollection* const parameters = tmp->getParameters()) {
          for (auto p : *parameters) p->setParent(func, true);
          tmp->setParameters(nullptr);
        }
        if (uhdm::ParamAssignCollection* const paramAssigns =
                tmp->getParamAssigns()) {
          for (auto p : *paramAssigns) p->setParent(func, true);
          tmp->setParamAssigns(nullptr);
        }
        delete tmp;
      }

      Tf_port_list = fC->Sibling(Tf_port_list);
    }
  }

  NodeId MoreFunction_statement_or_null =
      fC->Sibling(Function_statement_or_null);
  if (MoreFunction_statement_or_null &&
      (fC->Type(MoreFunction_statement_or_null) == VObjectType::ENDFUNCTION)) {
    MoreFunction_statement_or_null = InvalidNodeId;
  }
  if (MoreFunction_statement_or_null) {
    // Page 983, 2017 Standard: More than 1 Stmts
    uhdm::Begin* begin = s.make<uhdm::Begin>();
    func->setStmt(begin);
    begin->setParent(func);
    uhdm::AnyCollection* stmts = begin->getStmts(true);
    const uhdm::ScopedScope scopedScope(begin);
    const NodeId firstStatementId = Function_statement_or_null;
    NodeId lastStatementId = Function_statement_or_null;
    while (Function_statement_or_null) {
      if (NodeId Statement = fC->Child(Function_statement_or_null)) {
        if (uhdm::AnyCollection* sts =
                compileStmt(component, fC, Statement, begin, instance)) {
          for (uhdm::Any* st : *sts) {
            uhdm::UhdmType stmt_type = st->getUhdmType();
            if (stmt_type == uhdm::UhdmType::ParamAssign) {
              if (uhdm::ParamAssign* pst = any_cast<uhdm::ParamAssign>(st))
                func->getParamAssigns(true)->emplace_back(pst);
            } else if (stmt_type == uhdm::UhdmType::Assignment) {
              uhdm::Assignment* stmt = (uhdm::Assignment*)st;
              if ((stmt->getRhs() == nullptr) ||
                  stmt->getLhs<uhdm::Variable>()) {
                // Declaration
                if (stmt->getRhs() != nullptr) {
                  stmts->emplace_back(st);
                } else {
                  if (uhdm::Variable* var = stmt->getLhs<uhdm::Variable>())
                    var->setParent(begin);
                  // s.Erase(stmt);
                }
              } else {
                stmts->emplace_back(st);
              }
            } else {
              stmts->emplace_back(st);
            }
            st->setParent(begin);
          }
        }
      }
      lastStatementId = Function_statement_or_null;
      Function_statement_or_null = fC->Sibling(Function_statement_or_null);
    }
    // begin keyword in function is implicit, and so can't have an explicit
    // location. However, set the file parameter anyways.
    // fC->populateCoreMembers(firstStatementId, lastStatementId, begin);
    begin->setFile(
        m_session->getFileSystem()->toPath(fC->getFileId(firstStatementId)));
  } else {
    // Page 983, 2017 Standard: 0 or 1 Stmt
    NodeId Statement = fC->Child(Function_statement_or_null);
    if (Statement) {
      if (uhdm::AnyCollection* sts =
              compileStmt(component, fC, Statement, func, instance)) {
        uhdm::Any* st = sts->front();
        uhdm::UhdmType stmt_type = st->getUhdmType();
        if (stmt_type == uhdm::UhdmType::ParamAssign) {
          if (uhdm::ParamAssign* pst = any_cast<uhdm::ParamAssign>(st))
            func->getParamAssigns(true)->emplace_back(pst);
        } else if (stmt_type == uhdm::UhdmType::Assignment) {
          uhdm::Assignment* stmt = (uhdm::Assignment*)st;
          if ((stmt->getRhs() == nullptr) || stmt->getLhs<uhdm::Variable>()) {
            // Declaration
            if (stmt->getRhs() != nullptr) {
              func->setStmt(st);
            } else {
              if (uhdm::Variable* var = stmt->getLhs<uhdm::Variable>())
                var->setParent(func);
              // s.Erase(stmt);
            }
          } else {
            func->setStmt(st);
          }
        } else {
          func->setStmt(st);
        }
        st->setParent(func);
      }
    }
  }
  return true;
}

NodeId CompileHelper::setFuncTaskDeclQualifiers(const FileContent* fC,
                                                NodeId nodeId,
                                                uhdm::TaskFuncDecl* tfd) {
  NodeId func_decl = nodeId;
  VObjectType func_type = fC->Type(nodeId);

  if (func_type == VObjectType::paFunction_declaration) {
    func_decl = fC->Child(nodeId);
    func_type = fC->Type(func_decl);
  }
  if (func_type == VObjectType::paTask_declaration) {
    func_decl = fC->Child(nodeId);
    func_type = fC->Type(func_decl);
  }

  bool is_local = false;
  bool is_protected = false;
  while ((func_type == VObjectType::paMethodQualifier_Virtual) ||
         (func_type == VObjectType::paMethodQualifier_ClassItem) ||
         (func_type == VObjectType::paPure_virtual_qualifier) ||
         (func_type == VObjectType::paExtern_qualifier) ||
         (func_type == VObjectType::paClassItemQualifier_Protected) ||
         (func_type == VObjectType::paLifetime_Automatic) ||
         (func_type == VObjectType::paLifetime_Static) ||
         (func_type == VObjectType::paDpi_import_export) ||
         (func_type == VObjectType::paPure_keyword) ||
         (func_type == VObjectType::IMPORT) ||
         (func_type == VObjectType::EXPORT) ||
         (func_type == VObjectType::paContext_keyword) ||
         (func_type == VObjectType::STRING_CONST)) {
    if (func_type == VObjectType::paDpi_import_export) {
      func_decl = fC->Child(func_decl);
      func_type = fC->Type(func_decl);
    }
    if (func_type == VObjectType::paPure_keyword) {
      func_decl = fC->Sibling(func_decl);
      func_type = fC->Type(func_decl);
      if (tfd) tfd->setDPIPure(true);
    }
    if (func_type == VObjectType::EXPORT) {
      func_decl = fC->Sibling(func_decl);
      func_type = fC->Type(func_decl);
      if (tfd) tfd->setAccessType(vpiDPIExportAcc);
    }
    if (func_type == VObjectType::IMPORT) {
      func_decl = fC->Sibling(func_decl);
      func_type = fC->Type(func_decl);
      if (tfd) tfd->setAccessType(vpiDPIImportAcc);
    }
    if (func_type == VObjectType::STRING_LITERAL) {
      std::string_view ctype = StringUtils::unquoted(fC->SymName(func_decl));
      if (ctype == "DPI-C") {
        if (tfd) tfd->setDPICStr(vpiDPIC);
      } else if (ctype == "DPI") {
        if (tfd) tfd->setDPICStr(vpiDPI);
      }
      func_decl = fC->Sibling(func_decl);
      func_type = fC->Type(func_decl);
    }
    if (func_type == VObjectType::paContext_keyword) {
      func_decl = fC->Sibling(func_decl);
      func_type = fC->Type(func_decl);
      if (tfd) tfd->setDPIContext(true);
    }
    if (func_type == VObjectType::paMethodQualifier_Virtual) {
      func_decl = fC->Sibling(func_decl);
      func_type = fC->Type(func_decl);
      if (tfd) tfd->setVirtual(true);
    }
    if (func_type == VObjectType::paLifetime_Automatic) {
      func_decl = fC->Sibling(func_decl);
      func_type = fC->Type(func_decl);
      if (tfd) tfd->setAutomatic(true);
    }
    if (func_type == VObjectType::paLifetime_Static) {
      func_decl = fC->Sibling(func_decl);
      func_type = fC->Type(func_decl);
    }
    if (func_type == VObjectType::paClassItemQualifier_Protected) {
      is_protected = true;
      if (tfd) tfd->setVisibility(vpiProtectedVis);
      func_decl = fC->Sibling(func_decl);
      func_type = fC->Type(func_decl);
    }
    if (func_type == VObjectType::paPure_virtual_qualifier) {
      if (tfd) tfd->setDPIPure(true);
      if (tfd) tfd->setVirtual(true);
      func_decl = fC->Sibling(func_decl);
      func_type = fC->Type(func_decl);
    }
    if (func_type == VObjectType::paExtern_qualifier) {
      func_decl = fC->Sibling(func_decl);
      func_type = fC->Type(func_decl);
      if (tfd) tfd->setAccessType(vpiExternAcc);
    }
    if (func_type == VObjectType::paMethodQualifier_ClassItem) {
      NodeId qualifier = fC->Child(func_decl);
      VObjectType type = fC->Type(qualifier);
      if (type == VObjectType::paClassItemQualifier_Static) {
        // TODO: No VPI attribute for static!
      }
      if (type == VObjectType::paClassItemQualifier_Local) {
        if (tfd) tfd->setVisibility(vpiLocalVis);
        is_local = true;
      }
      if (type == VObjectType::paClassItemQualifier_Protected) {
        is_protected = true;
        if (tfd) tfd->setVisibility(vpiProtectedVis);
      }
      func_decl = fC->Sibling(func_decl);
      func_type = fC->Type(func_decl);
    }
  }
  if ((!is_local) && (!is_protected)) {
    if (tfd) tfd->setVisibility(vpiPublicVis);
  }
  return func_decl;
}

Task* CompileHelper::compileTaskPrototype(DesignComponent* scope,
                                          const FileContent* fC, NodeId id) {
  uhdm::Serializer& s = m_compileDesign->getSerializer();
  std::vector<uhdm::TaskFuncDecl*>* task_funcs = scope->getTaskFuncDecls();
  if (task_funcs == nullptr) {
    scope->setTaskFuncDecls(s.makeCollection<uhdm::TaskFuncDecl>());
    task_funcs = scope->getTaskFuncDecls();
  }
  uhdm::TaskDecl* task = s.make<uhdm::TaskDecl>();
  const uhdm::ScopedScope scopedScope(task);
  task_funcs->emplace_back(task);
  NodeId prop = setFuncTaskDeclQualifiers(fC, id, task);
  NodeId task_prototype = prop;
  NodeId task_name = fC->Child(task_prototype);
  std::string taskName(fC->SymName(task_name));
  fC->populateCoreMembers(id, id, task);

  NodeId Tf_port_list;
  if (fC->Type(task_name) == VObjectType::STRING_CONST) {
    Tf_port_list = fC->Sibling(task_name);
  } else if (fC->Type(task_name) == VObjectType::paClass_scope) {
    NodeId Class_type = fC->Child(task_name);
    NodeId suffixname = fC->Sibling(task_name);
    taskName.assign(fC->SymName(fC->Child(Class_type)))
        .append("::")
        .append(fC->SymName(suffixname));
    Tf_port_list = fC->Sibling(suffixname);
  }

  task->setName(taskName);
  task->setParent(scope->getUhdmModel());

  if (fC->Type(Tf_port_list) == VObjectType::paTf_port_item_list) {
    task->setIODecls(compileTfPortList(scope, task, fC, Tf_port_list));
  } else if (fC->Type(Tf_port_list) == VObjectType::paTf_item_declaration) {
    auto results = compileTfPortDecl(scope, task, fC, Tf_port_list);
    task->setIODecls(results.first);
  }

  Task* result = new Task(scope, fC, id, taskName);
  result->compile(*this);
  return result;
}

Function* CompileHelper::compileFunctionPrototype(DesignComponent* scope,
                                                  const FileContent* fC,
                                                  NodeId id) {
  std::string funcName;
  NodeId function_name;
  uhdm::Serializer& s = m_compileDesign->getSerializer();
  std::vector<uhdm::TaskFuncDecl*>* task_funcs = scope->getTaskFuncDecls();
  if (task_funcs == nullptr) {
    scope->setTaskFuncDecls(s.makeCollection<uhdm::TaskFuncDecl>());
    task_funcs = scope->getTaskFuncDecls();
  }
  uhdm::FunctionDecl* func = s.make<uhdm::FunctionDecl>();
  task_funcs->emplace_back(func);
  /*
  n<"DPI-C"> u<2> t<StringLiteral> p<15> s<3> l<3>
  n<> u<3> t<Context_keyword> p<15> s<14> l<3>
  n<> u<4> t<IntVec_TypeBit> p<5> l<3>
  n<> u<5> t<Data_type> p<6> c<4> l<3>
  n<> u<6> t<Function_data_type> p<14> c<5> s<7> l<3>
  n<SV2C_peek> u<7> t<StringConst> p<14> s<13> l<3>
  n<> u<8> t<IntegerAtomType_Int> p<9> l<3>
  n<> u<9> t<Data_type> p<10> c<8> l<3>
  n<> u<10> t<Data_type_or_implicit> p<12> c<9> s<11> l<3>
  n<x_id> u<11> t<StringConst> p<12> l<3>
  n<> u<12> t<Tf_port_item> p<13> c<10> l<3>
  n<> u<13> t<Tf_port_list> p<14> c<12> l<3>
  n<> u<14> t<Function_prototype> p<15> c<6> l<3>
  n<> u<15> t<Dpi_import_export> p<16> c<2> l<3>
   */
  NodeId prop = setFuncTaskDeclQualifiers(fC, id, func);
  NodeId func_prototype = prop;
  NodeId function_data_type = fC->Child(func_prototype);
  NodeId data_type = fC->Child(function_data_type);
  NodeId type = fC->Child(data_type);
  VObjectType the_type = fC->Type(type);
  std::string typeName;
  if (the_type == VObjectType::STRING_CONST) {
    typeName = fC->SymName(type);
  } else if (the_type == VObjectType::paClass_scope) {
    NodeId class_type = fC->Child(type);
    NodeId class_name = fC->Child(class_type);
    typeName = fC->SymName(class_name);
    typeName += "::";
    NodeId symb_id = fC->Sibling(type);
    typeName += fC->SymName(symb_id);
  } else {
    typeName = VObject::getTypeName(the_type);
  }

  if (fC->Type(fC->Child(id)) == VObjectType::EXPORT) {
    function_name = fC->Child(func_prototype);
    funcName = fC->SymName(function_name);
  } else if (fC->Type(fC->Child(id)) == VObjectType::IMPORT) {
    function_name = fC->Sibling(function_data_type);
    funcName = fC->SymName(function_name);
  }

  func->setName(funcName);
  func->setParent(scope->getUhdmModel());
  fC->populateCoreMembers(id, id, func);

  uhdm::Typespec* ts =
      compileTypespec(scope, fC, type, InvalidNodeId, func, nullptr, true);
  NodeId Packed_dimension = type;
  if (fC->Type(Packed_dimension) != VObjectType::paPacked_dimension)
    Packed_dimension = fC->Sibling(type);
  if (!Packed_dimension && (fC->Type(type) == VObjectType::paConstant_range)) {
    Packed_dimension = type;
  }

  if ((ts == nullptr) && Packed_dimension) {
    uhdm::LogicTypespec* lts = s.make<uhdm::LogicTypespec>();
    lts->setParent(func);  // Need input
    fC->populateCoreMembers(type, type, lts);

    int32_t size;
    uhdm::RangeCollection* ranges =
        compileRanges(scope, fC, Packed_dimension, func, nullptr, size, false);
    if ((ranges != nullptr) && !ranges->empty()) {
      lts->setRanges(ranges);
      for (uhdm::Range* r : *ranges) r->setParent(lts, true);
    }
    ts = lts;
  }

  if (ts != nullptr) {
    uhdm::RefTypespec* tsRef = s.make<uhdm::RefTypespec>();
    fC->populateCoreMembers(type, type, tsRef);
    setRefTypespecName(tsRef, ts, ts->getName());
    tsRef->setParent(func);
    tsRef->setActual(ts);
    func->setReturn(tsRef);
  }
  NodeId Tf_port_list;
  if (fC->Type(function_name) == VObjectType::STRING_CONST) {
    Tf_port_list = fC->Sibling(function_name);
  } else if (fC->Type(function_name) == VObjectType::paClass_scope) {
    NodeId Class_type = fC->Child(function_name);
    NodeId suffixname = fC->Sibling(function_name);
    funcName.assign(fC->SymName(fC->Child(Class_type)))
        .append("::")
        .append(fC->SymName(suffixname));
    Tf_port_list = fC->Sibling(suffixname);
  }

  if (fC->Type(Tf_port_list) == VObjectType::paTf_port_item_list) {
    func->setIODecls(compileTfPortList(scope, func, fC, Tf_port_list));
  } else if (fC->Type(Tf_port_list) == VObjectType::paTf_item_declaration) {
    auto results = compileTfPortDecl(scope, func, fC, Tf_port_list);
    func->setIODecls(results.first);
  }

  DataType* returnType = new DataType();
  returnType->init(fC, type, typeName, fC->Type(type));
  Function* result = new Function(scope, fC, id, funcName, returnType);
  Variable* variable =
      new Variable(returnType, fC, id, InvalidNodeId, funcName);
  result->addVariable(variable);
  result->compile(*this);
  return result;
}

uhdm::Any* CompileHelper::compileProceduralContinuousAssign(
    DesignComponent* component, const FileContent* fC, NodeId nodeId) {
  uhdm::Serializer& s = m_compileDesign->getSerializer();
  NodeId assigntypeid = fC->Child(nodeId);
  VObjectType assigntype = fC->Type(assigntypeid);
  uhdm::AtomicStmt* the_stmt = nullptr;
  switch (assigntype) {
    case VObjectType::ASSIGN: {
      uhdm::AssignStmt* assign = s.make<uhdm::AssignStmt>();
      NodeId Variable_assignment = fC->Sibling(assigntypeid);
      NodeId Variable_lvalue = fC->Child(Variable_assignment);
      NodeId Hierarchical_identifier = fC->Child(Variable_lvalue);
      if (fC->Type(fC->Child(Hierarchical_identifier)) ==
          VObjectType::paHierarchical_identifier) {
        Hierarchical_identifier = fC->Child(fC->Child(Hierarchical_identifier));
      } else if (fC->Type(Hierarchical_identifier) !=
                 VObjectType::paPs_or_hierarchical_identifier) {
        Hierarchical_identifier = Variable_lvalue;
      }
      NodeId Expression = fC->Sibling(Variable_lvalue);
      if (uhdm::Expr* const lhs = (uhdm::Expr*)compileExpression(
              component, fC, Hierarchical_identifier, assign)) {
        assign->setLhs(lhs);
      }
      if (uhdm::Expr* const rhs = (uhdm::Expr*)compileExpression(
              component, fC, Expression, assign)) {
        assign->setRhs(rhs);
      }
      the_stmt = assign;
      break;
    }
    case VObjectType::FORCE: {
      uhdm::Force* assign = s.make<uhdm::Force>();
      NodeId Variable_assignment = fC->Sibling(assigntypeid);
      NodeId Variable_lvalue = fC->Child(Variable_assignment);
      NodeId Hierarchical_identifier = fC->Child(Variable_lvalue);
      if (fC->Type(fC->Child(Hierarchical_identifier)) ==
          VObjectType::paHierarchical_identifier) {
        Hierarchical_identifier = fC->Child(fC->Child(Hierarchical_identifier));
      } else if (fC->Type(Hierarchical_identifier) !=
                 VObjectType::paPs_or_hierarchical_identifier) {
        Hierarchical_identifier = Variable_lvalue;
      }
      NodeId Expression = fC->Sibling(Variable_lvalue);
      if (uhdm::Expr* lhs = (uhdm::Expr*)compileExpression(
              component, fC, Hierarchical_identifier, assign)) {
        assign->setLhs(lhs);
      }
      if (uhdm::Expr* rhs = (uhdm::Expr*)compileExpression(
              component, fC, Expression, assign)) {
        assign->setRhs(rhs);
      }
      the_stmt = assign;
      break;
    }
    case VObjectType::DEASSIGN: {
      uhdm::Deassign* assign = s.make<uhdm::Deassign>();
      NodeId Variable_assignment = fC->Sibling(assigntypeid);
      NodeId Variable_lvalue = fC->Child(Variable_assignment);
      NodeId Hierarchical_identifier = fC->Child(Variable_lvalue);
      if (fC->Type(fC->Child(Hierarchical_identifier)) ==
          VObjectType::paHierarchical_identifier) {
        Hierarchical_identifier = fC->Child(fC->Child(Hierarchical_identifier));
      } else if (fC->Type(Hierarchical_identifier) !=
                 VObjectType::paPs_or_hierarchical_identifier) {
        Hierarchical_identifier = Variable_lvalue;
      }
      if (uhdm::Expr* lhs = (uhdm::Expr*)compileExpression(
              component, fC, Hierarchical_identifier, assign)) {
        assign->setLhs(lhs);
      }
      the_stmt = assign;
      break;
    }
    case VObjectType::RELEASE: {
      uhdm::Release* assign = s.make<uhdm::Release>();
      NodeId Variable_assignment = fC->Sibling(assigntypeid);
      NodeId Variable_lvalue = fC->Child(Variable_assignment);
      NodeId Hierarchical_identifier = fC->Child(Variable_lvalue);
      if (fC->Type(fC->Child(Hierarchical_identifier)) ==
          VObjectType::paHierarchical_identifier) {
        Hierarchical_identifier = fC->Child(fC->Child(Hierarchical_identifier));
      } else if (fC->Type(Hierarchical_identifier) !=
                 VObjectType::paPs_or_hierarchical_identifier) {
        Hierarchical_identifier = Variable_lvalue;
      }
      if (uhdm::Expr* lhs = (uhdm::Expr*)compileExpression(
              component, fC, Hierarchical_identifier, assign)) {
        assign->setLhs(lhs);
      }
      the_stmt = assign;
      break;
    }
    default:
      break;
  }
  return the_stmt;
}

uhdm::Any* CompileHelper::compileForLoop(DesignComponent* component,
                                         const FileContent* fC, NodeId nodeId,
                                         bool muteErrors) {
  /*
    n<> u<36> t<IntegerAtomType_Int> p<37> l<5>
    n<> u<37> t<Data_type> p<43> c<36> s<38> l<5>
    n<i> u<38> t<StringConst> p<43> s<42> l<5>
    n<PTR_WIDTH> u<39> t<StringConst> p<40> l<5>
    n<> u<40> t<Primary_literal> p<41> c<39> l<5>
    n<> u<41> t<Primary> p<42> c<40> l<5>
    n<> u<42> t<Expression> p<43> c<41> l<5>
    n<> u<43> t<For_variable_declaration> p<44> c<37> l<5>
    n<> u<44> t<For_initialization> p<84> c<43> s<54> l<5>
    n<i> u<45> t<StringConst> p<46> l<5>
    n<> u<46> t<Primary_literal> p<47> c<45> l<5>
    n<> u<47> t<Primary> p<48> c<46> l<5>
    n<> u<48> t<Expression> p<54> c<47> s<49> l<5>
    n<> u<49> t<BinOp_GreatEqual> p<54> s<53> l<5>
    n<0> u<50> t<IntConst> p<51> l<5>
    n<> u<51> t<Primary_literal> p<52> c<50> l<5>
    n<> u<52> t<Primary> p<53> c<51> l<5>
    n<> u<53> t<Expression> p<54> c<52> l<5>
    n<> u<54> t<Expression> p<84> c<48> s<63> l<5>
    n<i> u<55> t<StringConst> p<56> l<5>
    n<> u<56> t<Hierarchical_identifier> p<59> c<55> s<58> l<5>
    n<> u<57> t<Bit_select> p<58> l<5>
    n<> u<58> t<Select> p<59> c<57> l<5>
    n<> u<59> t<Variable_lvalue> p<61> c<56> s<60> l<5>
    n<> u<60> t<IncDec_MinusMinus> p<61> l<5>
    n<> u<61> t<Inc_or_dec_expression> p<62> c<59> l<5>
    n<> u<62> t<For_step_assignment> p<63> c<61> l<5>
    n<> u<63> t<For_step> p<84> c<62> s<82> l<5>
    n<dec_tmp> u<64> t<StringConst> p<65> l<6>
    n<> u<65> t<Hierarchical_identifier> p<72> c<64> s<71> l<6>
    n<i> u<66> t<StringConst> p<67> l<6>
    n<> u<67> t<Primary_literal> p<68> c<66> l<6>
    n<> u<68> t<Primary> p<69> c<67> l<6>
    n<> u<69> t<Expression> p<70> c<68> l<6>
    n<> u<70> t<Bit_select> p<71> c<69> l<6>
    n<> u<71> t<Select> p<72> c<70> l<6>
    n<> u<72> t<Variable_lvalue> p<78> c<65> s<73> l<6>
    n<> u<73> t<AssignOp_Assign> p<78> s<77> l<6>
    n<0> u<74> t<IntConst> p<75> l<6>
    n<> u<75> t<Primary_literal> p<76> c<74> l<6>
    n<> u<76> t<Primary> p<77> c<75> l<6>
    n<> u<77> t<Expression> p<78> c<76> l<6>
    n<> u<78> t<Operator_assignment> p<79> c<72> l<6>
    n<> u<79> t<Blocking_assignment> p<80> c<78> l<6>
    n<> u<80> t<Statement_item> p<81> c<79> l<6>
    n<> u<81> t<Statement> p<82> c<80> l<6>
    n<> u<82> t<Statement_or_null> p<84> c<81> l<6>
    n<> u<83> t<For> p<84> s<44> l<5>
  */
  uhdm::Serializer& s = m_compileDesign->getSerializer();
  uhdm::ForStmt* for_stmt = s.make<uhdm::ForStmt>();
  NodeId For_initialization;
  NodeId Condition;
  NodeId For_step;
  NodeId Statement_or_null;
  NodeId tmp = fC->Sibling(nodeId);
  while (tmp) {
    if (fC->Type(tmp) == VObjectType::paFor_initialization) {
      For_initialization = tmp;
    } else if (fC->Type(tmp) == VObjectType::paExpression) {
      Condition = tmp;
    } else if (fC->Type(tmp) == VObjectType::paFor_step) {
      For_step = tmp;
    } else if (fC->Type(tmp) == VObjectType::paStatement_or_null) {
      Statement_or_null = tmp;
    }
    tmp = fC->Sibling(tmp);
  }
  fC->populateCoreMembers(nodeId, Statement_or_null, for_stmt);
  // Init
  if (For_initialization) {
    NodeId For_variable_declaration = fC->Child(For_initialization);
    if (fC->Type(For_variable_declaration) ==
        VObjectType::paFor_variable_declaration) {
      while (For_variable_declaration) {
        uhdm::AnyCollection* stmts = for_stmt->getForInitStmts(true);
        NodeId Data_type = fC->Child(For_variable_declaration);
        NodeId Var = fC->Sibling(Data_type);
        NodeId Expression = fC->Sibling(Var);
        uhdm::Assignment* assign_stmt = s.make<uhdm::Assignment>();
        assign_stmt->setParent(for_stmt);
        fC->populateCoreMembers(For_variable_declaration,
                                For_variable_declaration, assign_stmt);

        if (uhdm::Any* var =
                compileVariable(component, fC, Data_type, Var, InvalidNodeId,
                                assign_stmt, nullptr, false)) {
          if (uhdm::Expr* const v = any_cast<uhdm::Expr>(var)) {
            assign_stmt->setLhs(v);
          }
          if (uhdm::Variable* const v = any_cast<uhdm::Variable>(var)) {
            v->setName(fC->SymName(Var));
          } else if (uhdm::RefObj* const ro = any_cast<uhdm::RefObj>(var)) {
            ro->setName(fC->SymName(Var));
          }
        }
        if (uhdm::Expr* rhs = (uhdm::Expr*)compileExpression(
                component, fC, Expression, assign_stmt)) {
          assign_stmt->setRhs(rhs);
        }
        stmts->emplace_back(assign_stmt);

        For_variable_declaration = fC->Sibling(For_variable_declaration);
      }
    } else if (fC->Type(For_variable_declaration) ==
               VObjectType::paVariable_assignment_list) {
      NodeId List_of_variable_assignments = For_variable_declaration;
      NodeId Variable_assignment = fC->Child(List_of_variable_assignments);
      while (Variable_assignment) {
        NodeId Variable_lvalue = fC->Child(Variable_assignment);
        NodeId Hierarchical_identifier = fC->Child(Variable_lvalue);
        if (fC->Type(fC->Child(Hierarchical_identifier)) ==
            VObjectType::paHierarchical_identifier) {
          Hierarchical_identifier =
              fC->Child(fC->Child(Hierarchical_identifier));
        } else if (fC->Type(Hierarchical_identifier) !=
                   VObjectType::paPs_or_hierarchical_identifier) {
          Hierarchical_identifier = Variable_lvalue;
        }
        NodeId Expression = fC->Sibling(Variable_lvalue);
        uhdm::AnyCollection* stmts = for_stmt->getForInitStmts(true);
        uhdm::Assignment* assign_stmt = s.make<uhdm::Assignment>();
        assign_stmt->setParent(for_stmt);
        fC->populateCoreMembers(Variable_assignment, Variable_assignment,
                                assign_stmt);

        // No type information available so create only a reference.
        // The actual variable is likely declared ahead.
        NodeId nameId = fC->Child(Hierarchical_identifier);
        uhdm::RefObj* var = s.make<uhdm::RefObj>();
        var->setName(fC->SymName(nameId));
        fC->populateCoreMembers(nameId, nameId, var);
        var->setParent(assign_stmt);
        assign_stmt->setLhs(var);

        if (uhdm::Expr* const rhs = (uhdm::Expr*)compileExpression(
                component, fC, Expression, assign_stmt)) {
          assign_stmt->setRhs(rhs);
        }
        stmts->emplace_back(assign_stmt);

        Variable_assignment = fC->Sibling(Variable_assignment);
      }
    }
  }

  // Condition
  if (Condition) {
    if (uhdm::Expr* cond = (uhdm::Expr*)compileExpression(
            component, fC, Condition, for_stmt)) {
      for_stmt->setCondition(cond);
    }
  }

  // Increment
  if (For_step) {
    NodeId For_step_assignment = fC->Child(For_step);
    while (For_step_assignment) {
      uhdm::AnyCollection* stmts = for_stmt->getForIncStmts(true);

      NodeId Expression = fC->Child(For_step_assignment);
      if (fC->Type(Expression) == VObjectType::paOperator_assignment) {
        if (std::vector<uhdm::Any*>* incstmts =
                compileStmt(component, fC, Expression, for_stmt)) {
          for (auto stmt : *incstmts) {
            stmts->emplace_back(stmt);
          }
        }
      } else {
        if (uhdm::Expr* exp = (uhdm::Expr*)compileExpression(
                component, fC, Expression, for_stmt)) {
          stmts->emplace_back(exp);
        }
      }
      For_step_assignment = fC->Sibling(For_step_assignment);
    }
  }

  // Stmt
  if (Statement_or_null) {
    if (uhdm::AnyCollection* stmts = compileStmt(
            component, fC, Statement_or_null, for_stmt, nullptr, muteErrors)) {
      for_stmt->setStmt(stmts->front());
    }
  }

  return for_stmt;
}

uhdm::MethodFuncCall* CompileHelper::compileRandomizeCall(
    DesignComponent* component, const FileContent* fC, NodeId id,
    uhdm::Any* pexpr) {
  uhdm::Serializer& s = m_compileDesign->getSerializer();
  uhdm::MethodFuncCall* func_call = s.make<uhdm::MethodFuncCall>();
  func_call->setParent(pexpr);
  func_call->setName("randomize");
  fC->populateCoreMembers(id, id, func_call);

  NodeId Identifier_list = id;
  if (fC->Type(id) == VObjectType::paRandomize_call) {
    Identifier_list = fC->Child(id);
  }

  NodeId With;
  if (fC->Type(Identifier_list) == VObjectType::paIdentifier_list) {
    With = fC->Sibling(Identifier_list);
  } else if (fC->Type(Identifier_list) == VObjectType::WITH) {
    With = Identifier_list;
  }
  NodeId Constraint_block = fC->Sibling(With);
  if (fC->Type(Identifier_list) == VObjectType::paIdentifier_list) {
    if (uhdm::AnyCollection* arguments = compileTfCallArguments(
            component, fC, Identifier_list, func_call, nullptr, false)) {
      func_call->setArguments(arguments);
    }
  }

  if (Constraint_block) {
    if (uhdm::Any* cblock = compileConstraintBlock(
            component, fC, Constraint_block, func_call)) {
      func_call->setWith(cblock);
    }
  }
  return func_call;
}

uhdm::Any* CompileHelper::compileConstraintBlock(DesignComponent* component,
                                                 const FileContent* fC,
                                                 NodeId nodeId,
                                                 uhdm::Any* pexpr) {
  NodeId constraint_name = fC->Child(fC->Child(nodeId));
  uhdm::Serializer& s = m_compileDesign->getSerializer();
  uhdm::Any* result = nullptr;
  uhdm::Constraint* cons = s.make<uhdm::Constraint>();
  cons->setParent(pexpr);
  cons->setName(fC->SymName(constraint_name));
  fC->populateCoreMembers(nodeId, nodeId, cons);
  result = cons;
  return result;
}

void CompileHelper::compileBindStmt(DesignComponent* component,
                                    const FileContent* fC,
                                    NodeId Bind_directive,
                                    ValuedComponentI* instance) {
  /*
  n<bp_lce> u<46> t<StringConst> p<65> s<64> l<9:5> el<9:11>
  n<bp_me_nonsynth_lce_tracer> u<47> t<StringConst> p<63> s<57> l<10:12>
  el<10:37> n<sets_p> u<48> t<StringConst> p<55> s<54> l<11:17> el<11:23>
  n<sets_p> u<49> t<StringConst> p<50> l<11:24> el<11:30>
  n<> u<50> t<Primary_literal> p<51> c<49> l<11:24> el<11:30>
  n<> u<51> t<Primary> p<52> c<50> l<11:24> el<11:30>
  n<> u<52> t<Expression> p<53> c<51> l<11:24> el<11:30>
  n<> u<53> t<Mintypmax_expression> p<54> c<52> l<11:24> el<11:30>
  n<> u<54> t<Param_expression> p<55> c<53> l<11:24> el<11:30>
  n<> u<55> t<Named_parameter_assignment> p<56> c<48> l<11:16> el<11:31>
  n<> u<56> t<List_of_parameter_assignments> p<57> c<55> l<11:16> el<11:31>
  n<> u<57> t<Parameter_value_assignment> p<63> c<56> s<62> l<11:14> el<11:32>
  n<lce_tracer1> u<58> t<StringConst> p<59> l<12:14> el<12:25>
  n<> u<59> t<Name_of_instance> p<62> c<58> s<61> l<12:14> el<12:25>
  n<> u<60> t<Ordered_port_connection> p<61> l<12:26> el<12:26>
  n<> u<61> t<List_of_port_connections> p<62> c<60> l<12:26> el<12:26>
  n<> u<62> t<Hierarchical_instance> p<63> c<59> l<12:14> el<12:27>
  n<> u<63> t<Module_instantiation> p<64> c<47> l<10:12> el<12:28>
  n<> u<64> t<Bind_instantiation> p<65> c<63> l<10:12> el<12:28>
  n<> u<65> t<Bind_directive> p<66> c<46> l<9:0> el<12:28>
  */
  /*
    bind_directive:
    bind bind_target_scope [ : bind_target_instance_list ] bind_instantiation ;
  | bind bind_target_instance bind_instantiation ;
  */
  NodeId Target_scope = fC->Child(Bind_directive);
  const std::string_view targetName = fC->SymName(Target_scope);
  NodeId Bind_instantiation = fC->Sibling(Target_scope);
  NodeId Instance_target;
  if (fC->Type(Bind_instantiation) == VObjectType::STRING_CONST) {
    Instance_target = Bind_instantiation;
    NodeId Constant_bit_select = fC->Sibling(Bind_instantiation);
    Bind_instantiation = fC->Sibling(Constant_bit_select);
  }
  NodeId Module_instantiation = fC->Child(Bind_instantiation);
  NodeId Source_scope = fC->Child(Module_instantiation);
  NodeId tmp = fC->Sibling(Source_scope);
  if (fC->Type(tmp) == VObjectType::paParameter_value_assignment) {
    tmp = fC->Sibling(tmp);
  }
  NodeId Instance_name = tmp;
  Instance_name = fC->Child(fC->Child(Instance_name));
  std::string fullName = StrCat(fC->getLibrary()->getName(), "@", targetName);
  BindStmt* bind = new BindStmt(fC, Module_instantiation, Target_scope,
                                Instance_target, Source_scope, Instance_name);
  m_compileDesign->getCompiler()->getDesign()->addBindStmt(fullName, bind);
}

uhdm::Any* CompileHelper::compileCheckerInstantiation(
    DesignComponent* component, const FileContent* fC, NodeId nodeId,
    uhdm::Any* pstmt, ValuedComponentI* instance) {
  uhdm::Serializer& s = m_compileDesign->getSerializer();
  uhdm::CheckerInst* result = s.make<uhdm::CheckerInst>();
  NodeId Ps_identifier = fC->Child(nodeId);
  const std::string_view CheckerName = fC->SymName(Ps_identifier);
  result->setDefName(CheckerName);
  NodeId Name_of_instance = fC->Sibling(nodeId);
  NodeId InstanceName = fC->Child(Name_of_instance);
  const std::string_view InstName = fC->SymName(InstanceName);
  result->setName(InstName);
  result->setParent(pstmt);
  return result;
}

}  // namespace SURELOG
