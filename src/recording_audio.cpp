#include "recording_audio.hpp"
#include "network.h"
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <thread>
#include <mutex>

namespace lb::recording::audio {
namespace {
uint64_t qpc_time(){LARGE_INTEGER tick{},frequency{};QueryPerformanceCounter(&tick);QueryPerformanceFrequency(&frequency);return uint64_t((long double)tick.QuadPart*10000000.L/frequency.QuadPart);}
void checked(HRESULT result){if(FAILED(result))throw winrt::hresult_error(result);}
class Wave {
    unique_handle file_;uint64_t frames_{};WORD stride_{};DWORD rate_{};uint64_t data_offset_{};
    void write(void const* data,DWORD bytes){DWORD written{};if(!WriteFile(file_.get(),data,bytes,&written,nullptr)||written!=bytes)throw winrt::hresult_error(HRESULT_FROM_WIN32(GetLastError()?GetLastError():ERROR_WRITE_FAULT));}
public:
    void open(std::filesystem::path const& path,WAVEFORMATEX const* format){
        if(!format||format->nSamplesPerSec<8000||format->nSamplesPerSec>384000||!format->nChannels||format->nChannels>32||!format->nBlockAlign||format->nBlockAlign>256||format->cbSize>64)throw winrt::hresult_invalid_argument();
        file_.reset(CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_SEQUENTIAL_SCAN,nullptr));if(!file_)throw winrt::hresult_error(HRESULT_FROM_WIN32(GetLastError()));
        stride_=format->nBlockAlign;rate_=format->nSamplesPerSec;
        uint32_t header[]{0x34364652,0xffffffff,0x45564157,0x34367364,28,0,0,0,0,0,0,0,0x20746d66,uint32_t(sizeof(WAVEFORMATEX)+format->cbSize)};
        write(header,sizeof header);write(format,header[13]);if(header[13]&1){BYTE zero{};write(&zero,1);}
        uint32_t data[]{0x61746164,0xffffffff};write(data,sizeof data);data_offset_=sizeof header+header[13]+(header[13]&1)+sizeof data;
    }
    void silence(uint64_t frames){std::array<BYTE,65536> zero{};while(frames){auto count=std::min<uint64_t>(frames,zero.size()/stride_);write(zero.data(),DWORD(count*stride_));frames_+=count;frames-=count;}}
    void packet(BYTE const* bytes,UINT32 frames,uint64_t relative_100ns,bool silent){
        uint64_t target=relative_100ns*rate_/10000000ULL;
        if(target>uint64_t(rate_)*7202)throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
        if(target>frames_)silence(target-frames_);
        uint64_t skip=frames_>target?std::min<uint64_t>(frames,frames_-target):0;frames-=UINT32(skip);if(!frames)return;
        if(silent)silence(frames);else{if(!bytes)throw winrt::hresult_invalid_argument();write(bytes+skip*stride_,frames*stride_);frames_+=frames;}
    }
    void finish(uint64_t duration_100ns){
        if(!file_)return;uint64_t wanted=std::min<uint64_t>(duration_100ns*rate_/10000000ULL,uint64_t(rate_)*7202);if(wanted>frames_)silence(wanted-frames_);
        uint64_t values[]{data_offset_+frames_*stride_-8,frames_*stride_,frames_};LARGE_INTEGER where{};where.QuadPart=20;if(!SetFilePointerEx(file_.get(),where,nullptr,FILE_BEGIN))throw winrt::hresult_error(HRESULT_FROM_WIN32(GetLastError()));write(values,sizeof values);file_.reset();
    }
    uint64_t frames() const{return frames_;}
};
struct Endpoint {winrt::com_ptr<IMMDevice> device;winrt::com_ptr<IAudioClient> client;winrt::com_ptr<IAudioCaptureClient> capture;Wave wave;DWORD rate{};bool running{};};
class DeviceChanges final:public IMMNotificationClient{
    std::atomic<ULONG> refs_{1};bool system_,microphone_;
public:
    std::atomic_bool changed{};
    DeviceChanges(bool system,bool microphone):system_(system),microphone_(microphone){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** value) override{if(!value)return E_POINTER;*value=nullptr;if(iid==__uuidof(IUnknown)||iid==__uuidof(IMMNotificationClient)){*value=static_cast<IMMNotificationClient*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release() override{auto value=--refs_;if(!value)delete this;return value;}
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow,ERole role,LPCWSTR) override{if(role==eConsole&&((flow==eRender&&system_)||(flow==eCapture&&microphone_)))changed=true;return S_OK;}
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR,DWORD) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR,PROPERTYKEY) override{return S_OK;}
};
void initialize(Endpoint& endpoint,bool system,std::filesystem::path const& path){
    winrt::com_ptr<IMMDeviceEnumerator> enumerator;checked(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),enumerator.put_void()));
    checked(enumerator->GetDefaultAudioEndpoint(system?eRender:eCapture,eConsole,endpoint.device.put()));
    checked(endpoint.device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,endpoint.client.put_void()));
    WAVEFORMATEX* format{};checked(endpoint.client->GetMixFormat(&format));scope_exit free_format([&]{CoTaskMemFree(format);});endpoint.rate=format->nSamplesPerSec;
    checked(endpoint.client->Initialize(AUDCLNT_SHAREMODE_SHARED,system?AUDCLNT_STREAMFLAGS_LOOPBACK:0,1000000,0,format,nullptr));
    checked(endpoint.client->GetService(__uuidof(IAudioCaptureClient),endpoint.capture.put_void()));endpoint.wave.open(path,format);
}
std::wstring reason(HRESULT error,bool english){
    auto tr=[english](wchar_t const* zh,wchar_t const* en){return english?en:zh;};
    if(error==E_ACCESSDENIED)return tr(L"Windows 已阻止麦克风访问，请在系统隐私设置中允许桌面应用使用麦克风。",L"Windows denied microphone access. Allow desktop apps in Windows microphone privacy settings.");
    if(error==HRESULT_FROM_WIN32(ERROR_NOT_FOUND))return tr(L"未找到默认音频设备，请连接设备并在 Windows 声音设置中选择默认设备。",L"No default audio device was found. Connect a device and select it in Windows sound settings.");
    if(error==AUDCLNT_E_DEVICE_INVALIDATED||error==AUDCLNT_E_RESOURCES_INVALIDATED)return tr(L"录音设备已断开或发生切换，录制已停止；已录视频保留，未静默忽略音轨。",L"The audio device was disconnected or switched. Recording stopped; the video was retained and audio was not silently omitted.");
    if(error==AUDCLNT_E_DEVICE_IN_USE)return tr(L"音频设备被独占，请关闭占用设备的应用后重试。",L"The audio device is in exclusive use. Close the app using it and retry.");
    return tr(L"音频采集未完成，已停止录制；请检查设备和磁盘空间。",L"Audio capture failed and recording stopped. Check the device and disk space.");
}
}
struct Capture::Impl {
    std::filesystem::path directory;bool system{},microphone{},english{};std::shared_ptr<std::atomic_bool> stop_signal;std::atomic_bool stop{};bool detached{};
    unique_handle ready{CreateEventW(nullptr,TRUE,FALSE,nullptr)},go{CreateEventW(nullptr,TRUE,FALSE,nullptr)},done{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    std::thread worker;std::atomic<HRESULT> error{S_OK};std::atomic<uint64_t> origin{};std::wstring detail;std::vector<std::filesystem::path> files;
    DWORD prepare_wait{10000},finish_wait{2000};std::function<void(Impl&)> fixture;
    Impl(std::filesystem::path dir,bool s,bool m,bool en,std::shared_ptr<std::atomic_bool> stopped):directory(std::move(dir)),system(s),microphone(m),english(en),stop_signal(std::move(stopped)){if(system)files.push_back(directory/L"system.wav");if(microphone)files.push_back(directory/L"microphone.wav");}
    void run(){
        auto tr=[this](wchar_t const* zh,wchar_t const* en){return english?en:zh;};
        com_apartment apartment(COINIT_MULTITHREADED);
        std::array<Endpoint,2> endpoints;int count=0;bool initialized=false;
        scope_exit stopped([&]{for(auto& endpoint:endpoints){if(endpoint.running)endpoint.client->Stop();try{endpoint.wave.finish(origin?std::min<uint64_t>(qpc_time()-origin,72000000000ULL):0);}catch(winrt::hresult_error const& e){if(SUCCEEDED(error))error=e.code();}}SetEvent(ready.get());});
        try{
            checked(apartment.result());
            winrt::com_ptr<IMMDeviceEnumerator> devices;checked(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),devices.put_void()));
            winrt::com_ptr<DeviceChanges> changes;changes.attach(new DeviceChanges(system,microphone));checked(devices->RegisterEndpointNotificationCallback(changes.get()));scope_exit unwatch([&]{devices->UnregisterEndpointNotificationCallback(changes.get());});
            for(bool is_system:{true,false})if(is_system?system:microphone){
                if(stop||*stop_signal)return;
                detail=is_system?tr(L"系统声音：",L"System audio: "):tr(L"麦克风：",L"Microphone: ");auto path=directory/(is_system?L"system.wav":L"microphone.wav");initialize(endpoints[count],is_system,path);++count;
                if(stop||*stop_signal)return;
            }
            initialized=true;SetEvent(ready.get());while(WaitForSingleObject(go.get(),10)!=WAIT_OBJECT_0){if(stop||*stop_signal)return;}
            if(stop||*stop_signal)return;
            if(changes->changed)checked(AUDCLNT_E_DEVICE_INVALIDATED);
            for(int n=0;n<count;++n){if(stop||*stop_signal)return;checked(endpoints[n].client->Start());endpoints[n].running=true;}
            while(!stop&&!*stop_signal){
                if(changes->changed)checked(AUDCLNT_E_DEVICE_INVALIDATED);
                for(int n=0;n<count;++n){auto& endpoint=endpoints[n];UINT32 available{};checked(endpoint.capture->GetNextPacketSize(&available));
                    while(available&&!stop&&!*stop_signal){BYTE* data{};UINT32 frames{};DWORD flags{};UINT64 device_position{},timestamp{};checked(endpoint.capture->GetBuffer(&data,&frames,&flags,&device_position,&timestamp));scope_exit release([&]{endpoint.capture->ReleaseBuffer(frames);});
                        if(flags&AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)timestamp=qpc_time()-uint64_t(frames)*10000000ULL/endpoint.rate;
                        // Ignore packets predating video launch; pad missing QPC time with zeros.
                        if(timestamp>=origin)endpoint.wave.packet(data,frames,timestamp-origin,(flags&AUDCLNT_BUFFERFLAGS_SILENT)!=0);
                        release.release();checked(endpoint.capture->ReleaseBuffer(frames));checked(endpoint.capture->GetNextPacketSize(&available));
                    }
                }
                Sleep(8);
            }
        }catch(winrt::hresult_error const& exception){error=exception.code();detail+=reason(error,english);}
         catch(...){error=E_FAIL;detail+=reason(E_FAIL,english);}
        if(initialized&&FAILED(error))*stop_signal=true;
    }
};
Capture::Capture(std::filesystem::path directory,bool system,bool microphone,bool english,std::shared_ptr<std::atomic_bool> stop):impl_(std::make_shared<Impl>(std::move(directory),system,microphone,english,std::move(stop))){}
Capture::~Capture(){finish();}
HRESULT Capture::prepare(){
    auto i=impl_;if(!i->ready||!i->go||!i->done||!i->stop_signal)return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY);if(i->worker.joinable())return E_UNEXPECTED;
    try{i->worker=std::thread([i]{try{if(i->fixture)i->fixture(*i);else i->run();}catch(...){i->error=E_FAIL;}SetEvent(i->ready.get());SetEvent(i->done.get());});}
    catch(...){i->error=E_OUTOFMEMORY;SetEvent(i->done.get());return E_OUTOFMEMORY;}
    auto deadline=GetTickCount64()+i->prepare_wait;
    for(;;){if(*i->stop_signal){i->stop=true;return HRESULT_FROM_WIN32(ERROR_CANCELLED);}if(WaitForSingleObject(i->ready.get(),10)==WAIT_OBJECT_0){if(*i->stop_signal){i->stop=true;return HRESULT_FROM_WIN32(ERROR_CANCELLED);}return i->error;}if(GetTickCount64()>=deadline){i->stop=true;return HRESULT_FROM_WIN32(ERROR_TIMEOUT);}}
}
void Capture::begin(){impl_->origin=qpc_time();SetEvent(impl_->go.get());}
HRESULT Capture::finish(){if(!impl_)return S_OK;auto& i=*impl_;i.stop=true;SetEvent(i.go.get());if(i.worker.joinable()){if(WaitForSingleObject(i.done.get(),i.finish_wait)==WAIT_OBJECT_0)i.worker.join();else{i.detached=true;i.worker.detach();auto marker=i.directory/L"cleanup-on-restart";auto pid=std::to_string(GetCurrentProcessId());unique_handle file(CreateFileW(marker.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,0,nullptr));if(file){DWORD written{};WriteFile(file.get(),pid.data(),DWORD(pid.size()),&written,nullptr);}}}return i.detached?HRESULT_FROM_WIN32(ERROR_TIMEOUT):i.error.load();}
HRESULT Capture::status() const{return impl_->error;}
bool Capture::abandoned() const{return impl_->detached;}
std::wstring Capture::error_text() const{if(impl_->detached||WaitForSingleObject(impl_->done.get(),0)!=WAIT_OBJECT_0)return impl_->english?L"The audio driver did not respond. This task stopped; recording files are retained. Exit the app to release a stuck audio driver.":L"音频驱动未响应，任务已停止，已录文件保留。如音频设备仍被占用，请退出应用以释放驱动。";return impl_->detail.size()>18?impl_->detail:impl_->detail+reason(impl_->error,impl_->english);}
std::vector<std::filesystem::path> Capture::paths() const{return impl_->files;}
void cleanup_abandoned(std::filesystem::path const& directory){
    try{
    std::error_code error;if(!std::filesystem::is_directory(directory,error))return;
    for(auto const& entry:std::filesystem::directory_iterator(directory,error)){
        auto folder=entry.path();if(folder.filename().wstring().rfind(L"audio-",0)!=0)continue;auto attributes=GetFileAttributesW(folder.c_str());if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY)||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))continue;
        auto marker=folder/L"cleanup-on-restart";unique_handle file(CreateFileW(marker.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));if(!file)continue;char text[16]{};DWORD count{};if(!ReadFile(file.get(),text,15,&count,nullptr)||!count)continue;file.reset();DWORD pid=0;bool valid=true;for(DWORD n=0;n<count;++n){if(text[n]<'0'||text[n]>'9'){valid=false;break;}pid=pid*10+DWORD(text[n]-'0');}if(!valid||!pid||pid==GetCurrentProcessId())continue;
        unique_handle process(OpenProcess(SYNCHRONIZE,FALSE,pid));if(process&&WaitForSingleObject(process.get(),0)!=WAIT_OBJECT_0)continue;if(!process&&GetLastError()==ERROR_ACCESS_DENIED)continue;
        // Only our known files are removed; never recurse into an unexpected directory.
        for(auto name:{L"system.wav",L"microphone.wav",L"cleanup-on-restart"})DeleteFileW((folder/name).c_str());RemoveDirectoryW(folder.c_str());
    }
    }catch(...){/* Cache cleanup must never prevent application startup. */}
}
bool lifetime_tests(std::filesystem::path const& directory){
    try{std::filesystem::create_directories(directory);for(int stage=0;stage<2;++stage){
        auto folder=directory/(L"audio-blocked-"+std::to_wstring(stage));std::filesystem::create_directories(folder);
        auto latch=std::make_shared<unique_handle>(CreateEventW(nullptr,TRUE,FALSE,nullptr));std::weak_ptr<Capture::Impl> weak;
        auto began=GetTickCount64();
        {auto stop=std::make_shared<std::atomic_bool>();Capture capture(folder,false,false,true,stop);auto state=capture.impl_;weak=state;state->prepare_wait=25;state->finish_wait=25;
            state->fixture=[latch,stage](Capture::Impl& owned){if(stage)SetEvent(owned.ready.get());WaitForSingleObject(latch->get(),5000);auto late=owned.directory/L"late-owned-write.txt";unique_handle file(CreateFileW(late.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,0,nullptr));if(file){DWORD written{};WriteFile(file.get(),"safe",4,&written,nullptr);}};
            auto result=capture.prepare();if(result!=(stage?S_OK:HRESULT_FROM_WIN32(ERROR_TIMEOUT)))return false;if(capture.finish()!=HRESULT_FROM_WIN32(ERROR_TIMEOUT)||!capture.abandoned())return false;
        }
        if(GetTickCount64()-began>1000||weak.expired()||!std::filesystem::exists(folder/L"cleanup-on-restart"))return false;cleanup_abandoned(directory);if(!std::filesystem::exists(folder))return false;
        auto owned=weak.lock();SetEvent(latch->get());if(!owned||WaitForSingleObject(owned->done.get(),2000)!=WAIT_OBJECT_0||!std::filesystem::exists(folder/L"late-owned-write.txt"))return false;owned.reset();auto deadline=GetTickCount64()+1000;while(!weak.expired()&&GetTickCount64()<deadline)Sleep(1);if(!weak.expired())return false;
    }return true;}catch(...){return false;}
}
HRESULT mux(std::filesystem::path const& video,std::vector<std::filesystem::path> const& tracks,std::filesystem::path const& output,int64_t duration_us,bool lossless,media::Control& control){
    if(tracks.empty()||tracks.size()>2||duration_us<=0)return E_INVALIDARG;
    auto partial=output;partial+=L".partial";auto log=output;log+=L".log";scope_exit cleanup([&]{std::error_code e;std::filesystem::remove(partial,e);std::filesystem::remove(log,e);});
    std::vector<std::wstring> args{L"-i",video.wstring()};for(auto const& file:tracks)args.insert(args.end(),{L"-i",file.wstring()});
    std::wstring filter;for(size_t n=0;n<tracks.size();++n)filter+=L"["+std::to_wstring(n+1)+L":a]aresample=48000,asetpts=PTS-STARTPTS[a"+std::to_wstring(n)+L"];";
    filter+=tracks.size()==2?L"[a0][a1]amix=inputs=2:duration=longest:normalize=1,apad[outa]":L"[a0]apad[outa]";
    wchar_t duration[40];StringCchPrintfW(duration,40,L"%.6f",duration_us/1000000.);
    args.insert(args.end(),{L"-filter_complex_threads",L"1",L"-filter_complex",filter,L"-map",L"0:v:0",L"-map",L"[outa]",L"-c:v",L"copy",L"-c:a",lossless?L"flac":L"aac",L"-ac",L"2",L"-t",duration});
    if(!lossless)args.insert(args.end(),{L"-b:a",L"192k",L"-movflags",L"+faststart"});args.insert(args.end(),{L"-f",lossless?L"matroska":L"mp4",partial.wstring()});
    auto result=media::run(args,log,control);if(FAILED(result.error))return result.error;if(control.cancel)return HRESULT_FROM_WIN32(ERROR_CANCELLED);return MoveFileExW(partial.c_str(),output.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)?S_OK:HRESULT_FROM_WIN32(GetLastError());
}
bool tests(std::filesystem::path const& directory){
    try{DeviceChanges changes(true,false);changes.OnDefaultDeviceChanged(eCapture,eConsole,L"synthetic");changes.OnDefaultDeviceChanged(eRender,eCommunications,L"synthetic");if(changes.changed)return false;changes.OnDefaultDeviceChanged(eRender,eConsole,L"synthetic");if(!changes.changed)return false;
        std::filesystem::create_directories(directory);WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=1;format.nSamplesPerSec=48000;format.wBitsPerSample=16;format.nBlockAlign=2;format.nAvgBytesPerSec=96000;
        std::array<int16_t,480> samples{};samples.fill(12000);Wave wave;auto file=directory/L"aligned.wav";wave.open(file,&format);wave.packet(reinterpret_cast<BYTE const*>(samples.data()),480,100000,false);wave.packet(nullptr,480,200000,true);wave.packet(reinterpret_cast<BYTE const*>(samples.data()),480,200000,false);wave.finish(400000);if(wave.frames()!=1920)return false;
        unique_handle input(CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));std::array<BYTE,4096> bytes{};DWORD read{};if(!input||!ReadFile(input.get(),bytes.data(),DWORD(bytes.size()),&read,nullptr))return false;
        uint64_t length{};memcpy(&length,bytes.data()+28,8);size_t offset=sizeof(uint32_t)*14+sizeof format+8;if(length!=3840||read!=offset+3840)return false;
        for(size_t n=0;n<1920;++n){int16_t value{};memcpy(&value,bytes.data()+offset+n*2,2);if(value!=(n>=480&&n<960?12000:0))return false;}
        auto first=directory/L"system-synthetic.wav",second=directory/L"microphone-synthetic.wav",video=directory/L"video.mp4";
        {Wave tone;tone.open(first,&format);std::vector<int16_t> values(48000);for(size_t n=0;n<values.size();++n)values[n]=int16_t(std::sin(double(n)*440.*6.283185307179586/48000.)*6000);tone.packet(reinterpret_cast<BYTE const*>(values.data()),DWORD(values.size()),0,false);tone.finish(10000000);}
        {WAVEFORMATEXTENSIBLE floating{};floating.Format={WAVE_FORMAT_EXTENSIBLE,2,48000,384000,8,32,22};floating.Samples.wValidBitsPerSample=32;floating.dwChannelMask=3;floating.SubFormat={3,0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};Wave tone;tone.open(second,&floating.Format);std::vector<float> values(96000);for(size_t n=0;n<values.size()/2;++n)values[n*2]=values[n*2+1]=float(std::sin(double(n)*880.*6.283185307179586/48000.)*.18);tone.packet(reinterpret_cast<BYTE const*>(values.data()),48000,0,false);tone.finish(10000000);}
        media::Control control;auto result=media::run({L"-f",L"lavfi",L"-i",L"color=c=black:s=160x90:r=10:d=1",L"-an",L"-c:v",L"libx264",L"-preset",L"ultrafast",L"-pix_fmt",L"yuv420p",L"-f",L"mp4",video.wstring()},directory/L"video.log",control);if(FAILED(result.error))return false;
        auto original_stream=directory/L"original.h264";
        result=media::run({L"-i",video.wstring(),L"-map",L"0:v:0",L"-c:v",L"copy",L"-f",L"h264",original_stream.wstring()},directory/L"extract.log",control);if(FAILED(result.error))return false;wchar_t original_hash[65];if(!hash_file(original_stream.c_str(),L"SHA256",original_hash,65))return false;
        for(int mode=0;mode<3;++mode){auto output=directory/(mode==2?L"dual-lossless.mkv":mode==1?L"dual.mp4":L"single.mp4");std::vector<std::filesystem::path> tracks{first};if(mode)tracks.push_back(second);if(FAILED(mux(video,tracks,output,1000000,mode==2,control)))return false;
            auto stream=directory/(L"copy-"+std::to_wstring(mode)+L".h264");result=media::run({L"-i",output.wstring(),L"-map",L"0:v:0",L"-c:v",L"copy",L"-f",L"h264",stream.wstring()},directory/L"extract.log",control);wchar_t actual[65];if(FAILED(result.error)||!hash_file(stream.c_str(),L"SHA256",actual,65)||wcscmp(actual,original_hash))return false;
            auto pcm=directory/(L"samples-"+std::to_wstring(mode)+L".pcm");result=media::run({L"-i",output.wstring(),L"-map",L"0:a:0",L"-ac",L"1",L"-ar",L"48000",L"-c:a",L"pcm_s16le",L"-f",L"s16le",pcm.wstring()},directory/L"decode.log",control);if(FAILED(result.error))return false;
            unique_handle decoded(CreateFileW(pcm.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));std::vector<int16_t> values(50000);DWORD count{};LARGE_INTEGER size{};if(!decoded||!GetFileSizeEx(decoded.get(),&size)||size.QuadPart<96000||size.QuadPart>100000||!ReadFile(decoded.get(),values.data(),DWORD(values.size()*2),&count,nullptr)||count<96000||count>100000)return false;
            auto energy=[&](double frequency){double real{},imaginary{};for(size_t n=4800;n<43200;++n){double angle=n*frequency*6.283185307179586/48000.;real+=values[n]*std::cos(angle);imaginary+=values[n]*std::sin(angle);if(std::abs(int(values[n]))>=32760)return -1.;}return real*real+imaginary*imaginary;};double e440=energy(440),e880=energy(880);if(e440<1e12||(mode?e880<1e12:e440<e880*100))return false;
        }
        control.cancel=true;auto cancelled=directory/L"cancelled.mp4";if(mux(video,{first,second},cancelled,1000000,false,control)!=HRESULT_FROM_WIN32(ERROR_CANCELLED)||std::filesystem::exists(cancelled)||std::filesystem::exists(cancelled.wstring()+L".partial"))return false;
        return true;
    }catch(...){return false;}
}
}
