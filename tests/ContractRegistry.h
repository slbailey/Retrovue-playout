#ifndef RETROVUE_TESTS_CONTRACT_REGISTRY_H_
#define RETROVUE_TESTS_CONTRACT_REGISTRY_H_

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace retrovue::tests
{

class ContractRegistry
{
public:
  static ContractRegistry& Instance();

  void RegisterSuite(const std::string& domain,
                     const std::string& suite_name,
                     const std::vector<std::string>& rule_ids);

  bool IsRuleCovered(const std::string& domain,
                     const std::string& rule_id) const;

  std::set<std::string> CoveredRules(const std::string& domain) const;

  std::vector<std::string> MissingRules(const std::string& domain,
                                        const std::vector<std::string>& expected) const;

  void Reset();

private:
  ContractRegistry() = default;
  ContractRegistry(const ContractRegistry&) = delete;
  ContractRegistry& operator=(const ContractRegistry&) = delete;

  mutable std::mutex mutex_;
  std::map<std::string, std::set<std::string>> coverage_;
  std::map<std::string, std::set<std::string>> suite_index_;
};

} // namespace retrovue::tests

#endif // RETROVUE_TESTS_CONTRACT_REGISTRY_H_

