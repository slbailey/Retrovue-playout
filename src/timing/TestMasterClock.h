#ifndef RETROVUE_TIMING_TEST_MASTER_CLOCK_H_
#define RETROVUE_TIMING_TEST_MASTER_CLOCK_H_

#include "retrovue/timing/MasterClock.h"

#include <cstdint>

namespace retrovue::timing {

class TestMasterClock : public MasterClock {
 public:
  TestMasterClock();

  int64_t now_utc_us() const override;
  double now_monotonic_s() const override;
  int64_t scheduled_to_utc_us(int64_t pts_us) const override;
  double drift_ppm() const override;

  void SetNow(int64_t utc_us, double monotonic_s);
  void AdvanceMicroseconds(int64_t delta_us);
  void AdvanceSeconds(double delta_s);
  void SetDriftPpm(double ppm);
  void SetEpochUtcUs(int64_t epoch_utc_us);
  void SetRatePpm(double rate_ppm);

 private:
  int64_t utc_us_;
  double monotonic_s_;
  int64_t epoch_utc_us_;
  double rate_ppm_;
  double drift_ppm_;
};

}  // namespace retrovue::timing

#endif  // RETROVUE_TIMING_TEST_MASTER_CLOCK_H_


