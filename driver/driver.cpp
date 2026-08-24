#include <openvr_driver.h>
#include "common/config.hpp"
#include "common/log.hpp"
#include "common/math.hpp"
#include "display/display.hpp"
#include "display/presenter.hpp"
#include "rokid/hid.hpp"
#include <Windows.h>
#include <TlHelp32.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

namespace rokidvr {
class HmdDevice final : public vr::ITrackedDeviceServerDriver, public vr::IVRDisplayComponent, public vr::IVRVirtualDisplay {
public:
    HmdDevice():config_(load_config()) {
        display_found_=select_rokid_display(config_.display_id,display_);
        if(display_found_)log("Selected display: "+narrow(describe_display(display_)));else log("Rokid display not found");
    }
    ~HmdDevice() { stop(); }
    vr::EVRInitError Activate(std::uint32_t id) override {
        object_id_=id;auto props=vr::VRProperties()->TrackedDeviceToPropertyContainer(id);
        vr::VRProperties()->SetStringProperty(props,vr::Prop_TrackingSystemName_String,"rokidvr");
        vr::VRProperties()->SetStringProperty(props,vr::Prop_ModelNumber_String,"Rokid Max");
        vr::VRProperties()->SetStringProperty(props,vr::Prop_ManufacturerName_String,"Rokid");
        vr::VRProperties()->SetStringProperty(props,vr::Prop_RenderModelName_String,"generic_hmd");
        vr::VRProperties()->SetBoolProperty(props,vr::Prop_HasDisplayComponent_Bool,true);
        // In direct mode SteamVR owns the physical Rokid output. Extended and
        // virtual-presenter modes keep it in the Windows desktop topology.
        vr::VRProperties()->SetBoolProperty(props,vr::Prop_IsOnDesktop_Bool,config_.video_path!=VideoPath::direct);
        vr::VRProperties()->SetBoolProperty(props,vr::Prop_WillDriftInYaw_Bool,true);
        vr::VRProperties()->SetBoolProperty(props,vr::Prop_DeviceIsWireless_Bool,false);
        vr::VRProperties()->SetFloatProperty(props,vr::Prop_UserIpdMeters_Float,static_cast<float>(config_.ipd_mm/1000.0));
        vr::VRProperties()->SetFloatProperty(props,vr::Prop_DisplayFrequency_Float,static_cast<float>(display_.current.refresh_hz>1?display_.current.refresh_hz:60));
        vr::VRProperties()->SetFloatProperty(props,vr::Prop_SecondsFromVsyncToPhotons_Float,0.011f);
        // EDID reported by NVIDIA for the connected Rokid Max. SteamVR uses
        // these properties to associate a direct-mode HMD with its hidden output.
        vr::VRProperties()->SetInt32Property(props,vr::Prop_EdidVendorID_Int32,0x4DD9);
        vr::VRProperties()->SetInt32Property(props,vr::Prop_EdidProductID_Int32,0x07C2);
        vr::VRProperties()->SetUint64Property(props,vr::Prop_CurrentUniverseId_Uint64,2);
        std::uint64_t luid=0;static_assert(sizeof(luid)==sizeof(display_.adapter_luid));std::memcpy(&luid,&display_.adapter_luid,sizeof(luid));
        if(display_found_)vr::VRProperties()->SetUint64Property(props,vr::Prop_GraphicsAdapterLuid_Uint64,luid);
        if(config_.video_path==VideoPath::direct){
            vr::EVRSettingsError error=vr::VRSettingsError_None;
            vr::VRSettings()->SetBool("direct_mode","enable",true,&error);
            vr::VRSettings()->SetInt32("direct_mode","count",1,&error);
            // direct_mode settings address NVIDIA's raw EDID byte order, while
            // the HMD properties above use OpenVR's normalized integer order.
            vr::VRSettings()->SetInt32("direct_mode","edidVid",0xD94D,&error);
            vr::VRSettings()->SetInt32("direct_mode","edidPid",0xC207,&error);
            log("Direct display configured for NVIDIA EDID D94D:C207");
        }
        if(config_.video_path==VideoPath::virtual_display){
            const std::int32_t width=static_cast<std::int32_t>(display_.current.width?display_.current.width:1920);
            const std::int32_t height=static_cast<std::int32_t>(display_.current.height?display_.current.height:1080);
            const double refresh=display_.current.refresh_hz>1?display_.current.refresh_hz:60.0;
            vr::EVRSettingsError error=vr::VRSettingsError_None;
            vr::VRSettings()->SetInt32("driver_rokidmax","displayWidth",width,&error);
            vr::VRSettings()->SetInt32("driver_rokidmax","displayHeight",height,&error);
            vr::VRSettings()->SetInt32("driver_rokidmax","displayRefreshRateNumerator",static_cast<std::int32_t>(std::round(refresh*1000.0)),&error);
            vr::VRSettings()->SetInt32("driver_rokidmax","displayRefreshRateDenominator",1000,&error);
            log("Virtual display backbuffer configured: "+std::to_string(width)+"x"+std::to_string(height)+" @ "+std::to_string(refresh)+" Hz");
        }
        stop_=false;tracker_.start([this](const HidTracker::Snapshot&s){on_pose(s);});pipe_thread_=std::thread(&HmdDevice::pipe_loop,this);log("HMD activated; yaw/pitch view gain "+std::to_string(config_.view_gain)+"x, roll gain 1.0x");return vr::VRInitError_None;
    }
    void Deactivate() override {stop();object_id_=vr::k_unTrackedDeviceIndexInvalid;}
    void EnterStandby() override {}
    void* GetComponent(const char* name) override {
        if(!_stricmp(name,vr::IVRDisplayComponent_Version))return static_cast<vr::IVRDisplayComponent*>(this);
        return nullptr;
    }
    void DebugRequest(const char* request,char* response,std::uint32_t size) override {if(!size)return;if(request&&!_stricmp(request,"recenter")){recenter();strcpy_s(response,size,"ok");}else strcpy_s(response,size,"Rokid Max 3DoF HMD");}
    vr::DriverPose_t GetPose() override {std::lock_guard lock(pose_mutex_);return pose_;}
    vr::DriverPose_t pose_snapshot(){std::lock_guard lock(pose_mutex_);return pose_;}
    void run_frame(){
        const bool recenter_hotkey=(GetAsyncKeyState(VK_CONTROL)&0x8000)&&(GetAsyncKeyState(VK_MENU)&0x8000)&&(GetAsyncKeyState('R')&0x8000);
        if(recenter_hotkey&&!recenter_hotkey_down_)recenter();
        recenter_hotkey_down_=recenter_hotkey;
        const auto s=tracker_.snapshot();if(s.connected!=last_connected_){last_connected_=s.connected;if(!s.connected){auto p=make_pose(s);std::lock_guard lock(pose_mutex_);pose_=p;if(object_id_!=vr::k_unTrackedDeviceIndexInvalid)vr::VRServerDriverHost()->TrackedDevicePoseUpdated(object_id_,p,sizeof(p));}}
    }

