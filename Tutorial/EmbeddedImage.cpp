#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include "EmbeddedImage.h"
#include <algorithm>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace EmbeddedImage
{
namespace
{
using Microsoft::WRL::ComPtr;
struct ComApartment
{
    HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~ComApartment()
    {
        if (SUCCEEDED(result))
            CoUninitialize();
    }
};
} // namespace
bool Decode(int resourceId, std::vector<unsigned char> &pixels, unsigned &width, unsigned &height,
            unsigned maxSide)
{
    ComApartment com;
    auto module = GetModuleHandleW(nullptr);
    auto resource = FindResourceW(module, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!resource)
        return false;
    auto bytes = SizeofResource(module, resource);
    auto data = LockResource(LoadResource(module, resource));
    if (!data || bytes == 0 || bytes > 16 * 1024 * 1024)
        return false;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromMemory(static_cast<BYTE *>(data), bytes)) ||
        FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad,
                                                &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) || FAILED(frame->GetSize(&width, &height)) || width == 0 ||
        height == 0 || width > 4096 || height > 4096 || FAILED(factory->CreateFormatConverter(&converter)))
        return false;
    IWICBitmapSource *source = frame.Get();
    ComPtr<IWICBitmapScaler> scaler;
    const unsigned longestSide = std::max(width, height);
    if (longestSide > maxSide)
    {
        width = std::max(1u, width * maxSide / longestSide);
        height = std::max(1u, height * maxSide / longestSide);
        if (FAILED(factory->CreateBitmapScaler(&scaler)) ||
            FAILED(scaler->Initialize(source, width, height, WICBitmapInterpolationModeFant)))
            return false;
        source = scaler.Get();
    }
    if (FAILED(converter->Initialize(source, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr,
                                     0, WICBitmapPaletteTypeCustom)))
        return false;
    pixels.resize(size_t(width) * height * 4);
    return SUCCEEDED(converter->CopyPixels(nullptr, width * 4, UINT(pixels.size()), pixels.data()));
}
} // namespace EmbeddedImage
