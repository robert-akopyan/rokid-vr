#include "display/display.hpp"
#include "display/presenter.hpp"
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <iostream>

using Microsoft::WRL::ComPtr;
int wmain(){
    rokidvr::DisplayInfo display;if(!rokidvr::select_rokid_display(L"",display)){std::cerr<<"Rokid display not found\n";return 2;}
    ComPtr<IDXGIFactory1> factory;ComPtr<IDXGIAdapter1> selected;if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))return 1;
    for(UINT i=0;;++i){ComPtr<IDXGIAdapter1>a;if(factory->EnumAdapters1(i,&a)==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 d{};a->GetDesc1(&d);if(d.AdapterLuid.HighPart==display.adapter_luid.HighPart&&d.AdapterLuid.LowPart==display.adapter_luid.LowPart){selected=a;break;}}
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};if(FAILED(D3D11CreateDevice(selected.Get(),selected?D3D_DRIVER_TYPE_UNKNOWN:D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)))return 1;
    const std::uint32_t pixels[2]={0xff2040ff,0xffff8020};D3D11_SUBRESOURCE_DATA data{pixels,sizeof(std::uint32_t)*2,0};D3D11_TEXTURE2D_DESC td{};td.Width=2;td.Height=1;td.MipLevels=1;td.ArraySize=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;td.MiscFlags=D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;ComPtr<ID3D11Texture2D> texture;if(FAILED(device->CreateTexture2D(&td,&data,&texture)))return 1;
    ComPtr<IDXGIResource> resource;if(FAILED(texture.As(&resource)))return 1;HANDLE shared{};if(FAILED(resource->GetSharedHandle(&shared)))return 1;
    {rokidvr::Config config;config.output_mode=rokidvr::OutputMode::mono_left;rokidvr::Presenter presenter({},config);if(!presenter.present(shared)){std::cerr<<"presenter did not recover from an initially missing display\n";return 1;}}
    {auto stale=display;OffsetRect(&stale.bounds,10000,0);rokidvr::Config config;config.output_mode=rokidvr::OutputMode::mono_left;rokidvr::Presenter presenter(stale,config);if(!presenter.present(shared)){std::cerr<<"presenter did not recover from stale display coordinates\n";return 1;}}
    for(const auto mode:{rokidvr::OutputMode::automatic,rokidvr::OutputMode::mono_left,rokidvr::OutputMode::mono_right,rokidvr::OutputMode::sbs}){rokidvr::Config config;config.output_mode=mode;config.vsync=true;rokidvr::Presenter presenter(display,config);for(int i=0;i<5;++i){if(!presenter.present(shared)){std::cerr<<"present failed\n";return 1;}presenter.wait_for_present();}}
    std::cout<<"D3D11 presenter recovery plus auto/mono-left/mono-right/SBS smoke passed on physical Rokid display\n";return 0;
}
