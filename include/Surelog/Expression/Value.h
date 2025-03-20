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
 * File:   Value.h
 * Author: alain
 *
 * Created on October 29, 2017, 10:33 PM
 */

#ifndef SURELOG_VALUE_H
#define SURELOG_VALUE_H
#pragma once

#include <Surelog/Common/RTTI.h>
#include <uhdm/typespec.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace SURELOG {

class Expr;
class LValue;
class StValue;
class ValueFactory;

class Value : public RTTI {
  SURELOG_IMPLEMENT_RTTI(Value, RTTI)
 public:
  friend Expr;
  friend ValueFactory;

  enum class Type {
    None,
    Binary,
    Hexadecimal,
    Octal,
    Unsigned,
    Integer,
    Double,
    String,
    Scalar
  };

  ~Value() override {}

  virtual int16_t getSize() const = 0;  // size in bits
  virtual int16_t getSize(
      uint32_t wordIndex) const = 0;  // size in bits of a multi-word value
  // nb of 64 bits words necessary to encode the size
  virtual uint16_t getNbWords() const = 0;

  virtual Type getType() const = 0;

  // return false if the value is not valid (like nan)
  virtual bool isValid() const = 0;
  virtual void setValid() = 0;
  virtual void setInvalid() = 0;
  virtual bool isSigned() const = 0;
  virtual void setSigned(bool isSigned) = 0;
  virtual bool isNegative() const = 0;
  virtual void setNegative() = 0;
  virtual void setRange(uint16_t lrange, uint16_t rrange) = 0;
  virtual void setTypespec(const uhdm::Typespec* tps) = 0;
  virtual const uhdm::Typespec* getTypespec() const = 0;
  virtual uint16_t getLRange() const = 0;
  virtual uint16_t getRRange() const = 0;
  // is large value (more than one 64 bit word)
  virtual bool isLValue() const = 0;

  virtual uint64_t getValueUL(uint16_t index = 0) const = 0;
  virtual int64_t getValueL(uint16_t index = 0) const = 0;
  virtual double getValueD(uint16_t index = 0) const = 0;
  virtual std::string getValueS() const = 0;

  virtual void set(uint64_t val) = 0;
  virtual void set(int64_t val) = 0;
  virtual void set(double val) = 0;
  virtual void set(uint64_t val, Type type, int16_t size) = 0;
  virtual void set(std::string_view val) = 0;
  virtual void set(std::string_view val, Type type) = 0;
  virtual bool operator<(const Value& rhs) const = 0;
  virtual bool operator==(const Value& rhs) const = 0;

  bool operator>(const Value& rhs) const { return rhs < (*this); }
  bool operator<=(const Value& rhs) const { return !(*this > rhs); }
  bool operator>=(const Value& rhs) const { return !(*this < rhs); }
  bool operator!=(const Value& rhs) const { return !((*this) == rhs); }

  virtual std::string uhdmValue() = 0;
  virtual std::string decompiledValue() = 0;
  virtual int32_t vpiValType() = 0;

  virtual void u_plus(const Value* a) = 0;
  virtual void u_minus(const Value* a) = 0;
  virtual void u_not(const Value* a) = 0;
  virtual void u_tilda(const Value* a) = 0;
  virtual void u_bitwAnd(const Value* a) = 0;
  virtual void u_bitwNand(const Value* a) = 0;
  virtual void u_bitwOr(const Value* a) = 0;
  virtual void u_bitwNor(const Value* a) = 0;
  virtual void u_bitwXor(const Value* a) = 0;
  virtual void u_bitwXnor(const Value* a) = 0;
  virtual void incr() = 0;
  virtual void decr() = 0;
  virtual void plus(const Value* a, const Value* b) = 0;
  virtual void minus(const Value* a, const Value* b) = 0;
  virtual void mult(const Value* a, const Value* b) = 0;
  virtual void div(const Value* a, const Value* b) = 0;
  virtual void power(const Value* a, const Value* b) = 0;
  virtual void mod(const Value* a, const Value* b) = 0;

