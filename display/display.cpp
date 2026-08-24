#include "display/display.hpp"
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <algorithm>
#include <cwctype>
#include <map>
#include <sstream>

namespace rokidvr { namespace {
std::wstring lower(std::wstring s){std::transform(s.begin(),s.end(),s.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});return s;}
double hz(const DISPLAYCONFIG_RATIONAL& r){return r.Denominator?static_cast<double>(r.Numerator)/r.Denominator:0;}
std::wstring luid_key(LUID l,UINT id){return std::to_wstring(l.HighPart)+L":"+std::to_wstring(l.LowPart)+L":"+std::to_wstring(id);}
}
std::vector<DisplayInfo> enumerate_displays(){
    UINT pc=0,mc=0;std::vector<DISPLAYCONFIG_PATH_INFO> paths;std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    LONG rc=ERROR_INSUFFICIENT_BUFFER;for(int tries=0;tries<4&&rc==ERROR_INSUFFICIENT_BUFFER;++tries){if(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&pc,&mc)!=ERROR_SUCCESS)break;paths.resize(pc);modes.resize(mc);rc=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&pc,paths.data(),&mc,modes.data(),nullptr);}if(rc!=ERROR_SUCCESS)return {};
    paths.resize(pc);modes.resize(mc);std::vector<DisplayInfo> out;
    for(const auto& p:paths){DISPLAYCONFIG_TARGET_DEVICE_NAME target{};target.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;target.header.size=sizeof(target);target.header.adapterId=p.targetInfo.adapterId;target.header.id=p.targetInfo.id;if(DisplayConfigGetDeviceInfo(&target.header)!=ERROR_SUCCESS)continue;DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};source.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;source.header.size=sizeof(source);source.header.adapterId=p.sourceInfo.adapterId;source.header.id=p.sourceInfo.id;DisplayConfigGetDeviceInfo(&source.header);
        DisplayInfo d;d.friendly_name=target.monitorFriendlyDeviceName;d.device_path=target.monitorDevicePath;d.gdi_name=source.viewGdiDeviceName;d.adapter_luid=p.targetInfo.adapterId;d.current.refresh_hz=hz(p.targetInfo.refreshRate);
        if(p.sourceInfo.modeInfoIdx<mc){const auto&s=modes[p.sourceInfo.modeInfoIdx].sourceMode;d.bounds={s.position.x,s.position.y,s.position.x+static_cast<LONG>(s.width),s.position.y+static_cast<LONG>(s.height)};d.current.width=s.width;d.current.height=s.height;}
        DISPLAY_DEVICEW dd{sizeof(dd)};if(EnumDisplayDevicesW(d.gdi_name.c_str(),0,&dd,0))d.primary=(dd.StateFlags&DISPLAY_DEVICE_PRIMARY_DEVICE)!=0;
        const auto probe=lower(d.friendly_name+L" "+d.device_path);d.rokid=probe.find(L"rokid")!=std::wstring::npos||probe.find(L"04d2")!=std::wstring::npos;
        DEVMODEW dm{};dm.dmSize=sizeof(dm);for(DWORD i=0;EnumDisplaySettingsExW(d.gdi_name.c_str(),i,&dm,0);++i){DisplayMode m{dm.dmPelsWidth,dm.dmPelsHeight,static_cast<double>(dm.dmDisplayFrequency)};const bool exists=std::any_of(d.modes.begin(),d.modes.end(),[&](const auto&x){return x.width==m.width&&x.height==m.height&&std::abs(x.refresh_hz-m.refresh_hz)<.1;});if(!exists)d.modes.push_back(m);dm={};dm.dmSize=sizeof(dm);}out.push_back(std::move(d));}
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;if(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))){for(UINT ai=0;;++ai){Microsoft::WRL::ComPtr<IDXGIAdapter1>a;if(factory->EnumAdapters1(ai,&a)==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 ad{};a->GetDesc1(&ad);for(UINT oi=0;;++oi){Microsoft::WRL::ComPtr<IDXGIOutput>o;if(a->EnumOutputs(oi,&o)==DXGI_ERROR_NOT_FOUND)break;DXGI_OUTPUT_DESC od{};o->GetDesc(&od);for(auto&d:out)if(EqualRect(&d.bounds,&od.DesktopCoordinates)){d.adapter_name=ad.Description;d.output_name=od.DeviceName;d.adapter_luid=ad.AdapterLuid;}}}}
    return out;
}
bool select_rokid_display(const std::wstring& id,DisplayInfo& result){auto all=enumerate_displays();if(!id.empty())for(auto&d:all)if(d.device_path==id||d.gdi_name==id){result=std::move(d);return true;}for(auto&d:all)if(d.rokid){result=std::move(d);return true;}return false;}
bool same_display_configuration(const DisplayInfo&a,const DisplayInfo&b){
    return a.device_path==b.device_path&&a.gdi_name==b.gdi_name&&
        a.adapter_luid.HighPart==b.adapter_luid.HighPart&&a.adapter_luid.LowPart==b.adapter_luid.LowPart&&
        EqualRect(&a.bounds,&b.bounds)&&a.current.width==b.current.width&&a.current.height==b.current.height&&
        std::abs(a.current.refresh_hz-b.current.refresh_hz)<.1;
}
std::wstring describe_display(const DisplayInfo& d){std::wostringstream s;s<<(d.friendly_name.empty()?L"Unnamed display":d.friendly_name)<<L" ["<<d.gdi_name<<L"] "<<d.current.width<<L"x"<<d.current.height<<L" @ "<<d.current.refresh_hz<<L" Hz, desktop ("<<d.bounds.left<<L","<<d.bounds.top<<L"), adapter "<<d.adapter_name<<L", output "<<d.output_name;return s.str();}
}
