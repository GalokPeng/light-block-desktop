#pragma once
namespace {
int option_px(int value) { return MulDiv(value,int(g_app.dpi),96); }
COLORREF option_bg() { return g_app.dark?RGB(18,23,32):RGB(246,247,250); }
COLORREF option_fg() { return g_app.dark?RGB(235,240,249):RGB(29,38,49); }
bool section_available(int group) { return s_options.family==1?group>=1&&group<=3:s_options.family==4?group==4:group==0||group==5; }
void section_caption(int group) {
    wchar_t const* zh[]={L"播放与系统",L"桌面收纳",L"原生任务栏",L"鼠标与指针",L"截屏与录屏",L"工具偏好"};
    wchar_t const* en[]={L"Playback and system",L"Desktop organizer",L"Native taskbar",L"Pointer",L"Capture and recording",L"Tool preferences"};
    auto caption=std::wstring(s_options.expanded[group]?L"⌄  ":L"›  ")+tr(zh[group],en[group]);SetWindowTextW(s_options.sections[group],caption.c_str());
}
void place_options() {
    if(!s_options.window||!s_options.viewport) return;RECT client{};GetClientRect(s_options.window,&client);
    int width=client.right,height=std::max(option_px(80),int(client.bottom)-option_px(70));
    MoveWindow(s_options.viewport,0,0,width,height,TRUE);
    MoveWindow(s_options.save,std::max(option_px(10),width-option_px(190)),height+option_px(17),option_px(180),option_px(36),TRUE);
    MoveWindow(s_options.status,option_px(18),height+option_px(16),std::max(option_px(80),width-option_px(230)),option_px(44),TRUE);
    for(UINT n=0;n<ARRAYSIZE(options);++n) { ShowWindow(s_options.controls[n],SW_HIDE);ShowWindow(s_options.labels[n],SW_HIDE); }
    ShowWindow(s_options.startup,SW_HIDE);ShowWindow(s_options.night,SW_HIDE);
    ShowWindow(s_options.start,s_options.family==4?SW_SHOW:SW_HIDE);
    int y=option_px(12)-s_options.scroll;
    if(s_options.family==4) { MoveWindow(s_options.start,option_px(16),y,option_px(170),option_px(38),TRUE);y+=option_px(58); }
    int label_width=std::max(option_px(160),int(width*.51)),control_x=label_width+option_px(22),control_width=std::max(option_px(100),width-control_x-option_px(24));
    for(int group=0;group<6;++group) {
        bool visible=section_available(group);ShowWindow(s_options.sections[group],visible?SW_SHOW:SW_HIDE);if(!visible) continue;
        section_caption(group);MoveWindow(s_options.sections[group],option_px(12),y,width-option_px(24),option_px(42),TRUE);y+=option_px(54);
        if(!s_options.expanded[group]) continue;
        for(UINT n=0;n<ARRAYSIZE(options);++n) if(options[n].page==group) {
            auto kind=options[n].kind;MoveWindow(s_options.labels[n],option_px(20),y+option_px(5),label_width-option_px(16),option_px(42),TRUE);
            MoveWindow(s_options.controls[n],control_x,y,control_width,kind==COMBO?option_px(250):option_px(34),TRUE);ShowWindow(s_options.labels[n],SW_SHOW);ShowWindow(s_options.controls[n],SW_SHOW);y+=option_px(57);
        }
        if(group==0) {
            MoveWindow(s_options.startup,option_px(20),y,width-option_px(40),option_px(32),TRUE);ShowWindow(s_options.startup,SW_SHOW);y+=option_px(47);
            MoveWindow(s_options.night,control_x,y,control_width,option_px(34),TRUE);ShowWindow(s_options.night,SW_SHOW);y+=option_px(54);
        }
        y+=option_px(8);
    }
    s_options.content=y+s_options.scroll+option_px(12);
    SCROLLINFO scroll{sizeof scroll,SIF_RANGE|SIF_PAGE|SIF_POS};scroll.nMin=0;scroll.nMax=std::max(0,s_options.content-1);scroll.nPage=UINT(height);scroll.nPos=s_options.scroll;
    SetScrollInfo(s_options.viewport,SB_VERT,&scroll,TRUE);InvalidateRect(s_options.viewport,nullptr,FALSE);
}
void scroll_options(int amount) {
    RECT client{};GetClientRect(s_options.viewport,&client);s_options.scroll=std::clamp(s_options.scroll+amount,0,std::max(0,s_options.content-int(client.bottom)));place_options();
}
LRESULT CALLBACK option_control(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR) {
    if(message==WM_MOUSEWHEEL) { if(!SendMessageW(window,CB_GETDROPPEDSTATE,0,0)) { scroll_options(-GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*option_px(65));return 0; } }
    if(message==WM_NCDESTROY) RemoveWindowSubclass(window,option_control,id);return DefSubclassProc(window,message,wp,lp);
}
LRESULT CALLBACK viewport_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    switch(message) {
    case WM_MOUSEWHEEL:scroll_options(-GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*option_px(65));return 0;
    case WM_VSCROLL: { int amount=0;switch(LOWORD(wp)) { case SB_LINEUP:amount=-option_px(36);break;case SB_LINEDOWN:amount=option_px(36);break;case SB_PAGEUP:amount=-option_px(240);break;case SB_PAGEDOWN:amount=option_px(240);break;case SB_THUMBTRACK: { SCROLLINFO info{sizeof info,SIF_TRACKPOS};GetScrollInfo(window,SB_VERT,&info);amount=info.nTrackPos-s_options.scroll;break; } }scroll_options(amount);return 0; }
    case WM_COMMAND:case WM_DRAWITEM:case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:return SendMessageW(GetParent(window),message,wp,lp);
    case WM_ERASEBKGND: { RECT rect{};GetClientRect(window,&rect);FillRect(HDC(wp),&rect,s_options.brush);return 1; }
    }return DefWindowProcW(window,message,wp,lp);
}
void refresh_options() {
    if(!s_options.window) return;if(s_options.brush) DeleteObject(s_options.brush);s_options.brush=CreateSolidBrush(option_bg());
    for(UINT n=0;n<ARRAYSIZE(options);++n) {
        auto const& field=options[n];SetWindowTextW(s_options.labels[n],tr(field.zh,field.en));
        if(field.kind==COMBO) { int selected=int(SendMessageW(s_options.controls[n],CB_GETCURSEL,0,0));if(field.offset==offsetof(Settings,monitor)){int actual=int(SendMessageW(s_options.controls[n],CB_GETITEMDATA,selected,0));populate_display_combo(s_options.controls[n],actual);}else{SendMessageW(s_options.controls[n],CB_RESETCONTENT,0,0);choices(s_options.controls[n],tr(field.choices_zh,field.choices_en));SendMessageW(s_options.controls[n],CB_SETCURSEL,selected,0);} }
    }
    SetWindowTextW(s_options.save,tr(L"保存并应用",L"Save and apply"));SetWindowTextW(s_options.start,tr(L"开始截屏",L"Start capture"));
    SetWindowTextW(s_options.startup,tr(L"开机启动",L"Start with Windows"));SetWindowTextW(s_options.night,tr(L"Windows 护眼设置",L"Windows night light"));
    place_options();RedrawWindow(s_options.window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_ERASE);
}
LRESULT CALLBACK options_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    switch(message) {
    case WM_CREATE: {
        s_options.window=window;s_options.brush=CreateSolidBrush(option_bg());
        WNDCLASSW viewport{};viewport.lpfnWndProc=viewport_proc;viewport.lpszClassName=L"LightBlockDesktopNative.SettingsViewport";viewport.hInstance=g_app.instance;viewport.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&viewport);
        s_options.viewport=CreateWindowExW(WS_EX_CONTROLPARENT,viewport.lpszClassName,L"",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_VSCROLL,0,0,1,1,window,nullptr,g_app.instance,nullptr);
        for(int group=0;group<6;++group) { s_options.sections[group]=CreateWindowExW(0,L"BUTTON",L"",WS_CHILD|BS_OWNERDRAW|WS_TABSTOP,0,0,1,1,s_options.viewport,reinterpret_cast<HMENU>(INT_PTR(200+group)),g_app.instance,nullptr);SendMessageW(s_options.sections[group],WM_SETFONT,WPARAM(g_app.font),TRUE); }
        for(UINT n=0;n<ARRAYSIZE(options);++n) {
            auto const& field=options[n];int value=get_value(&field);wchar_t const* class_name=L"BUTTON";DWORD style=WS_CHILD|WS_TABSTOP;
            if(field.kind==CHECK) style|=BS_AUTOCHECKBOX;else if(field.kind==COMBO) { class_name=L"COMBOBOX";style|=CBS_DROPDOWNLIST|WS_VSCROLL; }else if(field.kind==SLIDER) { class_name=TRACKBAR_CLASSW;style|=TBS_AUTOTICKS; }else if(field.kind==HOTKEY) class_name=HOTKEY_CLASSW;else style|=BS_OWNERDRAW;
            s_options.labels[n]=CreateWindowExW(0,L"STATIC",tr(field.zh,field.en),WS_CHILD,0,0,1,1,s_options.viewport,nullptr,g_app.instance,nullptr);
            HWND control=CreateWindowExW(0,class_name,field.kind==COLOR?tr(L"调色板…",L"Choose color…"):L"",style,0,0,1,option_px(250),s_options.viewport,reinterpret_cast<HMENU>(UINT_PTR(100+n)),g_app.instance,nullptr);s_options.controls[n]=control;
            SendMessageW(control,WM_SETFONT,WPARAM(g_app.font),0);SendMessageW(s_options.labels[n],WM_SETFONT,WPARAM(g_app.font),0);SetWindowSubclass(control,option_control,1,0);
            if(field.kind==CHECK) SendMessageW(control,BM_SETCHECK,value?BST_CHECKED:BST_UNCHECKED,0);
            else if(field.kind==COMBO) {
                if(field.offset==offsetof(Settings,monitor)){populate_display_combo(control,value);continue;}choices(control,tr(field.choices_zh,field.choices_en));
                if(field.offset==offsetof(Settings,record_resolution)) for(int k=0;k<4;++k) if(resolutions[k]==value) { value=k;break; }
                if(field.offset==offsetof(Settings,record_fps)) for(int k=0;k<6;++k) if(rates[k]==value) { value=k;break; }
                SendMessageW(control,CB_SETCURSEL,value,0);
            }else if(field.kind==SLIDER) { SendMessageW(control,TBM_SETRANGE,TRUE,MAKELPARAM(field.low,field.high));SendMessageW(control,TBM_SETPOS,TRUE,value); }
            else if(field.kind==HOTKEY) { int mods=g_app.settings.capture_modifiers;SendMessageW(control,HKM_SETHOTKEY,MAKEWORD(value,((mods&MOD_CONTROL)?HOTKEYF_CONTROL:0)|((mods&MOD_ALT)?HOTKEYF_ALT:0)|((mods&MOD_SHIFT)?HOTKEYF_SHIFT:0)),0); }
        }
        s_options.startup=CreateWindowExW(0,L"BUTTON",tr(L"开机启动",L"Start with Windows"),WS_CHILD|BS_OWNERDRAW|WS_TABSTOP,0,0,1,1,s_options.viewport,reinterpret_cast<HMENU>(10),g_app.instance,nullptr);s_options.startup_initial=s_options.startup_checked=startup_enabled()!=FALSE;
        s_options.night=CreateWindowExW(0,L"BUTTON",tr(L"Windows 护眼设置",L"Windows night light"),WS_CHILD|BS_OWNERDRAW|WS_TABSTOP,0,0,1,1,s_options.viewport,reinterpret_cast<HMENU>(11),g_app.instance,nullptr);EnableWindow(s_options.night,night_light_supported());
        s_options.save=CreateWindowExW(0,L"BUTTON",tr(L"保存并应用",L"Save and apply"),WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,window,reinterpret_cast<HMENU>(12),g_app.instance,nullptr);
        s_options.start=CreateWindowExW(0,L"BUTTON",tr(L"开始截屏",L"Start capture"),WS_CHILD|BS_OWNERDRAW|WS_TABSTOP,0,0,1,1,s_options.viewport,reinterpret_cast<HMENU>(13),g_app.instance,nullptr);
        s_options.status=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE,0,0,1,1,window,nullptr,g_app.instance,nullptr);
        for(HWND child:{s_options.startup,s_options.night,s_options.save,s_options.start,s_options.status}) SendMessageW(child,WM_SETFONT,WPARAM(g_app.font),TRUE);
        return 0;
    }
    case WM_SIZE:place_options();return 0;
    case WM_DRAWITEM: {
        auto item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);wchar_t text[240];GetWindowTextW(item->hwndItem,text,240);RECT bounds=item->rcItem;bool primary=item->CtlID==12||item->CtlID==13;COLORREF background=primary?RGB(112,217,185):g_app.dark?RGB(26,33,45):RGB(255,255,255);rounded(item->hDC,bounds,option_px(12),background,g_app.dark?RGB(48,59,76):RGB(223,229,237));
        if(item->CtlID==10) { bool checked=s_options.startup_checked;draw_text(item->hDC,checked?L"☑":L"☐",RECT{bounds.left+option_px(8),bounds.top,bounds.left+option_px(34),bounds.bottom},g_app.font,option_fg(),DT_VCENTER|DT_SINGLELINE);bounds.left+=option_px(40); }
        draw_text(item->hDC,text,bounds,g_app.font,primary?RGB(13,42,36):option_fg(),DT_CENTER|DT_VCENTER|DT_SINGLELINE);return TRUE;
    }
    case WM_COMMAND: {
        int id=LOWORD(wp);if(id>=200&&id<206) { int group=id-200;s_options.expanded[group]=!s_options.expanded[group];s_options.scroll=0;place_options(); }
        else if(id==10) { bool checked=s_options.startup_checked;s_options.startup_checked=!checked;InvalidateRect(s_options.startup,nullptr,TRUE); }
        else if(id==11) ShellExecuteW(window,L"open",L"ms-settings:nightlight",nullptr,nullptr,SW_SHOWNORMAL);
        else if(id==12||id==IDOK) { if(apply()) SetWindowTextW(s_options.status,tr(L"设置已保存",L"Settings saved")); }
        else if(id==13) PostMessageW(g_app.window,WM_COMMAND,701,0);
        else if(id>=100&&id<100+int(ARRAYSIZE(options))) {
            auto const& field=options[id-100];if(field.kind==COLOR) { static COLORREF colors[16];CHOOSECOLORW choose{sizeof choose};choose.hwndOwner=g_app.window;choose.rgbResult=COLORREF(get_value(&field));choose.lpCustColors=colors;choose.Flags=CC_FULLOPEN|CC_RGBINIT;if(ChooseColorW(&choose)) { set_value(&field,int(choose.rgbResult));wchar_t label[30];StringCchPrintfW(label,30,L"#%02X%02X%02X",GetRValue(choose.rgbResult),GetGValue(choose.rgbResult),GetBValue(choose.rgbResult));SetWindowTextW(s_options.controls[id-100],label); } }
        }return 0;
    }
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:SetTextColor(HDC(wp),option_fg());SetBkColor(HDC(wp),option_bg());return LRESULT(s_options.brush);
    case WM_ERASEBKGND: { RECT rect{};GetClientRect(window,&rect);FillRect(HDC(wp),&rect,s_options.brush);return 1; }
    case WM_MOUSEWHEEL:scroll_options(-GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*option_px(65));return 0;
    case WM_APP+120:refresh_options();return 0;
    case WM_DESTROY:DeleteObject(s_options.brush);s_options.brush=nullptr;s_options.window=nullptr;s_options.viewport=nullptr;return 0;
    }return DefWindowProcW(window,message,wp,lp);
}
}
void settings_layout() {
    if(!s_options.window) return;bool visible=g_app.page>=3&&g_app.page<=5;ShowWindow(s_options.window,visible?SW_SHOW:SW_HIDE);if(!visible) return;
    RECT bounds{};GetClientRect(g_app.window,&bounds);MoveWindow(s_options.window,option_px(236),option_px(137),std::max(option_px(340),int(bounds.right)-option_px(270)),std::max(option_px(180),int(bounds.bottom)-option_px(159)),TRUE);
}
void settings_mount(int page) {
    s_options.page=page;s_options.family=page>=1&&page<=3?1:page==4?4:0;s_options.scroll=0;std::fill(std::begin(s_options.expanded),std::end(s_options.expanded),false);s_options.expanded[std::clamp(page,0,5)]=true;
    if(!s_options.window) { s_options.baseline=s_options.draft=g_app.settings;WNDCLASSW cls{};cls.lpfnWndProc=options_proc;cls.hInstance=g_app.instance;cls.lpszClassName=L"LightBlockDesktopNative.Options";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&cls);CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,L"",WS_CHILD|WS_CLIPCHILDREN,0,0,1,1,g_app.window,nullptr,g_app.instance,nullptr); }
    settings_layout();place_options();
}
void settings_refresh() { if(s_options.window) SendMessageW(s_options.window,WM_APP+120,0,0); }
void settings_show(int page) { app_open_settings(page); }
void settings_close() { if(s_options.window) DestroyWindow(s_options.window); }
bool settings_dialog_message(MSG *message) { return s_options.window&&(message->hwnd==s_options.window||IsChild(s_options.window,message->hwnd))&&IsDialogMessageW(s_options.window,message); }
void settings_sync_clipboard() { if(!s_options.window) return;s_options.draft.clipboard_enabled=g_app.settings.clipboard_enabled;for(UINT n=0;n<ARRAYSIZE(options);++n) if(options[n].offset==offsetof(Settings,clipboard_enabled)) SendMessageW(s_options.controls[n],BM_SETCHECK,g_app.settings.clipboard_enabled?BST_CHECKED:BST_UNCHECKED,0); }
bool settings_navigation_test() {
    auto page=g_app.page;bool success=true;
    for(int group:{1,2,3,0,4}) { g_app.page=group>=1&&group<=3?3:group==4?5:4;settings_mount(group);success=success&&s_options.window&&GetParent(s_options.window)==g_app.window&&(GetWindowLongPtrW(s_options.window,GWL_STYLE)&WS_CHILD)&&!(GetWindowLongPtrW(s_options.window,GWL_STYLE)&WS_CAPTION); }
    g_app.page=page;settings_close();return success;
}
