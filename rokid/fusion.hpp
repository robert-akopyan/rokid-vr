#pragma once
#include "common/math.hpp"
#include <cstdint>

namespace rokidvr {
class Fusion {
public:
    struct Result { Quat orientation{}; bool calibrated{}; bool bias_active{}; };
    void reset();
    Result update(std::uint64_t timestamp_ns,Vec3 acceleration,Vec3 gyroscope);
    Result current() const { return {orientation_,calibrated_,bias_active_}; }
private:
    void calibrate(std::uint64_t,Vec3,Vec3);
    bool calibrated_{}; bool bias_active_{}; std::uint64_t warmup_{},window_{},last_{}; unsigned samples_{};
    Vec3 gyro_sum_{},gyro_sq_{},accel_sum_{}; double accel_norm_sum_{},accel_norm_sq_{},gravity_{9.82},stationary_{};
    Vec3 bias_{}; Quat orientation_{};
};
}

