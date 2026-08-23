#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace rokidvr {

struct Vec3 { double x{}, y{}, z{}; };
struct Quat { double w{1}, x{}, y{}, z{}; };

inline double length(Vec3 v) { return std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z); }
inline Quat normalize(Quat q) {
    const double n = std::sqrt(q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z);
    return (!std::isfinite(n) || n < 1e-12) ? Quat{} : Quat{q.w/n,q.x/n,q.y/n,q.z/n};
}
inline Quat conjugate(Quat q) { return {q.w,-q.x,-q.y,-q.z}; }
inline Quat multiply(Quat a, Quat b) {
    return {a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z,
            a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
            a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
            a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w};
}
inline Vec3 rotate(Quat q, Vec3 v) {
    q=normalize(q); const Vec3 t{2*(q.y*v.z-q.z*v.y),2*(q.z*v.x-q.x*v.z),2*(q.x*v.y-q.y*v.x)};
    return {v.x+q.w*t.x+(q.y*t.z-q.z*t.y),v.y+q.w*t.y+(q.z*t.x-q.x*t.z),v.z+q.w*t.z+(q.x*t.y-q.y*t.x)};
}
inline Quat axis_angle(Vec3 axis, double radians) {
    const double n=length(axis); if(n<1e-12) return {}; const double s=std::sin(radians/2)/n;
    return normalize({std::cos(radians/2),axis.x*s,axis.y*s,axis.z*s});
}
inline std::array<double,3> euler_degrees(Quat q) {
    q=normalize(q); constexpr double d=180.0/3.14159265358979323846;
    const double pitch=std::asin(std::clamp(2*(q.x*q.w-q.y*q.z),-1.0,1.0));
    const double yaw=std::atan2(2*(q.x*q.z+q.y*q.w),1-2*(q.x*q.x+q.y*q.y));
    const double roll=std::atan2(2*(q.x*q.y+q.z*q.w),1-2*(q.x*q.x+q.z*q.z));
    return {yaw*d,pitch*d,roll*d};
}
inline Quat quaternion_from_euler_degrees(std::array<double,3> ypr) {
    constexpr double r=3.14159265358979323846/180.0;
    return normalize(multiply(multiply(axis_angle({0,1,0},ypr[0]*r),axis_angle({1,0,0},ypr[1]*r)),axis_angle({0,0,1},ypr[2]*r)));
}
inline double angle_delta_degrees(double current,double center) {
    double delta=std::fmod(current-center+180.0,360.0);if(delta<0)delta+=360.0;return delta-180.0;
}
inline Quat recentered_euler(Quat current,std::array<double,3> center) {
    const auto value=euler_degrees(current);
    return quaternion_from_euler_degrees({angle_delta_degrees(value[0],center[0]),angle_delta_degrees(value[1],center[1]),angle_delta_degrees(value[2],center[2])});
}

// One centralized sensor-to-OpenVR convention transform. Rokid's verified
// tracker convention reports the inverse body rotation for yaw/pitch/roll.
inline Quat rokid_to_openvr(Quat sensor_body_to_world) { return normalize(conjugate(sensor_body_to_world)); }
inline Quat recentered(Quat center_inverse, Quat current) { return normalize(multiply(center_inverse,current)); }

} // namespace rokidvr
