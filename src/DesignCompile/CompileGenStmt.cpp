/*
 Copyright 2019-2023 Alain Dargelas

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
 * File:   CompileGenStmt.cpp
 * Author: alain
 *
 * Created on May 14, 2023, 11:45 AM
 */

#include <Surelog/CommandLine/CommandLineParser.h>
#include <Surelog/Common/FileSystem.h>
#include <Surelog/Design/DataType.h>
#include <Surelog/Design/DummyType.h>
#include <Surelog/Design/Enum.h>
#include <Surelog/Design/FileContent.h>
#include <Surelog/Design/ModuleDefinition.h>
#include <Surelog/Design/ModuleInstance.h>
#include <Surelog/Design/Netlist.h>
#include <Surelog/Design/ParamAssign.h>
#include <Surelog/Design/Parameter.h>
#include <Surelog/Design/Signal.h>
#include <Surelog/Design/SimpleType.h>
#include <Surelog/Design/Struct.h>
#include <Surelog/Design/TfPortItem.h>
#include <Surelog/Design/Union.h>
#include <Surelog/DesignCompile/CompileDesign.h>
#include <Surelog/DesignCompile/CompileHelper.h>
#include <Surelog/DesignCompile/UhdmWriter.h>
#include <Surelog/ErrorReporting/Error.h>
#include <Surelog/ErrorReporting/ErrorContainer.h>
#include <Surelog/ErrorReporting/Location.h>
#include <Surelog/Library/Library.h>
#include <Surelog/Package/Package.h>
#include <Surelog/SourceCompile/Compiler.h>
#include <Surelog/SourceCompile/SymbolTable.h>
#include <Surelog/Testbench/ClassDefinition.h>
#include <Surelog/Testbench/Program.h>
#include <Surelog/Testbench/TypeDef.h>
#include <Surelog/Testbench/Variable.h>
#include <Surelog/Utils/NumUtils.h>
#include <Surelog/Utils/StringUtils.h>

// UHDM
#include <string.h>
#include <uhdm/ElaboratorListener.h>
#include <uhdm/ExprEval.h>
#include <uhdm/VpiListener.h>
#include <uhdm/clone_tree.h>
#include <uhdm/uhdm.h>

#include <climits>
#include <iostream>
#include <string>
#include <vector>

