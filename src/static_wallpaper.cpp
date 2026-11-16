#include "static_wallpaper.hpp"
#include "json.h"
#include <filesystem>
#include <functional>
#include <system_error>
#include <propvarutil.h>

namespace lb::static_wallpaper {
namespace {
namespace fs=std::filesystem;
struct Snapshot { std::wstring path,style{L"10"},tile{L"0"}; };
struct Backend {
    std::function<HRESULT(Snapshot&)> read;
    std::function<HRESULT(Snapshot const&)> apply;
};
struct Session {
    unique_handle process,job,cancel_event;
    std::wstring path;
    ULONGLONG began{};
    bool restoring{},cancelled{},timed_out{};
} session;
HRESULT last_error() { auto error=GetLastError();return HRESULT_FROM_WIN32(error?error:ERROR_GEN_FAILURE); }
std::wstring quoted(std::wstring const& value) {
    std::wstring out=L"\"";
    for(auto c:value) { if(c==L'\\'||c==L'"') out+=L'\\';if(c<32) { wchar_t escaped[7];swprintf_s(escaped,L"\\u%04x",unsigned(c));out+=escaped; }else out+=c; }
    return out+L"\"";
}
bool save_snapshot(fs::path const& file,Snapshot const& value) {
    auto text=L"{\"schema\":1,\"path\":"+quoted(value.path)+L",\"WallpaperStyle\":"+quoted(value.style)+L",\"TileWallpaper\":"+quoted(value.tile)+L"}\n";
    return write_utf8_atomic(file.c_str(),text.c_str())!=FALSE;
}
bool load_snapshot(fs::path const& file,Snapshot &value) {
    Json json;if(!json_file(&json,file.c_str(),128*1024)) return false;
    wchar_t path[LB_PATH],style[16],tile[16];
    if(!json_string(&json,json_property(&json,0,"path"),path,LB_PATH)||!json_string(&json,json_property(&json,0,"WallpaperStyle"),style,16)||!json_string(&json,json_property(&json,0,"TileWallpaper"),tile,16)) return false;
    if(!*style||!*tile||wcsspn(style,L"0123456789")!=wcslen(style)||wcsspn(tile,L"01")!=wcslen(tile)) return false;
    value={path,style,tile};return true;
}
HRESULT read_system(Snapshot &snapshot) {
    wchar_t path[LB_PATH];if(!SystemParametersInfoW(SPI_GETDESKWALLPAPER,LB_PATH,path,0)) return last_error();snapshot.path=path;
    for(auto const& field:{std::pair{L"WallpaperStyle",&snapshot.style},std::pair{L"TileWallpaper",&snapshot.tile}}) {
        wchar_t value[32]{};DWORD bytes=sizeof value;
        auto error=RegGetValueW(HKEY_CURRENT_USER,L"Control Panel\\Desktop",field.first,RRF_RT_REG_SZ,nullptr,value,&bytes);
        if(error==ERROR_SUCCESS) *field.second=value;else if(error!=ERROR_FILE_NOT_FOUND) return HRESULT_FROM_WIN32(error);
    }
    return S_OK;
}
HRESULT apply_system(Snapshot const& snapshot) {
    HKEY raw{};auto status=RegOpenKeyExW(HKEY_CURRENT_USER,L"Control Panel\\Desktop",0,KEY_SET_VALUE,&raw);if(status!=ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
    scope_exit close([&]{RegCloseKey(raw);});
    for(auto const& field:{std::pair{L"WallpaperStyle",&snapshot.style},std::pair{L"TileWallpaper",&snapshot.tile}}) {
        status=RegSetValueExW(raw,field.first,0,REG_SZ,reinterpret_cast<BYTE const*>(field.second->c_str()),DWORD((field.second->size()+1)*sizeof(wchar_t)));
        if(status!=ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
    }
    return SystemParametersInfoW(SPI_SETDESKWALLPAPER,0,const_cast<wchar_t*>(snapshot.path.c_str()),SPIF_UPDATEINIFILE|SPIF_SENDCHANGE)?S_OK:last_error();
}
HRESULT make_native_image(fs::path const& source,fs::path const& destination) {
    // Decode and encode at the original resolution; PNG is lossless. This also
    // supports TIFF/WebP codecs without relying on Explorer's decoder set.
    winrt::com_ptr<IWICImagingFactory> factory;winrt::com_ptr<IWICBitmapDecoder> decoder;winrt::com_ptr<IWICBitmapFrameDecode> input;
    HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,factory.put_void());if(FAILED(hr)) return hr;
    hr=factory->CreateDecoderFromFilename(source.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,decoder.put());if(FAILED(hr)) return hr;
    hr=decoder->GetFrame(0,input.put());if(FAILED(hr)) return hr;UINT width{},height{};hr=input->GetSize(&width,&height);if(FAILED(hr)) return hr;
    if(!width||!height||width>32768||height>32768||uint64_t(width)*height>128ull*1024*1024) return WINCODEC_ERR_IMAGESIZEOUTOFRANGE;
    winrt::com_ptr<IWICBitmapSource> source_bitmap;input->QueryInterface(IID_IWICBitmapSource,source_bitmap.put_void());
    winrt::com_ptr<IWICMetadataQueryReader> metadata;
    if(SUCCEEDED(input->GetMetadataQueryReader(metadata.put()))) {
        PROPVARIANT orientation;PropVariantInit(&orientation);scope_exit clear([&]{PropVariantClear(&orientation);});
        auto result=metadata->GetMetadataByName(L"/app1/ifd/{ushort=274}",&orientation);
        if(FAILED(result)) result=metadata->GetMetadataByName(L"/ifd/{ushort=274}",&orientation);
        UINT value=SUCCEEDED(result)&&orientation.vt==VT_UI2?orientation.uiVal:1;
        if(value>=2&&value<=8) {
            constexpr WICBitmapTransformOptions transforms[]={WICBitmapTransformRotate0,WICBitmapTransformRotate0,WICBitmapTransformFlipHorizontal,WICBitmapTransformRotate180,WICBitmapTransformFlipVertical,WICBitmapTransformOptions(WICBitmapTransformRotate90|WICBitmapTransformFlipHorizontal),WICBitmapTransformRotate90,WICBitmapTransformOptions(WICBitmapTransformRotate270|WICBitmapTransformFlipHorizontal),WICBitmapTransformRotate270};
            winrt::com_ptr<IWICBitmapFlipRotator> rotated;hr=factory->CreateBitmapFlipRotator(rotated.put());if(FAILED(hr)) return hr;hr=rotated->Initialize(source_bitmap.get(),transforms[value]);if(FAILED(hr)) return hr;
            source_bitmap=nullptr;hr=rotated->QueryInterface(IID_IWICBitmapSource,source_bitmap.put_void());if(FAILED(hr)) return hr;source_bitmap->GetSize(&width,&height);
        }
    }
    auto temp=destination;temp+=L".partial";bool saved{};scope_exit cleanup([&]{if(!saved) DeleteFileW(temp.c_str());});
    {
        winrt::com_ptr<IWICStream> stream;winrt::com_ptr<IWICBitmapEncoder> encoder;winrt::com_ptr<IWICBitmapFrameEncode> output;winrt::com_ptr<IPropertyBag2> properties;
        hr=factory->CreateStream(stream.put());if(FAILED(hr)) return hr;hr=stream->InitializeFromFilename(temp.c_str(),GENERIC_WRITE);if(FAILED(hr)) return hr;
        hr=factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,encoder.put());if(FAILED(hr)) return hr;hr=encoder->Initialize(stream.get(),WICBitmapEncoderNoCache);if(FAILED(hr)) return hr;
        hr=encoder->CreateNewFrame(output.put(),properties.put());if(FAILED(hr)) return hr;hr=output->Initialize(properties.get());if(FAILED(hr)) return hr;
        hr=output->SetSize(width,height);if(FAILED(hr)) return hr;WICPixelFormatGUID format=GUID_WICPixelFormat24bppBGR;hr=output->SetPixelFormat(&format);if(FAILED(hr)) return hr;
        hr=output->WriteSource(source_bitmap.get(),nullptr);if(FAILED(hr)) return hr;hr=output->Commit();if(FAILED(hr)) return hr;hr=encoder->Commit();if(FAILED(hr)) return hr;
    }
    saved=MoveFileExW(temp.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;return saved?S_OK:last_error();
}
HRESULT copy_original(fs::path const& folder,Snapshot &snapshot) {
    if(snapshot.path.empty()) return S_OK;
    fs::path source(snapshot.path);std::error_code error;if(!fs::is_regular_file(source,error)) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    auto suffix=source.extension().wstring();if(suffix.size()>12) suffix=L".bmp";auto target=folder/(L"original-wallpaper"+suffix),temporary=target;temporary+=L".partial";
    if(_wcsicmp(source.c_str(),target.c_str())==0) return S_OK;
    if(!CopyFileW(source.c_str(),temporary.c_str(),FALSE)) return last_error();
    if(!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) { auto hr=last_error();DeleteFileW(temporary.c_str());return hr; }
    snapshot.path=target.wstring();return S_OK;
}
HRESULT operation(fs::path const& folder,std::wstring const& path,int fit,bool restoring,Backend const& backend,std::function<bool()> const& alive) {
    if(fit<0||fit>3) return E_INVALIDARG;
    std::error_code error;fs::create_directories(folder,error);if(error) return HRESULT_FROM_WIN32(error.value());
    Snapshot before;auto hr=backend.read(before);if(FAILED(hr)) return hr;
    auto receipt=folder/L"original.json";Snapshot original;
    bool exists=fs::exists(receipt,error);if(error) return HRESULT_FROM_WIN32(error.value());
    if(exists) { if(!load_snapshot(receipt,original)) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA); }
    else {
        if(restoring) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        original=before;hr=copy_original(folder,original);if(FAILED(hr)) return hr;
        // Do not alter any system state unless the recovery snapshot is durable.
        if(!save_snapshot(receipt,original)) return last_error();
    }
    Snapshot target;
    if(restoring) target=original;
    else {
        if(path.empty()||media_kind(path.c_str())!=MEDIA_IMAGE) return E_INVALIDARG;
        auto first=folder/L"active-a.png",second=folder/L"active-b.png";
        auto output=_wcsicmp(before.path.c_str(),first.c_str())==0?second:first;
        hr=make_native_image(path,output);if(FAILED(hr)) return hr;
        static wchar_t const* styles[]={L"10",L"6",L"2",L"0"};target={output.wstring(),styles[fit],L"0"};
    }
    if(!target.path.empty()&&!fs::is_regular_file(target.path,error)) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    if(!alive()) return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    hr=backend.apply(target);
    if(FAILED(hr)) { auto rollback=backend.apply(before);log_line(L"static wallpaper failed hr=0x%08lX rollback=0x%08lX",DWORD(hr),DWORD(rollback));return hr; }
    return S_OK;
}
bool launch(wchar_t const* path,bool restoring) {
    if(session.process) return false;
    wchar_t executable[LB_PATH*2+4],file[LB_PATH*2+4],data[LB_PATH*2+4],command[LB_PATH*6+800],event_name[160];
    if(!quote_arg(executable,ARRAYSIZE(executable),g_app.exe)||!quote_arg(file,ARRAYSIZE(file),path?path:L"")||!quote_arg(data,ARRAYSIZE(data),g_app.data)) return false;
    FILETIME created{},exit{},kernel{},user{};if(!GetProcessTimes(GetCurrentProcess(),&created,&exit,&kernel,&user)) return false;ULARGE_INTEGER creation{};creation.LowPart=created.dwLowDateTime;creation.HighPart=created.dwHighDateTime;
    static std::atomic_ulong serial{};StringCchPrintfW(event_name,ARRAYSIZE(event_name),L"Local\\LightBlock.Static.%lu.%llu.%lu",GetCurrentProcessId(),creation.QuadPart,++serial);
    unique_handle cancelled(CreateEventW(nullptr,TRUE,FALSE,event_name));if(!cancelled) return false;
    if(FAILED(StringCchPrintfW(command,ARRAYSIZE(command),L"%s --static-wallpaper-helper --path %s --fit %d --data-dir %s --parent %lu --parent-created %llu --cancel-event %s %s",executable,file,g_app.settings.fit,data,GetCurrentProcessId(),creation.QuadPart,event_name,restoring?L"--restore":L""))) return false;
    unique_handle job(CreateJobObjectW(nullptr,nullptr));JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!job||!SetInformationJobObject(job.get(),JobObjectExtendedLimitInformation,&limits,sizeof limits)) return false;
    STARTUPINFOW startup{sizeof startup};PROCESS_INFORMATION info{};
    if(!CreateProcessW(g_app.exe,command,nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,g_app.directory,&startup,&info)) return false;
    unique_handle process(info.hProcess),thread(info.hThread);
    if(!AssignProcessToJobObject(job.get(),process.get())) { TerminateProcess(process.get(),ERROR_CANCELLED);return false; }
    if(ResumeThread(thread.get())==DWORD(-1)) return false;
    session.process=std::move(process);session.job=std::move(job);session.cancel_event=std::move(cancelled);session.path=path?path:L"";session.restoring=restoring;session.began=GetTickCount64();session.cancelled=session.timed_out=false;
    SetTimer(g_app.window,1,80,nullptr);set_status(restoring?L"正在恢复原系统壁纸…":L"正在后台应用静态壁纸…",restoring?L"Restoring the original Windows wallpaper…":L"Applying the static wallpaper in the background…");return true;
}
}
bool request(wchar_t const* path) { return launch(path,false); }
bool restore() { return launch(L"",true); }
bool busy() noexcept { return bool(session.process); }
bool reading(wchar_t const* path) noexcept {return session.process&&!session.restoring&&path&&!_wcsicmp(path,session.path.c_str());}
bool has_original() noexcept {try {std::error_code error;return fs::is_regular_file(fs::path(g_app.data)/L"wallpaper-system"/L"original.json",error);}catch(...) {return false;}}
void cancel() noexcept { session.cancelled=true;if(session.cancel_event) SetEvent(session.cancel_event.get()); }
Outcome tick() {
    Outcome result;if(!session.process) return result;
    if(WaitForSingleObject(session.process.get(),0)==WAIT_TIMEOUT) {
        if(!session.timed_out&&GetTickCount64()-session.began>30000) { session.timed_out=true;TerminateJobObject(session.job.get(),DWORD(HRESULT_FROM_WIN32(ERROR_TIMEOUT))); }
        return result;
    }
    DWORD exit{};if(!GetExitCodeProcess(session.process.get(),&exit)) exit=DWORD(last_error());
    result={true,exit==0,session.restoring,session.cancelled,exit?HRESULT(exit):S_OK,std::move(session.path)};
    session=Session{};return result;
}
void dispose() noexcept { if(session.job) TerminateJobObject(session.job.get(),DWORD(HRESULT_FROM_WIN32(ERROR_CANCELLED)));session=Session{}; }
int helper_main(int argc,wchar_t** argv) {
    wchar_t const* path=nullptr;wchar_t const* event_name=nullptr;int fit=-1;bool restoring=false;DWORD parent_pid{};unsigned long long expected_created{};
    for(int i=1;i<argc;++i) {
        if(!wcscmp(argv[i],L"--path")&&i+1<argc) path=argv[++i];
        else if(!wcscmp(argv[i],L"--fit")&&i+1<argc) fit=_wtoi(argv[++i]);
        else if(!wcscmp(argv[i],L"--parent")&&i+1<argc) parent_pid=wcstoul(argv[++i],nullptr,10);
        else if(!wcscmp(argv[i],L"--parent-created")&&i+1<argc) expected_created=_wcstoui64(argv[++i],nullptr,10);
        else if(!wcscmp(argv[i],L"--cancel-event")&&i+1<argc) event_name=argv[++i];
        else if(!wcscmp(argv[i],L"--restore")) restoring=true;
    }
    if(!path||fit<0||fit>3||!parent_pid||!expected_created||!event_name) return int(E_INVALIDARG);
    unique_handle cancelled(OpenEventW(SYNCHRONIZE,FALSE,event_name));if(!cancelled) return int(last_error());
    unique_handle parent(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,parent_pid));if(!parent) return int(last_error());
    FILETIME created{},exit{},kernel{},user{};if(!GetProcessTimes(parent.get(),&created,&exit,&kernel,&user)) return int(last_error());ULARGE_INTEGER creation{};creation.LowPart=created.dwLowDateTime;creation.HighPart=created.dwHighDateTime;if(creation.QuadPart!=expected_created) return int(E_ACCESSDENIED);
    com_apartment apartment(COINIT_MULTITHREADED);if(FAILED(apartment.result())) return int(apartment.result());
    try { return int(operation(fs::path(g_app.data)/L"wallpaper-system",path,fit,restoring,{read_system,apply_system},[&]{return WaitForSingleObject(parent.get(),0)==WAIT_TIMEOUT&&WaitForSingleObject(cancelled.get(),0)==WAIT_TIMEOUT;})); }
    catch(winrt::hresult_error const& error) { return int(error.code()); }catch(...) { return int(E_FAIL); }
}
bool tests(wchar_t const* directory) {
    // No real wallpaper/registry calls: verify transaction ordering and recovery
    // against a fake shell, using a lossless image copied into an isolated root.
    try {
        auto folder=fs::path(directory)/L"static-wallpaper-fixture";std::error_code ec;fs::create_directories(folder,ec);if(ec) return false;
        auto source=folder/L"fixture.bmp";
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=11;info.bmiHeader.biHeight=-7;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* pixels{};
        unique_gdi<HBITMAP> bitmap(CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0));if(!bitmap) return false;std::fill_n(static_cast<uint32_t*>(pixels),77,0xff127bcdu);
        source=folder/L"fixture.png";if(!save_png(bitmap.get(),source.c_str(),false)) return false;
        Snapshot shell{source.wstring(),L"6",L"0"};int writes{};bool fail{},durable{};
        Backend backend{[&](Snapshot& value){value=shell;return S_OK;},[&](Snapshot const& value){++writes;Snapshot receipt;durable=load_snapshot(folder/L"state"/L"original.json",receipt);if(fail) {fail=false;return E_FAIL;}shell=value;return S_OK;}};
        auto alive=[] {return true;};
        if(FAILED(operation(folder/L"state",source.wstring(),0,false,backend,alive))||writes!=1||!durable||shell.style!=L"10") return false;
        Image image;if(FAILED(image_open(&image,shell.path.c_str()))||image.width!=11||image.height!=7||image.pixels[0]!=0xff127bcdu) return false;
        image_close(&image);
        auto first=shell.path;
        if(FAILED(operation(folder/L"state",source.wstring(),1,false,backend,alive))||shell.path==first||shell.style!=L"6") return false;
        auto previous=shell.path;fail=true;int before=writes;
        if(SUCCEEDED(operation(folder/L"state",source.wstring(),2,false,backend,alive))||writes!=before+2||shell.path!=previous) return false;
        before=writes;if(SUCCEEDED(operation(folder/L"state",source.wstring(),3,false,backend,[]{return false;}))||writes!=before) return false;
        if(FAILED(operation(folder/L"state",L"",0,true,backend,alive))||shell.style!=L"6"||fs::path(shell.path).filename()!=L"original-wallpaper.png") return false;
        Snapshot original;if(!load_snapshot(folder/L"state"/L"original.json",original)||original.path!=shell.path) return false;
        before=writes;if(SUCCEEDED(operation(folder/L"state",(folder/L"missing.png").wstring(),0,false,backend,alive))||writes!=before) return false;
        return true;
    }catch(...) {return false;}
}
}
