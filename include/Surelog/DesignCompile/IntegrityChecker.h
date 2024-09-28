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

#include <uhdm/UhdmListener.h>
#include <uhdm/uhdm_forward_decl.h>

#include <set>
#include <string_view>
#include <vector>

namespace SURELOG {
class Session;

class IntegrityChecker final : protected UHDM::UhdmListener {
 public:
  explicit IntegrityChecker(Session* session);

  void check(const UHDM::design* const object);
  void check(const std::vector<const UHDM::design*>& objects);

 private:
  bool isBuiltPackageOnStack(const UHDM::any* const object) const;
  bool isUVMMember(const UHDM::any* const object) const;

  enum class LineColumnRelation {
    Before,
    Inside,
    After,
    Inconclusive,
  };

  LineColumnRelation getLineColumnRelation(uint32_t csl, uint16_t csc,
                                           uint32_t cel, uint16_t cec) const;

  LineColumnRelation getLineColumnRelation(uint32_t csl, uint16_t csc,
                                           uint32_t cel, uint16_t cec,
                                           uint32_t psl, uint16_t psc,
                                           uint32_t pel, uint16_t pec) const;

  template <typename T>
  void reportAmbigiousMembership(const std::vector<T*>* const collection,
                                 const T* const object) const;

  template <typename T>
  void reportDuplicates(const UHDM::any* const object,
                        const std::vector<T*>* const collection,
                        std::string_view name) const;

  void reportInvalidLocation(const UHDM::any* const object) const;

  void reportMissingLocation(const UHDM::any* const object) const;

  static bool isImplicitFunctionReturnType(const UHDM::any* const object);

  static std::string_view stripDecorations(std::string_view name);

  static bool areNamedSame(const UHDM::any* const object,
                           const UHDM::any* const actual);

  void reportInvalidNames(const UHDM::any* const object) const;

  void reportInvalidFile(const UHDM::any* const object) const;

  void reportNullActual(const UHDM::any* const object) const;

  void enterAny(const UHDM::any* const object) final;

