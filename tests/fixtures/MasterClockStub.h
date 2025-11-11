#ifndef RETROVUE_TESTS_FIXTURES_MASTER_CLOCK_STUB_H_
#define RETROVUE_TESTS_FIXTURES_MASTER_CLOCK_STUB_H_

#include <atomic>
#include <cstdint>
#include <mutex>

namespace retrovue::tests::fixtures
{

class MasterClockStub
{
public:
  explicit MasterClockStub(int64_t start_time_us = 1'700'000'000'000'000,
                           int64_t frequency_hz = 1'000'000)
      : current_time_us_(start_time_us),
        frequency_hz_(frequency_hz),
        drift_us_(0)
  {
  }

  int64_t now_utc_us() const
  {
    return current_time_us_.load(std::memory_order_acquire);
  }

  int64_t now_local_us() const
  {
    return now_utc_us();
  }

  int64_t to_local(int64_t utc_us) const
  {
    return utc_us;
  }

  int64_t offset_from_schedule(int64_t scheduled_pts_us) const
  {
    return now_utc_us() - scheduled_pts_us + drift_us_.load(std::memory_order_acquire);
  }

  int64_t frequency() const
  {
    return frequency_hz_;
  }

  void Advance(int64_t delta_us)
  {
    current_time_us_.fetch_add(delta_us, std::memory_order_acq_rel);
  }

  void SetDrift(int64_t drift_us)
  {
    drift_us_.store(drift_us, std::memory_order_release);
  }

private:
  std::atomic<int64_t> current_time_us_;
  const int64_t frequency_hz_;
  std::atomic<int64_t> drift_us_;
};

} // namespace retrovue::tests::fixtures

#endif // RETROVUE_TESTS_FIXTURES_MASTER_CLOCK_STUB_H_