    void GetWindowBounds(std::int32_t*x,std::int32_t*y,std::uint32_t*w,std::uint32_t*h) override {*x=display_.bounds.left;*y=display_.bounds.top;*w=display_.current.width?display_.current.width:1920;*h=display_.current.height?display_.current.height:1080;}
    bool IsDisplayOnDesktop() override {const bool value=config_.video_path!=VideoPath::direct;if(!desktop_query_logged_){log(std::string("IsDisplayOnDesktop -> ")+(value?"true":"false"));desktop_query_logged_=true;}return value;}
    bool IsDisplayRealDisplay() override {const bool value=config_.video_path!=VideoPath::virtual_display;if(!real_query_logged_){log(std::string("IsDisplayRealDisplay -> ")+(value?"true":"false"));real_query_logged_=true;}return value;}
    void GetRecommendedRenderTargetSize(std::uint32_t*w,std::uint32_t*h) override {*w=display_.current.width>=3000?display_.current.width/2:(display_.current.width?display_.current.width:1920);*h=display_.current.height?display_.current.height:1080;}
    void GetEyeOutputViewport(vr::EVREye eye,std::uint32_t*x,std::uint32_t*y,std::uint32_t*w,std::uint32_t*h) override {const std::uint32_t width=display_.current.width?display_.current.width:1920;*w=width/2;*h=display_.current.height?display_.current.height:1080;*y=0;bool right=eye==vr::Eye_Right;if(config_.swap_eyes)right=!right;*x=right?*w:0;}
    void GetProjectionRaw(vr::EVREye,float*l,float*r,float*t,float*b) override {constexpr float fov=50.0f;const float tangent=std::tan(fov*3.14159265358979323846f/360.0f);*l=-tangent;*r=tangent;const float aspect=1920.0f/1080.0f;*t=-tangent/aspect;*b=tangent/aspect;}
    vr::DistortionCoordinates_t ComputeDistortion(vr::EVREye,float u,float v) override {return {{u,v},{u,v},{u,v}};}
    bool ComputeInverseDistortion(vr::HmdVector2_t* result,vr::EVREye,std::uint32_t,float u,float v) override {if(result)*result={{u,v}};return true;}