  virtual void greater(const Value* a, const Value* b) = 0;
  virtual void greater_equal(const Value* a, const Value* b) = 0;
  virtual void lesser(const Value* a, const Value* b) = 0;
  virtual void lesser_equal(const Value* a, const Value* b) = 0;
  virtual void equiv(const Value* a, const Value* b) = 0;
  virtual void logAnd(const Value* a, const Value* b) = 0;
  virtual void logOr(const Value* a, const Value* b) = 0;
  virtual void bitwAnd(const Value* a, const Value* b) = 0;
  virtual void bitwOr(const Value* a, const Value* b) = 0;
  virtual void bitwXor(const Value* a, const Value* b) = 0;
  virtual void notEqual(const Value* a, const Value* b) = 0;
  virtual void shiftLeft(const Value* a, const Value* b) = 0;
  virtual void shiftRight(const Value* a, const Value* b) = 0;
  void setValueFactory(ValueFactory* factory) { m_valueFactory = factory; }
  ValueFactory* getValueFactory() { return m_valueFactory; }

 protected:
  uint32_t nbWords_(uint32_t size);
  ValueFactory* m_valueFactory = nullptr;
};
}  // namespace SURELOG
SURELOG_IMPLEMENT_RTTI_CAST_FUNCTIONS(value_cast, SURELOG::Value)

namespace SURELOG {
class SValue final : public Value {
  SURELOG_IMPLEMENT_RTTI(SValue, Value)
  friend LValue;

 private:
  union Data {
    int64_t s_int;
    uint64_t u_int;
    double d_int;
  };
  Data m_value;

 public:
  SValue()
      : m_type(Value::Type::Unsigned), m_size(0), m_valid(1), m_negative(0) {
    m_value.u_int = 0;
  }
  SValue(int64_t val, int16_t size)
      : m_type(Value::Type::Integer),
        m_size(size),
        m_valid(1),
        m_negative(val < 0),
        m_signed(true) {
    m_value.s_int = val;
  }
  explicit SValue(uint64_t val)
      : m_type(Value::Type::Unsigned), m_size(64), m_valid(1), m_negative(0) {
    m_value.u_int = val;
  }
  explicit SValue(int64_t val)
      : m_type(Value::Type::Integer),
        m_size(64),
        m_valid(1),
        m_negative(val < 0),
        m_signed(true) {
    m_value.s_int = val;
  }
  explicit SValue(double val)
      : m_type(Value::Type::Double),
        m_size(64),
        m_valid(1),
        m_negative(val < 0),
        m_signed(true) {
    m_value.d_int = val;
  }
  ~SValue() final;

  int16_t getSize() const final { return m_size; }
  int16_t getSize(uint32_t wordIndex) const final { return m_size; }
  void setRange(uint16_t lrange, uint16_t rrange) final {
    m_lrange = lrange;
    m_rrange = rrange;
  }
  bool isSigned() const final { return m_signed; }
  void setSigned(bool isSigned) final { m_signed = isSigned; }
  uint16_t getLRange() const final { return m_lrange; }
  uint16_t getRRange() const final { return m_rrange; }
  uint16_t getNbWords() const final { return 1; }
  bool isLValue() const final { return false; }
  Type getType() const final { return m_type; }
  bool isValid() const final { return m_valid; }
  void setValid() final { m_valid = true; }
  void setTypespec(const uhdm::Typespec* tps) final {}
  const uhdm::Typespec* getTypespec() const final { return nullptr; }
  void setInvalid() final { m_valid = 0; }
  bool isNegative() const final { return m_negative; }
  void setNegative() final { m_negative = 1; }
  void set(uint64_t val) final;
  void set(int64_t val) final;
  void set(double val) final;
  void set(uint64_t val, Type type, int16_t size) final;

