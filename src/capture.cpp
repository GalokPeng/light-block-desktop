#include "annotation.hpp"
#include "recording.hpp"
#include "scroll_capture.hpp"
#include "frontend.hpp"
#include "pin_glow.hpp"
#include <commdlg.h>
#include <set>
#include <array>
#include <filesystem>
#include <gdiplus.h>

namespace {
using namespace lb;
using namespace lb::capture;
uint64_t next_surface_identity{};
auto image_save_dialog=&GetSaveFileNameW;
auto image_overwrite_dialog=&MessageBoxW;
auto image_save_config=&save_config;
auto open_image_clipboard=&OpenClipboard;
struct Surface {
    PinGlow glow;
    uint64_t identity{++next_surface_identity};
    HWND window{},toolbar{},edit{},tooltips{};
    unique_gdi<HFONT> edit_font;unique_gdi<HBRUSH> edit_brush;
    std::unique_ptr<Document> document;
    Tool tool{Tool::select};Style style;
    int active{-1},edit_index{-1};POINT previous{},move_anchor{};bool move_started{};
    int wheel_target{-3},wheel_delta{};
    bool drawing{},moving{},editing{},border{true},qt_toolbar{},brush_visible{},tracking{};double scale{1.};POINT brush_point{};
    double opacity{1.};bool wheel_sizes{true},long_image{};double scroll_y{};
    unique_gdi<HBITMAP> paint_buffer;SIZE paint_size{};
    bool cropping{},crop_selected{},resizable{};int crop_drag{};POINT crop_anchor{};RECT crop_selection{},crop_before{};
    int resize_drag{};POINT resize_anchor{};RECT resize_before{};
    std::function<void(std::vector<Annotation>)> complete;
    std::function<void(Document::History)> retain_history;
    std::array<Style,7> group_styles;
    Surface(){for(auto& value:group_styles){value.color=RGB(255,104,104);value.width=4;value.font=L"Microsoft YaHei UI";}group_styles[3].font_size=20;group_styles[4].font_size=22;group_styles[5].width=24;group_styles[6].width=32;style=group_styles[0];}
};
struct Capture : Surface {
    unique_gdi<HBITMAP> snapshot,buffer;
    std::wstring source_layout;
    int width{},height{};RECT selection{},before{};POINT anchor{};int drag{};unsigned transition_serial{};bool selected{},smart{true},record_after_selection{},transition_queued{};
};
std::wstring screen_layout(){
    std::vector<std::wstring> parts;auto visit=[](HMONITOR monitor,HDC,LPRECT,LPARAM data)->BOOL{MONITORINFOEXW info{};info.cbSize=sizeof info;if(GetMonitorInfoW(monitor,&info)){wchar_t part[256];StringCchPrintfW(part,256,L"%s:%ld,%ld,%ld,%ld",info.szDevice,info.rcMonitor.left,info.rcMonitor.top,info.rcMonitor.right,info.rcMonitor.bottom);reinterpret_cast<std::vector<std::wstring>*>(data)->emplace_back(part);}return TRUE;};EnumDisplayMonitors(nullptr,nullptr,visit,LPARAM(&parts));std::sort(parts.begin(),parts.end());std::wstring result;for(auto const& part:parts){result+=part;result+=L';';}return result;
}
struct Pin : Surface {};
std::unique_ptr<Capture> session;
std::array<std::unique_ptr<Pin>,12> pins;
Surface* current_surface(HWND window){if(session&&session->window==window)return session.get();for(auto const& pin:pins)if(pin&&pin->window==window)return pin.get();return nullptr;}
LRESULT CALLBACK capture_proc(HWND,UINT,WPARAM,LPARAM);
LRESULT CALLBACK pin_proc(HWND,UINT,WPARAM,LPARAM);
LRESULT CALLBACK toolbar_proc(HWND,UINT,WPARAM,LPARAM);
void update_toolbar(Surface &surface);
void commit_text(Surface &surface);
void change_style(Surface& surface);
void layout_text(Surface& surface);
void wheel(Surface& surface,POINT screen,WPARAM wp);
constexpr int text_sizes[]={8,10,12,14,16,18,20,22,24,28,32,36,40,48,56,64,80,96,120,160};
constexpr int stroke_sizes[]={1,2,3,4,6,8,10,12,16,20,24,32,36,48,64,80};
constexpr int number_sizes[]={12,16,20,22,26,32,40,48,60,72,90,120,160};
template<size_t N>int stepped_size(int value,int steps,int const(&sizes)[N]){if(!steps)return value;auto index=steps>0?std::upper_bound(std::begin(sizes),std::end(sizes),value)-std::begin(sizes)+steps-1:std::lower_bound(std::begin(sizes),std::end(sizes),value)-std::begin(sizes)+steps;return sizes[std::clamp<ptrdiff_t>(index,0,N-1)];}
int wheel_steps(Surface& surface,int target,int delta){if(surface.wheel_target!=target){surface.wheel_target=target;surface.wheel_delta=0;}surface.wheel_delta+=delta;int steps=surface.wheel_delta/WHEEL_DELTA;surface.wheel_delta-=steps*WHEEL_DELTA;return steps;}
int next_number(Document const& document){for(auto it=document.items.rbegin();it!=document.items.rend();++it){if(it->tool==Tool::number)return std::min(9999,it->style.number+1);if(it->tool==Tool::sequence)return it->style.number;}return 1;}
void sync_number(Surface& surface){if(!surface.document)return;int number=next_number(*surface.document);surface.group_styles[4].number=number;if(surface.tool==Tool::number)surface.style.number=number;}
int tool_group(Tool tool){switch(tool){case Tool::rectangle:case Tool::ellipse:return 0;case Tool::pen:case Tool::highlight:return 1;case Tool::line:case Tool::arrow:return 2;case Tool::text:return 3;case Tool::number:return 4;case Tool::mosaic:case Tool::mosaic_brush:return 5;case Tool::eraser:return 6;default:return -1;}}
void select_tool(Surface& surface,Tool next){if(surface.complete&&(next==Tool::mosaic||next==Tool::mosaic_brush))return;commit_text(surface);auto before=tool_group(surface.tool),after=tool_group(next);if(before>=0)surface.group_styles[size_t(before)]=surface.style;if(after>=0&&after!=before)surface.style=surface.group_styles[size_t(after)];surface.tool=next;surface.active=-1;surface.wheel_target=-3;surface.wheel_delta=0;sync_number(surface);surface.brush_visible=false;InvalidateRect(surface.window,nullptr,FALSE);update_toolbar(surface);if(IsWindowVisible(surface.window))SetFocus(surface.window);}
void finish(Capture &surface,bool pin,bool save);
void select_done(Capture &surface);
void capture_command(uintptr_t owner,int command,int value,wchar_t const* text);
RECT source_view(Surface const& surface);
void pin_view_changed(Surface& surface,RECT before);
void pin_crop_begin(Surface& surface);
void pin_crop_cancel(Surface& surface);
bool pin_crop_apply(Surface& surface);
HBITMAP render_result(Surface& surface,unique_gdi<HBITMAP>& storage);
bool copy_surface(Surface& surface);
int px(int value) { return MulDiv(value,int(g_app.dpi),96); }
RECT normalized(POINT a,POINT b) { return {std::min(a.x,b.x),std::min(a.y,b.y),std::max(a.x,b.x),std::max(a.y,b.y)}; }
bool valid(RECT const& rect) { return rect.right-rect.left>2&&rect.bottom-rect.top>2; }
void close_toolbar(Surface &surface) {
    surface.brush_visible=false;InvalidateRect(surface.window,nullptr,FALSE);
    commit_text(surface);if(surface.toolbar) { auto window=std::exchange(surface.toolbar,nullptr);if(surface.qt_toolbar) { if(auto ui=frontend::capture_ui())ui->destroy(uintptr_t(window)); }else DestroyWindow(window); }surface.qt_toolbar=false;surface.editing=false;
}
void hide_toolbar(Surface& surface) {
    surface.brush_visible=false;InvalidateRect(surface.window,nullptr,FALSE);
    commit_text(surface);if(!surface.toolbar)return;
    if(surface.qt_toolbar){if(auto ui=frontend::capture_ui()){UiCaptureState hidden{};hidden.mode=-1;ui->update(uintptr_t(surface.toolbar),&hidden);}}
    else ShowWindow(surface.toolbar,SW_HIDE);
}
UiCaptureState capture_state(Surface const& surface) {
    UiCaptureState state{};auto const& style=surface.style;state.english=g_app.english;state.mode=surface.complete?3:2;
    RECT target{};GetWindowRect(surface.window,&target);if(session&&surface.window==session->window) {state.mode=session->selected?1:0;state.smart=session->smart;if(session->selected){target=session->selection;OffsetRect(&target,GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN));}}
    state.left=target.left;state.top=target.top;state.right=target.right;state.bottom=target.bottom;state.tool=int(surface.tool);state.color=int(style.color);state.width=style.width;state.radius=style.radius;state.filled=style.filled;state.dashed=style.dashed;state.font_size=style.font_size;state.bold=style.bold;state.italic=style.italic;state.underline=style.underline;state.cropping=surface.cropping;state.number=style.number;state.block_size=style.block_size;StringCchCopyW(state.font,64,style.font.c_str());if(surface.document){state.can_undo=surface.document->can_undo();state.can_redo=surface.document->can_redo();}return state;
}
void place_toolbar(Surface &surface) {
    if(!surface.toolbar) return;
    if(surface.qt_toolbar){if(auto ui=frontend::capture_ui()){auto state=capture_state(surface);ui->update(uintptr_t(surface.toolbar),&state);}return;}
    RECT target{};GetWindowRect(surface.window,&target);
    if(session&&surface.window==session->window) { target=session->selection;OffsetRect(&target,GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN)); }
    MONITORINFO info{sizeof info};GetMonitorInfoW(MonitorFromRect(&target,MONITOR_DEFAULTTONEAREST),&info);
    int width=std::min(px(760),int(info.rcWork.right-info.rcWork.left)-16),height=px(104);
    int x=std::clamp(int(target.left),int(info.rcWork.left)+8,int(info.rcWork.right)-width-8),y=target.bottom+8;
    if(y+height>info.rcWork.bottom) y=std::max(int(info.rcWork.top)+8,int(target.top)-height-8);
    SetWindowPos(surface.toolbar,HWND_TOPMOST,x,y,width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW);
}
void show_toolbar(Surface &surface) {
    surface.editing=true;
    if(!IsWindowVisible(surface.window))return;
    if(auto ui=frontend::capture_ui()) { auto state=capture_state(surface);if(!surface.toolbar){surface.toolbar=HWND(ui->create(uintptr_t(surface.window),capture_command,&state));surface.qt_toolbar=surface.toolbar!=nullptr;}if(surface.qt_toolbar){ui->update(uintptr_t(surface.toolbar),&state);return;} }
    if(!surface.toolbar) {
        WNDCLASSW cls{};cls.hInstance=g_app.instance;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        cls.lpfnWndProc=toolbar_proc;cls.lpszClassName=L"LightBlockDesktopNative.CaptureToolbar";RegisterClassW(&cls);
        surface.toolbar=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,cls.lpszClassName,tr(L"截屏编辑",L"Capture tools"),
            WS_POPUP|WS_CLIPCHILDREN,0,0,px(760),px(104),surface.window,nullptr,g_app.instance,&surface);
    }
    place_toolbar(surface);update_toolbar(surface);
}
bool copy_bitmap(HWND owner,HBITMAP bitmap) {
    auto copied=static_cast<HBITMAP>(CopyImage(bitmap,IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION));if(!copied) return false;
    if(!open_image_clipboard(owner)) { DeleteObject(copied);return false; }
    EmptyClipboard();bool ok=SetClipboardData(CF_BITMAP,copied)!=nullptr;CloseClipboard();if(!ok) DeleteObject(copied);return ok;
}
bool save_image(Surface &surface) {
    if(!surface.document) return false;commit_text(surface);
    HWND owner=surface.window;auto identity=surface.identity;Surface* original=&surface;
    wchar_t file[LB_PATH]{};SYSTEMTIME now{};GetLocalTime(&now);StringCchPrintfW(file,LB_PATH,L"LightBlock_%04u%02u%02u_%02u%02u%02u.png",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond);OPENFILENAMEW dialog{sizeof dialog};
    dialog.hwndOwner=surface.window;dialog.lpstrFilter=L"PNG\0*.png\0\0";dialog.lpstrFile=file;dialog.nMaxFile=LB_PATH;
    dialog.lpstrDefExt=L"png";dialog.lpstrInitialDir=g_app.settings.capture_directory[0]?g_app.settings.capture_directory:g_app.data;dialog.Flags=OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    bool chosen=image_save_dialog(&dialog)!=FALSE;auto live=current_surface(owner);
    // A native dialog pumps the app's timers/tray commands. Closing the capture
    // or app while it is open invalidates the original Surface reference.
    if(!chosen||live!=original||!live||live->identity!=identity||!IsWindow(owner)||!live->document)return false;
    auto destination=std::filesystem::path(file);if(_wcsicmp(destination.extension().c_str(),L".png"))destination.replace_extension(L".png");
    // Confirm the actual normalized path. The common dialog otherwise checks
    // e.g. capture.jpg while the PNG writer would overwrite capture.png.
    if(GetFileAttributesW(destination.c_str())!=INVALID_FILE_ATTRIBUTES){auto question=std::wstring(tr(L"此 PNG 图片已存在，是否覆盖？\n\n",L"This PNG image already exists. Replace it?\n\n"))+destination.wstring();int answer=image_overwrite_dialog(owner,question.c_str(),tr(L"保存图片",L"Save image"),MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2);live=current_surface(owner);if(answer!=IDYES||live!=original||!live||live->identity!=identity||!IsWindow(owner)||!live->document)return false;}
    unique_gdi<HBITMAP> saved_result;if(!save_png(render_result(*live,saved_result),destination.c_str())) { error_box(owner,tr(L"图片保存失败",L"Could not save image"),E_FAIL);return false; }auto previous=std::wstring(g_app.settings.capture_directory);StringCchCopyW(g_app.settings.capture_directory,LB_PATH,destination.parent_path().c_str());if(!image_save_config())StringCchCopyW(g_app.settings.capture_directory,LB_PATH,previous.c_str());return true;
}
Surface *surface_for(HWND window) { return reinterpret_cast<Surface*>(GetWindowLongPtrW(window,GWLP_USERDATA)); }
LRESULT CALLBACK text_proc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data) {
    auto &surface=*reinterpret_cast<Surface*>(data);
    bool overlay=session&&surface.window==session->window;
    if(overlay&&(message==WM_RBUTTONDOWN||message==WM_CONTEXTMENU))return 0;
    if(overlay&&message==WM_RBUTTONUP){PostMessageW(surface.window,WM_RBUTTONUP,0,0);return 0;}
    if(message==WM_MOUSEWHEEL){if((GET_KEYSTATE_WPARAM(wp)&MK_CONTROL)&&!overlay)SendMessageW(surface.window,message,wp,lp);else wheel(surface,{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)},wp);return 0;}
    if(message==WM_KEYDOWN&&(wp==VK_ESCAPE||(wp==VK_RETURN&&GetKeyState(VK_CONTROL)<0))) { commit_text(surface);SetFocus(surface.window);return 0; }
    if(message==WM_KILLFOCUS) PostMessageW(surface.window,WM_APP+71,0,0);
    if(message==WM_NCDESTROY) RemoveWindowSubclass(window,text_proc,id);
    auto result=DefSubclassProc(window,message,wp,lp);
    if(message==WM_SETTEXT||message==WM_CHAR||message==WM_PASTE||message==WM_CUT||message==WM_CLEAR||message==WM_UNDO)layout_text(surface);
    return result;
}
RECT source_view(Surface const& surface){
    if(!surface.document)return {};RECT full{0,0,surface.document->width,surface.document->height};
    if(session&&session->window==surface.window)return full;
    for(auto it=surface.document->items.rbegin();it!=surface.document->items.rend();++it)if(it->tool==Tool::image_crop&&it->points.size()==2){RECT result{},proposed{it->points[0].x,it->points[0].y,it->points[1].x,it->points[1].y};if(IntersectRect(&result,&full,&proposed)&&result.right-result.left>=2&&result.bottom-result.top>=2)return result;}
    return full;
}
int pin_margin(Surface const& surface){auto dpi=GetDpiForWindow(surface.window);return surface.border?std::max(1,MulDiv(5,dpi?dpi:96,96)):0;}
RECT image_viewport(Surface const& surface){if(session&&surface.window==session->window)return session->selection;RECT area{};GetClientRect(surface.window,&area);int margin=pin_margin(surface);InflateRect(&area,-margin,-margin);return area;}
RECT display_view(Surface const& surface){
    auto view=source_view(surface);if(!surface.long_image)return view;
    auto area=image_viewport(surface);double scale=double(area.right-area.left)/std::max(1L,view.right-view.left);
    int visible=std::clamp(int(std::lround((area.bottom-area.top)/std::max(.001,scale))),1,int(view.bottom-view.top));
    view.top+=std::clamp(int(std::lround(surface.scroll_y)),0,int(view.bottom-view.top)-visible);view.bottom=view.top+visible;return view;
}
POINT to_document(Surface const& surface,POINT point,bool clamp=true) {
    if(!surface.document)return point;auto area=image_viewport(surface),source=display_view(surface);point.x=source.left+LONG(std::floor(double(point.x-area.left)*(source.right-source.left)/std::max(1L,area.right-area.left)));point.y=source.top+LONG(std::floor(double(point.y-area.top)*(source.bottom-source.top)/std::max(1L,area.bottom-area.top)));if(clamp){point.x=std::clamp(point.x,source.left,std::max(source.left,source.right-1));point.y=std::clamp(point.y,source.top,std::max(source.top,source.bottom-1));}return point;
}
POINT from_document(Surface const& surface,POINT point){auto area=image_viewport(surface),source=display_view(surface);if(!surface.document)return point;return {area.left+MulDiv(point.x-source.left,area.right-area.left,std::max(1L,source.right-source.left)),area.top+MulDiv(point.y-source.top,area.bottom-area.top,std::max(1L,source.bottom-source.top))};}
double image_scale_y(Surface const& surface){auto area=image_viewport(surface),source=display_view(surface);return surface.document?double(area.bottom-area.top)/std::max(1L,source.bottom-source.top):1.;}
int hit_annotation(Surface const& surface,POINT point,bool mosaics=true,int skip=-1){if(!surface.document)return -1;auto area=image_viewport(surface),source=display_view(surface);double scale=std::min(double(area.right-area.left)/std::max(1L,source.right-source.left),double(area.bottom-area.top)/std::max(1L,source.bottom-source.top));UINT dpi=GetDpiForWindow(surface.window);double margin=6.*(dpi?dpi:96)/96/std::max(.001,scale);return surface.document->hit(point,margin,mosaics,skip);}
RECT brush_bounds(Surface const& surface){auto area=image_viewport(surface),source=display_view(surface);auto point=from_document(surface,to_document(surface,surface.brush_point));int x=surface.document?int(std::ceil(double(surface.style.width)*(area.right-area.left)/std::max(1L,source.right-source.left)/2))+3:3,y=surface.document?int(std::ceil(double(surface.style.width)*(area.bottom-area.top)/std::max(1L,source.bottom-source.top)/2))+3:3;return {point.x-x,point.y-y,point.x+x+1,point.y+y+1};}
void brush_cursor(Surface& surface,POINT point,bool visible){if(surface.brush_visible){auto old=brush_bounds(surface);InvalidateRect(surface.window,&old,FALSE);}auto viewport=image_viewport(surface);surface.brush_point=point;surface.brush_visible=visible&&surface.editing&&surface.tool==Tool::eraser&&surface.document&&PtInRect(&viewport,point);if(surface.brush_visible){auto area=brush_bounds(surface);InvalidateRect(surface.window,&area,FALSE);}if(visible&&!surface.tracking){TRACKMOUSEEVENT track{sizeof track,TME_LEAVE,surface.window,0};if(TrackMouseEvent(&track))surface.tracking=true;}}
void paint_brush_cursor(Surface const& surface,HDC dc){if(!surface.brush_visible||surface.tool!=Tool::eraser||!surface.document)return;struct Runtime{ULONG_PTR token{};Runtime(){Gdiplus::GdiplusStartupInput input;Gdiplus::GdiplusStartup(&token,&input,nullptr);}~Runtime(){if(token)Gdiplus::GdiplusShutdown(token);}};static Runtime runtime;if(!runtime.token)return;auto area=image_viewport(surface),source=display_view(surface);auto point=to_document(surface,surface.brush_point);Gdiplus::Graphics graphics(dc);graphics.SetClip(Gdiplus::Rect(area.left,area.top,area.right-area.left,area.bottom-area.top));graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);float sx=float(area.right-area.left)/std::max(1L,source.right-source.left),sy=float(area.bottom-area.top)/std::max(1L,source.bottom-source.top);float width=surface.style.width*sx,height=surface.style.width*sy,cx=area.left+(point.x-source.left+.5f)*sx,cy=area.top+(point.y-source.top+.5f)*sy;Gdiplus::Pen dark(Gdiplus::Color(230,16,20,28),3.f),light(Gdiplus::Color(245,235,245,255),1.f);graphics.DrawEllipse(&dark,cx-width/2,cy-height/2,width,height);graphics.DrawEllipse(&light,cx-width/2,cy-height/2,width,height);graphics.DrawLine(&light,cx-2,cy,cx+2,cy);graphics.DrawLine(&light,cx,cy-2,cx,cy+2);}
void layout_text(Surface& surface) {
    if(!surface.edit||!surface.document||surface.edit_index<0||size_t(surface.edit_index)>=surface.document->items.size())return;
    auto const& item=surface.document->items[size_t(surface.edit_index)];if(item.points.empty())return;
    RECT area=image_viewport(surface);double scale=image_scale_y(surface);auto anchor=from_document(surface,item.points.front());int x=std::clamp(anchor.x,area.left,std::max(area.left,area.right-1)),y=std::clamp(anchor.y,area.top,std::max(area.top,area.bottom-1));
    int available=std::max(1,int(area.right)-x),vertical=std::max(1,int(area.bottom)-y);
    int length=GetWindowTextLengthW(surface.edit);std::wstring text(size_t(length)+1,L'\0');GetWindowTextW(surface.edit,text.data(),length+1);text.resize(size_t(length));
    if(text.empty()||text.back()==L'\n')text+=L' ';HDC dc=GetDC(surface.edit);if(!dc)return;auto old=SelectObject(dc,surface.edit_font.get());TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);
    int natural=0;size_t begin=0;while(begin<text.size()){auto end=text.find(L'\n',begin);if(end==std::wstring::npos)end=text.size();SIZE extent{};GetTextExtentPoint32W(dc,text.data()+begin,int(end-begin),&extent);natural=std::max(natural,int(extent.cx));begin=end+1;}
    int width=std::min(available,std::max(int(108*scale),natural+12));RECT measured{0,0,std::max(1,width-12),0};DrawTextW(dc,text.c_str(),int(text.size()),&measured,DT_CALCRECT|DT_EDITCONTROL|DT_NOPREFIX);
    int height=std::min(vertical,std::max(int(24*scale),std::max(int(metrics.tmHeight),int(measured.bottom))+10));SelectObject(dc,old);ReleaseDC(surface.edit,dc);
    RECT before{};GetWindowRect(surface.edit,&before);MapWindowPoints(nullptr,surface.window,reinterpret_cast<POINT*>(&before),2);
    if(before.left!=x||before.top!=y||before.right-before.left!=width||before.bottom-before.top!=height)SetWindowPos(surface.edit,nullptr,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
}
void start_text(Surface &surface,POINT point,int index=-1) {
    if(!surface.document) return;commit_text(surface);surface.document->checkpoint();surface.edit_index=index;
    Annotation item;
    if(index>=0) item=surface.document->items[size_t(index)];
    else { point.x=std::clamp(point.x,0L,std::max(0L,LONG(surface.document->width)-1));point.y=std::clamp(point.y,0L,std::max(0L,LONG(surface.document->height)-1));item.tool=Tool::text;item.style=surface.style;item.points={point};surface.document->items.push_back(item);surface.edit_index=int(surface.document->items.size()-1); }
    surface.active=surface.edit_index;surface.style=item.style;point=from_document(surface,item.points.front());int x=point.x,y=point.y;double scale=image_scale_y(surface);
    surface.edit_font.reset(CreateFontW(-int(item.style.font_size*scale),0,0,0,item.style.bold?FW_BOLD:FW_NORMAL,
        item.style.italic,item.style.underline,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,item.style.font.c_str()));
    surface.edit_brush.reset(CreateSolidBrush(RGB(27,33,42)));
    surface.edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",item.text.c_str(),WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_AUTOVSCROLL|ES_AUTOHSCROLL|ES_WANTRETURN,
        x,y,108,24,surface.window,reinterpret_cast<HMENU>(8),g_app.instance,nullptr);
    SendMessageW(surface.edit,WM_SETFONT,WPARAM(surface.edit_font.get()),TRUE);SendMessageW(surface.edit,EM_SETLIMITTEXT,8192,0);
    SetWindowSubclass(surface.edit,text_proc,1,DWORD_PTR(&surface));if(IsWindowVisible(surface.window)){SetForegroundWindow(surface.window);SetFocus(surface.edit);}
    int end=GetWindowTextLengthW(surface.edit);SendMessageW(surface.edit,EM_SETSEL,WPARAM(end),LPARAM(end));layout_text(surface);InvalidateRect(surface.window,nullptr,FALSE);update_toolbar(surface);
}
void commit_text(Surface &surface) {
    if(!surface.edit) return;HWND edit=std::exchange(surface.edit,nullptr);int length=GetWindowTextLengthW(edit);
    std::wstring value(size_t(length)+1,L'\0');GetWindowTextW(edit,value.data(),length+1);value.resize(size_t(length));
    if(surface.document&&surface.edit_index>=0&&size_t(surface.edit_index)<surface.document->items.size()) {
        auto &item=surface.document->items[size_t(surface.edit_index)];item.text=std::move(value);item.style=surface.style;
        if(std::all_of(item.text.begin(),item.text.end(),[](wchar_t value){return iswspace(value)!=0;})) { surface.document->items.erase(surface.document->items.begin()+surface.edit_index);surface.active=-1; }
        surface.document->changed();
    }
    surface.document->finish_edit();surface.edit_index=-1;DestroyWindow(edit);surface.edit_font.reset();surface.edit_brush.reset();InvalidateRect(surface.window,nullptr,FALSE);
}
void change_style(Surface &surface) {
    if(surface.tool==Tool::eraser){surface.group_styles[6]=surface.style;InvalidateRect(surface.window,nullptr,FALSE);}
    if(!surface.edit&&surface.tool!=Tool::eraser&&surface.document&&surface.active>=0&&size_t(surface.active)<surface.document->items.size()) {
        surface.document->checkpoint();auto& item=surface.document->items[size_t(surface.active)];int number=item.style.number;item.style=surface.style;if(item.tool==Tool::number)item.style.number=number;surface.document->changed();InvalidateRect(surface.window,nullptr,FALSE);
    }
    if(surface.edit) {
        double scale=image_scale_y(surface);
        unique_gdi<HFONT> font(CreateFontW(-int(surface.style.font_size*scale),0,0,0,surface.style.bold?FW_BOLD:FW_NORMAL,surface.style.italic,surface.style.underline,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,surface.style.font.c_str()));
        if(font){SendMessageW(surface.edit,WM_SETFONT,WPARAM(font.get()),TRUE);surface.edit_font=std::move(font);}layout_text(surface);InvalidateRect(surface.edit,nullptr,FALSE);
    }if(surface.document&&!surface.edit&&!surface.drawing&&!surface.moving)surface.document->finish_edit();update_toolbar(surface);
}
bool toolbar_has_focus(Surface const& surface,HWND focus) { if(!surface.toolbar)return false;if(surface.qt_toolbar){if(auto ui=frontend::capture_ui())return ui->owns_focus(uintptr_t(surface.toolbar),uintptr_t(focus))!=0;}return GetAncestor(focus,GA_ROOT)==surface.toolbar; }
void capture_command(uintptr_t owner,int command,int value,wchar_t const* text) {
    Surface* current=nullptr;if(session&&session->window==HWND(owner))current=session.get();else for(auto const& pin:pins)if(pin&&pin->window==HWND(owner)){current=pin.get();break;}
    if(!current||!IsWindow(current->window))return;auto& surface=*current;bool is_capture=session&&current==session.get();
    if(!is_capture&&!surface.complete){if(command==CaptureCrop){pin_crop_begin(surface);return;}if(command==CaptureCropApply){pin_crop_apply(surface);return;}if(command==CaptureCropCancel){pin_crop_cancel(surface);return;}if(surface.cropping&&command!=CaptureClose)return;}
    if(command==CaptureSelectTool) {if(value<0||value>int(Tool::eraser))return;select_tool(surface,Tool(value));return;}
    if(command==CaptureNumber){commit_text(surface);value=std::clamp(value,1,9999);if(surface.document&&next_number(*surface.document)!=value&&surface.document->items.size()<256){surface.document->checkpoint();Annotation marker;marker.tool=Tool::sequence;marker.style.number=value;surface.document->items.push_back(std::move(marker));surface.document->changed();surface.document->finish_edit();}sync_number(surface);update_toolbar(surface);return;}
    if(command==CaptureClose){DestroyWindow(surface.window);return;}
    if(command==CaptureSmart&&is_capture&&!session->selected){session->smart=value!=0;session->selection={};update_toolbar(surface);InvalidateRect(surface.window,nullptr,FALSE);return;}
    if(command==CaptureReset&&is_capture){ReleaseCapture();commit_text(surface);session->document.reset();session->selected=false;session->drag=0;session->selection={};session->before={};session->anchor={};session->active=-1;session->drawing=false;session->moving=false;session->record_after_selection=false;session->transition_queued=false;++session->transition_serial;session->tool=Tool::select;show_toolbar(surface);if(IsWindowVisible(surface.window))SetFocus(surface.window);InvalidateRect(surface.window,nullptr,FALSE);return;}
    if(command==CaptureFull&&is_capture){POINT point{};GetCursorPos(&point);MONITORINFO monitor{sizeof monitor};GetMonitorInfoW(MonitorFromPoint(point,MONITOR_DEFAULTTONEAREST),&monitor);close_toolbar(surface);session->selection=monitor.rcMonitor;OffsetRect(&session->selection,-GetSystemMetrics(SM_XVIRTUALSCREEN),-GetSystemMetrics(SM_YVIRTUALSCREEN));select_done(*session);return;}
    if(!surface.document)return;
    if(command==CaptureUndo||command==CaptureRedo){commit_text(surface);auto view_before=source_view(surface);if(command==CaptureUndo)surface.document->undo();else surface.document->redo();surface.active=-1;surface.wheel_target=-3;surface.wheel_delta=0;sync_number(surface);if(!is_capture)pin_view_changed(surface,view_before);InvalidateRect(surface.window,nullptr,FALSE);update_toolbar(surface);return;}
    if(command==CapturePin){if(surface.complete)DestroyWindow(surface.window);else if(is_capture)finish(*session,true,false);else close_toolbar(surface);return;}
    if(command==CaptureSave){if(is_capture)finish(*session,false,true);else save_image(surface);return;}
    if(command==CaptureCopy){commit_text(surface);if(copy_surface(surface)&&is_capture)DestroyWindow(surface.window);return;}
    if((command==CaptureRecord||command==CaptureLongshot)&&is_capture){if(!session->transition_queued){session->transition_queued=true;PostMessageW(surface.window,WM_APP+80,WPARAM(command),LPARAM(++session->transition_serial));}return;}
    switch(command){case CaptureColor:surface.style.color=COLORREF(value)&0xffffff;break;case CaptureWidth:surface.style.width=std::clamp(value,1,surface.tool==Tool::eraser?160:80);break;case CaptureRadius:surface.style.radius=std::clamp(value,0,80);break;case CaptureFill:surface.style.filled=value!=0;break;case CaptureDash:surface.style.dashed=value!=0;break;case CaptureFont:if(text&&wcslen(text)<64)surface.style.font=text;break;case CaptureFontSize:surface.style.font_size=std::clamp(value,8,160);break;case CaptureBold:surface.style.bold=value!=0;break;case CaptureItalic:surface.style.italic=value!=0;break;case CaptureUnderline:surface.style.underline=value!=0;break;case CaptureNumber:surface.style.number=std::clamp(value,1,9999);break;case CaptureBlockSize:surface.style.block_size=std::clamp(value,6,64);break;default:return;}
    change_style(surface);
}
int CALLBACK font_names(LOGFONTW const* font,TEXTMETRICW const*,DWORD,LPARAM parameter) {
    auto names=reinterpret_cast<std::set<std::wstring>*>(parameter);if(font->lfFaceName[0]!=L'@'&&names->size()<256) names->insert(font->lfFaceName);return 1;
}

