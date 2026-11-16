#include "media_process.hpp"
#include "network.h"
#include "runtime_identity.hpp"
#include <array>
#include <charconv>

namespace lb::media {
std::wstring runtime_path() { return (std::filesystem::path(g_app.directory)/L"runtime"/L"recording"/L"ffmpeg.exe").wstring(); }
namespace {
std::wstring quote_command_arg(std::wstring const& value) { std::vector<wchar_t> out(value.size()*2+5);if(!quote_arg(out.data(),out.size(),value.c_str())) throw std::length_error("argument too long");return out.data(); }
class AttributeList {
    std::unique_ptr<BYTE[]> storage_;LPPROC_THREAD_ATTRIBUTE_LIST value_{};HANDLE job_{};
public:
    explicit AttributeList(std::array<HANDLE,3> const& handles,HANDLE job):job_(job) {
        SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,2,0,&bytes);storage_=std::make_unique<BYTE[]>(bytes);auto value=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage_.get());
        if(!InitializeProcThreadAttributeList(value,2,0,&bytes)) throw winrt::hresult_error(HRESULT_FROM_WIN32(GetLastError()));value_=value;
        if(!UpdateProcThreadAttribute(value_,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,const_cast<HANDLE*>(handles.data()),sizeof handles,nullptr,nullptr)||
           !UpdateProcThreadAttribute(value_,0,PROC_THREAD_ATTRIBUTE_JOB_LIST,&job_,sizeof job_,nullptr,nullptr)) { HRESULT error=HRESULT_FROM_WIN32(GetLastError());DeleteProcThreadAttributeList(value_);value_=nullptr;throw winrt::hresult_error(error); }
    }
    ~AttributeList() { if(value_) DeleteProcThreadAttributeList(value_); }
    LPPROC_THREAD_ATTRIBUTE_LIST get() const { return value_; }
};
int64_t number(std::string_view value) { int64_t result=0;auto [end,error]=std::from_chars(value.data(),value.data()+value.size(),result);return error==std::errc{}&&end!=value.data()?std::max<int64_t>(0,result):0; }
}
Result run(std::vector<std::wstring> const& arguments,std::filesystem::path const& log,Control &control,std::function<void(Progress)> notify) {
    Result result;
    try {
        if(control.cancel) { result.error=HRESULT_FROM_WIN32(ERROR_CANCELLED);return result; }
        std::wstring executable=runtime_path();wchar_t digest[65];
        if(!hash_file(executable.c_str(),L"SHA256",digest,65)||_wcsicmp(digest,LB_FFMPEG_SHA256)) { result.error=HRESULT_FROM_WIN32(ERROR_FILE_INVALID);return result; }
        SECURITY_ATTRIBUTES security{sizeof security,nullptr,TRUE};HANDLE out_read{},out_write{},in_read{},in_write{};
        if(!CreatePipe(&out_read,&out_write,&security,16384)) throw winrt::hresult_error(HRESULT_FROM_WIN32(GetLastError()));
        unique_handle output(out_read),output_child(out_write);
        if(!CreatePipe(&in_read,&in_write,&security,4096)) throw winrt::hresult_error(HRESULT_FROM_WIN32(GetLastError()));
        unique_handle input_child(in_read),input(in_write);SetHandleInformation(output.get(),HANDLE_FLAG_INHERIT,0);SetHandleInformation(input.get(),HANDLE_FLAG_INHERIT,0);
        unique_handle error(CreateFileW(log.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));if(!error) throw winrt::hresult_error(HRESULT_FROM_WIN32(GetLastError()));
        std::array<HANDLE,3> handles{input_child.get(),output_child.get(),error.get()};
        std::wstring command=quote_command_arg(executable)+L" -hide_banner -loglevel warning -y -progress pipe:1 -nostats";for(auto const& argument:arguments) command+=L" "+quote_command_arg(argument);if(command.size()>30000) throw winrt::hresult_error(E_INVALIDARG);
        unique_handle job(CreateJobObjectW(nullptr,nullptr));JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;if(!job||!SetInformationJobObject(job.get(),JobObjectExtendedLimitInformation,&limits,sizeof limits)) throw winrt::hresult_error(HRESULT_FROM_WIN32(GetLastError()));
        // Assign at creation: a parent crash between CreateProcess and a later
        // AssignProcessToJobObject otherwise leaves a suspended orphan forever.
        AttributeList attributes(handles,job.get());
        STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof startup;startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;startup.StartupInfo.wShowWindow=SW_HIDE;
        startup.StartupInfo.hStdInput=input_child.get();startup.StartupInfo.hStdOutput=output_child.get();startup.StartupInfo.hStdError=error.get();startup.lpAttributeList=attributes.get();
        PROCESS_INFORMATION information{};if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,nullptr,g_app.directory,&startup.StartupInfo,&information)) throw winrt::hresult_error(HRESULT_FROM_WIN32(GetLastError()));
        unique_handle process(information.hProcess),thread(information.hThread);
        if(control.cancel||control.stop){result.error=HRESULT_FROM_WIN32(ERROR_CANCELLED);return result;}
        if(control.started)control.started();
        if(ResumeThread(thread.get())==DWORD(-1)) throw winrt::hresult_error(HRESULT_FROM_WIN32(GetLastError()));input_child.reset();output_child.reset();error.reset();
        bool requested_stop=false;ULONGLONG stopped=0,began=GetTickCount64();std::string pending;std::array<char,4096> bytes;
        auto drain=[&] {
            DWORD available=0;
            while(PeekNamedPipe(output.get(),nullptr,0,nullptr,&available,nullptr)&&available) {
                DWORD read=0;if(!ReadFile(output.get(),bytes.data(),std::min<DWORD>(DWORD(bytes.size()),available),&read,nullptr)||!read) break;pending.append(bytes.data(),read);
                for(;;) { auto end=pending.find('\n');if(end==std::string::npos) break;std::string line=pending.substr(0,end);pending.erase(0,end+1);auto equal=line.find('=');if(equal==std::string::npos) continue;auto key=std::string_view(line).substr(0,equal),value=std::string_view(line).substr(equal+1);if(key=="out_time_us") result.progress.microseconds=number(value);else if(key=="frame") result.progress.frames=number(value);else if(key=="progress"&&notify) notify(result.progress); }
                if(pending.size()>16384) pending.clear();
            }
        };
        for(;;) {
            if(control.heartbeat)control.heartbeat();
            drain();if(WaitForSingleObject(process.get(),100)==WAIT_OBJECT_0) break;
            if(control.cancel) { TerminateJobObject(job.get(),ERROR_CANCELLED);WaitForSingleObject(process.get(),2000);result.error=HRESULT_FROM_WIN32(ERROR_CANCELLED);return result; }
            if(control.timeout_ms&&GetTickCount64()-began>control.timeout_ms) { TerminateJobObject(job.get(),ERROR_TIMEOUT);result.error=HRESULT_FROM_WIN32(ERROR_TIMEOUT);return result; }
            if(control.stop&&!requested_stop) { DWORD written=0;WriteFile(input.get(),"q\n",2,&written,nullptr);requested_stop=true;stopped=GetTickCount64();input.reset(); }
            if(requested_stop&&GetTickCount64()-stopped>7000) { TerminateJobObject(job.get(),ERROR_TIMEOUT);result.error=HRESULT_FROM_WIN32(ERROR_TIMEOUT);return result; }
        }
        drain();GetExitCodeProcess(process.get(),&result.exit_code);result.error=result.exit_code?HRESULT_FROM_WIN32(ERROR_BAD_FORMAT):S_OK;return result;
    }catch(winrt::hresult_error const& error) { result.error=error.code();return result; }
     catch(...) { result.error=E_FAIL;return result; }
}
}
