#pragma once

#include <algorithm>
#include <cstdint>

namespace walle {
constexpr int kDefaultVoiceDriveMs = 10000;
constexpr int kMaximumVoiceDriveMs = 60000;

// A long user request is fulfilled with short motor-board leases. All methods
// run in the MotionLink worker, and all timestamps use the monotonic ESP timer.
class DriveLease {
public:
    void Start(int64_t now_us, int duration_ms) {
        deadline_us_ = now_us + std::clamp(duration_ms, 100, kMaximumVoiceDriveMs) * 1000LL;
        refresh_us_ = 0;
    }
    void Cancel() { deadline_us_ = 0; }
    bool Active() const { return deadline_us_ != 0; }
    bool Expired(int64_t now_us) const { return Active() && now_us >= deadline_us_; }
    bool RefreshDue(int64_t now_us) const {
        return Active() && !Expired(now_us) && now_us >= refresh_us_;
    }
    int Renew(int64_t now_us) {
        if (!RefreshDue(now_us))
            return 0;
        refresh_us_ = now_us + 250000;
        return std::min(750, static_cast<int>((deadline_us_ - now_us + 999) / 1000));
    }

private:
    int64_t deadline_us_ = 0;
    int64_t refresh_us_ = 0;
};
}  // namespace walle