    void Present(const vr::PresentInfo_t* info,std::uint32_t size) override {if(!info||size<sizeof(vr::PresentInfo_t)||!display_found_)return;if(!presenter_)presenter_=std::make_unique<Presenter>(display_,config_);presenter_->present(reinterpret_cast<HANDLE>(info->backbufferTextureHandle));}
    void WaitForPresent() override {if(presenter_)presenter_->wait_for_present();}
    bool GetTimeSinceLastVsync(float*s,std::uint64_t*f) override {return presenter_&&presenter_->time_since_vsync(s,f);}
private:
    void stop(){stop_=true;tracker_.stop();if(pipe_thread_.joinable())pipe_thread_.join();presenter_.reset();}
    vr::DriverPose_t make_pose(const HidTracker::Snapshot&s){vr::DriverPose_t p{};p.qWorldFromDriverRotation.w=1;p.qDriverFromHeadRotation.w=1;p.deviceIsConnected=s.connected;p.poseIsValid=s.connected&&s.calibrated;p.result=p.poseIsValid?vr::TrackingResult_Running_OK:vr::TrackingResult_Uninitialized;Quat q=conjugate(s.orientation);{std::lock_guard lock(center_mutex_);if(!centered_&&s.calibrated){center_angles_=euler_degrees(q);centered_=true;}if(centered_)q=recentered_euler(q,center_angles_,config_.view_gain);}p.qRotation={q.w,q.x,q.y,q.z};p.vecAngularVelocity[0]=s.angular_velocity.x*config_.view_gain;p.vecAngularVelocity[1]=s.angular_velocity.y*config_.view_gain;p.vecAngularVelocity[2]=s.angular_velocity.z;return p;}
    void on_pose(const HidTracker::Snapshot&s){const auto p=make_pose(s);{std::lock_guard lock(pose_mutex_);pose_=p;}if(object_id_!=vr::k_unTrackedDeviceIndexInvalid)vr::VRServerDriverHost()->TrackedDevicePoseUpdated(object_id_,p,sizeof(p));}
    void recenter(){const auto s=tracker_.snapshot();if(!s.calibrated){log("Recenter ignored: tracker is not calibrated");return;}{std::lock_guard lock(center_mutex_);center_angles_=euler_degrees(conjugate(s.orientation));centered_=true;}log("Recenter without axis rotation");on_pose(s);}
    void pipe_loop(){while(!stop_){HANDLE pipe=CreateNamedPipeW(L"\\\\.\\pipe\\RokidVR",PIPE_ACCESS_INBOUND,PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_NOWAIT,1,64,64,100,nullptr);if(pipe==INVALID_HANDLE_VALUE){Sleep(500);continue;}while(!stop_){BOOL connected=ConnectNamedPipe(pipe,nullptr);DWORD e=connected?ERROR_SUCCESS:GetLastError();if(connected||e==ERROR_PIPE_CONNECTED){char command[64]{};DWORD n=0;if(ReadFile(pipe,command,sizeof(command)-1,&n,nullptr)&&!_strnicmp(command,"recenter",8))recenter();DisconnectNamedPipe(pipe);break;}if(e!=ERROR_PIPE_LISTENING&&e!=ERROR_NO_DATA)break;Sleep(50);}CloseHandle(pipe);}}
    Config config_;DisplayInfo display_{};bool display_found_{};HidTracker tracker_;std::unique_ptr<Presenter>presenter_;std::uint32_t object_id_{vr::k_unTrackedDeviceIndexInvalid};
    std::mutex pose_mutex_,center_mutex_;vr::DriverPose_t pose_{};std::array<double,3> center_angles_{};bool centered_{},last_connected_{},desktop_query_logged_{},real_query_logged_{},recenter_hotkey_down_{};std::atomic_bool stop_{};std::thread pipe_thread_;
};

class MouseControllerDevice final : public vr::ITrackedDeviceServerDriver {
public:
    explicit MouseControllerDevice(HmdDevice* hmd):hmd_(hmd){}
    vr::EVRInitError Activate(std::uint32_t id) override {
        object_id_=id;
        const auto props=vr::VRProperties()->TrackedDeviceToPropertyContainer(id);
        vr::VRProperties()->SetStringProperty(props,vr::Prop_TrackingSystemName_String,"rokidvr");
        vr::VRProperties()->SetStringProperty(props,vr::Prop_ModelNumber_String,"Rokid Mouse Controller");
        vr::VRProperties()->SetStringProperty(props,vr::Prop_ManufacturerName_String,"RokidVR");
        vr::VRProperties()->SetStringProperty(props,vr::Prop_ControllerType_String,"vive_controller");
        vr::VRProperties()->SetStringProperty(props,vr::Prop_InputProfilePath_String,"{htc}/input/vive_controller_profile.json");
        vr::VRProperties()->SetStringProperty(props,vr::Prop_RenderModelName_String,"{htc}vr_controller_vive_1_5");
        vr::VRProperties()->SetStringProperty(props,vr::Prop_RegisteredDeviceType_String,"rokidvr/mouse_controller");
        vr::VRProperties()->SetInt32Property(props,vr::Prop_ControllerRoleHint_Int32,vr::TrackedControllerRole_RightHand);
        auto* input=vr::VRDriverInput();
        input->CreateBooleanComponent(props,"/input/trigger/click",&trigger_click_);
        input->CreateScalarComponent(props,"/input/trigger/value",&trigger_value_,vr::VRScalarType_Absolute,vr::VRScalarUnits_NormalizedOneSided);
        input->CreateBooleanComponent(props,"/input/application_menu/click",&menu_click_);
        input->CreateBooleanComponent(props,"/input/system/click",&system_click_);
        input->CreateBooleanComponent(props,"/input/grip/click",&grip_click_);
        input->CreateBooleanComponent(props,"/input/trackpad/click",&trackpad_click_);
        input->CreateBooleanComponent(props,"/input/trackpad/touch",&trackpad_touch_);
        input->CreateScalarComponent(props,"/input/trackpad/x",&trackpad_x_,vr::VRScalarType_Absolute,vr::VRScalarUnits_NormalizedTwoSided);
        input->CreateScalarComponent(props,"/input/trackpad/y",&trackpad_y_,vr::VRScalarType_Absolute,vr::VRScalarUnits_NormalizedTwoSided);
        active_=true;log("Mouse controller activated");return vr::VRInitError_None;
    }
    void Deactivate() override {active_=false;object_id_=vr::k_unTrackedDeviceIndexInvalid;}
    void EnterStandby() override {}
    void* GetComponent(const char*) override {return nullptr;}
    void DebugRequest(const char*,char* response,std::uint32_t size) override {if(size)response[0]=0;}
    vr::DriverPose_t GetPose() override {return pose_;}
    void set_dashboard_visible(bool visible){dashboard_visible_=visible;}
    void run_frame(){
        if(!active_||!hmd_)return;
        const auto now=GetTickCount64();
        if(now-last_process_check_>=500){
            last_process_check_=now;const bool running=process_running(L"aces.exe");
            if(running!=game_running_){game_running_=running;log(std::string("Mouse controller ")+(running?"suspended for War Thunder":"resumed after War Thunder"));}
        }
        const bool toggle=(GetAsyncKeyState(VK_CONTROL)&0x8000)&&(GetAsyncKeyState(VK_MENU)&0x8000)&&(GetAsyncKeyState('M')&0x8000);
        if(toggle&&!toggle_down_){enabled_=!enabled_;log(std::string("Mouse controller ")+(enabled_?"enabled":"disabled")+" by Ctrl+Alt+M");}
        toggle_down_=toggle;
        if(!enabled_||(game_running_&&!dashboard_visible_)){
            pose_.deviceIsConnected=false;pose_.poseIsValid=false;pose_.result=vr::TrackingResult_Uninitialized;
            vr::VRServerDriverHost()->TrackedDevicePoseUpdated(object_id_,pose_,sizeof(pose_));
            auto* input=vr::VRDriverInput();input->UpdateBooleanComponent(trigger_click_,false,0);input->UpdateScalarComponent(trigger_value_,0,0);
            input->UpdateBooleanComponent(menu_click_,false,0);input->UpdateBooleanComponent(system_click_,false,0);input->UpdateBooleanComponent(grip_click_,false,0);
            input->UpdateBooleanComponent(trackpad_click_,false,0);input->UpdateBooleanComponent(trackpad_touch_,false,0);return;
        }
        POINT cursor{};GetCursorPos(&cursor);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromPoint(cursor,MONITOR_DEFAULTTONEAREST),&monitor);
        const double width=std::max<LONG>(1,monitor.rcMonitor.right-monitor.rcMonitor.left),height=std::max<LONG>(1,monitor.rcMonitor.bottom-monitor.rcMonitor.top);
        const double nx=std::clamp(2.0*(cursor.x-monitor.rcMonitor.left)/width-1.0,-1.0,1.0);
        const double ny=std::clamp(2.0*(cursor.y-monitor.rcMonitor.top)/height-1.0,-1.0,1.0);
        const auto head=hmd_->pose_snapshot();Quat head_q{head.qRotation.w,head.qRotation.x,head.qRotation.y,head.qRotation.z};
        constexpr double radians=3.14159265358979323846/180.0;
        const Quat local=multiply(axis_angle({0,1,0},-nx*38.0*radians),axis_angle({1,0,0},-ny*28.0*radians));
        const Quat q=normalize(multiply(head_q,local));const Vec3 offset=rotate(head_q,{0.20,-0.20,-0.30});
        vr::DriverPose_t p{};p.qWorldFromDriverRotation.w=1;p.qDriverFromHeadRotation.w=1;p.qRotation={q.w,q.x,q.y,q.z};
        p.vecPosition[0]=head.vecPosition[0]+offset.x;p.vecPosition[1]=head.vecPosition[1]+offset.y;p.vecPosition[2]=head.vecPosition[2]+offset.z;
        p.deviceIsConnected=head.deviceIsConnected;p.poseIsValid=head.poseIsValid;p.result=head.result;pose_=p;
        vr::VRServerDriverHost()->TrackedDevicePoseUpdated(object_id_,pose_,sizeof(pose_));
        const bool left=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0,right=(GetAsyncKeyState(VK_RBUTTON)&0x8000)!=0,middle=(GetAsyncKeyState(VK_MBUTTON)&0x8000)!=0;
        const bool grip=(GetAsyncKeyState(VK_XBUTTON1)&0x8000)!=0;
        auto* input=vr::VRDriverInput();input->UpdateBooleanComponent(trigger_click_,left,0);input->UpdateScalarComponent(trigger_value_,left?1.f:0.f,0);
        input->UpdateBooleanComponent(menu_click_,right,0);input->UpdateBooleanComponent(system_click_,middle,0);input->UpdateBooleanComponent(grip_click_,grip,0);
        input->UpdateBooleanComponent(trackpad_click_,left,0);input->UpdateBooleanComponent(trackpad_touch_,left,0);
        input->UpdateScalarComponent(trackpad_x_,static_cast<float>(nx),0);input->UpdateScalarComponent(trackpad_y_,static_cast<float>(-ny),0);
    }
private:
    static bool process_running(const wchar_t* name){HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE)return false;PROCESSENTRY32W entry{sizeof(entry)};bool found=false;if(Process32FirstW(snapshot,&entry))do{if(!_wcsicmp(entry.szExeFile,name)){found=true;break;}}while(Process32NextW(snapshot,&entry));CloseHandle(snapshot);return found;}
    HmdDevice* hmd_{};std::uint32_t object_id_{vr::k_unTrackedDeviceIndexInvalid};bool active_{},enabled_{true},toggle_down_{},game_running_{},dashboard_visible_{};ULONGLONG last_process_check_{};vr::DriverPose_t pose_{};
    vr::VRInputComponentHandle_t trigger_click_{},trigger_value_{},menu_click_{},system_click_{},grip_click_{},trackpad_click_{},trackpad_touch_{},trackpad_x_{},trackpad_y_{};
};

