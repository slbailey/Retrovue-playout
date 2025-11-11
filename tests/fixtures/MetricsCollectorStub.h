#ifndef RETROVUE_TESTS_FIXTURES_METRICS_COLLECTOR_STUB_H_
#define RETROVUE_TESTS_FIXTURES_METRICS_COLLECTOR_STUB_H_

#include <map>
#include <mutex>
#include <string>

namespace retrovue::tests::fixtures
{

class MetricsCollectorStub
{
public:
  void SetGauge(const std::string& name, double value)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    gauges_[name] = value;
  }

  void IncrementCounter(const std::string& name, double delta = 1.0)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    counters_[name] += delta;
  }

  [[nodiscard]] double GetGauge(const std::string& name) const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = gauges_.find(name);
    return it != gauges_.end() ? it->second : 0.0;
  }

  [[nodiscard]] double GetCounter(const std::string& name) const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = counters_.find(name);
    return it != counters_.end() ? it->second : 0.0;
  }

  void Reset()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    gauges_.clear();
    counters_.clear();
  }

private:
  mutable std::mutex mutex_;
  std::map<std::string, double> gauges_;
  std::map<std::string, double> counters_;
};

} // namespace retrovue::tests::fixtures

#endif // RETROVUE_TESTS_FIXTURES_METRICS_COLLECTOR_STUB_H_