  void set(std::string_view val) final {
    m_type = Value::Type::None;
    m_value.u_int = 0;
    m_size = 0;
    m_valid = false;
    m_negative = 0;
  }

  void set(std::string_view val, Type type) final {
    m_type = Value::Type::None;
    m_value.u_int = 0;
    m_size = 0;
    m_valid = false;
    m_negative = 0;
  }

  bool operator<(const Value& rhs) const final {
    if (m_type == Value::Type::Integer) {
      return m_value.s_int < (value_cast<const SValue*>(&rhs))->m_value.s_int;
    } else if (m_type == Value::Type::Double) {
      return m_value.d_int < (value_cast<const SValue*>(&rhs))->m_value.d_int;
    } else {
      return m_value.u_int < (value_cast<const SValue*>(&rhs))->m_value.u_int;
    }
  }

  bool operator==(const Value& rhs) const final {
    if (m_type == Value::Type::Integer) {
      return m_value.s_int == (value_cast<const SValue*>(&rhs))->m_value.s_int;
    } else if (m_type == Value::Type::Double) {
      return m_value.d_int == (value_cast<const SValue*>(&rhs))->m_value.d_int;
    } else {
      return m_value.u_int == (value_cast<const SValue*>(&rhs))->m_value.u_int;
    }
  }

  uint64_t getValueUL(uint16_t index = 0) const final { return m_value.u_int; }
  int64_t getValueL(uint16_t index = 0) const final { return m_value.s_int; }
  double getValueD(uint16_t index = 0) const final { return m_value.d_int; }
  std::string getValueS() const final { return "NOT_A_STRING_VALUE"; }

  std::string uhdmValue() final;
  std::string decompiledValue() final;
  int32_t vpiValType() final;

  void u_plus(const Value* a) final;
  void u_minus(const Value* a) final;
  void u_not(const Value* a) final;
  void u_tilda(const Value* a) final;
  void u_bitwAnd(const Value* a) final;
  void u_bitwNand(const Value* a) final;
  void u_bitwOr(const Value* a) final;
  void u_bitwNor(const Value* a) final;
  void u_bitwXor(const Value* a) final;
  void u_bitwXnor(const Value* a) final;
  void incr() final;
  void decr() final;
  void plus(const Value* a, const Value* b) final;
  void minus(const Value* a, const Value* b) final;
  void mult(const Value* a, const Value* b) final;
  void div(const Value* a, const Value* b) final;
  void power(const Value* a, const Value* b) final;
  void mod(const Value* a, const Value* b) final;
  void greater(const Value* a, const Value* b) final;
  void greater_equal(const Value* a, const Value* b) final;
  void lesser(const Value* a, const Value* b) final;
  void lesser_equal(const Value* a, const Value* b) final;
  void equiv(const Value* a, const Value* b) final;
  void logAnd(const Value* a, const Value* b) final;
  void logOr(const Value* a, const Value* b) final;
  void bitwAnd(const Value* a, const Value* b) final;
  void bitwOr(const Value* a, const Value* b) final;
  void bitwXor(const Value* a, const Value* b) final;
  void notEqual(const Value* a, const Value* b) final;
  void shiftLeft(const Value* a, const Value* b) final;
  void shiftRight(const Value* a, const Value* b) final;

 private:
  Type m_type;
  int16_t m_size;
  uint16_t m_valid = 0;
  uint16_t m_negative = 0;
  uint16_t m_lrange = 0;
  uint16_t m_rrange = 0;
  bool m_signed = false;
};

class ValueFactory {
 public:
  ValueFactory();
  Value* newSValue();
  Value* newLValue();
  Value* newStValue();
  Value* newValue(SValue& initVal);
  Value* newValue(LValue& initVal);
  Value* newValue(StValue& initVal);
  void deleteValue(Value*);

