#include "common/config.hpp"
#include <ShlObj.h>
#include <filesystem>

namespace rokidvr {
std::wstring data_directory() {
    PWSTR raw=nullptr; std::wstring result;
    if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_CREATE,nullptr,&raw))){result=raw;CoTaskMemFree(raw);}
    if(result.empty()) result=L".";
    result += L"\\RokidVR"; std::filesystem::create_directories(result+L"\\logs"); return result;
}
std::wstring config_path(){ return data_directory()+L"\\config.ini"; }
static std::wstring get(const wchar_t* section,const wchar_t* key,const wchar_t* fallback){
    wchar_t b[512]{}; GetPrivateProfileStringW(section,key,fallback,b,512,config_path().c_str()); return b;
}
Config load_config(){
    Config c; const auto path=get(L"video",L"path",L"extended");
    c.video_path=path==L"virtual"?VideoPath::virtual_display:(path==L"direct"?VideoPath::direct:VideoPath::extended);
    const auto mode=get(L"video",L"output_mode",L"auto");
    if(mode==L"mono_left")c.output_mode=OutputMode::mono_left; else if(mode==L"mono_right")c.output_mode=OutputMode::mono_right; else if(mode==L"sbs")c.output_mode=OutputMode::sbs;
    c.ipd_mm=GetPrivateProfileIntW(L"hmd",L"ipd_tenths_mm",630,config_path().c_str())/10.0;
    c.vsync=GetPrivateProfileIntW(L"video",L"vsync",1,config_path().c_str())!=0;
    c.swap_eyes=GetPrivateProfileIntW(L"video",L"swap_eyes",0,config_path().c_str())!=0;
    c.display_id=get(L"display",L"id",L""); return c;
}
void save_config(const Config& c){
    const auto p=config_path();
    const wchar_t* video_path=c.video_path==VideoPath::virtual_display?L"virtual":(c.video_path==VideoPath::direct?L"direct":L"extended");
    WritePrivateProfileStringW(L"video",L"path",video_path,p.c_str());
    const wchar_t* mode=L"auto"; if(c.output_mode==OutputMode::mono_left)mode=L"mono_left"; else if(c.output_mode==OutputMode::mono_right)mode=L"mono_right"; else if(c.output_mode==OutputMode::sbs)mode=L"sbs";
    WritePrivateProfileStringW(L"video",L"output_mode",mode,p.c_str());
    const auto ipd=std::to_wstring(static_cast<int>(c.ipd_mm*10));
    WritePrivateProfileStringW(L"hmd",L"ipd_tenths_mm",ipd.c_str(),p.c_str());
    WritePrivateProfileStringW(L"video",L"vsync",c.vsync?L"1":L"0",p.c_str()); WritePrivateProfileStringW(L"video",L"swap_eyes",c.swap_eyes?L"1":L"0",p.c_str());
    WritePrivateProfileStringW(L"display",L"id",c.display_id.c_str(),p.c_str());
}
}
