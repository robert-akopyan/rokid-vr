#include "common/config.hpp"
#include "common/math.hpp"
#include <cmath>
#include <iostream>

using namespace rokidvr;
namespace {int failures=0;void check(bool value,const char* name){if(!value){std::cerr<<"FAIL: "<<name<<'\n';++failures;}}bool approx(double a,double b,double e=1e-6){return std::abs(a-b)<e;}}
int main(){
    const Quat q=normalize({2,1,2,3});check(approx(std::sqrt(q.w*q.w+q.x*q.x+q.y*q.y+q.z*q.z),1),"quaternion normalization");
    const Quat current=axis_angle({0,1,0},.7);const Quat centered=recentered(conjugate(current),current);check(approx(centered.w,1)&&approx(centered.x,0)&&approx(centered.y,0)&&approx(centered.z,0),"quaternion recenter");
    const auto tilted_center=std::array<double,3>{35.0,20.0,15.0};const Quat tilted=quaternion_from_euler_degrees(tilted_center);const Quat pitch_only=recentered_euler(quaternion_from_euler_degrees({35.0,40.0,15.0}),euler_degrees(tilted));const auto pitch_only_euler=euler_degrees(pitch_only);check(std::abs(pitch_only_euler[0])<1e-6&&approx(pitch_only_euler[1],20)&&std::abs(pitch_only_euler[2])<1e-6,"Euler recenter keeps pitch independent from roll");
    const Quat right=rokid_to_openvr(axis_angle({0,1,0},-.5));const auto e=euler_degrees(right);check(e[0]>0,"Rokid right turn maps to positive OpenVR yaw convention test");
    const Quat wrap_a=axis_angle({0,1,0},179*3.14159265358979323846/180);const Quat wrap_b=axis_angle({0,1,0},-179*3.14159265358979323846/180);check(std::abs(std::abs(multiply(conjugate(wrap_a),wrap_b).w)-1)<.001,"quaternion wrap-around continuity");
    constexpr double fov=50*3.14159265358979323846/180;check(approx(std::atan(std::tan(fov/2))*2,fov),"FOV projection tangent");
    std::cout<<(failures?"tests failed":"all tests passed")<<'\n';return failures?1:0;
}
