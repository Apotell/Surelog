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
 * File:   IntegrityChecker.h
 * Author: hs
 *
 * Created on August 10, 2024, 00:00 AM
 */

#ifndef SURELOG_INTEGRITYCHECKER_H
#define SURELOG_INTEGRITYCHECKER_H
#pragma once

#include <uhdm/UhdmVisitor.h>
#include <uhdm/uhdm_forward_decl.h>

#include <set>
#include <string_view>
#include <vector>

#include "Surelog/ErrorReporting/ErrorDefinition.h"

namespace SURELOG {
class Session;

class IntegrityChecker final : protected uhdm::UhdmVisitor {
 public:
  explicit IntegrityChecker(Session* session);

  void check(const uhdm::Design* object);
  void check(const std::vector<const uhdm::Design*>& objects);

 private:
  static bool isUVMMember(const uhdm::Any* object);
  static bool isImplicitFunctionReturnType(const uhdm::RefTypespec* object);
  static std::string_view stripDecorations(std::string_view name);
  static bool areNamedSame(const uhdm::Any* object, const uhdm::Any* actual);
  static bool isValidFile(const uhdm::Any* object);
  static bool isValidName(const uhdm::Any* object);
  static bool isValidLocation(const uhdm::Any* object);

  std::set<const uhdm::PreprocMacroInstance*> getMacroInstances(
      const uhdm::Any* object) const;

  void populateAnyMacroInstanceCache(const uhdm::PreprocMacroInstance* pmi);
  void populateAnyMacroInstanceCache();

  enum class LineColumnRelation {
    Before,
    Inside,
    After,
    Inconclusive,
  };

  std::string_view toString(LineColumnRelation relation) const;

  LineColumnRelation getLineColumnRelation(uint32_t sl, uint16_t sc,
                                           uint32_t el, uint16_t ec) const;
  LineColumnRelation getLineColumnRelation(uint32_t l, uint16_t c, uint32_t sl,
                                           uint16_t sc, uint32_t el,
                                           uint16_t ec) const;
  LineColumnRelation getLineColumnRelation(uint32_t csl, uint16_t csc,
                                           uint32_t cel, uint16_t cec,
                                           uint32_t psl, uint16_t psc,
                                           uint32_t pel, uint16_t pec) const;

  void reportError(ErrorDefinition::ErrorType errorType,
                   const uhdm::Any* object) const;
  template <typename T>
  void reportDuplicates(const uhdm::Any* object,
                        const std::vector<T*>& collection) const;
  void reportInvalidLocation(const uhdm::Any* object) const;
  void reportMissingLocation(const uhdm::Any* object) const;
  void reportMissingName(const uhdm::Any* object) const;
  void reportInvalidName(const uhdm::Any* object) const;
  void reportMissingFile(const uhdm::Any* object) const;
  void reportMissingParent(const uhdm::Any* object) const;
  void reportInvalidParent(const uhdm::Any* object) const;
  void reportNullActual(const uhdm::Any* object) const;
  void reportNullTypespec(const uhdm::Any* object) const;
  void reportUnsupportedTypespec(const uhdm::Any* object) const;
  void reportInvalidForeachVariable(const uhdm::Any* object) const;
  void reportInvalidTypespecLocation(const uhdm::Any* object);