LRESULT CALLBACK tool_button_proc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR) {
    if(message==WM_MOUSEMOVE&&!GetPropW(window,L"LightBlock.Hover")) { SetPropW(window,L"LightBlock.Hover",HANDLE(1));TRACKMOUSEEVENT track{sizeof track,TME_LEAVE,window,0};TrackMouseEvent(&track);InvalidateRect(window,nullptr,FALSE); }
    if(message==WM_MOUSELEAVE) { RemovePropW(window,L"LightBlock.Hover");InvalidateRect(window,nullptr,FALSE); }
    if(message==WM_SETCURSOR) { SetCursor(LoadCursorW(nullptr,IDC_HAND));return TRUE; }
    if(message==WM_NCDESTROY) { RemovePropW(window,L"LightBlock.Hover");RemoveWindowSubclass(window,tool_button_proc,id); }
    return DefSubclassProc(window,message,wp,lp);
}
void tool_tip(Surface& surface,HWND control,wchar_t const* text) {
    if(!surface.tooltips) return;TOOLINFOW info{sizeof info};info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;info.hwnd=surface.toolbar;info.uId=UINT_PTR(control);info.lpszText=const_cast<wchar_t*>(text);SendMessageW(surface.tooltips,TTM_ADDTOOLW,0,LPARAM(&info));
}
LRESULT CALLBACK tool_combo_proc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR) {
    if(message==WM_PAINT) {
        PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);RECT r{};GetClientRect(window,&r);rounded(dc,r,px(8),RGB(35,40,49),GetFocus()==window?RGB(107,164,145):RGB(52,59,70));
        wchar_t value[260]{};int current=int(SendMessageW(window,CB_GETCURSEL,0,0));if(current>=0&&SendMessageW(window,CB_GETLBTEXTLEN,current,0)<260) SendMessageW(window,CB_GETLBTEXT,current,LPARAM(value));
        auto font=SelectObject(dc,g_app.small);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(232,237,242));RECT text=r;text.left+=px(10);text.right-=px(25);DrawTextW(dc,value,-1,&text,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);SelectObject(dc,font);
        unique_gdi<HPEN> pen(CreatePen(PS_SOLID,std::max(1,px(1)),RGB(153,169,180)));auto old=SelectObject(dc,pen.get());int x=r.right-px(15),y=(r.bottom+r.top)/2;MoveToEx(dc,x-px(4),y-px(2),nullptr);LineTo(dc,x,y+px(2));LineTo(dc,x+px(4),y-px(2));SelectObject(dc,old);EndPaint(window,&paint);return 0;
    }
    if(message==WM_ERASEBKGND) return 1;
    if(message==WM_NCDESTROY) RemoveWindowSubclass(window,tool_combo_proc,id);
    return DefSubclassProc(window,message,wp,lp);
}
void vector_tool(HDC dc,RECT const& rect,int id,COLORREF foreground,bool complete) {
    struct Startup { ULONG_PTR token{};Startup(){Gdiplus::GdiplusStartupInput in;Gdiplus::GdiplusStartup(&token,&in,nullptr);}~Startup(){if(token) Gdiplus::GdiplusShutdown(token);} };static Startup startup;
    using namespace Gdiplus;Graphics g(dc);g.SetSmoothingMode(SmoothingModeAntiAlias);g.TranslateTransform(REAL((rect.left+rect.right-px(24))/2),REAL((rect.top+rect.bottom-px(24))/2));g.ScaleTransform(REAL(g_app.dpi)/96,REAL(g_app.dpi)/96);
    Color color(255,GetRValue(foreground),GetGValue(foreground),GetBValue(foreground));Pen pen(color,1.7f);pen.SetLineJoin(LineJoinRound);pen.SetStartCap(LineCapRound);pen.SetEndCap(LineCapRound);SolidBrush brush(color);
    auto line=[&](REAL x,REAL y,REAL a,REAL b){g.DrawLine(&pen,x,y,a,b);};
    switch(id) {
    case 10:{PointF points[]={{5,3},{5,20},{10,15},{14,22},{17,20},{13,14},{20,14},{5,3}};g.DrawLines(&pen,points,8);break;}
    case 11:g.DrawRectangle(&pen,4,4,16,16);break;
    case 12:g.DrawEllipse(&pen,3.5f,3.5f,17.f,17.f);break;
    case 13:line(4,20,20,4);break;
    case 14:line(4,19,20,4);line(11,4,20,4);line(20,4,20,13);break;
    case 15:{PointF points[]={{3,17},{7,10},{12,16},{17,6},{21,9}};g.DrawLines(&pen,points,5);break;}
    case 16:{PointF points[]={{5,14},{13,4},{20,10},{12,20},{5,20},{5,14}};g.DrawLines(&pen,points,6);line(5,14,12,20);line(3,22,14,22);break;}
    case 17:line(4,4,20,4);line(12,4,12,21);line(9,21,15,21);break;
    case 18:g.DrawEllipse(&pen,2,2,20,20);line(10,8,12,6);line(12,6,12,18);line(9,18,15,18);break;
    case 19:case 20:for(int y=0;y<3;++y)for(int x=0;x<3;++x){Rect r{4+x*6,4+y*6,5,5};if((x+y)%2==0)g.FillRectangle(&brush,r);else g.DrawRectangle(&pen,r);}if(id==20){Pen slash(Color(255,166,228,202),2);g.DrawLine(&slash,2,22,22,2);}break;
    case 21:g.DrawArc(&pen,5,7,15,14,200,245);line(4,6,4,13);line(4,13,11,13);break;
    case 22:if(complete){line(4,12,10,18);line(10,18,21,5);}else{line(8,4,18,4);line(10,4,10,10);line(16,4,16,10);line(10,10,7,15);line(16,10,19,15);line(7,15,19,15);line(13,15,13,22);}break;
    case 23:line(12,3,12,16);line(7,11,12,16);line(12,16,17,11);line(4,17,4,21);line(4,21,20,21);line(20,21,20,17);break;
    case 24:g.DrawRectangle(&pen,8,3,13,14);g.DrawRectangle(&pen,3,8,13,14);break;
    case 25:line(5,5,19,19);line(19,5,5,19);break;
    case 26:g.DrawRectangle(&pen,3,6,12,12);line(15,10,21,6);line(21,6,21,18);line(21,18,15,14);break;
    case 27:g.DrawRectangle(&pen,5,2,14,20);line(12,5,12,18);line(9,8,12,5);line(12,5,15,8);line(9,15,12,18);line(12,18,15,15);break;
    }
}

