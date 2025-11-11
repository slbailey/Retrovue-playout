#ifndef RETROVUE_TESTS_CONTRACTS_CONTRACT_REGISTRY_ENVIRONMENT_H_
#define RETROVUE_TESTS_CONTRACTS_CONTRACT_REGISTRY_ENVIRONMENT_H_

#include <string>
#include <vector>

namespace retrovue::tests
{

// Registers the expected rule coverage for a domain within the current
// contract test binary. Multiple registrations for the same domain are merged.
void RegisterExpectedDomainCoverage(std::string domain,
                                    std::vector<std::string> rule_ids);

} // namespace retrovue::tests

#endif // RETROVUE_TESTS_CONTRACTS_CONTRACT_REGISTRY_ENVIRONMENT_H_

