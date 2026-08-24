#include "display/presenter.hpp"
#include "common/log.hpp"
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <cstring>

namespace rokidvr { namespace {
const char* shader=R"(
cbuffer C:register(b0){float4 crop;float mode;float3 pad;}
struct O{float4 p:SV_Position;float2 uv:TEXCOORD0;};
O vs(uint id:SV_VertexID){O o;float2 p=float2((id<<1)&2,id&2);o.uv=p;o.p=float4(p*float2(2,-2)+float2(-1,1),0,1);return o;}
Texture2D tex:register(t0);SamplerState samp:register(s0);
float4 ps(O i):SV_Target{float2 uv=crop.xy+i.uv*crop.zw;if(mode>1.5)uv.x=frac(uv.x+.5);return tex.Sample(samp,uv);}
)";
}
Presenter::Presenter(DisplayInfo d,Config c):display_(std::move(d)),config_(c){display_available_=!display_.gdi_name.empty()&&display_.current.width&&display_.current.height;QueryPerformanceFrequency(&frequency_);}
Presenter::~Presenter(){release_display_resources();}
LRESULT CALLBACK Presenter::window_proc(HWND h,UINT m,WPARAM w,LPARAM l){
    Presenter* self=reinterpret_cast<Presenter*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(m==WM_NCCREATE){const auto* create=reinterpret_cast<const CREATESTRUCTW*>(l);self=static_cast<Presenter*>(create->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(self&&(m==WM_DISPLAYCHANGE||m==WM_DEVICECHANGE))self->display_refresh_requested_=true;
    if(m==WM_NCDESTROY)SetWindowLongPtrW(h,GWLP_USERDATA,0);
    return DefWindowProcW(h,m,w,l);
}
void Presenter::release_display_resources(){
    if(context_){context_->ClearState();context_->Flush();}
    source_view_.Reset();source_.Reset();last_handle_=nullptr;constants_.Reset();sampler_.Reset();ps_.Reset();vs_.Reset();
    if(swapchain_)swapchain_->SetFullscreenState(FALSE,nullptr);swapchain_.Reset();
    if(frame_wait_){CloseHandle(frame_wait_);frame_wait_=nullptr;}
    context_.Reset();device_.Reset();
    if(window_){DestroyWindow(window_);window_=nullptr;}
    last_present_={};first_frame_logged_=false;source_description_logged_=false;mode_description_logged_=false;
}
bool Presenter::refresh_display(bool force){
    const auto now=GetTickCount64();if(!force&&!display_refresh_requested_&&now<next_display_probe_)return display_available_;
    display_refresh_requested_=false;next_display_probe_=now+500;
    DisplayInfo current{};if(!select_rokid_display(config_.display_id,current)){
        if(display_available_||!display_loss_logged_)log("Rokid display disconnected; presenter is waiting for reconnect");
        display_available_=false;display_loss_logged_=true;release_display_resources();return false;
    }
    const bool changed=!display_available_||!same_display_configuration(display_,current);
    if(changed){
        const std::string before=display_available_?narrow(describe_display(display_)):"disconnected";
        release_display_resources();display_=std::move(current);display_available_=true;display_loss_logged_=false;
        log("Rokid display configuration changed: "+before+" -> "+narrow(describe_display(display_))+"; presenter will be recreated");
    }else display_available_=true;
    return true;
}
bool Presenter::initialize(){
    if(!display_available_||!display_.current.width||!display_.current.height)return false;
    log("Presenter initialization started");
    WNDCLASSW wc{};wc.lpfnWndProc=window_proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"RokidVRPresenter";if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){log("Presenter window class registration failed");return false;}
    window_=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"RokidVRPresenter",L"Rokid VR Output",WS_POPUP,display_.bounds.left,display_.bounds.top,display_.bounds.right-display_.bounds.left,display_.bounds.bottom-display_.bounds.top,nullptr,nullptr,wc.hInstance,this);if(!window_){log("Presenter window creation failed");return false;}ShowWindow(window_,SW_SHOWNOACTIVATE);
    Microsoft::WRL::ComPtr<IDXGIFactory1> f;Microsoft::WRL::ComPtr<IDXGIAdapter1> selected;if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&f)))){log("Presenter DXGI factory creation failed");release_display_resources();return false;}for(UINT i=0;;++i){Microsoft::WRL::ComPtr<IDXGIAdapter1>a;if(f->EnumAdapters1(i,&a)==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 desc{};a->GetDesc1(&desc);if(desc.AdapterLuid.HighPart==display_.adapter_luid.HighPart&&desc.AdapterLuid.LowPart==display_.adapter_luid.LowPart){selected=a;break;}}
    UINT flags= D3D11_CREATE_DEVICE_BGRA_SUPPORT;D3D_FEATURE_LEVEL level{};if(FAILED(D3D11CreateDevice(selected.Get(),selected?D3D_DRIVER_TYPE_UNKNOWN:D3D_DRIVER_TYPE_HARDWARE,nullptr,flags,nullptr,0,D3D11_SDK_VERSION,&device_,&level,&context_))){log("Presenter D3D11 device creation failed");release_display_resources();return false;}
    Microsoft::WRL::ComPtr<IDXGIDevice> dxdev;Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;Microsoft::WRL::ComPtr<IDXGIFactory2> factory;if(FAILED(device_.As(&dxdev))||FAILED(dxdev->GetAdapter(&adapter))||FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))){log("Presenter DXGI adapter lookup failed");release_display_resources();return false;}
    DXGI_SWAP_CHAIN_DESC1 sd{};sd.Width=display_.current.width;sd.Height=display_.current.height;sd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=2;sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;sd.Flags=DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;Microsoft::WRL::ComPtr<IDXGISwapChain1> sc;if(FAILED(factory->CreateSwapChainForHwnd(device_.Get(),window_,&sd,nullptr,nullptr,&sc))||FAILED(sc.As(&swapchain_))){log("Presenter swap chain creation failed");release_display_resources();return false;}swapchain_->SetMaximumFrameLatency(1);frame_wait_=swapchain_->GetFrameLatencyWaitableObject();if(!create_pipeline()){log("Presenter pipeline creation failed");release_display_resources();return false;}log("Presenter initialized on "+narrow(display_.gdi_name)+" at "+std::to_string(display_.bounds.left)+","+std::to_string(display_.bounds.top));return true;
}
bool Presenter::create_pipeline(){Microsoft::WRL::ComPtr<ID3DBlob> vb,pb,err;if(FAILED(D3DCompile(shader,std::strlen(shader),nullptr,nullptr,nullptr,"vs","vs_5_0",0,0,&vb,&err))||FAILED(D3DCompile(shader,std::strlen(shader),nullptr,nullptr,nullptr,"ps","ps_5_0",0,0,&pb,&err)))return false;if(FAILED(device_->CreateVertexShader(vb->GetBufferPointer(),vb->GetBufferSize(),nullptr,&vs_))||FAILED(device_->CreatePixelShader(pb->GetBufferPointer(),pb->GetBufferSize(),nullptr,&ps_)))return false;D3D11_SAMPLER_DESC ss{};ss.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;ss.AddressU=ss.AddressV=ss.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;if(FAILED(device_->CreateSamplerState(&ss,&sampler_)))return false;D3D11_BUFFER_DESC cb{};cb.ByteWidth=32;cb.Usage=D3D11_USAGE_DYNAMIC;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;cb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;return SUCCEEDED(device_->CreateBuffer(&cb,nullptr,&constants_));}
void Presenter::pump_messages(){if(!window_)return;MSG m{};while(PeekMessageW(&m,window_,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
bool Presenter::present(HANDLE h){pump_messages();if(!refresh_display())return false;if(!device_&&!initialize())return false;if(h!=last_handle_){source_.Reset();source_view_.Reset();if(FAILED(device_->OpenSharedResource(h,IID_PPV_ARGS(&source_)))||FAILED(device_->CreateShaderResourceView(source_.Get(),nullptr,&source_view_))){log("OpenSharedResource failed; presenter will retry with new D3D resources");release_display_resources();display_refresh_requested_=true;return false;}last_handle_=h;}
    if(!source_description_logged_){D3D11_TEXTURE2D_DESC desc{};source_->GetDesc(&desc);log("SteamVR shared texture: "+std::to_string(desc.Width)+"x"+std::to_string(desc.Height)+", format "+std::to_string(static_cast<unsigned>(desc.Format))+", samples "+std::to_string(desc.SampleDesc.Count));source_description_logged_=true;}
    Microsoft::WRL::ComPtr<IDXGIKeyedMutex> mutex;source_.As(&mutex);if(mutex&&mutex->AcquireSync(0,10)!=S_OK)return false;Microsoft::WRL::ComPtr<ID3D11Texture2D> back;Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;if(FAILED(swapchain_->GetBuffer(0,IID_PPV_ARGS(&back)))||FAILED(device_->CreateRenderTargetView(back.Get(),nullptr,&rtv))){if(mutex)mutex->ReleaseSync(0);log("Presenter back buffer became invalid; resources will be recreated");release_display_resources();display_refresh_requested_=true;return false;}
    struct Constants{float crop[4];float mode;float pad[3];} c{{0,0,1,1},0,{}};OutputMode mode=config_.output_mode;if(mode==OutputMode::automatic)mode=display_.current.width>=3000?OutputMode::sbs:OutputMode::mono_left;if(!mode_description_logged_){const char* configured=config_.output_mode==OutputMode::automatic?"auto":(config_.output_mode==OutputMode::mono_left?"mono-left":(config_.output_mode==OutputMode::mono_right?"mono-right":"SBS"));const char* resolved=mode==OutputMode::mono_left?"mono-left":(mode==OutputMode::mono_right?"mono-right":"SBS");log(std::string("Presenter output mode: configured ")+configured+", resolved "+resolved+", physical display "+std::to_string(display_.current.width)+"x"+std::to_string(display_.current.height)+(config_.swap_eyes?", eyes swapped":""));if(mode==OutputMode::sbs&&display_.current.width<3000)log("Warning: SBS output selected while Rokid is in a narrow Windows display mode; switch the glasses to 3D/SBS mode and restart SteamVR");if((mode==OutputMode::mono_left||mode==OutputMode::mono_right)&&display_.current.width>=3000)log("Warning: mono output selected while Rokid is in a wide SBS display mode");mode_description_logged_=true;}if(mode==OutputMode::mono_left||mode==OutputMode::mono_right){c.crop[0]=mode==OutputMode::mono_right?.5f:0;c.crop[2]=.5f;}else if(mode==OutputMode::sbs&&config_.swap_eyes)c.mode=2;
    D3D11_MAPPED_SUBRESOURCE map{};if(FAILED(context_->Map(constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&map))){if(mutex)mutex->ReleaseSync(0);release_display_resources();display_refresh_requested_=true;return false;}std::memcpy(map.pData,&c,sizeof(c));context_->Unmap(constants_.Get(),0);D3D11_VIEWPORT vp{0,0,static_cast<float>(display_.current.width),static_cast<float>(display_.current.height),0,1};context_->RSSetViewports(1,&vp);context_->OMSetRenderTargets(1,rtv.GetAddressOf(),nullptr);context_->VSSetShader(vs_.Get(),nullptr,0);context_->PSSetShader(ps_.Get(),nullptr,0);context_->PSSetShaderResources(0,1,source_view_.GetAddressOf());context_->PSSetSamplers(0,1,sampler_.GetAddressOf());context_->PSSetConstantBuffers(0,1,constants_.GetAddressOf());context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context_->Draw(3,0);ID3D11ShaderResourceView* nullview=nullptr;context_->PSSetShaderResources(0,1,&nullview);if(mutex)mutex->ReleaseSync(0);const HRESULT hr=swapchain_->Present(config_.vsync?1:0,0);if(FAILED(hr)){log("Presenter swap-chain Present failed (HRESULT "+std::to_string(static_cast<long long>(hr))+"); resources will be recreated");release_display_resources();display_refresh_requested_=true;return false;}QueryPerformanceCounter(&last_present_);++frames_;if(!first_frame_logged_){log("Presenter displayed first frame");first_frame_logged_=true;}return true;}
void Presenter::wait_for_present(){if(frame_wait_)WaitForSingleObject(frame_wait_,50);}
bool Presenter::time_since_vsync(float* seconds,std::uint64_t* frame){if(!last_present_.QuadPart)return false;LARGE_INTEGER now{};QueryPerformanceCounter(&now);if(seconds)*seconds=static_cast<float>(static_cast<double>(now.QuadPart-last_present_.QuadPart)/frequency_.QuadPart);if(frame)*frame=frames_;return true;}
}