  void enterAlias_stmts(const UHDM::any* const object,
                        const UHDM::VectorOfalias_stmt& objects) final;
  void enterAllClasses(const UHDM::any* const object,
                       const UHDM::VectorOfclass_defn& objects) final;
  void enterAllInterfaces(const UHDM::any* const object,
                          const UHDM::VectorOfinterface_inst& objects) final;
  void enterAllModules(const UHDM::any* const object,
                       const UHDM::VectorOfmodule_inst& objects) final;
  void enterAllPackages(const UHDM::any* const object,
                        const UHDM::VectorOfpackage& objects) final;
  void enterAllPrograms(const UHDM::any* const object,
                        const UHDM::VectorOfprogram& objects) final;
  void enterAllUdps(const UHDM::any* const object,
                    const UHDM::VectorOfudp_defn& objects) final;
  void enterArguments(const UHDM::any* const object,
                      const UHDM::VectorOfexpr& objects) final;
  void enterArray_nets(const UHDM::any* const object,
                       const UHDM::VectorOfarray_net& objects) final;
  void enterArray_var_mems(const UHDM::any* const object,
                           const UHDM::VectorOfarray_var& objects) final;
  void enterArray_vars(const UHDM::any* const object,
                       const UHDM::VectorOfarray_var& objects) final;
  void enterAssertions(const UHDM::any* const object,
                       const UHDM::VectorOfany& objects) final;
  void enterAttributes(const UHDM::any* const object,
                       const UHDM::VectorOfattribute& objects) final;
  void enterBits(const UHDM::any* const object,
                 const UHDM::VectorOfport_bit& objects) final;
  void enterCase_items(const UHDM::any* const object,
                       const UHDM::VectorOfcase_item& objects) final;
  void enterCase_property_items(
      const UHDM::any* const object,
      const UHDM::VectorOfcase_property_item& objects) final;
  void enterClass_defns(const UHDM::any* const object,
                        const UHDM::VectorOfclass_defn& objects) final;
  void enterClass_typespecs(const UHDM::any* const object,
                            const UHDM::VectorOfclass_typespec& objects) final;
  void enterClocked_seqs(const UHDM::any* const object,
                         const UHDM::VectorOfclocked_seq& objects) final;
  void enterClocking_blocks(const UHDM::any* const object,
                            const UHDM::VectorOfclocking_block& objects) final;
  void enterClocking_io_decls(
      const UHDM::any* const object,
      const UHDM::VectorOfclocking_io_decl& objects) final;
  void enterConcurrent_assertions(
      const UHDM::any* const object,
      const UHDM::VectorOfconcurrent_assertions& objects) final;
  void enterConstraint_exprs(
      const UHDM::any* const object,
      const UHDM::VectorOfconstraint_expr& objects) final;
  void enterConstraint_items(const UHDM::any* const object,
                             const UHDM::VectorOfany& objects) final;
  void enterConstraints(const UHDM::any* const object,
                        const UHDM::VectorOfconstraint& objects) final;
  void enterCont_assign_bits(
      const UHDM::any* const object,
      const UHDM::VectorOfcont_assign_bit& objects) final;
  void enterCont_assigns(const UHDM::any* const object,
                         const UHDM::VectorOfcont_assign& objects) final;
  void enterDef_params(const UHDM::any* const object,
                       const UHDM::VectorOfdef_param& objects) final;
  void enterDeriveds(const UHDM::any* const object,
                     const UHDM::VectorOfclass_defn& objects) final;
  void enterDist_items(const UHDM::any* const object,
                       const UHDM::VectorOfdist_item& objects) final;
  void enterDrivers(const UHDM::any* const object,
                    const UHDM::VectorOfnet_drivers& objects) final;
  void enterElab_tasks(const UHDM::any* const object,
                       const UHDM::VectorOftf_call& objects) final;
  void enterElements(const UHDM::any* const object,
                     const UHDM::VectorOfany& objects) final;
  void enterElse_constraint_exprs(
      const UHDM::any* const object,
      const UHDM::VectorOfconstraint_expr& objects) final;
  void enterEnum_consts(const UHDM::any* const object,
                        const UHDM::VectorOfenum_const& objects) final;
  void enterExpr_indexes(const UHDM::any* const object,
                         const UHDM::VectorOfexpr& objects) final;
  void enterExpr_tchk_terms(const UHDM::any* const object,
                            const UHDM::VectorOfany& objects) final;
  void enterExpressions(const UHDM::any* const object,
                        const UHDM::VectorOfexpr& objects) final;
  void enterExprs(const UHDM::any* const object,
                  const UHDM::VectorOfexpr& objects) final;
  void enterFunctions(const UHDM::any* const object,
                      const UHDM::VectorOffunction& objects) final;
  void enterGen_scope_arrays(
      const UHDM::any* const object,
      const UHDM::VectorOfgen_scope_array& objects) final;
  void enterGen_scopes(const UHDM::any* const object,
                       const UHDM::VectorOfgen_scope& objects) final;
  void enterGen_stmts(const UHDM::any* const object,
                      const UHDM::VectorOfany& objects) final;
  void enterIncludes(const UHDM::any* const object,
                     const UHDM::VectorOfsource_file& objects) final;
  void enterIndexes(const UHDM::any* const object,
                    const UHDM::VectorOfexpr& objects) final;
  void enterInstance_items(const UHDM::any* const object,
                           const UHDM::VectorOfany& objects) final;
  void enterInstances(const UHDM::any* const object,
                      const UHDM::VectorOfinstance& objects) final;
  void enterInterface_arrays(
      const UHDM::any* const object,
      const UHDM::VectorOfinterface_array& objects) final;
  void enterInterface_tf_decls(
      const UHDM::any* const object,
      const UHDM::VectorOfinterface_tf_decl& objects) final;
  void enterInterfaces(const UHDM::any* const object,
                       const UHDM::VectorOfinterface_inst& objects) final;
  void enterIo_decls(const UHDM::any* const object,
                     const UHDM::VectorOfio_decl& objects) final;
  void enterLet_decls(const UHDM::any* const object,
                      const UHDM::VectorOflet_decl& objects) final;
  void enterLoads(const UHDM::any* const object,
                  const UHDM::VectorOfnet_loads& objects) final;
  void enterLocal_drivers(const UHDM::any* const object,
                          const UHDM::VectorOfnet_drivers& objects) final;
  void enterLocal_loads(const UHDM::any* const object,
                        const UHDM::VectorOfnet_loads& objects) final;
  void enterLogic_vars(const UHDM::any* const object,
                       const UHDM::VectorOflogic_var& objects) final;
  void enterMembers(const UHDM::any* const object,
                    const UHDM::VectorOftypespec_member& objects) final;
  void enterMessages(const UHDM::any* const object,
                     const UHDM::VectorOfexpr& objects) final;
  void enterMod_paths(const UHDM::any* const object,
                      const UHDM::VectorOfmod_path& objects) final;
  void enterModports(const UHDM::any* const object,
                     const UHDM::VectorOfmodport& objects) final;
  void enterModule_arrays(const UHDM::any* const object,
                          const UHDM::VectorOfmodule_array& objects) final;
  void enterModules(const UHDM::any* const object,
                    const UHDM::VectorOfmodule_inst& objects) final;
  void enterNamed_event_arrays(
      const UHDM::any* const object,
      const UHDM::VectorOfnamed_event_array& objects) final;
  void enterNamed_event_sequence_expr_groups(
      const UHDM::any* const object, const UHDM::VectorOfany& objects) final;
  void enterNamed_events(const UHDM::any* const object,
                         const UHDM::VectorOfnamed_event& objects) final;
  void enterNet_bits(const UHDM::any* const object,
                     const UHDM::VectorOfnet_bit& objects) final;
  void enterNets(const UHDM::any* const object,
                 const UHDM::VectorOfnet& objects) final;
  void enterNets(const UHDM::any* const object,
                 const UHDM::VectorOfnets& objects) final;
  void enterOperands(const UHDM::any* const object,
                     const UHDM::VectorOfany& objects) final;
  void enterParam_assigns(const UHDM::any* const object,
                          const UHDM::VectorOfparam_assign& objects) final;
  void enterParameters(const UHDM::any* const object,
                       const UHDM::VectorOfany& objects) final;
  void enterPath_elems(const UHDM::any* const object,
                       const UHDM::VectorOfany& objects) final;
  void enterPath_terms(const UHDM::any* const object,
                       const UHDM::VectorOfpath_term& objects) final;
  void enterPorts(const UHDM::any* const object,
                  const UHDM::VectorOfchecker_inst_port& objects) final;
  void enterPorts(const UHDM::any* const object,
                  const UHDM::VectorOfchecker_port& objects) final;
  void enterPorts(const UHDM::any* const object,
                  const UHDM::VectorOfport& objects) final;
  void enterPorts(const UHDM::any* const object,
                  const UHDM::VectorOfports& objects) final;
  void enterPreproc_macro_definitions(
      const UHDM::any* const object,
      const UHDM::VectorOfpreproc_macro_definition& objects) final;
  void enterPreproc_macro_instances(
      const UHDM::any* const object,
      const UHDM::VectorOfpreproc_macro_instance& objects) final;
  void enterPrim_terms(const UHDM::any* const object,
                       const UHDM::VectorOfprim_term& objects) final;
  void enterPrimitive_arrays(
      const UHDM::any* const object,
      const UHDM::VectorOfprimitive_array& objects) final;
  void enterPrimitives(const UHDM::any* const object,
                       const UHDM::VectorOfprimitive& objects) final;
  void enterProcess(const UHDM::any* const object,
                    const UHDM::VectorOfprocess_stmt& objects) final;
  void enterProgram_arrays(const UHDM::any* const object,
                           const UHDM::VectorOfprogram& objects) final;
  void enterProgram_arrays(const UHDM::any* const object,
                           const UHDM::VectorOfprogram_array& objects) final;
  void enterPrograms(const UHDM::any* const object,
                     const UHDM::VectorOfprogram& objects) final;
  void enterProp_formal_decls(
      const UHDM::any* const object,
      const UHDM::VectorOfprop_formal_decl& objects) final;
  void enterProperty_decls(const UHDM::any* const object,
                           const UHDM::VectorOfproperty_decl& objects) final;
  void enterRanges(const UHDM::any* const object,
                   const UHDM::VectorOfrange& objects) final;
  void enterRef_modules(const UHDM::any* const object,
                        const UHDM::VectorOfref_module& objects) final;
  void enterRegs(const UHDM::any* const object,
                 const UHDM::VectorOfreg& objects) final;
  void enterScopes(const UHDM::any* const object,
                   const UHDM::VectorOfscope& objects) final;
  void enterSeq_formal_decls(
      const UHDM::any* const object,
      const UHDM::VectorOfseq_formal_decl& objects) final;
  void enterSequence_decls(const UHDM::any* const object,
                           const UHDM::VectorOfsequence_decl& objects) final;
  void enterSolve_afters(const UHDM::any* const object,
                         const UHDM::VectorOfexpr& objects) final;
  void enterSolve_befores(const UHDM::any* const object,
                          const UHDM::VectorOfexpr& objects) final;
  void enterSource_files(const UHDM::any* const object,
                         const UHDM::VectorOfsource_file& objects) final;
  void enterSpec_params(const UHDM::any* const object,
                        const UHDM::VectorOfspec_param& objects) final;
  void enterStmts(const UHDM::any* const object,
                  const UHDM::VectorOfany& objects) final;
  void enterTable_entrys(const UHDM::any* const object,
                         const UHDM::VectorOftable_entry& objects) final;
  void enterTask_func_decls(const UHDM::any* const object,
                            const UHDM::VectorOftask_func_decl& objects) final;
  void enterTask_funcs(const UHDM::any* const object,
                       const UHDM::VectorOftask_func& objects) final;
  void enterTasks(const UHDM::any* const object,
                  const UHDM::VectorOftask& objects) final;
  void enterTchk_terms(const UHDM::any* const object,
                       const UHDM::VectorOftchk_term& objects) final;
  void enterTchks(const UHDM::any* const object,
                  const UHDM::VectorOftchk& objects) final;
  void enterTf_call_args(const UHDM::any* const object,
                         const UHDM::VectorOfany& objects) final;
  void enterThreads(const UHDM::any* const object,
                    const UHDM::VectorOfthread_obj& objects) final;
  void enterTopModules(const UHDM::any* const object,
                       const UHDM::VectorOfmodule_inst& objects) final;
  void enterTopPackages(const UHDM::any* const object,
                        const UHDM::VectorOfpackage& objects) final;
  void enterTypespecs(const UHDM::any* const object,
                      const UHDM::VectorOftypespec& objects) final;
  void enterVar_bits(const UHDM::any* const object,
                     const UHDM::VectorOfvar_bit& objects) final;
  void enterVar_selects(const UHDM::any* const object,
                        const UHDM::VectorOfvar_select& objects) final;
  void enterVariable_drivers(const UHDM::any* const object,
                             const UHDM::VectorOfany& objects) final;
  void enterVariable_loads(const UHDM::any* const object,
                           const UHDM::VectorOfany& objects) final;
  void enterVariables(const UHDM::any* const object,
                      const UHDM::VectorOfvariables& objects) final;
  void enterVirtual_interface_vars(
      const UHDM::any* const object,
      const UHDM::VectorOfvirtual_interface_var& objects) final;
  void enterVpiArguments(const UHDM::any* const object,
                         const UHDM::VectorOfany& objects) final;
  void enterVpiConditions(const UHDM::any* const object,
                          const UHDM::VectorOfany& objects) final;
  void enterVpiExprs(const UHDM::any* const object,
                     const UHDM::VectorOfany& objects) final;
  void enterVpiForIncStmts(const UHDM::any* const object,
                           const UHDM::VectorOfany& objects) final;
  void enterVpiForInitStmts(const UHDM::any* const object,
                            const UHDM::VectorOfany& objects) final;
  void enterVpiLoopVars(const UHDM::any* const object,
                        const UHDM::VectorOfany& objects) final;
  void enterVpiUses(const UHDM::any* const object,
                    const UHDM::VectorOfany& objects) final;

 private:
  Session* const m_session = nullptr;

  typedef std::set<UHDM::UHDM_OBJECT_TYPE> object_type_set_t;
  const object_type_set_t m_acceptedObjectsWithInvalidLocations;
};

};  // namespace SURELOG

#endif /* SURELOG_INTEGRITYCHECKER_H */