  // clang-format off
  void visitAny2(const uhdm::Any* object);
  void visitAny(const uhdm::Any* object) final;
  void visitAlias(const uhdm::Alias* object) final;
  void visitAlways(const uhdm::Always* object) final;
  void visitAnyPattern(const uhdm::AnyPattern* object) final;
  void visitArrayExpr(const uhdm::ArrayExpr* object) final;
  void visitArrayTypespec(const uhdm::ArrayTypespec* object) final;
  void visitAssert(const uhdm::Assert* object) final;
  void visitAssignStmt(const uhdm::AssignStmt* object) final;
  void visitAssignment(const uhdm::Assignment* object) final;
  void visitAssume(const uhdm::Assume* object) final;
  void visitAttribute(const uhdm::Attribute* object) final;
  void visitBegin(const uhdm::Begin* object) final;
  void visitBitSelect(const uhdm::BitSelect* object) final;
  void visitBitTypespec(const uhdm::BitTypespec* object) final;
  void visitBreakStmt(const uhdm::BreakStmt* object) final;
  void visitByteTypespec(const uhdm::ByteTypespec* object) final;
  void visitCaseItem(const uhdm::CaseItem* object) final;
  void visitCaseProperty(const uhdm::CaseProperty* object) final;
  void visitCasePropertyItem(const uhdm::CasePropertyItem* object) final;
  void visitCaseStmt(const uhdm::CaseStmt* object) final;
  void visitChandleTypespec(const uhdm::ChandleTypespec* object) final;
  void visitCheckerDecl(const uhdm::CheckerDecl* object) final;
  void visitCheckerInst(const uhdm::CheckerInst* object) final;
  void visitCheckerInstPort(const uhdm::CheckerInstPort* object) final;
  void visitCheckerPort(const uhdm::CheckerPort* object) final;
  void visitClassDefn(const uhdm::ClassDefn* object) final;
  void visitClassObj(const uhdm::ClassObj* object) final;
  void visitClassTypespec(const uhdm::ClassTypespec* object) final;
  void visitClockedProperty(const uhdm::ClockedProperty* object) final;
  void visitClockedSeq(const uhdm::ClockedSeq* object) final;
  void visitClockingBlock(const uhdm::ClockingBlock* object) final;
  void visitClockingIODecl(const uhdm::ClockingIODecl* object) final;
  void visitConstant(const uhdm::Constant* object) final;
  void visitConstrForeach(const uhdm::ConstrForeach* object) final;
  void visitConstrIf(const uhdm::ConstrIf* object) final;
  void visitConstrIfElse(const uhdm::ConstrIfElse* object) final;
  void visitConstraint(const uhdm::Constraint* object) final;
  void visitConstraintOrdering(const uhdm::ConstraintOrdering* object) final;
  void visitContAssign(const uhdm::ContAssign* object) final;
  void visitContAssignBit(const uhdm::ContAssignBit* object) final;
  void visitContinueStmt(const uhdm::ContinueStmt* object) final;
  void visitCover(const uhdm::Cover* object) final;
  void visitDeassign(const uhdm::Deassign* object) final;
  void visitDefParam(const uhdm::DefParam* object) final;
  void visitDelayControl(const uhdm::DelayControl* object) final;
  void visitDelayTerm(const uhdm::DelayTerm* object) final;
  void visitDesign(const uhdm::Design* object) final;
  void visitDisable(const uhdm::Disable* object) final;
  void visitDisableFork(const uhdm::DisableFork* object) final;
  void visitDistItem(const uhdm::DistItem* object) final;
  void visitDistribution(const uhdm::Distribution* object) final;
  void visitDoWhile(const uhdm::DoWhile* object) final;
  void visitEnumConst(const uhdm::EnumConst* object) final;
  void visitEnumTypespec(const uhdm::EnumTypespec* object) final;
  void visitEventControl(const uhdm::EventControl* object) final;
  void visitEventStmt(const uhdm::EventStmt* object) final;
  void visitEventTypespec(const uhdm::EventTypespec* object) final;
  void visitExpectStmt(const uhdm::ExpectStmt* object) final;
  void visitExtends(const uhdm::Extends* object) final;
  void visitFinalStmt(const uhdm::FinalStmt* object) final;
  void visitForStmt(const uhdm::ForStmt* object) final;
  void visitForce(const uhdm::Force* object) final;
  void visitForeachStmt(const uhdm::ForeachStmt* object) final;
  void visitForeverStmt(const uhdm::ForeverStmt* object) final;
  void visitForkStmt(const uhdm::ForkStmt* object) final;
  void visitFuncCall(const uhdm::FuncCall* object) final;
  void visitFunction(const uhdm::Function* object) final;
  void visitFunctionDecl(const uhdm::FunctionDecl* object) final;
  void visitGate(const uhdm::Gate* object) final;
  void visitGateArray(const uhdm::GateArray* object) final;
  void visitGenCase(const uhdm::GenCase* object) final;
  void visitGenFor(const uhdm::GenFor* object) final;
  void visitGenIf(const uhdm::GenIf* object) final;
  void visitGenIfElse(const uhdm::GenIfElse* object) final;
  void visitGenRegion(const uhdm::GenRegion* object) final;
  void visitGenScope(const uhdm::GenScope* object) final;
  void visitGenScopeArray(const uhdm::GenScopeArray* object) final;
  void visitHierPath(const uhdm::HierPath* object) final;
  void visitIODecl(const uhdm::IODecl* object) final;
  void visitIdentifier(const uhdm::Identifier* object) final;
  void visitIfElse(const uhdm::IfElse* object) final;
  void visitIfStmt(const uhdm::IfStmt* object) final;
  void visitImmediateAssert(const uhdm::ImmediateAssert* object) final;
  void visitImmediateAssume(const uhdm::ImmediateAssume* object) final;
  void visitImmediateCover(const uhdm::ImmediateCover* object) final;
  void visitImplication(const uhdm::Implication* object) final;
  void visitImportTypespec(const uhdm::ImportTypespec* object) final;
  void visitIndexedPartSelect(const uhdm::IndexedPartSelect* object) final;
  void visitInitial(const uhdm::Initial* object) final;
  void visitIntTypespec(const uhdm::IntTypespec* object) final;
  void visitIntegerTypespec(const uhdm::IntegerTypespec* object) final;
  void visitInterface(const uhdm::Interface* object) final;
  void visitInterfaceArray(const uhdm::InterfaceArray* object) final;
  void visitInterfaceTFDecl(const uhdm::InterfaceTFDecl* object) final;
  void visitInterfaceTypespec(const uhdm::InterfaceTypespec* object) final;
  void visitLetDecl(const uhdm::LetDecl* object) final;
  void visitLetExpr(const uhdm::LetExpr* object) final;
  void visitLogicTypespec(const uhdm::LogicTypespec* object) final;
  void visitLongIntTypespec(const uhdm::LongIntTypespec* object) final;
  void visitMethodFuncCall(const uhdm::MethodFuncCall* object) final;
  void visitMethodTaskCall(const uhdm::MethodTaskCall* object) final;
  void visitModPath(const uhdm::ModPath* object) final;
  void visitModport(const uhdm::Modport* object) final;
  void visitModule(const uhdm::Module* object) final;
  void visitModuleArray(const uhdm::ModuleArray* object) final;
  void visitModuleTypespec(const uhdm::ModuleTypespec* object) final;
  void visitMulticlockSequenceExpr(const uhdm::MulticlockSequenceExpr* object) final;
  void visitNamedEvent(const uhdm::NamedEvent* object) final;
  void visitNamedEventArray(const uhdm::NamedEventArray* object) final;
  void visitNet(const uhdm::Net* object) final;
  void visitNullStmt(const uhdm::NullStmt* object) final;
  void visitOperation(const uhdm::Operation* object) final;
  void visitOrderedWait(const uhdm::OrderedWait* object) final;
  void visitPackage(const uhdm::Package* object) final;
  void visitParamAssign(const uhdm::ParamAssign* object) final;
  void visitParameter(const uhdm::Parameter* object) final;
  void visitPartSelect(const uhdm::PartSelect* object) final;
  void visitPathTerm(const uhdm::PathTerm* object) final;
  void visitPort(const uhdm::Port* object) final;
  void visitPortBit(const uhdm::PortBit* object) final;
  void visitPreprocMacroDefinition(const uhdm::PreprocMacroDefinition* object) final;
  void visitPreprocMacroInstance(const uhdm::PreprocMacroInstance* object) final;
  void visitPrimTerm(const uhdm::PrimTerm* object) final;
  void visitProgram(const uhdm::Program* object) final;
  void visitProgramArray(const uhdm::ProgramArray* object) final;
  void visitProgramTypespec(const uhdm::ProgramTypespec* object) final;
  void visitPropFormalDecl(const uhdm::PropFormalDecl* object) final;
  void visitPropertyDecl(const uhdm::PropertyDecl* object) final;
  void visitPropertyInst(const uhdm::PropertyInst* object) final;
  void visitPropertySpec(const uhdm::PropertySpec* object) final;
  void visitPropertyTypespec(const uhdm::PropertyTypespec* object) final;
  void visitRange(const uhdm::Range* object) final;
  void visitRealTypespec(const uhdm::RealTypespec* object) final;
  void visitRefModule(const uhdm::RefModule* object) final;
  void visitRefObj(const uhdm::RefObj* object) final;
  void visitRefTypespec(const uhdm::RefTypespec* object) final;
  void visitReg(const uhdm::Reg* object) final;
  void visitRegArray(const uhdm::RegArray* object) final;
  void visitRelease(const uhdm::Release* object) final;
  void visitRepeat(const uhdm::Repeat* object) final;
  void visitRepeatControl(const uhdm::RepeatControl* object) final;
  void visitRestrict(const uhdm::Restrict* object) final;
  void visitReturnStmt(const uhdm::ReturnStmt* object) final;
  void visitSeqFormalDecl(const uhdm::SeqFormalDecl* object) final;
  void visitSequenceDecl(const uhdm::SequenceDecl* object) final;
  void visitSequenceInst(const uhdm::SequenceInst* object) final;
  void visitSequenceTypespec(const uhdm::SequenceTypespec* object) final;
  void visitShortIntTypespec(const uhdm::ShortIntTypespec* object) final;
  void visitShortRealTypespec(const uhdm::ShortRealTypespec* object) final;
  void visitSoftDisable(const uhdm::SoftDisable* object) final;
  void visitSourceFile(const uhdm::SourceFile* object) final;
  void visitSpecParam(const uhdm::SpecParam* object) final;
  void visitStringTypespec(const uhdm::StringTypespec* object) final;
  void visitStructPattern(const uhdm::StructPattern* object) final;
  void visitStructTypespec(const uhdm::StructTypespec* object) final;
  void visitSwitchArray(const uhdm::SwitchArray* object) final;
  void visitSwitchTran(const uhdm::SwitchTran* object) final;
  void visitSysFuncCall(const uhdm::SysFuncCall* object) final;
  void visitSysTaskCall(const uhdm::SysTaskCall* object) final;
  void visitTableEntry(const uhdm::TableEntry* object) final;
  void visitTaggedPattern(const uhdm::TaggedPattern* object) final;
  void visitTask(const uhdm::Task* object) final;
  void visitTaskCall(const uhdm::TaskCall* object) final;
  void visitTaskDecl(const uhdm::TaskDecl* object) final;
  void visitTchk(const uhdm::Tchk* object) final;
  void visitTchkTerm(const uhdm::TchkTerm* object) final;
  void visitThread(const uhdm::Thread* object) final;
  void visitTimeTypespec(const uhdm::TimeTypespec* object) final;
  void visitTypeParameter(const uhdm::TypeParameter* object) final;
  void visitTypedefTypespec(const uhdm::TypedefTypespec* object) final;
  void visitTypespecMember(const uhdm::TypespecMember* object) final;
  void visitUdp(const uhdm::Udp* object) final;
  void visitUdpArray(const uhdm::UdpArray* object) final;
  void visitUdpDefn(const uhdm::UdpDefn* object) final;
  void visitUdpDefnTypespec(const uhdm::UdpDefnTypespec* object) final;
  void visitUnionTypespec(const uhdm::UnionTypespec* object) final;
  void visitUnsupportedExpr(const uhdm::UnsupportedExpr* object) final;
  void visitUnsupportedStmt(const uhdm::UnsupportedStmt* object) final;
  void visitUnsupportedTypespec(const uhdm::UnsupportedTypespec* object) final;
  void visitUserSystf(const uhdm::UserSystf* object) final;
  void visitVarSelect(const uhdm::VarSelect* object) final;
  void visitVariable(const uhdm::Variable* object) final;
  void visitVoidTypespec(const uhdm::VoidTypespec* object) final;
  void visitWaitFork(const uhdm::WaitFork* object) final;
  void visitWaitStmt(const uhdm::WaitStmt* object) final;
  void visitWhileStmt(const uhdm::WhileStmt* object) final;

