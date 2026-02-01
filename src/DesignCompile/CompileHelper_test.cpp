/*
 Copyright 2021 Alain Dargelas

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
#include "Surelog/DesignCompile/CompileHelper.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <uhdm/Serializer.h>
#include <uhdm/constant.h>
#include <uhdm/vpi_user.h>

#include <cstdint>
#include <limits>
#include <string_view>

#include "Surelog/Common/Session.h"

namespace SURELOG {

TEST(CompileHelper, ParseConstants) {
  uhdm::Serializer s;
  auto tester = [&s](int32_t type, std::string_view value, int64_t* result) {
    uhdm::Constant* val = s.make<uhdm::Constant>();
    val->setConstType(type);
    val->setValue(value);
    return CompileHelper::parseConstant(*val, result);
  };

  int64_t result;
  {  // Binary
    EXPECT_TRUE(tester(vpiBinaryConst, "1010", &result));
    EXPECT_EQ(result, 0b1010);

    EXPECT_TRUE(tester(vpiBinaryConst, std::string(63, '1'), &result));
    EXPECT_EQ((uint64_t)result, 0x7FFFFFFFFFFFFFFFuLL);

    EXPECT_TRUE(tester(vpiBinaryConst, std::string(64, '1'), &result));
    EXPECT_EQ((uint64_t)result, 0xFFFFFFFFFFFFFFFFuLL);

    // Out of range.
    EXPECT_FALSE(tester(vpiBinaryConst, std::string(65, '1'), &result));
  }

  {  // Decimal tests

    EXPECT_TRUE(tester(vpiDecConst, "42", &result));
    EXPECT_EQ(result, 42);

    EXPECT_TRUE(tester(vpiDecConst, "-42", &result));
    EXPECT_EQ(result, -42);

    // Decimal is signed, so we expect overrflow using more than 63 bits.
    EXPECT_TRUE(tester(vpiDecConst, "9223372036854775807", &result));
    EXPECT_EQ(result, std::numeric_limits<int64_t>::max());

    // Positive Out of range
    EXPECT_FALSE(tester(vpiDecConst, "9223372036854775808", &result));

    EXPECT_TRUE(tester(vpiDecConst, "-9223372036854775808", &result));
    EXPECT_EQ(result, std::numeric_limits<int64_t>::min());

    // Negative Out of range
    EXPECT_FALSE(tester(vpiDecConst, "-9223372036854775809", &result));
  }

  {  // Integer tests. Essentially same as decimal
    EXPECT_TRUE(tester(vpiIntConst, "42", &result));
    EXPECT_EQ(result, 42);

    EXPECT_TRUE(tester(vpiIntConst, "-42", &result));
    EXPECT_EQ(result, -42);

    // Decimal is signed, so we expect overrflow using more than 63 bits.
    EXPECT_TRUE(tester(vpiIntConst, "9223372036854775807", &result));
    EXPECT_EQ(result, std::numeric_limits<int64_t>::max());

    // Positive Out of range
    EXPECT_FALSE(tester(vpiIntConst, "9223372036854775808", &result));

    EXPECT_TRUE(tester(vpiIntConst, "-9223372036854775808", &result));
    EXPECT_EQ(result, std::numeric_limits<int64_t>::min());

    // Negative Out of range
    EXPECT_FALSE(tester(vpiIntConst, "-9223372036854775809", &result));
  }

  {  // Unsigned
    EXPECT_TRUE(tester(vpiUIntConst, "18446744073709551615", &result));
    EXPECT_EQ((uint64_t)result, 18446744073709551615uLL);

    // Out of range.
    EXPECT_FALSE(tester(vpiUIntConst, "18446744073709551616", &result));

    // Negative values are allowed and interpreted as 2's complement, then
    // interpreted as unsigned.
    EXPECT_TRUE(tester(vpiUIntConst, "-1", &result));
    EXPECT_EQ((uint64_t)result, std::numeric_limits<uint64_t>::max());
  }

  {  // Hex
    EXPECT_TRUE(tester(vpiHexConst, "FF", &result));
    EXPECT_EQ(result, 0xFF);

    EXPECT_TRUE(tester(vpiHexConst, "FFFFFFFFFFFFFFFF", &result));
    EXPECT_EQ((uint64_t)result, std::numeric_limits<uint64_t>::max());
  }

  {  // Octal
    EXPECT_TRUE(tester(vpiOctConst, "377", &result));
    EXPECT_EQ(result, 0xff);

    EXPECT_TRUE(tester(vpiOctConst, "1777777777777777777777", &result));
    EXPECT_EQ((uint64_t)result, std::numeric_limits<uint64_t>::max());

    // Out of range.
    EXPECT_FALSE(tester(vpiOctConst, "3777777777777777777777", &result));
  }
}
}  // namespace SURELOG
