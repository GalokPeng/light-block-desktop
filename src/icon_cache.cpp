#include "icon_cache.hpp"
#include <commoncontrols.h>
#include <unordered_map>

namespace lb {
namespace {
HICON normalize(HICON source,int output) {
    output=std::clamp(output,16,256);int input=256;ICONINFO original{};if(GetIconInfo(source,&original)){BITMAP meta{};if(GetObjectW(original.hbmColor?original.hbmColor:original.hbmMask,sizeof meta,&meta))input=std::clamp(int(meta.bmWidth),16,1024);if(original.hbmColor)DeleteObject(original.hbmColor);if(original.hbmMask)DeleteObject(original.hbmMask);}BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=input;info.bmiHeader.biHeight=-input;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void *raw=nullptr;unique_gdi<HBITMAP> bitmap(CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&raw,nullptr,0));if(!bitmap) return CopyIcon(source);
    HDC dc=CreateCompatibleDC(nullptr);if(!dc) return CopyIcon(source);scope_exit close_dc([&]{DeleteDC(dc);});auto previous=SelectObject(dc,bitmap.get());scope_exit select_back([&]{SelectObject(dc,previous);});
    auto pixels=static_cast<uint32_t*>(raw);std::fill_n(pixels,input*input,0u);DrawIconEx(dc,0,0,source,input,input,0,nullptr,DI_NORMAL);
    bool alpha=false;for(int n=0;n<input*input;++n) if(pixels[n]>>24) { alpha=true;break; }
    if(!alpha) { std::vector<uint32_t> color(pixels,pixels+input*input);std::fill_n(pixels,input*input,0xffffffffu);DrawIconEx(dc,0,0,source,input,input,0,nullptr,DI_MASK);for(int n=0;n<input*input;++n) pixels[n]=color[n]|((pixels[n]&0xffffff)?0:0xff000000u); }
    int left=input,top=input,right=0,bottom=0;
    for(int y=0;y<input;++y) for(int x=0;x<input;++x) if((pixels[y*input+x]>>24)>8) { left=std::min(left,x);top=std::min(top,y);right=std::max(right,x+1);bottom=std::max(bottom,y+1); }
    if(right<=left||bottom<=top) return CopyIcon(source);
    info.bmiHeader.biWidth=output;info.bmiHeader.biHeight=-output;void *target=nullptr;unique_gdi<HBITMAP> result(CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&target,nullptr,0));if(!result) return CopyIcon(source);
    auto out=static_cast<uint32_t*>(target);std::fill_n(out,output*output,0u);double scale=(output*116./128.)/std::max(right-left,bottom-top),w=(right-left)*scale,h=(bottom-top)*scale;int start_x=int((output-w)/2),start_y=int((output-h)/2);
    for(int y=0;y<int(h);++y) for(int x=0;x<int(w);++x) { double sx=left+(x+.5)/scale-.5,sy=top+(y+.5)/scale-.5;int x0=std::clamp(int(floor(sx)),left,right-1),y0=std::clamp(int(floor(sy)),top,bottom-1),x1=std::min(x0+1,right-1),y1=std::min(y0+1,bottom-1);double fx=std::clamp(sx-x0,0.,1.),fy=std::clamp(sy-y0,0.,1.);uint32_t value=0;for(int channel=0;channel<4;++channel) { auto component=[channel](uint32_t p){return double((p>>(channel*8))&255);};double a=component(pixels[y0*input+x0])*(1-fx)+component(pixels[y0*input+x1])*fx,b=component(pixels[y1*input+x0])*(1-fx)+component(pixels[y1*input+x1])*fx;value|=uint32_t(std::clamp(int(std::lround(a*(1-fy)+b*fy)),0,255))<<(channel*8); }out[(start_y+y)*output+start_x+x]=value; }
    std::vector<BYTE> zeros(size_t(((output+15)/16)*2)*output);unique_gdi<HBITMAP> mask(CreateBitmap(output,output,1,1,zeros.data()));ICONINFO icon{};icon.fIcon=TRUE;icon.hbmColor=result.get();icon.hbmMask=mask.get();return CreateIconIndirect(&icon);
}
HICON load(std::wstring const& path,bool folder,bool system,bool resolve_metadata,int size) {
    HICON icon=nullptr;
    if(system) { SHSTOCKICONINFO info{sizeof info};auto id=path==L"shell:MyComputerFolder"?SIID_DESKTOPPC:path==L"shell:RecycleBinFolder"?SIID_RECYCLER:SIID_APPLICATION;if(SUCCEEDED(SHGetStockIconInfo(id,SHGSI_SYSICONINDEX,&info))){winrt::com_ptr<IImageList> list;if(SUCCEEDED(SHGetImageList(SHIL_JUMBO,__uuidof(IImageList),list.put_void())))list->GetIcon(info.iSysImageIndex,ILD_TRANSPARENT,&icon);} }
    else {
        auto extension=std::filesystem::path(path).extension().wstring();bool specific=!_wcsicmp(extension.c_str(),L".exe")||!_wcsicmp(extension.c_str(),L".lnk")||!_wcsicmp(extension.c_str(),L".url");SHFILEINFOW info{};
        if(SHGetFileInfoW(path.c_str(),folder?FILE_ATTRIBUTE_DIRECTORY:FILE_ATTRIBUTE_NORMAL,&info,sizeof info,SHGFI_SYSICONINDEX|((resolve_metadata||(specific&&!folder))?0:SHGFI_USEFILEATTRIBUTES))) {
            winrt::com_ptr<IImageList> list;if(SUCCEEDED(SHGetImageList(SHIL_JUMBO,__uuidof(IImageList),list.put_void()))) list->GetIcon(info.iIcon,ILD_TRANSPARENT,&icon);
        }
    }
    if(!icon) icon=CopyIcon(LoadIconW(nullptr,IDI_APPLICATION));if(!icon) return nullptr;scope_exit release([&]{DestroyIcon(icon);});return normalize(icon,size);
}
}
IconCache::~IconCache() { stop(); }
void IconCache::start(HWND window,UINT message,size_t pending_limit,bool resolve_metadata) {
    stop();auto state=std::make_shared<State>();state->window=window;state->message=message;state->pending_limit=std::clamp<size_t>(pending_limit,1,512);state->resolve_metadata=resolve_metadata;state_=state;
    thread_=std::thread([state] {
        com_apartment apartment;scope_exit done([&]{SetEvent(state->done.get());});
        for(;;) {
            Request request;
            { std::unique_lock lock(state->mutex);state->condition.wait(lock,[&]{return state->stop||!state->requests.empty();});if(state->stop) return;request=std::move(state->requests.front());state->requests.pop_front(); }
            auto reply=std::make_unique<Reply>();reply->key=request.key;
            try { reply->icon=load(request.path,request.folder,request.system,state->resolve_metadata,request.size); }catch(...) { reply->icon=CopyIcon(LoadIconW(nullptr,IDI_APPLICATION)); }
            { std::lock_guard lock(state->mutex);if(state->stop) return;state->replies.push_back(std::move(reply)); }
            if(state->window&&state->message) PostMessageW(state->window,state->message,0,0);
        }
    });
}
void IconCache::stop(DWORD wait_ms) noexcept {
    for(auto const& [key,icon]:icons_)if(icon)DestroyIcon(icon);icons_.clear();order_.clear();
    if(!state_) return;{ std::lock_guard lock(state_->mutex);state_->stop=true; }state_->condition.notify_all();if(thread_.joinable()) { if(WaitForSingleObject(state_->done.get(),wait_ms)==WAIT_OBJECT_0) thread_.join();else thread_.detach(); }state_.reset();
}
HICON IconCache::get(std::wstring const& identity,std::wstring const& path,bool folder,bool system,int size) {
    size=std::clamp(size,16,256);auto key=identity+L"\npx="+std::to_wstring(size);
    if(auto icon=icons_.find(key);icon!=icons_.end()) return icon->second;if(!state_) return nullptr;
    { std::lock_guard lock(state_->mutex);if(state_->pending.size()<state_->pending_limit && state_->pending.insert(key).second) state_->requests.push_back({key,path,folder,system,size}); }
    state_->condition.notify_one();return nullptr;
}
void IconCache::receive() {
    if(!state_) return;std::vector<std::unique_ptr<Reply>> replies;{ std::lock_guard lock(state_->mutex);replies=std::move(state_->replies);for(auto const& reply:replies) state_->pending.erase(reply->key); }
    for(auto &reply:replies) { if(icons_.contains(reply->key)) continue;while(order_.size()>=128) { auto old=std::move(order_.front());order_.pop_front();if(icons_[old]) DestroyIcon(icons_[old]);icons_.erase(old); }order_.push_back(reply->key);icons_.emplace(reply->key,std::exchange(reply->icon,nullptr)); }
}
bool icon_cache_tests(){
    auto source=LoadIconW(nullptr,IDI_INFORMATION);if(!source)return false;
    auto dimensions=[](HICON icon,int expected){ICONINFO info{};if(!icon||!GetIconInfo(icon,&info))return false;scope_exit clean([&]{if(info.hbmColor)DeleteObject(info.hbmColor);if(info.hbmMask)DeleteObject(info.hbmMask);});BITMAP bitmap{};return GetObjectW(info.hbmColor,sizeof bitmap,&bitmap)&&bitmap.bmWidth==expected&&bitmap.bmHeight==expected;};
    for(int size:{24,48,96,160,256}){auto normalized=normalize(source,size);bool valid=dimensions(normalized,size);if(normalized)DestroyIcon(normalized);if(!valid)return false;}
    IconCache cache;cache.start(nullptr,0,8);auto obtain=[&](int size){HICON icon{};auto end=GetTickCount64()+5000;while(!icon&&GetTickCount64()<end){cache.receive();icon=cache.get(L"fixture",L"shell:MyComputerFolder",false,true,size);if(!icon)Sleep(10);}return icon;};
    auto small=obtain(48),large=obtain(160);if(!dimensions(small,48)||!dimensions(large,160)||small==large)return false;
    if(cache.get(L"fixture",L"shell:MyComputerFolder",false,true,48)!=small)return false;
    cache.stop();return !cache.get(L"fixture",L"shell:MyComputerFolder",false,true,48);
}
}
