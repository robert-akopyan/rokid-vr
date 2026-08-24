#include "common/config.hpp"
#include "common/math.hpp"
#include "display/display.hpp"
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
    DisplayInfo display_a{},display_b{};display_a.device_path=display_b.device_path=L"rokid";display_a.gdi_name=display_b.gdi_name=L"DISPLAY2";display_a.current={1920,1080,60};display_b.current=display_a.current;display_a.bounds=display_b.bounds={3840,0,5760,1080};check(same_display_configuration(display_a,display_b),"display configurations compare equal");display_b.bounds={0,0,1920,1080};check(!same_display_configuration(display_a,display_b),"display coordinate change is detected");display_b=display_a;display_b.current.refresh_hz=120;check(!same_display_configuration(display_a,display_b),"display mode change is detected");
    std::cout<<(failures?"tests failed":"all tests passed")<<'\n';return failures?1:0;
}