 protected:
  LValue* m_headFree;
  LValue* m_headInUse;
};

class LValue final : public Value {
  SURELOG_IMPLEMENT_RTTI(LValue, Value)
  friend ValueFactory;

 public:
  LValue(const LValue&);
  LValue() = default;
  LValue(Type type, SValue* values, uint16_t nbWords)
      : m_type(type),
        m_nbWords(nbWords),
        m_valueArray(values),
        m_valid(1),
        m_negative(0) {}
  explicit LValue(uint64_t val);
  explicit LValue(int64_t val);
  explicit LValue(double val);
  LValue(int64_t val, Type type, int16_t size);
  ~LValue() final;

  int16_t getSize() const final;
  int16_t getSize(uint32_t wordIndex) const final {
    if (m_valueArray) {
      return m_valueArray[wordIndex].m_size;
    } else
      return 0;
  }
  bool isSigned() const final { return m_signed; }
  void setSigned(bool isSigned) final { m_signed = isSigned; }
  void setRange(uint16_t lrange, uint16_t rrange) final {
    m_lrange = lrange;
    m_rrange = rrange;
  }
  uint16_t getLRange() const final { return m_lrange; }
  uint16_t getRRange() const final { return m_rrange; }
  uint16_t getNbWords() const final { return m_nbWords; }
  bool isLValue() const final { return true; }
  Type getType() const final { return m_type; }
  bool isValid() const final { return m_valid; }
  void setValid() final { m_valid = true; }
  void setInvalid() final { m_valid = 0; }
  void setTypespec(const uhdm::Typespec* tps) final { m_typespec = tps; }
  const uhdm::Typespec* getTypespec() const final { return m_typespec; };
  bool isNegative() const final { return m_negative; }
  void setNegative() final { m_negative = 1; }
  void set(uint64_t val) final;
  void set(int64_t val) final;
  void set(double val) final;
  void set(uint64_t val, Type type, int16_t size) final;
  void set(std::string_view val) final {}
  void set(std::string_view val, Type type) final {}
  bool operator<(const Value& rhs) const final;
  bool operator==(const Value& rhs) const final;

  uint64_t getValueUL(uint16_t index = 0) const final {
    return ((index < m_nbWords) ? m_valueArray[index].m_value.u_int : 0);
  }
  int64_t getValueL(uint16_t index = 0) const final {
    return ((index < m_nbWords) ? m_valueArray[index].m_value.s_int : 0);
  }
  double getValueD(uint16_t index = 0) const final {
    return ((index < m_nbWords) ? m_valueArray[index].m_value.d_int : 0);
  }
  std::string getValueS() const final { return "NOT_A_STRING_VALUE"; }

  std::string uhdmValue() final;
  std::string decompiledValue() final;
  int32_t vpiValType() final;

  void u_plus(const Value* a) final;
  void u_minus(const Value* a) final;
  void u_not(const Value* a) final;
  void u_tilda(const Value* a) final;
  void u_bitwAnd(const Value* a) final;
  void u_bitwNand(const Value* a) final;
  void u_bitwOr(const Value* a) final;
  void u_bitwNor(const Value* a) final;
  void u_bitwXor(const Value* a) final;
  void u_bitwXnor(const Value* a) final;
  void incr() final;
  void decr() final;
  void plus(const Value* a, const Value* b) final;
  void minus(const Value* a, const Value* b) final;
  void mult(const Value* a, const Value* b) final;
  void div(const Value* a, const Value* b) final;
  void power(const Value* a, const Value* b) final;
  void mod(const Value* a, const Value* b) final;
  void greater(const Value* a, const Value* b) final;
  void greater_equal(const Value* a, const Value* b) final;
  void lesser(const Value* a, const Value* b) final;
  void lesser_equal(const Value* a, const Value* b) final;
  void equiv(const Value* a, const Value* b) final;
  void logAnd(const Value* a, const Value* b) final;
  void logOr(const Value* a, const Value* b) final;
  void bitwAnd(const Value* a, const Value* b) final;
  void bitwOr(const Value* a, const Value* b) final;
  void bitwXor(const Value* a, const Value* b) final;
  void notEqual(const Value* a, const Value* b) final;
  void shiftLeft(const Value* a, const Value* b) final;
  void shiftRight(const Value* a, const Value* b) final;

