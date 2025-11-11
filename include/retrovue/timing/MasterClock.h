#ifndef RETROVUE_TIMING_MASTER_CLOCK_H_
#define RETROVUE_TIMING_MASTER_CLOCK_H_

#include <cstdint>
#include <memory>

namespace retrovue::timing {

// MasterClock provides monotonic and wall-clock time along with PTS to UTC mapping.
class MasterClock {
 public:
  virtual ~MasterClock() = default;

  // Returns current UTC time in microseconds since Unix epoch.
  virtual int64_t now_utc_us() const = 0;

  // Returns current monotonic time in seconds relative to clock start.
  virtual double now_monotonic_s() const = 0;

  // Maps a presentation timestamp (in microseconds) to an absolute UTC deadline.
  virtual int64_t scheduled_to_utc_us(int64_t pts_us) const = 0;

  // Reports measured drift in parts per million relative to upstream reference.
  virtual double drift_ppm() const = 0;
};

std::shared_ptr<MasterClock> MakeSystemMasterClock(int64_t epoch_utc_us, double rate_ppm);
}  // namespace retrovue::timing

#endif  // RETROVUE_TIMING_MASTER_CLOCK_H_
