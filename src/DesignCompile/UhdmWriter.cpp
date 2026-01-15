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
 * File:   UhdmWriter.cpp
 * Author: alain
 *
 * Created on January 17, 2020, 9:13 PM
 */

#include "Surelog/DesignCompile/UhdmWriter.h"

#include <uhdm/BaseClass.h>
#include <uhdm/expr.h>
#include <uhdm/import_typespec.h>
#include <uhdm/uhdm_types.h>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Surelog/CommandLine/CommandLineParser.h"
#include "Surelog/Common/Containers.h"
#include "Surelog/Common/FileSystem.h"
#include "Surelog/Common/NodeId.h"
#include "Surelog/Common/Session.h"
#include "Surelog/Design/ClockingBlock.h"
#include "Surelog/Design/DataType.h"
#include "Surelog/Design/FileContent.h"
#include "Surelog/Design/Modport.h"
#include "Surelog/Design/ModuleDefinition.h"
#include "Surelog/Design/ModuleInstance.h"
#include "Surelog/Design/Signal.h"
#include "Surelog/DesignCompile/CompileDesign.h"
#include "Surelog/DesignCompile/CompileHelper.h"
#include "Surelog/DesignCompile/IntegrityChecker.h"
#include "Surelog/DesignCompile/ObjectBinder.h"
#include "Surelog/ErrorReporting/Error.h"
#include "Surelog/ErrorReporting/ErrorDefinition.h"
#include "Surelog/ErrorReporting/Location.h"
#include "Surelog/Expression/Value.h"
#include "Surelog/Package/Package.h"
#include "Surelog/SourceCompile/Compiler.h"
#include "Surelog/SourceCompile/SymbolTable.h"
#include "Surelog/SourceCompile/VObjectTypes.h"
#include "Surelog/Testbench/ClassDefinition.h"
#include "Surelog/Testbench/Program.h"
#include "Surelog/Testbench/Variable.h"
#include "Surelog/Utils/StringUtils.h"

// UHDM
#include <uhdm/ExprEval.h>
#include <uhdm/Serializer.h>
#include <uhdm/UhdmVisitor.h>
#include <uhdm/sv_vpi_user.h>
#include <uhdm/uhdm.h>
#include <uhdm/vpi_uhdm.h>
#include <uhdm/vpi_user.h>
#include <uhdm/vpi_visitor.h>