HWND combo(HWND window,int id,int x,int width,std::initializer_list<wchar_t const*> options,int selected) {
    HWND control=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL,
        px(x),px(62),px(width),px(250),window,reinterpret_cast<HMENU>(INT_PTR(id)),g_app.instance,nullptr);
    for(auto text:options) SendMessageW(control,CB_ADDSTRING,0,LPARAM(text));
    SendMessageW(control,WM_SETFONT,WPARAM(g_app.small),TRUE);SendMessageW(control,CB_SETITEMHEIGHT,WPARAM(-1),px(26));SendMessageW(control,CB_SETITEMHEIGHT,0,px(26));SendMessageW(control,CB_SETCURSEL,selected,0);SetWindowSubclass(control,tool_combo_proc,1,0);return control;
}
void update_toolbar(Surface &surface) {
    if(surface.qt_toolbar){place_toolbar(surface);return;}
    if(!surface.toolbar) return;SendMessageW(surface.toolbar,WM_SETREDRAW,FALSE,0);for(int id=100;id<=109;++id) if(HWND child=GetDlgItem(surface.toolbar,id)) DestroyWindow(child);
    bool text=surface.tool==Tool::text||surface.tool==Tool::number;auto const& style=surface.style;
    if(text) {
        HWND fonts=combo(surface.toolbar,100,12,190,{},0);static std::set<std::wstring> names;
        if(names.empty()) { HDC dc=GetDC(surface.toolbar);LOGFONTW log{};log.lfCharSet=DEFAULT_CHARSET;EnumFontFamiliesExW(dc,&log,reinterpret_cast<FONTENUMPROCW>(font_names),LPARAM(&names),0);ReleaseDC(surface.toolbar,dc); }
        int current=0,n=0;for(auto const& name:names) { SendMessageW(fonts,CB_ADDSTRING,0,LPARAM(name.c_str()));if(name==style.font) current=n;++n; }SendMessageW(fonts,CB_SETCURSEL,current,0);
        HWND sizes=combo(surface.toolbar,101,210,70,{L"12",L"16",L"20",L"24",L"32",L"40",L"48",L"64",L"72",L"96"},3);
        wchar_t value[30];StringCchPrintfW(value,30,L"%d",style.font_size);LRESULT found=SendMessageW(sizes,CB_FINDSTRINGEXACT,WPARAM(-1),LPARAM(value));if(found==CB_ERR) found=SendMessageW(sizes,CB_ADDSTRING,0,LPARAM(value));SendMessageW(sizes,CB_SETCURSEL,found,0);
        HWND bold=CreateWindowExW(0,L"BUTTON",L"B",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW|WS_TABSTOP,px(290),px(60),px(36),px(31),surface.toolbar,reinterpret_cast<HMENU>(102),g_app.instance,nullptr);SendMessageW(bold,BM_SETCHECK,style.bold?BST_CHECKED:BST_UNCHECKED,0);
        HWND italic=CreateWindowExW(0,L"BUTTON",L"I",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW|WS_TABSTOP,px(330),px(60),px(36),px(31),surface.toolbar,reinterpret_cast<HMENU>(103),g_app.instance,nullptr);SendMessageW(italic,BM_SETCHECK,style.italic?BST_CHECKED:BST_UNCHECKED,0);
        if(surface.tool==Tool::number) combo(surface.toolbar,104,374,80,{L"1",L"2",L"3",L"4",L"5",L"6",L"7",L"8",L"9",L"10"},std::clamp(style.number-1,0,9));
    } else {
        combo(surface.toolbar,100,12,110,{tr(L"轮廓",L"Outline"),tr(L"填充",L"Filled")},style.filled?1:0);
        combo(surface.toolbar,101,130,105,{tr(L"实线",L"Solid"),tr(L"虚线",L"Dashed")},style.dashed?1:0);
        HWND widths=combo(surface.toolbar,102,243,80,{L"1 px",L"2 px",L"3 px",L"5 px",L"8 px",L"12 px",L"20 px"},2);
        wchar_t value[24];StringCchPrintfW(value,24,L"%d px",style.width);LRESULT found=SendMessageW(widths,CB_FINDSTRINGEXACT,WPARAM(-1),LPARAM(value));if(found==CB_ERR) found=SendMessageW(widths,CB_ADDSTRING,0,LPARAM(value));SendMessageW(widths,CB_SETCURSEL,found,0);
        HWND radii=combo(surface.toolbar,103,331,110,{tr(L"圆角 0",L"Radius 0"),tr(L"圆角 8",L"Radius 8"),tr(L"圆角 16",L"Radius 16"),tr(L"圆角 24",L"Radius 24"),tr(L"圆角 48",L"Radius 48")},0);
        int radii_values[]={0,8,16,24,48};for(int k=0;k<5;++k) if(radii_values[k]==style.radius) SendMessageW(radii,CB_SETCURSEL,k,0);
    }
    SendMessageW(surface.toolbar,WM_SETREDRAW,TRUE,0);RedrawWindow(surface.toolbar,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
}
LRESULT CALLBACK toolbar_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto surface=surface_for(window);
    if(message==WM_NCCREATE) { surface=static_cast<Surface*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,LPARAM(surface)); }
    if(!surface) return DefWindowProcW(window,message,wp,lp);
    switch(message) {
    case WM_CREATE: {
        surface->toolbar=window;surface->tooltips=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,window,nullptr,g_app.instance,nullptr);
        SendMessageW(surface->tooltips,TTM_SETMAXTIPWIDTH,0,px(240));SendMessageW(surface->tooltips,TTM_SETDELAYTIME,TTDT_INITIAL,450);
        wchar_t const* zh[]={L"选择与移动",L"矩形",L"圆形",L"直线",L"箭头",L"画笔",L"高亮笔",L"文字：点击输入，双击编辑",L"序号标记",L"框选马赛克",L"涂鸦马赛克",L"撤销",L"贴到屏幕",L"保存图片",L"复制",L"关闭",L"录屏",L"滚动长截图"};
        wchar_t const* en[]={L"Select and move",L"Rectangle",L"Ellipse",L"Line",L"Arrow",L"Pen",L"Highlighter",L"Text: click to type, double-click to edit",L"Number marker",L"Mosaic region",L"Mosaic brush",L"Undo",L"Pin to screen",L"Save image",L"Copy",L"Close",L"Record screen",L"Scrolling capture"};
        for(int n=0;n<18;++n) { auto name=tr(zh[n],en[n]);HWND button=CreateWindowExW(0,L"BUTTON",name,WS_CHILD|WS_VISIBLE|BS_OWNERDRAW|WS_TABSTOP,px(10+n*41),px(8),px(36),px(38),window,reinterpret_cast<HMENU>(INT_PTR(n+10)),g_app.instance,nullptr);SetWindowSubclass(button,tool_button_proc,1,0);tool_tip(*surface,button,name); }
        for(int n=0;n<6;++n) { HWND button=CreateWindowExW(0,L"BUTTON",n==0?tr(L"自定义颜色",L"Custom color"):tr(L"预设颜色",L"Preset color"),WS_CHILD|WS_VISIBLE|BS_OWNERDRAW|WS_TABSTOP,px(460+n*46),px(59),px(35),px(32),window,reinterpret_cast<HMENU>(INT_PTR(50+n)),g_app.instance,nullptr);SetWindowSubclass(button,tool_button_proc,1,0);tool_tip(*surface,button,n==0?tr(L"调色板",L"Color palette"):tr(L"应用此颜色",L"Use this color")); }
        BOOL dark=TRUE;DwmSetWindowAttribute(window,20,&dark,sizeof dark);DWORD rounded=2;DwmSetWindowAttribute(window,33,&rounded,sizeof rounded);
        if(surface->complete) { for(int id:{19,20,24,26,27}) EnableWindow(GetDlgItem(window,id),FALSE);SetWindowTextW(GetDlgItem(window,22),L"✓"); }
        return 0;
    }
    case WM_SIZE: { RECT r{};GetClientRect(window,&r);SetWindowRgn(window,CreateRoundRectRgn(0,0,r.right+1,r.bottom+1,px(22),px(22)),TRUE);return 0; }
    case WM_ERASEBKGND: { RECT rect{};GetClientRect(window,&rect);rounded(HDC(wp),rect,px(11),RGB(24,27,33),RGB(55,63,74));RECT line{px(12),px(51),rect.right-px(12),px(52)};SetDCBrushColor(HDC(wp),RGB(47,53,63));FillRect(HDC(wp),&line,HBRUSH(GetStockObject(DC_BRUSH)));return 1; }
    case WM_MEASUREITEM: { auto measure=reinterpret_cast<MEASUREITEMSTRUCT*>(lp);if(measure->CtlType==ODT_COMBOBOX) { measure->itemHeight=px(26);return TRUE; }break; }
    case WM_CTLCOLORLISTBOX:SetTextColor(HDC(wp),RGB(232,237,242));SetBkColor(HDC(wp),RGB(35,40,49));SetDCBrushColor(HDC(wp),RGB(35,40,49));return LRESULT(GetStockObject(DC_BRUSH));
    case WM_DRAWITEM: {
        auto draw=reinterpret_cast<DRAWITEMSTRUCT*>(lp);auto r=draw->rcItem;int id=int(draw->CtlID);SetBkMode(draw->hDC,TRANSPARENT);
        if(draw->CtlType==ODT_COMBOBOX) {
            SetDCBrushColor(draw->hDC,draw->itemState&ODS_SELECTED?RGB(54,83,76):RGB(35,40,49));FillRect(draw->hDC,&r,HBRUSH(GetStockObject(DC_BRUSH)));wchar_t text[260]{};if(draw->itemID!=UINT(-1)&&SendMessageW(draw->hwndItem,CB_GETLBTEXTLEN,draw->itemID,0)<260) SendMessageW(draw->hwndItem,CB_GETLBTEXT,draw->itemID,LPARAM(text));auto old=SelectObject(draw->hDC,g_app.small);SetTextColor(draw->hDC,RGB(234,240,244));r.left+=px(9);DrawTextW(draw->hDC,text,-1,&r,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);SelectObject(draw->hDC,old);return TRUE;
        }
        bool disabled=(draw->itemState&ODS_DISABLED)!=0,hot=GetPropW(draw->hwndItem,L"LightBlock.Hover")!=nullptr,selected=(id>=10&&id<=20&&int(surface->tool)==id-10)||(id==102&&(surface->tool==Tool::text||surface->tool==Tool::number)&&surface->style.bold)||(id==103&&(surface->tool==Tool::text||surface->tool==Tool::number)&&surface->style.italic);
        COLORREF fill=selected?RGB(48,75,67):(draw->itemState&ODS_SELECTED)?RGB(57,67,78):hot?RGB(43,50,61):RGB(24,27,33);rounded(draw->hDC,r,px(8),fill,selected?RGB(100,158,137):fill);
        if(id>=50&&id<56) { COLORREF colors[]={surface->style.color,RGB(247,91,86),RGB(250,201,88),RGB(102,205,171),RGB(86,154,250),RGB(250,250,250)};InflateRect(&r,-px(3),-px(3));rounded(draw->hDC,r,px(7),colors[id-50],colors[id-50]);if(id==50) { SetTextColor(draw->hDC,RGB(20,28,34));auto old=SelectObject(draw->hDC,g_app.heading);DrawTextW(draw->hDC,L"+",1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);SelectObject(draw->hDC,old); } }
        else if(id>=10&&id<=27) vector_tool(draw->hDC,r,id,disabled?RGB(73,81,91):selected?RGB(166,228,202):RGB(230,237,244),bool(surface->complete));
        else { wchar_t text[8]{};GetWindowTextW(draw->hwndItem,text,8);auto old=SelectObject(draw->hDC,g_app.heading);SetTextColor(draw->hDC,selected?RGB(166,228,202):RGB(230,237,244));DrawTextW(draw->hDC,text,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);SelectObject(draw->hDC,old); }
        if(draw->itemState&ODS_FOCUS) { InflateRect(&r,-2,-2);DrawFocusRect(draw->hDC,&r); }return TRUE;
    }
    case WM_COMMAND: {
        int id=LOWORD(wp);
        if(id>=10&&id<=20) { commit_text(*surface);surface->tool=Tool(id-10);surface->active=-1;update_toolbar(*surface);SetFocus(surface->window); }
        else if(id==21&&surface->document) { commit_text(*surface);surface->document->undo();surface->active=-1;InvalidateRect(surface->window,nullptr,FALSE); }
        else if(id==22) { if(surface->complete) DestroyWindow(surface->window);else if(session&&surface->window==session->window) finish(*session,true,false);else close_toolbar(*surface); }
        else if(id==23) { if(session&&surface->window==session->window) finish(*session,false,true);else save_image(*surface); }
        else if(id==24&&surface->document) { commit_text(*surface);copy_bitmap(surface->window,surface->document->render());if(session&&surface->window==session->window) DestroyWindow(surface->window); }
        else if(id==25) DestroyWindow(surface->window);
        else if(id==26&&session&&surface->window==session->window) { RECT area=session->selection;OffsetRect(&area,GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN));DestroyWindow(surface->window);lb::recording::start(area); }
        else if(id==27&&session&&surface->window==session->window) { RECT area=session->selection;OffsetRect(&area,GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN));DestroyWindow(surface->window);lb::scroll::start(area); }
        else if(id>=50&&id<56) {
            if(id==50) { HWND owner=surface->window;auto identity=surface->identity;static COLORREF custom[16];CHOOSECOLORW choose{sizeof choose};choose.hwndOwner=window;choose.rgbResult=surface->style.color;choose.lpCustColors=custom;choose.Flags=CC_FULLOPEN|CC_RGBINIT;bool accepted=ChooseColorW(&choose)!=FALSE;auto live=current_surface(owner);if(live!=surface||!live||live->identity!=identity)return 0;if(accepted)surface->style.color=choose.rgbResult; }
            else { COLORREF colors[]={RGB(247,91,86),RGB(250,201,88),RGB(102,205,171),RGB(86,154,250),RGB(250,250,250)};surface->style.color=colors[id-51]; }change_style(*surface);
        } else if(id>=100&&id<=104&&(HIWORD(wp)==CBN_SELCHANGE||HIWORD(wp)==BN_CLICKED)) {
            int choice=int(SendMessageW(HWND(lp),CB_GETCURSEL,0,0));bool text=surface->tool==Tool::text||surface->tool==Tool::number;
            if(text) {
                if(id==100) { wchar_t font[LF_FACESIZE];SendMessageW(HWND(lp),CB_GETLBTEXT,choice,LPARAM(font));surface->style.font=font; }
                else if(id==101) { wchar_t size[30];SendMessageW(HWND(lp),CB_GETLBTEXT,choice,LPARAM(size));surface->style.font_size=_wtoi(size); }
                else if(id==102) surface->style.bold=!surface->style.bold;
                else if(id==103) surface->style.italic=!surface->style.italic;
                else surface->style.number=choice+1;
            } else {
                if(id==100) surface->style.filled=choice==1;else if(id==101) surface->style.dashed=choice==1;
                else if(id==102) { wchar_t size[30];SendMessageW(HWND(lp),CB_GETLBTEXT,choice,LPARAM(size));surface->style.width=_wtoi(size); }
                else if(id==103) { int radii[]={0,8,16,24,48};surface->style.radius=radii[std::clamp(choice,0,4)]; }
            }
            change_style(*surface);
        }
        return 0;
    }
    case WM_CLOSE:close_toolbar(*surface);return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
