#pragma once
#include <Windows.h>
#include <string>

namespace rokidvr {
enum class VideoPath { extended, direct, virtual_display };
enum class OutputMode { automatic, mono_left, mono_right, sbs };
struct Config {
    VideoPath video_path{VideoPath::extended};
    OutputMode output_mode{OutputMode::automatic};
    double ipd_mm{63.0};
    double view_gain{1.0};
    bool vsync{true};
    bool swap_eyes{false};
    bool virtual_controller{false};
    std::wstring display_id;
};
std::wstring data_directory();
std::wstring config_path();
Config load_config();
void save_config(const Config& value);
}
