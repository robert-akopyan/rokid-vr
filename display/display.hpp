#pragma once
#include <Windows.h>
#include <dxgi.h>
#include <cstdint>
#include <string>
#include <vector>

namespace rokidvr {
struct DisplayMode { std::uint32_t width{},height{}; double refresh_hz{}; };
struct DisplayInfo {
    std::wstring friendly_name,device_path,gdi_name,adapter_name,output_name;
    RECT bounds{}; DisplayMode current{}; std::vector<DisplayMode> modes; LUID adapter_luid{}; bool primary{},rokid{};
};
std::vector<DisplayInfo> enumerate_displays();
bool select_rokid_display(const std::wstring& configured_id,DisplayInfo& result);
bool same_display_configuration(const DisplayInfo& left,const DisplayInfo& right);
std::wstring describe_display(const DisplayInfo& display);
}
