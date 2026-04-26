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
              if (c->getValue() == "associative") {
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
              } else if (c->getValue() == "unsized") {
                array_ts->setArrayType(vpiDynamicArray);
              } else if (c->getValue() == "$") {
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
        ts->setParent(dest_port);
        dest_port->getTypespec()->setActual(ts);
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

void UhdmWriter::writeModPorts(ModuleDefinition* mod, uhdm::Serializer& s, ModportMap& modPortMap,
                               ModuleInstance* instance) {
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
}

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

bool UhdmWriter::write(PathId uhdmFileId) {
  FileSystem* const fileSystem = m_session->getFileSystem();
  SymbolTable* const symbols = m_session->getSymbolTable();
  ErrorContainer* const errors = m_session->getErrorContainer();
  CommandLineParser* const clp = m_session->getCommandLineParser();
  uhdm::Serializer& s = m_compileDesign->getSerializer();

  ModportMap modPortMap;
  InstanceDefinitionMap instanceDefinitionMap;
  ModuleInstanceMap moduleInstanceMap;

  Location loc(uhdmFileId);
  Error err(ErrorDefinition::UHDM_CREATING_MODEL, loc);
  errors->addError(err);
  errors->printMessages(clp->muteStdout());

  std::vector<vpiHandle> designs;
  uhdm::Design* d = nullptr;
  if (m_design) {
    d = m_design->getUhdmDesign();
    const uhdm::ScopedScope scopedScope(d);
    vpiHandle designHandle = s.makeUhdmHandle(UhdmType::Design, d);
    std::string designName = "unnamed";
    const auto& topLevelModules = m_design->getTopLevelModuleInstances();
    if (!topLevelModules.empty()) {
      designName = topLevelModules.front()->getModuleName();
    }
    d->setName(designName);
    designs.emplace_back(designHandle);

    // Packages
    SURELOG::PackageDefinitionVec packages = m_design->getOrderedPackageDefinitions();
    for (auto& pack : m_design->getPackageDefinitions()) {
      if ((pack.first == "builtin") && (pack.second != nullptr)) {
        if (uhdm::Package* const p = pack.second->getUhdmModel<uhdm::Package>()) {
          p->setDefName(pack.second->getName());
          p->setParent(d);
          m_componentMap.emplace(pack.second, p);
        }
        break;
      }
    }

    for (Package* pack : packages) {
      if (uhdm::Package* const p = pack->getUhdmModel<uhdm::Package>()) {
        p->setDefName(pack->getName());
        p->setParent(d);
        m_componentMap.emplace(pack, p);

        SignalBaseClassMap signalBaseMap;
        SignalMap portMap;
        SignalMap netMap;
        const std::vector<Signal*>& orig_nets = pack->getSignals();
        writeNets(pack, orig_nets, p, s, signalBaseMap, netMap, portMap, nullptr);

        // if (uhdm::AttributeCollection* const collection = pack->getAttributes()) {
        //   for (uhdm::Attribute* a : *collection) {
        //     a->setParent(p);
        //     p->getAttributes(true)->emplace_back(a);  // TODO(HS): Fix the parenting issue
        //   }
        // }
      }
    }

    const auto& programs = m_design->getProgramDefinitions();
    for (const auto& [name, program] : programs) {
      if (uhdm::Program* const p = program->getUhdmModel<uhdm::Program>()) {
        p->setDefName(program->getName());
        p->setParent(d);
        m_componentMap.emplace(program, p);

        SignalBaseClassMap signalBaseMap;
        SignalMap portMap;
        SignalMap netMap;

        const std::vector<Signal*>& orig_ports = program->getPorts();
        writePorts(orig_ports, p, s, modPortMap, signalBaseMap, portMap, nullptr, program);
        mapLowConns(orig_ports, s, signalBaseMap);

        const std::vector<Signal*>& orig_nets = program->getSignals();
        writeNets(program, orig_nets, p, s, signalBaseMap, netMap, portMap, nullptr);

        // if (uhdm::AttributeCollection* const collection = program->getAttributes()) {
        //   for (uhdm::Attribute* a : *collection) {
        //     a->setParent(p);
        //     p->getAttributes(true)->emplace_back(a);  // TODO(HS): Fix the parenting issue
        //   }
        // }
      }
      if (uhdm::Typespec* const t = program->getUhdmTypespecModel()) {
        t->setParent(d);
      }
    }

    // Interfaces, Modules & Udps
    const auto& modules = m_design->getModuleDefinitions();
    for (const auto& [name, md] : modules) {
      if (!md->getFileContents().empty()) {
        m_componentMap.emplace(md, md->getUhdmModel());
        instanceDefinitionMap.emplace(md->getName(), md->getUhdmModel<uhdm::Instance>());
      }
    }

    for (const auto& [name, md] : modules) {
      if (md->getFileContents().empty()) {
        // Built-in primitive
      } else {
        Any* parent = d;
        std::string_view modName = md->getName();
        if (modName.find("::") != std::string_view::npos) {
          modName = StringUtils::rtrim_until(modName, ':');
          modName.remove_suffix(1);
          InstanceDefinitionMap::const_iterator pmodIt = instanceDefinitionMap.find(modName);
          if (pmodIt != instanceDefinitionMap.cend()) {
            parent = pmodIt->second;
          }
        }

        if (md->getType() == VObjectType::paModule_declaration) {
          if (uhdm::Module* const m = md->getUhdmModel<uhdm::Module>()) {
            m->setParent(parent);
            m->setDefName(md->getName());
            m->setParent(d);

            const uhdm::ScopedScope scopedScope(m);
            SignalBaseClassMap signalBaseMap;
            SignalMap portMap;
            SignalMap netMap;

            const std::vector<Signal*>& orig_ports = md->getPorts();
            writePorts(orig_ports, m, s, modPortMap, signalBaseMap, portMap, nullptr, md);
            mapLowConns(orig_ports, s, signalBaseMap);

            // if (uhdm::AttributeCollection* const collection = md->getAttributes()) {
            //   for (uhdm::Attribute* a : *collection) {
            //     a->setParent(m);
            //     m->getAttributes(true)->emplace_back(a);  // TODO(HS): Fix the parenting issue
            //   }
            // }
          }
          if (uhdm::Typespec* const t = md->getUhdmTypespecModel()) {
            t->setParent(parent);
          }
        } else if (md->getType() == VObjectType::paInterface_declaration) {
          if (uhdm::Interface* const i = md->getUhdmModel<uhdm::Interface>()) {
            i->setDefName(md->getName());
            i->setParent(d);

            const uhdm::ScopedScope scopedScope(i);
            SignalBaseClassMap signalBaseMap;
            SignalMap portMap;
            SignalMap netMap;

            const std::vector<Signal*>& orig_ports = md->getPorts();
            writePorts(orig_ports, i, s, modPortMap, signalBaseMap, portMap, nullptr, md);
            mapLowConns(orig_ports, s, signalBaseMap);

            const std::vector<Signal*>& orig_nets = md->getSignals();
            writeNets(md, orig_nets, i, s, signalBaseMap, netMap, portMap, nullptr);
            mapLowConns(orig_ports, s, signalBaseMap);

            writeModPorts(md, s, modPortMap, nullptr);

            // if (uhdm::AttributeCollection* const collection = md->getAttributes()) {
            //   for (uhdm::Attribute* a : *collection) {
            //     a->setParent(i);
            //     i->getAttributes(true)->emplace_back(a);  // TODO(HS): Fix the parenting issue
            //   }
            // }
          }
          if (uhdm::Typespec* const t = md->getUhdmTypespecModel()) {
            t->setParent(d);
          }
        } else if (md->getType() == VObjectType::paUdp_declaration) {
          if (uhdm::UdpDefn* const ud = md->getUhdmModel<uhdm::UdpDefn>()) {
            ud->setDefName(md->getName());
            ud->setParent(d);

            const uhdm::ScopedScope scopedScope(ud);
            SignalBaseClassMap signalBaseMap;
            SignalMap portMap;
            SignalMap netMap;

            const std::vector<Signal*>& orig_ports = md->getPorts();
            writePorts(orig_ports, ud, s, modPortMap, signalBaseMap, portMap, nullptr, md);
            const std::vector<Signal*>& orig_nets = md->getSignals();
            writeNets(md, orig_nets, ud, s, signalBaseMap, netMap, portMap, nullptr);
            mapLowConns(orig_ports, s, signalBaseMap);

            // if (uhdm::AttributeCollection* const collection = md->getAttributes()) {
            //   for (uhdm::Attribute* a : *collection) {
            //     a->setParent(ud);
            //     ud->getAttributes(true)->emplace_back(a);  // TODO(HS): Fix the parenting issue
            //   }
            // }
          }
          if (uhdm::Typespec* const t = md->getUhdmTypespecModel()) {
            t->setParent(d);
          }
        }
      }
    }

    const auto& classes = m_design->getClassDefinitions();
    for (const auto& [name, definition] : classes) {
      if (uhdm::ClassDefn* const cd = definition->getUhdmModel<uhdm::ClassDefn>()) {
        cd->setParent(d);
        m_componentMap.emplace(definition, cd);

        // if (uhdm::AttributeCollection* const collection = definition->getAttributes()) {
        //   for (uhdm::Attribute* a : *collection) {
        //     a->setParent(cd);
        //     cd->getAttributes(true)->emplace_back(a);  // TODO(HS): Fix the parenting issue
        //   }
        // }
      }
      if (uhdm::Typespec* const t = definition->getUhdmTypespecModel()) {
        t->setParent(d);
      }
    }
  }
  if (clp->getUhdmStats()) {
    s.printStats(std::cerr, "Non-Elaborated Model");
  }

  bind(s, designs);

  if (clp->writeUhdm()) {
    if (!fileSystem->canResolveToPlatformPath(uhdmFileId)) {
      Error err(ErrorDefinition::CMD_CANNOT_OPEN_FILE_FOR_WRITE, loc);
      errors->addError(err);
      errors->printMessages(clp->muteStdout());
      return false;
    }

    const fs::path uhdmFile = fileSystem->toPlatformAbsPath(uhdmFileId);
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
