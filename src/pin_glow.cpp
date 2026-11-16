#include "pin_glow.hpp"
#include <gdiplus.h>

namespace lb::capture {
namespace {
bool graphics_ready(){
    struct Runtime{ULONG_PTR token{};Runtime(){Gdiplus::GdiplusStartupInput input;Gdiplus::GdiplusStartup(&token,&input,nullptr);}~Runtime(){if(token)Gdiplus::GdiplusShutdown(token);}};
    static Runtime runtime;return runtime.token!=0;
}
std::array<RECT,4> strips(int width,int height,int margin){
    return {{{0,0,width,margin},{0,height-margin,width,height},{0,margin,margin,height-margin},{width-margin,margin,width,height-margin}}};
}
unique_gdi<HBITMAP> render(int width,int height,int margin,RECT strip){
    int w=strip.right-strip.left,h=strip.bottom-strip.top;if(w<1||h<1||!graphics_ready())return {};
    BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),w,-h,1,32,BI_RGB,0,0,0,0,0};void* pixels{};
    unique_gdi<HBITMAP> image(CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0));if(!image)return {};
    memset(pixels,0,size_t(w)*h*4);
    Gdiplus::Bitmap bitmap(w,h,w*4,PixelFormat32bppPARGB,static_cast<BYTE*>(pixels));
    Gdiplus::Graphics painter(&bitmap);painter.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    painter.TranslateTransform(float(-strip.left),float(-strip.top));
    float scale=margin/5.f;
    for(auto layer:std::array<std::pair<float,BYTE>,3>{{{1.5f,18},{2.5f,32},{4.f,110}}}){
        float inset=layer.first*scale,radius=4.f*scale,x=inset,y=inset,wf=width-2*inset,hf=height-2*inset;
        float diameter=std::min(2*radius,std::min(wf,hf));if(diameter<=0)continue;
        Gdiplus::GraphicsPath path;
        path.AddArc(x,y,diameter,diameter,180,90);path.AddArc(x+wf-diameter,y,diameter,diameter,270,90);
        path.AddArc(x+wf-diameter,y+hf-diameter,diameter,diameter,0,90);path.AddArc(x,y+hf-diameter,diameter,diameter,90,90);path.CloseFigure();
        Gdiplus::Pen pen(Gdiplus::Color(layer.second,145,229,218),scale);painter.DrawPath(&pen,&path);
    }
    painter.Flush(Gdiplus::FlushIntentionSync);return image;
}
LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    if(message==WM_NCHITTEST)return HTTRANSPARENT;
    if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    return DefWindowProcW(window,message,wp,lp);
}
}
PinGlow::~PinGlow(){reset();}
void PinGlow::reset(){for(auto& window:windows_)if(auto owned=std::exchange(window,nullptr);IsWindow(owned))DestroyWindow(owned);width_=height_=margin_=0;clipped_=false;}
bool PinGlow::update(HWND owner,bool border,int margin,double opacity){
    if(updating_)return clipped_;updating_=true;scope_exit done([&]{updating_=false;});
    if(!IsWindow(owner))return false;
    auto disable=[&]{for(auto window:windows_)if(IsWindow(window))ShowWindow(window,SW_HIDE);if(clipped_){SetWindowRgn(owner,nullptr,TRUE);clipped_=false;}width_=height_=0;};
    if(!border||!IsWindowVisible(owner)||IsIconic(owner)){disable();return false;}
    RECT bounds{};GetWindowRect(owner,&bounds);int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
    if(margin<1||width<=2*margin||height<=2*margin){disable();return false;}
    BYTE alpha=BYTE(std::lround(std::clamp(opacity,.25,1.)*255));bool redraw=width!=width_||height!=height_||margin!=margin_||alpha!=alpha_;
    WNDCLASSW cls{};cls.hInstance=g_app.instance;cls.lpfnWndProc=proc;cls.lpszClassName=L"LightBlockDesktopNative.PinGlow";RegisterClassW(&cls);
    auto regions=strips(width,height,margin);
    for(size_t n=0;n<regions.size();++n){
        if(!IsWindow(windows_[n])){windows_[n]=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT,cls.lpszClassName,L"",WS_POPUP,0,0,1,1,owner,nullptr,g_app.instance,nullptr);redraw=true;}
        if(!windows_[n]){disable();return false;}
        auto region=regions[n];POINT position{bounds.left+region.left,bounds.top+region.top};SIZE size{region.right-region.left,region.bottom-region.top};
        if(redraw){
            auto image=render(width,height,margin,region);HDC dc=CreateCompatibleDC(nullptr);if(!image||!dc){if(dc)DeleteDC(dc);disable();return false;}
            auto previous=SelectObject(dc,image.get());POINT origin{};BLENDFUNCTION blend{AC_SRC_OVER,0,alpha,AC_SRC_ALPHA};
            bool ok=UpdateLayeredWindow(windows_[n],nullptr,&position,&size,dc,&origin,0,&blend,ULW_ALPHA)!=FALSE;
            SelectObject(dc,previous);DeleteDC(dc);if(!ok){disable();return false;}
        }
        SetWindowPos(windows_[n],HWND_TOPMOST,position.x,position.y,size.cx,size.cy,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    }
    if(!clipped_||width!=width_||height!=height_||margin!=margin_){
        auto clip=CreateRectRgn(margin,margin,width-margin,height-margin);
        if(!clip||!SetWindowRgn(owner,clip,TRUE)){if(clip)DeleteObject(clip);disable();return false;}
        clipped_=true;
    }
    width_=width;height_=height;margin_=margin;alpha_=alpha;return true;
}
bool PinGlow::tests(){
    for(int margin:{5,8,10,15}){
        int width=500,height=300;auto regions=strips(width,height,margin);size_t allocated{};
        for(auto region:regions){auto bitmap=render(width,height,margin,region);BITMAP b{};if(!bitmap||!GetObjectW(bitmap.get(),sizeof b,&b)||!b.bmBits)return false;allocated+=size_t(b.bmWidth)*b.bmHeight;bool visible=false;
            auto pixels=static_cast<uint32_t*>(b.bmBits);for(int n=0;n<b.bmWidth*b.bmHeight;++n){auto pixel=pixels[n];unsigned a=pixel>>24;if(a){visible=true;if(a>160||((pixel>>16)&255)>a||((pixel>>8)&255)>a||(pixel&255)>a)return false;}}
            if(!visible)return false;
        }
        if(allocated!=size_t(width)*height-size_t(width-2*margin)*(height-2*margin))return false;
    }
    // Synthetic visual evidence, never a capture of the user's desktop.
    constexpr int width=500,height=300,margin=8,padding=24,w=width+2*padding,h=height+2*padding;
    BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),w,-h,1,32,BI_RGB,0,0,0,0,0};void* bits{};
    unique_gdi<HBITMAP> evidence(CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&bits,nullptr,0));if(!evidence)return false;
    auto canvas=static_cast<uint32_t*>(bits);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){bool paper=x>=padding+margin&&x<padding+width-margin&&y>=padding+margin&&y<padding+height-margin;int r=paper?32+x/6:22,g=paper?60+y/4:27,b=paper?95+y/5:35;canvas[y*w+x]=0xff000000u|uint32_t(r<<16|g<<8|b);}
    for(auto region:strips(width,height,margin)){auto image=render(width,height,margin,region);BITMAP bitmap{};if(!image||!GetObjectW(image.get(),sizeof bitmap,&bitmap))return false;auto pixels=static_cast<uint32_t*>(bitmap.bmBits);
        for(int y=0;y<bitmap.bmHeight;++y)for(int x=0;x<bitmap.bmWidth;++x){auto pixel=pixels[y*bitmap.bmWidth+x];unsigned a=pixel>>24;auto& dest=canvas[(padding+region.top+y)*w+padding+region.left+x];uint32_t result=0xff000000u;for(int shift:{0,8,16})result|=std::min(255u,((pixel>>shift)&255)+(((dest>>shift)&255)*(255-a)+127)/255)<<shift;dest=result;}
    }
    wchar_t path[LB_PATH];path_join(path,LB_PATH,g_app.data,L"pin-glow-preview.png");return save_png(evidence.get(),path)!=FALSE;
}
}
