#include "rokid/hid.hpp"
#include "common/log.hpp"
#include <SetupAPI.h>
#include <hidsdi.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>

namespace rokidvr { namespace {
constexpr USHORT vid=0x04d2,pid=0x162f; constexpr std::size_t payload=64;
template<class T>bool readv(const unsigned char* p,std::size_t n,std::size_t o,T& v){if(o>n||n-o<sizeof(T))return false;std::memcpy(&v,p+o,sizeof(T));return true;}
std::vector<std::wstring> paths(){std::vector<std::wstring> r;GUID g{};HidD_GetHidGuid(&g);HDEVINFO d=SetupDiGetClassDevsW(&g,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);if(d==INVALID_HANDLE_VALUE)return r;for(DWORD i=0;;++i){SP_DEVICE_INTERFACE_DATA x{sizeof(x)};if(!SetupDiEnumDeviceInterfaces(d,nullptr,&g,i,&x)){if(GetLastError()==ERROR_NO_MORE_ITEMS)break;continue;}DWORD need=0;SetupDiGetDeviceInterfaceDetailW(d,&x,nullptr,0,&need,nullptr);std::vector<unsigned char>b(need);auto* detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(b.data());detail->cbSize=sizeof(*detail);if(SetupDiGetDeviceInterfaceDetailW(d,&x,detail,need,nullptr,nullptr)){std::wstring p=detail->DevicePath,t=p;std::transform(t.begin(),t.end(),t.begin(),::towlower);if(t.find(L"vid_04d2")!=std::wstring::npos&&t.find(L"pid_162f")!=std::wstring::npos)r.push_back(std::move(p));}}SetupDiDestroyDeviceInfoList(d);return r;}
}
HidTracker::~HidTracker(){stop();} bool HidTracker::present(){return !paths().empty();}
std::wstring HidTracker::serial_number(){for(const auto&p:paths()){HANDLE h=CreateFileW(p.c_str(),0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);if(h==INVALID_HANDLE_VALUE)continue;HIDD_ATTRIBUTES a{sizeof(a)};wchar_t s[256]{};const bool ok=HidD_GetAttributes(h,&a)&&a.VendorID==vid&&a.ProductID==pid&&HidD_GetSerialNumberString(h,s,sizeof(s));CloseHandle(h);if(ok&&*s)return s;}return {};}
void HidTracker::start(PoseCallback cb){if(thread_.joinable())return;callback_=std::move(cb);stop_=false;thread_=std::thread(&HidTracker::run,this);}
void HidTracker::stop(){stop_=true;{std::lock_guard l(handle_mutex_);if(handle_!=INVALID_HANDLE_VALUE)CancelIoEx(handle_,nullptr);}if(thread_.joinable())thread_.join();}
HidTracker::Snapshot HidTracker::snapshot()const{std::lock_guard l(mutex_);return state_;}
HANDLE HidTracker::open(std::wstring& selected,std::wstring& serial,USHORT& report,bool& is_present,DWORD& error){auto list=paths();is_present=!list.empty();std::stable_sort(list.begin(),list.end(),[](auto&a,auto&b){return a.find(L"mi_02")!=std::wstring::npos&&b.find(L"mi_02")==std::wstring::npos;});for(const auto&p:list){HANDLE h=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);if(h==INVALID_HANDLE_VALUE){error=GetLastError();continue;}HIDD_ATTRIBUTES a{sizeof(a)};PHIDP_PREPARSED_DATA prep=nullptr;HIDP_CAPS caps{};if(!HidD_GetAttributes(h,&a)||a.VendorID!=vid||a.ProductID!=pid||!HidD_GetPreparsedData(h,&prep)||HidP_GetCaps(prep,&caps)!=HIDP_STATUS_SUCCESS||caps.InputReportByteLength<payload){if(prep)HidD_FreePreparsedData(prep);CloseHandle(h);continue;}HidD_FreePreparsedData(prep);wchar_t s[256]{};if(HidD_GetSerialNumberString(h,s,sizeof(s)))serial=s;selected=p;report=caps.InputReportByteLength;return h;}return INVALID_HANDLE_VALUE;}
void HidTracker::run(){
    while(!stop_){
        std::wstring p,s;USHORT n=0;bool pr=false;DWORD e=0;HANDLE h=open(p,s,n,pr,e);
        if(h==INVALID_HANDLE_VALUE){
            {std::lock_guard l(mutex_);state_.present=pr;state_.connected=false;state_.calibrated=false;state_.packet_rate=0;state_.error=e;}
            for(int i=0;i<10&&!stop_;++i)std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        {std::lock_guard l(handle_mutex_);handle_=h;}
        {std::lock_guard l(mutex_);state_={};state_.present=state_.connected=true;state_.path=p;state_.serial=s;fusion_.reset();latest_accel_={};accel_ts_=0;packets_=0;rate_epoch_=GetTickCount64();}
        log("Rokid HID connected: "+narrow(p));read_loop(h,n);
        {std::lock_guard l(handle_mutex_);if(handle_==h)handle_=INVALID_HANDLE_VALUE;}
        CloseHandle(h);
        {std::lock_guard l(mutex_);state_.connected=false;state_.calibrated=false;state_.packet_rate=0;}
        log("Rokid HID disconnected");
    }
}
bool HidTracker::read_loop(HANDLE h,USHORT n){std::vector<unsigned char>b(n);HANDLE ev=CreateEventW(nullptr,TRUE,FALSE,nullptr);while(!stop_){ResetEvent(ev);OVERLAPPED ov{};ov.hEvent=ev;DWORD got=0;BOOL ok=ReadFile(h,b.data(),n,&got,&ov);if(!ok&&GetLastError()==ERROR_IO_PENDING){DWORD w=WaitForSingleObject(ev,250);if(w==WAIT_TIMEOUT){CancelIoEx(h,&ov);WaitForSingleObject(ev,INFINITE);continue;}ok=w==WAIT_OBJECT_0&&GetOverlappedResult(h,&ov,&got,FALSE);}if(!ok)break;if(got>=payload)decode(b.data(),got);}CancelIoEx(h,nullptr);CloseHandle(ev);return !stop_;}
bool HidTracker::decode(const unsigned char* r,std::size_t n){std::size_t b=0;if(r[0]!=4&&r[0]!=17){if(n<=payload||(r[1]!=4&&r[1]!=17))return false;b=1;}if(n-b<payload)return false;const auto type=r[b];std::uint64_t ts=0;if(!readv(r,n,type==4?b+9:b+1,ts)||!ts)return false;Snapshot copy;bool has_pose=false;{std::lock_guard l(mutex_);Fusion::Result value;
 if(type==4){float x,y,z;if(!readv(r,n,b+21,x)||!readv(r,n,b+25,y)||!readv(r,n,b+29,z)||!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z))return false;const auto t=ts*1000ULL;switch(r[b+1]){case 1:latest_accel_={x,y,z};accel_ts_=t;break;case 2:if(!accel_ts_)return false;state_.angular_velocity={x,y,z};value=fusion_.update(t,latest_accel_,{x,y,z});has_pose=true;break;case 3:break;default:return false;}}
 else{float ax,ay,az,gx,gy,gz;if(!readv(r,n,b+9,ax)||!readv(r,n,b+13,ay)||!readv(r,n,b+17,az)||!readv(r,n,b+21,gx)||!readv(r,n,b+25,gy)||!readv(r,n,b+29,gz)||!std::isfinite(ax)||!std::isfinite(ay)||!std::isfinite(az)||!std::isfinite(gx)||!std::isfinite(gy)||!std::isfinite(gz))return false;state_.angular_velocity={gx,gy,gz};value=fusion_.update(ts,{ax,ay,az},{gx,gy,gz});has_pose=true;}
 if(has_pose){state_.orientation=rokid_to_openvr(value.orientation);state_.calibrated=value.calibrated;}++packets_;const auto now=GetTickCount64();if(now-rate_epoch_>=1000){state_.packet_rate=static_cast<unsigned>(packets_*1000ULL/std::max<std::uint64_t>(1,now-rate_epoch_));packets_=0;rate_epoch_=now;}copy=state_;}
 if(callback_&&has_pose&&copy.calibrated)callback_(copy);return true;}
}
