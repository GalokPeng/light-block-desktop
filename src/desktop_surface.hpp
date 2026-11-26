#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace lb::desktop_surface {
// The alpha surface, including its antialiased edge, owns the complete material.
// A rectangular DWM accent/backdrop must never be applied to this HWND.
inline BYTE coverage(int x,int y,int w,int h,float radius) {
    radius=std::clamp(radius,0.f,float(std::min(w,h))/2.f);
    if((x>=radius&&x<w-radius)||(y>=radius&&y<h-radius))return 255;
    float dx=std::max(std::abs(float(x)+.5f-w*.5f)-(w*.5f-radius),0.f);
    float dy=std::max(std::abs(float(y)+.5f-h*.5f)-(h*.5f-radius),0.f);
    return BYTE(std::lround(std::clamp(radius+.5f-std::sqrt(dx*dx+dy*dy),0.f,1.f)*255.f));
}
inline void mask(uint32_t* pixels,int stride,int w,int h,float radius,bool opaque=true,RECT const* dirty=nullptr) {
    // Only the small corner squares need distance/coverage calculations.
    const RECT area=dirty?*dirty:RECT{0,0,w,h};
    if(opaque)for(int y=area.top;y<area.bottom;++y)for(int x=area.left;x<area.right;++x)pixels[size_t(y)*stride+x]|=0xff000000u;
    int corner=int(std::ceil(std::clamp(radius,0.f,float(std::min(w,h))/2.f)));
    if((area.left>=corner&&area.right<=w-corner)||(area.top>=corner&&area.bottom<=h-corner))return;
    for(int y=0;y<corner;++y)for(int x=0;x<corner;++x){BYTE a=coverage(x,y,w,h,radius);if(a==255)continue;
        for(int yy:{y,h-1-y})for(int xx:{x,w-1-x}){if(xx<area.left||xx>=area.right||yy<area.top||yy>=area.bottom)continue;auto& p=pixels[size_t(yy)*stride+xx];if(!a)p=0;else p=((p>>24)*a/255)<<24|(((p>>16)&255)*a/255)<<16|(((p>>8)&255)*a/255)<<8|(p&255)*a/255;}}
}
// Recover GDI glyph/icon coverage from black/white passes. GDI itself does not
// consistently write alpha. Keep foreground opacity independent of the panel.
inline uint32_t composite(uint32_t background,uint32_t black,uint32_t white,BYTE opacity){
    black&=0xffffff;white&=0xffffff;
    if(black==white)return black|0xff000000u;
    if(black==0&&white==0xffffff){
        auto channel=[&](int shift){return ((background>>shift)&255)*opacity/255;};
        return (uint32_t(opacity)<<24)|(channel(16)<<16)|(channel(8)<<8)|channel(0);
    }
    unsigned remaining=255,largest=0;
    for(int shift:{0,8,16}){unsigned b=(black>>shift)&255,w=(white>>shift)&255;remaining=std::min(remaining,w>=b?w-b:0);largest=std::max(largest,b);}
    unsigned alpha=std::max(255-remaining,largest),back=unsigned(opacity)*(255-alpha)/255;
    uint32_t result=(alpha+back)<<24;
    for(int shift:{0,8,16})result|=std::min(255u,((black>>shift)&255)+((background>>shift)&255)*back/255)<<shift;
    return result;
}
class Buffer {
    HDC dc_{};HBITMAP bitmap_{};HGDIOBJ old_{};uint32_t* pixels_{};int width_{},height_{};
public:
    Buffer()=default;Buffer(Buffer const&)=delete;Buffer& operator=(Buffer const&)=delete;
    ~Buffer(){clear();}
    void clear(){if(dc_&&old_)SelectObject(dc_,old_);if(bitmap_)DeleteObject(bitmap_);if(dc_)DeleteDC(dc_);dc_=nullptr;bitmap_=nullptr;old_=nullptr;pixels_=nullptr;width_=height_=0;}
    bool reserve(int w,int h){
        if(w<=0||h<=0||w>16384||h>16384)return false;
        if(dc_&&w<=width_&&h<=height_)return true;
        int nw=(w+127)/128*128,nh=(h+127)/128*128;
        // Reuse the surface during animation; allocation happens only on growth.
        HDC dc=CreateCompatibleDC(nullptr);if(!dc)return false;
        BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),nw,-nh,1,32,BI_RGB,0,0,0,0,0};void* bits=nullptr;
        HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
        if(!bitmap){DeleteDC(dc);return false;}clear();dc_=dc;bitmap_=bitmap;pixels_=static_cast<uint32_t*>(bits);width_=nw;height_=nh;old_=SelectObject(dc_,bitmap_);return true;
    }
    HDC dc()const{return dc_;}uint32_t* pixels()const{return pixels_;}int stride()const{return width_;}
    void trim(int w,int h){if(dc_&&(width_>(w+127)/128*128||height_>(h+127)/128*128)){clear();reserve(w,h);}}
    bool present(HWND window,int w,int h,BYTE opacity,float radius,bool opaque=true,RECT const* dirty=nullptr){
        if(!dc_||w>width_||h>height_)return false;GdiFlush();mask(pixels_,width_,w,h,radius,opaque,dirty);
        POINT source{};SIZE size{w,h};BLENDFUNCTION blend{AC_SRC_OVER,0,opacity,AC_SRC_ALPHA};
        if(dirty){
            UPDATELAYEREDWINDOWINFO info{sizeof info};
            info.psize=&size;info.hdcSrc=dc_;info.pptSrc=&source;info.pblend=&blend;
            info.dwFlags=ULW_ALPHA;info.prcDirty=dirty;
            if(UpdateLayeredWindowIndirect(window,&info))return true;
        }
        return UpdateLayeredWindow(window,nullptr,nullptr,&size,dc_,&source,0,&blend,ULW_ALPHA)!=FALSE;
    }
};
inline bool tests(){
    for(BYTE opacity:{BYTE(0),BYTE(25),BYTE(128),BYTE(255)}){
        if(composite(0x00123456,0x00000000,0x00000000,opacity)!=0xff000000u||composite(0x00123456,0x00ffffff,0x00ffffff,opacity)!=0xffffffffu)return false;
        if((composite(0x00123456,0,0xffffff,opacity)>>24)!=opacity)return false;
        if(composite(0,0x00408020,0x00408020,opacity)!=0xff408020u)return false;
        auto partial=composite(0x00123456,0x00404040,0x00bfbfbf,opacity);auto a=partial>>24;
        if(a<128||(partial&255)>a||((partial>>8)&255)>a||((partial>>16)&255)>a)return false;
    }
    for(int dpi:{96,120,144,192,240}){
        int w=MulDiv(900,dpi,96),h=MulDiv(124,dpi,96);float r=18.f*dpi/96.f;Buffer b;if(!b.reserve(w,h))return false;
        auto prior=b.dc();if(!b.reserve(w-1,h-1)||prior!=b.dc())return false;
        for(int y=0;y<h;++y)std::fill_n(b.pixels()+size_t(y)*b.stride(),w,0x00abcdefu);
        mask(b.pixels(),b.stride(),w,h,r);int partial=0;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto p=b.pixels()[size_t(y)*b.stride()+x];auto a=p>>24;if(a>0&&a<255)++partial;if((p&255)>a||((p>>8)&255)>a||((p>>16)&255)>a)return false;}
        if(partial<4||b.pixels()[0]||b.pixels()[w-1]||b.pixels()[size_t(h-1)*b.stride()]||b.pixels()[size_t(h-1)*b.stride()+w-1]||b.pixels()[size_t(h/2)*b.stride()+w/2]!=0xffabcdefu)return false;
    }return true;
}
}
