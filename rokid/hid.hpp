#pragma once
#include "common/math.hpp"
#include "rokid/fusion.hpp"
#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace rokidvr {
class HidTracker {
public:
    struct Snapshot { Quat orientation{}; Vec3 angular_velocity{}; std::wstring path,serial; bool present{},connected{},calibrated{}; unsigned packet_rate{}; DWORD error{}; };
    using PoseCallback=std::function<void(const Snapshot&)>;
    ~HidTracker(); void start(PoseCallback callback={}); void stop(); Snapshot snapshot() const; static bool present(); static std::wstring serial_number();
private:
    void run(); HANDLE open(std::wstring&,std::wstring&,USHORT&,bool&,DWORD&); bool read_loop(HANDLE,USHORT); bool decode(const unsigned char*,std::size_t);
    mutable std::mutex mutex_; Snapshot state_; Fusion fusion_; Vec3 latest_accel_{}; std::uint64_t accel_ts_{},rate_epoch_{};unsigned packets_{};PoseCallback callback_;
    std::mutex handle_mutex_;HANDLE handle_{INVALID_HANDLE_VALUE};std::atomic_bool stop_{};std::thread thread_;
};
}