class DisplayRedirectDevice final : public vr::ITrackedDeviceServerDriver, public vr::IVRVirtualDisplay {
public:
    DisplayRedirectDevice():config_(load_config()){display_found_=select_rokid_display(config_.display_id,display_);}
    ~DisplayRedirectDevice(){presenter_.reset();}
    vr::EVRInitError Activate(std::uint32_t id) override {
        object_id_=id;auto props=vr::VRProperties()->TrackedDeviceToPropertyContainer(id);
        vr::VRProperties()->SetStringProperty(props,vr::Prop_ModelNumber_String,"Rokid Max Display Redirect");
        vr::VRProperties()->SetStringProperty(props,vr::Prop_ManufacturerName_String,"RokidVR");
        vr::VRProperties()->SetBoolProperty(props,vr::Prop_HasVirtualDisplayComponent_Bool,true);
        vr::VRProperties()->SetFloatProperty(props,vr::Prop_SecondsFromVsyncToPhotons_Float,0.011f);
        std::uint64_t luid=0;static_assert(sizeof(luid)==sizeof(display_.adapter_luid));std::memcpy(&luid,&display_.adapter_luid,sizeof(luid));
        if(display_found_)vr::VRProperties()->SetUint64Property(props,vr::Prop_GraphicsAdapterLuid_Uint64,luid);
        log("Virtual display redirect activated");return vr::VRInitError_None;
    }
    void Deactivate() override {presenter_.reset();object_id_=vr::k_unTrackedDeviceIndexInvalid;}
    void EnterStandby() override {}
    void* GetComponent(const char* name) override {if(!_stricmp(name,vr::IVRVirtualDisplay_Version))return static_cast<vr::IVRVirtualDisplay*>(this);return nullptr;}
    void DebugRequest(const char*,char* response,std::uint32_t size) override {if(size)response[0]=0;}
    vr::DriverPose_t GetPose() override {vr::DriverPose_t p{};p.qWorldFromDriverRotation.w=1;p.qDriverFromHeadRotation.w=1;p.qRotation.w=1;p.poseIsValid=true;p.deviceIsConnected=true;p.result=vr::TrackingResult_Running_OK;return p;}
    void Present(const vr::PresentInfo_t* info,std::uint32_t size) override {
        if(!first_present_logged_){log("Virtual display Present called: info size="+std::to_string(size)+(info?", frame="+std::to_string(info->nFrameId):", null info"));first_present_logged_=true;}
        if(!info||size<sizeof(vr::PresentInfo_t))return;
        if(!presenter_)presenter_=std::make_unique<Presenter>(display_,config_);
        presenter_->present(reinterpret_cast<HANDLE>(info->backbufferTextureHandle));
    }
    void WaitForPresent() override {if(presenter_)presenter_->wait_for_present();}
    bool GetTimeSinceLastVsync(float* seconds,std::uint64_t* frame) override {return presenter_&&presenter_->time_since_vsync(seconds,frame);}
private:
    Config config_;DisplayInfo display_{};bool display_found_{};bool first_present_logged_{};std::unique_ptr<Presenter>presenter_;std::uint32_t object_id_{vr::k_unTrackedDeviceIndexInvalid};
};

