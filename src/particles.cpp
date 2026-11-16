#include "particles.hpp"
#include <array>
namespace lb::particles {
void Model::move(POINT point,double elapsed) {
    double dx=point.x-previous_.x,dy=point.y-previous_.y,distance=std::hypot(dx,dy);bool first=!initialized_;if(first||distance>240) { items.clear();dx=dy=distance=0;first=true; }previous_=point;initialized_=true;if(!first&&distance<.5) return;
    double dt=std::clamp(elapsed,.016,.05);unsigned count=style==1?unsigned(std::clamp(int(std::nearbyint(distance/14)),1,2)):1;
    if(style==2) items.clear();
    for(unsigned n=0;n<count;++n) { double phase=style==1?++serial_*2.39996323:0,ratio=double(n+1)/count;Particle p;p.x=point.x-dx*(1-ratio);p.y=point.y-dy*(1-ratio);p.life=style==0?.42:style==1?1.25:1.05;p.phase=phase;p.size=style==0?2.2:style==1?.55+.3*(.5+.5*std::sin(phase)):1;
        if(style==1) { p.vx=std::clamp(dx/dt*.07,-42.,42.)+std::sin(phase)*10;p.vy=std::clamp(dy/dt*.04,-25.,15.)-10; }items.push_back(p);
    }double extent=style==0?190:270;std::erase_if(items,[&](auto const& p){return std::abs(p.x-point.x)>=extent||std::abs(p.y-point.y)>=extent;});size_t cap=style==0?20:style==1?64:1;if(items.size()>cap) items.erase(items.begin(),items.end()-ptrdiff_t(cap));
}
void Model::advance(double dt) {
    if(dt>.3) { items.clear();return; }double elapsed=std::max(0.,dt);dt=std::clamp(dt,0.,.05);
    for(auto &p:items) { p.age+=elapsed;if(style==1) { double drag=std::exp(-2.6*dt);p.x+=p.vx*(1-drag)/2.6;p.y+=p.vy*dt+55*dt*dt;p.vx*=drag;p.vy+=110*dt; } }
    std::erase_if(items,[](auto const& p){return p.age>=p.life;});
}
namespace {
struct Shape { HCURSOR cursor{};int width{},height{},hot_x{},hot_y{};std::vector<BYTE> edge; };
struct Overlay {
    HWND window{};HHOOK hook{};HDC dc{};unique_gdi<HBITMAP> bitmap;HGDIOBJ old{};uint32_t* pixels{};Model model;COLORREF color{};ULONGLONG time{},move_time{};POINT latest{};bool pending{},animating{};std::vector<Shape> shapes;double scale{1};int width{1},height{1};
} state;
constexpr int extent=1024,margin=12;
LRESULT CALLBACK hook(int,WPARAM,LPARAM);
std::vector<BYTE> blur(std::vector<BYTE> const& source,int width,int height,int radius) {
    std::vector<BYTE> horizontal(source.size()),result(source.size());for(int y=0;y<height;++y) { int sum=0;for(int x=-radius;x<width+radius;++x) { if(x+radius<width) sum+=source[size_t(y)*width+std::max(0,x+radius)];if(x-radius-1>=0) sum-=source[size_t(y)*width+x-radius-1];if(x>=0&&x<width) horizontal[size_t(y)*width+x]=BYTE(sum/(radius*2+1)); } }
    for(int x=0;x<width;++x) { int sum=0;for(int y=-radius;y<height+radius;++y) { if(y+radius<height) sum+=horizontal[size_t(std::max(0,y+radius))*width+x];if(y-radius-1>=0) sum-=horizontal[size_t(y-radius-1)*width+x];if(y>=0&&y<height) result[size_t(y)*width+x]=BYTE(sum/(radius*2+1)); } }return result;
}
Shape const* shape(HCURSOR cursor) {
    auto found=std::find_if(state.shapes.begin(),state.shapes.end(),[&](auto const& value){return value.cursor==cursor;});if(found!=state.shapes.end()) return &*found;
    ICONINFO info{};if(!GetIconInfo(cursor,&info)) return nullptr;unique_gdi<HBITMAP> color(info.hbmColor),mask(info.hbmMask);BITMAP bits{};if(!GetObjectW(color?color.get():mask.get(),sizeof bits,&bits)) return nullptr;
    int w=bits.bmWidth,h=color?bits.bmHeight:bits.bmHeight/2;if(w<1||h<1||w>256||h>256) return nullptr;Shape value;value.cursor=cursor;value.width=w+2*margin;value.height=h+2*margin;value.hot_x=int(info.xHotspot)+margin;value.hot_y=int(info.yHotspot)+margin;
    BITMAPINFO format{};format.bmiHeader={sizeof(BITMAPINFOHEADER),w,-h,1,32,BI_RGB,0,0,0,0,0};void* pixels=nullptr;unique_gdi<HBITMAP> bitmap(CreateDIBSection(nullptr,&format,DIB_RGB_COLORS,&pixels,nullptr,0));HDC dc=CreateCompatibleDC(nullptr);if(!bitmap||!dc) { if(dc) DeleteDC(dc);return nullptr; }auto old=SelectObject(dc,bitmap.get());scope_exit cleanup([&]{SelectObject(dc,old);DeleteDC(dc);});
    auto data=static_cast<uint32_t*>(pixels);std::fill_n(data,w*h,0xff000000);DrawIconEx(dc,0,0,cursor,w,h,0,nullptr,DI_NORMAL);std::vector<uint32_t> black(data,data+w*h);std::fill_n(data,w*h,0xffffffff);DrawIconEx(dc,0,0,cursor,w,h,0,nullptr,DI_NORMAL);
    std::vector<BYTE> alpha(size_t(value.width)*value.height,0);for(int y=0;y<h;++y) for(int x=0;x<w;++x) { size_t n=size_t(y)*w+x;int difference=0;for(int shift:{0,8,16}) difference=std::max(difference,int((data[n]>>shift)&255)-int((black[n]>>shift)&255));alpha[size_t(y+margin)*value.width+x+margin]=BYTE(std::clamp(255-difference,0,255)); }
    auto soft=blur(blur(alpha,value.width,value.height,3),value.width,value.height,3);value.edge.resize(alpha.size());for(size_t n=0;n<alpha.size();++n) value.edge[n]=BYTE(std::min(255,int(soft[n])*3)*(255-int(alpha[n]))/255);
    if(state.shapes.size()>=16) state.shapes.erase(state.shapes.begin());state.shapes.push_back(std::move(value));return &state.shapes.back();
}
void pixel(int x,int y,double alpha) {
    if(x<0||y<0||x>=state.width||y>=state.height||alpha<=0) return;unsigned a=unsigned(std::clamp(alpha,0.,255.));auto &dst=state.pixels[size_t(y)*extent+x];unsigned old_a=dst>>24,inv=255-a;unsigned r=GetRValue(state.color)*a/255+((dst>>16)&255)*inv/255,g=GetGValue(state.color)*a/255+((dst>>8)&255)*inv/255,b=GetBValue(state.color)*a/255+(dst&255)*inv/255;dst=((a+old_a*inv/255)<<24)|(r<<16)|(g<<8)|b;
}
void draw() {
    if(!state.window||state.model.items.empty()) { if(state.window) { ShowWindow(state.window,SW_HIDE);KillTimer(state.window,1);state.animating=false;if(!state.hook) state.hook=SetWindowsHookExW(WH_MOUSE_LL,hook,g_app.instance,0); }return; }
    CURSORINFO cursor{sizeof cursor};if(!GetCursorInfo(&cursor)||!(cursor.flags&CURSOR_SHOWING)) { ShowWindow(state.window,SW_HIDE);return; }
    double min_x=cursor.ptScreenPos.x-160.,max_x=cursor.ptScreenPos.x+160.,min_y=cursor.ptScreenPos.y-160.,max_y=cursor.ptScreenPos.y+160.;if(state.model.style!=2) { min_x=min_y=1e20;max_x=max_y=-1e20;for(auto const& p:state.model.items) { min_x=std::min(min_x,p.x*state.scale-10*state.scale);max_x=std::max(max_x,p.x*state.scale+10*state.scale);min_y=std::min(min_y,p.y*state.scale-10*state.scale);max_y=std::max(max_y,p.y*state.scale+10*state.scale); } }POINT origin{LONG(std::floor(min_x)),LONG(std::floor(min_y))};state.width=std::clamp(int(std::ceil(max_x-min_x+1)),1,extent);state.height=std::clamp(int(std::ceil(max_y-min_y+1)),1,extent);for(int y=0;y<state.height;++y) std::fill_n(state.pixels+size_t(y)*extent,state.width,0);
    if(state.model.style==2) {
        if(auto silhouette=shape(cursor.hCursor)) { auto const& p=state.model.items.back();double pulse=.58+.42*std::sin(double(GetTickCount64())/150.);double fade=std::clamp(1-p.age/p.life,0.,1.);int left=cursor.ptScreenPos.x-silhouette->hot_x-origin.x,top=cursor.ptScreenPos.y-silhouette->hot_y-origin.y;
            for(int y=0;y<silhouette->height;++y) for(int x=0;x<silhouette->width;++x) pixel(left+x,top+y,silhouette->edge[size_t(y)*silhouette->width+x]*pulse*fade);
        }
    }else for(auto const& p:state.model.items) {
        double age=std::clamp(p.age/p.life,0.,1.),fade=state.model.style==1?std::min(1.,age/.08)*std::pow(1-age,1.6):std::pow(1-age,2),radius=(state.model.style==1?p.size:1.2+1.7*(1-age))*state.scale;double x=p.x*state.scale-origin.x,y=p.y*state.scale-origin.y;int bound=int(std::ceil(radius*(state.model.style==1?3.2:1)+1));
        double alpha=state.model.style==1?185+25*std::pow(std::sin(GetTickCount64()/1000.*5+p.phase),2):150,tail=std::clamp(p.vy/80.,0.,1.6)*state.scale;
        for(int dy=-bound;dy<=bound;++dy) for(int dx=-bound;dx<=bound;++dx) {
            int px=int(std::floor(x))+dx,py=int(std::floor(y))+dy;double distance=std::hypot(px+.5-x,py+.5-y);
            if(state.model.style==1) pixel(px,py,28*fade*std::clamp(radius*3.2+.5-distance,0.,1.));
            pixel(px,py,alpha*fade*std::clamp(radius+.5-distance,0.,1.));
            // Match the reference's subtle 0.55 px round-ended vertical tail.
            // It lengthens only as gravity produces downward velocity.
            if(state.model.style==1&&tail>0) { double nearest=std::clamp(py+.5,y-tail,y);double line_distance=std::hypot(px+.5-x,py+.5-nearest);pixel(px,py,60*fade*std::clamp(.275*state.scale+.5-line_distance,0.,1.)); }
        }
    }
    SIZE size{state.width,state.height};POINT source{};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};UpdateLayeredWindow(state.window,nullptr,&origin,&size,state.dc,&source,0,&blend,ULW_ALPHA);SetWindowPos(state.window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
}
LRESULT CALLBACK hook(int code,WPARAM wp,LPARAM lp) {
    if(code>=0&&wp==WM_MOUSEMOVE) { state.latest=reinterpret_cast<MSLLHOOKSTRUCT*>(lp)->pt;if(!state.pending) { state.pending=PostMessageW(state.window,WM_APP+146,0,0)!=FALSE; } }return CallNextHookEx(state.hook,code,wp,lp);
}
LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_APP+146||(message==WM_TIMER&&wp==1)) { state.pending=false;if(!state.animating) { if(state.hook) UnhookWindowsHookEx(std::exchange(state.hook,nullptr));state.animating=true;SetTimer(window,1,33,nullptr); }auto now=GetTickCount64();double dt=state.time?(now-state.time)/1000.:0;state.model.advance(dt);POINT point{};GetCursorPos(&point);state.model.move({LONG(std::lround(point.x/state.scale)),LONG(std::lround(point.y/state.scale))},dt);state.time=now;draw();if(state.model.items.empty()) state.time=0;return 0; }
    if(message==WM_NCHITTEST) return HTTRANSPARENT;if(message==WM_MOUSEACTIVATE) return MA_NOACTIVATE;if(message==WM_ERASEBKGND) return 1;return DefWindowProcW(window,message,wp,lp);
}
}
bool configure() {
    if(!g_app.settings.cursor_particles) { shutdown();return true; }state.color=g_app.settings.particle_color;if(state.model.style!=g_app.settings.particle_style) { state.model=Model{};ShowWindow(state.window,SW_HIDE); }state.model.style=g_app.settings.particle_style;
    if(state.window) return true;state.scale=GetDpiForSystem()/96.;WNDCLASSW cls{};cls.hInstance=g_app.instance;cls.lpfnWndProc=proc;cls.lpszClassName=L"LightBlockDesktopNative.Particles";RegisterClassW(&cls);
    state.window=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TOPMOST,cls.lpszClassName,L"",WS_POPUP,0,0,extent,extent,nullptr,nullptr,g_app.instance,nullptr);if(!state.window) return false;SetWindowDisplayAffinity(state.window,0x11);
    BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),extent,-extent,1,32,BI_RGB,0,0,0,0,0};void* pixels=nullptr;state.bitmap.reset(CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0));state.pixels=static_cast<uint32_t*>(pixels);state.dc=CreateCompatibleDC(nullptr);if(state.bitmap&&state.dc) state.old=SelectObject(state.dc,state.bitmap.get());state.hook=SetWindowsHookExW(WH_MOUSE_LL,hook,g_app.instance,0);if(!state.bitmap||!state.dc||!state.hook) { shutdown();return false; }return true;
}
void shutdown() { if(state.hook) { UnhookWindowsHookEx(state.hook);state.hook=nullptr; }if(state.window) { KillTimer(state.window,1);DestroyWindow(state.window);state.window=nullptr; }if(state.dc) { SelectObject(state.dc,state.old);DeleteDC(state.dc);state.dc=nullptr; }state.bitmap.reset();state.pixels=nullptr;state.model.clear();state.shapes.clear();state.pending=false;state.animating=false;state.time=state.move_time=0; }
bool tests() {
    Model model;for(int style=0;style<3;++style) { model.clear();model.style=style;model.move({0,0},.01);for(int n=1;n<120;++n) { model.advance(.01);model.move({n,0},.01); }if(model.items.empty()||model.items.size()>size_t(style==0?20:style==1?64:1)) return false;if(style==1&&model.items.front().y<=0) return false;for(int n=0;n<50;++n) model.advance(.05);if(!model.items.empty()) return false;model.move({1000,0},.01);if(model.items.size()!=1) return false; }return true;
}
}