namespace SURELOG {
namespace fs = std::filesystem;
using namespace uhdm;  // NOLINT (we're using a whole bunch of these)

static uhdm::Typespec* replace(const uhdm::Typespec* orig,
                               std::map<const uhdm::Typespec*, const uhdm::Typespec*>& typespecSwapMap) {
  if (orig && (orig->getUhdmType() == uhdm::UhdmType::UnsupportedTypespec)) {
    std::map<const uhdm::Typespec*, const uhdm::Typespec*>::const_iterator itr = typespecSwapMap.find(orig);
    if (itr != typespecSwapMap.end()) {
      const uhdm::Typespec* tps = (*itr).second;
      return (uhdm::Typespec*)tps;
    }
  }
  return (uhdm::Typespec*)orig;
}

std::string UhdmWriter::builtinGateName(VObjectType type) {
  std::string modName;
  switch (type) {
    case VObjectType::paNInpGate_And: modName = "work@and"; break;
    case VObjectType::paNInpGate_Or: modName = "work@or"; break;
    case VObjectType::paNInpGate_Nand: modName = "work@nand"; break;
    case VObjectType::paNInpGate_Nor: modName = "work@nor"; break;
    case VObjectType::paNInpGate_Xor: modName = "work@xor"; break;
    case VObjectType::paNInpGate_Xnor: modName = "work@xnor"; break;
    case VObjectType::paNOutGate_Buf: modName = "work@buf"; break;
    case VObjectType::paNOutGate_Not: modName = "work@not"; break;
    case VObjectType::paPassEnSwitch_Tranif0: modName = "work@tranif0"; break;
    case VObjectType::paPassEnSwitch_Tranif1: modName = "work@tranif1"; break;
    case VObjectType::paPassEnSwitch_RTranif1: modName = "work@rtranif1"; break;
    case VObjectType::paPassEnSwitch_RTranif0: modName = "work@rtranif0"; break;
    case VObjectType::paPassSwitch_Tran: modName = "work@tran"; break;
    case VObjectType::paPassSwitch_RTran: modName = "work@rtran"; break;
    case VObjectType::paCmosSwitchType_Cmos: modName = "work@cmos"; break;
    case VObjectType::paCmosSwitchType_RCmos: modName = "work@rcmos"; break;
    case VObjectType::paEnableGateType_Bufif0: modName = "work@bufif0"; break;
    case VObjectType::paEnableGateType_Bufif1: modName = "work@bufif1"; break;
    case VObjectType::paEnableGateType_Notif0: modName = "work@notif0"; break;
    case VObjectType::paEnableGateType_Notif1: modName = "work@notif1"; break;
    case VObjectType::paMosSwitchType_NMos: modName = "work@nmos"; break;
    case VObjectType::paMosSwitchType_PMos: modName = "work@pmos"; break;
    case VObjectType::paMosSwitchType_RNMos: modName = "work@rnmos"; break;
    case VObjectType::paMosSwitchType_RPMos: modName = "work@rpmos"; break;
    case VObjectType::PULLUP: modName = "work@pullup"; break;
    case VObjectType::PULLDOWN: modName = "work@pulldown"; break;
    default: modName = "work@UnsupportedPrimitive"; break;
  }
  return modName;
}

UhdmWriter::UhdmWriter(Session* session, CompileDesign* compileDesign, Design* design)
    : m_session(session), m_compileDesign(compileDesign), m_design(design), m_helper(session, compileDesign) {}

uint32_t UhdmWriter::getStrengthType(VObjectType type) {
  switch (type) {
    case VObjectType::SUPPLY0: return vpiSupply0;
    case VObjectType::SUPPLY1: return vpiSupply1;
    case VObjectType::STRONG0: return vpiStrongDrive;
    case VObjectType::STRONG1: return vpiStrongDrive;
    case VObjectType::PULL0: return vpiPullDrive;
    case VObjectType::PULL1: return vpiPullDrive;
    case VObjectType::WEAK0: return vpiWeakDrive;
    case VObjectType::WEAK1: return vpiWeakDrive;
    case VObjectType::HIGHZ0: return vpiHighZ;
    case VObjectType::HIGHZ1: return vpiHighZ;
    default: return 0;
  }
}

uint32_t UhdmWriter::getVpiOpType(VObjectType type) {
  switch (type) {
    case VObjectType::paBinOp_Plus: return vpiAddOp;
    case VObjectType::paBinOp_Minus: return vpiSubOp;
    case VObjectType::paBinOp_Mult: return vpiMultOp;
    case VObjectType::paBinOp_MultMult: return vpiPowerOp;
    case VObjectType::paBinOp_Div: return vpiDivOp;
    case VObjectType::paBinOp_Great: return vpiGtOp;
    case VObjectType::paBinOp_GreatEqual: return vpiGeOp;
    case VObjectType::paBinOp_Less: return vpiLtOp;
    case VObjectType::paBinOp_Imply: return vpiImplyOp;
    case VObjectType::paBinOp_Equivalence: return vpiEqOp;
    case VObjectType::paBinOp_LessEqual: return vpiLeOp;
    case VObjectType::paBinOp_Equiv: return vpiEqOp;
    case VObjectType::paBinOp_Not:
    case VObjectType::NOT: return vpiNeqOp;
    case VObjectType::paBinOp_Percent: return vpiModOp;
    case VObjectType::paBinOp_LogicAnd: return vpiLogAndOp;
    case VObjectType::paBinOp_LogicOr: return vpiLogOrOp;
    case VObjectType::paBinOp_BitwAnd: return vpiBitAndOp;
    case VObjectType::paBinOp_BitwOr: return vpiBitOrOp;
    case VObjectType::paBinOp_BitwXor: return vpiBitXorOp;
    case VObjectType::paBinOp_ReductXnor1:
    case VObjectType::paBinOp_ReductXnor2:
    case VObjectType::paBinModOp_ReductXnor1:
    case VObjectType::paBinModOp_ReductXnor2: return vpiBitXNorOp;
    case VObjectType::paBinOp_ReductNand: return vpiUnaryNandOp;
    case VObjectType::paBinOp_ReductNor: return vpiUnaryNorOp;
    case VObjectType::paUnary_Plus: return vpiPlusOp;
    case VObjectType::paUnary_Minus: return vpiMinusOp;
    case VObjectType::paUnary_Not: return vpiNotOp;
    case VObjectType::paUnary_Tilda: return vpiBitNegOp;
    case VObjectType::paUnary_BitwAnd: return vpiUnaryAndOp;
    case VObjectType::paUnary_BitwOr: return vpiUnaryOrOp;
    case VObjectType::paUnary_BitwXor: return vpiUnaryXorOp;
    case VObjectType::paUnary_ReductNand: return vpiUnaryNandOp;
    case VObjectType::paUnary_ReductNor: return vpiUnaryNorOp;
    case VObjectType::paUnary_ReductXnor1:
    case VObjectType::paUnary_ReductXnor2: return vpiUnaryXNorOp;
    case VObjectType::paBinOp_ShiftLeft: return vpiLShiftOp;
    case VObjectType::paBinOp_ShiftRight: return vpiRShiftOp;
    case VObjectType::paBinOp_ArithShiftLeft: return vpiArithLShiftOp;
    case VObjectType::paBinOp_ArithShiftRight: return vpiArithRShiftOp;
    case VObjectType::paIncDec_PlusPlus: return vpiPostIncOp;
    case VObjectType::paIncDec_MinusMinus: return vpiPostDecOp;
    case VObjectType::paConditional_operator:
    case VObjectType::QMARK: return vpiConditionOp;
    case VObjectType::INSIDE:
    case VObjectType::paOpen_range_list: return vpiInsideOp;
    case VObjectType::paBinOp_FourStateLogicEqual: return vpiCaseEqOp;
    case VObjectType::paBinOp_FourStateLogicNotEqual: return vpiCaseNeqOp;
    case VObjectType::paAssignOp_Assign: return vpiAssignmentOp;
    case VObjectType::paAssignOp_Add: return vpiAddOp;
    case VObjectType::paAssignOp_Sub: return vpiSubOp;
    case VObjectType::paAssignOp_Mult: return vpiMultOp;
    case VObjectType::paAssignOp_Div: return vpiDivOp;
    case VObjectType::paAssignOp_Modulo: return vpiModOp;
    case VObjectType::paAssignOp_BitwAnd: return vpiBitAndOp;
    case VObjectType::paAssignOp_BitwOr: return vpiBitOrOp;
    case VObjectType::paAssignOp_BitwXor: return vpiBitXorOp;
    case VObjectType::paAssignOp_BitwLeftShift: return vpiLShiftOp;
    case VObjectType::paAssignOp_BitwRightShift: return vpiRShiftOp;
    case VObjectType::paAssignOp_ArithShiftLeft: return vpiArithLShiftOp;
    case VObjectType::paAssignOp_ArithShiftRight: return vpiArithRShiftOp;
    case VObjectType::paMatches: return vpiMatchOp;
    case VObjectType::paBinOp_WildcardEqual:
    case VObjectType::paBinOp_WildEqual: return vpiWildEqOp;
    case VObjectType::paBinOp_WildcardNotEqual:
    case VObjectType::paBinOp_WildNotEqual: return vpiWildNeqOp;
    case VObjectType::IFF: return vpiIffOp;
    case VObjectType::OR: return vpiLogOrOp;
    case VObjectType::AND: return vpiLogAndOp;
    case VObjectType::NON_OVERLAP_IMPLY: return vpiNonOverlapImplyOp;
    case VObjectType::OVERLAP_IMPLY: return vpiOverlapImplyOp;
    case VObjectType::OVERLAPPED: return vpiOverlapFollowedByOp;
    case VObjectType::NONOVERLAPPED: return vpiNonOverlapFollowedByOp;
    case VObjectType::UNTIL: return vpiUntilOp;
    case VObjectType::S_UNTIL: return vpiUntilOp;
    case VObjectType::UNTIL_WITH: return vpiUntilWithOp;
    case VObjectType::S_UNTIL_WITH: return vpiUntilWithOp;
    case VObjectType::IMPLIES: return vpiImpliesOp;
    case VObjectType::paCycle_delay_range: return vpiCycleDelayOp;
    case VObjectType::paConsecutive_repetition: return vpiConsecutiveRepeatOp;
    case VObjectType::paNon_consecutive_repetition: return vpiRepeatOp;
    case VObjectType::paGoto_repetition: return vpiGotoRepeatOp;
    case VObjectType::THROUGHOUT: return vpiThroughoutOp;
    case VObjectType::WITHIN: return vpiWithinOp;
    case VObjectType::INTERSECT: return vpiIntersectOp;
    case VObjectType::FIRST_MATCH: return vpiFirstMatchOp;
    case VObjectType::STRONG: return vpiOpStrong;
    case VObjectType::ACCEPT_ON: return vpiAcceptOnOp;
    case VObjectType::SYNC_ACCEPT_ON: return vpiSyncAcceptOnOp;
    case VObjectType::REJECT_ON: return vpiRejectOnOp;
    case VObjectType::SYNC_REJECT_ON: return vpiSyncRejectOnOp;
    case VObjectType::NEXTTIME: return vpiNexttimeOp;
    case VObjectType::S_NEXTTIME: return vpiNexttimeOp;
    case VObjectType::ALWAYS: return vpiAlwaysOp;
    case VObjectType::EVENTUALLY: return vpiEventuallyOp;
    default: return 0;
  }
}

bool isMultidimensional(const uhdm::Typespec* ts) {
  bool isMultiDimension = false;
  if (ts) {
    if (ts->getUhdmType() == uhdm::UhdmType::LogicTypespec) {
      uhdm::LogicTypespec* lts = (uhdm::LogicTypespec*)ts;
      if (lts->getRanges() && lts->getRanges()->size() > 1) isMultiDimension = true;
    } else if (ts->getUhdmType() == uhdm::UhdmType::ArrayTypespec) {
      uhdm::ArrayTypespec* lts = (uhdm::ArrayTypespec*)ts;
      if (lts->getRanges() && lts->getRanges()->size() > 1) isMultiDimension = true;
    } else if (ts->getUhdmType() == uhdm::UhdmType::BitTypespec) {
      uhdm::BitTypespec* lts = (uhdm::BitTypespec*)ts;
      if (lts->getRanges() && lts->getRanges()->size() > 1) isMultiDimension = true;
    }
  }
  return isMultiDimension;
}

uint32_t UhdmWriter::getVpiDirection(VObjectType type) {
  uint32_t direction = vpiInout;
  if (type == VObjectType::paPortDir_Inp || type == VObjectType::paTfPortDir_Inp)
    direction = vpiInput;
  else if (type == VObjectType::paPortDir_Out || type == VObjectType::paTfPortDir_Out)
    direction = vpiOutput;
  else if (type == VObjectType::paPortDir_Inout || type == VObjectType::paTfPortDir_Inout)
    direction = vpiInout;
  else if (type == VObjectType::paTfPortDir_Ref || type == VObjectType::paTfPortDir_ConstRef)
    direction = vpiRef;
  return direction;
}

uint32_t UhdmWriter::getVpiNetType(VObjectType type) {
  uint32_t nettype = 0;
  switch (type) {
    case VObjectType::paNetType_Wire: nettype = vpiWire; break;
    case VObjectType::paIntVec_TypeReg: nettype = vpiReg; break;
    case VObjectType::paNetType_Supply0: nettype = vpiSupply0; break;
    case VObjectType::paNetType_Supply1: nettype = vpiSupply1; break;
    case VObjectType::paIntVec_TypeLogic: nettype = vpiLogicNet; break;
    case VObjectType::paNetType_Wand: nettype = vpiWand; break;
    case VObjectType::paNetType_Wor: nettype = vpiWor; break;
    case VObjectType::paNetType_Tri: nettype = vpiTri; break;
    case VObjectType::paNetType_Tri0: nettype = vpiTri0; break;
    case VObjectType::paNetType_Tri1: nettype = vpiTri1; break;
    case VObjectType::paNetType_TriReg: nettype = vpiTriReg; break;
    case VObjectType::paNetType_TriAnd: nettype = vpiTriAnd; break;
    case VObjectType::paNetType_TriOr: nettype = vpiTriOr; break;
    case VObjectType::paNetType_Uwire: nettype = vpiUwire; break;
    case VObjectType::paImplicit_data_type:
    case VObjectType::paSigning_Signed:
    case VObjectType::paPacked_dimension:
    case VObjectType::paSigning_Unsigned: nettype = vpiNone; break;
    default: break;
  }
  return nettype;
}

void UhdmWriter::writePorts(const std::vector<Signal*>& orig_ports, uhdm::BaseClass* parent, uhdm::Serializer& s,
                            ModportMap& modPortMap, SignalBaseClassMap& signalBaseMap, SignalMap& signalMap,
                            ModuleInstance* instance, DesignComponent* mod) {
  int32_t lastPortDirection = vpiInout;
  for (Signal* orig_port : orig_ports) {
    uhdm::Port* dest_port = s.make<uhdm::Port>();
    signalBaseMap.emplace(orig_port, dest_port);
    signalMap.emplace(orig_port->getName(), orig_port);

    if (orig_port->attributes()) {
      dest_port->setAttributes(orig_port->attributes());
      for (auto ats : *orig_port->attributes()) {
        ats->setParent(dest_port);
      }
    }

    const FileContent* fC = orig_port->getFileContent();
    if (fC->Type(orig_port->getNameId()) == VObjectType::STRING_CONST) dest_port->setName(orig_port->getName());
    if (orig_port->getDirection() != VObjectType::NO_TYPE)
      lastPortDirection = UhdmWriter::getVpiDirection(orig_port->getDirection());
    dest_port->setDirection(lastPortDirection);
    if (const FileContent* const fC = orig_port->getFileContent()) {
      fC->populateCoreMembers(orig_port->getNameId(), orig_port->getNameId(), dest_port);
    }
    dest_port->setParent(parent);
    if (Modport* orig_modport = orig_port->getModport()) {
      uhdm::RefObj* ref = s.make<uhdm::RefObj>();
      ref->setName(orig_port->getName());
      ref->setParent(dest_port);
      dest_port->setLowConn(ref);
      std::map<Modport*, uhdm::Modport*>::iterator itr = modPortMap.find(orig_modport);
      if (itr != modPortMap.end()) {
        ref->setActual((*itr).second);
      }
    } else if (ModuleDefinition* orig_interf = orig_port->getInterfaceDef()) {
      uhdm::RefObj* ref = s.make<uhdm::RefObj>();
      ref->setName(orig_port->getName());
      ref->setParent(dest_port);
      dest_port->setLowConn(ref);
      const auto& found = m_componentMap.find(orig_interf);
      if (found != m_componentMap.end()) {
        ref->setActual(found->second);
      }
    }
    if (NodeId defId = orig_port->getDefaultValue()) {
      if (uhdm::Any* exp = m_helper.compileExpression(mod, fC, defId, dest_port, instance, false)) {
        dest_port->setHighConn(exp);
      }
    }
    if (orig_port->getTypespecId() && mod) {
      if (NodeId unpackedDimensions = orig_port->getUnpackedDimension()) {
        NodeId packedDimensions = orig_port->getPackedDimension();
        int32_t unpackedSize = 0;
        const FileContent* fC = orig_port->getFileContent();
        uhdm::ArrayTypespec* array_ts = s.make<uhdm::ArrayTypespec>();
        if (std::vector<uhdm::Range*>* ranges =
                m_helper.compileRanges(mod, fC, unpackedDimensions, array_ts, instance, unpackedSize, false)) {
          array_ts->setRanges(ranges);
          array_ts->setParent(dest_port);
          fC->populateCoreMembers(unpackedDimensions, unpackedDimensions, array_ts);
          for (uhdm::Range* r : *ranges) {
            r->setParent(array_ts);
            if (const uhdm::Constant* const c = r->getRightExpr<Constant>()) {
              if (c->getValue() == "STRING:associative") {
                array_ts->setArrayType(vpiAssocArray);
                if (const uhdm::RefTypespec* rt = c->getTypespec()) {
                  if (const uhdm::Typespec* ag = rt->getActual()) {
                    uhdm::RefTypespec* cro = s.make<uhdm::RefTypespec>();
                    cro->setName(ag->getName());
                    cro->setParent(array_ts);
                    cro->setFile(array_ts->getFile());
                    cro->setStartLine(array_ts->getStartLine());
                    cro->setStartColumn(array_ts->getStartColumn());
                    cro->setEndLine(array_ts->getEndLine());
                    cro->setEndColumn(array_ts->getEndColumn());
                    cro->setActual(const_cast<uhdm::Typespec*>(ag));
                    array_ts->setIndexTypespec(cro);
                  }
                }
              } else if (c->getValue() == "STRING:unsized") {
                array_ts->setArrayType(vpiDynamicArray);
              } else if (c->getValue() == "STRING:$") {
                array_ts->setArrayType(vpiQueueArray);
              } else {
                array_ts->setArrayType(vpiStaticArray);
              }
            }
          }
          if (dest_port->getTypespec() == nullptr) {
            uhdm::RefTypespec* dest_port_rt = s.make<uhdm::RefTypespec>();
            CompileHelper::setRefTypespecName(dest_port_rt, array_ts, fC->SymName(orig_port->getTypespecId()));
            dest_port_rt->setParent(dest_port);
            fC->populateCoreMembers(orig_port->getTypespecId(), orig_port->getTypespecId(), dest_port_rt);
            dest_port->setTypespec(dest_port_rt);
          }
          dest_port->getTypespec()->setActual(array_ts);
          if (uhdm::Typespec* ts = m_helper.compileTypespec(
                  mod, fC, orig_port->getTypespecId(), orig_port->getUnpackedDimension(), array_ts, nullptr, true)) {
            if (array_ts->getElemTypespec() == nullptr) {
              uhdm::RefTypespec* array_ts_rt = s.make<uhdm::RefTypespec>();
              array_ts_rt->setParent(array_ts);
              fC->populateCoreMembers(orig_port->getTypespecId(),
                                      packedDimensions ? packedDimensions : orig_port->getTypespecId(), array_ts_rt);
              array_ts_rt->setName(ts->getName());
              array_ts->setElemTypespec(array_ts_rt);
            }
            array_ts->getElemTypespec()->setActual(ts);
          }
        }
      } else if (uhdm::Typespec* ts =
                     m_helper.compileTypespec(mod, fC, orig_port->getTypespecId(), orig_port->getUnpackedDimension(),
                                              dest_port, nullptr, true)) {
        if (dest_port->getTypespec() == nullptr) {
          uhdm::RefTypespec* dest_port_rt = s.make<uhdm::RefTypespec>();
          dest_port_rt->setName(ts->getName());
          dest_port_rt->setParent(dest_port);
          dest_port->setTypespec(dest_port_rt);
          fC->populateCoreMembers(orig_port->getTypespecId(), orig_port->getTypespecId(), dest_port_rt);
        }
        dest_port->getTypespec()->setActual(ts);
      }
    }
  }
}

void UhdmWriter::writeDataTypes(const DesignComponent::DataTypeMap& datatypeMap, uhdm::BaseClass* parent,
                                uhdm::TypespecCollection* dest_typespecs, uhdm::Serializer& s, bool setParent) {
  std::set<uint64_t> ids;
  for (const uhdm::Typespec* t : *dest_typespecs) ids.emplace(t->getUhdmId());
  for (const auto& entry : datatypeMap) {
    const DataType* dtype = entry.second;
    if (dtype->getCategory() == DataType::Category::REF) {
      dtype = dtype->getDefinition();
    }
    if (dtype->getCategory() == DataType::Category::TYPEDEF) {
      if (dtype->getTypespec() == nullptr) dtype = dtype->getDefinition();
    }
    uhdm::Typespec* tps = dtype->getTypespec();
    tps = replace(tps, m_compileDesign->getSwapedObjects());
    if (parent->getUhdmType() == uhdm::UhdmType::Package) {
      if (tps && (tps->getName().find("::") == std::string::npos)) {
        const std::string newName = StrCat(parent->getName(), "::", tps->getName());
        if (uhdm::TypedefTypespec* tt = any_cast<uhdm::TypedefTypespec>(tps)) {
          tt->setName(newName);
        }
      }
    }

    if (tps) {
      if (!tps->getInstance()) {
        if (parent->getUhdmType() != uhdm::UhdmType::ClassDefn) tps->setInstance((uhdm::Instance*)parent);
      }
      if (setParent && (tps->getParent() == nullptr)) {
        tps->setParent(parent);
        ids.emplace(tps->getUhdmId());
      } else if (ids.emplace(tps->getUhdmId()).second) {
        tps->setParent(parent);
      }
    }
  }
}

void UhdmWriter::writeNets(DesignComponent* mod, const std::vector<Signal*>& orig_nets, uhdm::BaseClass* parent,
                           uhdm::Serializer& s, SignalBaseClassMap& signalBaseMap, SignalMap& signalMap,
                           SignalMap& portMap, ModuleInstance* instance /* = nullptr */) {
  for (auto& orig_net : orig_nets) {
    uhdm::Net* dest_net = nullptr;
    if (instance == nullptr) {
      dest_net = s.make<uhdm::Net>();
    }
    if (dest_net) {
      const FileContent* fC = orig_net->getFileContent();
      if (fC->Type(orig_net->getNameId()) == VObjectType::STRING_CONST) {
        auto portItr = portMap.find(orig_net->getName());
        if (portItr != portMap.end()) {
          Signal* sig = (*portItr).second;
          if (sig) {
            UhdmWriter::SignalBaseClassMap::iterator itr = signalBaseMap.find(sig);
            if (itr != signalBaseMap.end()) {
              uhdm::Port* p = (uhdm::Port*)((*itr).second);
              NodeId nameId = orig_net->getNameId();
              if (p->getLowConn() == nullptr) {
                uhdm::RefObj* ref = s.make<uhdm::RefObj>();
                ref->setName(p->getName());
                ref->setActual(dest_net);
                ref->setParent(p);
                p->setLowConn(ref);
                fC->populateCoreMembers(nameId, nameId, ref);
              } else if (p->getLowConn()->getUhdmType() == uhdm::UhdmType::RefObj) {
                uhdm::RefObj* ref = (uhdm::RefObj*)p->getLowConn();
                ref->setName(p->getName());
                if (ref->getStartLine() == 0) {
                  fC->populateCoreMembers(nameId, nameId, ref);
                }
                if (ref->getActual() == nullptr) {
                  ref->setActual(dest_net);
                }
                ref->setParent(p);
              }
            }
          }
        } else if (dest_net->getTypespec() == nullptr) {
          // compileTypespec function need to account for range
          // location information if there is any in the typespec.
          if (orig_net->getTypespecId()) {
            if (uhdm::Typespec* ts = m_helper.compileTypespec(
                    mod, fC, orig_net->getTypespecId(), orig_net->getUnpackedDimension(), nullptr, nullptr, true)) {
              uhdm::RefTypespec* rt = s.make<uhdm::RefTypespec>();
              rt->setName(ts->getName());
              rt->setParent(dest_net);
              rt->setActual(ts);
              dest_net->setTypespec(rt);
              fC->populateCoreMembers(orig_net->getTypespecId(), orig_net->getTypespecId(), rt);
              NodeId dimensions = orig_net->getUnpackedDimension();
              if (!dimensions) dimensions = orig_net->getPackedDimension();
              if (dimensions) {
                fC->populateCoreMembers(InvalidNodeId, dimensions, ts);
              }
            }
          }
        }
        signalBaseMap.emplace(orig_net, dest_net);
        signalMap.emplace(orig_net->getName(), orig_net);
        dest_net->setName(orig_net->getName());
        if (const FileContent* const fC = orig_net->getFileContent()) {
          fC->populateCoreMembers(orig_net->getNameId(), orig_net->getNameId(), dest_net);
        }
        dest_net->setNetType(UhdmWriter::getVpiNetType(orig_net->getType()));
        dest_net->setParent(parent);
      }
    }
  }
}

void mapLowConns(const std::vector<Signal*>& orig_ports, uhdm::Serializer& s,
                 UhdmWriter::SignalBaseClassMap& signalBaseMap) {
  for (Signal* orig_port : orig_ports) {
    if (Signal* lowconn = orig_port->getLowConn()) {
      UhdmWriter::SignalBaseClassMap::iterator itrlow = signalBaseMap.find(lowconn);
      if (itrlow != signalBaseMap.end()) {
        UhdmWriter::SignalBaseClassMap::iterator itrport = signalBaseMap.find(orig_port);
        if (itrport != signalBaseMap.end()) {
          uhdm::RefObj* ref = s.make<uhdm::RefObj>();
          ((uhdm::Port*)itrport->second)->setLowConn(ref);
          ref->setParent(itrport->second);
          ref->setActual(itrlow->second);
          ref->setName(orig_port->getName());
          orig_port->getFileContent()->populateCoreMembers(orig_port->getNodeId(), orig_port->getNodeId(), ref);
        }
      }
    }
  }
}

void UhdmWriter::writeClass(ClassDefinition* classDef, uhdm::Serializer& s, uhdm::BaseClass* parent) {
  if (!classDef->getFileContents().empty() && classDef->getType() == VObjectType::paClass_declaration) {
    const FileContent* fC = classDef->getFileContents()[0];
    uhdm::ClassDefn* c = classDef->getUhdmModel<uhdm::ClassDefn>();
    m_componentMap.emplace(classDef, c);
    c->setParent(parent);
    classDef->getUhdmTypespecModel()->setParent(parent);
    c->setEndLabel(classDef->getEndLabel());
    c->setTaskFuncDecls(classDef->getTaskFuncDecls());

    // Typepecs
    uhdm::TypespecCollection* typespecs = c->getTypespecs(true);
    writeDataTypes(classDef->getDataTypeMap(), c, typespecs, s, true);

    // Variables
    // Already bound in TestbenchElaboration

    // Extends, fix late binding
    if (const uhdm::Extends* ext = c->getExtends()) {
      if (const uhdm::RefTypespec* ext_rt = ext->getClassTypespec()) {
        if (const uhdm::ClassTypespec* tps = ext_rt->getActual<uhdm::ClassTypespec>()) {
          if (tps->getClassDefn() == nullptr) {
            const std::string_view tpsName = tps->getName();
            if (c->getParameters()) {
              for (auto ps : *c->getParameters()) {
                if (ps->getName() == tpsName) {
                  if (ps->getUhdmType() == uhdm::UhdmType::TypeParameter) {
                    uhdm::TypeParameter* tp = (uhdm::TypeParameter*)ps;
                    if (const uhdm::RefTypespec* tp_rt = tp->getTypespec()) {
                      if (const uhdm::ClassTypespec* cptp = tp_rt->getActual<uhdm::ClassTypespec>()) {
                        ((uhdm::ClassTypespec*)tps)->setClassDefn((uhdm::ClassDefn*)cptp->getClassDefn());
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }

    // Param_assigns
    if (classDef->getParamAssigns()) {
      c->setParamAssigns(classDef->getParamAssigns());
      for (auto ps : *c->getParamAssigns()) {
        ps->setParent(c);
      }
    }
    c->setParent(parent);

    const std::string_view name = classDef->getName();
    if (c->getName().empty()) c->setName(name);
    if (c->getFullName().empty()) c->setFullName(name);
    if (classDef->getAttributes() != nullptr) {
      c->setAttributes(classDef->getAttributes());
      for (auto a : *c->getAttributes()) {
        a->setParent(c);
      }
    }
    if (fC) {
      // Builtin classes have no file
      const NodeId modId = classDef->getNodeIds()[0];
      const NodeId startId = fC->sl_collect(modId, VObjectType::CLASS);
      fC->populateCoreMembers(startId, modId, c);
    }
    // Activate when hier_path is better supported
    // lateTypedefBinding(s, classDef, c);
    // lateBinding(s, classDef, c);

    for (auto& nested : classDef->getClassMap()) {
      writeClass(nested.second, s, c);
    }
  }
}

void UhdmWriter::writeClasses(ClassNameClassDefinitionMultiMap& orig_classes, uhdm::Serializer& s,
                              uhdm::BaseClass* parent) {
  for (auto& orig_class : orig_classes) {
    writeClass(orig_class.second, s, parent);
  }
}

void UhdmWriter::writeVariables(const DesignComponent::VariableMap& orig_vars, uhdm::BaseClass* parent,
                                uhdm::Serializer& s) {
  for (auto& orig_var : orig_vars) {
    Variable* var = orig_var.second;
    const DataType* dtype = var->getDataType();
    const ClassDefinition* classdef = datatype_cast<ClassDefinition>(dtype);
    if (classdef) {
      uhdm::Variable* cvar = s.make<uhdm::Variable>();
      cvar->setName(var->getName());
      var->getFileContent()->populateCoreMembers(var->getNodeId(), var->getNodeId(), cvar);
      cvar->setParent(parent);
      const auto& found = m_componentMap.find(classdef);
      if (found != m_componentMap.end()) {
        // TODO: Bind Class type,
        // class_var -> class_typespec -> class_defn
      }
    }
  }
}

class ReInstanceTypespec final : public UhdmVisitor {
 public:
  explicit ReInstanceTypespec(uhdm::Package* p) : m_package(p) {}
  ~ReInstanceTypespec() override = default;

  void visitAny(const uhdm::Any* object) final {
    if (any_cast<uhdm::Typespec>(object) != nullptr) {
      if ((object->getUhdmType() != uhdm::UhdmType::EventTypespec) &&
          (object->getUhdmType() != uhdm::UhdmType::ImportTypespec) &&
          (object->getUhdmType() != uhdm::UhdmType::TypeParameter)) {
        reInstance(object);
      }
    }
  }

  void visitFunction(const uhdm::Function* object) final { reInstance(object); }
  void visitTask(const uhdm::Task* object) final { reInstance(object); }

  void reInstance(const uhdm::Any* cobject) {
    if (cobject == nullptr) return;
    uhdm::Any* object = (uhdm::Any*)cobject;
    const uhdm::Instance* inst = nullptr;
    if (uhdm::Typespec* tps = any_cast<uhdm::Typespec>(object)) {
      inst = (uhdm::Instance*)tps->getInstance();
    } else if (uhdm::Function* tps = any_cast<uhdm::Function>(object)) {
      inst = (uhdm::Instance*)tps->getInstance();
    } else if (uhdm::Task* tps = any_cast<uhdm::Task>(object)) {
      inst = (uhdm::Instance*)tps->getInstance();
    }
    if (inst) {
      const std::string_view name = inst->getName();
      uhdm::Design* d = (uhdm::Design*)m_package->getParent();
      if (d->getAllPackages() != nullptr) {
        for (auto pack : *d->getAllPackages()) {
          if (pack->getName() == name) {
            if (uhdm::Typespec* tps = any_cast<uhdm::Typespec>(object)) {
              tps->setInstance(pack);
              if (const uhdm::EnumTypespec* et = any_cast<uhdm::EnumTypespec>(cobject)) {
                reInstance(et->getBaseTypespec());
              }
            } else if (uhdm::Function* tps = any_cast<uhdm::Function>(object)) {
              tps->setInstance(pack);
            } else if (uhdm::Task* tps = any_cast<uhdm::Task>(object)) {
              tps->setInstance(pack);
            }
            break;
          }
        }
      }
    }
  }

 private:
  uhdm::Package* m_package = nullptr;
};

// Non-elaborated package typespec Instance relation need to point to
// non-elablarated package
void reInstanceTypespec(Serializer& serializer, uhdm::Any* root, uhdm::Package* p) {
  ReInstanceTypespec(p).visit(root);
}

void UhdmWriter::writePackage(Package* pack, uhdm::Package* p, uhdm::Serializer& s) {
  const uhdm::ScopedScope scopedScope(p);

  if (!pack->getEndLabel().empty()) {
    p->setEndLabel(pack->getEndLabel());
  }

  // Classes
  ClassNameClassDefinitionMultiMap& orig_classes = pack->getClassDefinitions();
  writeClasses(orig_classes, s, p);

  // Parameters
  if (p->getParameters()) {
    for (auto ps : *p->getParameters()) {
      if (ps->getUhdmType() == uhdm::UhdmType::Parameter) {
        ((uhdm::Parameter*)ps)->setFullName(StrCat(pack->getName(), "::", ps->getName()));
      } else {
        ((uhdm::TypeParameter*)ps)->setFullName(StrCat(pack->getName(), "::", ps->getName()));
      }
    }
  }

  // Param_assigns

  if (pack->getParamAssigns()) {
    p->setParamAssigns(pack->getParamAssigns());
    for (auto ps : *p->getParamAssigns()) {
      ps->setParent(p);
      reInstanceTypespec(s, ps, p);
    }
  }

  if (p->getTypespecs()) {
    for (auto t : *p->getTypespecs()) {
      reInstanceTypespec(s, t, p);
    }
  }

  if (p->getVariables()) {
    for (auto v : *p->getVariables()) {
      reInstanceTypespec(s, v, p);
    }
  }

  // Function and tasks
  if (pack->getTaskFuncs()) {
    for (auto tf : *pack->getTaskFuncs()) {
      const std::string_view funcName = tf->getName();
      if (funcName.find("::") != std::string::npos) {
        std::vector<std::string_view> res;
        StringUtils::tokenizeMulti(funcName, "::", res);
        const std::string_view className = res[0];
        const std::string_view funcName = res[1];
        bool foundParentClass = false;
        if (p->getClassDefns()) {
          for (auto cl : *p->getClassDefns()) {
            if (cl->getName() == className) {
              tf->setParent(cl, true);
              tf->setInstance(p);
              foundParentClass = true;
              break;
            }
          }
        }
        if (foundParentClass) {
          tf->setName(funcName);
          ((uhdm::TaskFunc*)tf)->setFullName(StrCat(pack->getName(), "::", className, "::", tf->getName()));
        } else {
          tf->setParent(p);
          tf->setInstance(p);
          ((uhdm::TaskFunc*)tf)->setFullName(StrCat(pack->getName(), "::", tf->getName()));
        }
      } else {
        tf->setParent(p);
        tf->setInstance(p);
        ((uhdm::TaskFunc*)tf)->setFullName(StrCat(pack->getName(), "::", tf->getName()));
      }
    }
  }
  // Nets
  SignalBaseClassMap signalBaseMap;
  SignalMap portMap;
  SignalMap netMap;
  const std::vector<Signal*>& orig_nets = pack->getSignals();
  writeNets(pack, orig_nets, p, s, signalBaseMap, netMap, portMap, nullptr);
}

void UhdmWriter::writeModule(ModuleDefinition* mod, uhdm::Module* m, uhdm::Serializer& s,
                             InstanceDefinitionMap& instanceDefinitionMap, ModportMap& modPortMap,
                             ModuleInstance* instance) {
  const uhdm::ScopedScope scopedScope(m);
  SignalBaseClassMap signalBaseMap;
  SignalMap portMap;
  SignalMap netMap;

  m->setEndLabel(mod->getEndLabel());

  // Let decls
  if (!mod->getLetStmts().empty()) {
    for (auto& stmt : mod->getLetStmts()) {
      const_cast<uhdm::LetDecl*>(stmt.second->getDecl())->setParent(m);
    }
  }
  // Gen vars
  if (mod->getGenVars()) {
    for (auto var : *mod->getGenVars()) {
      var->setParent(m);
    }
  }
  // Gen stmts
  if (mod->getGenStmts()) {
    for (auto stmt : *mod->getGenStmts()) {
      stmt->setParent(m);
    }
  }
  if (!mod->getPropertyDecls().empty()) {
    for (auto decl : mod->getPropertyDecls()) {
      decl->setParent(m);
    }
  }
  if (!mod->getSequenceDecls().empty()) {
    for (auto decl : mod->getSequenceDecls()) {
      decl->setParent(m);
    }
  }

  // Ports
  const std::vector<Signal*>& orig_ports = mod->getPorts();
  writePorts(orig_ports, m, s, modPortMap, signalBaseMap, portMap, instance, mod);
  // Nets
  mapLowConns(orig_ports, s, signalBaseMap);
  // Classes
  ClassNameClassDefinitionMultiMap& orig_classes = mod->getClassDefinitions();
  writeClasses(orig_classes, s, m);

  // ClockingBlocks
  for (auto& ctupple : mod->getClockingBlockMap()) {
    ClockingBlock& cblock = ctupple.second;
    cblock.getUhdmModel()->setParent(m);
    switch (cblock.getType()) {
      case ClockingBlock::Type::Default: {
        m->setDefaultClocking(cblock.getUhdmModel());
        break;
      }
      case ClockingBlock::Type::Global: {
        m->setGlobalClocking(cblock.getUhdmModel());
        break;
      }
      default: break;
    }
  }

  // Assertions
  if (mod->getAssertions()) {
    for (auto ps : *mod->getAssertions()) {
      ps->setParent(m);
    }
  }
  // Module Instantiation
  if (std::vector<uhdm::RefModule*>* subModules = mod->getRefModules()) {
    for (auto subModArr : *subModules) {
      subModArr->setParent(m);
    }
  }
  if (uhdm::ModuleArrayCollection* subModuleArrays = mod->getModuleArrays()) {
    for (auto subModArr : *subModuleArrays) {
      subModArr->setParent(m);
    }
  }
  if (uhdm::PrimitiveCollection* subModules = mod->getPrimitives()) {
    for (auto subModArr : *subModules) {
      subModArr->setParent(m);
    }
  }
  if (uhdm::PrimitiveArrayCollection* subModules = mod->getPrimitiveArrays()) {
    for (auto subModArr : *subModules) {
      subModArr->setParent(m);
    }
  }
  // Interface instantiation
  const std::vector<Signal*>& signals = mod->getSignals();
  if (!signals.empty()) {
    uhdm::InterfaceArrayCollection* subInterfaceArrays = m->getInterfaceArrays(true);
    for (Signal* sig : signals) {
      NodeId unpackedDimension = sig->getUnpackedDimension();
      if (unpackedDimension && sig->getInterfaceDef()) {
        uhdm::InterfaceArray* smarray = s.make<uhdm::InterfaceArray>();
        int32_t unpackedSize = 0;
        const FileContent* fC = sig->getFileContent();
        if (std::vector<uhdm::Range*>* unpackedDimensions =
                m_helper.compileRanges(mod, fC, unpackedDimension, smarray, instance, unpackedSize, false)) {
          NodeId id = sig->getNodeId();
          const std::string typeName = sig->getInterfaceTypeName();
          smarray->setRanges(unpackedDimensions);
          for (auto d : *unpackedDimensions) d->setParent(smarray);
          if (fC->Type(sig->getNameId()) == VObjectType::STRING_CONST) {
            smarray->setName(sig->getName());
          }
          smarray->setFullName(typeName);
          smarray->setParent(m);
          fC->populateCoreMembers(id, id, smarray);

          NodeId typespecStart = sig->getInterfaceTypeNameId();
          NodeId typespecEnd = typespecStart;
          while (fC->Sibling(typespecEnd)) {
            typespecEnd = fC->Sibling(typespecEnd);
          }
          if (smarray->getElemTypespec() == nullptr) {
            uhdm::RefTypespec* tps_rt = s.make<uhdm::RefTypespec>();
            tps_rt->setParent(smarray);
            smarray->setElemTypespec(tps_rt);
          }
          ModuleDefinition* intefacedef = m_design->getModuleDefinition(typeName);
          smarray->getElemTypespec()->setActual(intefacedef->getUhdmTypespecModel());

          subInterfaceArrays->emplace_back(smarray);
        }
      }
    }
  }
}

void UhdmWriter::writeInterface(ModuleDefinition* mod, uhdm::Interface* m, uhdm::Serializer& s, ModportMap& modPortMap,
                                ModuleInstance* instance) {
  const uhdm::ScopedScope scopedScope(m);

  SignalBaseClassMap signalBaseMap;
  SignalMap portMap;
  SignalMap netMap;

  m->setEndLabel(mod->getEndLabel());

  // Let decls
  if (!mod->getLetStmts().empty()) {
    uhdm::LetDeclCollection* decls = m->getLetDecls(true);
    for (auto stmt : mod->getLetStmts()) {
      decls->emplace_back((uhdm::LetDecl*)stmt.second->getDecl());
    }
  }
  // Gen stmts
  if (mod->getGenStmts()) {
    for (auto stmt : *mod->getGenStmts()) {
      stmt->setParent(m);
    }
  }
  if (!mod->getPropertyDecls().empty()) {
    for (auto decl : mod->getPropertyDecls()) {
      decl->setParent(m);
    }
  }
  if (!mod->getSequenceDecls().empty()) {
    for (auto decl : mod->getSequenceDecls()) {
      decl->setParent(m);
    }
  }

  // Typepecs
  uhdm::TypespecCollection* typespecs = m->getTypespecs(true);
  writeDataTypes(mod->getDataTypeMap(), m, typespecs, s, true);

  // Ports
  const std::vector<Signal*>& orig_ports = mod->getPorts();
  writePorts(orig_ports, m, s, modPortMap, signalBaseMap, portMap, instance, mod);
  const std::vector<Signal*>& orig_nets = mod->getSignals();
  writeNets(mod, orig_nets, m, s, signalBaseMap, netMap, portMap, instance);

  // Modports
  ModuleDefinition::ModportSignalMap& orig_modports = mod->getModportSignalMap();
  for (auto& orig_modport : orig_modports) {
    uhdm::Modport* dest_modport = orig_modport.second.getUhdmModel();
    modPortMap.emplace(&orig_modport.second, dest_modport);
    for (auto& sig : orig_modport.second.getPorts()) {
      const FileContent* fC = sig.getFileContent();
      uhdm::IODecl* io = s.make<uhdm::IODecl>();
      io->setName(sig.getName());
      fC->populateCoreMembers(sig.getNameId(), sig.getNameId(), io);
      if (NodeId Expression = fC->Sibling(sig.getNodeId())) {
        m_helper.checkForLoops(true);
        if (uhdm::Any* exp = m_helper.compileExpression(mod, fC, Expression, io, instance, true)) {
          io->setExpr(exp);
        }
        m_helper.checkForLoops(false);
      }
      uint32_t direction = UhdmWriter::getVpiDirection(sig.getDirection());
      io->setDirection(direction);
      io->setParent(dest_modport);
    }
  }

  // Cont assigns
  if (mod->getContAssigns()) {
    for (auto ps : *mod->getContAssigns()) {
      ps->setParent(m);
    }
  }

  // Assertions
  if (mod->getAssertions()) {
    for (auto ps : *mod->getAssertions()) {
      ps->setParent(m);
    }
  }

  // Processes
  if (mod->getProcesses()) {
    for (auto ps : *mod->getProcesses()) {
      ps->setParent(m);
    }
  }

  // ClockingBlocks
  for (auto& ctupple : mod->getClockingBlockMap()) {
    ClockingBlock& cblock = ctupple.second;
    cblock.getUhdmModel()->setParent(m);
    switch (cblock.getType()) {
      case ClockingBlock::Type::Default: {
        m->setDefaultClocking(cblock.getUhdmModel());
        break;
      }
      case ClockingBlock::Type::Global: {
        m->setGlobalClocking(cblock.getUhdmModel());
        break;
      }
      default: break;
    }
  }
}

void UhdmWriter::writeProgram(Program* mod, uhdm::Program* m, uhdm::Serializer& s, ModportMap& modPortMap,
                              ModuleInstance* instance) {
  const uhdm::ScopedScope scopedScope(m);

  SignalBaseClassMap signalBaseMap;
  SignalMap portMap;
  SignalMap netMap;

  m->setEndLabel(mod->getEndLabel());

  // Typepecs
  uhdm::TypespecCollection* typespecs = m->getTypespecs(true);
  writeDataTypes(mod->getDataTypeMap(), m, typespecs, s, true);

  // Ports
  const std::vector<Signal*>& orig_ports = mod->getPorts();
  writePorts(orig_ports, m, s, modPortMap, signalBaseMap, portMap, instance, mod);

  // Nets
  const std::vector<Signal*>& orig_nets = mod->getSignals();
  writeNets(mod, orig_nets, m, s, signalBaseMap, netMap, portMap, instance);
  mapLowConns(orig_ports, s, signalBaseMap);

  // Assertions
  if (mod->getAssertions()) {
    for (auto ps : *mod->getAssertions()) {
      ps->setParent(m);
    }
  }

  // Classes
  ClassNameClassDefinitionMultiMap& orig_classes = mod->getClassDefinitions();
  writeClasses(orig_classes, s, m);

  // Variables
  const DesignComponent::VariableMap& orig_vars = mod->getVariables();
  writeVariables(orig_vars, m, s);

  // Cont assigns
  if (mod->getContAssigns()) {
    for (auto ps : *mod->getContAssigns()) {
      ps->setParent(m);
    }
  }
  // Processes
  if (mod->getProcesses()) {
    for (auto ps : *mod->getProcesses()) {
      ps->setParent(m);
    }
  }
  if (mod->getTaskFuncs()) {
    for (auto tf : *mod->getTaskFuncs()) {
      tf->setParent(m);
    }
  }

  // ClockingBlocks
  for (auto& ctupple : mod->getClockingBlockMap()) {
    ClockingBlock& cblock = ctupple.second;
    cblock.getUhdmModel()->setParent(m);
    switch (cblock.getType()) {
      case ClockingBlock::Type::Default: {
        m->setDefaultClocking(cblock.getUhdmModel());
        break;
      }
      case ClockingBlock::Type::Global: {
        // m->Global_clocking(cblock.getUhdmModel());
        break;
      }
      default: break;
    }
  }
}

class DetectUnsizedConstant final : public UhdmVisitor {
 public:
  DetectUnsizedConstant() = default;
  bool unsizedDetected() const { return m_unsized; }

 private:
  void visitConstant(const uhdm::Constant* object) final {
    if (object->getSize() == -1) {
      m_unsized = true;
      requestAbort();
    }
  }
  bool m_unsized = false;
};

void UhdmWriter::bind(uhdm::Serializer& s, const std::vector<vpiHandle>& designs) {
  CommandLineParser* commandLineParser = m_session->getCommandLineParser();
  if (ObjectBinder* const listener = new ObjectBinder(m_session, m_componentMap, s, commandLineParser->muteStdout())) {
    for (auto h : designs) {
      const uhdm::Design* const d = UhdmDesignFromVpiHandle(h);
      listener->bind(d, true);
    }
    delete listener;
  }
}

class AlwaysWithForLoop final : public UhdmVisitor {
 public:
  explicit AlwaysWithForLoop() = default;
  void visitForStmt(const uhdm::ForStmt* object) final {
    containtsForStmt = true;
    requestAbort();
  }
  bool containtsForStmt = false;
};

bool alwaysContainsForLoop(Serializer& serializer, uhdm::Any* root) {
  AlwaysWithForLoop listener;
  listener.visit(root);
  return listener.containtsForStmt;
}

// synlig has a major problem processing always blocks.
// They are processed mainly in the allModules section which is incorrect in
// some case. They should be processed from the topModules section. Here we try
// to fix temporarily this by filtering out the always blocks containing
// for-loops from the allModules, and those without from the topModules
void filterAlwaysBlocks(Serializer& s, uhdm::Design* d) {
  if (d->getAllModules()) {
    for (auto module : *d->getAllModules()) {
      if (module->getProcesses()) {
        bool more = true;
        while (more) {
          more = false;
          for (std::vector<uhdm::Process*>::iterator itr = module->getProcesses()->begin();
               itr != module->getProcesses()->end(); itr++) {
            if ((*itr)->getUhdmType() == uhdm::UhdmType::Always) {
              if (alwaysContainsForLoop(s, (*itr))) {
                more = true;
                module->getProcesses()->erase(itr);
                break;
              }
            }
          }
        }
      }
    }
  }
  std::queue<uhdm::Scope*> instances;
  if (d->getTopModules()) {
    for (auto mod : *d->getTopModules()) {
      instances.push(mod);
    }
  }
  while (!instances.empty()) {
    uhdm::Scope* current = instances.front();
    instances.pop();
    if (current->getUhdmType() == uhdm::UhdmType::Module) {
      uhdm::Module* mod = (uhdm::Module*)current;
      if (mod->getProcesses()) {
        bool more = true;
        while (more) {
          more = false;
          for (std::vector<uhdm::Process*>::iterator itr = mod->getProcesses()->begin();
               itr != mod->getProcesses()->end(); itr++) {
            if ((*itr)->getUhdmType() == uhdm::UhdmType::Always) {
              if (!alwaysContainsForLoop(s, (*itr))) {
                more = true;
                mod->getProcesses()->erase(itr);
                break;
              }
            }
          }
        }
      }
      if (mod->getModules()) {
        for (auto m : *mod->getModules()) {
          instances.push(m);
        }
      }
      if (mod->getGenScopeArrays()) {
        for (auto m : *mod->getGenScopeArrays()) {
          instances.push(m->getGenScopes()->at(0));
        }
      }
    } else if (current->getUhdmType() == uhdm::UhdmType::GenScope) {
      uhdm::GenScope* sc = (uhdm::GenScope*)current;
      if (sc->getModules()) {
        for (auto m : *sc->getModules()) {
          instances.push(m);
        }
      }
      if (sc->getGenScopeArrays()) {
        for (auto m : *sc->getGenScopeArrays()) {
          instances.push(m->getGenScopes()->at(0));
        }
      }
    }
  }
}

bool UhdmWriter::write(PathId uhdmFileId) {
  Compiler* const compiler = m_compileDesign->getCompiler();
  FileSystem* const fileSystem = m_session->getFileSystem();
  SymbolTable* const symbols = m_session->getSymbolTable();
  ErrorContainer* const errors = m_session->getErrorContainer();
  CommandLineParser* const clp = m_session->getCommandLineParser();
  ModportMap modPortMap;
  InstanceDefinitionMap instanceDefinitionMap;
  ModuleInstanceMap moduleInstanceMap;
  uhdm::Serializer& s = m_compileDesign->getSerializer();
  ExprBuilder exprBuilder(m_session);

  Location loc(uhdmFileId);
  Error err(ErrorDefinition::UHDM_CREATING_MODEL, loc);
  errors->addError(err);
  errors->printMessages(clp->muteStdout());

  // Compute list of design components that are part of the instance tree
  std::set<DesignComponent*> designComponents;
  {
    std::queue<ModuleInstance*> queue;
    for (const auto& pack : m_design->getPackageDefinitions()) {
      if (!pack.second->getFileContents().empty()) {
        if (pack.second->getFileContents()[0] != nullptr) designComponents.insert(pack.second);
      }
    }
    for (auto instance : m_design->getTopLevelModuleInstances()) {
      queue.push(instance);
    }

    while (!queue.empty()) {
      ModuleInstance* current = queue.front();
      DesignComponent* def = current->getDefinition();
      queue.pop();
      if (current == nullptr) continue;
      for (ModuleInstance* sub : current->getAllSubInstances()) {
        queue.push(sub);
      }
      const FileContent* fC = current->getFileContent();
      if (fC) {
        designComponents.insert(def);
      }
    }
  }

  std::vector<vpiHandle> designs;
  uhdm::Design* d = nullptr;
  if (m_design) {
    d = m_design->getUhdmDesign();
    vpiHandle designHandle = s.makeUhdmHandle(UhdmType::Design, d);
    std::string designName = "unnamed";
    const auto& topLevelModules = m_design->getTopLevelModuleInstances();
    if (!topLevelModules.empty()) {
      designName = topLevelModules.front()->getModuleName();
    }
    d->setName(designName);
    designs.emplace_back(designHandle);

    // -------------------------------
    // Non-Elaborated Model

    // Packages
    SURELOG::PackageDefinitionVec packages = m_design->getOrderedPackageDefinitions();
    for (auto& pack : m_design->getPackageDefinitions()) {
      if (pack.first == "builtin") {
        pack.second->getUhdmModel()->setParent(d);
        if (Typespec* const ts = pack.second->getUhdmTypespecModel()) {
          ts->setParent(d);
        }
        if (pack.second) packages.insert(packages.begin(), pack.second);
        break;
      }
    }

    for (Package* pack : packages) {
      if (!pack) continue;
      if (!pack->getFileContents().empty() && pack->getType() == VObjectType::paPackage_declaration) {
        const FileContent* fC = pack->getFileContents()[0];
        uhdm::Package* p = pack->getUhdmModel<uhdm::Package>();
        m_componentMap.emplace(pack, p);
        p->setName(pack->getName());
        p->setParent(d);
        p->setDefName(pack->getName());
        if (pack->getAttributes() != nullptr) {
          p->setAttributes(pack->getAttributes());
          for (auto a : *p->getAttributes()) {
            a->setParent(p);
          }
        }
        writePackage(pack, p, s);
        if (fC) {
          // Builtin package has no file
          const NodeId modId = pack->getNodeIds()[0];
          const NodeId startId = fC->sl_collect(modId, VObjectType::PACKAGE);
          fC->populateCoreMembers(startId, modId, p);
        }
      }
    }

    // Programs
    const auto& programs = m_design->getProgramDefinitions();
    for (const auto& progNamePair : programs) {
      Program* prog = progNamePair.second;
      if (!prog->getFileContents().empty() && prog->getType() == VObjectType::paProgram_declaration) {
        const FileContent* fC = prog->getFileContents()[0];
        uhdm::Program* p = prog->getUhdmModel<uhdm::Program>();
        m_componentMap.emplace(prog, p);
        instanceDefinitionMap.emplace(prog->getName(), p);
        p->setParent(d);
        prog->getUhdmTypespecModel()->setParent(d);
        p->setDefName(prog->getName());
        const NodeId modId = prog->getNodeIds()[0];
        const NodeId startId = fC->sl_collect(modId, VObjectType::PROGRAM);
        fC->populateCoreMembers(startId, modId, p);
        if (prog->getAttributes() != nullptr) {
          p->setAttributes(prog->getAttributes());
          for (auto a : *p->getAttributes()) {
            a->setParent(p);
          }
        }
        writeProgram(prog, p, s, modPortMap);
      }
    }

    // Interfaces
    const auto& modules = m_design->getModuleDefinitions();
    for (const auto& modNamePair : modules) {
      ModuleDefinition* mod = modNamePair.second;
      if (mod->getFileContents().empty()) {
        // Built-in primitive
      } else if (mod->getType() == VObjectType::paInterface_declaration) {
        const FileContent* fC = mod->getFileContents()[0];
        uhdm::Interface* m = mod->getUhdmModel<uhdm::Interface>();
        m_componentMap.emplace(mod, m);
        instanceDefinitionMap.emplace(mod->getName(), m);
        m->setParent(d);
        mod->getUhdmTypespecModel()->setParent(d);
        m->setDefName(mod->getName());
        const NodeId modId = mod->getNodeIds()[0];
        const NodeId startId = fC->sl_collect(modId, VObjectType::INTERFACE);
        fC->populateCoreMembers(startId, modId, m);
        if (mod->getAttributes() != nullptr) {
          m->setAttributes(mod->getAttributes());
          for (auto a : *m->getAttributes()) {
            a->setParent(m);
          }
        }
        writeInterface(mod, m, s, modPortMap);
      }
    }

    // Modules & Udps
    for (const auto& modNamePair : modules) {
      ModuleDefinition* mod = modNamePair.second;
      if (mod->getFileContents().empty()) {
        // Built-in primitive
      } else if (mod->getType() == VObjectType::paModule_declaration) {
        uhdm::Module* m = mod->getUhdmModel<uhdm::Module>();
        if (clp->getElabUhdm() && compiler->isLibraryFile(mod->getFileContents()[0]->getFileId())) {
          m->setCellInstance(true);
          // Don't list library cells unused in the design
          if (mod && (designComponents.find(mod) == designComponents.end())) continue;
        }
        m_componentMap.emplace(mod, m);
        std::string_view modName = mod->getName();
        instanceDefinitionMap.emplace(modName, m);
        m->setDefName(modName);
        uhdm::Typespec* mtps = mod->getUhdmTypespecModel();
        if (modName.find("::") == std::string_view::npos) {
          m->setParent(d);
          mtps->setParent(d);
          mod->getUhdmTypespecModel()->setParent(d);
        } else {
          modName = StringUtils::rtrim_until(modName, ':');
          modName.remove_suffix(1);
          InstanceDefinitionMap::const_iterator pmodIt = instanceDefinitionMap.find(modName);
          if (pmodIt == instanceDefinitionMap.end()) {
            m->setParent(d);
            mtps->setParent(d);
            mod->getUhdmTypespecModel()->setParent(d);
          } else {
            m->setParent(pmodIt->second);
            mtps->setParent(pmodIt->second);
            mod->getUhdmTypespecModel()->setParent(pmodIt->second);
          }
        }
        if (mod->getAttributes() != nullptr) {
          m->setAttributes(mod->getAttributes());
          for (auto a : *m->getAttributes()) {
            a->setParent(m);
          }
        }
        writeModule(mod, m, s, instanceDefinitionMap, modPortMap);
      } else if (mod->getType() == VObjectType::paUdp_declaration) {
        const FileContent* fC = mod->getFileContents()[0];
        if (uhdm::UdpDefn* defn = mod->getUhdmModel<uhdm::UdpDefn>()) {
          m_componentMap.emplace(mod, defn);
          defn->setParent(d);
          mod->getUhdmTypespecModel()->setParent(d);
          defn->setDefName(mod->getName());
          const NodeId modId = mod->getNodeIds()[0];
          const NodeId startId = fC->sl_collect(modId, VObjectType::PRIMITIVE);
          fC->populateCoreMembers(startId, modId, defn);
          if (mod->getAttributes() != nullptr) {
            defn->setAttributes(mod->getAttributes());
            for (auto a : *defn->getAttributes()) {
              a->setParent(defn);
            }
          }
        }
      }
    }

    if (uhdm::ModuleCollection* uhdm_modules = d->getAllModules()) {
      for (uhdm::Module* mod : *uhdm_modules) {
        if (mod->getRefModules()) {
          for (auto subMod : *mod->getRefModules()) {
            InstanceDefinitionMap::iterator itr = instanceDefinitionMap.find(std::string(subMod->getDefName()));
            if (itr != instanceDefinitionMap.end()) {
              subMod->setActual(itr->second);
            }
          }
        }
      }
    }

    // Classes
    const auto& classes = m_design->getClassDefinitions();
    for (const auto& classNamePair : classes) {
      ClassDefinition* classDef = classNamePair.second;
      if (!classDef->getFileContents().empty() && classDef->getType() == VObjectType::paClass_declaration) {
        uhdm::ClassDefn* c = classDef->getUhdmModel<uhdm::ClassDefn>();
        if (!c->getParent()) {
          writeClass(classDef, s, d);
        }
      }
    }
  }
  if (clp->getUhdmStats()) {
    s.printStats(std::cerr, "Non-Elaborated Model");
  }

  bind(s, designs);

  // Purge obsolete typespecs
  for (auto o : m_compileDesign->getSwapedObjects()) {
    const uhdm::Typespec* orig = o.first;
    const uhdm::Typespec* tps = o.second;
    if (tps != orig) s.erase(orig);
  }

  const fs::path uhdmFile = fileSystem->toPlatformAbsPath(uhdmFileId);
  if (clp->writeUhdm()) {
    Error err(ErrorDefinition::UHDM_WRITE_DB, loc);
    errors->addError(err);
    errors->printMessages(clp->muteStdout());
    s.setGCEnabled(clp->gc());
    s.save(uhdmFile);
  }

  if (IntegrityChecker* const checker = new IntegrityChecker(m_session)) {
    for (auto h : designs) {
      const uhdm::Design* const d = UhdmDesignFromVpiHandle(h);
      checker->check(d);
    }

    delete checker;
    errors->printMessages(clp->muteStdout());
  }

  // if (clp->getDebugUhdm() || clp->getCoverUhdm()) {
  //   // Check before restore
  //   Location loc(fileSystem->getCheckerHtmlFile(uhdmFileId, symbols));
  //   Error err(ErrorDefinition::UHDM_WRITE_HTML_COVERAGE, loc);
  //   errors->addError(err);
  //   errors->printMessages(clp->muteStdout());
  //
  //   if (UhdmChecker* uhdmchecker =
  //           new UhdmChecker(m_session, m_compileDesign, m_design)) {
  //     uhdmchecker->check(uhdmFileId);
  //     delete uhdmchecker;
  //   }
  // }

  if (clp->getDebugUhdm()) {
    Location loc(symbols->registerSymbol("in-memory uhdm"));
    Error err2(ErrorDefinition::UHDM_VISITOR, loc);
    errors->addError(err2);
    errors->printMessages(clp->muteStdout());
    std::cout << "====== UHDM =======\n";
    vpi_show_ids(clp->showVpiIds());
    visit_designs(designs, std::cout);
    std::cout << "===================\n";
  }
  errors->printMessages(clp->muteStdout());
  for (vpiHandle vh : designs) vpi_release_handle(vh);
  designs.clear();
  return true;
}
}  // namespace SURELOG
