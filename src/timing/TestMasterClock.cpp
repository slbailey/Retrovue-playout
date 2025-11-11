#include "timing/TestMasterClock.h"

#include <cmath>

namespace retrovue::timing {

namespace {
constexpr double kMillion = 1'000'000.0;
}

TestMasterClock::TestMasterClock()
    : utc_us_(0),
      monotonic_s_(0.0),
      epoch_utc_us_(0),
      rate_ppm_(0.0),
      drift_ppm_(0.0) {}

int64_t TestMasterClock::now_utc_us() const { return utc_us_; }

double TestMasterClock::now_monotonic_s() const { return monotonic_s_; }

int64_t TestMasterClock::scheduled_to_utc_us(int64_t pts_us) const {
  const long double scale =
      1.0L + (static_cast<long double>(rate_ppm_) + static_cast<long double>(drift_ppm_)) /
                 kMillion;
  const long double adjusted = static_cast<long double>(pts_us) * scale;
  const auto rounded = static_cast<int64_t>(std::llround(adjusted));
  return epoch_utc_us_ + rounded;
}

double TestMasterClock::drift_ppm() const { return drift_ppm_; }

void TestMasterClock::SetNow(int64_t utc_us, double monotonic_s) {
  utc_us_ = utc_us;
  monotonic_s_ = monotonic_s;
}

void TestMasterClock::AdvanceMicroseconds(int64_t delta_us) {
  utc_us_ += delta_us;
  monotonic_s_ += static_cast<double>(delta_us) / kMillion;
}

void TestMasterClock::AdvanceSeconds(double delta_s) {
  monotonic_s_ += delta_s;
  utc_us_ += static_cast<int64_t>(std::llround(delta_s * kMillion));
}

void TestMasterClock::SetDriftPpm(double ppm) { drift_ppm_ = ppm; }

void TestMasterClock::SetEpochUtcUs(int64_t epoch_utc_us) { epoch_utc_us_ = epoch_utc_us; }

void TestMasterClock::SetRatePpm(double rate_ppm) { rate_ppm_ = rate_ppm; }

}  // namespace retrovue::timing


