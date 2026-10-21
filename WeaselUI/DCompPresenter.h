#pragma once
#include <d3d11.h>
#include <d2d1_1.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cstring>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "dxgi.lib")

namespace weasel {
// Experimental migration stage 1: present the existing complete skin canvas.
// Device / premultiplied swap-chain setup follows official Weasel d2d.cpp at
// 82b50c9 (RIME Developers, GPL-3.0); the GDI layout and pixel caches stay intact.
// This deliberately does NOT claim a completed Direct2D drawing migration.
class DCompPresenter {
  template <class T> using Ptr = Microsoft::WRL::ComPtr<T>;
  Ptr<ID3D11Device> device_;
  Ptr<ID3D11DeviceContext> immediate_;
  Ptr<ID2D1Factory1> factory_;
  Ptr<ID2D1Device> d2device_;
  Ptr<ID2D1DeviceContext> dc_;
  Ptr<IDXGISwapChain1> swap_;
  Ptr<IDCompositionDevice> composition_;
  Ptr<IDCompositionTarget> target_;
  Ptr<IDCompositionVisual> visual_;
  Ptr<ID2D1Bitmap1> surface_, canvas_;
  unsigned width_ = 0, height_ = 0;

  HRESULT Initialize(HWND hwnd) {
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
        &device_, nullptr, &immediate_);
    if (FAILED(hr)) return hr;
    Ptr<IDXGIDevice> dxgi;
    if (FAILED(hr = device_.As(&dxgi))) return hr;
    if (FAILED(hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf()))) return hr;
    if (FAILED(hr = factory_->CreateDevice(dxgi.Get(), &d2device_))) return hr;
    if (FAILED(hr = d2device_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc_))) return hr;
    if (FAILED(hr = DCompositionCreateDevice(dxgi.Get(), IID_PPV_ARGS(composition_.GetAddressOf())))) return hr;
    if (FAILED(hr = composition_->CreateTargetForHwnd(hwnd, TRUE, &target_))) return hr;
    if (FAILED(hr = composition_->CreateVisual(&visual_))) return hr;
    return target_->SetRoot(visual_.Get());
  }
  HRESULT Resize(unsigned width, unsigned height) {
    if (width_ == width && height_ == height) return S_OK;
    dc_->SetTarget(nullptr);
    surface_.Reset(); canvas_.Reset();
    HRESULT hr;
    if (swap_) {
      hr = swap_->ResizeBuffers(2, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
    } else {
      Ptr<IDXGIDevice> dxgi; Ptr<IDXGIAdapter> adapter; Ptr<IDXGIFactory2> factory;
      if (FAILED(hr = device_.As(&dxgi))) return hr;
      if (FAILED(hr = dxgi->GetAdapter(&adapter))) return hr;
      if (FAILED(hr = adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf())))) return hr;
      DXGI_SWAP_CHAIN_DESC1 desc = {};
      desc.Width = width; desc.Height = height;
      desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
      desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount = 2;
      desc.Scaling = DXGI_SCALING_STRETCH; desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
      desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
      hr = factory->CreateSwapChainForComposition(device_.Get(), &desc, nullptr, &swap_);
    }
    if (FAILED(hr)) return hr;
    Ptr<IDXGISurface> surface;
    if (FAILED(hr = swap_->GetBuffer(0, IID_PPV_ARGS(surface.GetAddressOf())))) return hr;
    auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    if (FAILED(hr = dc_->CreateBitmapFromDxgiSurface(surface.Get(), &props, &surface_))) return hr;
    props.bitmapOptions = D2D1_BITMAP_OPTIONS_NONE;
    if (FAILED(hr = dc_->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0, &props, &canvas_))) return hr;
    dc_->SetTarget(surface_.Get());
    if (FAILED(hr = visual_->SetContent(swap_.Get()))) return hr;
    if (FAILED(hr = composition_->Commit())) return hr;
    width_ = width; height_ = height;
    return S_OK;
  }
  HRESULT Verify(const BYTE* pixels, unsigned stride) {
    // CI-only readback, excluded from normal rendering/performance comparisons.
    Ptr<ID3D11Texture2D> back, staging;
    HRESULT hr = swap_->GetBuffer(0, IID_PPV_ARGS(back.GetAddressOf()));
    if (FAILED(hr)) return hr;
    D3D11_TEXTURE2D_DESC desc; back->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0;
    if (FAILED(hr = device_->CreateTexture2D(&desc, nullptr, &staging))) return hr;
    immediate_->CopyResource(staging.Get(), back.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(hr = immediate_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return hr;
    bool matches = true;
    for (unsigned y = 0; y < height_; ++y)
      if (std::memcmp(pixels + size_t(y)*stride, static_cast<BYTE*>(mapped.pData) + size_t(y)*mapped.RowPitch, size_t(width_)*4)) { matches = false; break; }
    immediate_->Unmap(staging.Get(), 0);
    return matches ? S_OK : E_FAIL;
  }
 public:
  void Reset() {
    if (target_) target_->SetRoot(nullptr);
    if (composition_) composition_->Commit();
    if (dc_) dc_->SetTarget(nullptr);
    canvas_.Reset(); surface_.Reset(); visual_.Reset(); target_.Reset();
    composition_.Reset(); swap_.Reset(); dc_.Reset(); d2device_.Reset();
    factory_.Reset(); immediate_.Reset(); device_.Reset(); width_ = height_ = 0;
  }
  ~DCompPresenter() { Reset(); }
  HRESULT Present(HWND hwnd, HDC source, unsigned width, unsigned height, bool verify) {
    DIBSECTION section = {};
    if (!width || !height || !GetObjectW(GetCurrentObject(source, OBJ_BITMAP), sizeof(section), &section) ||
        !section.dsBm.bmBits || section.dsBm.bmBitsPixel != 32 ||
        section.dsBm.bmWidth != width || section.dsBm.bmHeight != height)
      return E_INVALIDARG;
    // Both production DIB constructors are explicitly top-down. GetObjectW
    // normalizes biHeight to a positive value, so it cannot validate orientation.
    GdiFlush();
    HRESULT hr = S_OK;
    if (!device_ && FAILED(hr = Initialize(hwnd))) return hr;
    if (FAILED(hr = Resize(width, height))) return hr;
    auto pixels = static_cast<const BYTE*>(section.dsBm.bmBits);
    if (FAILED(hr = canvas_->CopyFromMemory(nullptr, pixels, section.dsBm.bmWidthBytes))) return hr;
    dc_->BeginDraw(); dc_->Clear(D2D1::ColorF(0, 0.f));
    dc_->DrawBitmap(canvas_.Get(), D2D1::RectF(0, 0, float(width), float(height)), 1,
                    D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    if (FAILED(hr = dc_->EndDraw())) return hr;
    if (verify && FAILED(hr = Verify(pixels, section.dsBm.bmWidthBytes))) return hr;
    return swap_->Present(1, 0);
  }
};
}  // namespace weasel
