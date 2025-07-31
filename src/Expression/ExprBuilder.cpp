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
 * File:   ExprBuilder.cpp
 * Author: alain
 *
 * Created on November 2, 2017, 9:45 PM
 */
#include "Surelog/Expression/ExprBuilder.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Surelog/Design/Design.h"
#include "Surelog/Design/FileContent.h"
#include "Surelog/ErrorReporting/Error.h"
#include "Surelog/ErrorReporting/ErrorContainer.h"
#include "Surelog/ErrorReporting/ErrorDefinition.h"
#include "Surelog/ErrorReporting/Location.h"
#include "Surelog/Expression/Value.h"
#include "Surelog/Package/Package.h"
#include "Surelog/SourceCompile/SymbolTable.h"
#include "Surelog/SourceCompile/VObjectTypes.h"
#include "Surelog/Utils/NumUtils.h"
#include "Surelog/Utils/StringUtils.h"

#if defined(_MSC_VER)
#define strcasecmp _stricmp
#define strdup _strdup
#else
#include <strings.h>
#endif

namespace SURELOG {

Value* ExprBuilder::clone(Value* val) {
  Value* clone = nullptr;
  if (StValue* v = value_cast<StValue*>(val)) {
    clone = m_valueFactory.newValue(*v);
  } else if (LValue* v = value_cast<LValue*>(val)) {
    clone = m_valueFactory.newValue(*v);
  } else if (SValue* v = value_cast<SValue*>(val)) {
    clone = m_valueFactory.newValue(*v);
  }
  return clone;
}

// Often, there are assignments to muteErrors here, that are never read.
// It seems like there is a (future?) intention here. So for now, disable
// warnings from clang-tidy.
// TODO(Alain): remove N0LINTBEGIN, run .github/bin/run-clang-tidy.sh
//              and fix the intended places.
//
// NOLINTBEGIN(*.DeadStores)
Value* ExprBuilder::evalExpr(const FileContent* fC, NodeId parent,
                             ValuedComponentI* instance, bool muteErrors) {
  Value* value = m_valueFactory.newLValue();
  NodeId child = fC->Child(parent);
  VObjectType type = fC->Type(parent);
  switch (type) {
    case VObjectType::paPackage_scope: {
      Value* sval = nullptr;
      const std::string_view packageName = fC->SymName(child);
      const std::string_view name = fC->SymName(fC->Sibling(parent));
      if (m_design) {
        Package* pack = m_design->getPackage(packageName);
        if (pack) {
          if (pack->getComplexValue(name)) {
            muteErrors = true;
            value->setInvalid();
            break;
          } else {
            sval = pack->getValue(name);
          }
        }
      }
      std::string fullName;
      if (sval == nullptr) fullName = StrCat(packageName, "::", name);
      if (sval == nullptr) {
        if (muteErrors == false) {
          Location loc(fC->getFileId(parent), fC->Line(parent),
                       fC->Column(parent), m_symbols->registerSymbol(fullName));
          Error err(ErrorDefinition::ELAB_UNDEF_VARIABLE, loc);
          m_errors->addError(err);
        }
        value->setInvalid();
        return value;
      }
      if (sval->getType() == Value::Type::String ||
          sval->getType() == Value::Type::Hexadecimal) {
        m_valueFactory.deleteValue(value);
        value = clone(sval);
      } else {
        value->u_plus(sval);
      }
      return value;
    }
    default:
      break;
  }

  if (child) {
    VObjectType childType = fC->Type(child);
    switch (childType) {
      case VObjectType::paIncDec_PlusPlus: {
        // Pre increment
        NodeId sibling = fC->Sibling(child);
        Value* tmp = evalExpr(fC, sibling, instance, muteErrors);
        value->u_plus(tmp);
        value->incr();
        m_valueFactory.deleteValue(tmp);
        break;
      }
      case VObjectType::paIncDec_MinusMinus: {
        // Pre decrement
        NodeId sibling = fC->Sibling(child);
        Value* tmp = evalExpr(fC, sibling, instance, muteErrors);
        value->u_plus(tmp);
        value->decr();
        m_valueFactory.deleteValue(tmp);
        break;
      }
      case VObjectType::paUnary_Minus: {
        NodeId sibling = fC->Sibling(child);
        Value* tmp = evalExpr(fC, sibling, instance, muteErrors);
        value->u_minus(tmp);
        m_valueFactory.deleteValue(tmp);
        break;
      }
      case VObjectType::paUnary_Plus: {
        NodeId sibling = fC->Sibling(child);
        Value* tmp = evalExpr(fC, sibling, instance, muteErrors);
        value->u_plus(tmp);
        m_valueFactory.deleteValue(tmp);
        break;
      }
      case VObjectType::paUnary_Not: {
        NodeId sibling = fC->Sibling(child);
        Value* tmp = evalExpr(fC, sibling, instance, muteErrors);
        value->u_not(tmp);
        m_valueFactory.deleteValue(tmp);
        break;
      }
      case VObjectType::paUnary_Tilda: {
        NodeId sibling = fC->Sibling(child);
        Value* tmp = evalExpr(fC, sibling, instance, muteErrors);
        value->u_tilda(tmp);
        m_valueFactory.deleteValue(tmp);
        break;
      }
      case VObjectType::paUnary_BitwAnd: {
        NodeId sibling = fC->Sibling(child);
        Value* tmp = evalExpr(fC, sibling, instance, muteErrors);
        value->u_bitwAnd(tmp);
        m_valueFactory.deleteValue(tmp);
        break;
      }
      case VObjectType::paUnary_BitwOr: {
        NodeId sibling = fC->Sibling(child);
        Value* tmp = evalExpr(fC, sibling, instance, muteErrors);
        value->u_bitwOr(tmp);
        m_valueFactory.deleteValue(tmp);
        break;
      }
      case VObjectType::paUnary_BitwXor: {
        NodeId sibling = fC->Sibling(child);
        Value* tmp = evalExpr(fC, sibling, instance, muteErrors);
        value->u_bitwXor(tmp);
        m_valueFactory.deleteValue(tmp);
        break;
      }
      case VObjectType::paConstant_primary:
        m_valueFactory.deleteValue(value);
        value = evalExpr(fC, child, instance, muteErrors);
        break;
      case VObjectType::paPrimary_literal:
        m_valueFactory.deleteValue(value);
        value = evalExpr(fC, child, instance, muteErrors);
        break;
      case VObjectType::paPrimary:
        m_valueFactory.deleteValue(value);
        value = evalExpr(fC, child, instance, muteErrors);
        break;
      case VObjectType::paUnpacked_dimension:
        // Only works for the case of constant_expression, not range
        m_valueFactory.deleteValue(value);
        value = evalExpr(fC, child, instance, muteErrors);
        break;
      case VObjectType::paInc_or_dec_expression:
        m_valueFactory.deleteValue(value);
        value = evalExpr(fC, child, instance, muteErrors);
        break;
      case VObjectType::paConstant_mintypmax_expression:
        m_valueFactory.deleteValue(value);
        value = evalExpr(fC, child, instance, muteErrors);
        break;
      case VObjectType::paMintypmax_expression:
        m_valueFactory.deleteValue(value);
        value = evalExpr(fC, child, instance, muteErrors);
        break;
      case VObjectType::paParam_expression:
        m_valueFactory.deleteValue(value);
        value = evalExpr(fC, child, instance, muteErrors);
        break;
      case VObjectType::paHierarchical_identifier: {
        m_valueFactory.deleteValue(value);
        value = evalExpr(fC, child, instance, muteErrors);
        break;
      }
      case VObjectType::paExpression:
      case VObjectType::paConstant_expression: {
        Value* valueL = evalExpr(fC, child, instance, muteErrors);
        NodeId op = fC->Sibling(child);
        if (!op) {
          m_valueFactory.deleteValue(value);
          value = valueL;
          break;
        }
        VObjectType opType = fC->Type(op);
        switch (opType) {
          case VObjectType::paBinOp_Plus: {
            NodeId rval = fC->Sibling(op);
            Value* valueR = evalExpr(fC, rval, instance, muteErrors);
            value->plus(valueL, valueR);
            m_valueFactory.deleteValue(valueL);
            m_valueFactory.deleteValue(valueR);
            break;
          }
          case VObjectType::paBinOp_Minus: {
            NodeId rval = fC->Sibling(op);
            Value* valueR = evalExpr(fC, rval, instance, muteErrors);
            value->minus(valueL, valueR);
            m_valueFactory.deleteValue(valueL);
            m_valueFactory.deleteValue(valueR);
            break;
          }
          case VObjectType::paBinOp_Mult: {
            NodeId rval = fC->Sibling(op);
            Value* valueR = evalExpr(fC, rval, instance, muteErrors);
            value->mult(valueL, valueR);
            m_valueFactory.deleteValue(valueL);
            m_valueFactory.deleteValue(valueR);
            break;
          }
          case VObjectType::paBinOp_MultMult: {
            NodeId rval = fC->Sibling(op);
            Value* valueR = evalExpr(fC, rval, instance, muteErrors);
            value->power(valueL, valueR);
            m_valueFactory.deleteValue(valueL);
            m_valueFactory.deleteValue(valueR);
            break;
          }
          case VObjectType::paQMARK:
          case VObjectType::paConditional_operator: {
            int64_t v = valueL->getValueL();
            m_valueFactory.deleteValue(valueL);
            NodeId Expression = fC->Sibling(op);
            NodeId ConstantExpr = fC->Sibling(Expression);
            if (v) {
              value = evalExpr(fC, Expression, instance, muteErrors);
            } else {
              value = evalExpr(fC, ConstantExpr, instance, muteErrors);
            }
            break;
          }
          case VObjectType::paBinOp_Div: {
            NodeId rval = fC->Sibling(op);
            Value* valueR = evalExpr(fC, rval, instance, muteErrors);
            value->div(valueL, valueR);
            m_valueFactory.deleteValue(valueL);
            m_valueFactory.deleteValue(valueR);
            break;
          }
          case VObjectType::paBinOp_Great: {
            NodeId rval = fC->Sibling(op);
            Value* valueR = evalExpr(fC, rval, instance, muteErrors);
            value->greater(valueL, valueR);
            m_valueFactory.deleteValue(valueL);
            m_valueFactory.deleteValue(valueR);
            break;
          }
          case VObjectType::paBinOp_GreatEqual: {
            NodeId rval = fC->Sibling(op);
            Value* valueR = evalExpr(fC, rval, instance, muteErrors);
            value->greater_equal(valueL, valueR);
            m_valueFactory.deleteValue(valueL);
            m_valueFactory.deleteValue(valueR);
            break;
          }
          case VObjectType::paBinOp_Less: {
            NodeId rval = fC->Sibling(op);
            Value* valueR = evalExpr(fC, rval, instance, muteErrors);
            value->lesser(valueL, valueR);
            m_valueFactory.deleteValue(valueL);
            m_valueFactory.deleteValue(valueR);
            break;
          }
          case VObjectType::paBinOp_LessEqual: {
            NodeId rval = fC->Sibling(op);
            Value* valueR = evalExpr(fC, rval, instance, muteErrors);
            value->lesser_equal(valueL, valueR);
            m_valueFactory.deleteValue(valueL);
            m_valueFactory.deleteValue(valueR);
            break;
          }
          case VObjectType::paBinOp_Equiv: {
            NodeId rval = fC->Sibling(op);
            Value* valueR = evalExpr(fC, rval, instance, muteErrors);
            if ((valueL->getType() == Value::Type::String) &&
                (valueR->getType() == Value::Type::String)) {
              m_valueFactory.deleteValue(value);
              value = m_valueFactory.newStValue();
            }
            value->equiv(valueL, valueR);
            m_valueFactory.deleteValue(valueL);
            m_valueFactory.deleteValue(valueR);
            break;
          }
          case VObjectType::paBinOp_Not: {
            NodeId rval = fC->Sibling(op);
            Value* valueR = evalExpr(fC, rval, instance, muteErrors);
            if ((valueL->getType() == Value::Type::String) &&
                (valueR->getType() == Value::Type::String)) {
              m_valueFactory.deleteValue(value);
              value = m_valueFactory.newStValue();
            }
            value->notEqual(valueL, valueR);
            m_valueFactory.deleteValue(valueL);
            m_valueFactory.deleteValue(valueR);
            break;
          }
          case VObjectType::paBinOp_Percent: {
            NodeId rval = fC->Sibling(op);
            Value* valueR = evalExpr(fC, rval, instance, muteErrors);
            value->mod(valueL, valueR);
            m_valueFactory.deleteValue(valueL);
            m_valueFactory.deleteValue(valueR);
            break;
          }
          case VObjectType::paBinOp_LogicAnd: {
            NodeId rval = fC->Sibling(op);
            Value* valueR = evalExpr(fC, rval, instance, muteErrors);
            value->logAnd(valueL, valueR);
            m_valueFactory.deleteValue(valueL);
            m_valueFactory.deleteValue(valueR);
            break;
          }
          case VObjectType::paBinOp_LogicOr: {
            NodeId rval = fC->Sibling(op);
            Value* valueR = evalExpr(fC, rval, instance, muteErrors);
            value->logOr(valueL, valueR);
            m_valueFactory.deleteValue(valueL);
            m_valueFactory.deleteValue(valueR);
            break;
          }
          case VObjectType::paBinOp_BitwAnd: {
            NodeId rval = fC->Sibling(op);
            Value* valueR = evalExpr(fC, rval, instance, muteErrors);
            value->bitwAnd(valueL, valueR);
            m_valueFactory.deleteValue(valueL);
            m_valueFactory.deleteValue(valueR);
            break;
          }
          case VObjectType::paBinOp_BitwOr: {
            NodeId rval = fC->Sibling(op);
            Value* valueR = evalExpr(fC, rval, instance, muteErrors);
            value->bitwOr(valueL, valueR);
            m_valueFactory.deleteValue(valueL);
            m_valueFactory.deleteValue(valueR);
            break;
          }
          case VObjectType::paBinOp_BitwXor: {
            NodeId rval = fC->Sibling(op);
            Value* valueR = evalExpr(fC, rval, instance, muteErrors);
            value->bitwXor(valueL, valueR);
            m_valueFactory.deleteValue(valueL);
            m_valueFactory.deleteValue(valueR);
            break;
          }
          case VObjectType::paBinOp_ShiftLeft: {
            NodeId rval = fC->Sibling(op);
            Value* valueR = evalExpr(fC, rval, instance, muteErrors);
            value->shiftLeft(valueL, valueR);
            m_valueFactory.deleteValue(valueL);
            m_valueFactory.deleteValue(valueR);
            break;
          }
          case VObjectType::paBinOp_ShiftRight: {
            NodeId rval = fC->Sibling(op);
            Value* valueR = evalExpr(fC, rval, instance, muteErrors);
            value->shiftRight(valueL, valueR);
            m_valueFactory.deleteValue(valueL);
            m_valueFactory.deleteValue(valueR);
            break;
          }
          default:
            m_valueFactory.deleteValue(value);
            value = valueL;
            break;
        }
      } break;
      case VObjectType::slIntConst: {
        const std::string_view val = fC->SymName(child);
        const std::string_view size = StringUtils::rtrim_until(val, '\'');
        int64_t intsize = 0;
        if (NumUtils::parseInt64(size, &intsize) == nullptr) {
          intsize = 0;
        }
        if (val.find('\'') != std::string::npos) {
          uint64_t hex_value = 0;
          char base = 'h';
          uint32_t i = 0;
          for (i = 0; i < val.size(); i++) {
            if (val[i] == '\'') {
              base = val[i + 1];
              if (base == 's' || base == 'S') base = val[i + 2];
              break;
            }
          }
          std::string v;
          if (val.find_first_of("sS") != std::string::npos) {
            v = val.substr(i + 3);
          } else {
            v = val.substr(i + 2);
          }
          v = StringUtils::replaceAll(v, "_", "");
          bool intformat = false;
          switch (base) {
            case 'h':
            case 'H': {
              if (intsize > 64) {
                m_valueFactory.deleteValue(value);
                StValue* stval = (StValue*)m_valueFactory.newStValue();
                stval->set(v, Value::Type::Hexadecimal, intsize);
                value = stval;
              } else {
                intformat = NumUtils::parseHex(v, &hex_value) != nullptr;
              }
              break;
            }
            case 'b':
            case 'B':
              if (v.find_first_of("xzXZ") != std::string::npos) {
                StValue* stval = (StValue*)m_valueFactory.newStValue();
                stval->set(v, Value::Type::Binary,
                           (intsize ? intsize : v.size()));
                value = stval;
              } else {
                intformat = NumUtils::parseBinary(v, &hex_value) != nullptr;
              }
              break;
            case 'o':
            case 'O':
              intformat = NumUtils::parseOctal(v, &hex_value) != nullptr;
              break;
            case 'd':
            case 'D':
              intformat = NumUtils::parseUint64(v, &hex_value) != nullptr;
              break;
            default:
              // '1
              intformat = NumUtils::parseBinary(v, &hex_value) != nullptr;
              break;
          }
          if (intformat) {
            if (size.empty())
              value->set(hex_value, Value::Type::Integer, 0);
            else
              value->set(hex_value, Value::Type::Unsigned, intsize);
          }
        } else if (!val.empty()) {
          if (val[0] == '-') {
            int64_t i = 0;
            if (NumUtils::parseInt64(val, &i) == nullptr) {
              i = 0;
            }
            value->set(i);
          } else {
            uint64_t u = 0;
            if (NumUtils::parseUint64(val, &u) == nullptr) {
              u = 0;
            }
            value->set(u);
          }
        }
        break;
      }
      case VObjectType::slRealConst: {
        double d;
        value->set(NumUtils::parseDouble(fC->SymName(child), &d) ? d : 0);
        break;
      }
      case VObjectType::paNull_keyword: {
        value->set((uint64_t)0);
        break;
      }
      case VObjectType::paPackage_scope:
      case VObjectType::slStringConst: {
        Value* sval = nullptr;
        std::string fullName;
        if (childType == VObjectType::paPackage_scope) {
          const std::string_view packageName = fC->SymName(fC->Child(child));
          const std::string_view name = fC->SymName(fC->Sibling(child));
          if (m_design) {
            Package* pack = m_design->getPackage(packageName);
            if (pack) {
              if (pack->getComplexValue(name)) {
                muteErrors = true;
                value->setInvalid();
                break;
              } else {
                sval = pack->getValue(name);
              }
            }
          }
          if (sval == nullptr) fullName = StrCat(packageName, "::", name);
        } else {
          const std::string_view name = fC->SymName(child);
          if (instance) {
            if (instance->getComplexValue(name)) {
              muteErrors = true;
              value->setInvalid();
              break;
            } else {
              sval = instance->getValue(name, *this);
            }
          }
          if (sval == nullptr) fullName = name;
        }

        if (sval == nullptr) {
          if (muteErrors == false) {
            Location loc(fC->getFileId(child), fC->Line(child),
                         fC->Column(child),
                         m_symbols->registerSymbol(fullName));
            Error err(ErrorDefinition::ELAB_UNDEF_VARIABLE, loc);
            m_errors->addError(err);
          }
          value->setInvalid();
          break;
        }
        if (sval->getType() == Value::Type::String ||
            sval->getType() == Value::Type::Hexadecimal) {
          m_valueFactory.deleteValue(value);
          value = clone(sval);
        } else {
          value->u_plus(sval);
        }
        break;
      }
      case VObjectType::slStringLiteral: {
        m_valueFactory.deleteValue(value);
        value = m_valueFactory.newStValue();
        const std::string_view name = StringUtils::unquoted(fC->SymName(child));
        value->set(name);
        break;
      }
      case VObjectType::paNumber_1Tickb0:
      case VObjectType::paNumber_1TickB0:
      case VObjectType::paInitVal_1Tickb0:
      case VObjectType::paInitVal_1TickB0:
      case VObjectType::paScalar_1Tickb0:
      case VObjectType::paScalar_1TickB0: {
        value->set(0, Value::Type::Scalar, 1);
        break;
      }
      case VObjectType::paNumber_Tickb0:
      case VObjectType::paNumber_TickB0:
      case VObjectType::paNumber_Tick0:
      case VObjectType::paScalar_Tickb0:
      case VObjectType::paScalar_TickB0:
      case VObjectType::pa0: {
        value->set(0, Value::Type::Scalar, 0);
        break;
      }
      case VObjectType::paNumber_1Tickb1:
      case VObjectType::paNumber_1TickB1:
      case VObjectType::paInitVal_1Tickb1:
      case VObjectType::paInitVal_1TickB1:
      case VObjectType::paScalar_1Tickb1:
      case VObjectType::paScalar_1TickB1: {
        value->set(1, Value::Type::Scalar, 1);
        break;
      }
      case VObjectType::paNumber_Tickb1:
      case VObjectType::paNumber_TickB1:
      case VObjectType::paNumber_Tick1:
      case VObjectType::paScalar_Tickb1:
      case VObjectType::paScalar_TickB1:
      case VObjectType::pa1: {
        value->set(1, Value::Type::Scalar, 0);
        break;
      }
      case VObjectType::paVariable_lvalue: {
        Value* variableVal = evalExpr(fC, child, instance, muteErrors);
        NodeId sibling = fC->Sibling(child);
        if (sibling) {
          VObjectType opType = fC->Type(sibling);
          if (opType == VObjectType::paIncDec_PlusPlus)
            variableVal->incr();
          else if (opType == VObjectType::paIncDec_MinusMinus)
            variableVal->decr();
        }
        m_valueFactory.deleteValue(value);
        value = variableVal;
        break;
      }
      case VObjectType::paSubroutine_call: {
        NodeId dollar = fC->Child(child);
        NodeId function = fC->Sibling(dollar);
        NodeId List_of_arguments = fC->Sibling(function);
        NodeId Expression = fC->Child(List_of_arguments);
        std::vector<Value*> args;
        while (Expression) {
          args.push_back(evalExpr(fC, Expression, instance, muteErrors));
          Expression = fC->Sibling(Expression);
        }
        const std::string_view funcName = fC->SymName(function);
        if (funcName == "clog2") {
          int32_t val = args[0]->getValueL();
          val = val - 1;
          if (val < 0) {
            value->set((int64_t)0);
            value->setInvalid();
            break;
          }
          int32_t clog2 = 0;
          for (; val > 0; clog2 = clog2 + 1) {
            val = val >> 1;
          }
          value->set((int64_t)clog2);
        } else if (funcName == "ln") {
          int32_t val = args[0]->getValueL();
          value->set((int64_t)std::log(val));
        } else if (funcName == "clog") {
          int32_t val = args[0]->getValueL();
          value->set((int64_t)std::log10(val));
        } else if (funcName == "exp") {
          int32_t val = args[0]->getValueL();
          value->set((int64_t)std::exp2(val));
        } else if (funcName == "bits") {
          // $bits is implemented in compileExpression.cpp
          value->set((int64_t)0);
          value->setInvalid();
        } else {
          value->set((int64_t)0);
          value->setInvalid();
        }
        break;
      }
      case VObjectType::paConstant_concatenation: {
        NodeId Constant_expression = fC->Child(child);
        char base = 'h';
        std::string svalue;
        while (Constant_expression) {
          NodeId Constant_primary = fC->Child(Constant_expression);
          NodeId Primary_literal = fC->Child(Constant_primary);
          NodeId ConstVal = fC->Child(Primary_literal);
          std::string token;
          if (fC->Type(ConstVal) == VObjectType::slIntConst) {
            token = fC->SymName(ConstVal);
          } else {
            Value* constVal =
                evalExpr(fC, Primary_literal, instance, muteErrors);
            uint64_t v = constVal->getValueUL();
            token = NumUtils::toBinary(constVal->getSize(), v);
          }
          if (token.find('\'') != std::string::npos) {
            uint32_t i = 0;
            for (i = 0; i < token.size(); i++) {
              if (token[i] == '\'') {
                base = token[i + 1];
                if (base == 's' || base == 'S') base = token[i + 2];
                break;
              }
            }
            std::string v;
            if (token.find_first_of("sS") != std::string::npos) {
              v = token.substr(i + 3);
            } else {
              v = token.substr(i + 2);
            }
            v = StringUtils::replaceAll(v, "_", "");
            std::string size = token.substr(0, i);
            uint64_t isize = 0;
            if (NumUtils::parseUint64(size, &isize) == nullptr) {
              isize = 0;
            }
            if (base == 'd') {
              uint64_t iv = 0;
              if (NumUtils::parseUint64(v, &iv) == nullptr) {
                iv = 0;
              }
              v = NumUtils::toBinary(isize, iv);
            } else if (base == 'h') {
              uint64_t iv = 0;
              if (NumUtils::parseHex(v, &iv) == nullptr) {
                iv = 0;
              }
              v = NumUtils::toBinary(isize, iv);
            } else if (base == 'o') {
              uint64_t iv = 0;
              if (NumUtils::parseOctal(v, &iv) == nullptr) {
                iv = 0;
              }
              v = NumUtils::toBinary(isize, iv);
            }
            svalue += v;
          } else {
            svalue += token;
            base = 'b';
          }
          Constant_expression = fC->Sibling(Constant_expression);
        }
        base = 'b';
        if (svalue.empty()) {
          value->set((int64_t)0);
        } else {
          m_valueFactory.deleteValue(value);
          value = m_valueFactory.newStValue();
          value->set(svalue, Value::Type::Binary);
        }
        value->setInvalid();  // We can't distinguish in between concatenation
                              // or array initialization in this context
        // so we mark the value as invalid for most purposes. Enum value can
        // still use it as concatenation
        break;
      }
      default:
        value->set((int64_t)0);
        value->setInvalid();
        break;
    }
  } else {
    VObjectType type = fC->Type(parent);
    switch (type) {
      case VObjectType::slStringConst: {
        Value* sval = nullptr;
        std::string fullName;
        const std::string_view name = fC->SymName(parent);
        if (instance) {
          if (instance->getComplexValue(name)) {
            muteErrors = true;
            value->setInvalid();
            break;
          } else {
            sval = instance->getValue(name, *this);
          }
        }
        if (sval == nullptr) fullName = name;

        if (sval == nullptr) {
          if (muteErrors == false) {
            Location loc(fC->getFileId(parent), fC->Line(parent),
                         fC->Column(parent),
                         m_symbols->registerSymbol(fullName));
            Error err(ErrorDefinition::ELAB_UNDEF_VARIABLE, loc);
            m_errors->addError(err);
          }
          value->setInvalid();
          break;
        }
        NodeId op = fC->Sibling(parent);
        VObjectType op_type = fC->Type(op);
        switch (op_type) {
          case VObjectType::paIncDec_PlusPlus:
            value->u_plus(sval);
            value->incr();
            return value;
          case VObjectType::paIncDec_MinusMinus:
            value->u_plus(sval);
            value->decr();
            return value;
          case VObjectType::paAssignOp_Add: {
            NodeId rval = fC->Sibling(op);
            Value* valueR = evalExpr(fC, rval, instance, muteErrors);
            value->plus(sval, valueR);
            m_valueFactory.deleteValue(valueR);
            return value;
          }
          case VObjectType::paAssignOp_Mult: {
            NodeId rval = fC->Sibling(op);
            Value* valueR = evalExpr(fC, rval, instance, muteErrors);
            value->mult(sval, valueR);
            m_valueFactory.deleteValue(valueR);
            return value;
          }
          case VObjectType::paAssignOp_Sub: {
            NodeId rval = fC->Sibling(op);
            Value* valueR = evalExpr(fC, rval, instance, muteErrors);
            value->minus(sval, valueR);
            m_valueFactory.deleteValue(valueR);
            return value;
          }
          case VObjectType::paAssignOp_Div: {
            NodeId rval = fC->Sibling(op);
            Value* valueR = evalExpr(fC, rval, instance, muteErrors);
            value->div(sval, valueR);
            m_valueFactory.deleteValue(valueR);
            return value;
          }
          case VObjectType::paAssignOp_Modulo: {
            NodeId rval = fC->Sibling(op);
            Value* valueR = evalExpr(fC, rval, instance, muteErrors);
            value->mod(sval, valueR);
            m_valueFactory.deleteValue(valueR);
            return value;
          }
          case VObjectType::paAssignOp_ArithShiftLeft: {
            NodeId rval = fC->Sibling(op);
            Value* valueR = evalExpr(fC, rval, instance, muteErrors);
            value->shiftLeft(sval, valueR);
            m_valueFactory.deleteValue(valueR);
            return value;
          }
          case VObjectType::paAssignOp_ArithShiftRight: {
            NodeId rval = fC->Sibling(op);
            Value* valueR = evalExpr(fC, rval, instance, muteErrors);
            value->shiftRight(sval, valueR);
            m_valueFactory.deleteValue(valueR);
            return value;
          }
          case VObjectType::paAssignOp_BitwAnd: {
            NodeId rval = fC->Sibling(op);
            Value* valueR = evalExpr(fC, rval, instance, muteErrors);
            value->bitwAnd(sval, valueR);
            m_valueFactory.deleteValue(valueR);
            return value;
          }
          case VObjectType::paAssignOp_BitwOr: {
            NodeId rval = fC->Sibling(op);
            Value* valueR = evalExpr(fC, rval, instance, muteErrors);
            value->bitwOr(sval, valueR);
            m_valueFactory.deleteValue(valueR);
            return value;
          }
          case VObjectType::paAssignOp_BitwXor: {
            NodeId rval = fC->Sibling(op);
            Value* valueR = evalExpr(fC, rval, instance, muteErrors);
            value->bitwXor(sval, valueR);
            m_valueFactory.deleteValue(valueR);
            return value;
          }
          case VObjectType::paAssignOp_BitwLeftShift: {
            NodeId rval = fC->Sibling(op);
            Value* valueR = evalExpr(fC, rval, instance, muteErrors);
            value->shiftLeft(sval, valueR);
            m_valueFactory.deleteValue(valueR);
            return value;
          }
          case VObjectType::paAssignOp_BitwRightShift: {
            NodeId rval = fC->Sibling(op);
            Value* valueR = evalExpr(fC, rval, instance, muteErrors);
            value->shiftRight(sval, valueR);
            m_valueFactory.deleteValue(valueR);
            return value;
          }
          default:
            break;
        }
        break;
      }
      case VObjectType::paIncDec_PlusPlus: {
        const std::string_view name = fC->SymName(fC->Sibling(parent));
        Value* sval = nullptr;
        if (instance) {
          if (instance->getComplexValue(name)) {
            muteErrors = true;
            value->setInvalid();
            break;
          } else {
            sval = instance->getValue(name);
          }
          value->u_plus(sval);
          value->incr();
          return value;
        }
        break;
      }
      case VObjectType::paIncDec_MinusMinus: {
        const std::string_view name = fC->SymName(fC->Sibling(parent));
        Value* sval = nullptr;
        if (instance) {
          if (instance->getComplexValue(name)) {
            muteErrors = true;
            value->setInvalid();
            break;
          } else {
            sval = instance->getValue(name);
          }
          value->u_plus(sval);
          value->decr();
          return value;
        }
        break;
      }
      default:
        break;
    }

    value->setInvalid();
  }
  return value;
}
// NOLINTEND(*.DeadStores)

Value* ExprBuilder::fromVpiValue(std::string_view s, int32_t size) {
  Value* val = nullptr;
  if (s.find("UINT:") == 0) {
    val = m_valueFactory.newLValue();
    uint64_t v = 0;
    s.remove_prefix(std::string_view("UINT:").length());
    if (NumUtils::parseUint64(s, &v) == nullptr) {
      v = 0;
    }
    if (size)
      val->set(v, Value::Type::Unsigned, size);
    else
      val->set(v);
  } else if (s.find("INT:") == 0) {
    val = m_valueFactory.newLValue();
    int64_t v = 0;
    s.remove_prefix(std::string_view("INT:").length());
    if (NumUtils::parseInt64(s, &v) == nullptr) {
      v = 0;
    }
    if (size)
      val->set(v, Value::Type::Integer, size);
    else
      val->set(v);
  } else if (s.find("DEC:") == 0) {
    val = m_valueFactory.newLValue();
    int64_t v = 0;
    s.remove_prefix(std::string_view("DEC:").length());
    if (NumUtils::parseInt64(s, &v) == nullptr) {
      v = 0;
    }
    if (size)
      val->set(v, Value::Type::Integer, size);
    else
      val->set(v);
  } else if (s.find("SCAL:") == 0) {
    s.remove_prefix(std::string_view("SCAL:").length());
    switch (s.front()) {
      case 'Z':
        break;
      case 'X':
        break;
      case 'H':
        break;
      case 'L':
        break;
        // Not really clear what the difference between X and DontCare is.
        // Let's parse 'W'eak don't care as this one.
      case 'W':
        break;
      default:
        if (strcasecmp(s.data(), "DontCare") == 0) {
        } else if (strcasecmp(s.data(), "NoChange") == 0) {
        } else {
          val = m_valueFactory.newLValue();
          int64_t v = 0;
          if (NumUtils::parseInt64(s, &v) == nullptr) {
            v = 0;
          }
          val->set(v);
        }
        break;
    }
  } else if (s.find("BIN:") == 0) {
    s.remove_prefix(std::string_view("BIN:").length());
    StValue* sval = (StValue*)m_valueFactory.newStValue();
    sval->set(s, Value::Type::Binary, (size ? size : s.size()));
    val = sval;
  } else if (s.find("HEX:") == 0) {
    s.remove_prefix(std::string_view("HEX:").length());
    StValue* sval = (StValue*)m_valueFactory.newStValue();
    sval->set(s, Value::Type::Hexadecimal, (size ? size : (s.size() - 4) * 4));
    val = sval;
  } else if (s.find("OCT:") == 0) {
    val = m_valueFactory.newLValue();
    uint64_t v = 0;
    s.remove_prefix(std::string_view("OCT:").length());
    if (NumUtils::parseOctal(s, &v) == nullptr) {
      v = 0;
    }
    if (size)
      val->set(v, Value::Type::Unsigned, size);
    else
      val->set(v, Value::Type::Unsigned, (size ? size : (s.size() - 4) * 4));
  } else if (s.find("STRING:") == 0) {
    val = m_valueFactory.newStValue();
    val->set(s.data() + std::string_view("STRING:").length());
  } else if (s.find("REAL:") == 0) {
    val = m_valueFactory.newLValue();
    s.remove_prefix(std::string_view("REAL:").length());
    double v = 0;
    if (NumUtils::parseDouble(s, &v) == nullptr) {
      v = 0;
    }
    val->set(v);
  }
  return val;
}

Value* ExprBuilder::fromString(std::string_view value) {
  Value* val = nullptr;
  if (value.find('\'') != std::string::npos) {
    std::string sval;
    char base = 'b';
    uint32_t i = 0;
    for (i = 0; i < value.size(); i++) {
      if (value[i] == '\'') {
        base = value[i + 1];
        if (base == 's' || base == 'S') base = value[i + 2];
        break;
      }
    }
    if (value.find_first_of("sS") != std::string::npos) {
      sval = value.substr(i + 3);
    } else {
      sval = value.substr(i + 2);
    }
    sval = StringUtils::replaceAll(sval, "_", "");
    // No check for validity of sval being a legal parse-able value.
    switch (base) {
      case 'h': {
        std::string_view size = StringUtils::rtrim_until(value, '\'');
        int32_t s = 0;
        if (NumUtils::parseInt32(size, &s) == nullptr) {
          s = 0;
        }
        StValue* stval = (StValue*)m_valueFactory.newStValue();
        stval->set(sval, Value::Type::Hexadecimal, s);
        val = stval;
        break;
      }
      case 'b': {
        std::string_view size = StringUtils::rtrim_until(value, '\'');
        int32_t s = 0;
        if (NumUtils::parseInt32(size, &s) == nullptr) {
          s = 0;
        }
        StValue* stval = (StValue*)m_valueFactory.newStValue();
        stval->set(sval, Value::Type::Binary, s);
        val = stval;
        break;
      }
      case 'o': {
        std::string_view size = StringUtils::rtrim_until(value, '\'');
        int32_t s = 0;
        if (NumUtils::parseInt32(size, &s) == nullptr) {
          s = 0;
        }
        StValue* stval = (StValue*)m_valueFactory.newStValue();
        stval->set(sval, Value::Type::Octal, s);
        val = stval;
        break;
      }
      case 'd': {
        long double v = 0;
        if ((sval.find('.') != std::string::npos) &&
            (NumUtils::parseLongDouble(sval, &v) != nullptr)) {
          val = m_valueFactory.newLValue();
          val->set((double)v);
        } else {
          std::string size(StringUtils::rtrim_until(value, '\''));
          int32_t s = 0;
          if (NumUtils::parseInt32(size, &s) == nullptr) {
            s = 0;
          }
          if (!value.empty()) {
            if (value[0] == '-') {
              int64_t v = 0;
              if (NumUtils::parseInt64(sval, &v) != nullptr) {
                val = m_valueFactory.newLValue();
                val->set(v, Value::Type::Integer, s);
              }
            } else {
              uint64_t v = 0;
              if (NumUtils::parseUint64(sval, &v) != nullptr) {
                val = m_valueFactory.newLValue();
                val->set(v, Value::Type::Unsigned, s);
              }
            }
          }
          if (val == nullptr) {
            val = m_valueFactory.newStValue();
            val->set(value);
          }
        }
        break;
      }
      default: {
        std::string_view size = StringUtils::rtrim_until(value, '\'');
        int32_t s = 0;
        if (NumUtils::parseInt32(size, &s) == nullptr) {
          s = 0;
        }
        StValue* stval = (StValue*)m_valueFactory.newStValue();
        stval->set(sval, Value::Type::Binary, s);
        val = stval;
        break;
      }
    }
  } else {
    long double v = 0;
    if ((value.find('.') != std::string::npos) &&
        (NumUtils::parseLongDouble(value, &v) != nullptr)) {
      val = m_valueFactory.newLValue();
      val->set((double)v);
    } else if (!value.empty()) {
      if (value.front() == '-') {
        int64_t v = 0;
        if (NumUtils::parseInt64(value, &v) != nullptr) {
          val = m_valueFactory.newLValue();
          val->set(v);
        }
      } else {
        uint64_t v = 0;
        if (NumUtils::parseUint64(value, &v) != nullptr) {
          val = m_valueFactory.newLValue();
          val->set(v);
        }
      }
      if (val == nullptr) {
        val = m_valueFactory.newStValue();
        val->set(value);
      }
    }
  }
  return val;
}

}  // namespace SURELOG