  void visitAliasCollection(const uhdm::Any* object, const uhdm::AliasCollection& objects) final;
  void visitAlwaysCollection(const uhdm::Any* object, const uhdm::AlwaysCollection& objects) final;
  void visitAnyCollection(const uhdm::Any* object, const uhdm::AnyCollection& objects) final;
  void visitAnyPatternCollection(const uhdm::Any* object, const uhdm::AnyPatternCollection& objects) final;
  void visitArrayExprCollection(const uhdm::Any* object, const uhdm::ArrayExprCollection& objects) final;
  void visitArrayTypespecCollection(const uhdm::Any* object, const uhdm::ArrayTypespecCollection& objects) final;
  void visitAssertCollection(const uhdm::Any* object, const uhdm::AssertCollection& objects) final;
  void visitAssignStmtCollection(const uhdm::Any* object, const uhdm::AssignStmtCollection& objects) final;
  void visitAssignmentCollection(const uhdm::Any* object, const uhdm::AssignmentCollection& objects) final;
  void visitAssumeCollection(const uhdm::Any* object, const uhdm::AssumeCollection& objects) final;
  void visitAtomicStmtCollection(const uhdm::Any* object, const uhdm::AtomicStmtCollection& objects) final;
  void visitAttributeCollection(const uhdm::Any* object, const uhdm::AttributeCollection& objects) final;
  void visitBeginCollection(const uhdm::Any* object, const uhdm::BeginCollection& objects) final;
  void visitBitSelectCollection(const uhdm::Any* object, const uhdm::BitSelectCollection& objects) final;
  void visitBitTypespecCollection(const uhdm::Any* object, const uhdm::BitTypespecCollection& objects) final;
  void visitBreakStmtCollection(const uhdm::Any* object, const uhdm::BreakStmtCollection& objects) final;
  void visitByteTypespecCollection(const uhdm::Any* object, const uhdm::ByteTypespecCollection& objects) final;
  void visitCaseItemCollection(const uhdm::Any* object, const uhdm::CaseItemCollection& objects) final;
  void visitCasePropertyCollection(const uhdm::Any* object, const uhdm::CasePropertyCollection& objects) final;
  void visitCasePropertyItemCollection(const uhdm::Any* object, const uhdm::CasePropertyItemCollection& objects) final;
  void visitCaseStmtCollection(const uhdm::Any* object, const uhdm::CaseStmtCollection& objects) final;
  void visitChandleTypespecCollection(const uhdm::Any* object, const uhdm::ChandleTypespecCollection& objects) final;
  void visitCheckerDeclCollection(const uhdm::Any* object, const uhdm::CheckerDeclCollection& objects) final;
  void visitCheckerInstCollection(const uhdm::Any* object, const uhdm::CheckerInstCollection& objects) final;
  void visitCheckerInstPortCollection(const uhdm::Any* object, const uhdm::CheckerInstPortCollection& objects) final;
  void visitCheckerPortCollection(const uhdm::Any* object, const uhdm::CheckerPortCollection& objects) final;
  void visitClassDefnCollection(const uhdm::Any* object, const uhdm::ClassDefnCollection& objects) final;
  void visitClassObjCollection(const uhdm::Any* object, const uhdm::ClassObjCollection& objects) final;
  void visitClassTypespecCollection(const uhdm::Any* object, const uhdm::ClassTypespecCollection& objects) final;
  void visitClockedPropertyCollection(const uhdm::Any* object, const uhdm::ClockedPropertyCollection& objects) final;
  void visitClockedSeqCollection(const uhdm::Any* object, const uhdm::ClockedSeqCollection& objects) final;
  void visitClockingBlockCollection(const uhdm::Any* object, const uhdm::ClockingBlockCollection& objects) final;
  void visitClockingIODeclCollection(const uhdm::Any* object, const uhdm::ClockingIODeclCollection& objects) final;
  void visitConcurrentAssertionsCollection(const uhdm::Any* object, const uhdm::ConcurrentAssertionsCollection& objects) final;
  void visitConstantCollection(const uhdm::Any* object, const uhdm::ConstantCollection& objects) final;
  void visitConstrForeachCollection(const uhdm::Any* object, const uhdm::ConstrForeachCollection& objects) final;
  void visitConstrIfCollection(const uhdm::Any* object, const uhdm::ConstrIfCollection& objects) final;
  void visitConstrIfElseCollection(const uhdm::Any* object, const uhdm::ConstrIfElseCollection& objects) final;
  void visitConstraintCollection(const uhdm::Any* object, const uhdm::ConstraintCollection& objects) final;
  void visitConstraintExprCollection(const uhdm::Any* object, const uhdm::ConstraintExprCollection& objects) final;
  void visitConstraintOrderingCollection(const uhdm::Any* object, const uhdm::ConstraintOrderingCollection& objects) final;
  void visitContAssignCollection(const uhdm::Any* object, const uhdm::ContAssignCollection& objects) final;
  void visitContAssignBitCollection(const uhdm::Any* object, const uhdm::ContAssignBitCollection& objects) final;
  void visitContinueStmtCollection(const uhdm::Any* object, const uhdm::ContinueStmtCollection& objects) final;
  void visitCoverCollection(const uhdm::Any* object, const uhdm::CoverCollection& objects) final;
  void visitDeassignCollection(const uhdm::Any* object, const uhdm::DeassignCollection& objects) final;
  void visitDefParamCollection(const uhdm::Any* object, const uhdm::DefParamCollection& objects) final;
  void visitDelayControlCollection(const uhdm::Any* object, const uhdm::DelayControlCollection& objects) final;
  void visitDelayTermCollection(const uhdm::Any* object, const uhdm::DelayTermCollection& objects) final;
  void visitDesignCollection(const uhdm::Any* object, const uhdm::DesignCollection& objects) final;
  void visitDisableCollection(const uhdm::Any* object, const uhdm::DisableCollection& objects) final;
  void visitDisableForkCollection(const uhdm::Any* object, const uhdm::DisableForkCollection& objects) final;
  void visitDisablesCollection(const uhdm::Any* object, const uhdm::DisablesCollection& objects) final;
  void visitDistItemCollection(const uhdm::Any* object, const uhdm::DistItemCollection& objects) final;
  void visitDistributionCollection(const uhdm::Any* object, const uhdm::DistributionCollection& objects) final;
  void visitDoWhileCollection(const uhdm::Any* object, const uhdm::DoWhileCollection& objects) final;
  void visitEnumConstCollection(const uhdm::Any* object, const uhdm::EnumConstCollection& objects) final;
  void visitEnumTypespecCollection(const uhdm::Any* object, const uhdm::EnumTypespecCollection& objects) final;
  void visitEventControlCollection(const uhdm::Any* object, const uhdm::EventControlCollection& objects) final;
  void visitEventStmtCollection(const uhdm::Any* object, const uhdm::EventStmtCollection& objects) final;
  void visitEventTypespecCollection(const uhdm::Any* object, const uhdm::EventTypespecCollection& objects) final;
  void visitExpectStmtCollection(const uhdm::Any* object, const uhdm::ExpectStmtCollection& objects) final;
  void visitExprCollection(const uhdm::Any* object, const uhdm::ExprCollection& objects) final;
  void visitExtendsCollection(const uhdm::Any* object, const uhdm::ExtendsCollection& objects) final;
  void visitFinalStmtCollection(const uhdm::Any* object, const uhdm::FinalStmtCollection& objects) final;
  void visitForStmtCollection(const uhdm::Any* object, const uhdm::ForStmtCollection& objects) final;
  void visitForceCollection(const uhdm::Any* object, const uhdm::ForceCollection& objects) final;
  void visitForeachStmtCollection(const uhdm::Any* object, const uhdm::ForeachStmtCollection& objects) final;
  void visitForeverStmtCollection(const uhdm::Any* object, const uhdm::ForeverStmtCollection& objects) final;
  void visitForkStmtCollection(const uhdm::Any* object, const uhdm::ForkStmtCollection& objects) final;
  void visitFuncCallCollection(const uhdm::Any* object, const uhdm::FuncCallCollection& objects) final;
  void visitFunctionCollection(const uhdm::Any* object, const uhdm::FunctionCollection& objects) final;
  void visitFunctionDeclCollection(const uhdm::Any* object, const uhdm::FunctionDeclCollection& objects) final;
  void visitGateCollection(const uhdm::Any* object, const uhdm::GateCollection& objects) final;
  void visitGateArrayCollection(const uhdm::Any* object, const uhdm::GateArrayCollection& objects) final;
  void visitGenCaseCollection(const uhdm::Any* object, const uhdm::GenCaseCollection& objects) final;
  void visitGenForCollection(const uhdm::Any* object, const uhdm::GenForCollection& objects) final;
  void visitGenIfCollection(const uhdm::Any* object, const uhdm::GenIfCollection& objects) final;
  void visitGenIfElseCollection(const uhdm::Any* object, const uhdm::GenIfElseCollection& objects) final;
  void visitGenRegionCollection(const uhdm::Any* object, const uhdm::GenRegionCollection& objects) final;
  void visitGenScopeCollection(const uhdm::Any* object, const uhdm::GenScopeCollection& objects) final;
  void visitGenScopeArrayCollection(const uhdm::Any* object, const uhdm::GenScopeArrayCollection& objects) final;
  void visitGenStmtCollection(const uhdm::Any* object, const uhdm::GenStmtCollection& objects) final;
  void visitHierPathCollection(const uhdm::Any* object, const uhdm::HierPathCollection& objects) final;
  void visitIODeclCollection(const uhdm::Any* object, const uhdm::IODeclCollection& objects) final;
  void visitIdentifierCollection(const uhdm::Any* object, const uhdm::IdentifierCollection& objects) final;
  void visitIfElseCollection(const uhdm::Any* object, const uhdm::IfElseCollection& objects) final;
  void visitIfStmtCollection(const uhdm::Any* object, const uhdm::IfStmtCollection& objects) final;
  void visitImmediateAssertCollection(const uhdm::Any* object, const uhdm::ImmediateAssertCollection& objects) final;
  void visitImmediateAssumeCollection(const uhdm::Any* object, const uhdm::ImmediateAssumeCollection& objects) final;
  void visitImmediateCoverCollection(const uhdm::Any* object, const uhdm::ImmediateCoverCollection& objects) final;
  void visitImplicationCollection(const uhdm::Any* object, const uhdm::ImplicationCollection& objects) final;
  void visitImportTypespecCollection(const uhdm::Any* object, const uhdm::ImportTypespecCollection& objects) final;
  void visitIndexedPartSelectCollection(const uhdm::Any* object, const uhdm::IndexedPartSelectCollection& objects) final;
  void visitInitialCollection(const uhdm::Any* object, const uhdm::InitialCollection& objects) final;
  void visitInstanceCollection(const uhdm::Any* object, const uhdm::InstanceCollection& objects) final;
  void visitInstanceArrayCollection(const uhdm::Any* object, const uhdm::InstanceArrayCollection& objects) final;
  void visitIntTypespecCollection(const uhdm::Any* object, const uhdm::IntTypespecCollection& objects) final;
  void visitIntegerTypespecCollection(const uhdm::Any* object, const uhdm::IntegerTypespecCollection& objects) final;
  void visitInterfaceCollection(const uhdm::Any* object, const uhdm::InterfaceCollection& objects) final;
  void visitInterfaceArrayCollection(const uhdm::Any* object, const uhdm::InterfaceArrayCollection& objects) final;
  void visitInterfaceTFDeclCollection(const uhdm::Any* object, const uhdm::InterfaceTFDeclCollection& objects) final;
  void visitInterfaceTypespecCollection(const uhdm::Any* object, const uhdm::InterfaceTypespecCollection& objects) final;
  void visitLetDeclCollection(const uhdm::Any* object, const uhdm::LetDeclCollection& objects) final;
  void visitLetExprCollection(const uhdm::Any* object, const uhdm::LetExprCollection& objects) final;
  void visitLogicTypespecCollection(const uhdm::Any* object, const uhdm::LogicTypespecCollection& objects) final;
  void visitLongIntTypespecCollection(const uhdm::Any* object, const uhdm::LongIntTypespecCollection& objects) final;
  void visitMethodFuncCallCollection(const uhdm::Any* object, const uhdm::MethodFuncCallCollection& objects) final;
  void visitMethodTaskCallCollection(const uhdm::Any* object, const uhdm::MethodTaskCallCollection& objects) final;
  void visitModPathCollection(const uhdm::Any* object, const uhdm::ModPathCollection& objects) final;
  void visitModportCollection(const uhdm::Any* object, const uhdm::ModportCollection& objects) final;
  void visitModuleCollection(const uhdm::Any* object, const uhdm::ModuleCollection& objects) final;
  void visitModuleArrayCollection(const uhdm::Any* object, const uhdm::ModuleArrayCollection& objects) final;
  void visitModuleTypespecCollection(const uhdm::Any* object, const uhdm::ModuleTypespecCollection& objects) final;
  void visitMulticlockSequenceExprCollection(const uhdm::Any* object, const uhdm::MulticlockSequenceExprCollection& objects) final;
  void visitNamedEventCollection(const uhdm::Any* object, const uhdm::NamedEventCollection& objects) final;
  void visitNamedEventArrayCollection(const uhdm::Any* object, const uhdm::NamedEventArrayCollection& objects) final;
  void visitNetCollection(const uhdm::Any* object, const uhdm::NetCollection& objects) final;
  void visitNetDriversCollection(const uhdm::Any* object, const uhdm::NetDriversCollection& objects) final;
  void visitNetLoadsCollection(const uhdm::Any* object, const uhdm::NetLoadsCollection& objects) final;
  void visitNetsCollection(const uhdm::Any* object, const uhdm::NetsCollection& objects) final;
  void visitNullStmtCollection(const uhdm::Any* object, const uhdm::NullStmtCollection& objects) final;
  void visitOperationCollection(const uhdm::Any* object, const uhdm::OperationCollection& objects) final;
  void visitOrderedWaitCollection(const uhdm::Any* object, const uhdm::OrderedWaitCollection& objects) final;
  void visitPackageCollection(const uhdm::Any* object, const uhdm::PackageCollection& objects) final;
  void visitParamAssignCollection(const uhdm::Any* object, const uhdm::ParamAssignCollection& objects) final;
  void visitParameterCollection(const uhdm::Any* object, const uhdm::ParameterCollection& objects) final;
  void visitPartSelectCollection(const uhdm::Any* object, const uhdm::PartSelectCollection& objects) final;
  void visitPathTermCollection(const uhdm::Any* object, const uhdm::PathTermCollection& objects) final;
  void visitPortCollection(const uhdm::Any* object, const uhdm::PortCollection& objects) final;
  void visitPortBitCollection(const uhdm::Any* object, const uhdm::PortBitCollection& objects) final;
  void visitPortsCollection(const uhdm::Any* object, const uhdm::PortsCollection& objects) final;
  void visitPreprocMacroDefinitionCollection(const uhdm::Any* object, const uhdm::PreprocMacroDefinitionCollection& objects) final;
  void visitPreprocMacroInstanceCollection(const uhdm::Any* object, const uhdm::PreprocMacroInstanceCollection& objects) final;
  void visitPrimTermCollection(const uhdm::Any* object, const uhdm::PrimTermCollection& objects) final;
  void visitPrimitiveCollection(const uhdm::Any* object, const uhdm::PrimitiveCollection& objects) final;
  void visitPrimitiveArrayCollection(const uhdm::Any* object, const uhdm::PrimitiveArrayCollection& objects) final;
  void visitProcessCollection(const uhdm::Any* object, const uhdm::ProcessCollection& objects) final;
  void visitProgramCollection(const uhdm::Any* object, const uhdm::ProgramCollection& objects) final;
  void visitProgramArrayCollection(const uhdm::Any* object, const uhdm::ProgramArrayCollection& objects) final;
  void visitProgramTypespecCollection(const uhdm::Any* object, const uhdm::ProgramTypespecCollection& objects) final;
  void visitPropFormalDeclCollection(const uhdm::Any* object, const uhdm::PropFormalDeclCollection& objects) final;
  void visitPropertyDeclCollection(const uhdm::Any* object, const uhdm::PropertyDeclCollection& objects) final;
  void visitPropertyInstCollection(const uhdm::Any* object, const uhdm::PropertyInstCollection& objects) final;
  void visitPropertySpecCollection(const uhdm::Any* object, const uhdm::PropertySpecCollection& objects) final;
  void visitPropertyTypespecCollection(const uhdm::Any* object, const uhdm::PropertyTypespecCollection& objects) final;
  void visitRangeCollection(const uhdm::Any* object, const uhdm::RangeCollection& objects) final;
  void visitRealTypespecCollection(const uhdm::Any* object, const uhdm::RealTypespecCollection& objects) final;
  void visitRefModuleCollection(const uhdm::Any* object, const uhdm::RefModuleCollection& objects) final;
  void visitRefObjCollection(const uhdm::Any* object, const uhdm::RefObjCollection& objects) final;
  void visitRefTypespecCollection(const uhdm::Any* object, const uhdm::RefTypespecCollection& objects) final;
  void visitRegCollection(const uhdm::Any* object, const uhdm::RegCollection& objects) final;
  void visitRegArrayCollection(const uhdm::Any* object, const uhdm::RegArrayCollection& objects) final;
  void visitReleaseCollection(const uhdm::Any* object, const uhdm::ReleaseCollection& objects) final;
  void visitRepeatCollection(const uhdm::Any* object, const uhdm::RepeatCollection& objects) final;
  void visitRepeatControlCollection(const uhdm::Any* object, const uhdm::RepeatControlCollection& objects) final;
  void visitRestrictCollection(const uhdm::Any* object, const uhdm::RestrictCollection& objects) final;
  void visitReturnStmtCollection(const uhdm::Any* object, const uhdm::ReturnStmtCollection& objects) final;
  void visitScopeCollection(const uhdm::Any* object, const uhdm::ScopeCollection& objects) final;
  void visitSeqFormalDeclCollection(const uhdm::Any* object, const uhdm::SeqFormalDeclCollection& objects) final;
  void visitSequenceDeclCollection(const uhdm::Any* object, const uhdm::SequenceDeclCollection& objects) final;
  void visitSequenceInstCollection(const uhdm::Any* object, const uhdm::SequenceInstCollection& objects) final;
  void visitSequenceTypespecCollection(const uhdm::Any* object, const uhdm::SequenceTypespecCollection& objects) final;
  void visitShortIntTypespecCollection(const uhdm::Any* object, const uhdm::ShortIntTypespecCollection& objects) final;
  void visitShortRealTypespecCollection(const uhdm::Any* object, const uhdm::ShortRealTypespecCollection& objects) final;
  void visitSimpleExprCollection(const uhdm::Any* object, const uhdm::SimpleExprCollection& objects) final;
  void visitSoftDisableCollection(const uhdm::Any* object, const uhdm::SoftDisableCollection& objects) final;
  void visitSourceFileCollection(const uhdm::Any* object, const uhdm::SourceFileCollection& objects) final;
  void visitSpecParamCollection(const uhdm::Any* object, const uhdm::SpecParamCollection& objects) final;
  void visitStringTypespecCollection(const uhdm::Any* object, const uhdm::StringTypespecCollection& objects) final;
  void visitStructPatternCollection(const uhdm::Any* object, const uhdm::StructPatternCollection& objects) final;
  void visitStructTypespecCollection(const uhdm::Any* object, const uhdm::StructTypespecCollection& objects) final;
  void visitSwitchArrayCollection(const uhdm::Any* object, const uhdm::SwitchArrayCollection& objects) final;
  void visitSwitchTranCollection(const uhdm::Any* object, const uhdm::SwitchTranCollection& objects) final;
  void visitSysFuncCallCollection(const uhdm::Any* object, const uhdm::SysFuncCallCollection& objects) final;
  void visitSysTaskCallCollection(const uhdm::Any* object, const uhdm::SysTaskCallCollection& objects) final;
  void visitTFCallCollection(const uhdm::Any* object, const uhdm::TFCallCollection& objects) final;
  void visitTableEntryCollection(const uhdm::Any* object, const uhdm::TableEntryCollection& objects) final;
  void visitTaggedPatternCollection(const uhdm::Any* object, const uhdm::TaggedPatternCollection& objects) final;
  void visitTaskCollection(const uhdm::Any* object, const uhdm::TaskCollection& objects) final;
  void visitTaskCallCollection(const uhdm::Any* object, const uhdm::TaskCallCollection& objects) final;
  void visitTaskDeclCollection(const uhdm::Any* object, const uhdm::TaskDeclCollection& objects) final;
  void visitTaskFuncCollection(const uhdm::Any* object, const uhdm::TaskFuncCollection& objects) final;
  void visitTaskFuncDeclCollection(const uhdm::Any* object, const uhdm::TaskFuncDeclCollection& objects) final;
  void visitTchkCollection(const uhdm::Any* object, const uhdm::TchkCollection& objects) final;
  void visitTchkTermCollection(const uhdm::Any* object, const uhdm::TchkTermCollection& objects) final;
  void visitThreadCollection(const uhdm::Any* object, const uhdm::ThreadCollection& objects) final;
  void visitTimeTypespecCollection(const uhdm::Any* object, const uhdm::TimeTypespecCollection& objects) final;
  void visitTypeParameterCollection(const uhdm::Any* object, const uhdm::TypeParameterCollection& objects) final;
  void visitTypedefTypespecCollection(const uhdm::Any* object, const uhdm::TypedefTypespecCollection& objects) final;
  void visitTypespecCollection(const uhdm::Any* object, const uhdm::TypespecCollection& objects) final;
  void visitTypespecMemberCollection(const uhdm::Any* object, const uhdm::TypespecMemberCollection& objects) final;
  void visitUdpCollection(const uhdm::Any* object, const uhdm::UdpCollection& objects) final;
  void visitUdpArrayCollection(const uhdm::Any* object, const uhdm::UdpArrayCollection& objects) final;
  void visitUdpDefnCollection(const uhdm::Any* object, const uhdm::UdpDefnCollection& objects) final;
  void visitUdpDefnTypespecCollection(const uhdm::Any* object, const uhdm::UdpDefnTypespecCollection& objects) final;
  void visitUnionTypespecCollection(const uhdm::Any* object, const uhdm::UnionTypespecCollection& objects) final;
  void visitUnsupportedExprCollection(const uhdm::Any* object, const uhdm::UnsupportedExprCollection& objects) final;
  void visitUnsupportedStmtCollection(const uhdm::Any* object, const uhdm::UnsupportedStmtCollection& objects) final;
  void visitUnsupportedTypespecCollection(const uhdm::Any* object, const uhdm::UnsupportedTypespecCollection& objects) final;
  void visitUserSystfCollection(const uhdm::Any* object, const uhdm::UserSystfCollection& objects) final;
  void visitVarSelectCollection(const uhdm::Any* object, const uhdm::VarSelectCollection& objects) final;
  void visitVariableCollection(const uhdm::Any* object, const uhdm::VariableCollection& objects) final;
  void visitVoidTypespecCollection(const uhdm::Any* object, const uhdm::VoidTypespecCollection& objects) final;
  void visitWaitForkCollection(const uhdm::Any* object, const uhdm::WaitForkCollection& objects) final;
  void visitWaitStmtCollection(const uhdm::Any* object, const uhdm::WaitStmtCollection& objects) final;
  void visitWaitsCollection(const uhdm::Any* object, const uhdm::WaitsCollection& objects) final;
  void visitWhileStmtCollection(const uhdm::Any* object, const uhdm::WhileStmtCollection& objects) final;
  // clang-format on

