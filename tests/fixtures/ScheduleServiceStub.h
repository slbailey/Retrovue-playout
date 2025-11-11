#ifndef RETROVUE_TESTS_FIXTURES_SCHEDULE_SERVICE_STUB_H_
#define RETROVUE_TESTS_FIXTURES_SCHEDULE_SERVICE_STUB_H_

#include <mutex>
#include <string>
#include <vector>

namespace retrovue::tests::fixtures
{

struct ScheduledAsset
{
  std::string asset_uri;
  int64_t start_pts_us;
  double duration_seconds;
};

class ScheduleServiceStub
{
public:
  void AddAsset(const ScheduledAsset& asset)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    assets_.push_back(asset);
  }

  std::vector<ScheduledAsset> GetSchedule() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return assets_;
  }

  void Clear()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    assets_.clear();
  }

private:
  mutable std::mutex mutex_;
  std::vector<ScheduledAsset> assets_;
};

} // namespace retrovue::tests::fixtures

#endif // RETROVUE_TESTS_FIXTURES_SCHEDULE_SERVICE_STUB_H_