class Provider final : public vr::IServerTrackedDeviceProvider {
public:
    vr::EVRInitError Init(vr::IVRDriverContext* context) override {VR_INIT_SERVER_DRIVER_CONTEXT(context);log("SteamVR driver init");vr::VRDriverLog()->Log("RokidVR: driver init");maybe_add();return vr::VRInitError_None;}
    void Cleanup() override {controller_.reset();redirect_.reset();device_.reset();VR_CLEANUP_SERVER_DRIVER_CONTEXT();log("SteamVR driver cleanup");}
    const char* const* GetInterfaceVersions() override {return vr::k_InterfaceVersions;}
    void RunFrame() override {
        if(!device_)maybe_add();
        vr::VREvent_t event{};while(vr::VRServerDriverHost()->PollNextEvent(&event,sizeof(event))){
            if(event.eventType==vr::VREvent_DashboardActivated){dashboard_visible_=true;log(controller_?"SteamVR Dashboard activated: optional mouse controller available":"SteamVR Dashboard activated: optional mouse controller is disabled");}
            else if(event.eventType==vr::VREvent_DashboardDeactivated){dashboard_visible_=false;log("SteamVR Dashboard deactivated: game mouse protection restored");}
        }
        if(device_)device_->run_frame();if(controller_){controller_->set_dashboard_visible(dashboard_visible_);controller_->run_frame();}
    }
    bool ShouldBlockStandbyMode() override {return false;}void EnterStandby() override {}void LeaveStandby() override {}
private:
    void maybe_add(){
        if(device_||!HidTracker::present())return;
        const auto hardware_serial=HidTracker::serial_number();
        serial_=hardware_serial.empty()?"ROKID_MAX_04D2_162F":narrow(hardware_serial);
        if(load_config().video_path==VideoPath::virtual_display){
            redirect_serial_=serial_+"_DISPLAY";
            redirect_=std::make_unique<DisplayRedirectDevice>();
            if(!vr::VRServerDriverHost()->TrackedDeviceAdded(redirect_serial_.c_str(),vr::TrackedDeviceClass_DisplayRedirect,redirect_.get())){
                log("Display redirect registration failed");redirect_.reset();
            }else log("Registered Rokid Max display redirect");
        }
        device_=std::make_unique<HmdDevice>();
        if(!vr::VRServerDriverHost()->TrackedDeviceAdded(serial_.c_str(),vr::TrackedDeviceClass_HMD,device_.get())){
            log("TrackedDeviceAdded failed");device_.reset();redirect_.reset();return;
        }
        log("Registered Rokid Max HMD serial "+serial_);
        if(load_config().virtual_controller){
            controller_=std::make_unique<MouseControllerDevice>(device_.get());
            if(!vr::VRServerDriverHost()->TrackedDeviceAdded("ROKID_MOUSE_RIGHT",vr::TrackedDeviceClass_Controller,controller_.get())){
                log("Mouse controller registration failed");controller_.reset();
            }else log("Registered optional Rokid mouse controller");
        }else log("Optional mouse controller disabled; native game mouse and keyboard preserved");
    }
    std::unique_ptr<HmdDevice>device_;std::unique_ptr<DisplayRedirectDevice>redirect_;std::unique_ptr<MouseControllerDevice>controller_;std::string serial_,redirect_serial_;bool dashboard_visible_{};
};
Provider provider;
}

#if defined(_WIN32)
#define DRIVER_EXPORT extern "C" __declspec(dllexport)
#else
#define DRIVER_EXPORT extern "C"
#endif
DRIVER_EXPORT void* HmdDriverFactory(const char* interface_name,int* return_code){if(interface_name&&!std::strcmp(interface_name,vr::IServerTrackedDeviceProvider_Version))return &rokidvr::provider;if(return_code)*return_code=vr::VRInitError_Init_InterfaceNotFound;return nullptr;}