 private:
  Session* const m_session = nullptr;
  const uhdm::Design* m_design = nullptr;

  using uhdm_type_set_t = std::set<uhdm::UhdmType>;
  const uhdm_type_set_t m_typesWithValidName;
  const uhdm_type_set_t m_typesWithMissingFile;
  const uhdm_type_set_t m_typesWithMissingParent;
  const uhdm_type_set_t m_typesWithMissingLocation;

  using any_macro_instance_map_t =
      std::multimap<const uhdm::Any*, const uhdm::PreprocMacroInstance*>;
  any_macro_instance_map_t m_anyMacroInstance;

  bool m_reportInvalidName = true;
  bool m_reportMissingName = true;
  bool m_reportMissingFile = true;
  bool m_reportMissingParent = true;
  bool m_reportMissingLocation = true;
  bool m_reportNullActual = true;
  bool m_reportNullTypespec = true;
  bool m_reportUnsupportedTypespec = true;
  bool m_reportDuplicates = true;
  bool m_reportInvalidForeachVariable = true;
};

class FullNameChecker final : public uhdm::UhdmVisitor {
 public:
  explicit FullNameChecker(Session* session) : m_session(session) {}

  void visitAny(const uhdm::Any* object) final;
  
 private:
  bool hasAtMostOneDoubleColon(std::string_view& s);
  bool isNameValid(std::string_view& fname, std::string_view& sName);
  std::string_view getFullName(const uhdm::Any* object);
  Session* m_session = nullptr;
};

};  // namespace SURELOG

#endif /* SURELOG_INTEGRITYCHECKER_H */
