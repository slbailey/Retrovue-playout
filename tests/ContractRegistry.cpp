#include "tests/ContractRegistry.h"

#include <algorithm>
#include <iostream>

namespace retrovue::tests
{

ContractRegistry& ContractRegistry::Instance()
{
  static ContractRegistry instance;
  return instance;
}

void ContractRegistry::RegisterSuite(const std::string& domain,
                                     const std::string& suite_name,
                                     const std::vector<std::string>& rule_ids)
{
  std::lock_guard<std::mutex> lock(mutex_);

  auto& domain_rules = coverage_[domain];
  for (const auto& rule : rule_ids)
  {
    if (!rule.empty())
    {
      domain_rules.insert(rule);
      suite_index_[suite_name].insert(rule);
    }
  }
}

bool ContractRegistry::IsRuleCovered(const std::string& domain,
                                     const std::string& rule_id) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto domain_it = coverage_.find(domain);
  if (domain_it == coverage_.end())
  {
    return false;
  }
  return domain_it->second.count(rule_id) > 0;
}

void ContractRegistry::Reset()
{
  std::lock_guard<std::mutex> lock(mutex_);
  coverage_.clear();
  suite_index_.clear();
}

std::set<std::string> ContractRegistry::CoveredRules(const std::string& domain) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto domain_it = coverage_.find(domain);
  if (domain_it == coverage_.end())
  {
    return {};
  }
  return domain_it->second;
}

std::vector<std::string> ContractRegistry::MissingRules(
    const std::string& domain,
    const std::vector<std::string>& expected) const
{
  const auto covered = CoveredRules(domain);

  std::vector<std::string> missing;
  missing.reserve(expected.size());
  for (const auto& rule : expected)
  {
    if (!rule.empty() && covered.count(rule) == 0)
    {
      missing.push_back(rule);
    }
  }
  return missing;
}

} // namespace retrovue::tests