namespace SURELOG {

using namespace UHDM;  // NOLINT (we use a good chunk of these here)

UHDM::VectorOfany* CompileHelper::compileGenStmt(ModuleDefinition* component,
                                                 const FileContent* fC,
                                                 CompileDesign* compileDesign,
                                                 NodeId id) {
  Serializer& s = compileDesign->getSerializer();
  NodeId stmtId = fC->Child(id);
  any* genstmt = nullptr;
  if (fC->Type(id) == VObjectType::paGenerate_region) {
    gen_region* genreg = s.MakeGen_region();
    fC->populateCoreMembers(id, id, genreg);
    genstmt = genreg;
    const ScopedScope scopedScope1(genreg);

    begin* stmt = s.MakeBegin();
    stmt->VpiParent(genreg);
    fC->populateCoreMembers(stmtId, stmtId, stmt);
    genreg->VpiStmt(stmt);

    checkForLoops(true);
    const ScopedScope scopedScope2(stmt);
    if (VectorOfany* stmts = compileStmt(component, fC, stmtId, compileDesign,
                                         Reduce::No, stmt, nullptr, true)) {
      stmt->Stmts(stmts);
    }
    checkForLoops(false);

    NodeId blockNameId = fC->Child(fC->Child(stmtId));
    if (fC->Type(blockNameId) == VObjectType::slStringConst) {
      stmt->VpiName(fC->SymName(blockNameId));
    }
  } else if (fC->Type(stmtId) ==
             VObjectType::paIf_generate_construct) {  // If, If-Else stmt
    NodeId ifElseId = fC->Child(stmtId);
    if (fC->Type(ifElseId) == VObjectType::paIF) {
      // lookahead
      NodeId tmp = ifElseId;
      bool ifelse = false;
      while (tmp) {
        if (fC->Type(tmp) == VObjectType::paELSE) {
          ifelse = true;
          break;
        }
        tmp = fC->Sibling(tmp);
      }

      NodeId condId = fC->Sibling(ifElseId);
      checkForLoops(true);
      expr* cond = (expr*)compileExpression(component, fC, condId,
                                            compileDesign, Reduce::No, nullptr);
      checkForLoops(false);
      NodeId stmtId = fC->Sibling(condId);
      if (ifelse) {
        gen_if_else* genif = s.MakeGen_if_else();
        genstmt = genif;
        if (cond != nullptr) {
          cond->VpiParent(genif);
          genif->VpiCondition(cond);
        }

        begin* stmt1 = s.MakeBegin();
        fC->populateCoreMembers(stmtId, stmtId, stmt1);
        stmt1->VpiParent(genif);
        genif->VpiStmt(stmt1);

        checkForLoops(true);
        {
          const ScopedScope scopedScope1(stmt1);
          if (VectorOfany* stmts =
                  compileStmt(component, fC, stmtId, compileDesign, Reduce::No,
                              stmt1, nullptr, true)) {
            stmt1->Stmts(stmts);
          }
        }
        checkForLoops(false);

        NodeId blockNameId = fC->Child(fC->Child(stmtId));
        if (fC->Type(blockNameId) == VObjectType::slStringConst) {
          stmt1->VpiName(fC->SymName(blockNameId));
        }

        NodeId ElseId = fC->Sibling(stmtId);
        NodeId elseStmtId = fC->Sibling(ElseId);

        begin* stmt2 = s.MakeBegin();
        fC->populateCoreMembers(elseStmtId, elseStmtId, stmt2);
        stmt2->VpiParent(genif);
        genif->VpiElseStmt(stmt2);

        checkForLoops(true);
        {
          const ScopedScope scopedScope2(stmt2);
          if (VectorOfany* stmts =
                  compileStmt(component, fC, elseStmtId, compileDesign,
                              Reduce::No, stmt2, nullptr, true)) {
            stmt2->Stmts(stmts);
          }
        }
        checkForLoops(false);

        blockNameId = fC->Child(fC->Child(elseStmtId));
        if (fC->Type(blockNameId) == VObjectType::slStringConst) {
          stmt2->VpiName(fC->SymName(blockNameId));
        }
        fC->populateCoreMembers(ifElseId, elseStmtId, genif);
      } else {
        gen_if* genif = s.MakeGen_if();
        genstmt = genif;
        if (cond != nullptr) {
          genif->VpiCondition(cond);
          cond->VpiParent(genif);
        }
        fC->populateCoreMembers(id, id, genif);

        begin* stmt = s.MakeBegin();
        fC->populateCoreMembers(stmtId, stmtId, stmt);
        stmt->VpiParent(genif);
        genif->VpiStmt(stmt);

        checkForLoops(true);
        const ScopedScope scopedScope(stmt);
        if (VectorOfany* stmts =
                compileStmt(component, fC, stmtId, compileDesign, Reduce::No,
                            stmt, nullptr, true)) {
          stmt->Stmts(stmts);
        }
        checkForLoops(false);

        NodeId blockNameId = fC->Child(fC->Child(stmtId));
        if (fC->Type(blockNameId) == VObjectType::slStringConst) {
          stmt->VpiName(fC->SymName(blockNameId));
        }
      }
    }
  } else if (fC->Type(stmtId) ==
             VObjectType::paCase_generate_construct) {  // Case
    NodeId tmp = fC->Child(stmtId);
    gen_case* gencase = s.MakeGen_case();
    fC->populateCoreMembers(stmtId, stmtId, gencase);
    genstmt = gencase;
    checkForLoops(true);
    if (expr* cond = (expr*)compileExpression(component, fC, tmp, compileDesign,
                                              Reduce::No, gencase)) {
      gencase->VpiCondition(cond);
    }
    checkForLoops(false);
    VectorOfcase_item* items = gencase->Case_items(true);
    tmp = fC->Sibling(tmp);
    while (tmp) {
      if (fC->Type(tmp) == VObjectType::paCase_generate_item) {
        NodeId itemExp = fC->Child(tmp);
        expr* ex = nullptr;
        NodeId stmtId = itemExp;
        if (fC->Type(itemExp) == VObjectType::paConstant_expression) {
          checkForLoops(true);
          ex = (expr*)compileExpression(component, fC, itemExp, compileDesign,
                                        Reduce::No);
          checkForLoops(false);
          stmtId = fC->Sibling(stmtId);
        }

        case_item* citem = s.MakeCase_item();
        citem->VpiParent(gencase);
        fC->populateCoreMembers(tmp, tmp, citem);
        items->push_back(citem);
        if (ex) {
          ex->VpiParent(citem);
          citem->VpiExprs(true)->push_back(ex);
        }

        begin* stmt = s.MakeBegin();
        stmt->VpiParent(citem);
        fC->populateCoreMembers(stmtId, stmtId, stmt);
        citem->Stmt(stmt);

        checkForLoops(true);
        const ScopedScope scopedScope(stmt);
        if (VectorOfany* stmts =
                compileStmt(component, fC, stmtId, compileDesign, Reduce::No,
                            stmt, nullptr, true)) {
          stmt->Stmts(stmts);
        }
        checkForLoops(false);

        NodeId blockNameId = fC->Child(fC->Child(stmtId));
        if (fC->Type(blockNameId) == VObjectType::slStringConst) {
          stmt->VpiName(fC->SymName(blockNameId));
        }
      }
      tmp = fC->Sibling(tmp);
    }
  } else if (fC->Type(stmtId) == VObjectType::paGenvar_initialization ||
             fC->Type(stmtId) ==
                 VObjectType::paGenvar_decl_assignment) {  // For loop stmt
    NodeId varInit = stmtId;
    NodeId endLoopTest = fC->Sibling(varInit);
    gen_for* genfor = s.MakeGen_for();
    fC->populateCoreMembers(id, id, genfor);
    genstmt = genfor;
    const ScopedScope scopedScope1(genfor);

    // Var init
    NodeId Var = fC->Child(varInit);
    NodeId Expression = fC->Sibling(Var);
    assignment* assign_stmt = s.MakeAssignment();
    assign_stmt->VpiParent(genfor);
    fC->populateCoreMembers(varInit, varInit, assign_stmt);
    if (variables* varb =
            (variables*)compileVariable(component, fC, Var, Var, compileDesign,
                                        Reduce::No, genfor, nullptr, false)) {
      assign_stmt->Lhs(varb);
      varb->VpiParent(assign_stmt);
      varb->VpiName(fC->SymName(Var));
      fC->populateCoreMembers(Var, Var, varb);
    }
    checkForLoops(true);
    if (expr* rhs =
            (expr*)compileExpression(component, fC, Expression, compileDesign,
                                     Reduce::No, assign_stmt)) {
      assign_stmt->Rhs(rhs);
    }
    checkForLoops(false);
    genfor->VpiForInitStmts(true)->push_back(assign_stmt);

    // Condition
    checkForLoops(true);
    if (expr* cond = (expr*)compileExpression(
            component, fC, endLoopTest, compileDesign, Reduce::No, genfor)) {
      genfor->VpiCondition(cond);
    }
    checkForLoops(false);

    // Iteration
    NodeId iteration = fC->Sibling(endLoopTest);
    NodeId var = fC->Child(iteration);
    NodeId assignOp = fC->Sibling(var);
    NodeId exprId = fC->Sibling(assignOp);
    if (!exprId) {  // Unary operator like i++
      exprId = iteration;
      checkForLoops(true);
      if (expr* ex = (expr*)compileExpression(
              component, fC, exprId, compileDesign, Reduce::No, genfor)) {
        genfor->VpiForIncStmt(ex);
      }
      checkForLoops(false);
    } else {
      assignment* assign_stmt = s.MakeAssignment();
      assign_stmt->VpiOpType(UhdmWriter::getVpiOpType(fC->Type(assignOp)));
      genfor->VpiForIncStmt(assign_stmt);
      assign_stmt->VpiParent(genfor);
      fC->populateCoreMembers(iteration, iteration, assign_stmt);
      checkForLoops(true);
      if (expr* lhs = (expr*)compileExpression(
              component, fC, var, compileDesign, Reduce::No, assign_stmt)) {
        assign_stmt->Lhs(lhs);
      }
      if (expr* rhs = (expr*)compileExpression(
              component, fC, exprId, compileDesign, Reduce::No, assign_stmt)) {
        assign_stmt->Rhs(rhs);
      }
      checkForLoops(false);
    }

    // Stmts
    NodeId genBlock = fC->Sibling(iteration);

    begin* stmt = s.MakeBegin();
    stmt->VpiParent(genfor);
    fC->populateCoreMembers(genBlock, genBlock, stmt);
    genfor->VpiStmt(stmt);

    checkForLoops(true);
    const ScopedScope scopedScope2(stmt);
    if (VectorOfany* stmts = compileStmt(component, fC, genBlock, compileDesign,
                                         Reduce::No, stmt, nullptr, true)) {
      stmt->Stmts(stmts);
    }
    checkForLoops(false);

    NodeId blockNameId = fC->Child(fC->Child(genBlock));
    if (fC->Type(blockNameId) == VObjectType::slStringConst) {
      stmt->VpiName(fC->SymName(blockNameId));
    }
  }
  VectorOfany* stmts = s.MakeAnyVec();
  if (genstmt) stmts->push_back(genstmt);
  return stmts;
}

}  // namespace SURELOG