void mouse_down(Surface &surface,POINT point,bool twice=false) {
    surface.move_anchor=point;surface.move_started=false;
    auto viewport=image_viewport(surface);if(!PtInRect(&viewport,point))return;
    if(!surface.document) return;if(surface.edit){commit_text(surface);return;}point=to_document(surface,point);surface.previous=point;int hit=hit_annotation(surface,point);
    if(surface.tool==Tool::eraser) {if(surface.document->begin_erase(point,surface.style.width)){surface.active=int(surface.document->items.size()-1);surface.drawing=true;SetCapture(surface.window);InvalidateRect(surface.window,nullptr,FALSE);update_toolbar(surface);}return;}
    if(twice&&hit>=0&&surface.document->items[size_t(hit)].tool==Tool::text) { surface.tool=Tool::text;start_text(surface,point,hit);return; }
    if(surface.tool==Tool::select) { surface.active=hit;if(hit>=0) { surface.document->checkpoint();surface.moving=true;surface.style=surface.document->items[size_t(hit)].style; } }
    else if(surface.tool==Tool::text) {if(hit>=0&&surface.document->items[size_t(hit)].tool==Tool::text){surface.active=hit;surface.document->checkpoint();surface.moving=true;surface.style=surface.document->items[size_t(hit)].style;}else{start_text(surface,point);return;}}
    else {
        if(surface.document->items.size()>=256) return;surface.document->checkpoint();Annotation item;item.tool=surface.tool;item.style=surface.style;item.points={point,point};
        surface.document->items.push_back(std::move(item));surface.active=int(surface.document->items.size()-1);surface.drawing=true;
        if(surface.tool==Tool::number) {sync_number(surface);surface.drawing=false; }
    }
    surface.document->changed();SetCapture(surface.window);InvalidateRect(surface.window,nullptr,FALSE);update_toolbar(surface);
}
POINT constrained_shape(POINT anchor,POINT point,bool constrain){if(!constrain)return point;int size=std::min(std::abs(point.x-anchor.x),std::abs(point.y-anchor.y));return {anchor.x+(point.x<anchor.x?-size:size),anchor.y+(point.y<anchor.y?-size:size)};}
void mouse_move(Surface &surface,POINT point) {
    brush_cursor(surface,point,true);
    if(!surface.document||surface.active<0||size_t(surface.active)>=surface.document->items.size()||(!surface.drawing&&!surface.moving)) return;
    if(surface.moving&&!surface.move_started){auto dpi=GetDpiForWindow(surface.window);int threshold=std::max(1,MulDiv(10,dpi?dpi:96,96));if(std::abs(point.x-surface.move_anchor.x)+std::abs(point.y-surface.move_anchor.y)<threshold)return;surface.move_started=true;}
    point=to_document(surface,point,false);auto &item=surface.document->items[size_t(surface.active)];
    if(surface.drawing&&item.tool==Tool::eraser){if(surface.document->extend_erase(point))InvalidateRect(surface.window,nullptr,FALSE);surface.previous=point;return;}
    if(surface.moving){int dx=point.x-surface.previous.x,dy=point.y-surface.previous.y;if(item.tool==Tool::text&&!item.points.empty()){auto anchor=item.points.front();auto view=source_view(surface);int inset=std::max(1,int(std::ceil(8./std::max(.001,image_scale_y(surface)))));dx=std::clamp(anchor.x+dx,view.left,std::max(view.left,view.right-inset))-anchor.x;dy=std::clamp(anchor.y+dy,view.top,std::max(view.top,view.bottom-inset))-anchor.y;}item.translate(dx,dy);}
    else if(item.tool==Tool::pen||item.tool==Tool::highlight||item.tool==Tool::mosaic_brush) {
        double distance=std::hypot(double(point.x-surface.previous.x),double(point.y-surface.previous.y));int steps=std::max(1,int(distance/(item.tool==Tool::mosaic_brush?std::max(1,item.style.width/3):std::max(2,item.style.width))));
        for(int n=1;n<=steps&&item.points.size()<4096;++n) item.points.push_back({surface.previous.x+(point.x-surface.previous.x)*n/steps,surface.previous.y+(point.y-surface.previous.y)*n/steps});
    } else {if(item.tool==Tool::rectangle||item.tool==Tool::ellipse)point=constrained_shape(item.points.front(),point,GetKeyState(VK_SHIFT)<0);item.points.back()=point;}
    surface.previous=point;surface.document->changed();InvalidateRect(surface.window,nullptr,FALSE);
}
void mouse_up(Surface &surface) {if(surface.document&&surface.drawing&&surface.active>=0&&size_t(surface.active)<surface.document->items.size()){auto const& item=surface.document->items[size_t(surface.active)];bool shape=item.tool==Tool::rectangle||item.tool==Tool::ellipse||item.tool==Tool::mosaic,line=item.tool==Tool::line||item.tool==Tool::arrow;if((shape||line)&&item.points.size()>=2){auto a=item.points.front(),b=item.points.back();if((shape&&(a.x==b.x||a.y==b.y))||(line&&a.x==b.x&&a.y==b.y)){surface.document->items.erase(surface.document->items.begin()+surface.active);surface.active=-1;surface.document->changed();}}}surface.drawing=false;surface.moving=false;surface.move_started=false;if(surface.document)surface.document->finish_edit();ReleaseCapture();update_toolbar(surface);}
void wheel(Surface &surface,POINT screen,WPARAM wp) {
    int delta=GET_WHEEL_DELTA_WPARAM(wp);auto reset=[&]{surface.wheel_target=-3;surface.wheel_delta=0;};if(!surface.document||!surface.wheel_sizes||!delta||surface.drawing||surface.moving){reset();return;}ScreenToClient(surface.window,&screen);auto viewport=image_viewport(surface);if(!PtInRect(&viewport,screen)){reset();return;}
    if(surface.tool==Tool::eraser){int steps=wheel_steps(surface,-1,delta);if(!steps)return;brush_cursor(surface,screen,false);surface.style.width=std::clamp(surface.style.width+steps*2,1,160);surface.group_styles[6]=surface.style;brush_cursor(surface,screen,true);update_toolbar(surface);return;}
    if(surface.edit){RECT editor{};GetWindowRect(surface.edit,&editor);MapWindowPoints(nullptr,surface.window,reinterpret_cast<POINT*>(&editor),2);if(PtInRect(&editor,screen)){int size=stepped_size(surface.style.font_size,wheel_steps(surface,-2,delta),text_sizes);if(size!=surface.style.font_size){surface.style.font_size=size;change_style(surface);}return;}}
    int hit=hit_annotation(surface,to_document(surface,screen),false,surface.edit_index);if(hit<0){reset();return;}int steps=wheel_steps(surface,hit,delta);if(!steps)return;auto& item=surface.document->items[size_t(hit)];int value=item.tool==Tool::text||item.tool==Tool::number?item.style.font_size:item.style.width;int size=item.tool==Tool::text?stepped_size(value,steps,text_sizes):item.tool==Tool::number?stepped_size(value,steps,number_sizes):stepped_size(value,steps,stroke_sizes);if(size==value)return;surface.document->checkpoint();if(item.tool==Tool::text||item.tool==Tool::number)item.style.font_size=size;else item.style.width=size;
    if(!surface.edit){surface.active=hit;surface.style=item.style;}surface.document->changed();InvalidateRect(surface.window,nullptr,FALSE);update_toolbar(surface);
}
void draw_bitmap(HDC dc,HBITMAP bitmap,RECT destination,int width,int height) {
    if(!bitmap) return;HDC source=CreateCompatibleDC(dc);auto old=SelectObject(source,bitmap);SetStretchBltMode(dc,HALFTONE);
    StretchBlt(dc,destination.left,destination.top,destination.right-destination.left,destination.bottom-destination.top,source,0,0,width,height,SRCCOPY);SelectObject(source,old);DeleteDC(source);
}
void paint_text_selection(Surface const& surface,HDC dc){if(!surface.editing||surface.edit||!surface.document||surface.active<0||size_t(surface.active)>=surface.document->items.size())return;auto const& item=surface.document->items[size_t(surface.active)];if(item.tool!=Tool::text)return;auto rect=item.bounds();POINT a=from_document(surface,{rect.left,rect.top}),b=from_document(surface,{rect.right,rect.bottom});auto viewport=image_viewport(surface);SaveDC(dc);IntersectClipRect(dc,viewport.left,viewport.top,viewport.right,viewport.bottom);unique_gdi<HPEN> pen(CreatePen(PS_DASH,1,RGB(77,155,255)));auto before=SelectObject(dc,pen.get()),brush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));Rectangle(dc,a.x-2,a.y-2,b.x+2,b.y+2);SelectObject(dc,before);SelectObject(dc,brush);RestoreDC(dc,-1);}
HWND create_pin(std::unique_ptr<Document> document,RECT screen,bool fit=false,std::function<void(std::vector<Annotation>)> complete={},bool edit=false,bool wheel_sizes=true) {
    auto slot=std::find_if(pins.begin(),pins.end(),[](auto const& pin){return !pin;});
    if(slot==pins.end()) { error_box(g_app.window,tr(L"最多同时显示 12 张贴图",L"Up to twelve pins can be open at once"),E_OUTOFMEMORY);return nullptr; }
    auto pin=std::make_unique<Pin>();pin->wheel_sizes=wheel_sizes;pin->resizable=fit&&!complete;pin->long_image=fit&&!wheel_sizes&&!complete;
    if(fit) { MONITORINFO monitor{sizeof monitor};GetMonitorInfoW(MonitorFromRect(&screen,MONITOR_DEFAULTTONEAREST),&monitor);pin->scale=std::min({1.,double(monitor.rcWork.right-monitor.rcWork.left-32)/document->width,double(monitor.rcWork.bottom-monitor.rcWork.top-32)/document->height});if(pin->long_image)pin->scale=std::min(1.,double(monitor.rcWork.right-monitor.rcWork.left-48)*.7/document->width);screen.right=screen.left+int(document->width*pin->scale);screen.bottom=screen.top+std::min(int(document->height*pin->scale),int(monitor.rcWork.bottom-monitor.rcWork.top-48));if(screen.bottom>monitor.rcWork.bottom-16) OffsetRect(&screen,0,monitor.rcWork.bottom-16-screen.bottom);if(screen.right>monitor.rcWork.right-16)OffsetRect(&screen,monitor.rcWork.right-16-screen.right,0); }
    if(!fit&&complete&&document->width>0)pin->scale=double(screen.right-screen.left)/document->width;
    pin->document=std::move(document);pin->complete=std::move(complete);WNDCLASSW cls{};cls.hInstance=g_app.instance;cls.lpfnWndProc=pin_proc;
    cls.lpszClassName=L"LightBlockDesktopNative.Pin";cls.style=CS_DBLCLKS;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&cls);auto pointer=pin.get();*slot=std::move(pin);
    int margin=px(5);HWND window=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,cls.lpszClassName,tr(L"轻块贴图",L"Light Block Pin"),WS_POPUP,
        screen.left-margin,screen.top-margin,screen.right-screen.left+2*margin,screen.bottom-screen.top+2*margin,nullptr,nullptr,g_app.instance,pointer);
    if(window) { pointer->window=window;margin=pin_margin(*pointer);SetWindowPos(window,nullptr,screen.left-margin,screen.top-margin,screen.right-screen.left+2*margin,screen.bottom-screen.top+2*margin,SWP_NOZORDER|SWP_NOACTIVATE);ShowWindow(window,SW_SHOWNOACTIVATE);if(!pointer->complete)copy_surface(*pointer);if(pointer->complete||edit)show_toolbar(*pointer); }else slot->reset();return window;
}
void finish(Capture &surface,bool pin,bool save) {
    if(!surface.document) return;commit_text(surface);
    if(save) { if(save_image(surface)) DestroyWindow(surface.window);return; }
    RECT rect=surface.selection;OffsetRect(&rect,GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN));auto document=std::move(surface.document);HWND window=surface.window;DestroyWindow(window);if(pin) create_pin(std::move(document),rect);
}
bool pin_opacity(Surface& surface,double value){
    value=std::clamp(value,.25,1.);auto style=GetWindowLongPtrW(surface.window,GWL_EXSTYLE);
    if(!(style&WS_EX_LAYERED)){SetLastError(0);if(!SetWindowLongPtrW(surface.window,GWL_EXSTYLE,style|WS_EX_LAYERED)&&GetLastError())return false;}
    if(!SetLayeredWindowAttributes(surface.window,0,BYTE(std::lround(value*255)),LWA_ALPHA)){if(!(style&WS_EX_LAYERED))SetWindowLongPtrW(surface.window,GWL_EXSTYLE,style);return false;}
    surface.opacity=value;surface.glow.update(surface.window,surface.border,pin_margin(surface),value);return true;
}
void pin_toggle_border(Surface& surface){
    // Preserve the image's screen coordinates and scale, not the old HWND frame.
    RECT content=image_viewport(surface);MapWindowPoints(surface.window,nullptr,reinterpret_cast<POINT*>(&content),2);
    surface.border=!surface.border;int margin=pin_margin(surface);
    SetWindowPos(surface.window,nullptr,content.left-margin,content.top-margin,content.right-content.left+2*margin,content.bottom-content.top+2*margin,SWP_NOZORDER|SWP_NOACTIVATE);
    InvalidateRect(surface.window,nullptr,FALSE);layout_text(surface);place_toolbar(surface);
}
void pin_zoom(Surface& surface,POINT anchor,int delta){
    if(!delta||!surface.document||surface.drawing||surface.moving)return;commit_text(surface);
    auto view=source_view(surface);int source_width=view.right-view.left,source_height=view.bottom-view.top;
    RECT content=image_viewport(surface);MapWindowPoints(surface.window,nullptr,reinterpret_cast<POINT*>(&content),2);
    int width=content.right-content.left,height=content.bottom-content.top;if(width<1||height<1)return;
    MONITORINFO monitor{sizeof monitor};if(!GetMonitorInfoW(MonitorFromPoint(anchor,MONITOR_DEFAULTTONEAREST),&monitor))return;
    int margin=pin_margin(surface);double factor=std::pow(1.16,std::clamp(delta/120.,-5.,5.));
    double maximum=double(monitor.rcWork.right-monitor.rcWork.left-2*margin)/source_width;if(!surface.long_image)maximum=std::min(maximum,double(monitor.rcWork.bottom-monitor.rcWork.top-2*margin)/source_height);
    double minimum=std::max(double(std::min(100,source_width))/source_width,double(std::min(70,source_height))/source_height);
    if(maximum<=0)return;double scale=std::clamp(double(width)/source_width*factor,std::min(minimum,maximum),maximum);
    int target_width=std::max(1,int(std::lround(source_width*scale))),target_height=std::max(1,int(std::lround(source_height*scale)));
    double anchor_y=surface.scroll_y+(anchor.y-content.top)*double(source_width)/width;
    if(surface.long_image)target_height=std::min(target_height,int(monitor.rcWork.bottom-monitor.rcWork.top-2*margin));
    int x=anchor.x-int(std::lround(double(anchor.x-content.left)*target_width/width))-margin,y=surface.long_image?content.top-margin:anchor.y-int(std::lround(double(anchor.y-content.top)*target_height/height))-margin;
    x=std::clamp(x,int(monitor.rcWork.left),std::max(int(monitor.rcWork.left),int(monitor.rcWork.right)-target_width-2*margin));y=std::clamp(y,int(monitor.rcWork.top),std::max(int(monitor.rcWork.top),int(monitor.rcWork.bottom)-target_height-2*margin));
    if(surface.long_image)surface.scroll_y=std::clamp(anchor_y-(anchor.y-y-margin)/scale,0.,std::max(0.,source_height-target_height/scale));
    surface.scale=scale;SetWindowPos(surface.window,nullptr,x,y,target_width+2*margin,target_height+2*margin,SWP_NOZORDER|SWP_NOACTIVATE);InvalidateRect(surface.window,nullptr,FALSE);place_toolbar(surface);
}
void pin_scroll(Surface& surface,double delta){
    if(!surface.long_image||surface.drawing||surface.moving||surface.cropping)return;commit_text(surface);
    auto view=source_view(surface),area=image_viewport(surface);
    double scale=double(area.right-area.left)/std::max(1L,view.right-view.left);
    double limit=std::max(0.,view.bottom-view.top-(area.bottom-area.top)/std::max(.001,scale));
    double next=std::clamp(surface.scroll_y+delta/std::max(.001,scale),0.,limit);
    if(std::abs(next-surface.scroll_y)<.01)return;surface.scroll_y=next;
    layout_text(surface);InvalidateRect(surface.window,nullptr,FALSE);
}
HBITMAP render_result(Surface& surface,unique_gdi<HBITMAP>& storage){
    if(!surface.document)return nullptr;auto image=surface.document->render();RECT full{0,0,surface.document->width,surface.document->height},view=source_view(surface);
    if(EqualRect(&full,&view))return image;storage=crop(image,view);return storage.get();
}
bool copy_surface(Surface& surface){unique_gdi<HBITMAP> result;bool ok=copy_bitmap(surface.window,render_result(surface,result));if(!ok&&IsWindowVisible(surface.window))MessageBeep(MB_ICONWARNING);return ok;}
void pin_view_changed(Surface& surface,RECT before){
    auto view=source_view(surface);if(EqualRect(&before,&view))return;
    if(surface.long_image){surface.scroll_y=0;InvalidateRect(surface.window,nullptr,FALSE);layout_text(surface);place_toolbar(surface);return;}
    RECT content=image_viewport(surface);MapWindowPoints(surface.window,nullptr,reinterpret_cast<POINT*>(&content),2);
    double sx=double(content.right-content.left)/std::max(1L,before.right-before.left),sy=double(content.bottom-content.top)/std::max(1L,before.bottom-before.top);int margin=pin_margin(surface);
    int x=content.left+int(std::lround((view.left-before.left)*sx)),y=content.top+int(std::lround((view.top-before.top)*sy));
    int width=std::max(1,int(std::lround((view.right-view.left)*sx))),height=std::max(1,int(std::lround((view.bottom-view.top)*sy)));
    surface.scale=sx;SetWindowPos(surface.window,nullptr,x-margin,y-margin,width+margin*2,height+margin*2,SWP_NOZORDER|SWP_NOACTIVATE);layout_text(surface);place_toolbar(surface);InvalidateRect(surface.window,nullptr,FALSE);
}
void pin_crop_begin(Surface& surface){
    if(!surface.document||surface.complete)return;commit_text(surface);surface.active=-1;surface.drawing=surface.moving=surface.brush_visible=false;ReleaseCapture();surface.cropping=true;surface.crop_selected=false;surface.crop_drag=0;surface.crop_selection=source_view(surface);show_toolbar(surface);InvalidateRect(surface.window,nullptr,FALSE);
}
void pin_crop_cancel(Surface& surface){surface.cropping=false;surface.crop_drag=0;ReleaseCapture();update_toolbar(surface);InvalidateRect(surface.window,nullptr,FALSE);}
bool pin_crop_apply(Surface& surface){
    if(!surface.cropping||!surface.document)return false;auto before=source_view(surface);RECT selected{};if(!IntersectRect(&selected,&before,&surface.crop_selection)||!valid(selected))return false;
    if(!EqualRect(&before,&selected)){surface.document->checkpoint();Annotation marker;marker.tool=Tool::image_crop;marker.points={{selected.left,selected.top},{selected.right,selected.bottom}};surface.document->items.push_back(std::move(marker));surface.document->changed();surface.document->finish_edit();}
    surface.cropping=false;surface.crop_drag=0;ReleaseCapture();pin_view_changed(surface,before);update_toolbar(surface);InvalidateRect(surface.window,nullptr,FALSE);return true;
}
RECT pin_crop_client(Surface const& surface){auto a=from_document(surface,{surface.crop_selection.left,surface.crop_selection.top}),b=from_document(surface,{surface.crop_selection.right,surface.crop_selection.bottom});return {a.x,a.y,b.x,b.y};}
int pin_crop_hit(Surface const& surface,POINT point){
    if(!surface.crop_selected)return 0;auto r=pin_crop_client(surface);int pad=std::max(4,MulDiv(6,int(GetDpiForWindow(surface.window)),96));RECT reach=r;InflateRect(&reach,pad,pad);if(!PtInRect(&reach,point))return 0;int hit=0;if(abs(point.x-r.left)<=pad)hit|=1;if(abs(point.x-r.right)<=pad)hit|=2;if(abs(point.y-r.top)<=pad)hit|=4;if(abs(point.y-r.bottom)<=pad)hit|=8;return hit?hit:PtInRect(&r,point)?16:0;
}
void pin_crop_down(Surface& surface,POINT point){surface.crop_anchor=to_document(surface,point);surface.crop_before=surface.crop_selection;surface.crop_drag=pin_crop_hit(surface,point);if(!surface.crop_drag){surface.crop_drag=32;surface.crop_selection={surface.crop_anchor.x,surface.crop_anchor.y,surface.crop_anchor.x+1,surface.crop_anchor.y+1};}SetCapture(surface.window);InvalidateRect(surface.window,nullptr,FALSE);}
void pin_crop_move(Surface& surface,POINT point){
    if(!surface.crop_drag)return;auto at=to_document(surface,point);auto view=source_view(surface);auto r=surface.crop_before;
    if(surface.crop_drag==32){r=normalized(surface.crop_anchor,at);++r.right;++r.bottom;}
    else if(surface.crop_drag==16){int x=std::clamp(at.x-surface.crop_anchor.x,view.left-r.left,view.right-r.right),y=std::clamp(at.y-surface.crop_anchor.y,view.top-r.top,view.bottom-r.bottom);OffsetRect(&r,x,y);}
    else {if(surface.crop_drag&1)r.left=std::min(at.x,r.right-3);if(surface.crop_drag&2)r.right=std::max(at.x+1,r.left+3);if(surface.crop_drag&4)r.top=std::min(at.y,r.bottom-3);if(surface.crop_drag&8)r.bottom=std::max(at.y+1,r.top+3);}
    IntersectRect(&surface.crop_selection,&r,&view);InvalidateRect(surface.window,nullptr,FALSE);
}
void pin_crop_paint(Surface const& surface,HDC dc){
    if(!surface.cropping)return;auto r=pin_crop_client(surface),area=image_viewport(surface);HDC shade=CreateCompatibleDC(dc);unique_gdi<HBITMAP> pixel(CreateCompatibleBitmap(dc,1,1));if(shade&&pixel){auto prior=SelectObject(shade,pixel.get());SetPixel(shade,0,0,RGB(9,15,23));BLENDFUNCTION blend{AC_SRC_OVER,0,120,0};for(RECT outside:{RECT{area.left,area.top,area.right,r.top},RECT{area.left,r.bottom,area.right,area.bottom},RECT{area.left,r.top,r.left,r.bottom},RECT{r.right,r.top,area.right,r.bottom}})if(outside.right>outside.left&&outside.bottom>outside.top)AlphaBlend(dc,outside.left,outside.top,outside.right-outside.left,outside.bottom-outside.top,shade,0,0,1,1,blend);SelectObject(shade,prior);}if(shade)DeleteDC(shade);
    unique_gdi<HPEN> pen(CreatePen(PS_SOLID,1,RGB(90,189,250)));auto old=SelectObject(dc,pen.get()),brush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));Rectangle(dc,r.left,r.top,r.right,r.bottom);SelectObject(dc,brush);SelectObject(dc,old);
    unique_gdi<HBRUSH> handle(CreateSolidBrush(RGB(90,189,250)));for(POINT p:std::array<POINT,8>{{{r.left,r.top},{(r.left+r.right)/2,r.top},{r.right,r.top},{r.left,(r.top+r.bottom)/2},{r.right,(r.top+r.bottom)/2},{r.left,r.bottom},{(r.left+r.right)/2,r.bottom},{r.right,r.bottom}}}){RECT dot{p.x-3,p.y-3,p.x+4,p.y+4};FillRect(dc,&dot,handle.get());}
}
void draw_pin_image(Surface& surface,HDC dc){
    auto source=display_view(surface),destination=image_viewport(surface);auto image=surface.document->render(surface.edit?surface.edit_index:-1);if(!image)return;
    HDC pixels=CreateCompatibleDC(dc);if(!pixels)return;auto old=SelectObject(pixels,image);SetStretchBltMode(dc,HALFTONE);SetBrushOrgEx(dc,0,0,nullptr);StretchBlt(dc,destination.left,destination.top,destination.right-destination.left,destination.bottom-destination.top,pixels,source.left,source.top,source.right-source.left,source.bottom-source.top,SRCCOPY);SelectObject(pixels,old);DeleteDC(pixels);
}
int pin_resize_hit(Surface const& surface,POINT screen){
    if(!surface.resizable||surface.editing||!surface.border)return HTCAPTION;RECT r{};GetWindowRect(surface.window,&r);int pad=std::max(5,MulDiv(7,int(GetDpiForWindow(surface.window)),96));bool left=screen.x<r.left+pad,right=screen.x>=r.right-pad,top=screen.y<r.top+pad,bottom=screen.y>=r.bottom-pad;
    if(screen.x>=r.right-pad*2&&screen.y>=r.bottom-pad*2)return HTBOTTOMRIGHT;
    if(top)return left?HTTOPLEFT:right?HTTOPRIGHT:HTTOP;if(bottom)return left?HTBOTTOMLEFT:right?HTBOTTOMRIGHT:HTBOTTOM;return left?HTLEFT:right?HTRIGHT:HTCAPTION;
}
void pin_sizing(Surface& surface,int edge,RECT& proposed){
    if(surface.long_image){MONITORINFO monitor{sizeof monitor};if(!GetMonitorInfoW(MonitorFromWindow(surface.window,MONITOR_DEFAULTTONEAREST),&monitor))return;auto work=monitor.rcWork,view=source_view(surface);int margin=pin_margin(surface)*2;
        int width=std::clamp(int(proposed.right-proposed.left),100,std::max(100,int(work.right-work.left)));
        int full_height=int(std::lround(double(width-margin)*(view.bottom-view.top)/std::max(1L,view.right-view.left)))+margin;
        int height=std::clamp(int(proposed.bottom-proposed.top),std::min(100,full_height),std::max(1,std::min(full_height,int(work.bottom-work.top))));
        if(edge==WMSZ_LEFT||edge==WMSZ_TOPLEFT||edge==WMSZ_BOTTOMLEFT)proposed.left=proposed.right-width;else proposed.right=proposed.left+width;
        if(edge==WMSZ_TOP||edge==WMSZ_TOPLEFT||edge==WMSZ_TOPRIGHT)proposed.top=proposed.bottom-height;else proposed.bottom=proposed.top+height;return;}
    auto view=source_view(surface);int margin=pin_margin(surface);double aspect=double(view.right-view.left)/std::max(1L,view.bottom-view.top);if(aspect<=0)return;
    int width=std::max(40L,proposed.right-proposed.left-margin*2),height=std::max(30L,proposed.bottom-proposed.top-margin*2);
    if(edge==WMSZ_TOP||edge==WMSZ_BOTTOM)width=std::max(40,int(std::lround(height*aspect)));else height=std::max(30,int(std::lround(width/aspect)));
    if(width<height*aspect)width=std::max(40,int(std::lround(height*aspect)));else height=std::max(30,int(std::lround(width/aspect)));
    if(edge==WMSZ_LEFT||edge==WMSZ_TOPLEFT||edge==WMSZ_BOTTOMLEFT)proposed.left=proposed.right-width-margin*2;else proposed.right=proposed.left+width+margin*2;
    if(edge==WMSZ_TOP||edge==WMSZ_TOPLEFT||edge==WMSZ_TOPRIGHT)proposed.top=proposed.bottom-height-margin*2;else proposed.bottom=proposed.top+height+margin*2;
}
int pin_resize_edge(int hit){switch(hit){case HTLEFT:return WMSZ_LEFT;case HTRIGHT:return WMSZ_RIGHT;case HTTOP:return WMSZ_TOP;case HTBOTTOM:return WMSZ_BOTTOM;case HTTOPLEFT:return WMSZ_TOPLEFT;case HTTOPRIGHT:return WMSZ_TOPRIGHT;case HTBOTTOMLEFT:return WMSZ_BOTTOMLEFT;case HTBOTTOMRIGHT:return WMSZ_BOTTOMRIGHT;default:return 0;}}
void pin_resize_move(Surface& surface,POINT point){
    if(!surface.resize_drag)return;ClientToScreen(surface.window,&point);auto r=surface.resize_before;int dx=point.x-surface.resize_anchor.x,dy=point.y-surface.resize_anchor.y,edge=surface.resize_drag;
    if(edge==WMSZ_LEFT||edge==WMSZ_TOPLEFT||edge==WMSZ_BOTTOMLEFT)r.left+=dx;else if(edge!=WMSZ_TOP&&edge!=WMSZ_BOTTOM)r.right+=dx;
    if(edge==WMSZ_TOP||edge==WMSZ_TOPLEFT||edge==WMSZ_TOPRIGHT)r.top+=dy;else if(edge!=WMSZ_LEFT&&edge!=WMSZ_RIGHT)r.bottom+=dy;
    pin_sizing(surface,edge,r);SetWindowPos(surface.window,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);
}
HMENU pin_menu(Surface const& surface){
    HMENU menu=CreatePopupMenu();if(!menu)return nullptr;
    AppendMenuW(menu,MF_STRING,1,surface.editing?tr(L"完成编辑",L"Finish editing"):tr(L"编辑贴图",L"Edit in place"));AppendMenuW(menu,MF_STRING,2,tr(L"复制图片 · Ctrl+C",L"Copy image · Ctrl+C"));AppendMenuW(menu,MF_STRING,3,tr(L"保存 PNG… · Ctrl+S",L"Save PNG… · Ctrl+S"));
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,4,surface.border?tr(L"隐藏微光边框",L"Hide glow border"):tr(L"显示微光边框",L"Show glow border"));AppendMenuW(menu,MF_STRING,6,tr(L"恢复不透明",L"Restore opacity"));AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,5,tr(L"关闭贴图 · Esc",L"Close pin · Esc"));return menu;
}
LRESULT CALLBACK pin_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto surface=surface_for(window);
    if(message==WM_NCCREATE) { surface=static_cast<Pin*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);surface->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,LPARAM(surface)); }
    if(!surface) return DefWindowProcW(window,message,wp,lp);
    switch(message) {
    case WM_ERASEBKGND:return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;HDC output=BeginPaint(window,&ps);RECT bounds{};GetClientRect(window,&bounds);
        if(surface->paint_size.cx!=bounds.right||surface->paint_size.cy!=bounds.bottom||!surface->paint_buffer){surface->paint_buffer.reset(CreateCompatibleBitmap(output,std::max(1L,bounds.right),std::max(1L,bounds.bottom)));surface->paint_size={bounds.right,bounds.bottom};}
        HDC memory=surface->paint_buffer?CreateCompatibleDC(output):nullptr;HDC dc=memory?memory:output;
        auto previous=memory?SelectObject(memory,surface->paint_buffer.get()):nullptr;
        FillRect(dc,&bounds,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        draw_pin_image(*surface,dc);
        if(surface->border) { unique_gdi<HPEN> pen(CreatePen(PS_SOLID,1,RGB(123,224,205)));auto p=SelectObject(dc,pen.get()),b=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));Rectangle(dc,0,0,bounds.right,bounds.bottom);if(surface->resizable&&!surface->editing)for(int offset:{5,9}){MoveToEx(dc,bounds.right-offset,bounds.bottom-3,nullptr);LineTo(dc,bounds.right-3,bounds.bottom-offset);}SelectObject(dc,p);SelectObject(dc,b); }
        paint_text_selection(*surface,dc);paint_brush_cursor(*surface,dc);pin_crop_paint(*surface,dc);if(memory){BitBlt(output,ps.rcPaint.left,ps.rcPaint.top,ps.rcPaint.right-ps.rcPaint.left,ps.rcPaint.bottom-ps.rcPaint.top,dc,ps.rcPaint.left,ps.rcPaint.top,SRCCOPY);SelectObject(memory,previous);DeleteDC(memory);}EndPaint(window,&ps);return 0;
    }
    case WM_NCHITTEST:if(!surface->editing)return pin_resize_hit(*surface,{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});break;
    case WM_SETCURSOR:if(LOWORD(lp)==HTCLIENT&&surface->cropping){SetCursor(LoadCursorW(nullptr,IDC_CROSS));return TRUE;}if(LOWORD(lp)==HTCLIENT&&surface->editing&&surface->tool==Tool::eraser){POINT point{};GetCursorPos(&point);ScreenToClient(window,&point);auto area=image_viewport(*surface);if(PtInRect(&area,point)){brush_cursor(*surface,point,true);SetCursor(nullptr);return TRUE;}}break;
    case WM_MOUSELEAVE:surface->tracking=false;brush_cursor(*surface,surface->brush_point,false);return 0;
    case WM_NCLBUTTONDBLCLK:commit_text(*surface);if(surface->complete||copy_surface(*surface))DestroyWindow(window);return 0;
    case WM_NCLBUTTONDOWN:if(!surface->editing&&surface->resizable&&surface->border){auto edge=pin_resize_edge(int(wp));if(edge){surface->resize_drag=edge;surface->resize_anchor={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};GetWindowRect(window,&surface->resize_before);SetCapture(window);return 0;}}break;
    case WM_LBUTTONDBLCLK:if(surface->cropping){pin_crop_apply(*surface);return 0;}mouse_down(*surface,{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)},true);return 0;
    case WM_LBUTTONDOWN:if(surface->cropping){pin_crop_down(*surface,{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});return 0;}if(surface->editing) mouse_down(*surface,{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});return 0;
    case WM_MOUSEMOVE:if(surface->resize_drag){pin_resize_move(*surface,{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});return 0;}if(surface->cropping){pin_crop_move(*surface,{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});return 0;}mouse_move(*surface,{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});return 0;
    case WM_LBUTTONUP:if(surface->resize_drag){surface->resize_drag=0;ReleaseCapture();return 0;}if(surface->cropping){surface->crop_drag=0;surface->crop_selected=valid(surface->crop_selection);ReleaseCapture();return 0;}mouse_up(*surface);return 0;
    case WM_CAPTURECHANGED:surface->resize_drag=0;surface->crop_drag=0;surface->drawing=surface->moving=surface->move_started=false;if(surface->document)surface->document->finish_edit();return 0;
    case WM_CANCELMODE:surface->resize_drag=0;surface->crop_drag=0;surface->drawing=surface->moving=surface->move_started=false;if(surface->document)surface->document->finish_edit();if(GetCapture()==window)ReleaseCapture();return 0;
    case WM_MOUSEWHEEL:
        if(surface->cropping||surface->resize_drag)return 0;
        if(GET_KEYSTATE_WPARAM(wp)&MK_CONTROL)pin_zoom(*surface,{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)},GET_WHEEL_DELTA_WPARAM(wp));
        else if(surface->long_image&&!(GET_KEYSTATE_WPARAM(wp)&MK_SHIFT))pin_scroll(*surface,-GET_WHEEL_DELTA_WPARAM(wp)*96./120.);
        else if(surface->editing&&!(GET_KEYSTATE_WPARAM(wp)&MK_SHIFT))wheel(*surface,{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)},wp);
        else if(GET_WHEEL_DELTA_WPARAM(wp))pin_opacity(*surface,surface->opacity+GET_WHEEL_DELTA_WPARAM(wp)*.05/120.);return 0;
    case WM_NCRBUTTONUP:PostMessageW(window,WM_CONTEXTMENU,WPARAM(window),lp);return 0;
    case WM_RBUTTONUP:{if(surface->cropping){pin_crop_cancel(*surface);return 0;}POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ClientToScreen(window,&point);PostMessageW(window,WM_CONTEXTMENU,WPARAM(window),MAKELPARAM(point.x,point.y));return 0;}
    case WM_CONTEXTMENU: {
        if(surface->cropping){pin_crop_cancel(*surface);return 0;}
        auto identity=surface->identity;
        POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if(point.x==-1&&point.y==-1){RECT bounds{};GetWindowRect(window,&bounds);point={bounds.left+(bounds.right-bounds.left)/2,bounds.top+(bounds.bottom-bounds.top)/2};}
        int id=0;if(auto ui=frontend::capture_ui();ui&&ui->pin_menu)id=ui->pin_menu(uintptr_t(window),(g_app.english?1:0)|(surface->editing?2:0)|(surface->border?4:0),point.x,point.y);
        else {HMENU menu=pin_menu(*surface);if(!menu)return 0;scope_exit destroy([&]{DestroyMenu(menu);});id=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,window,nullptr);}
        auto live=current_surface(window);if(live!=surface||!live||live->identity!=identity)return 0;
        if(id==1){if(surface->editing)close_toolbar(*surface);else show_toolbar(*surface);}else if(id==2) { commit_text(*surface);copy_surface(*surface); }
        else if(id==3) save_image(*surface);else if(id==4)pin_toggle_border(*surface);else if(id==6)pin_opacity(*surface,1.);
        else if(id==5){commit_text(*surface);DestroyWindow(window);}return 0;
    }
    case WM_WINDOWPOSCHANGED:DefWindowProcW(window,message,wp,lp);surface->glow.update(window,surface->border,pin_margin(*surface),surface->opacity);return 0;
    case WM_MOVE:place_toolbar(*surface);return 0;
    case WM_SIZING:pin_sizing(*surface,int(wp),*reinterpret_cast<RECT*>(lp));return TRUE;
    case WM_SIZE:{auto view=source_view(*surface),area=image_viewport(*surface);surface->scale=double(area.right-area.left)/std::max(1L,view.right-view.left);layout_text(*surface);place_toolbar(*surface);InvalidateRect(window,nullptr,FALSE);return 0;}
    case WM_APP+71:if(surface->edit&&GetFocus()!=surface->edit&&!toolbar_has_focus(*surface,GetFocus())) commit_text(*surface);return 0;
    case WM_COMMAND:if(LOWORD(wp)==8&&HIWORD(wp)==EN_CHANGE)layout_text(*surface);return 0;
    case WM_CTLCOLOREDIT:SetTextColor(HDC(wp),surface->style.color);SetBkColor(HDC(wp),RGB(27,33,42));return LRESULT(surface->edit_brush.get());
    case WM_KEYDOWN:
        if(surface->long_image&&(wp==VK_UP||wp==VK_DOWN||wp==VK_PRIOR||wp==VK_NEXT||wp==VK_HOME||wp==VK_END)){auto area=image_viewport(*surface);double step=wp==VK_UP?-48:wp==VK_DOWN?48:wp==VK_PRIOR?-(area.bottom-area.top)*.9:wp==VK_NEXT?(area.bottom-area.top)*.9:wp==VK_HOME?-1e9:1e9;pin_scroll(*surface,step);return 0;}
        if(wp==VK_ESCAPE) { if(surface->cropping)pin_crop_cancel(*surface);else if(surface->complete||!surface->editing)DestroyWindow(window);else close_toolbar(*surface); }
        else if(surface->cropping){if(wp==VK_RETURN)pin_crop_apply(*surface);}
        else if(wp=='C'&&GetKeyState(VK_CONTROL)<0){commit_text(*surface);copy_surface(*surface);}
        else if(wp=='S'&&GetKeyState(VK_CONTROL)<0)save_image(*surface);
        else if(wp=='Z'&&GetKeyState(VK_CONTROL)<0) capture_command(uintptr_t(window),GetKeyState(VK_SHIFT)<0?CaptureRedo:CaptureUndo,0,nullptr);
        else if(wp=='Y'&&GetKeyState(VK_CONTROL)<0) capture_command(uintptr_t(window),CaptureRedo,0,nullptr);return 0;
    case WM_CLOSE:DestroyWindow(window);return 0;
    case WM_DESTROY:surface->glow.reset();close_toolbar(*surface);if(surface->complete) { auto callback=std::move(surface->complete);auto retain=std::move(surface->retain_history);auto history=surface->document->take_history();auto items=std::move(surface->document->items);if(retain)retain(std::move(history));callback(std::move(items)); }for(auto &pin:pins) if(pin.get()==surface) { pin.reset();break; }return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
