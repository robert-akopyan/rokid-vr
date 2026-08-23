#include "rokid/fusion.hpp"
#include <algorithm>
#include <cmath>

namespace rokidvr {
// Independent quaternion complementary filter. Rokid packet units and gravity
// direction were checked against the BSL-1.0 Monado Rokid driver; no Monado or
// Phoenix source is compiled into this project.
void Fusion::reset(){*this={};}
void Fusion::calibrate(std::uint64_t ts,Vec3 a,Vec3 g){
    constexpr std::uint64_t warm=500000000, duration=2000000000;
    if(!warmup_){warmup_=ts;return;} if(ts-warmup_<warm)return; if(!window_)window_=ts;
    gyro_sum_.x+=g.x;gyro_sum_.y+=g.y;gyro_sum_.z+=g.z;gyro_sq_.x+=g.x*g.x;gyro_sq_.y+=g.y*g.y;gyro_sq_.z+=g.z*g.z;
    accel_sum_.x+=a.x;accel_sum_.y+=a.y;accel_sum_.z+=a.z;const double an=length(a);accel_norm_sum_+=an;accel_norm_sq_+=an*an;++samples_;
    if(samples_<12||ts-window_<duration)return; const double inv=1.0/samples_;
    const Vec3 mg{gyro_sum_.x*inv,gyro_sum_.y*inv,gyro_sum_.z*inv};
    const double gs=std::sqrt(std::max({0.0,gyro_sq_.x*inv-mg.x*mg.x,gyro_sq_.y*inv-mg.y*mg.y,gyro_sq_.z*inv-mg.z*mg.z}));
    const double ma=accel_norm_sum_*inv;const double as=ma>1e-6?std::sqrt(std::max(0.0,accel_norm_sq_*inv-ma*ma))/ma:1.0;
    if(gs>.03||as>.05){window_=0;samples_=0;gyro_sum_={};gyro_sq_={};accel_sum_={};accel_norm_sum_=accel_norm_sq_=0;return;}
    bias_=mg;const Vec3 mean{accel_sum_.x*inv,accel_sum_.y*inv,accel_sum_.z*inv};gravity_=length(mean);if(gravity_<1e-6)gravity_=9.82;
    else{const Vec3 measured{mean.x/gravity_,mean.y/gravity_,mean.z/gravity_};const double dot=std::clamp(measured.y,-1.0,1.0);orientation_=dot<-.999999?Quat{0,1,0,0}:normalize({1+dot,-measured.z,0,measured.x});}
    calibrated_=true;last_=0;
}
Fusion::Result Fusion::update(std::uint64_t ts,Vec3 a,Vec3 g){
    if(!std::isfinite(a.x)||!std::isfinite(a.y)||!std::isfinite(a.z)||!std::isfinite(g.x)||!std::isfinite(g.y)||!std::isfinite(g.z))return current();
    if(!calibrated_){calibrate(ts,a,g);return current();} if(!last_){last_=ts;return current();} if(ts<=last_)return current();
    const double dt=static_cast<double>(ts-last_)/1e9;last_=ts;if(dt<=0||dt>.2)return current();
    Vec3 omega{g.x-bias_.x,g.y-bias_.y,g.z-bias_.z};const double an=length(a),gn=length(omega),ae=std::abs(an-gravity_)/gravity_;
    if(ae<=.18&&gn<=.05){stationary_=std::min(4.0,stationary_+dt);bias_active_=stationary_>=.5;if(bias_active_){const double kxy=1-std::exp(-dt/60),kz=1-std::exp(-dt/18);bias_.x+=(g.x-bias_.x)*kxy;bias_.y+=(g.y-bias_.y)*kxy;bias_.z+=(g.z-bias_.z)*kz;omega={g.x-bias_.x,g.y-bias_.y,g.z-bias_.z};}}
    else{stationary_=std::max(0.0,stationary_-dt*2);bias_active_=false;}
    if(an>1e-6&&ae<=.18){const Vec3 measured{a.x/an,a.y/an,a.z/an};const Vec3 predicted=rotate(conjugate(orientation_),{0,1,0});omega.x+=2*(measured.y*predicted.z-measured.z*predicted.y);omega.y+=2*(measured.z*predicted.x-measured.x*predicted.z);omega.z+=2*(measured.x*predicted.y-measured.y*predicted.x);}
    const Quat d=multiply(orientation_,{0,omega.x,omega.y,omega.z});orientation_=normalize({orientation_.w+.5*d.w*dt,orientation_.x+.5*d.x*dt,orientation_.y+.5*d.y*dt,orientation_.z+.5*d.z*dt});return current();
}
}