  void adjust(const Value* a);

 private:
  Type m_type = Type::None;
  uint16_t m_nbWords = 0;
  SValue* m_valueArray = nullptr;
  uint16_t m_valid = 0;
  uint16_t m_negative = 0;
  uint16_t m_lrange = 0;
  uint16_t m_rrange = 0;
  bool m_signed = false;
  const uhdm::Typespec* m_typespec = nullptr;
};

class StValue final : public Value {
  SURELOG_IMPLEMENT_RTTI(StValue, Value)
  friend LValue;

 public:
  StValue()
      : m_type(Type::String), m_size(0), m_valid(false), m_typespec(nullptr) {}
  explicit StValue(std::string_view val)
      : m_type(Type::String),
        m_value(val),
        m_size(val.size()),
        m_valid(true),
        m_typespec(nullptr) {}
  ~StValue() final;

  int16_t getSize() const final { return m_size; }
  int16_t getSize(uint32_t wordIndex) const final { return m_size; }
  void setRange(uint16_t lrange, uint16_t rrange) final {
    m_lrange = lrange;
    m_rrange = rrange;
  }
  bool isSigned() const final { return m_signed; }
  void setSigned(bool isSigned) final { m_signed = isSigned; }
  uint16_t getLRange() const final { return m_lrange; };
  uint16_t getRRange() const final { return m_rrange; };
  uint16_t getNbWords() const final { return 1; }
  bool isLValue() const final { return false; }
  Type getType() const final { return m_type; }
  bool isValid() const final { return m_valid; }
  void setValid() final { m_valid = true; }
  void setInvalid() final { m_valid = false; }
  void setTypespec(const uhdm::Typespec* tps) final { m_typespec = tps; }
  const uhdm::Typespec* getTypespec() const final { return m_typespec; };
  void setNegative() final {}
  bool isNegative() const final { return false; }
  void set(uint64_t val) final {
    m_type = Type::Unsigned;
    m_value = std::to_string(val);
    m_valid = true;
    m_signed = false;
    m_typespec = nullptr;
  }
  void set(int64_t val) final {
    m_type = Type::Integer;
    m_value = std::to_string(val);
    m_valid = true;
    m_signed = true;
    m_typespec = nullptr;
  }
  void set(double val) final {
    m_type = Type::Double;
    m_value = std::to_string(val);
    m_valid = true;
    m_signed = true;
    m_typespec = nullptr;
  }
  void set(uint64_t val, Type type, int16_t size) final {
    m_type = type;
    m_value = std::to_string(val);
    m_size = size;
    m_valid = true;
    m_signed = false;
    m_typespec = nullptr;
  }
  void set(std::string_view val, Type type) final {
    m_type = type;
    m_value = val;
    m_size = val.size();
    if (type == Type::String) {
      m_size = m_size * 8;
    }
    m_valid = true;
    m_signed = false;
    m_typespec = nullptr;
  }
  void set(std::string_view val, Type type, int16_t size) {
    m_type = type;
    m_value = val;
    m_size = size;
    m_valid = true;
    m_signed = false;
    m_typespec = nullptr;
  }
  void set(std::string_view val) final {
    m_type = Type::String;
    m_value = val;
    m_size = val.size() * 8;
    m_valid = true;
    m_signed = false;
    m_typespec = nullptr;
  }
  bool operator<(const Value& rhs) const final {
    return m_value < (value_cast<const StValue*>(&rhs))->m_value;
  }
  bool operator==(const Value& rhs) const final {
    return m_value == (value_cast<const StValue*>(&rhs))->m_value;
  }
  uint64_t getValueUL(uint16_t index = 0) const final {
    switch (m_type) {
      case Value::Type::Integer:
        return (uint64_t)std::strtoull(m_value.c_str(), nullptr, 10);
      case Value::Type::Unsigned:
        return (uint64_t)std::strtoull(m_value.c_str(), nullptr, 10);
      case Value::Type::Hexadecimal:
        return (uint64_t)std::strtoull(m_value.c_str(), nullptr, 16);
      case Value::Type::Octal:
        return (uint64_t)std::strtoull(m_value.c_str(), nullptr, 8);
      case Value::Type::Binary:
        return (uint64_t)std::strtoull(m_value.c_str(), nullptr, 2);
      default:
        return (uint64_t)std::strtoull(m_value.c_str(), nullptr, 10);
    }
  }
  int64_t getValueL(uint16_t index = 0) const final {
    switch (m_type) {
      case Value::Type::Integer:
        return (int64_t)std::strtoll(m_value.c_str(), nullptr, 10);
      case Value::Type::Unsigned:
        return (int64_t)std::strtoll(m_value.c_str(), nullptr, 10);
      case Value::Type::Hexadecimal:
        return (int64_t)std::strtoll(m_value.c_str(), nullptr, 16);
      case Value::Type::Octal:
        return (int64_t)std::strtoll(m_value.c_str(), nullptr, 8);
      case Value::Type::Binary:
        return (int64_t)std::strtoll(m_value.c_str(), nullptr, 2);
      default:
        return (int64_t)std::strtoll(m_value.c_str(), nullptr, 10);
    }
  }
  double getValueD(uint16_t index = 0) const final {
    return strtod(m_value.c_str(), nullptr);
  }
  std::string getValueS() const final { return m_value; }

