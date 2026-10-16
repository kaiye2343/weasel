// Isolated compositor microbenchmark. No HWND, Rime sessions, IPC or installed data.
// Uses the upstream WARP/D2D device path, NOT its complete DComp/Present renderer.
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <gdiplus.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "../WeaselUI/SkinImageLoader.h"
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")
using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;
void HR(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("HRESULT " + std::to_string(static_cast<unsigned long>(hr))); }
void GD(Gdiplus::Status s) { if (s != Gdiplus::Ok) throw std::runtime_error("GDI+ " + std::to_string(s)); }
double Ms(Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now()-start).count(); }
double Percentile(std::vector<double> values, size_t p) { std::sort(values.begin(),values.end()); return values[(values.size()-1)*p/100]; }
struct WARP {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> immediate;
  ComPtr<ID2D1Factory1> factory;
  ComPtr<ID2D1Device> d2device;
  ComPtr<ID2D1DeviceContext> dc;
  ComPtr<ID3D11Texture2D> texture, staging;
  ComPtr<ID2D1Bitmap1> target, base, decal;
  ComPtr<ID2D1SolidColorBrush> brush;
  int width = 0, height = 0;
  WARP() {
    HR(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
      nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&immediate));
    HR(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf()));
    ComPtr<IDXGIDevice> dxgi; HR(device.As(&dxgi));
    HR(factory->CreateDevice(dxgi.Get(),&d2device));
    HR(d2device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,&dc));
    HR(dc->CreateSolidColorBrush(D2D1::ColorF(0.2f,0.5f,0.8f,1),&brush));
    dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
  }
  ComPtr<ID2D1Bitmap1> Upload(int w,int h,const std::vector<BYTE>& pixels) {
    ComPtr<ID2D1Bitmap1> bitmap;
    auto prop=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
    HR(dc->CreateBitmap(D2D1::SizeU(w,h),pixels.data(),w*4,&prop,&bitmap)); return bitmap;
  }
  void Resize(int w,int h) {
    if(w==width&&h==height)return;
    dc->SetTarget(nullptr);target.Reset();texture.Reset();staging.Reset();
    D3D11_TEXTURE2D_DESC desc={};desc.Width=w;desc.Height=h;desc.MipLevels=1;desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;
    desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    HR(device->CreateTexture2D(&desc,nullptr,&texture));
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    HR(device->CreateTexture2D(&desc,nullptr,&staging));
    ComPtr<IDXGISurface> surface;HR(texture.As(&surface));
    auto prop=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
    HR(dc->CreateBitmapFromDxgiSurface(surface.Get(),&prop,&target));dc->SetTarget(target.Get());width=w;height=h;
  }
  void Complete(std::vector<BYTE>& pixels) {
    immediate->CopyResource(staging.Get(),texture.Get());
    D3D11_MAPPED_SUBRESOURCE map={};HR(immediate->Map(staging.Get(),0,D3D11_MAP_READ,0,&map));
    for(int y=0;y<height;y++)std::memcpy(pixels.data()+size_t(y)*width*4,static_cast<BYTE*>(map.pData)+size_t(y)*map.RowPitch,size_t(width)*4);
    immediate->Unmap(staging.Get(),0);
  }
  ~WARP() { if(dc)dc->SetTarget(nullptr); }
};
std::vector<BYTE> Pixels(Gdiplus::Bitmap& bitmap) {
  const int w=bitmap.GetWidth(),h=bitmap.GetHeight();std::vector<BYTE> pixels(size_t(w)*h*4);
  Gdiplus::Rect rect(0,0,w,h);Gdiplus::BitmapData bits={};
  GD(bitmap.LockBits(&rect,Gdiplus::ImageLockModeRead,PixelFormat32bppPARGB,&bits));
  for(int y=0;y<h;y++)std::memcpy(pixels.data()+size_t(y)*w*4,static_cast<BYTE*>(bits.Scan0)+ptrdiff_t(y)*bits.Stride,size_t(w)*4);
  GD(bitmap.UnlockBits(&bits));return pixels;
}
void Run(const wchar_t* image) {
  weasel::SkinImageLoadError error;auto source=weasel::LoadSkinBitmap(image,error);
  if(!source)throw std::runtime_error(std::string("PNG ")+error.stage);
  WARP warp;
  std::ofstream report("render-backend-benchmark.csv");
  report<<"backend,workload,dpi,annotation,content_width,submit_p50_ms,complete_p50_ms,complete_p95_ms,mean_pixel_difference,max_pixel_difference\n";
  for(int dpi : {96,144,192})for(bool annotation : {false,true})for(int content : {600,1800})for(bool resizing : {false,true}) {
    const float scale=dpi/96.f;const int deco=int(80*scale),bodyHeight=int((annotation?56:36)*scale);
    const int cw=int(content*scale),w=cw+deco+8,h=std::max(bodyHeight,deco)+8;
    auto decal=weasel::ScaleSkinBitmap(*source,deco,deco,Gdiplus::PixelOffsetModeHighQuality);
    if(!decal)throw std::runtime_error("Cannot scale PNG");auto decalPixels=Pixels(*decal);
    // Identical pre-rendered bases and fitted PNG pixels are inputs to both paths.
    std::vector<std::vector<BYTE>> bases;
    for(int extra : {0,16}) {
      const int bw=w+extra;std::vector<BYTE> pixels(size_t(bw)*h*4);
      Gdiplus::Bitmap bitmap(bw,h,bw*4,PixelFormat32bppPARGB,pixels.data());
      {Gdiplus::Graphics graphics(&bitmap);Gdiplus::SolidBrush background(Gdiplus::Color(255,245,250,255));
       GD(graphics.FillRectangle(&background,0,h-bodyHeight,bw-deco,bodyHeight));GD(graphics.Flush(Gdiplus::FlushIntentionSync));}
      bases.push_back(std::move(pixels));
    }
    std::vector<ComPtr<ID2D1Bitmap1>> uploaded;
    for(int i=0;i<2;i++)uploaded.push_back(warp.Upload(w+i*16,h,bases[i]));
    warp.decal=warp.Upload(deco,deco,decalPixels);
    std::vector<BYTE> cpuResult,d2Result;double mean=0;int maximum=0;
    for(bool d2 : {false,true}) {
      std::vector<double> submit,complete;
      for(int i=0;i<45;i++) {
        const int index=resizing?i%2:0,bw=w+index*16;std::vector<BYTE>& pixels=d2?d2Result:cpuResult;
        const int hx=(i%6)*int(70*scale),hy=h-bodyHeight+4,hw=int(60*scale),hh=bodyHeight-8;
        auto start=Clock::now();pixels.resize(size_t(bw)*h*4);
        if(d2) {
          warp.Resize(bw,h);warp.dc->BeginDraw();warp.dc->Clear(D2D1::ColorF(0,0.f));
          warp.dc->DrawBitmap(uploaded[index].Get(),D2D1::RectF(0,0,float(bw),float(h)),1,D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
          warp.dc->FillRectangle(D2D1::RectF(float(hx),float(hy),float(hx+hw),float(hy+hh)),warp.brush.Get());
          warp.dc->DrawBitmap(warp.decal.Get(),D2D1::RectF(float(bw-deco),0,float(bw),float(deco)),1,D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
          HR(warp.dc->EndDraw());const double submitted=Ms(start);
          warp.Complete(pixels);if(i>=5){submit.push_back(submitted);complete.push_back(Ms(start));}
        } else {
          std::memcpy(pixels.data(),bases[index].data(),pixels.size());
          Gdiplus::Bitmap bitmap(bw,h,bw*4,PixelFormat32bppPARGB,pixels.data());
          {Gdiplus::Graphics graphics(&bitmap);Gdiplus::SolidBrush highlight(Gdiplus::Color(255,51,128,204));
           GD(graphics.FillRectangle(&highlight,hx,hy,hw,hh));
           graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);
           GD(graphics.DrawImage(decal.get(),bw-deco,0,deco,deco));GD(graphics.Flush(Gdiplus::FlushIntentionSync));}
          const double elapsed=Ms(start);if(i>=5){submit.push_back(elapsed);complete.push_back(elapsed);}
        }
      }
      if(d2) {
        if(cpuResult.size()!=d2Result.size())throw std::runtime_error("Readback geometry differs");
        double delta=0;for(size_t i=0;i<cpuResult.size();i++){int d=std::abs(int(cpuResult[i])-int(d2Result[i]));delta+=d;maximum=std::max(maximum,d);}
        mean=delta/cpuResult.size();
        // Same transparent corner and fully opaque candidate background must survive readback.
        if(d2Result[3]!=0||d2Result[(size_t(h-2)*warp.width+1)*4+3]!=255)throw std::runtime_error("Alpha/canvas readback failed");
        if(mean>2.0)throw std::runtime_error("Compositors disagree beyond rounding tolerance");
      }
      report<<(d2?"d2d-warp-cached":"gdi-cached")<<','<<(resizing?"resizing":"steady")<<','<<dpi<<','<<annotation<<','<<content<<','
            <<Percentile(submit,50)<<','<<Percentile(complete,50)<<','<<Percentile(complete,95)<<','<<mean<<','<<maximum<<'\n';
    }
  }
  if(!report.good())throw std::runtime_error("Cannot save benchmark CSV");
}
int wmain(int argc,wchar_t** argv) {
  if(argc!=2)return 2;HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(FAILED(com))return 3;
  Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;
  if(Gdiplus::GdiplusStartup(&token,&input,nullptr)!=Gdiplus::Ok){CoUninitialize();return 4;}
  int result=0;try { Run(argv[1]);std::cout<<"PASS isolated WARP/GDI cached compositor; see CSV (no text, DComp or Rime)\n"; }
  catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
  Gdiplus::GdiplusShutdown(token);CoUninitialize();return result;
}
