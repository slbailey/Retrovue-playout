#ifndef RETROVUE_TESTS_BASE_CONTRACT_TEST_H_
#define RETROVUE_TESTS_BASE_CONTRACT_TEST_H_

#include <cmath>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "tests/ContractRegistry.h"

namespace retrovue::tests
{

// BaseContractTest provides the common harness for all contract test suites.
// It registers the rules covered by a suite with the global ContractRegistry
// and offers utility helpers shared across domains.
class BaseContractTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    ContractRegistry::Instance().RegisterSuite(
        DomainName(), SuiteName(), CoveredRuleIds());
  }

  [[nodiscard]] virtual std::string DomainName() const = 0;

  [[nodiscard]] virtual std::string SuiteName() const
  {
    const ::testing::TestInfo* const info =
        ::testing::UnitTest::GetInstance()->current_test_info();
    return info ? info->test_suite_name() : "UnknownSuite";
  }

  [[nodiscard]] virtual std::vector<std::string> CoveredRuleIds() const = 0;

  template <typename T>
  void AssertWithinTolerance(const T& value,
                             const T& expected,
                             const T& tolerance,
                             const std::string& message = {}) const
  {
    SCOPED_TRACE(message);
    ASSERT_LE(std::abs(static_cast<double>(value - expected)),
              static_cast<double>(tolerance));
  }
};

} // namespace retrovue::tests

#endif // RETROVUE_TESTS_BASE_CONTRACT_TEST_H_

