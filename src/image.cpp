#include "lb.h"
#include <propvarutil.h>

namespace {
UINT metadata(IWICMetadataQueryReader *reader,const wchar_t *name,UINT fallback) {
    PROPVARIANT value{};lb::scope_exit clear([&]{PropVariantClear(&value);});
    if(reader && SUCCEEDED(reader->GetMetadataByName(name,&value))) { switch(value.vt) {
    case VT_UI1:return value.bVal;case VT_UI2:return value.uiVal;case VT_UI4:return value.ulVal;case VT_BOOL:return value.boolVal!=VARIANT_FALSE;default:break;
    } } return fallback;
}
bool valid_size(UINT width,UINT height) noexcept { return width && height && width<=16384 && height<=16384 && uint64_t(width)*height<=32u*1024u*1024u; }
}
void image_close(Image *image) { *image=Image{}; }
HRESULT image_open(Image *image,const wchar_t *path) {
    Image result;winrt::com_ptr<IWICBitmapFrameDecode> frame;winrt::com_ptr<IWICMetadataQueryReader> reader;
    HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,result.factory.put_void());if(FAILED(hr)) return hr;
    hr=result.factory->CreateDecoderFromFilename(path,nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,result.decoder.put());if(FAILED(hr)) return hr;
    hr=result.decoder->GetFrameCount(&result.count);if(FAILED(hr)||!result.count) return WINCODEC_ERR_BADIMAGE;
    GUID format{};result.decoder->GetContainerFormat(&format);result.gif=IsEqualGUID(format,GUID_ContainerFormatGif);
    hr=result.decoder->GetFrame(0,frame.put());if(FAILED(hr)) return hr;hr=frame->GetSize(&result.width,&result.height);if(FAILED(hr)) return hr;
    if(result.gif && SUCCEEDED(result.decoder->GetMetadataQueryReader(reader.put()))) {
        result.width=metadata(reader.get(),L"/logscrdesc/Width",result.width);result.height=metadata(reader.get(),L"/logscrdesc/Height",result.height);
        UINT index=metadata(reader.get(),L"/logscrdesc/BackgroundColorIndex",0);winrt::com_ptr<IWICMetadataQueryReader> frame_reader;bool transparent=false;
        if(SUCCEEDED(frame->GetMetadataQueryReader(frame_reader.put()))) transparent=metadata(frame_reader.get(),L"/grctlext/TransparencyFlag",0) && metadata(frame_reader.get(),L"/grctlext/TransparentColorIndex",256)==index;
        winrt::com_ptr<IWICPalette> palette;
        if(!transparent && SUCCEEDED(result.factory->CreatePalette(palette.put())) && SUCCEEDED(result.decoder->CopyPalette(palette.get()))) { WICColor colors[256];UINT actual=0;if(SUCCEEDED(palette->GetColors(256,colors,&actual)) && index<actual) result.background=colors[index]|0xff000000u; }
    }
    if(!valid_size(result.width,result.height)) return WINCODEC_ERR_IMAGESIZEOUTOFRANGE;
    result.bytes=size_t(result.width)*result.height*4;result.pixels.reset(new(std::nothrow) uint32_t[result.bytes/4]{});if(!result.pixels) return E_OUTOFMEMORY;
    hr=image_next(&result);if(SUCCEEDED(hr)) *image=std::move(result);return hr;
}
HRESULT image_next(Image *image) {
    if(!image->decoder || !image->pixels) return E_UNEXPECTED;
    if(image->index==0) std::fill_n(image->pixels.get(),image->bytes/4,image->background);
    else if(image->disposal==2||image->disposal==3) {
        auto rect=image->previous;
        auto span=size_t(rect.right-rect.left);
        for(LONG y=rect.top;y<rect.bottom;++y) {
            auto row=image->pixels.get()+size_t(y)*image->width+rect.left;
            if(image->disposal==2) std::fill_n(row,span,image->background);
            else std::copy_n(image->backup.get()+size_t(y-rect.top)*span,span,row);
        }
    }
    winrt::com_ptr<IWICBitmapFrameDecode> frame;winrt::com_ptr<IWICFormatConverter> converter;winrt::com_ptr<IWICMetadataQueryReader> reader;
    UINT width=0,height=0,left=0,top=0,disposal=0,delay=100;bool transparent=image->gif;
    HRESULT hr=image->decoder->GetFrame(image->index,frame.put());if(FAILED(hr)) return hr;hr=frame->GetSize(&width,&height);if(FAILED(hr)) return hr;
    if(image->gif && SUCCEEDED(frame->GetMetadataQueryReader(reader.put()))) { left=metadata(reader.get(),L"/imgdesc/Left",0);top=metadata(reader.get(),L"/imgdesc/Top",0);disposal=metadata(reader.get(),L"/grctlext/Disposal",0);transparent=metadata(reader.get(),L"/grctlext/TransparencyFlag",0)!=0;UINT ticks=metadata(reader.get(),L"/grctlext/Delay",10);delay=ticks?std::max(10u,ticks*10):100; }
    if(!valid_size(width,height)||left>=image->width||top>=image->height||width>image->width-left||height>image->height-top) return WINCODEC_ERR_BADIMAGE;
    if(disposal==3) {
        const size_t needed=size_t(width)*height;
        if(needed>image->backup_capacity) {
            auto buffer=std::unique_ptr<uint32_t[]>(new(std::nothrow) uint32_t[needed]);
            if(!buffer) return E_OUTOFMEMORY;
            image->backup=std::move(buffer);image->backup_capacity=needed;
        }
        for(UINT y=0;y<height;++y)
            std::copy_n(image->pixels.get()+size_t(top+y)*image->width+left,width,image->backup.get()+size_t(y)*width);
    }
    hr=image->factory->CreateFormatConverter(converter.put());if(FAILED(hr)) return hr;
    hr=converter->Initialize(frame.get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom);if(FAILED(hr)) return hr;
    // Full opaque GIF frames and still images can decode into the canvas itself.
    // Delta/transparent GIFs retain one bounded scratch buffer, not a new large
    // heap allocation for every frame. Disposal-3 backup remains independent.
    bool direct=width==image->width&&!left&&(!image->gif||!transparent);
    if(direct) {
        hr=converter->CopyPixels(nullptr,width*4,width*height*4,reinterpret_cast<BYTE*>(image->pixels.get()+size_t(top)*width));if(FAILED(hr)) return hr;
    }else {
    size_t needed=size_t(width)*height;
    if(needed>image->scratch_capacity){auto buffer=std::unique_ptr<uint32_t[]>(new(std::nothrow)uint32_t[needed]);if(!buffer)return E_OUTOFMEMORY;image->scratch=std::move(buffer);image->scratch_capacity=needed;}
    hr=converter->CopyPixels(nullptr,width*4,width*height*4,reinterpret_cast<BYTE*>(image->scratch.get()));if(FAILED(hr)) return hr;
    for(UINT y=0;y<height;++y) for(UINT x=0;x<width;++x) {
        uint32_t source=image->scratch[size_t(y)*width+x],&dest=image->pixels[size_t(y+top)*image->width+x+left];UINT a=source>>24;
        if(a==255) dest=source;else if(a) { UINT inverse=255-a,b=(source&255)+((dest&255)*inverse+127)/255,g=((source>>8)&255)+(((dest>>8)&255)*inverse+127)/255,r=((source>>16)&255)+(((dest>>16)&255)*inverse+127)/255,alpha=a+((dest>>24)*inverse+127)/255;dest=(alpha<<24)|(r<<16)|(g<<8)|b; }
    }
    }
    image->previous=RECT{LONG(left),LONG(top),LONG(left+width),LONG(top+height)};image->disposal=disposal;image->delay=delay;image->index=(image->index+1)%image->count;return S_OK;
}
void image_draw(HDC dc,const Image *image,RECT bounds,int mode) {
    if(!image->pixels) return;RECT target=fit_rect(bounds,image->width,image->height,mode==0);
    if(mode==2) target=bounds;else if(mode==3) { target.left=bounds.left+((bounds.right-bounds.left)-LONG(image->width))/2;target.top=bounds.top+((bounds.bottom-bounds.top)-LONG(image->height))/2;target.right=target.left+LONG(image->width);target.bottom=target.top+LONG(image->height); }
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=LONG(image->width);info.bmiHeader.biHeight=-LONG(image->height);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    int saved=SaveDC(dc);lb::scope_exit restore([&]{RestoreDC(dc,saved);});IntersectClipRect(dc,bounds.left,bounds.top,bounds.right,bounds.bottom);SetStretchBltMode(dc,HALFTONE);SetBrushOrgEx(dc,0,0,nullptr);
    StretchDIBits(dc,target.left,target.top,target.right-target.left,target.bottom-target.top,0,0,image->width,image->height,image->pixels.get(),&info,DIB_RGB_COLORS,SRCCOPY);
}
HBITMAP image_thumbnail(const wchar_t *path,UINT width,UINT height) {
    winrt::com_ptr<IWICImagingFactory> factory;winrt::com_ptr<IWICBitmapDecoder> decoder;winrt::com_ptr<IWICBitmapFrameDecode> frame;winrt::com_ptr<IWICBitmapScaler> scaler;winrt::com_ptr<IWICFormatConverter> converter;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,factory.put_void())) || FAILED(factory->CreateDecoderFromFilename(path,nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,decoder.put())) || FAILED(decoder->GetFrame(0,frame.put()))) return nullptr;
    UINT w=0,h=0;frame->GetSize(&w,&h);if(!valid_size(w,h)) return nullptr;RECT fit=fit_rect(RECT{0,0,LONG(width),LONG(height)},w,h,FALSE);width=UINT(fit.right-fit.left);height=UINT(fit.bottom-fit.top);if(!width||!height) return nullptr;
    if(FAILED(factory->CreateBitmapScaler(scaler.put()))||FAILED(scaler->Initialize(frame.get(),width,height,WICBitmapInterpolationModeFant))||FAILED(factory->CreateFormatConverter(converter.put()))||FAILED(converter->Initialize(scaler.get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))) return nullptr;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=LONG(width);info.bmiHeader.biHeight=-LONG(height);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void *pixels=nullptr;
    lb::unique_gdi<HBITMAP> bitmap(CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0));if(!bitmap || FAILED(converter->CopyPixels(nullptr,width*4,width*height*4,static_cast<BYTE*>(pixels)))) return nullptr;return bitmap.release();
}
BOOL save_png(HBITMAP bitmap,const wchar_t *path,bool alpha) {
    wchar_t temp[LB_PATH];if(FAILED(StringCchPrintfW(temp,LB_PATH,L"%s.partial",path))) return FALSE;
    bool committed=false;lb::scope_exit cleanup([&]{if(!committed) DeleteFileW(temp);});
    {
        winrt::com_ptr<IWICImagingFactory> factory;winrt::com_ptr<IWICBitmap> source;winrt::com_ptr<IWICStream> stream;winrt::com_ptr<IWICBitmapEncoder> encoder;winrt::com_ptr<IWICBitmapFrameEncode> frame;winrt::com_ptr<IPropertyBag2> bag;
        if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,factory.put_void()))||FAILED(factory->CreateBitmapFromHBITMAP(bitmap,nullptr,alpha?WICBitmapUsePremultipliedAlpha:WICBitmapIgnoreAlpha,source.put()))||FAILED(factory->CreateStream(stream.put()))||FAILED(stream->InitializeFromFilename(temp,GENERIC_WRITE))||FAILED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,encoder.put()))||FAILED(encoder->Initialize(stream.get(),WICBitmapEncoderNoCache))||FAILED(encoder->CreateNewFrame(frame.put(),bag.put()))||FAILED(frame->Initialize(bag.get()))) return FALSE;
        UINT width=0,height=0;source->GetSize(&width,&height);WICPixelFormatGUID format=alpha?GUID_WICPixelFormat32bppBGRA:GUID_WICPixelFormat24bppBGR;
        if(FAILED(frame->SetSize(width,height))||FAILED(frame->SetPixelFormat(&format))||FAILED(frame->WriteSource(source.get(),nullptr))||FAILED(frame->Commit())||FAILED(encoder->Commit())) return FALSE;
    }
    committed=MoveFileExW(temp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;return committed;
}