struct WindowPick { POINT point;RECT rectangle{},frame{};bool outer{};unsigned count{}; };
bool pick_child_rect(WindowPick& pick,RECT candidate){RECT clipped{};if(!IntersectRect(&clipped,&candidate,&pick.frame)||EqualRect(&clipped,&pick.frame)||clipped.right-clipped.left<32||clipped.bottom-clipped.top<24||!PtInRect(&clipped,pick.point))return false;auto area=[](RECT r){return int64_t(r.right-r.left)*(r.bottom-r.top);};if(area(clipped)>=area(pick.rectangle))return false;pick.rectangle=clipped;return true;}
BOOL CALLBACK pick_child(HWND window,LPARAM data){auto& pick=*reinterpret_cast<WindowPick*>(data);if(++pick.count>50)return FALSE;RECT rect{};if(IsWindowVisible(window)&&GetWindowRect(window,&rect))pick_child_rect(pick,rect);return TRUE;}
BOOL CALLBACK pick_window(HWND window,LPARAM data) {
    auto &pick=*reinterpret_cast<WindowPick*>(data);DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    if(pid==GetCurrentProcessId()||!IsWindowVisible(window)||IsIconic(window)) return TRUE;
    wchar_t name[80];GetClassNameW(window,name,80);if(!wcscmp(name,L"Progman")||!wcscmp(name,L"WorkerW")||!wcscmp(name,L"Shell_TrayWnd")||!wcscmp(name,L"Shell_SecondaryTrayWnd")) return TRUE;
    DWORD cloaked=0;DwmGetWindowAttribute(window,DWMWA_CLOAKED,&cloaked,sizeof cloaked);if(cloaked) return TRUE;
    RECT bounds{};if(FAILED(DwmGetWindowAttribute(window,DWMWA_EXTENDED_FRAME_BOUNDS,&bounds,sizeof bounds))) GetWindowRect(window,&bounds);
    if(PtInRect(&bounds,pick.point)&&bounds.right-bounds.left>=32&&bounds.bottom-bounds.top>=24) { pick.rectangle=pick.frame=bounds;if(!pick.outer)EnumChildWindows(window,pick_child,data);return FALSE; }return TRUE;
}
void select_done(Capture &surface) {
    surface.selected=valid(surface.selection);if(!surface.selected) return;auto bitmap=crop(surface.snapshot.get(),surface.selection);if(!bitmap) { surface.selected=false;return; }
    surface.document=std::make_unique<Document>(bitmap.release());surface.tool=Tool::select;
    if(surface.record_after_selection){surface.record_after_selection=false;PostMessageW(surface.window,WM_APP+79,surface.transition_serial,0);}else show_toolbar(surface);
    InvalidateRect(surface.window,nullptr,FALSE);
}
int resize_hit(Capture const& surface,POINT point) {
    auto rect=surface.selection;int margin=px(8);
    if(point.x<rect.left-margin||point.x>rect.right+margin||point.y<rect.top-margin||point.y>rect.bottom+margin) return 0;
    int hit=0;if(abs(point.x-rect.left)<margin) hit|=1;if(abs(point.x-rect.right)<margin) hit|=2;if(abs(point.y-rect.top)<margin) hit|=4;if(abs(point.y-rect.bottom)<margin) hit|=8;return hit;
}
void invalidate_selection(Capture& surface,RECT before) {
    auto current=surface.selection;if(EqualRect(&before,&current))return;
    unique_gdi<HRGN> dirty(CreateRectRgnIndirect(&before)),next(CreateRectRgnIndirect(&current));CombineRgn(dirty.get(),dirty.get(),next.get(),RGN_XOR);
    for(RECT r:{before,current}){RECT outer=r,inner=r;InflateRect(&outer,px(8),px(8));InflateRect(&inner,-px(8),-px(8));unique_gdi<HRGN> edge(CreateRectRgnIndirect(&outer)),hole(CreateRectRgnIndirect(&inner));if(valid(inner))CombineRgn(edge.get(),edge.get(),hole.get(),RGN_DIFF);CombineRgn(dirty.get(),dirty.get(),edge.get(),RGN_OR);RECT label{r.left,std::max(0L,r.top-32),r.left+260,r.top};unique_gdi<HRGN> text(CreateRectRgnIndirect(&label));CombineRgn(dirty.get(),dirty.get(),text.get(),RGN_OR);}
    InvalidateRgn(surface.window,dirty.get(),FALSE);
}
void shade_capture(Capture const& surface,HDC dc,HDC shade){
    if(!surface.selected||surface.drag==32)return;RECT rect=surface.selection;BLENDFUNCTION blend{AC_SRC_OVER,0,105,0};
    AlphaBlend(dc,0,0,surface.width,rect.top,shade,0,0,1,1,blend);AlphaBlend(dc,0,rect.bottom,surface.width,surface.height-rect.bottom,shade,0,0,1,1,blend);
    AlphaBlend(dc,0,rect.top,rect.left,rect.bottom-rect.top,shade,0,0,1,1,blend);AlphaBlend(dc,rect.right,rect.top,surface.width-rect.right,rect.bottom-rect.top,shade,0,0,1,1,blend);
}
void paint_capture(Capture &surface) {
    unique_gdi<HRGN> update(CreateRectRgn(0,0,0,0));GetUpdateRgn(surface.window,update.get(),FALSE);
    PAINTSTRUCT ps;HDC output=BeginPaint(surface.window,&ps),dc=CreateCompatibleDC(output),source=CreateCompatibleDC(output);
    SelectClipRgn(dc,update.get());auto a=SelectObject(dc,surface.buffer.get()),b=SelectObject(source,surface.snapshot.get());BitBlt(dc,ps.rcPaint.left,ps.rcPaint.top,ps.rcPaint.right-ps.rcPaint.left,ps.rcPaint.bottom-ps.rcPaint.top,source,ps.rcPaint.left,ps.rcPaint.top,SRCCOPY);
    HDC shade=CreateCompatibleDC(output);unique_gdi<HBITMAP> pixel(CreateCompatibleBitmap(output,1,1));auto previous=SelectObject(shade,pixel.get());SetPixel(shade,0,0,RGB(13,19,27));
    RECT rect=surface.selection;bool selected=surface.selected||surface.drag!=0||valid(rect);shade_capture(surface,dc,shade);
    if(surface.document&&surface.selected&&!surface.drag&&!surface.document->items.empty()) draw_bitmap(dc,surface.document->render(surface.edit?surface.edit_index:-1),rect,surface.document->width,surface.document->height);
    if(selected) {
        unique_gdi<HPEN> pen(CreatePen(PS_SOLID,2,RGB(90,189,250)));auto old_pen=SelectObject(dc,pen.get()),old_brush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
        Rectangle(dc,rect.left,rect.top,rect.right,rect.bottom);SelectObject(dc,old_brush);SelectObject(dc,old_pen);
        if(surface.selected) {
            unique_gdi<HBRUSH> handles(CreateSolidBrush(RGB(90,189,250)));
            for(POINT p:std::array<POINT,8>{{{rect.left,rect.top},{(rect.left+rect.right)/2,rect.top},{rect.right,rect.top},{rect.left,(rect.top+rect.bottom)/2},{rect.right,(rect.top+rect.bottom)/2},{rect.left,rect.bottom},{(rect.left+rect.right)/2,rect.bottom},{rect.right,rect.bottom}}}) { RECT handle{p.x-3,p.y-3,p.x+4,p.y+4};FillRect(dc,&handle,handles.get()); }
        }
        wchar_t dimensions[90];StringCchPrintfW(dimensions,90,L"%ld × %ld px",rect.right-rect.left,rect.bottom-rect.top);
        draw_text(dc,dimensions,RECT{rect.left,std::max(0L,rect.top-30),rect.left+240,rect.top},g_app.small,RGB(255,255,255),DT_SINGLELINE);
    }
    if(!surface.selected) draw_text(dc,tr(L"拖动选择区域 · 右键取消 · Esc 退出",L"Drag to select a region · Right-click or Esc to cancel"),RECT{20,surface.height-px(54),surface.width-20,surface.height-px(16)},g_app.small,RGB(235,240,247),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    paint_text_selection(surface,dc);paint_brush_cursor(surface,dc);BitBlt(output,ps.rcPaint.left,ps.rcPaint.top,ps.rcPaint.right-ps.rcPaint.left,ps.rcPaint.bottom-ps.rcPaint.top,dc,ps.rcPaint.left,ps.rcPaint.top,SRCCOPY);
    SelectObject(shade,previous);DeleteDC(shade);SelectObject(dc,a);SelectObject(source,b);DeleteDC(dc);DeleteDC(source);EndPaint(surface.window,&ps);
}
LRESULT CALLBACK capture_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(!session) return DefWindowProcW(window,message,wp,lp);auto &surface=*session;
    switch(message) {
    case WM_DISPLAYCHANGE:if(!surface.source_layout.empty()&&surface.source_layout!=screen_layout()){DestroyWindow(window);set_status(L"显示器布局已改变，请重新框选截图。",L"Display layout changed. Select a new capture region.");}return 0;
    case WM_APP+79:if(wp==surface.transition_serial)capture_command(uintptr_t(window),CaptureRecord,0,nullptr);return 0;
    case WM_APP+80:if(surface.transition_queued&&surface.selected&&unsigned(lp)==surface.transition_serial){RECT area=surface.selection;OffsetRect(&area,GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN));auto first=wp==CaptureLongshot?crop(surface.snapshot.get(),surface.selection):unique_gdi<HBITMAP>{};DestroyWindow(window);if(wp==CaptureRecord)recording::start(area);else if(wp==CaptureLongshot)scroll::start(area,first.get());}return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:paint_capture(surface);return 0;
    case WM_SYSKEYDOWN:case WM_SYSKEYUP:
        if(wp==VK_MENU&&!surface.selected&&!surface.drag&&surface.smart){POINT point{};GetCursorPos(&point);WindowPick pick{point};pick.outer=message==WM_SYSKEYDOWN;EnumWindows(pick_window,LPARAM(&pick));OffsetRect(&pick.rectangle,-GetSystemMetrics(SM_XVIRTUALSCREEN),-GetSystemMetrics(SM_YVIRTUALSCREEN));auto before=surface.selection;RECT client{0,0,surface.width,surface.height};IntersectRect(&surface.selection,&pick.rectangle,&client);invalidate_selection(surface,before);return 0;}break;
    case WM_LBUTTONDBLCLK:if(surface.selected) mouse_down(surface,{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)},true);return 0;
    case WM_LBUTTONDOWN: {
        if(surface.edit){commit_text(surface);return 0;}
        POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};surface.anchor=point;surface.before=surface.selection;
        if(surface.selected) {
            int edge=resize_hit(surface,point);
            if(edge) { commit_text(surface);surface.drag=edge;hide_toolbar(surface);SetCapture(window);InvalidateRect(window,&surface.selection,FALSE); }
            else if(PtInRect(&surface.selection,point)) {
                POINT local=to_document(surface,point);
                if(surface.edit){commit_text(surface);return 0;}
                if(surface.tool==Tool::select&&hit_annotation(surface,local)<0) { surface.drag=16;hide_toolbar(surface);SetCapture(window);InvalidateRect(window,&surface.selection,FALSE); }
                else mouse_down(surface,point);
            }else {hide_toolbar(surface);surface.drag=32;surface.selected=false;auto before=surface.selection;surface.selection=normalized(point,point);invalidate_selection(surface,before);SetCapture(window);}
        }else { hide_toolbar(surface);surface.drag=32;auto before=surface.selection;surface.selection=normalized(point,point);invalidate_selection(surface,before);SetCapture(window); }
        return 0;
    }
    case WM_MOUSEMOVE: {
        RECT before=surface.selection;
        POINT point{std::clamp(GET_X_LPARAM(lp),0,surface.width),std::clamp(GET_Y_LPARAM(lp),0,surface.height)};
        if(surface.drag==32) surface.selection=normalized(surface.anchor,point);
        else if(surface.drag==16) {
            int dx=std::clamp(int(point.x-surface.anchor.x),-int(surface.before.left),surface.width-int(surface.before.right));
            int dy=std::clamp(int(point.y-surface.anchor.y),-int(surface.before.top),surface.height-int(surface.before.bottom));
            surface.selection=surface.before;OffsetRect(&surface.selection,dx,dy);
        } else if(surface.drag) {
            surface.selection=surface.before;
            if(surface.drag&1) surface.selection.left=std::min(point.x,surface.before.right-3);
            if(surface.drag&2) surface.selection.right=std::max(point.x,surface.before.left+3);
            if(surface.drag&4) surface.selection.top=std::min(point.y,surface.before.bottom-3);
            if(surface.drag&8) surface.selection.bottom=std::max(point.y,surface.before.top+3);
        } else if(surface.selected) { mouse_move(surface,point);return 0; }
        else if(surface.smart) {
            WindowPick pick{POINT{point.x+GetSystemMetrics(SM_XVIRTUALSCREEN),point.y+GetSystemMetrics(SM_YVIRTUALSCREEN)}};pick.outer=GetKeyState(VK_MENU)<0;EnumWindows(pick_window,LPARAM(&pick));
            OffsetRect(&pick.rectangle,-GetSystemMetrics(SM_XVIRTUALSCREEN),-GetSystemMetrics(SM_YVIRTUALSCREEN));RECT client{0,0,surface.width,surface.height};IntersectRect(&surface.selection,&pick.rectangle,&client);
        }
        invalidate_selection(surface,before);return 0;
    }
    case WM_LBUTTONUP: {
        int drag=surface.drag;surface.drag=0;mouse_up(surface);
        if(drag) {
            if(drag==32&&!valid(surface.selection)&&valid(surface.before))surface.selection=surface.before;
            else if(drag==32&&!valid(surface.selection)&&surface.smart) {
                WindowPick pick{POINT{GET_X_LPARAM(lp)+GetSystemMetrics(SM_XVIRTUALSCREEN),GET_Y_LPARAM(lp)+GetSystemMetrics(SM_YVIRTUALSCREEN)}};pick.outer=GetKeyState(VK_MENU)<0;EnumWindows(pick_window,LPARAM(&pick));
                OffsetRect(&pick.rectangle,-GetSystemMetrics(SM_XVIRTUALSCREEN),-GetSystemMetrics(SM_YVIRTUALSCREEN));RECT client{0,0,surface.width,surface.height};IntersectRect(&surface.selection,&pick.rectangle,&client);
            }
            std::vector<Annotation> items;if(surface.document){items=surface.document->items;for(auto& item:items)item.translate(surface.before.left-surface.selection.left,surface.before.top-surface.selection.top);}select_done(surface);
            if(surface.document) { surface.document->items=std::move(items);surface.document->changed(); }else show_toolbar(surface);
        }return 0;
    }
    case WM_RBUTTONUP:
        if(surface.selected||surface.drag) capture_command(uintptr_t(window),CaptureReset,0,nullptr);
        else DestroyWindow(window);return 0;
    case WM_MOUSELEAVE:surface.tracking=false;brush_cursor(surface,surface.brush_point,false);return 0;
    case WM_SETCURSOR:if(LOWORD(lp)==HTCLIENT){POINT point{};GetCursorPos(&point);ScreenToClient(window,&point);LPCWSTR cursor=IDC_CROSS;if(surface.selected){int edge=resize_hit(surface,point);if(edge==5||edge==10)cursor=IDC_SIZENWSE;else if(edge==6||edge==9)cursor=IDC_SIZENESW;else if(edge&3)cursor=IDC_SIZEWE;else if(edge&12)cursor=IDC_SIZENS;else if(PtInRect(&surface.selection,point)){if(surface.tool==Tool::eraser){brush_cursor(surface,point,true);SetCursor(nullptr);return TRUE;}if(surface.tool==Tool::select)cursor=IDC_SIZEALL;else if(surface.tool==Tool::text)cursor=surface.document&&hit_annotation(surface,to_document(surface,point))>=0?IDC_SIZEALL:IDC_IBEAM;}}SetCursor(LoadCursorW(nullptr,cursor));return TRUE;}break;
    case WM_MOUSEWHEEL:if(surface.selected) wheel(surface,{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)},wp);return 0;
    case WM_APP+71:if(surface.edit&&GetFocus()!=surface.edit&&!toolbar_has_focus(surface,GetFocus())) commit_text(surface);return 0;
    case WM_COMMAND:if(LOWORD(wp)==8&&HIWORD(wp)==EN_CHANGE)layout_text(surface);return 0;
    case WM_CTLCOLOREDIT:SetTextColor(HDC(wp),surface.style.color);SetBkColor(HDC(wp),RGB(27,33,42));return LRESULT(surface.edit_brush.get());
    case WM_KEYDOWN:
        if(wp==VK_ESCAPE) DestroyWindow(window);
        else if(wp==VK_RETURN&&surface.selected&&!surface.edit) capture_command(uintptr_t(window),CaptureCopy,0,nullptr);
        else if(wp=='Z'&&GetKeyState(VK_CONTROL)<0&&surface.document) capture_command(uintptr_t(window),GetKeyState(VK_SHIFT)<0?CaptureRedo:CaptureUndo,0,nullptr);
        else if(wp=='Y'&&GetKeyState(VK_CONTROL)<0&&surface.document) capture_command(uintptr_t(window),CaptureRedo,0,nullptr);
        else if(wp=='C'&&GetKeyState(VK_CONTROL)<0&&surface.document) capture_command(uintptr_t(window),CaptureCopy,0,nullptr);
        else if(wp=='S'&&GetKeyState(VK_CONTROL)<0&&surface.document) capture_command(uintptr_t(window),CaptureSave,0,nullptr);return 0;
    case WM_DESTROY:ReleaseCapture();close_toolbar(surface);session.reset();return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
}
void capture_start(HWND owner,bool record) {
    (void)owner;if(g_app.close_requested||session) return;
    int x=GetSystemMetrics(SM_XVIRTUALSCREEN),y=GetSystemMetrics(SM_YVIRTUALSCREEN),w=GetSystemMetrics(SM_CXVIRTUALSCREEN),h=GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if(w<=0||h<=0||uint64_t(w)*h>64*1024*1024) {log_line(L"capture rejected virtual bounds %d x %d",w,h);return;}
    auto surface=std::make_unique<Capture>();surface->source_layout=screen_layout();surface->record_after_selection=record;surface->width=w;surface->height=h;HDC screen=GetDC(nullptr),memory=CreateCompatibleDC(screen);
    surface->snapshot.reset(CreateCompatibleBitmap(screen,w,h));surface->buffer.reset(CreateCompatibleBitmap(screen,w,h));auto old=SelectObject(memory,surface->snapshot.get());
    BOOL ok=BitBlt(memory,0,0,w,h,screen,x,y,SRCCOPY|CAPTUREBLT);SelectObject(memory,old);DeleteDC(memory);ReleaseDC(nullptr,screen);if(!ok||!surface->buffer) {log_line(L"capture bitmap failed error=%lu",GetLastError());return;}
    WNDCLASSW cls{};cls.hInstance=g_app.instance;cls.lpfnWndProc=capture_proc;cls.style=CS_DBLCLKS;cls.hCursor=LoadCursorW(nullptr,IDC_CROSS);
    cls.lpszClassName=L"LightBlockDesktopNative.Capture";RegisterClassW(&cls);session=std::move(surface);
    session->window=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,cls.lpszClassName,tr(L"轻块截屏",L"Light Block Capture"),WS_POPUP,x,y,w,h,nullptr,nullptr,g_app.instance,nullptr);
    if(session->window) { ShowWindow(session->window,SW_SHOW);SetForegroundWindow(session->window);SetFocus(session->window);show_toolbar(*session);log_line(L"capture selection shown bounds=%d x %d",w,h); }else {log_line(L"capture window creation failed error=%lu",GetLastError());session.reset();}
}
void capture_shutdown(void) { if(session) DestroyWindow(session->window);for(auto &pin:pins) if(pin) DestroyWindow(pin->window); }
void capture_pin_bitmap(HBITMAP bitmap,RECT location,bool fit,bool edit,bool wheel_sizes) { create_pin(std::make_unique<lb::capture::Document>(bitmap),location,fit,{},edit,wheel_sizes); }
void lb::capture::edit_clipboard(HWND owner) {
    unique_gdi<HBITMAP> copied;if(OpenClipboard(owner)){auto bitmap=HBITMAP(GetClipboardData(CF_BITMAP));BITMAP info{};if(bitmap&&GetObjectW(bitmap,sizeof info,&info)&&info.bmWidth>0&&info.bmHeight>0&&uint64_t(info.bmWidth)*info.bmHeight<=32*1024*1024)copied.reset(HBITMAP(CopyImage(bitmap,IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION)));CloseClipboard();}
    if(!copied){set_status(L"剪贴板中没有可编辑的图片。",L"There is no editable image on the clipboard.");return;}
    BITMAP info{};GetObjectW(copied.get(),sizeof info,&info);POINT cursor{};GetCursorPos(&cursor);MONITORINFO monitor{sizeof monitor};GetMonitorInfoW(MonitorFromPoint(cursor,MONITOR_DEFAULTTONEAREST),&monitor);int x=monitor.rcWork.left+std::max(0L,(monitor.rcWork.right-monitor.rcWork.left-info.bmWidth)/2),y=monitor.rcWork.top+std::max(0L,(monitor.rcWork.bottom-monitor.rcWork.top-info.bmHeight)/2);HWND window=create_pin(std::make_unique<Document>(copied.release()),RECT{x,y,x+info.bmWidth,y+info.bmHeight},true);for(auto& pin:pins)if(pin&&pin->window==window){show_toolbar(*pin);break;}
}
HWND lb::capture::edit_project(HBITMAP bitmap,RECT location,std::vector<Annotation> items,std::function<void(std::vector<Annotation>)> complete,Tool initial,Document::History history,std::function<void(Document::History)> retain_history) { auto document=std::make_unique<Document>(bitmap);document->items=std::move(items);document->restore_history(std::move(history));document->changed();auto window=create_pin(std::move(document),location,false,std::move(complete));if(auto surface=current_surface(window)){surface->retain_history=std::move(retain_history);select_tool(*surface,initial);}return window; }
bool lb::capture::editor_tests() {
    Surface surface;surface.window=CreateWindowExW(0,L"STATIC",L"editor regression fixture",WS_POPUP,0,0,400,200,nullptr,nullptr,g_app.instance,nullptr);if(!surface.window)return false;
    scope_exit close([&]{commit_text(surface);DestroyWindow(surface.window);});HDC dc=GetDC(surface.window);HBITMAP bitmap=CreateCompatibleBitmap(dc,400,200);ReleaseDC(surface.window,dc);if(!bitmap)return false;surface.document=std::make_unique<Document>(bitmap);surface.tool=Tool::text;start_text(surface,{24,24});HWND original=surface.edit;if(!original||surface.document->items.size()!=1)return false;
    RECT empty{},longer{},multiline{},shorter{};GetWindowRect(original,&empty);SetWindowTextW(original,L"中文 test dynamically grows");GetWindowRect(original,&longer);SetWindowTextW(original,L"中文\r\nsecond line\r\nthird line");GetWindowRect(original,&multiline);SetWindowTextW(original,L"x");GetWindowRect(original,&shorter);
    if(longer.right-longer.left<=empty.right-empty.left||multiline.bottom-multiline.top<=empty.bottom-empty.top||shorter.right-shorter.left>=longer.right-longer.left)return false;
    surface.style.font_size=48;surface.style.bold=true;surface.style.underline=true;change_style(surface);LOGFONTW selectedFont{};GetObjectW(surface.edit_font.get(),sizeof selectedFont,&selectedFont);if(surface.edit!=original||surface.document->items.size()!=1||surface.style.font_size!=48||!selectedFont.lfUnderline)return false;
    SetWindowTextW(original,std::wstring(160,L'A').c_str());if(SendMessageW(original,EM_GETLINECOUNT,0,0)!=1)return false;
    SetWindowTextW(surface.edit,L"中文 text");SendMessageW(surface.edit,EM_SETSEL,3,3);RECT text_rect{};GetWindowRect(surface.edit,&text_rect);SendMessageW(surface.edit,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),MAKELPARAM(text_rect.left+6,text_rect.top+6));if(surface.style.font_size!=56||surface.edit!=original)return false;surface.style.color=RGB(104,173,255);change_style(surface);DWORD first{},last{};SendMessageW(surface.edit,EM_GETSEL,WPARAM(&first),LPARAM(&last));if(surface.edit!=original||first!=3||last!=3||GetWindowTextLengthW(surface.edit)!=7)return false;commit_text(surface);if(surface.document->items[0].text!=L"中文 text"||surface.document->items[0].style.font_size!=56)return false;
    auto start=to_document(surface,{28,28}),end=to_document(surface,{48,38});auto anchor=surface.document->items[0].points[0];mouse_down(surface,{28,28});if(surface.edit||!surface.moving)return false;mouse_move(surface,{48,38});mouse_up(surface);if(surface.document->items[0].points[0].x!=anchor.x+end.x-start.x||surface.document->items[0].points[0].y!=anchor.y+end.y-start.y)return false;
    mouse_down(surface,{48,38},true);if(!surface.edit)return false;DWORD caret_begin{},caret_end{};SendMessageW(surface.edit,EM_GETSEL,WPARAM(&caret_begin),LPARAM(&caret_end));if(caret_begin!=7||caret_end!=7)return false;mouse_down(surface,{390,190});if(surface.edit||surface.document->items.size()!=1)return false;
    start_text(surface,{390,190});RECT edge{};GetWindowRect(surface.edit,&edge);MapWindowPoints(nullptr,surface.window,reinterpret_cast<POINT*>(&edge),2);if(edge.right>398||edge.bottom>198)return false;SetWindowTextW(surface.edit,L" \r\n ");commit_text(surface);if(surface.document->items.size()!=1)return false;
    Annotation number;number.tool=Tool::number;number.style.number=1;number.points={{60,60}};surface.document->items={number};surface.active=0;surface.style=number.style;surface.style.number=2;surface.style.font_size=40;change_style(surface);if(surface.document->items[0].style.number!=1||surface.document->items[0].style.font_size!=40||surface.style.number!=2)return false;
    select_tool(surface,Tool::rectangle);surface.style.width=64;surface.style.color=RGB(104,173,255);select_tool(surface,Tool::mosaic);if(surface.style.width!=24)return false;surface.style.width=32;select_tool(surface,Tool::rectangle);if(surface.style.width!=64||surface.style.color!=RGB(104,173,255))return false;select_tool(surface,Tool::mosaic_brush);return surface.style.width==32;
}
bool lb::capture::selection_visual_tests(){
    if(session)return false;Capture surface;surface.width=160;surface.height=100;surface.selection={40,20,120,80};
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=160;info.bmiHeader.biHeight=-100;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* pixels{};unique_gdi<HBITMAP> image(CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0));if(!image)return false;
    HDC dc=CreateCompatibleDC(nullptr),shade=CreateCompatibleDC(nullptr);auto before=SelectObject(dc,image.get());unique_gdi<HBITMAP> pixel(CreateBitmap(1,1,1,32,nullptr));auto previous=SelectObject(shade,pixel.get());scope_exit release([&]{SelectObject(dc,before);SelectObject(shade,previous);DeleteDC(dc);DeleteDC(shade);});SetPixel(shade,0,0,RGB(13,19,27));
    for(int mode=0;mode<3;++mode){std::fill_n(static_cast<uint32_t*>(pixels),160*100,0xffffffffu);surface.selected=mode==2;surface.drag=mode==1?32:0;shade_capture(surface,dc,shade);GdiFlush();if(GetPixel(dc,80,50)!=RGB(255,255,255))return false;auto corner=GetPixel(dc,10,10);if(mode<2?corner!=RGB(255,255,255):GetRValue(corner)>=200)return false;}
    auto state=std::make_unique<Capture>();state->source_layout=screen_layout();if(state->source_layout.empty())return false;session=std::move(state);WNDCLASSW cls{};cls.hInstance=g_app.instance;cls.lpfnWndProc=capture_proc;cls.lpszClassName=L"LightBlockDesktopNative.SourceLayoutFixture";RegisterClassW(&cls);auto window=CreateWindowExW(0,cls.lpszClassName,L"hidden source layout fixture",WS_POPUP,0,0,160,100,nullptr,nullptr,g_app.instance,nullptr);if(!window){session.reset();return false;}session->window=window;scope_exit close([&]{if(IsWindow(window))DestroyWindow(window);});SendMessageW(window,WM_DISPLAYCHANGE,0,0);if(!session||!IsWindow(window))return false;session->source_layout=L"fixture-disconnected-display";SendMessageW(window,WM_DISPLAYCHANGE,0,0);return !session&&!IsWindow(window);
}
bool lb::capture::selection_tests() {
    if(session)return false;auto state=std::make_unique<Capture>();state->width=640;state->height=480;HDC dc=GetDC(nullptr);state->snapshot.reset(CreateCompatibleBitmap(dc,640,480));state->buffer.reset(CreateCompatibleBitmap(dc,640,480));ReleaseDC(nullptr,dc);if(!state->snapshot||!state->buffer)return false;
    WNDCLASSW cls{};cls.hInstance=g_app.instance;cls.lpfnWndProc=capture_proc;cls.lpszClassName=L"LightBlockDesktopNative.CaptureFixture";RegisterClassW(&cls);session=std::move(state);HWND window=CreateWindowExW(0,cls.lpszClassName,L"hidden capture fixture",WS_POPUP,0,0,640,480,nullptr,nullptr,g_app.instance,nullptr);if(!window){session.reset();return false;}session->window=window;scope_exit cleanup([&]{if(IsWindow(window))DestroyWindow(window);});
    session->selected=true;session->tool=Tool::mosaic;session->drawing=true;session->moving=true;session->record_after_selection=true;session->transition_queued=true;session->selection={20,20,400,300};unsigned obsolete=session->transition_serial;
    capture_command(uintptr_t(window),CaptureReset,0,nullptr);if(session->selected||session->tool!=Tool::select||session->drawing||session->moving||session->record_after_selection||session->transition_queued)return false;
    session->smart=false;SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(20,20));SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(400,300));SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(400,300));if(!session->selected||!session->document||session->document->width!=380||session->document->height!=280)return false;
    SendMessageW(window,WM_APP+79,obsolete,0);SendMessageW(window,WM_APP+80,CaptureRecord,obsolete);if(!session||session->transition_queued)return false;
    // A gesture begun at an edge must remain live after a reselect, and the
    // cursor's new source pixels are used instead of stretching the old image.
    Annotation mark;mark.points={{100,100},{120,120}};session->document->items={mark};session->document->changed();SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(200,150));SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(250,180));if(session->selection.left!=70||session->selection.top!=50)return false;SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(250,180));if(session->document->items.size()!=1||session->document->items[0].points[0].x!=50||session->document->items[0].points[0].y!=70)return false;
    // Erasing either a hit or empty canvas is editing, never a new selection.
    mark.tool=Tool::rectangle;mark.style.filled=true;mark.points={{20,20},{100,100}};session->document->items={mark};session->document->changed();capture_command(uintptr_t(window),CaptureSelectTool,int(Tool::eraser),nullptr);SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(120,100));SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(120,100));if(session->document->items.size()!=2||session->document->items.back().tool!=Tool::eraser||!session->selected||session->drag||session->tool!=Tool::eraser||!session->document->can_undo())return false;
    SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(200,180));SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(200,180));if(!session->selected||session->drag||session->tool!=Tool::eraser)return false;
    session->document->items.clear();session->document->changed();capture_command(uintptr_t(window),CaptureSelectTool,int(Tool::text),nullptr);start_text(*session,{40,40});if(!session->edit)return false;SetWindowTextW(session->edit,L"Erase after inline text");capture_command(uintptr_t(window),CaptureSelectTool,int(Tool::eraser),nullptr);if(session->edit||session->document->items.size()!=1||session->tool!=Tool::eraser||!session->selected)return false;
    SendMessageW(window,WM_RBUTTONUP,0,0);if(!session||session->selected||session->document)return false;SendMessageW(window,WM_RBUTTONUP,0,0);return !session&&!IsWindow(window);
}
bool lb::capture::coordinate_tests(){
    for(POINT end:std::array<POINT,4>{{{80,50},{-40,50},{80,-70},{-40,-70}}}){auto constrained=constrained_shape({20,10},end,true);if(std::abs(constrained.x-20)!=std::abs(constrained.y-10))return false;auto normal=constrained_shape({20,10},end,false);if(normal.x!=end.x||normal.y!=end.y)return false;}
    WindowPick pick{{-600,150},{-800,0,-200,600},{-800,0,-200,600}};if(!pick_child_rect(pick,{-700,50,-500,250})||pick_child_rect(pick,{-800,0,-200,600})||!pick_child_rect(pick,{-650,100,-550,200})||pick.rectangle.left!=-650||pick_child_rect(pick,{-610,140,-600,150}))return false;
    auto old_dpi=g_app.dpi;scope_exit restore([&]{g_app.dpi=old_dpi;});Surface surface;surface.window=CreateWindowExW(0,L"STATIC",L"hidden annotation coordinates",WS_POPUP,-1400,-900,324,184,nullptr,nullptr,g_app.instance,nullptr);if(!surface.window)return false;scope_exit close([&]{commit_text(surface);DestroyWindow(surface.window);});
    HDC dc=GetDC(surface.window);auto bitmap=CreateCompatibleBitmap(dc,320,180);ReleaseDC(surface.window,dc);if(!bitmap)return false;surface.document=std::make_unique<Document>(bitmap);surface.editing=true;
    for(UINT dpi:{96u,144u,192u,288u})for(bool border:{false,true})for(double zoom:{.5,1.,1.25,2.,4.}){
        g_app.dpi=dpi;surface.border=border;int margin=pin_margin(surface);int width=int(320*zoom)+3,height=int(180*zoom)+1;SetWindowPos(surface.window,nullptr,-1400,-900,width+margin*2,height+margin*2,SWP_NOACTIVATE|SWP_NOZORDER);auto viewport=image_viewport(surface);width=viewport.right-viewport.left;height=viewport.bottom-viewport.top;
        // Exercise every viewport pixel, not just exact multiples of zoom.
        for(int x=0;x<width;++x){auto point=to_document(surface,{viewport.left+x,viewport.top+height/2});if(point.x!=int(std::floor(double(x)*320/width)))return false;}
        for(int y=0;y<height;++y){auto point=to_document(surface,{viewport.left+width/2,viewport.top+y});if(point.y!=int(std::floor(double(y)*180/height)))return false;}
        auto outside_drag=to_document(surface,{viewport.left-width/2,viewport.top+height*2},false);if(outside_drag.x>=0||outside_drag.y<180)return false;
        POINT client{viewport.left+width/3,viewport.top+height/3},screen=client;ClientToScreen(surface.window,&screen);if(screen.x>=0||screen.y>=0)return false;ScreenToClient(surface.window,&screen);auto expected=to_document(surface,client),actual=to_document(surface,screen);if(actual.x!=expected.x||actual.y!=expected.y)return false;
        for(auto tool:{Tool::rectangle,Tool::ellipse,Tool::line,Tool::arrow,Tool::pen,Tool::highlight,Tool::number,Tool::mosaic,Tool::mosaic_brush}){surface.document->items.clear();surface.document->changed();surface.active=-1;surface.tool=tool;surface.style.width=12;mouse_down(surface,client);mouse_move(surface,{client.x+17,client.y+13});mouse_up(surface);if(surface.document->items.size()!=1||surface.document->items[0].points[0].x!=expected.x||surface.document->items[0].points[0].y!=expected.y)return false;}
        surface.tool=Tool::eraser;surface.style.width=20;mouse_down(surface,client);mouse_move(surface,{client.x+17,client.y+13});mouse_up(surface);auto const& erase=surface.document->items.back();if(erase.tool!=Tool::eraser||erase.points[0].x!=expected.x||erase.points[0].y!=expected.y)return false;auto bounds=brush_bounds(surface);if(bounds.right-bounds.left<20*double(width)/320||bounds.bottom-bounds.top<20*double(height)/180)return false;
        screen=client;ClientToScreen(surface.window,&screen);wheel(surface,screen,MAKEWPARAM(0,WHEEL_DELTA));if(surface.style.width!=22||erase.style.width!=20)return false;POINT outside{viewport.left-1,viewport.top};ClientToScreen(surface.window,&outside);wheel(surface,outside,MAKEWPARAM(0,WHEEL_DELTA));if(surface.style.width!=22)return false;
        surface.document->items.clear();surface.document->changed();surface.active=-1;surface.tool=Tool::text;mouse_down(surface,client);if(!surface.edit||surface.document->items[0].points[0].x!=expected.x||surface.document->items[0].points[0].y!=expected.y)return false;SetWindowTextW(surface.edit,L"x");commit_text(surface);
    }
    surface.complete=[](std::vector<Annotation>){};surface.tool=Tool::pen;select_tool(surface,Tool::mosaic);if(surface.tool!=Tool::pen)return false;select_tool(surface,Tool::mosaic_brush);return surface.tool==Tool::pen;
}
bool lb::capture::wheel_tests(){
    Surface surface;surface.window=CreateWindowExW(0,L"STATIC",L"hidden wheel fixture",WS_POPUP,-800,-500,404,304,nullptr,nullptr,g_app.instance,nullptr);if(!surface.window)return false;scope_exit close([&]{commit_text(surface);DestroyWindow(surface.window);});HDC dc=GetDC(surface.window);auto image=CreateCompatibleBitmap(dc,400,300);ReleaseDC(surface.window,dc);if(!image)return false;surface.document=std::make_unique<Document>(image);surface.editing=true;auto spin=[&](POINT point,int delta){point=from_document(surface,point);ClientToScreen(surface.window,&point);wheel(surface,point,MAKEWPARAM(0,delta));};
    Annotation line;line.tool=Tool::line;line.style.width=4;line.points={{30,60},{200,60}};surface.document->items={line};for(int n=0;n<3;++n)spin({100,60},30);if(surface.document->items[0].style.width!=4||surface.document->can_undo())return false;spin({100,60},30);if(surface.document->items[0].style.width!=6||!surface.document->can_undo())return false;spin({100,60},-120);if(surface.document->items[0].style.width!=4)return false;spin({100,60},240);if(surface.document->items[0].style.width!=8)return false;
    line.points={{30,160},{200,160}};surface.document->items.push_back(line);spin({100,60},60);spin({100,160},60);if(surface.document->items[1].style.width!=4)return false;spin({100,160},60);if(surface.document->items[1].style.width!=6)return false;
    surface.wheel_sizes=false;spin({100,160},120);if(surface.document->items[1].style.width!=6)return false;surface.wheel_sizes=true;surface.document->items[1].tool=Tool::mosaic;spin({100,160},120);if(surface.document->items[1].style.width!=6)return false;
    surface.tool=Tool::eraser;surface.style.width=32;spin({100,250},60);if(surface.style.width!=32)return false;spin({100,250},60);if(surface.style.width!=34)return false;
    surface.tool=Tool::text;surface.style.font_size=20;start_text(surface,{40,210});if(!surface.edit)return false;SetWindowTextW(surface.edit,L"typing");SendMessageW(surface.edit,EM_SETSEL,3,3);spin({60,220},120);DWORD a{},b{};SendMessageW(surface.edit,EM_GETSEL,WPARAM(&a),LPARAM(&b));if(!surface.edit||surface.style.font_size!=22||a!=3||b!=3)return false;commit_text(surface);
    auto count=surface.document->items.size(),history=surface.document->history().past.size();start_text(surface,{300,250});surface.style.bold=true;change_style(surface);SetWindowTextW(surface.edit,L"  ");commit_text(surface);if(surface.document->items.size()!=count||surface.document->history().past.size()!=history)return false;
    surface.tool=Tool::rectangle;mouse_down(surface,from_document(surface,{250,30}));mouse_up(surface);if(surface.document->items.size()!=count||surface.document->history().past.size()!=history)return false;
    surface.tool=Tool::text;auto& text=surface.document->items.back();auto before=text.points[0];auto client=from_document(surface,{before.x+2,before.y+2});mouse_down(surface,client);mouse_move(surface,{client.x+1,client.y+1});mouse_up(surface);if(text.points[0].x!=before.x||text.points[0].y!=before.y||surface.document->history().past.size()!=history)return false;
    mouse_down(surface,client);mouse_move(surface,{-300,-300});mouse_up(surface);if(text.points[0].x<0||text.points[0].y<0)return false;
    Annotation marker;marker.tool=Tool::sequence;marker.style.number=9876;surface.document->checkpoint();surface.document->items.push_back(marker);surface.document->finish_edit();if(next_number(*surface.document)!=9876)return false;surface.document->undo();if(next_number(*surface.document)!=1)return false;surface.document->redo();if(next_number(*surface.document)!=9876)return false;
    return stepped_size(9,1,text_sizes)==10&&stepped_size(9,-1,text_sizes)==8&&stepped_size(22,1,number_sizes)==26&&stepped_size(80,3,stroke_sizes)==80;
}
namespace {
std::wstring image_dialog_test_path;
BOOL WINAPI retired_image_dialog(OPENFILENAMEW* dialog){StringCchCopyW(dialog->lpstrFile,dialog->nMaxFile,image_dialog_test_path.c_str());DestroyWindow(dialog->hwndOwner);return TRUE;}
struct ImageSaveFixture {std::wstring choice,directory;int response{IDNO},confirmations{},persists{};BOOL persist_ok{TRUE};bool retire{},correct_target{true};};
ImageSaveFixture* image_save_fixture{};
BOOL WINAPI selected_image_dialog(OPENFILENAMEW* dialog){if(!image_save_fixture)return FALSE;StringCchCopyW(dialog->lpstrFile,dialog->nMaxFile,image_save_fixture->choice.c_str());return TRUE;}
int WINAPI confirm_image_dialog(HWND owner,LPCWSTR text,LPCWSTR,UINT flags){if(!image_save_fixture)return IDNO;auto& fixture=*image_save_fixture;++fixture.confirmations;auto actual=std::filesystem::path(fixture.choice);if(_wcsicmp(actual.extension().c_str(),L".png"))actual.replace_extension(L".png");fixture.correct_target=fixture.correct_target&&wcsstr(text,actual.c_str())&&(flags&MB_DEFBUTTON2);if(fixture.retire)DestroyWindow(owner);return fixture.response;}
BOOL remember_image_directory(){if(!image_save_fixture)return FALSE;auto& fixture=*image_save_fixture;++fixture.persists;fixture.correct_target=fixture.correct_target&&fixture.directory==g_app.settings.capture_directory;return fixture.persist_ok;}
std::vector<BYTE> image_file_bytes(std::filesystem::path const& path){HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return {};scope_exit close([&]{CloseHandle(file);});DWORD size=GetFileSize(file,nullptr);if(size==INVALID_FILE_SIZE||size>1024*1024)return {};std::vector<BYTE> bytes(size);DWORD count{};if(!ReadFile(file,bytes.data(),size,&count,nullptr)||count!=size)return {};return bytes;}
}
bool lb::capture::dialog_tests(std::wstring const& directory){
    if(session||std::any_of(pins.begin(),pins.end(),[](auto const& p){return bool(p);}))return false;
    auto old=image_save_dialog;scope_exit restore([&]{image_save_dialog=old;for(auto& pin:pins)if(pin&&IsWindow(pin->window))DestroyWindow(pin->window);image_dialog_test_path.clear();});
    std::error_code error;std::filesystem::create_directories(directory,error);if(error)return false;image_dialog_test_path=(std::filesystem::path(directory)/L"must-not-save.png").wstring();
    WNDCLASSW cls{};cls.hInstance=g_app.instance;cls.lpfnWndProc=pin_proc;cls.lpszClassName=L"LightBlockDesktopNative.PinDialogFixture";RegisterClassW(&cls);HDC dc=GetDC(nullptr);HBITMAP bitmap=CreateCompatibleBitmap(dc,80,60);ReleaseDC(nullptr,dc);if(!bitmap)return false;
    auto pin=std::make_unique<Pin>();pin->document=std::make_unique<Document>(bitmap);auto pointer=pin.get();pins[0]=std::move(pin);HWND window=CreateWindowExW(0,cls.lpszClassName,L"hidden save dialog fixture",WS_POPUP,0,0,80,60,nullptr,nullptr,g_app.instance,pointer);if(!window){pins[0].reset();return false;}
    image_save_dialog=retired_image_dialog;capture_command(uintptr_t(window),CaptureSave,0,nullptr);
    return !pins[0]&&!IsWindow(window)&&!std::filesystem::exists(image_dialog_test_path,error);
}
bool lb::capture::save_tests(std::wstring const& directory){
    if(session||std::any_of(pins.begin(),pins.end(),[](auto const& pin){return bool(pin);}))return false;
    auto previous_dialog=image_save_dialog;auto previous_prompt=image_overwrite_dialog;auto previous_save=image_save_config;auto previous_directory=std::wstring(g_app.settings.capture_directory);
    scope_exit restore([&]{image_save_dialog=previous_dialog;image_overwrite_dialog=previous_prompt;image_save_config=previous_save;image_save_fixture=nullptr;StringCchCopyW(g_app.settings.capture_directory,LB_PATH,previous_directory.c_str());for(auto& pin:pins)if(pin&&IsWindow(pin->window))DestroyWindow(pin->window);});
    std::error_code error;auto folder=std::filesystem::absolute(directory,error);if(error)return false;std::filesystem::create_directories(folder,error);if(error)return false;
    ImageSaveFixture fixture;fixture.directory=folder.wstring();image_save_fixture=&fixture;image_save_dialog=selected_image_dialog;image_overwrite_dialog=confirm_image_dialog;image_save_config=remember_image_directory;
    WNDCLASSW cls{};cls.hInstance=g_app.instance;cls.lpfnWndProc=pin_proc;cls.lpszClassName=L"LightBlockDesktopNative.PngSaveFixture";RegisterClassW(&cls);HDC dc=GetDC(nullptr);HBITMAP bitmap=CreateCompatibleBitmap(dc,80,60);ReleaseDC(nullptr,dc);if(!bitmap)return false;auto pin=std::make_unique<Pin>();pin->document=std::make_unique<Document>(bitmap);auto pointer=pin.get();pins[0]=std::move(pin);HWND window=CreateWindowExW(0,cls.lpszClassName,L"hidden PNG save fixture",WS_POPUP,-900,-500,84,64,nullptr,nullptr,g_app.instance,pointer);if(!window){pins[0].reset();return false;}
    auto existing=folder/L"existing.png";if(!write_utf8_atomic(existing.c_str(),L"preserve existing content"))return false;auto original=image_file_bytes(existing);if(original.empty())return false;fixture.choice=(folder/L"existing.jpg").wstring();
    if(save_image(*pointer)||fixture.confirmations!=1||fixture.persists||image_file_bytes(existing)!=original||GetFileAttributesW(fixture.choice.c_str())!=INVALID_FILE_ATTRIBUTES||previous_directory!=g_app.settings.capture_directory)return false;
    fixture.response=IDYES;if(!save_image(*pointer)||fixture.confirmations!=2||fixture.persists!=1||!fixture.correct_target||fixture.directory!=g_app.settings.capture_directory)return false;auto png=image_file_bytes(existing);constexpr BYTE signature[]={137,80,78,71,13,10,26,10};if(png.size()<sizeof signature||!std::equal(std::begin(signature),std::end(signature),png.begin()))return false;
    StringCchCopyW(g_app.settings.capture_directory,LB_PATH,previous_directory.c_str());fixture.persist_ok=FALSE;fixture.choice=(folder/(L"fresh-"+std::to_wstring(GetTickCount64())+L".JPG")).wstring();auto expected=std::filesystem::path(fixture.choice);expected.replace_extension(L".png");if(!save_image(*pointer)||fixture.confirmations!=2||fixture.persists!=2||previous_directory!=g_app.settings.capture_directory||GetFileAttributesW(fixture.choice.c_str())!=INVALID_FILE_ATTRIBUTES||image_file_bytes(expected).empty())return false;
    // A modal confirmation may destroy its owner. No captured Surface is used
    // afterwards, and no file is overwritten even when the dialog returns Yes.
    fixture.choice=(folder/L"existing.jpg").wstring();fixture.retire=true;if(save_image(*pointer)||IsWindow(window)||pins[0]||fixture.confirmations!=3||fixture.persists!=2||image_file_bytes(existing)!=png)return false;
    return fixture.correct_target&&previous_directory==g_app.settings.capture_directory;
}
bool capture_pin_tests(){
    if(session||std::any_of(pins.begin(),pins.end(),[](auto const& pin){return bool(pin);}))return false;
    if(!PinGlow::tests())return false;DWORD clipboard_before=GetClipboardSequenceNumber();
    WNDCLASSW cls{};cls.hInstance=g_app.instance;cls.lpfnWndProc=pin_proc;cls.lpszClassName=L"LightBlockDesktopNative.PinParityFixture";RegisterClassW(&cls);
    BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),400,-240,1,32,BI_RGB,0,0,0,0,0};void* pixels{};HBITMAP bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0);if(!bitmap)return false;
    for(int y=0;y<240;++y)for(int x=0;x<400;++x)static_cast<DWORD*>(pixels)[size_t(y)*400+x]=(DWORD(x%256)<<16)|(DWORD(y%256)<<8)|73;
    auto pin=std::make_unique<Pin>();pin->document=std::make_unique<Document>(bitmap);auto surface=pin.get();pins[0]=std::move(pin);
    HWND window=CreateWindowExW(0,cls.lpszClassName,L"hidden pin parity fixture",WS_POPUP,-340,-210,400+2*px(5),240+2*px(5),nullptr,nullptr,g_app.instance,surface);if(!window){pins[0].reset();return false;}
    scope_exit close([&]{if(IsWindow(window))DestroyWindow(window);});
    // Like create_pin, use the created HWND's actual monitor DPI. Unit startup
    // deliberately leaves the unrelated settings-window DPI uninitialized.
    int margin=pin_margin(*surface);SetWindowPos(window,nullptr,-340,-210,400+2*margin,240+2*margin,SWP_NOZORDER|SWP_NOACTIVATE);
    auto content=[&]{RECT result=image_viewport(*surface);MapWindowPoints(window,nullptr,reinterpret_cast<POINT*>(&result),2);return result;};
    RECT original=content();if(original.right-original.left!=400||original.bottom-original.top!=240)return false;for(int n=0;n<8;++n){pin_toggle_border(*surface);auto current=content();if(!EqualRect(&original,&current)||surface->document->width!=400||surface->document->height!=240)return false;}
    pin_toggle_border(*surface);RECT client{};GetClientRect(window,&client);auto viewport=image_viewport(*surface);if(!EqualRect(&client,&viewport))return false;
    SendMessageW(window,WM_MOUSEWHEEL,MAKEWPARAM(0,short(-WHEEL_DELTA)),0);if(std::abs(surface->opacity-.95)>.0001)return false;
    for(int n=0;n<40;++n)SendMessageW(window,WM_MOUSEWHEEL,MAKEWPARAM(0,short(-WHEEL_DELTA)),0);
    BYTE alpha{};DWORD flags{};if(surface->opacity!=.25||!GetLayeredWindowAttributes(window,nullptr,&alpha,&flags)||alpha!=64||!(flags&LWA_ALPHA))return false;
    if(!pin_opacity(*surface,1.)||surface->opacity!=1.)return false;
    MONITORINFO monitor{sizeof monitor};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTOPRIMARY),&monitor);
    SetWindowPos(window,nullptr,monitor.rcWork.left+80,monitor.rcWork.top+80,400,240,SWP_NOZORDER|SWP_NOACTIVATE);
    auto before=content();POINT anchor{before.left+120,before.top+90};
    SendMessageW(window,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,WHEEL_DELTA),MAKELPARAM(anchor.x,anchor.y));auto after=content();
    if(after.right-after.left<=before.right-before.left||surface->opacity!=1.||std::abs((anchor.x-after.left)*400./(after.right-after.left)-120)>1.5||std::abs((anchor.y-after.top)*240./(after.bottom-after.top)-90)>1.5)return false;
    for(int n=0;n<30;++n)pin_zoom(*surface,anchor,WHEEL_DELTA);RECT outer{};GetWindowRect(window,&outer);if(outer.left<monitor.rcWork.left||outer.top<monitor.rcWork.top||outer.right>monitor.rcWork.right||outer.bottom>monitor.rcWork.bottom)return false;
    for(int n=0;n<30;++n)pin_zoom(*surface,anchor,-WHEEL_DELTA);auto small=content();if(small.right-small.left<100||small.bottom-small.top<70)return false;
    auto menu=pin_menu(*surface);if(!menu)return false;wchar_t label[80]{};GetMenuStringW(menu,4,label,80,MF_BYCOMMAND);bool menu_ok=GetMenuItemCount(menu)==8&&!wcscmp(label,tr(L"显示微光边框",L"Show glow border"))&&GetMenuState(menu,6,MF_BYCOMMAND)!=UINT(-1);DestroyMenu(menu);if(!menu_ok)return false;
    SendMessageW(window,WM_NCRBUTTONUP,HTCAPTION,MAKELPARAM(anchor.x,anchor.y));MSG message{};if(!PeekMessageW(&message,window,WM_CONTEXTMENU,WM_CONTEXTMENU,PM_REMOVE)||GET_X_LPARAM(message.lParam)!=anchor.x||GET_Y_LPARAM(message.lParam)!=anchor.y)return false;
    RECT unchanged{};GetWindowRect(window,&unchanged);show_toolbar(*surface);RECT editing{};GetWindowRect(window,&editing);if(!surface->editing||!EqualRect(&unchanged,&editing))return false;
    // Cropping is only a source view and a history entry. It neither flattens
    // annotations nor allocates an original-image copy for each undo step.
    pin_crop_begin(*surface);surface->crop_selection={40,30,280,180};if(!pin_crop_apply(*surface)||surface->cropping||!surface->editing)return false;
    auto clipped=source_view(*surface);if(clipped.left!=40||clipped.top!=30||clipped.right!=280||clipped.bottom!=180||surface->document->width!=400)return false;
    viewport=image_viewport(*surface);auto origin=to_document(*surface,{viewport.left,viewport.top});if(origin.x!=40||origin.y!=30)return false;
    unique_gdi<HBITMAP> exported;auto rendered=render_result(*surface,exported);BITMAP metadata{};if(!GetObjectW(rendered,sizeof metadata,&metadata)||metadata.bmWidth!=240||metadata.bmHeight!=150)return false;
    HDC dc=CreateCompatibleDC(nullptr);auto selected=SelectObject(dc,rendered);auto actual=GetPixel(dc,3,4);SelectObject(dc,selected);DeleteDC(dc);if(actual!=RGB(43,34,73))return false;
    capture_command(uintptr_t(window),CaptureUndo,0,nullptr);auto full=source_view(*surface);if(full.left||full.top||full.right!=400||full.bottom!=240)return false;
    capture_command(uintptr_t(window),CaptureRedo,0,nullptr);auto redone=source_view(*surface);if(!EqualRect(&clipped,&redone))return false;
    pin_crop_begin(*surface);pin_crop_down(*surface,{viewport.left+2,viewport.top+2});pin_crop_move(*surface,{viewport.right-3,viewport.bottom-3});pin_crop_cancel(*surface);auto cancelled=source_view(*surface);if(!EqualRect(&clipped,&cancelled)||surface->cropping)return false;
    close_toolbar(*surface);surface->resizable=true;surface->border=true;RECT placement{};GetWindowRect(window,&placement);if(pin_resize_hit(*surface,{placement.right-1,placement.bottom-1})!=HTBOTTOMRIGHT)return false;surface->border=false;if(pin_resize_hit(*surface,{placement.right-1,placement.bottom-1})!=HTCAPTION)return false;
    RECT sizing{10,20,414,284};pin_sizing(*surface,WMSZ_BOTTOMRIGHT,sizing);if(std::abs(double(sizing.right-sizing.left)/(sizing.bottom-sizing.top)-240./150)>.01)return false;
    // A long pin scrolls a viewport while copy/save retain the whole source.
    surface->document->items.clear();surface->document->changed();surface->long_image=true;surface->wheel_sizes=false;
    SetWindowPos(window,nullptr,monitor.rcWork.left+40,monitor.rcWork.top+40,400,100,SWP_NOZORDER|SWP_NOACTIVATE);
    SendMessageW(window,WM_MOUSEWHEEL,MAKEWPARAM(0,short(-WHEEL_DELTA)),0);
    auto visible=display_view(*surface);if(visible.top<=0||visible.bottom-visible.top>101||surface->opacity!=1.)return false;
    auto top_point=to_document(*surface,{0,0});if(top_point.y!=visible.top)return false;
    exported.reset();rendered=render_result(*surface,exported);if(!GetObjectW(rendered,sizeof metadata,&metadata)||metadata.bmWidth!=400||metadata.bmHeight!=240)return false;
    SendMessageW(window,WM_KEYDOWN,VK_END,0);visible=display_view(*surface);if(visible.bottom!=240)return false;
    SendMessageW(window,WM_KEYDOWN,VK_HOME,0);if(display_view(*surface).top)return false;
    SendMessageW(window,WM_MOUSEWHEEL,MAKEWPARAM(MK_SHIFT,short(-WHEEL_DELTA)),0);if(std::abs(surface->opacity-.95)>.001)return false;
    pin_opacity(*surface,1.);for(int n=0;n<30;++n)pin_zoom(*surface,anchor,WHEEL_DELTA);
    GetWindowRect(window,&outer);if(outer.right>monitor.rcWork.right||outer.bottom>monitor.rcWork.bottom)return false;
    RECT tall_size{10,20,210,1020};pin_sizing(*surface,WMSZ_BOTTOMRIGHT,tall_size);if(tall_size.bottom-tall_size.top!=120)return false;
    RECT left_size{100,20,350,170};pin_sizing(*surface,WMSZ_TOPLEFT,left_size);if(left_size.right!=350||left_size.bottom!=170)return false;
    surface->long_image=false;surface->wheel_sizes=true;
    auto previous_open=open_image_clipboard;open_image_clipboard=+[](HWND)->BOOL{return FALSE;};SendMessageW(window,WM_NCLBUTTONDBLCLK,HTCAPTION,0);open_image_clipboard=previous_open;if(!IsWindow(window)||!pins[0])return false;
    show_toolbar(*surface);
    // Losing capture mid-stroke must finalize the gesture once. Subsequent
    // hover moves cannot extend a pen path or leave the resize mode latched.
    surface->tool=Tool::pen;viewport=image_viewport(*surface);
    mouse_down(*surface,{viewport.left+8,viewport.top+8});
    mouse_move(*surface,{viewport.left+15,viewport.top+13});
    if(!surface->drawing||surface->document->items.empty())return false;
    auto points_before=surface->document->items.back().points;
    surface->resize_drag=WMSZ_BOTTOMRIGHT;
    SendMessageW(window,WM_CAPTURECHANGED,0,0);
    if(GetCapture()==window)ReleaseCapture();
    mouse_move(*surface,{viewport.left+25,viewport.top+18});
    auto const& points_after=surface->document->items.back().points;
    if(surface->drawing||surface->moving||surface->resize_drag||points_before.size()!=points_after.size()||!std::equal(points_before.begin(),points_before.end(),points_after.begin(),[](POINT a,POINT b){return a.x==b.x&&a.y==b.y;}))return false;
    SendMessageW(window,WM_KEYDOWN,VK_ESCAPE,0);if(!IsWindow(window)||surface->editing)return false;
    SendMessageW(window,WM_KEYDOWN,VK_ESCAPE,0);return !IsWindow(window)&&!pins[0]&&GetClipboardSequenceNumber()==clipboard_before;
}
