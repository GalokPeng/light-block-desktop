#include "annotation.hpp"
#include <gdiplus.h>
#include <limits>

namespace lb::capture {
namespace {
struct GdiPlus {
    ULONG_PTR token{};
    GdiPlus() { Gdiplus::GdiplusStartupInput input;Gdiplus::GdiplusStartup(&token,&input,nullptr); }
    ~GdiPlus() { if(token) Gdiplus::GdiplusShutdown(token); }
};
Gdiplus::Color color(COLORREF value,BYTE alpha=255) { return {alpha,GetRValue(value),GetGValue(value),GetBValue(value)}; }
Gdiplus::RectF number_badge(Annotation const& item){
    float height=float(std::max(1,item.style.font_size));float width=std::max(height,float(std::to_wstring(item.style.number).size())*height*.37f+8.f);auto p=item.points.front();return {float(p.x)-width/2,float(p.y)-height/2,width,height};
}
void rounded_path(Gdiplus::GraphicsPath &path,RECT const& bounds,int radius) {
    float x=float(bounds.left),y=float(bounds.top),w=float(bounds.right-bounds.left),h=float(bounds.bottom-bounds.top),r=float(std::min({radius,int(w/2),int(h/2)}))*2;
    if(r<1) { path.AddRectangle(Gdiplus::RectF{x,y,w,h});return; }
    path.AddArc(x,y,r,r,180,90);path.AddArc(x+w-r,y,r,r,270,90);path.AddArc(x+w-r,y+h-r,r,r,0,90);path.AddArc(x,y+h-r,r,r,90,90);path.CloseFigure();
}
void brush_path(Gdiplus::GraphicsPath& path,Annotation const& item){
    using namespace Gdiplus;if(item.points.empty())return;REAL width=REAL(std::clamp(item.style.width,1,160)),radius=width/2;path.SetFillMode(FillModeWinding);
    // Union identical capsules for live segments and full history replay. A
    // single widened polyline can self-intersect into holes or change joins.
    for(size_t n=0;n<item.points.size();++n){auto p=item.points[n];REAL x=REAL(p.x)+.5f,y=REAL(p.y)+.5f;path.AddEllipse(x-radius,y-radius,width,width);if(!n)continue;auto a=item.points[n-1];double dx=p.x-a.x,dy=p.y-a.y,length=std::hypot(dx,dy);if(!length)continue;REAL nx=REAL(-dy/length*radius),ny=REAL(dx/length*radius),ax=REAL(a.x)+.5f,ay=REAL(a.y)+.5f;PointF polygon[]={{ax-nx,ay-ny},{x-nx,y-ny},{x+nx,y+ny},{ax+nx,ay+ny}};path.AddPolygon(polygon,4);}
}
unique_gdi<HRGN> brush_region(HDC dc,Annotation const& item){Gdiplus::GraphicsPath path;brush_path(path,item);Gdiplus::Region region(&path);Gdiplus::Graphics graphics(dc);return unique_gdi<HRGN>(region.GetHRGN(&graphics));}
bool brush_contains(Annotation const& item,POINT p){
    double radius=std::max(1,item.style.width)*.5;if(item.points.empty())return false;
    if(item.points.size()==1)return std::hypot(double(p.x-item.points[0].x),double(p.y-item.points[0].y))<=radius;
    for(size_t n=1;n<item.points.size();++n){auto a=item.points[n-1],b=item.points[n];double x=b.x-a.x,y=b.y-a.y,length=x*x+y*y,t=length?std::clamp(((p.x-a.x)*x+(p.y-a.y)*y)/length,0.,1.):0.;if(std::hypot(p.x-a.x-t*x,p.y-a.y-t*y)<=radius)return true;}return false;
}
void erase_pixels(HDC destination,HDC original,Annotation const& item){
    auto region=brush_region(destination,item);if(!region)return;SaveDC(destination);ExtSelectClipRgn(destination,region.get(),RGN_AND);RECT bounds{};if(GetClipBox(destination,&bounds)!=NULLREGION)BitBlt(destination,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,original,bounds.left,bounds.top,SRCCOPY);RestoreDC(destination,-1);
}
bool erase_alpha(void* pixels,int width,int height,Annotation const& item){
    HDC dc=CreateCompatibleDC(nullptr);if(!dc)return false;auto region=brush_region(dc,item);DeleteDC(dc);if(!region)return false;DWORD bytes=GetRegionData(region.get(),0,nullptr);if(!bytes||bytes>64*1024*1024)return false;std::vector<BYTE> storage(bytes);auto data=reinterpret_cast<RGNDATA*>(storage.data());if(!GetRegionData(region.get(),bytes,data))return false;auto rects=reinterpret_cast<RECT const*>(data->Buffer);for(DWORD n=0;n<data->rdh.nCount;++n){auto r=rects[n];int left=std::clamp(int(r.left),0,width),right=std::clamp(int(r.right),0,width);for(int y=std::max(0L,r.top);y<std::min(LONG(height),r.bottom);++y)std::fill_n(static_cast<DWORD*>(pixels)+size_t(y)*width+left,std::max(0,right-left),DWORD{});}return true;
}
void mosaic(HDC dc,Annotation const& item) {
    RECT shape=item.bounds(),bounds=shape;BITMAP selected{};if(GetObjectW(GetCurrentObject(dc,OBJ_BITMAP),sizeof selected,&selected)){RECT image{0,0,selected.bmWidth,selected.bmHeight};if(!IntersectRect(&bounds,&shape,&image))return;}if(bounds.right<=bounds.left||bounds.bottom<=bounds.top) return;
    int block=std::clamp(item.style.block_size,6,64),w=bounds.right-bounds.left,h=bounds.bottom-bounds.top;
    HRGN region=nullptr;
    if(item.tool==Tool::mosaic) region=CreateRoundRectRgn(shape.left,shape.top,shape.right,shape.bottom,item.style.radius*2,item.style.radius*2);
    else region=brush_region(dc,item).release();
    unique_gdi<HRGN> clip(region);SaveDC(dc);ExtSelectClipRgn(dc,region,RGN_AND);scope_exit restore([&]{RestoreDC(dc,-1);});
    HDC temporary=CreateCompatibleDC(dc);if(!temporary) return;scope_exit close([&]{DeleteDC(temporary);});unique_gdi<HBITMAP> small(CreateCompatibleBitmap(dc,std::max(1,(w+block-1)/block),std::max(1,(h+block-1)/block)));if(!small) return;auto old=SelectObject(temporary,small.get());scope_exit select([&]{SelectObject(temporary,old);});
    SetStretchBltMode(temporary,COLORONCOLOR);StretchBlt(temporary,0,0,std::max(1,(w+block-1)/block),std::max(1,(h+block-1)/block),dc,bounds.left,bounds.top,w,h,SRCCOPY);SetStretchBltMode(dc,COLORONCOLOR);StretchBlt(dc,bounds.left,bounds.top,w,h,temporary,0,0,std::max(1,(w+block-1)/block),std::max(1,(h+block-1)/block),SRCCOPY);
}
void render_vector(Gdiplus::Graphics &graphics,Annotation const& item) {
    using namespace Gdiplus;if(item.points.empty()) return;
    if(item.tool==Tool::mosaic||item.tool==Tool::mosaic_brush||item.tool==Tool::select||item.tool==Tool::eraser||item.tool==Tool::sequence||item.tool==Tool::image_crop) return;
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);graphics.SetPixelOffsetMode(PixelOffsetModeHalf);
    Pen pen(color(item.style.color,item.tool==Tool::highlight?100:255),REAL(item.tool==Tool::highlight?item.style.width*3:item.style.width));pen.SetLineJoin(LineJoinRound);pen.SetStartCap(LineCapRound);pen.SetEndCap(LineCapRound);if(item.style.dashed) pen.SetDashStyle(DashStyleDash);
    SolidBrush brush(color(item.style.color));auto first=item.points.front(),last=item.points.back();RECT rect=item.bounds();
    if(item.tool==Tool::text) { Font font(item.style.font.c_str(),REAL(item.style.font_size),(item.style.bold?FontStyleBold:0)|(item.style.italic?FontStyleItalic:0)|(item.style.underline?FontStyleUnderline:0),UnitPixel);RectF bounds{REAL(first.x),REAL(first.y),REAL(std::max(1L,rect.right-first.x)),REAL(std::max(1L,rect.bottom-first.y))};StringFormat format(StringFormatFlagsNoWrap);graphics.DrawString(item.text.c_str(),-1,&font,bounds,&format,&brush); }
    else if(item.tool==Tool::number) {
        auto badge=number_badge(item);GraphicsPath path;path.AddArc(badge.X,badge.Y,badge.Height,badge.Height,90,180);path.AddArc(badge.GetRight()-badge.Height,badge.Y,badge.Height,badge.Height,270,180);path.CloseFigure();graphics.FillPath(&brush,&path);
        Font font(L"Segoe UI",REAL(std::max(1L,std::lround(badge.Height*.56f))),FontStyleBold,UnitPixel);StringFormat format;format.SetAlignment(StringAlignmentCenter);format.SetLineAlignment(StringAlignmentCenter);
        double luminance=GetRValue(item.style.color)*.299+GetGValue(item.style.color)*.587+GetBValue(item.style.color)*.114;SolidBrush ink(luminance>150?Color(255,21,32,40):Color(255,255,255,255));auto label=std::to_wstring(item.style.number);graphics.DrawString(label.c_str(),-1,&font,badge,&format,&ink);
    }else if(item.tool==Tool::rectangle||item.tool==Tool::ellipse) {
        GraphicsPath path;if(item.tool==Tool::ellipse) path.AddEllipse(RectF{REAL(rect.left),REAL(rect.top),REAL(rect.right-rect.left),REAL(rect.bottom-rect.top)});else rounded_path(path,rect,item.style.radius);if(item.style.filled){SolidBrush fill(color(item.style.color,80));graphics.FillPath(&fill,&path);}graphics.DrawPath(&pen,&path);
    }else if(item.tool==Tool::line||item.tool==Tool::arrow) {
        graphics.DrawLine(&pen,REAL(first.x),REAL(first.y),REAL(last.x),REAL(last.y));if(item.tool==Tool::arrow&&(first.x!=last.x||first.y!=last.y)) { double angle=std::atan2(double(last.y-first.y),double(last.x-first.x)),length=std::min(std::hypot(double(last.x-first.x),double(last.y-first.y)),double(item.style.width*4+8));Pen tip(color(item.style.color),REAL(item.style.width));tip.SetStartCap(LineCapRound);tip.SetEndCap(LineCapRound);for(double delta:{-.55,.55})graphics.DrawLine(&tip,REAL(last.x),REAL(last.y),REAL(last.x-length*std::cos(angle+delta)),REAL(last.y-length*std::sin(angle+delta))); }
    }else { if(item.points.size()==1) graphics.DrawEllipse(&pen,REAL(first.x),REAL(first.y),1.f,1.f);else { std::vector<PointF> points;points.reserve(item.points.size());for(auto const& point:item.points) points.emplace_back(REAL(point.x),REAL(point.y));graphics.DrawLines(&pen,points.data(),INT(points.size())); } }
    graphics.Flush(FlushIntentionSync);
}
void render_item(HDC dc,Annotation const& item) { if(item.tool==Tool::mosaic||item.tool==Tool::mosaic_brush) mosaic(dc,item);else { Gdiplus::Graphics graphics(dc);render_vector(graphics,item); } }
RECT text_bounds(Annotation const& item){
    using namespace Gdiplus;if(item.points.empty())return {};auto point=item.points.front();static GdiPlus runtime;if(!runtime.token)return {point.x,point.y,point.x+1,point.y+item.style.font_size+2};Bitmap canvas(1,1);Graphics graphics(&canvas);Font font(item.style.font.c_str(),REAL(item.style.font_size),(item.style.bold?FontStyleBold:0)|(item.style.italic?FontStyleItalic:0)|(item.style.underline?FontStyleUnderline:0),UnitPixel);StringFormat format(StringFormatFlagsNoWrap|StringFormatFlagsMeasureTrailingSpaces);RectF measured{};if(graphics.MeasureString(item.text.c_str(),int(item.text.size()),&font,RectF(0,0,16384,16384),&format,&measured)!=Ok)return {point.x,point.y,point.x+1,point.y+item.style.font_size+2};return {point.x,point.y,point.x+LONG(std::ceil(measured.Width))+4,point.y+LONG(std::ceil(measured.Height))+2};
}
bool annotation_hit(Annotation const& item,POINT point,double margin){
    using namespace Gdiplus;if(item.points.empty())return false;auto first=item.points.front(),last=item.points.back();auto rect=item.bounds();
    if(item.tool==Tool::text||item.tool==Tool::number){int pad=int(std::ceil(margin));InflateRect(&rect,pad,pad);return PtInRect(&rect,point)!=FALSE;}
    double radius=std::max(margin,item.style.width*(item.tool==Tool::highlight?1.5:.5)+margin*.5);
    if(item.tool==Tool::rectangle||item.tool==Tool::ellipse||item.tool==Tool::mosaic){GraphicsPath path;if(item.tool==Tool::ellipse)path.AddEllipse(RectF(REAL(rect.left),REAL(rect.top),REAL(rect.right-rect.left),REAL(rect.bottom-rect.top)));else rounded_path(path,rect,item.style.radius);if(item.tool==Tool::mosaic)return path.IsVisible(INT(point.x),INT(point.y));Pen pen(Color(255,255,255,255),REAL(radius*2));pen.SetLineJoin(LineJoinRound);return (item.style.filled&&path.IsVisible(INT(point.x),INT(point.y)))||path.IsOutlineVisible(INT(point.x),INT(point.y),&pen);}
    if(item.tool==Tool::mosaic_brush)return brush_contains(item,point);
    auto segment_hit=[&](double ax,double ay,double bx,double by){double dx=bx-ax,dy=by-ay,length=dx*dx+dy*dy,t=length?std::clamp(((point.x-ax)*dx+(point.y-ay)*dy)/length,0.,1.):0.;return std::hypot(point.x-ax-t*dx,point.y-ay-t*dy)<=radius;};
    if(item.tool==Tool::arrow&&item.points.size()>1){double angle=std::atan2(double(last.y-first.y),double(last.x-first.x)),length=std::min(std::hypot(double(last.x-first.x),double(last.y-first.y)),double(item.style.width*4+8));for(double delta:{-.55,.55})if(segment_hit(last.x,last.y,last.x-length*std::cos(angle+delta),last.y-length*std::sin(angle+delta)))return true;}
    if(item.points.size()==1)return segment_hit(first.x,first.y,first.x,first.y);for(size_t n=1;n<item.points.size();++n)if(segment_hit(item.points[n-1].x,item.points[n-1].y,item.points[n].x,item.points[n].y))return true;return false;
}
}
RECT Annotation::bounds() const {
    if(points.empty()) return {};RECT result{points.front().x,points.front().y,points.front().x+1,points.front().y+1};for(auto const& point:points) { result.left=std::min(result.left,point.x);result.top=std::min(result.top,point.y);result.right=std::max(result.right,point.x);result.bottom=std::max(result.bottom,point.y); }
    if(tool==Tool::text) return text_bounds(*this);
    if(tool==Tool::number){auto badge=number_badge(*this);result={LONG(std::floor(badge.X)),LONG(std::floor(badge.Y)),LONG(std::ceil(badge.GetRight())),LONG(std::ceil(badge.GetBottom()))};}
    if(tool==Tool::mosaic_brush||tool==Tool::eraser) InflateRect(&result,std::max(1,(style.width+1)/2),std::max(1,(style.width+1)/2));return result;
}
void Annotation::translate(int x,int y) { for(auto &point:points) { point.x+=x;point.y+=y; } }
unique_gdi<HBITMAP> crop(HBITMAP source,RECT bounds) {
    int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;if(!source||width<=0||height<=0||uint64_t(width)*height>64*1024*1024) return {};
    HDC screen=GetDC(nullptr);if(!screen) return {};scope_exit release([&]{ReleaseDC(nullptr,screen);});HDC src=CreateCompatibleDC(screen),dst=CreateCompatibleDC(screen);scope_exit close([&]{if(src) DeleteDC(src);if(dst) DeleteDC(dst);});if(!src||!dst) return {};
    unique_gdi<HBITMAP> bitmap(CreateCompatibleBitmap(screen,width,height));if(!bitmap) return {};auto a=SelectObject(src,source),b=SelectObject(dst,bitmap.get());BOOL ok=BitBlt(dst,0,0,width,height,src,bounds.left,bounds.top,SRCCOPY);SelectObject(src,a);SelectObject(dst,b);if(!ok) return {};return bitmap;
}
unique_gdi<HBITMAP> overlay(int width,int height,std::vector<Annotation> const& items) {
    if(width<1||height<1||uint64_t(width)*height>32*1024*1024) return {};static GdiPlus runtime;if(!runtime.token) return {};
    BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),width,-height,1,32,BI_RGB,0,0,0,0,0};void* pixels=nullptr;unique_gdi<HBITMAP> bitmap(CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0));if(!bitmap) return {};memset(pixels,0,size_t(width)*height*4);
    Gdiplus::Bitmap target(width,height,width*4,PixelFormat32bppPARGB,static_cast<BYTE*>(pixels));Gdiplus::Graphics graphics(&target);for(auto const& item:items){if(item.tool==Tool::eraser){graphics.Flush(Gdiplus::FlushIntentionSync);if(!erase_alpha(pixels,width,height,item))return {};}else render_vector(graphics,item);}graphics.Flush(Gdiplus::FlushIntentionSync);return bitmap;
}
Document::Document(HBITMAP source):original_(source) { BITMAP bitmap{};if(source&&GetObjectW(source,sizeof bitmap,&bitmap)) { width=bitmap.bmWidth;height=bitmap.bmHeight; } }
namespace {size_t snapshot_bytes(std::vector<Annotation> const& snapshot){size_t bytes=256+snapshot.capacity()*sizeof(Annotation);for(auto const& item:snapshot)bytes+=item.points.capacity()*sizeof(POINT)+(item.text.capacity()+item.style.font.capacity()+2)*sizeof(wchar_t);return bytes;}}
size_t Document::history_bytes() const{size_t total=0;for(auto const& entry:history_)total+=snapshot_bytes(entry);for(auto const& entry:future_)total+=snapshot_bytes(entry);return total;}
void Document::trim_history(){while(history_.size()>24)history_.pop_front();while(future_.size()>24)future_.pop_front();size_t total=history_bytes();while(total>8*1024*1024&&(!history_.empty()||!future_.empty())){auto& queue=!history_.empty()?history_:future_;total-=snapshot_bytes(queue.front());queue.pop_front();}}
void Document::checkpoint(){finish_edit();pending_before_=items;dirty_=true;}
void Document::finish_edit(){if(!pending_before_)return;if(*pending_before_!=items){history_.push_back(std::move(*pending_before_));future_.clear();}pending_before_.reset();trim_history();}
bool Document::undo() {finish_edit();if(history_.empty()) return false;future_.push_back(items);items=std::move(history_.back());history_.pop_back();trim_history();dirty_=true;return true; }
bool Document::redo() {finish_edit();if(future_.empty()) return false;history_.push_back(items);items=std::move(future_.back());future_.pop_back();trim_history();dirty_=true;return true; }
HBITMAP Document::render(int skip) {
    if(items.empty()) { dirty_=false;return original_.get(); }
    static GdiPlus runtime;if(!runtime.token) return original_.get();if(!dirty_&&composite_&&render_skip_==skip) return composite_.get();if(!composite_)composite_=crop(original_.get(),RECT{0,0,width,height});if(!composite_) return original_.get();
    HDC dc=CreateCompatibleDC(nullptr),source=CreateCompatibleDC(nullptr);if(!dc||!source){if(dc)DeleteDC(dc);if(source)DeleteDC(source);return original_.get();}auto old=SelectObject(dc,composite_.get()),base=SelectObject(source,original_.get());BitBlt(dc,0,0,width,height,source,0,0,SRCCOPY);for(size_t n=0;n<items.size();++n){if(int(n)==skip)continue;auto const& item=items[n];if(item.tool==Tool::eraser)erase_pixels(dc,source,item);else render_item(dc,item);}SelectObject(source,base);DeleteDC(source);SelectObject(dc,old);DeleteDC(dc);dirty_=false;render_skip_=skip;return composite_.get();
}
bool Document::begin_erase(POINT point,int diameter){
    if(items.empty()||items.size()>=256)return false;render();if(!composite_||dirty_)return false;checkpoint();Annotation stroke;stroke.tool=Tool::eraser;stroke.style.width=std::clamp(diameter,1,160);stroke.points.push_back(point);items.push_back(std::move(stroke));dirty_=false;
    HDC dc=CreateCompatibleDC(nullptr),source=CreateCompatibleDC(nullptr);if(!dc||!source){if(dc)DeleteDC(dc);if(source)DeleteDC(source);dirty_=true;return true;}auto a=SelectObject(dc,composite_.get()),b=SelectObject(source,original_.get());erase_pixels(dc,source,items.back());SelectObject(dc,a);SelectObject(source,b);DeleteDC(dc);DeleteDC(source);return true;
}
bool Document::extend_erase(POINT point){
    if(items.empty()||items.back().tool!=Tool::eraser||items.back().points.empty())return false;auto& stroke=items.back();auto before=stroke.points.back();if(before.x==point.x&&before.y==point.y)return false;if(stroke.points.size()>=4096)return false;stroke.points.push_back(point);
    if(dirty_||!composite_){dirty_=true;return true;}Annotation segment;segment.tool=Tool::eraser;segment.style.width=stroke.style.width;segment.points={before,point};HDC dc=CreateCompatibleDC(nullptr),source=CreateCompatibleDC(nullptr);if(!dc||!source){if(dc)DeleteDC(dc);if(source)DeleteDC(source);dirty_=true;return true;}auto a=SelectObject(dc,composite_.get()),b=SelectObject(source,original_.get());erase_pixels(dc,source,segment);SelectObject(dc,a);SelectObject(source,b);DeleteDC(dc);DeleteDC(source);return true;
}
int Document::hit(POINT point,double margin,bool include_mosaic,int skip) const {static GdiPlus runtime;if(!runtime.token)return -1;for(size_t n=items.size();n>0;--n){if(int(n-1)==skip)continue;auto const& item=items[n-1];if(item.tool==Tool::sequence||item.tool==Tool::image_crop)continue;if(item.tool==Tool::eraser){if(brush_contains(item,point))return -1;continue;}if(!include_mosaic&&(item.tool==Tool::mosaic||item.tool==Tool::mosaic_brush))continue;if(annotation_hit(item,point,std::max(0.,margin)))return int(n-1);}return -1;}
bool annotation_tests() {
    HDC screen=GetDC(nullptr);unique_gdi<HBITMAP> source(CreateCompatibleBitmap(screen,160,120));ReleaseDC(nullptr,screen);if(!source) return false;HDC dc=CreateCompatibleDC(nullptr);auto old=SelectObject(dc,source.get());RECT area{0,0,160,120};FillRect(dc,&area,static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));SelectObject(dc,old);DeleteDC(dc);
    Document doc(source.release());doc.checkpoint();Annotation rect;rect.tool=Tool::rectangle;rect.points={{10,10},{80,80}};rect.style.filled=true;rect.style.color=RGB(255,0,0);doc.items.push_back(rect);auto bitmap=doc.render();dc=CreateCompatibleDC(nullptr);old=SelectObject(dc,bitmap);COLORREF value=GetPixel(dc,30,30);SelectObject(dc,old);DeleteDC(dc);if(GetRValue(value)!=255||std::abs(int(GetGValue(value))-175)>1||std::abs(int(GetBValue(value))-175)>1||doc.hit({30,30})!=0) return false;
    doc.items[0].translate(20,0);doc.changed();if(doc.hit({15,15})!=-1||!doc.undo()||!doc.items.empty()) return false;
    Annotation text;text.tool=Tool::text;text.text=L"中文\ntext";text.points={{10,10}};if(text.bounds().bottom<=text.bounds().top+24) return false;
    // Redo restores the complete annotation state, and a new edit branches history.
    if(doc.can_undo()||!doc.can_redo()||!doc.redo()||doc.items.size()!=1||doc.items[0].points[0].x!=30) return false;
    if(!doc.can_undo()||doc.can_redo()||!doc.undo()) return false;
    doc.checkpoint();doc.items.push_back(text);doc.changed();if(doc.can_redo()||doc.redo()) return false;
    if(!doc.undo()||!doc.items.empty()||!doc.redo()||doc.items[0].text!=text.text) return false;
    Document bounded(nullptr);for(int n=0;n<30;++n) { bounded.checkpoint();rect.style.number=n;bounded.items={rect}; }
    int undos=0;while(bounded.undo()) ++undos;if(undos!=24||bounded.items[0].style.number!=5) return false;
    int redos=0;while(bounded.redo()) ++redos;if(redos!=24||bounded.items[0].style.number!=29) return false;
    if(!bounded.undo()) return false;bounded.checkpoint();bounded.items.clear();if(bounded.can_redo()||bounded.redo()) return false;

    // Pixel size is independent of brush width. Render a nonuniform image so this
    // checks the actual raster output rather than just the stored style fields.
    screen=GetDC(nullptr);unique_gdi<HBITMAP> pattern(CreateCompatibleBitmap(screen,96,64));ReleaseDC(nullptr,screen);if(!pattern) return false;
    dc=CreateCompatibleDC(nullptr);if(!dc) return false;old=SelectObject(dc,pattern.get());
    for(int y=0;y<64;++y) for(int x=0;x<96;++x) SetPixelV(dc,x,y,RGB((x*3)%256,(y*4)%256,((x+y)*7)%256));
    SelectObject(dc,old);DeleteDC(dc);
    auto pixels=[&](int width,int block) {
        auto bitmap=crop(pattern.get(),RECT{0,0,96,64});std::vector<COLORREF> values;if(!bitmap) return values;
        Document image(bitmap.release());Annotation mosaic_item;mosaic_item.tool=Tool::mosaic;mosaic_item.points={{0,0},{96,64}};mosaic_item.style.width=width;mosaic_item.style.block_size=block;image.items.push_back(mosaic_item);
        HDC rendered=CreateCompatibleDC(nullptr);if(!rendered) return values;auto previous=SelectObject(rendered,image.render());
        for(int y=3;y<64;y+=5) for(int x=3;x<96;x+=5) values.push_back(GetPixel(rendered,x,y));
        SelectObject(rendered,previous);DeleteDC(rendered);return values;
    };
    auto fine=pixels(3,8),wide=pixels(30,8),coarse=pixels(3,32);if(fine.empty()||fine!=wide||fine==coarse) return false;
    Annotation stroke;stroke.tool=Tool::mosaic_brush;stroke.points={{40,30}};stroke.style.width=24;stroke.style.block_size=64;
    auto brush_bounds=stroke.bounds();if(brush_bounds.left!=28||brush_bounds.top!=18||brush_bounds.right!=53||brush_bounds.bottom!=43) return false;
    stroke.style.block_size=6;auto small_blocks=stroke.bounds();if(!EqualRect(&brush_bounds,&small_blocks)) return false;
    // An isolated eraser must never paint over a recording's transparent layer.
    stroke.tool=Tool::eraser;auto transparent=overlay(80,60,{stroke});BITMAP info{};if(!transparent||!GetObjectW(transparent.get(),sizeof info,&info)||!info.bmBits) return false;
    auto data=static_cast<BYTE const*>(info.bmBits);for(int n=0;n<80*60;++n) if(data[n*4+3]) return false;
    return true;
}
bool eraser_tests(){
    constexpr int width=180,height=120;BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),width,-height,1,32,BI_RGB,0,0,0,0,0};void* raw{};unique_gdi<HBITMAP> base(CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&raw,nullptr,0));if(!base)return false;
    auto data=static_cast<DWORD*>(raw);for(int y=0;y<height;++y)for(int x=0;x<width;++x)data[y*width+x]=DWORD((x*5)%256)<<16|DWORD((y*7)%256)<<8|DWORD((x+y)*3%256);
    auto pixels=[](HBITMAP image){BITMAP b{};GetObjectW(image,sizeof b,&b);std::vector<COLORREF> result;HDC dc=CreateCompatibleDC(nullptr);auto old=SelectObject(dc,image);for(int y=0;y<b.bmHeight;++y)for(int x=0;x<b.bmWidth;++x)result.push_back(GetPixel(dc,x,y));SelectObject(dc,old);DeleteDC(dc);return result;};
    auto background=pixels(base.get());
    for(auto tool:{Tool::pen,Tool::arrow,Tool::text,Tool::rectangle,Tool::number,Tool::mosaic,Tool::mosaic_brush}){
        Document doc(crop(base.get(),{0,0,width,height}).release());Annotation item;item.tool=tool;item.style.width=30;item.style.font_size=60;item.style.filled=true;item.style.color=RGB(230,14,25);item.style.block_size=24;item.style.radius=18;item.text=L"MMMM";item.points=tool==Tool::text?std::vector<POINT>{{12,16}}:tool==Tool::number?std::vector<POINT>{{80,60}}:std::vector<POINT>{{15,20},{150,90}};doc.items={item};
        auto before=pixels(doc.render());auto cache=doc.render();if(before==background||!doc.begin_erase({60,0},18)||!doc.extend_erase({60,119})||doc.render()!=cache)return false;auto after=pixels(doc.render());if(doc.items.size()!=2||doc.items.back().tool!=Tool::eraser)return false;int removed=0,retained=0;
        for(int y=0;y<height;++y)for(int x=0;x<width;++x){auto index=size_t(y*width+x);if(x>=54&&x<=66){if(after[index]!=background[index])return false;if(before[index]!=background[index])++removed;}if(x<45||x>76){if(after[index]!=before[index])return false;if(after[index]!=background[index])++retained;}}
        if(!removed||!retained||!doc.undo()||pixels(doc.render())!=before||!doc.redo()||pixels(doc.render())!=after)return false;
        doc.checkpoint();Annotation later;later.tool=Tool::line;later.points={{25,60},{120,60}};later.style.width=5;later.style.color=RGB(11,240,19);doc.items.push_back(later);doc.changed();auto drawn=pixels(doc.render());if(drawn[60*width+60]!=later.style.color||doc.can_redo())return false;
        // Video export must clear only earlier annotations, and later drawing
        // is allowed over erased pixels. Mosaic is intentionally image-only.
        if(tool!=Tool::mosaic&&tool!=Tool::mosaic_brush){auto layer=overlay(width,height,{item,doc.items[1]});BITMAP b{};if(!layer||!GetObjectW(layer.get(),sizeof b,&b)||!b.bmBits)return false;auto alpha=static_cast<BYTE*>(b.bmBits);for(int y=0;y<height;++y)if(alpha[(y*width+60)*4+3])return false;auto over=overlay(width,height,doc.items);if(!over||!GetObjectW(over.get(),sizeof b,&b)||static_cast<BYTE*>(b.bmBits)[(60*width+60)*4+3]<250)return false;}
    }
    // A sparse fast gesture remains continuous; all intermediate updates use
    // one cached raster and vector history rather than full-screen snapshots.
    Document doc(crop(base.get(),{0,0,width,height}).release());Annotation fill;fill.tool=Tool::rectangle;fill.style.filled=true;fill.style.color=RGB(10,20,30);fill.points={{0,0},{width,height}};doc.items={fill};auto cache=doc.render();if(!doc.begin_erase({10,60},20)||!doc.extend_erase({170,60}))return false;auto erased=pixels(doc.render());for(int x=10;x<170;++x)if(erased[60*width+x]!=background[60*width+x])return false;for(int n=0;n<300;++n)if(!doc.extend_erase({20+n%140,30+n%50})||doc.render()!=cache)return false;auto incremental=pixels(doc.render());doc.changed();if(pixels(doc.render())!=incremental)return false;
    // Rounded region mosaic clips every sampled pixel to its real shape.
    doc.items={fill};doc.items[0].tool=Tool::mosaic;doc.items[0].points={{20,20},{160,100}};doc.items[0].style.radius=25;doc.items[0].style.block_size=20;doc.changed();auto mosaic=pixels(doc.render());if(mosaic[20*width+20]!=background[20*width+20]||mosaic[60*width+90]==background[60*width+90])return false;
    doc.items[0].tool=Tool::mosaic_brush;doc.items[0].points={{20,60},{160,60}};doc.items[0].style.width=20;doc.changed();mosaic=pixels(doc.render());if(mosaic[60*width+90]==background[60*width+90]||mosaic[30*width+90]!=background[30*width+90])return false;
    Document empty(crop(base.get(),{0,0,width,height}).release());if(empty.begin_erase({20,20},20)||pixels(base.get())!=background)return false;
    Annotation erase;erase.tool=Tool::eraser;erase.style.width=17;erase.points={{22,22},{100,82},{140,15}};doc.items={fill,erase};doc.changed();auto composited=pixels(doc.render());auto layer=overlay(width,height,doc.items);BITMAP b{};if(!layer||!GetObjectW(layer.get(),sizeof b,&b)||!b.bmBits)return false;auto alpha=static_cast<BYTE*>(b.bmBits);for(int y=4;y<height-4;++y)for(int x=4;x<width-4;++x){auto i=y*width+x;if((composited[i]==background[i])!=(alpha[i*4+3]==0))return false;}
    doc.finish_edit();auto history=doc.history();Document reopened(crop(base.get(),{0,0,width,height}).release());reopened.items=doc.items;reopened.restore_history(history);if(reopened.can_undo()!=doc.can_undo()||reopened.can_redo()!=doc.can_redo())return false;
    Annotation text;text.tool=Tool::text;text.points={{10,10}};text.text=L"Underline";auto plain=overlay(width,height,{text});text.style.underline=true;auto underlined=overlay(width,height,{text});if(!plain||!underlined||pixels(plain.get())==pixels(underlined.get()))return false;Document editing(crop(base.get(),{0,0,width,height}).release());editing.items={text};auto visible=pixels(editing.render());return visible!=background&&pixels(editing.render(0))==background&&pixels(editing.render())==visible;
}
bool hit_tests(){
    Document doc(nullptr);Annotation item;item.tool=Tool::rectangle;item.points={{20,20},{120,100}};item.style.width=4;doc.items={item};if(doc.hit({60,60})!=-1||doc.hit({21,60})!=0)return false;doc.items[0].style.filled=true;if(doc.hit({60,60})!=0)return false;doc.items[0].style.filled=false;doc.items[0].style.radius=25;if(doc.hit({20,20},0)!=-1||doc.hit({45,20},0)!=0)return false;
    doc.items[0].tool=Tool::ellipse;if(doc.hit({60,60})!=-1||doc.hit({20,60})!=0)return false;doc.items[0].tool=Tool::line;doc.items[0].points={{10,10},{150,100}};if(doc.hit({80,55})!=0||doc.hit({10,100})!=-1)return false;
    doc.items[0].tool=Tool::arrow;doc.items[0].points={{10,60},{150,60}};if(doc.hit({130,47},1)!=0||doc.hit({130,30},1)!=-1)return false;
    doc.items[0].tool=Tool::mosaic;doc.items[0].points={{20,20},{120,100}};doc.items[0].style.radius=24;if(doc.hit({20,20},0)!=-1||doc.hit({60,60},0)!=0||doc.hit({60,60},0,false)!=-1)return false;
    doc.items[0].tool=Tool::text;doc.items[0].points={{10,10}};doc.items[0].style.font_size=20;doc.items[0].text=L"i";auto narrow=doc.items[0].bounds();doc.items[0].text=L"WWWW";auto wide=doc.items[0].bounds();if(wide.right-narrow.right<20||doc.hit({wide.right+30,20})!=-1||doc.hit({wide.left+5,wide.top+5})!=0)return false;
    doc.checkpoint();doc.items[0].text=L"changed";doc.finish_edit();if(!doc.undo()||!doc.can_redo())return false;doc.checkpoint();doc.finish_edit();if(!doc.can_redo()||!doc.redo()||doc.items[0].text!=L"changed")return false;
    for(int n=0;n<40;++n){doc.checkpoint();doc.items[0].style.number=n;doc.finish_edit();}auto before=doc.history();doc.checkpoint();doc.finish_edit();auto after=doc.history();if(before.past!=after.past||before.future!=after.future)return false;
    Annotation sequence;sequence.tool=Tool::sequence;sequence.style.number=9876;doc.items.push_back(sequence);Annotation crop_item;crop_item.tool=Tool::image_crop;crop_item.points={{0,0},{180,120}};doc.items.push_back(crop_item);return doc.hit({160,110})==-1;
}
bool history_budget_tests(){
    constexpr size_t limit=8*1024*1024;Document doc(nullptr);Annotation line;line.tool=Tool::pen;line.points.resize(4096,{20,30});doc.items.assign(20,line);
    for(int n=0;n<30;++n){doc.checkpoint();doc.items[0].style.number=n+2;doc.finish_edit();if(doc.history_bytes()>limit||doc.items[0].style.number!=n+2)return false;}if(doc.history().past.size()>=24||!doc.can_undo())return false;
    auto complete=doc.items;int undone=0;while(doc.undo()){++undone;if(doc.history_bytes()>limit)return false;}if(!undone||!doc.can_redo())return false;while(doc.redo())if(doc.history_bytes()>limit)return false;if(doc.items!=complete)return false;
    if(!doc.undo())return false;auto before=doc.history();doc.checkpoint();doc.finish_edit();if(doc.history().past!=before.past||doc.history().future!=before.future)return false;
    Document::History imported;for(int n=0;n<30;++n)imported.past.push_back(complete);doc.restore_history(std::move(imported));if(doc.history_bytes()>limit||doc.items==complete)return false;auto transferred=doc.take_history();if(doc.history_bytes()!=0||transferred.past.empty())return false;Document reopened(nullptr);reopened.items=complete;reopened.restore_history(std::move(transferred));return reopened.history_bytes()<=limit&&reopened.items==complete;
}
}
