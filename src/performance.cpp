#include "lb.h"
#include "performance.hpp"
#include <psapi.h>
#include <map>
#include <sstream>
#include <filesystem>

namespace lb::performance {
namespace {
struct Usage {uint64_t private_bytes{},working_set{},cpu_100ns{};DWORD handles{},gdi{},user{};};
Usage usage(HANDLE process){
    PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof memory;
    GetProcessMemoryInfo(process,reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof memory);
    FILETIME created{},exit{},kernel{},user{};GetProcessTimes(process,&created,&exit,&kernel,&user);
    auto value=[](FILETIME f){return (uint64_t(f.dwHighDateTime)<<32)|f.dwLowDateTime;};
    Usage result{memory.PrivateUsage,memory.WorkingSetSize,value(kernel)+value(user)};
    GetProcessHandleCount(process,&result.handles);result.gdi=GetGuiResources(process,GR_GDIOBJECTS);result.user=GetGuiResources(process,GR_USEROBJECTS);return result;
}
struct Sample {uint64_t ms{};int phase{};Usage parent,total;unsigned children{};};
std::wstring destination;std::vector<Sample> samples;std::map<DWORD,uint64_t> child_cpu;
ULONGLONG began{},phase_start{},ready_at{};uint64_t parent_cpu_baseline{};int phase{};unsigned ready_count{},failures{};bool requested{},closing{};
constexpr wchar_t const* media[]={L"night-drive.jpg",L"startrail.gif",L"motion.mp4"};
void report(){
    uint64_t peak=0,parent_peak=0,total_cpu=0;unsigned max_children=0;
    for(auto const& sample:samples){peak=std::max(peak,sample.total.private_bytes);parent_peak=std::max(parent_peak,sample.parent.private_bytes);max_children=std::max(max_children,sample.children);}
    for(auto const& item:child_cpu)total_cpu+=item.second;
    auto final=usage(GetCurrentProcess());total_cpu+=final.cpu_100ns-parent_cpu_baseline;
    std::wostringstream out;out<<L"{\"scope\":\"Isolated Qt application/tray baseline without Main settings window, plus native image/GIF/MF video workers; no desktop takeover, audio, GPU-composition or real-user UI measurements\",\"ready_switches\":"<<ready_count<<L",\"failures\":"<<failures<<L",\"peak_total_private_bytes\":"<<peak<<L",\"peak_parent_private_bytes\":"<<parent_peak<<L",\"max_children\":"<<max_children<<L",\"remaining_children\":"<<(unsigned(bool(g_app.player.process))+unsigned(bool(g_app.wallpaper.process)))<<L",\"sampled_cpu_seconds\":"<<double(total_cpu)/1e7<<L",\"elapsed_ms\":"<<GetTickCount64()-began<<L",\"samples\":[";
    bool comma=false;for(auto const& s:samples){if(comma)out<<L",";comma=true;out<<L"{\"ms\":"<<s.ms<<L",\"phase\":"<<s.phase<<L",\"parent_private\":"<<s.parent.private_bytes<<L",\"total_private\":"<<s.total.private_bytes<<L",\"total_working_set\":"<<s.total.working_set<<L",\"children\":"<<s.children<<L",\"handles\":"<<s.total.handles<<L",\"gdi\":"<<s.total.gdi<<L",\"user\":"<<s.total.user<<L"}";}
    out<<L"]}\n";if(!write_utf8_atomic(destination.c_str(),out.str().c_str()))++failures;
    KillTimer(g_app.window,23);RemovePropW(g_app.window,L"LB.HiddenProbe");PostMessageW(g_app.window,WM_COMMAND,900,0);
}
}
void start(wchar_t const* report_path){destination=report_path;parent_cpu_baseline=usage(GetCurrentProcess()).cpu_100ns;began=phase_start=GetTickCount64();phase=-1;SetPropW(g_app.window,L"LB.HiddenProbe",HANDLE(1));SetTimer(g_app.window,23,250,nullptr);}
void tick(){
    auto now=GetTickCount64();Sample sample;sample.ms=now-began;sample.phase=phase;sample.parent=sample.total=usage(GetCurrentProcess());
    for(auto* p:{&g_app.player,&g_app.wallpaper})if(p->process){auto child=usage(p->process.get());child_cpu[p->pid]=std::max(child_cpu[p->pid],child.cpu_100ns);sample.total.private_bytes+=child.private_bytes;sample.total.working_set+=child.working_set;sample.total.handles+=child.handles;sample.total.gdi+=child.gdi;sample.total.user+=child.user;++sample.children;}
    samples.push_back(sample);
    if(now-began>110000){if(!closing){++failures;player_stop(TRUE);closing=true;}if(!g_app.player.process&&!g_app.wallpaper.process)report();return;}
    if(phase<0){if(now-phase_start<3000)return;phase=0;phase_start=now;}
    if(phase>=12){player_stop_preview();if(g_app.player.process)return;if(!closing){closing=true;phase_start=now;}if(now-phase_start>=3000)report();return;}
    if(!requested){wchar_t path[LB_PATH];auto full=std::filesystem::path(g_app.directory)/L"assets"/media[phase%3];StringCchCopyW(path,LB_PATH,full.c_str());player_preview_embed(path,g_app.window);requested=true;ready_at=0;phase_start=now;return;}
    if(g_app.player.failed||now-phase_start>18000){++failures;phase=12;player_stop_preview();return;}
    if(g_app.player.ready&&!g_app.player.pending&&!g_app.player.closing){if(!ready_at){ready_at=now;++ready_count;}if(now-ready_at>=3000){++phase;requested=false;}}
}
}
