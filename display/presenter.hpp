#pragma once
#include "common/config.hpp"
#include "display/display.hpp"
#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <wrl/client.h>

namespace rokidvr {
class Presenter {
public:
    Presenter(DisplayInfo display,Config config); ~Presenter();
    bool present(HANDLE shared_texture); void wait_for_present(); bool time_since_vsync(float*,std::uint64_t*);
private:
    bool initialize(); bool create_pipeline(); void pump_messages();
    DisplayInfo display_;Config config_;HWND window_{};HANDLE frame_wait_{};std::uint64_t frames_{};LARGE_INTEGER last_present_{},frequency_{};
    Microsoft::WRL::ComPtr<ID3D11Device> device_;Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;Microsoft::WRL::ComPtr<IDXGISwapChain2> swapchain_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_;Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
    HANDLE last_handle_{};bool first_frame_logged_{};Microsoft::WRL::ComPtr<ID3D11Texture2D> source_;Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> source_view_;
};
}