  std::string uhdmValue() final;
  std::string decompiledValue() final;
  int32_t vpiValType() final;

  void u_plus(const Value* a) final {}
  void u_minus(const Value* a) final {}
  void u_not(const Value* a) final {}
  void u_tilda(const Value* a) final {}
  void u_bitwAnd(const Value* a) final {}
  void u_bitwNand(const Value* a) final {}
  void u_bitwOr(const Value* a) final {}
  void u_bitwNor(const Value* a) final {}
  void u_bitwXor(const Value* a) final {}
  void u_bitwXnor(const Value* a) final {}
  void incr() final {}
  void decr() final {}
  void plus(const Value* a, const Value* b) final {}
  void minus(const Value* a, const Value* b) final {}
  void mult(const Value* a, const Value* b) final {}
  void div(const Value* a, const Value* b) final {}
  void power(const Value* a, const Value* b) final {}
  void mod(const Value* a, const Value* b) final {}
  void greater(const Value* a, const Value* b) final {}
  void greater_equal(const Value* a, const Value* b) final {}
  void lesser(const Value* a, const Value* b) final {}
  void lesser_equal(const Value* a, const Value* b) final {}
  void equiv(const Value* a, const Value* b) final;
  void logAnd(const Value* a, const Value* b) final {}
  void logOr(const Value* a, const Value* b) final {}
  void bitwAnd(const Value* a, const Value* b) final {}
  void bitwOr(const Value* a, const Value* b) final {}
  void bitwXor(const Value* a, const Value* b) final {}
  void notEqual(const Value* a, const Value* b) final;
  void shiftLeft(const Value* a, const Value* b) final {}
  void shiftRight(const Value* a, const Value* b) final {}

 private:
  Type m_type;
  std::string m_value;
  int16_t m_size = 0;
  uint16_t m_valid = 0;
  uint16_t m_lrange = 0;
  uint16_t m_rrange = 0;
  bool m_signed = false;
  const uhdm::Typespec* m_typespec = nullptr;
};

};  // namespace SURELOG

#endif /* SURELOG_VALUE_H */
