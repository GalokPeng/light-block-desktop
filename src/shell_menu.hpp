#pragma once
#include "lb.h"

namespace lb::shell_menu {
// Let Explorer supply file verbs, including installed extensions. Custom shelf
// commands live below first_id and must never be mistaken for Shell offsets.
class Menu {
    winrt::com_ptr<IContextMenu> menu_;
    winrt::com_ptr<IContextMenu2> menu2_;
    winrt::com_ptr<IContextMenu3> menu3_;
    HWND owner_{};
    UINT count_{};
    static LRESULT CALLBACK messages(HWND hwnd,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data) {
        auto self=reinterpret_cast<Menu*>(data);
        if(message==WM_NCDESTROY){RemoveWindowSubclass(hwnd,messages,id);self->owner_=nullptr;}
        if(message==WM_INITMENUPOPUP||message==WM_DRAWITEM||message==WM_MEASUREITEM||message==WM_MENUCHAR){
            LRESULT result{};
            if(self->menu3_&&self->menu3_->HandleMenuMsg2(message,wp,lp,&result)==S_OK)return result;
            if(!self->menu3_&&self->menu2_&&message!=WM_MENUCHAR&&self->menu2_->HandleMenuMsg(message,wp,lp)==S_OK)return 0;
        }
        return DefSubclassProc(hwnd,message,wp,lp);
    }
public:
    static constexpr UINT first_id=0x4000,last_id=0x7fff;
    Menu()=default;Menu(Menu const&)=delete;Menu& operator=(Menu const&)=delete;
    ~Menu(){if(owner_)RemoveWindowSubclass(owner_,messages,UINT_PTR(this));}
    bool append(HWND owner,HMENU target,std::wstring const& path,bool extended=false){
        if(path.empty())return false;
        PIDLIST_ABSOLUTE item{};if(FAILED(SHParseDisplayName(path.c_str(),nullptr,&item,0,nullptr)))return false;
        scope_exit free_item([&]{CoTaskMemFree(item);});winrt::com_ptr<IShellFolder> folder;PCUITEMID_CHILD child{};
        if(FAILED(SHBindToParent(item,__uuidof(IShellFolder),folder.put_void(),&child)))return false;
        if(FAILED(folder->GetUIObjectOf(owner,1,&child,__uuidof(IContextMenu),nullptr,menu_.put_void())))return false;
        auto result=menu_->QueryContextMenu(target,0,first_id,last_id,CMF_NORMAL|CMF_CANRENAME|(extended?CMF_EXTENDEDVERBS:0));
        if(FAILED(result)){menu_=nullptr;return false;}count_=HRESULT_CODE(result);
        menu2_=menu_.try_as<IContextMenu2>();menu3_=menu_.try_as<IContextMenu3>();owner_=owner;
        if(!SetWindowSubclass(owner,messages,UINT_PTR(this),DWORD_PTR(this)))owner_=nullptr;
        return count_!=0;
    }
    bool owns(UINT command)const{return menu_&&command>=first_id&&command<first_id+count_;}
    bool invoke(UINT command,POINT point){
        if(!owns(command)||!owner_||!IsWindow(owner_))return false;
        CMINVOKECOMMANDINFOEX info{sizeof info};info.fMask=CMIC_MASK_UNICODE|CMIC_MASK_PTINVOKE;
        if(GetKeyState(VK_SHIFT)<0)info.fMask|=CMIC_MASK_SHIFT_DOWN;
        if(GetKeyState(VK_CONTROL)<0)info.fMask|=CMIC_MASK_CONTROL_DOWN;
        info.hwnd=owner_;info.lpVerb=MAKEINTRESOURCEA(command-first_id);info.lpVerbW=MAKEINTRESOURCEW(command-first_id);info.nShow=SW_SHOWNORMAL;info.ptInvoke=point;
        return SUCCEEDED(menu_->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(&info)));
    }
    bool is_verb(UINT command,char const* name)const{char verb[128]{};return owns(command)&&SUCCEEDED(menu_->GetCommandString(command-first_id,GCS_VERBA,nullptr,verb,sizeof verb))&&!_stricmp(verb,name);}
    bool has_verb(char const* name)const{
        for(UINT offset=0;menu_&&offset<count_;++offset){char verb[128]{};if(SUCCEEDED(menu_->GetCommandString(offset,GCS_VERBA,nullptr,verb,sizeof verb))&&!_stricmp(verb,name))return true;}return false;
    }
};
inline bool tests(std::filesystem::path const& directory){
    std::error_code error;std::filesystem::create_directories(directory,error);if(error)return false;
    auto path=directory/L"Shell menu fixture.txt";if(!write_utf8_atomic(path.c_str(),L"isolated shell menu fixture"))return false;
    HWND owner=CreateWindowExW(0,L"STATIC",L"Hidden shell menu test",WS_POPUP,0,0,1,1,nullptr,nullptr,g_app.instance,nullptr);if(!owner)return false;
    scope_exit close([&]{DestroyWindow(owner);});HMENU popup=CreatePopupMenu();if(!popup)return false;scope_exit destroy([&]{DestroyMenu(popup);});
    Menu menu;AppendMenuW(popup,MF_STRING,100,L"Organizer command");
    return menu.append(owner,popup,path.wstring())&&menu.has_verb("properties")&&menu.has_verb("delete")&&menu.has_verb("rename")&&!menu.owns(100)&&std::filesystem::exists(path);
}
}
