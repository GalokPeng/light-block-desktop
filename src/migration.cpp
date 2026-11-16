#include "migration.hpp"
#include "lb.h"
#include "json.h"
#include "network.h"
#include "sqlite.hpp"
#include <map>
#include <set>
#include <fstream>

namespace lb::migration {
namespace {
namespace fs=std::filesystem;
constexpr int64_t epoch_ms=11644473600000LL;
void require(bool value,char const* error) { if(!value) throw std::runtime_error(error); }
std::wstring lower(std::wstring value) { if(!value.empty())CharLowerBuffW(value.data(),DWORD(value.size()));return value; }
std::wstring quote(std::wstring_view value) { std::wstring out=L"\"";for(auto c:value) { if(c=='"'||c=='\\'){out+=L'\\';out+=c;}else if(c=='\n')out+=L"\\n";else if(c=='\r')out+=L"\\r";else if(c=='\t')out+=L"\\t";else { require(c>=32,"Control character in migration metadata");out+=c;} }return out+L'"'; }
std::wstring str(Json const& j,int object,char const* key,std::wstring fallback={}) { int t=json_property(&j,object,key);if(t<0||j.tokens[t].type==JSON_NULL)return fallback;require(j.tokens[t].type==JSON_STRING,"Invalid string setting");std::vector<wchar_t> text(size_t(j.tokens[t].end-j.tokens[t].start)+2);require(json_string(&j,t,text.data(),text.size()),"Invalid UTF-8 setting");return text.data(); }
int64_t number(Json const& j,int object,char const* key,int64_t fallback) { int t=json_property(&j,object,key);if(t<0||j.tokens[t].type==JSON_NULL)return fallback;require(j.tokens[t].type==JSON_NUMBER,"Invalid integer setting");return json_integer(&j,t,fallback); }
bool flag(Json const& j,int object,char const* key,bool fallback=false) { int t=json_property(&j,object,key);if(t<0||j.tokens[t].type==JSON_NULL)return fallback;require(j.tokens[t].type==JSON_TRUE||j.tokens[t].type==JSON_FALSE,"Invalid boolean setting");return j.tokens[t].type==JSON_TRUE; }
std::vector<int> array(Json const& j,int object,char const* key) { int t=json_property(&j,object,key);if(t<0)return {};require(j.tokens[t].type==JSON_ARRAY,"Invalid list in old profile");std::vector<int> result;for(int n=t+1;n<j.tokens[t].next;n=j.tokens[n].next)result.push_back(n);return result; }
Json read_json(fs::path const& file,DWORD limit=8*1024*1024) { Json j;require(json_file(&j,file.c_str(),limit)&&j.count&&j.tokens[0].type==JSON_OBJECT,"Invalid or oversized JSON in old profile");return j; }
void no_link(fs::path const& path) { auto absolute=fs::absolute(path).lexically_normal();for(auto current=absolute;!current.empty();current=current.parent_path()) { DWORD a=GetFileAttributesW(current.c_str());if(a!=INVALID_FILE_ATTRIBUTES)require(!(a&FILE_ATTRIBUTE_REPARSE_POINT),"Migration refuses filesystem junctions or symbolic links");if(current==current.parent_path())break; } }
bool beneath(fs::path const& path,fs::path const& root) { auto a=lower(fs::absolute(path).lexically_normal().wstring()),b=lower(fs::absolute(root).lexically_normal().wstring());if(!b.empty()&&b.back()!=L'\\')b+=L'\\';return a.starts_with(b); }
std::wstring digest(fs::path const& file) { wchar_t out[65]{};require(hash_file(file.c_str(),L"SHA256",out,65),"Cannot checksum migration file");return out; }
std::wstring digest_text(std::wstring const& text) { auto utf8=sql::utf8(text);wchar_t out[65]{};require(hash_memory(utf8.data(),utf8.size(),L"SHA256",out,65),"Cannot checksum migration identity");return out; }
void write_bytes(fs::path const& file,void const* data,size_t size) { require(size<=MAXDWORD,"Migration output is too large");fs::create_directories(file.parent_path());unique_handle handle(CreateFileW(file.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));require(bool(handle),"Cannot create migration output");DWORD written{};require(WriteFile(handle.get(),data,DWORD(size),&written,nullptr)&&written==size&&FlushFileBuffers(handle.get()),"Cannot flush migration output"); }
void write(fs::path const& file,std::wstring const& text) { auto bytes=sql::utf8(text);write_bytes(file,bytes.data(),bytes.size()); }
void copy_checked(fs::path const& source,fs::path const& destination) { no_link(source);require(fs::is_regular_file(source),"A migration source file is missing");fs::create_directories(destination.parent_path());require(CopyFileW(source.c_str(),destination.c_str(),TRUE),"Cannot copy old profile file (disk space or permissions)");require(digest(source)==digest(destination),"Copied file changed or failed checksum"); }
COLORREF color(std::wstring value,COLORREF fallback) { static std::map<std::wstring,std::wstring> named={{L"mint",L"#a6e4ca"},{L"blue",L"#9ac8ff"},{L"violet",L"#cbb7f7"},{L"amber",L"#f5d18e"},{L"rose",L"#efb5c5"}};if(named.contains(value))value=named[value];if(value.empty())return fallback;require(value.size()==7&&value[0]=='#'&&std::all_of(value.begin()+1,value.end(),[](auto c){return iswxdigit(c);}),"Invalid legacy color");auto n=wcstoul(value.c_str()+1,nullptr,16);return RGB((n>>16)&255,(n>>8)&255,n&255); }
int choice(std::wstring const& value,std::initializer_list<wchar_t const*> values,int fallback=0) { int n=0;for(auto item:values) { if(value==item)return n;++n; }return fallback; }
int monitor(std::wstring const& name) { struct Search { std::wstring name;int count{},found{}; } search{name};if(!name.empty())EnumDisplayMonitors(nullptr,nullptr,+[](HMONITOR m,HDC,LPRECT,LPARAM p)->BOOL{auto &s=*reinterpret_cast<Search*>(p);MONITORINFOEXW info{};info.cbSize=sizeof info;++s.count;if(GetMonitorInfoW(m,&info)&&!_wcsicmp(info.szDevice,s.name.c_str()))s.found=s.count;return TRUE;},LPARAM(&search));return search.found; }
int64_t timestamp(std::wstring const& text,bool nullable=true) {
    if(text.empty()&&nullable)return 0;require(text.size()>=19,"Invalid legacy date");SYSTEMTIME t{};unsigned year{},month{},day{},hour{},minute{},second{};require(swscanf(text.c_str(),L"%u-%u-%uT%u:%u:%u",&year,&month,&day,&hour,&minute,&second)==6,"Invalid legacy date");require(year>=1601&&year<=9999&&month>=1&&month<=12&&day>=1&&day<=31&&hour<24&&minute<60&&second<60,"Invalid legacy date fields");t.wYear=WORD(year);t.wMonth=WORD(month);t.wDay=WORD(day);t.wHour=WORD(hour);t.wMinute=WORD(minute);t.wSecond=WORD(second);size_t p=19;if(p<text.size()&&text[p]=='.') { ++p;int fraction=0,digits=0;while(p<text.size()&&iswdigit(text[p])) { if(digits<3)fraction=fraction*10+(text[p]-'0');++digits;++p; }while(digits++<3)fraction*=10;t.wMilliseconds=WORD(fraction); }
    int offset=0;if(p<text.size()&&(text[p]=='+'||text[p]=='-')) { unsigned h{},m{};require(swscanf(text.c_str()+p+1,L"%u:%u",&h,&m)==2&&h<=23&&m<=59,"Invalid legacy timezone");offset=int(h*60+m)*(text[p]=='-'?-1:1); }else require(p==text.size()||text[p]=='Z',"Invalid legacy timezone");FILETIME ft{};require(SystemTimeToFileTime(&t,&ft),"Invalid legacy date");return int64_t((uint64_t(ft.dwHighDateTime)<<32)|ft.dwLowDateTime)/10000-int64_t(offset)*60000;
}
// Source connections are opened read-only. SQLite backup includes committed WAL
// pages without checkpointing or changing the Python database.
struct sqlite3;struct sqlite3_stmt;struct sqlite3_backup;
struct LegacySql {
    HMODULE module{};sqlite3* db{};
    int(*open)(char const*,sqlite3**,int,char const*){};int(*close)(sqlite3*){};
    int(*exec_)(sqlite3*,char const*,int(*)(void*,int,char**,char**),void*,char**){};
    int(*prepare)(sqlite3*,char const*,int,sqlite3_stmt**,char const**){};int(*step)(sqlite3_stmt*){};int(*finalize)(sqlite3_stmt*){};
    int64_t(*integer_)(sqlite3_stmt*,int){};unsigned char const*(*text_)(sqlite3_stmt*,int){};void const*(*blob_)(sqlite3_stmt*,int){};int(*bytes_)(sqlite3_stmt*,int){};
    sqlite3_backup*(*backup_init)(sqlite3*,char const*,sqlite3*,char const*){};int(*backup_step)(sqlite3_backup*,int){};int(*backup_finish)(sqlite3_backup*){};
    explicit LegacySql(fs::path const& path,bool writable=false) {
        module=LoadLibraryExW(L"winsqlite3.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);require(module,"System SQLite unavailable");
        try {
#define LOAD(field,name) field=reinterpret_cast<decltype(field)>(GetProcAddress(module,name));require(field,"SQLite API unavailable")
        LOAD(open,"sqlite3_open_v2");LOAD(close,"sqlite3_close_v2");LOAD(exec_,"sqlite3_exec");LOAD(prepare,"sqlite3_prepare_v2");LOAD(step,"sqlite3_step");LOAD(finalize,"sqlite3_finalize");LOAD(integer_,"sqlite3_column_int64");LOAD(text_,"sqlite3_column_text");LOAD(blob_,"sqlite3_column_blob");LOAD(bytes_,"sqlite3_column_bytes");LOAD(backup_init,"sqlite3_backup_init");LOAD(backup_step,"sqlite3_backup_step");LOAD(backup_finish,"sqlite3_backup_finish");
#undef LOAD
        require(open(sql::utf8(path.wstring()).c_str(),&db,writable?6:1,nullptr)==0,"Cannot open legacy SQLite database");
        }catch(...) { if(db)close(db);FreeLibrary(module);throw; }
    }
    ~LegacySql(){if(db)close(db);if(module)FreeLibrary(module);}
    void exec(char const* query){require(exec_(db,query,nullptr,nullptr,nullptr)==0,"Legacy SQLite statement failed");}
    struct Rows { LegacySql& sql;sqlite3_stmt* stmt{};Rows(LegacySql& s,char const* q):sql(s){require(sql.prepare(sql.db,q,-1,&stmt,nullptr)==0,"Legacy database schema is incomplete");}~Rows(){if(stmt)sql.finalize(stmt);}bool next(){int n=sql.step(stmt);require(n==100||n==101,"Cannot read legacy database row");return n==100;}int64_t integer(int c){return sql.integer_(stmt,c);}std::wstring text(int c){auto p=sql.text_(stmt,c);int n=sql.bytes_(stmt,c);require(n>=0&&n<=8*1024*1024,"Oversized legacy database value");return p?sql::wide(std::string_view(reinterpret_cast<char const*>(p),size_t(n))):L"";}std::vector<BYTE> blob(int c){int n=sql.bytes_(stmt,c);require(n>=0&&n<=5*1024*1024,"Oversized legacy clipboard image");auto p=static_cast<BYTE const*>(sql.blob_(stmt,c));return p?std::vector<BYTE>(p,p+n):std::vector<BYTE>{};} };
    bool table(char const* name){std::string query="SELECT name FROM sqlite_master WHERE type='table' AND name='"+std::string(name)+"'";Rows row(*this,query.c_str());return row.next();}
    void validate(){Rows row(*this,"PRAGMA quick_check");require(row.next()&&row.text(0)==L"ok","Legacy database integrity check failed");}
    void backup(fs::path const& destination){LegacySql output(destination,true);auto b=backup_init(output.db,"main",db,"main");require(b,"Cannot create database backup");int r=backup_step(b,-1),f=backup_finish(b);require(r==101&&f==0,"Cannot finish consistent database backup");output.validate();}
};
struct Ini {
    std::map<std::wstring,std::map<std::wstring,std::wstring>> sections;
    void put(wchar_t const* section,wchar_t const* key,int64_t value){sections[section][key]=std::to_wstring(value);}
    void put(wchar_t const* section,wchar_t const* key,std::wstring value){require(value.find_first_of(L"\r\n\0",0,3)==std::wstring::npos,"Invalid multiline INI value");sections[section][key]=std::move(value);}
    void save(fs::path const& path,bool utf16=true){std::wstring text;for(auto const& [section,values]:sections){text+=L"["+section+L"]\r\n";for(auto const& [key,value]:values)text+=key+L"="+value+L"\r\n";}if(utf16){text.insert(text.begin(),wchar_t(0xfeff));write_bytes(path,text.data(),text.size()*sizeof(wchar_t));}else write(path,text);}
};
struct Context {
    fs::path source,stage,destination,assets;Json settings;Ini ini;
    std::map<std::wstring,std::wstring> paths,groups;std::vector<std::wstring> files;std::vector<std::wstring> warnings;
    Context(fs::path s,fs::path t,fs::path d,fs::path a):source(std::move(s)),stage(std::move(t)),destination(std::move(d)),assets(std::move(a)),settings(read_json(source/L"settings.json")){}
    void backup(fs::path const& relative){auto file=source/relative;if(fs::exists(file)){copy_checked(file,stage/L"legacy-backup"/relative);files.push_back(relative.wstring());}}
    std::wstring wallpaper(std::wstring id) { if(id==L"builtin:dunes")id=L"builtin:nightdrive";if(id==L"builtin:mountain")id=L"builtin:mountainbay";if(id==L"builtin:glow")id=L"builtin:milkyway";auto found=paths.find(id);return found==paths.end()?L"":found->second; }
};
void settings(Context& c) {
    auto &j=c.settings;auto &ini=c.ini;
    ini.put(L"appearance",L"dark",str(j,0,"settings_theme",L"dark")!=L"light");ini.put(L"appearance",L"language",choice(str(j,0,"settings_language"),{L"system",L"zh",L"en"}));
    ini.put(L"wallpaper",L"fit",choice(str(j,0,"fit"),{L"fill",L"fit",L"stretch",L"center"}));ini.put(L"wallpaper",L"monitor",monitor(str(j,0,"screen")));
    for(auto const& field:{"fullscreen_pause","battery_pause","restore_on_start"})ini.put(L"wallpaper",sql::wide(field).c_str(),flag(j,0,field,true));
    for(auto const& field:{"organizer_enabled","organizer_auto_collect","organizer_glass"})ini.put(L"organizer",sql::wide(field).c_str(),flag(j,0,field,std::string_view(field)!="organizer_enabled"));
    auto alignment=str(j,0,"organizer_alignment",L"bottom");ini.put(L"organizer",L"organizer_alignment",choice(alignment,{L"top",L"center",L"bottom"},2));int views=json_property(&j,0,"organizer_views");ini.put(L"organizer",L"organizer_view",choice(views<0?L"name":str(j,views,sql::utf8(alignment).c_str(),L"name"),{L"name",L"time",L"groups"}));
    for(auto mode:{L"top",L"center",L"bottom"}){auto key=L"view_"+std::wstring(mode);ini.put(L"organizer",key.c_str(),choice(views<0?L"name":str(j,views,sql::utf8(mode).c_str(),L"name"),{L"name",L"time",L"groups"}));}
    int size=int(number(j,0,"organizer_icon_size",44));ini.put(L"organizer",L"organizer_icon_size",size==32?0:size==60?2:1);ini.put(L"organizer",L"organizer_opacity",std::clamp<int64_t>(number(j,0,"organizer_opacity",165),30,255));
    int p=json_property(&j,0,"organizer_position");if(p>=0&&j.tokens[p].type==JSON_ARRAY){auto a=array(j,0,"organizer_position");require(a.size()==2,"Invalid organizer position");ini.put(L"organizer",L"organizer_custom_position",1);ini.put(L"organizer",L"organizer_x",std::clamp<int64_t>(json_integer(&j,a[0],0),-32768,32767));ini.put(L"organizer",L"organizer_y",std::clamp<int64_t>(json_integer(&j,a[1],0),-32768,32767));}
    ini.put(L"taskbar",L"taskbar_enabled",flag(j,0,"taskbar_style_enabled"));ini.put(L"taskbar",L"taskbar_radius",std::clamp<int64_t>(number(j,0,"taskbar_radius",22),6,26));ini.put(L"taskbar",L"taskbar_opacity",std::clamp<int64_t>(number(j,0,"taskbar_opacity",79),0,100));
    auto theme=str(j,0,"cursor_theme");if(theme==L"frost")theme=L"stream";if(theme==L"graphite")theme=L"outline";if(theme==L"mint")theme=L"pixel";ini.put(L"pointer",L"cursor_theme",choice(theme,{L"system",L"stream",L"outline",L"pixel"}));ini.put(L"pointer",L"cursor_scale",std::clamp<int64_t>(number(j,0,"cursor_scale",100),75,200));ini.put(L"pointer",L"cursor_particles",flag(j,0,"cursor_particles"));ini.put(L"pointer",L"particle_style",choice(str(j,0,"particle_style"),{L"dots",L"fall",L"glow"}));
    for(auto const& field:{"clipboard_enabled","notes_background"})ini.put(L"tools",sql::wide(field).c_str(),flag(j,0,field));
    ini.put(L"capture",L"capture_enabled",flag(j,0,"capture_hotkey",true));for(auto const& field:{"record_resolution","record_fps"})ini.put(L"capture",sql::wide(field).c_str(),number(j,0,field,0));ini.put(L"capture",L"record_lossless",flag(j,0,"record_lossless"));
    auto hotkey=lower(str(j,0,"capture_shortcut",L"Ctrl+Alt+A"));int mods=0,key=0;size_t start=0;while(start<=hotkey.size()){size_t end=hotkey.find(L'+',start);if(end==std::wstring::npos)end=hotkey.size();auto part=hotkey.substr(start,end-start);if(part==L"ctrl"||part==L"control")mods|=MOD_CONTROL;else if(part==L"alt")mods|=MOD_ALT;else if(part==L"shift")mods|=MOD_SHIFT;else if(part==L"win"||part==L"meta")mods|=MOD_WIN;else if(part.size()==1&&iswalnum(part[0]))key=towupper(part[0]);else if(part.starts_with(L"f")&&part.size()<=3){int f=_wtoi(part.c_str()+1);if(f>=1&&f<=24)key=VK_F1+f-1;}else if(part==L"print"||part==L"printscreen")key=VK_SNAPSHOT;else if(part==L"space")key=VK_SPACE;else if(part==L"insert")key=VK_INSERT;else if(part==L"home")key=VK_HOME;else if(part==L"end")key=VK_END;if(end==hotkey.size())break;start=end+1;}require(key!=0,"The old screenshot shortcut cannot be converted safely");ini.put(L"capture",L"capture_modifiers",mods);ini.put(L"capture",L"capture_key",key);
    struct Color{char const* from;wchar_t const* to;COLORREF initial;};for(auto f:{Color{"organizer_background_color",L"organizer_color",RGB(29,38,49)},Color{"organizer_icon_color",L"icon_color",RGB(66,83,100)},Color{"taskbar_color",L"taskbar_color",RGB(38,48,56)},Color{"cursor_color",L"cursor_color",RGB(217,242,237)},Color{"particle_color",L"particle_color",RGB(166,228,202)}})ini.put(L"colors",f.to,color(str(j,0,f.from),f.initial));
}
void library(Context& c) {
    std::pair<wchar_t const*,wchar_t const*> builtins[]={{L"builtin:nightdrive",L"night-drive.jpg"},{L"builtin:milkyway",L"milky-way.jpg"},{L"builtin:mountainbay",L"mountain-bay.jpg"},{L"builtin:raincoast",L"rain-coast.jpg"},{L"builtin:motion",L"motion.mp4"},{L"builtin:ripple",L"startrail.gif"}};for(auto [id,file]:builtins)c.paths[id]=(c.assets/file).wstring();
    std::wstring manifest=L"{\"schema\":1,\"entries\":[";bool first=true;std::set<std::wstring> identifiers;size_t count=0;
    for(int item:array(c.settings,0,"library")){require(++count<=10000,"Legacy library exceeds the bounded migration capacity");auto id=str(c.settings,item,"id"),name=str(c.settings,item,"name"),path=str(c.settings,item,"path");require(!id.empty()&&identifiers.insert(id).second,"Duplicate or missing wallpaper identity");fs::path source(path);if(source.is_relative()){source=c.source/source;require(beneath(source,c.source/L"wallpapers"),"Wallpaper path escapes managed directory");}source=fs::absolute(source).lexically_normal();no_link(source);require(media_kind(source.c_str())!=MEDIA_UNKNOWN,"Unsupported wallpaper file type");if(!fs::exists(source)){c.warnings.push_back(L"Missing wallpaper retained in backup: "+name);continue;}auto filename=L"import-"+digest_text(id).substr(0,16)+source.extension().wstring();copy_checked(source,c.stage/L"media"/filename);auto final=(c.destination/L"media"/filename).wstring();c.paths[id]=final;if(!first)manifest+=L",";first=false;manifest+=L"{\"id\":"+quote(id)+L",\"title\":"+quote(name)+L",\"path\":"+quote(final)+L",\"original_path\":"+quote(source.wstring())+L"}";
        auto origin=source.parent_path()/L"origin.json";if(fs::exists(origin))copy_checked(origin,c.stage/L"legacy-backup"/L"wallpaper-origins"/(filename+L".json"));
    }
    write(c.stage/L"migrated-library.json",manifest+L"]}\n");write(c.stage/L"library.json",L"{\"schema\":1,\"hidden_builtins\":0}\n");c.ini.put(L"wallpaper",L"active_path",c.wallpaper(str(c.settings,0,"active_id")));
}
void collections(Context& c) {
    Json manifest;Json const* j=&c.settings;char const* group_key="organizer_groups",*loose_key="organizer_loose",*pending_key="organizer_pending";
    if(fs::exists(c.source/L"collections.json")){manifest=read_json(c.source/L"collections.json");j=&manifest;group_key="groups";loose_key="loose";pending_key="pending";require(json_property(j,0,"groups")>=0&&json_property(j,0,"pending")>=0,"Incomplete legacy collections manifest");}
    require(array(*j,0,pending_key).empty(),"Unfinished desktop file operation: reopen the old version to recover before upgrading");
    std::wstring groups,entries;std::set<std::wstring> paths,ids;size_t total=0;auto add_entry=[&](Json const& source,int n,std::wstring const& group,bool loose){auto path=str(source,n,"target"),name=str(source,n,"name");require(!path.empty(),"Missing organizer target");bool system=path==L"shell:MyComputerFolder"||path==L"shell:RecycleBinFolder"||path==L"ms-settings:";require(system||fs::path(path).is_absolute(),"Invalid organizer target");if(!paths.insert(lower(path)).second)return;require(++total<=10000,"Too many organizer entries");if(!entries.empty())entries+=L",";entries+=L"{\"path\":"+quote(path)+L",\"name\":"+quote(name)+L",\"group\":"+quote(group)+L",\"system\":"+(system?L"true":L"false")+L",\"external\":"+((flag(source,n,"managed")||!flag(source,n,"desktop_auto"))?L"true":L"false")+L",\"loose\":"+(loose?L"true":L"false")+L"}";};
    for(int g:array(*j,0,group_key)) {
        auto original=str(*j,g,"id"),id=original,name=str(*j,g,"name"),category=str(*j,g,"auto_category");
        require(!id.empty()&&id.size()<=100&&name.size()<=120&&!c.groups.contains(original),"Invalid organizer group");
        static std::set<std::wstring> categories={L"system",L"folders",L"shortcuts",L"documents",L"images",L"media",L"archives",L"other"};
        bool automatic=categories.contains(category)&&!ids.contains(category);if(automatic)id=category;
        require(ids.insert(id).second,"Duplicate organizer group");c.groups[original]=id;
        if(!groups.empty())groups+=L",";
        groups+=L"{\"id\":"+quote(id)+L",\"name\":"+quote(name)+L",\"color\":"+std::to_wstring(color(str(*j,g,"color",L"mint"),RGB(166,228,202)))+L",\"automatic\":"+(automatic?L"true":L"false")+L"}";
        for(int e:array(*j,g,"entries"))add_entry(*j,e,id,false);
    }
    // Required classification destinations are added without replacing user's
    // custom UUID group ids; automatic category IDs are mapped in workspaces.
    std::pair<wchar_t const*,wchar_t const*> defaults[]={{L"system",L"系统图标"},{L"folders",L"文件夹"},{L"shortcuts",L"快捷方式"},{L"documents",L"文档与表格"},{L"images",L"图片与设计"},{L"media",L"视频与音频"},{L"archives",L"压缩包与安装包"},{L"other",L"其他文件"}};
    for(auto [id,name]:defaults)if(!ids.contains(id)){ids.insert(id);if(!groups.empty())groups+=L",";groups+=L"{\"id\":"+quote(id)+L",\"name\":"+quote(name)+L",\"color\":13296806,\"automatic\":true}";}
    require(ids.size()<=128,"Too many organizer groups");for(int e:array(*j,0,loose_key)){
        auto target=str(*j,e,"target");std::wstring group=L"other";DWORD attributes=GetFileAttributesW(target.c_str());if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_DIRECTORY))group=L"folders";else{
            auto extension=lower(fs::path(target).extension().wstring());std::pair<wchar_t const*,wchar_t const*> types[]={{L"shortcuts",L"|.lnk|.url|.appref-ms|.website|.exe|"},{L"documents",L"|.txt|.md|.pdf|.doc|.docx|.xls|.xlsx|.csv|.ppt|.pptx|.rtf|.odt|"},{L"images",L"|.png|.jpg|.jpeg|.webp|.gif|.bmp|.svg|.tif|.tiff|.ico|.psd|.ai|.heic|"},{L"media",L"|.mp4|.mkv|.mov|.avi|.webm|.m4v|.mp3|.wav|.flac|.m4a|.ogg|"},{L"archives",L"|.zip|.7z|.rar|.gz|.tar|.msi|.msix|.iso|"}};for(auto [id,list]:types)if(!extension.empty()&&std::wstring_view(list).find(L"|"+extension+L"|")!=std::wstring_view::npos){group=id;break;}
        }add_entry(*j,e,group,true);
    }
    auto receipts=c.source/L"organizer_files"/L".receipts";if(fs::exists(receipts)){no_link(receipts);for(auto const& f:fs::directory_iterator(receipts))if(f.path().extension()==L".json"){c.backup(fs::relative(f.path(),c.source));auto r=read_json(f.path());int e=json_property(&r,0,"entry"),g=json_property(&r,0,"group");require(e>=0&&g>=0,"Invalid managed-file receipt");auto target=str(r,e,"target");if(!fs::exists(target))continue;require(paths.contains(lower(target)),"An orphaned managed file needs recovery in the old version before upgrade");}}
    write(c.stage/L"organizer.json",L"{\"schema\":1,\"groups\":["+groups+L"],\"entries\":["+entries+L"]}\n");
}
void notes(Context& c,LegacySql& old) {
    if(!old.table("notes"))return;LegacySql::Rows rows(old,"SELECT id,title,body,color,done,due,notified,pinned,updated,archived,pin_x,pin_y,pin_w,pin_h FROM notes ORDER BY updated,id");size_t count=0;
    while(rows.next()){require(++count<=10000,"Too many legacy notes");auto id=rows.text(0),title=rows.text(1),body=rows.text(2);require(!id.empty()&&sql::utf8(body).size()<=1024*1024,"Invalid or oversized legacy note");auto file=c.stage/L"notes"/(L"note-"+digest_text(id).substr(0,24)+L".txt");write(file,body);Ini meta;meta.put(L"note",L"title",quote(title));meta.put(L"note",L"color",color(rows.text(3),RGB(166,228,202)));meta.put(L"note",L"done",rows.integer(4)!=0);meta.put(L"note",L"due",timestamp(rows.text(5))*10000);meta.put(L"note",L"notified",rows.integer(6)!=0);meta.put(L"note",L"pinned",rows.integer(7)!=0);meta.put(L"note",L"archived",rows.integer(9)!=0);meta.put(L"note",L"x",rows.integer(10));meta.put(L"note",L"y",rows.integer(11));meta.put(L"note",L"width",std::clamp<int64_t>(rows.integer(12),220,1400));meta.put(L"note",L"height",std::clamp<int64_t>(rows.integer(13),150,1400));meta.save(fs::path(file.wstring()+L".ini"),false);auto updated=timestamp(rows.text(8));if(updated){uint64_t value=uint64_t(updated)*10000;FILETIME ft{DWORD(value),DWORD(value>>32)};unique_handle h(CreateFileW(file.c_str(),FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));require(h&&SetFileTime(h.get(),nullptr,nullptr,&ft),"Cannot preserve note timestamp");}}
}
void clipboard(Context& c,LegacySql& old) {
    if(!old.table("clipboard"))return;fs::create_directories(c.stage/L"clipboard");sql::Database out(c.stage/L"clipboard"/L"history.sqlite3");out.exec("CREATE TABLE clipboard(id INTEGER PRIMARY KEY,kind INTEGER NOT NULL,text TEXT NOT NULL,payload BLOB NOT NULL,digest TEXT NOT NULL UNIQUE,created INTEGER NOT NULL,pinned INTEGER NOT NULL DEFAULT 0);CREATE INDEX clip_order ON clipboard(pinned,created)");sql::Transaction tx(out);LegacySql::Rows rows(old,"SELECT id,kind,text,payload,created,pinned FROM clipboard ORDER BY id");size_t count=0,bytes=0;
    while(rows.next()){require(++count<=1000,"Too many legacy clipboard entries");auto kind=rows.text(1),text=rows.text(2);require(kind==L"text"||kind==L"image","Unknown legacy clipboard kind");auto payload=rows.blob(3);std::vector<BYTE> content=payload;if(kind==L"text"){auto utf8=sql::utf8(text);content.assign(utf8.begin(),utf8.end());}require(content.size()<=5*1024*1024&&(bytes+=content.size())<=64*1024*1024,"Oversized legacy clipboard history");content.insert(content.begin(),kind==L"image"?1:0);wchar_t hash[65]{};require(hash_memory(content.data(),content.size(),L"SHA256",hash,65),"Cannot checksum clipboard history");sql::Statement insert(out,"INSERT INTO clipboard VALUES(?,?,?,?,?,?,?)");insert.bind(1,rows.integer(0));insert.bind(2,int64_t(kind==L"image"));insert.bind(3,text);insert.bind(4,payload);insert.bind(5,std::wstring(hash));insert.bind(6,timestamp(rows.text(4),false));insert.bind(7,rows.integer(5)!=0);insert.next();}tx.commit();out.exec("PRAGMA wal_checkpoint(TRUNCATE)");
}
void focus(Context& c,LegacySql& old) {
    sql::Database out(c.stage/L"focus.db");out.exec("CREATE TABLE focus_state(id INTEGER PRIMARY KEY CHECK(id=1),stage INTEGER NOT NULL,deadline INTEGER NOT NULL,rest INTEGER NOT NULL,pause INTEGER NOT NULL,saved_path TEXT NOT NULL,saved_pause INTEGER NOT NULL);CREATE TABLE focus_options(id INTEGER PRIMARY KEY CHECK(id=1),minutes INTEGER NOT NULL,session INTEGER NOT NULL);CREATE TABLE focus_sessions(id INTEGER PRIMARY KEY,started INTEGER NOT NULL,ended INTEGER NOT NULL DEFAULT 0,duration INTEGER NOT NULL DEFAULT 0,completed INTEGER NOT NULL DEFAULT 0);CREATE TABLE workspaces(id INTEGER PRIMARY KEY,name TEXT NOT NULL UNIQUE,path TEXT NOT NULL,fit INTEGER NOT NULL,monitor INTEGER NOT NULL,enabled INTEGER NOT NULL,alignment INTEGER NOT NULL,view INTEGER NOT NULL,size INTEGER NOT NULL,opacity INTEGER NOT NULL,glass INTEGER NOT NULL,color INTEGER NOT NULL,icon INTEGER NOT NULL,group_order TEXT NOT NULL);INSERT INTO focus_state VALUES(1,0,0,5,1,'',0);INSERT INTO focus_options VALUES(1,25,0)");sql::Transaction tx(out);
    if(old.table("focus_sessions")){LegacySql::Rows rows(old,"SELECT started,duration,deadline,state FROM focus_sessions ORDER BY started,id");size_t count=0;while(rows.next()){require(++count<=100000,"Too many focus sessions");auto start=timestamp(rows.text(0),false),seconds=rows.integer(1),end=timestamp(rows.text(2));auto state=rows.text(3);bool completed=state==L"completed"||state==L"resting";require(seconds>=0&&seconds<=86400,"Invalid focus duration");sql::Statement insert(out,"INSERT INTO focus_sessions(started,ended,duration,completed) VALUES(?,?,?,?)");insert.bind(1,start);insert.bind(2,end?end:start+seconds*1000);insert.bind(3,completed?seconds:0);insert.bind(4,completed);insert.next();if(state==L"running"||state==L"resting")c.warnings.push_back(L"An unfinished focus timer was stopped during upgrade; its history is retained.");}}
    if(old.table("profiles")){LegacySql::Rows rows(old,"SELECT id,name,data FROM profiles ORDER BY updated,id");std::set<std::wstring> names;size_t count=0;while(rows.next()){require(++count<=1000,"Too many workspace profiles");auto name=rows.text(1);auto data=sql::utf8(rows.text(2));Json j;require(json_parse(&j,data.data(),data.size())&&j.count&&j.tokens[0].type==JSON_OBJECT,"Invalid workspace profile");auto unique=name;for(int n=2;names.contains(unique);++n)unique=name+L" ("+std::to_wstring(n)+L")";names.insert(unique);std::wstring order;for(int t:array(j,0,"group_order")){std::vector<wchar_t> text(size_t(j.tokens[t].end-j.tokens[t].start)+2);require(json_string(&j,t,text.data(),text.size()),"Invalid workspace group order");if(!order.empty())order+=L'\n';auto mapped=c.groups.find(text.data());order+=mapped==c.groups.end()?std::wstring(text.data()):mapped->second;}
        sql::Statement insert(out,"INSERT INTO workspaces(name,path,fit,monitor,enabled,alignment,view,size,opacity,glass,color,icon,group_order) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)");insert.bind(1,unique);insert.bind(2,c.wallpaper(str(j,0,"wallpaper")));insert.bind(3,choice(str(j,0,"fit"),{L"fill",L"fit",L"stretch",L"center"}));insert.bind(4,monitor(str(j,0,"screen")));insert.bind(5,flag(j,0,"organizer_enabled"));int index=6;for(auto key:{L"organizer_alignment",L"organizer_view",L"organizer_icon_size",L"organizer_opacity",L"organizer_glass"})insert.bind(index++,int64_t(_wtoi(c.ini.sections[L"organizer"][key].c_str())));insert.bind(11,int64_t(_wtoi(c.ini.sections[L"colors"][L"organizer_color"].c_str())));insert.bind(12,int64_t(_wtoi(c.ini.sections[L"colors"][L"icon_color"].c_str())));insert.bind(13,order);insert.next();}}
    tx.commit();out.exec("PRAGMA wal_checkpoint(TRUNCATE)");
}
void online(Context& c) {
    auto catalog=c.assets/L"online-wallpapers.json";if(!fs::exists(catalog))return;auto j=read_json(catalog,2*1024*1024);
    for(int n:array(j,0,"entries")){auto id=str(j,n,"id"),filename=str(j,n,"filename"),sha1=str(j,n,"sha1");require(safe_filename(filename.c_str()),"Unsafe online catalog filename");auto expected=number(j,n,"bytes",0);auto match=std::find_if(c.paths.begin(),c.paths.end(),[&](auto const& pair){return pair.first==L"online:"+id||pair.first==id;});fs::path source;
        for(int item:array(c.settings,0,"library"))if(str(c.settings,item,"online_id")==id){auto old_id=str(c.settings,item,"id");auto found=c.paths.find(old_id);if(found!=c.paths.end())source=c.stage/L"media"/fs::path(found->second).filename();break;}
        if(source.empty()&&match!=c.paths.end())source=c.stage/L"media"/fs::path(match->second).filename();if(source.empty()||!fs::exists(source))continue;wchar_t actual[41]{};if(int64_t(fs::file_size(source))!=expected||!hash_file(source.c_str(),L"SHA1",actual,41)||_wcsicmp(actual,sha1.c_str())){c.warnings.push_back(L"Online receipt not trusted; local file retained: "+id);continue;}auto out=c.stage/L"online"/filename;copy_checked(source,out);WIN32_FILE_ATTRIBUTE_DATA info{};require(GetFileAttributesExW(out.c_str(),GetFileExInfoStandard,&info),"Cannot read online metadata");uint64_t modified=(uint64_t(info.ftLastWriteTime.dwHighDateTime)<<32)|info.ftLastWriteTime.dwLowDateTime;write(fs::path(out.wstring()+L".verified"),sha1+L"\n"+std::to_wstring(expected)+L"\n"+std::to_wstring(modified)+L"\n");
    }
}
void original_wallpaper(Context& c) {
    int object=json_property(&c.settings,0,"original_wallpaper");if(object<0||c.settings.tokens[object].type==JSON_NULL)return;require(c.settings.tokens[object].type==JSON_OBJECT,"Invalid original wallpaper state");
    auto path=str(c.settings,object,"path"),style=str(c.settings,object,"WallpaperStyle",L"10"),tile=str(c.settings,object,"TileWallpaper",L"0");
    require(style.size()<16&&tile.size()<16,"Invalid original wallpaper style");
    if(!path.empty()){
        auto source=fs::path(path);require(source.is_absolute(),"Original wallpaper path is not absolute");
        if(fs::is_regular_file(source)){auto name=L"original-wallpaper"+source.extension().wstring();copy_checked(source,c.stage/L"wallpaper-system"/name);path=(c.destination/L"wallpaper-system"/name).wstring();}
        else c.warnings.push_back(L"The old original wallpaper file is missing. Its original path is retained; restore will report that it is unavailable.");
    }
    write(c.stage/L"wallpaper-system"/L"original.json",L"{\"schema\":1,\"path\":"+quote(path)+L",\"WallpaperStyle\":"+quote(style)+L",\"TileWallpaper\":"+quote(tile)+L"}\n");
}
std::wstring legacy_casefold(std::wstring const& text) {
    // Generated from the archived Python runtime's Unicode 14.0.0 casefold table.
    // Keep this exact mapping: QLocalServer identities use casefold, not lower.
    struct Range { uint32_t first,last,step;int32_t delta; };
    static constexpr Range ranges[]={
        {0x41,0x5a,1,32},
        {0xb5,0xb5,1,775},
        {0xc0,0xd6,1,32},
        {0xd8,0xde,1,32},
        {0x100,0x12e,2,1},
        {0x132,0x136,2,1},
        {0x139,0x147,2,1},
        {0x14a,0x176,2,1},
        {0x178,0x178,1,-121},
        {0x179,0x17d,2,1},
        {0x17f,0x17f,1,-268},
        {0x181,0x181,1,210},
        {0x182,0x184,2,1},
        {0x186,0x186,1,206},
        {0x187,0x187,1,1},
        {0x189,0x18a,1,205},
        {0x18b,0x18b,1,1},
        {0x18e,0x18e,1,79},
        {0x18f,0x18f,1,202},
        {0x190,0x190,1,203},
        {0x191,0x191,1,1},
        {0x193,0x193,1,205},
        {0x194,0x194,1,207},
        {0x196,0x196,1,211},
        {0x197,0x197,1,209},
        {0x198,0x198,1,1},
        {0x19c,0x19c,1,211},
        {0x19d,0x19d,1,213},
        {0x19f,0x19f,1,214},
        {0x1a0,0x1a4,2,1},
        {0x1a6,0x1a6,1,218},
        {0x1a7,0x1a7,1,1},
        {0x1a9,0x1a9,1,218},
        {0x1ac,0x1ac,1,1},
        {0x1ae,0x1ae,1,218},
        {0x1af,0x1af,1,1},
        {0x1b1,0x1b2,1,217},
        {0x1b3,0x1b5,2,1},
        {0x1b7,0x1b7,1,219},
        {0x1b8,0x1b8,1,1},
        {0x1bc,0x1bc,1,1},
        {0x1c4,0x1c4,1,2},
        {0x1c5,0x1c5,1,1},
        {0x1c7,0x1c7,1,2},
        {0x1c8,0x1c8,1,1},
        {0x1ca,0x1ca,1,2},
        {0x1cb,0x1db,2,1},
        {0x1de,0x1ee,2,1},
        {0x1f1,0x1f1,1,2},
        {0x1f2,0x1f4,2,1},
        {0x1f6,0x1f6,1,-97},
        {0x1f7,0x1f7,1,-56},
        {0x1f8,0x21e,2,1},
        {0x220,0x220,1,-130},
        {0x222,0x232,2,1},
        {0x23a,0x23a,1,10795},
        {0x23b,0x23b,1,1},
        {0x23d,0x23d,1,-163},
        {0x23e,0x23e,1,10792},
        {0x241,0x241,1,1},
        {0x243,0x243,1,-195},
        {0x244,0x244,1,69},
        {0x245,0x245,1,71},
        {0x246,0x24e,2,1},
        {0x345,0x345,1,116},
        {0x370,0x372,2,1},
        {0x376,0x376,1,1},
        {0x37f,0x37f,1,116},
        {0x386,0x386,1,38},
        {0x388,0x38a,1,37},
        {0x38c,0x38c,1,64},
        {0x38e,0x38f,1,63},
        {0x391,0x3a1,1,32},
        {0x3a3,0x3ab,1,32},
        {0x3c2,0x3c2,1,1},
        {0x3cf,0x3cf,1,8},
        {0x3d0,0x3d0,1,-30},
        {0x3d1,0x3d1,1,-25},
        {0x3d5,0x3d5,1,-15},
        {0x3d6,0x3d6,1,-22},
        {0x3d8,0x3ee,2,1},
        {0x3f0,0x3f0,1,-54},
        {0x3f1,0x3f1,1,-48},
        {0x3f4,0x3f4,1,-60},
        {0x3f5,0x3f5,1,-64},
        {0x3f7,0x3f7,1,1},
        {0x3f9,0x3f9,1,-7},
        {0x3fa,0x3fa,1,1},
        {0x3fd,0x3ff,1,-130},
        {0x400,0x40f,1,80},
        {0x410,0x42f,1,32},
        {0x460,0x480,2,1},
        {0x48a,0x4be,2,1},
        {0x4c0,0x4c0,1,15},
        {0x4c1,0x4cd,2,1},
        {0x4d0,0x52e,2,1},
        {0x531,0x556,1,48},
        {0x10a0,0x10c5,1,7264},
        {0x10c7,0x10c7,1,7264},
        {0x10cd,0x10cd,1,7264},
        {0x13f8,0x13fd,1,-8},
        {0x1c80,0x1c80,1,-6222},
        {0x1c81,0x1c81,1,-6221},
        {0x1c82,0x1c82,1,-6212},
        {0x1c83,0x1c84,1,-6210},
        {0x1c85,0x1c85,1,-6211},
        {0x1c86,0x1c86,1,-6204},
        {0x1c87,0x1c87,1,-6180},
        {0x1c88,0x1c88,1,35267},
        {0x1c90,0x1cba,1,-3008},
        {0x1cbd,0x1cbf,1,-3008},
        {0x1e00,0x1e94,2,1},
        {0x1e9b,0x1e9b,1,-58},
        {0x1ea0,0x1efe,2,1},
        {0x1f08,0x1f0f,1,-8},
        {0x1f18,0x1f1d,1,-8},
        {0x1f28,0x1f2f,1,-8},
        {0x1f38,0x1f3f,1,-8},
        {0x1f48,0x1f4d,1,-8},
        {0x1f59,0x1f5f,2,-8},
        {0x1f68,0x1f6f,1,-8},
        {0x1fb8,0x1fb9,1,-8},
        {0x1fba,0x1fbb,1,-74},
        {0x1fbe,0x1fbe,1,-7173},
        {0x1fc8,0x1fcb,1,-86},
        {0x1fd8,0x1fd9,1,-8},
        {0x1fda,0x1fdb,1,-100},
        {0x1fe8,0x1fe9,1,-8},
        {0x1fea,0x1feb,1,-112},
        {0x1fec,0x1fec,1,-7},
        {0x1ff8,0x1ff9,1,-128},
        {0x1ffa,0x1ffb,1,-126},
        {0x2126,0x2126,1,-7517},
        {0x212a,0x212a,1,-8383},
        {0x212b,0x212b,1,-8262},
        {0x2132,0x2132,1,28},
        {0x2160,0x216f,1,16},
        {0x2183,0x2183,1,1},
        {0x24b6,0x24cf,1,26},
        {0x2c00,0x2c2f,1,48},
        {0x2c60,0x2c60,1,1},
        {0x2c62,0x2c62,1,-10743},
        {0x2c63,0x2c63,1,-3814},
        {0x2c64,0x2c64,1,-10727},
        {0x2c67,0x2c6b,2,1},
        {0x2c6d,0x2c6d,1,-10780},
        {0x2c6e,0x2c6e,1,-10749},
        {0x2c6f,0x2c6f,1,-10783},
        {0x2c70,0x2c70,1,-10782},
        {0x2c72,0x2c72,1,1},
        {0x2c75,0x2c75,1,1},
        {0x2c7e,0x2c7f,1,-10815},
        {0x2c80,0x2ce2,2,1},
        {0x2ceb,0x2ced,2,1},
        {0x2cf2,0x2cf2,1,1},
        {0xa640,0xa66c,2,1},
        {0xa680,0xa69a,2,1},
        {0xa722,0xa72e,2,1},
        {0xa732,0xa76e,2,1},
        {0xa779,0xa77b,2,1},
        {0xa77d,0xa77d,1,-35332},
        {0xa77e,0xa786,2,1},
        {0xa78b,0xa78b,1,1},
        {0xa78d,0xa78d,1,-42280},
        {0xa790,0xa792,2,1},
        {0xa796,0xa7a8,2,1},
        {0xa7aa,0xa7aa,1,-42308},
        {0xa7ab,0xa7ab,1,-42319},
        {0xa7ac,0xa7ac,1,-42315},
        {0xa7ad,0xa7ad,1,-42305},
        {0xa7ae,0xa7ae,1,-42308},
        {0xa7b0,0xa7b0,1,-42258},
        {0xa7b1,0xa7b1,1,-42282},
        {0xa7b2,0xa7b2,1,-42261},
        {0xa7b3,0xa7b3,1,928},
        {0xa7b4,0xa7c2,2,1},
        {0xa7c4,0xa7c4,1,-48},
        {0xa7c5,0xa7c5,1,-42307},
        {0xa7c6,0xa7c6,1,-35384},
        {0xa7c7,0xa7c9,2,1},
        {0xa7d0,0xa7d0,1,1},
        {0xa7d6,0xa7d8,2,1},
        {0xa7f5,0xa7f5,1,1},
        {0xab70,0xabbf,1,-38864},
        {0xff21,0xff3a,1,32},
        {0x10400,0x10427,1,40},
        {0x104b0,0x104d3,1,40},
        {0x10570,0x1057a,1,39},
        {0x1057c,0x1058a,1,39},
        {0x1058c,0x10592,1,39},
        {0x10594,0x10595,1,39},
        {0x10c80,0x10cb2,1,64},
        {0x118a0,0x118bf,1,32},
        {0x16e40,0x16e5f,1,32},
        {0x1e900,0x1e921,1,34},
    };
    struct Expansion { uint32_t code;wchar_t const* text; };
    static constexpr Expansion expansions[]={
        {0xdf,L"\u0073\u0073"},
        {0x130,L"\u0069\u0307"},
        {0x149,L"\u02bc\u006e"},
        {0x1f0,L"\u006a\u030c"},
        {0x390,L"\u03b9\u0308\u0301"},
        {0x3b0,L"\u03c5\u0308\u0301"},
        {0x587,L"\u0565\u0582"},
        {0x1e96,L"\u0068\u0331"},
        {0x1e97,L"\u0074\u0308"},
        {0x1e98,L"\u0077\u030a"},
        {0x1e99,L"\u0079\u030a"},
        {0x1e9a,L"\u0061\u02be"},
        {0x1e9e,L"\u0073\u0073"},
        {0x1f50,L"\u03c5\u0313"},
        {0x1f52,L"\u03c5\u0313\u0300"},
        {0x1f54,L"\u03c5\u0313\u0301"},
        {0x1f56,L"\u03c5\u0313\u0342"},
        {0x1f80,L"\u1f00\u03b9"},
        {0x1f81,L"\u1f01\u03b9"},
        {0x1f82,L"\u1f02\u03b9"},
        {0x1f83,L"\u1f03\u03b9"},
        {0x1f84,L"\u1f04\u03b9"},
        {0x1f85,L"\u1f05\u03b9"},
        {0x1f86,L"\u1f06\u03b9"},
        {0x1f87,L"\u1f07\u03b9"},
        {0x1f88,L"\u1f00\u03b9"},
        {0x1f89,L"\u1f01\u03b9"},
        {0x1f8a,L"\u1f02\u03b9"},
        {0x1f8b,L"\u1f03\u03b9"},
        {0x1f8c,L"\u1f04\u03b9"},
        {0x1f8d,L"\u1f05\u03b9"},
        {0x1f8e,L"\u1f06\u03b9"},
        {0x1f8f,L"\u1f07\u03b9"},
        {0x1f90,L"\u1f20\u03b9"},
        {0x1f91,L"\u1f21\u03b9"},
        {0x1f92,L"\u1f22\u03b9"},
        {0x1f93,L"\u1f23\u03b9"},
        {0x1f94,L"\u1f24\u03b9"},
        {0x1f95,L"\u1f25\u03b9"},
        {0x1f96,L"\u1f26\u03b9"},
        {0x1f97,L"\u1f27\u03b9"},
        {0x1f98,L"\u1f20\u03b9"},
        {0x1f99,L"\u1f21\u03b9"},
        {0x1f9a,L"\u1f22\u03b9"},
        {0x1f9b,L"\u1f23\u03b9"},
        {0x1f9c,L"\u1f24\u03b9"},
        {0x1f9d,L"\u1f25\u03b9"},
        {0x1f9e,L"\u1f26\u03b9"},
        {0x1f9f,L"\u1f27\u03b9"},
        {0x1fa0,L"\u1f60\u03b9"},
        {0x1fa1,L"\u1f61\u03b9"},
        {0x1fa2,L"\u1f62\u03b9"},
        {0x1fa3,L"\u1f63\u03b9"},
        {0x1fa4,L"\u1f64\u03b9"},
        {0x1fa5,L"\u1f65\u03b9"},
        {0x1fa6,L"\u1f66\u03b9"},
        {0x1fa7,L"\u1f67\u03b9"},
        {0x1fa8,L"\u1f60\u03b9"},
        {0x1fa9,L"\u1f61\u03b9"},
        {0x1faa,L"\u1f62\u03b9"},
        {0x1fab,L"\u1f63\u03b9"},
        {0x1fac,L"\u1f64\u03b9"},
        {0x1fad,L"\u1f65\u03b9"},
        {0x1fae,L"\u1f66\u03b9"},
        {0x1faf,L"\u1f67\u03b9"},
        {0x1fb2,L"\u1f70\u03b9"},
        {0x1fb3,L"\u03b1\u03b9"},
        {0x1fb4,L"\u03ac\u03b9"},
        {0x1fb6,L"\u03b1\u0342"},
        {0x1fb7,L"\u03b1\u0342\u03b9"},
        {0x1fbc,L"\u03b1\u03b9"},
        {0x1fc2,L"\u1f74\u03b9"},
        {0x1fc3,L"\u03b7\u03b9"},
        {0x1fc4,L"\u03ae\u03b9"},
        {0x1fc6,L"\u03b7\u0342"},
        {0x1fc7,L"\u03b7\u0342\u03b9"},
        {0x1fcc,L"\u03b7\u03b9"},
        {0x1fd2,L"\u03b9\u0308\u0300"},
        {0x1fd3,L"\u03b9\u0308\u0301"},
        {0x1fd6,L"\u03b9\u0342"},
        {0x1fd7,L"\u03b9\u0308\u0342"},
        {0x1fe2,L"\u03c5\u0308\u0300"},
        {0x1fe3,L"\u03c5\u0308\u0301"},
        {0x1fe4,L"\u03c1\u0313"},
        {0x1fe6,L"\u03c5\u0342"},
        {0x1fe7,L"\u03c5\u0308\u0342"},
        {0x1ff2,L"\u1f7c\u03b9"},
        {0x1ff3,L"\u03c9\u03b9"},
        {0x1ff4,L"\u03ce\u03b9"},
        {0x1ff6,L"\u03c9\u0342"},
        {0x1ff7,L"\u03c9\u0342\u03b9"},
        {0x1ffc,L"\u03c9\u03b9"},
        {0xfb00,L"\u0066\u0066"},
        {0xfb01,L"\u0066\u0069"},
        {0xfb02,L"\u0066\u006c"},
        {0xfb03,L"\u0066\u0066\u0069"},
        {0xfb04,L"\u0066\u0066\u006c"},
        {0xfb05,L"\u0073\u0074"},
        {0xfb06,L"\u0073\u0074"},
        {0xfb13,L"\u0574\u0576"},
        {0xfb14,L"\u0574\u0565"},
        {0xfb15,L"\u0574\u056b"},
        {0xfb16,L"\u057e\u0576"},
        {0xfb17,L"\u0574\u056d"},
    };
    std::wstring out;
    for(size_t i=0;i<text.size();++i) {
        uint32_t cp=text[i];
        if(cp>=0xd800&&cp<=0xdbff&&i+1<text.size()&&text[i+1]>=0xdc00&&text[i+1]<=0xdfff)cp=0x10000+((cp-0xd800)<<10)+(text[++i]-0xdc00);
        auto special=std::lower_bound(std::begin(expansions),std::end(expansions),cp,[](auto const& item,uint32_t value){return item.code<value;});
        if(special!=std::end(expansions)&&special->code==cp){out+=special->text;continue;}
        auto range=std::lower_bound(std::begin(ranges),std::end(ranges),cp,[](auto const& item,uint32_t value){return item.last<value;});
        if(range!=std::end(ranges)&&cp>=range->first&&(cp-range->first)%range->step==0)cp=uint32_t(int64_t(cp)+range->delta);
        if(cp>0xffff){cp-=0x10000;out+=wchar_t(0xd800+(cp>>10));out+=wchar_t(0xdc00+(cp&1023));}else out+=wchar_t(cp);
    }
    return out;
}
std::wstring legacy_pipe(fs::path const& source) { return L"\\\\.\\pipe\\StillWallpaper-"+digest_text(legacy_casefold(fs::absolute(source).lexically_normal().wstring())).substr(0,16); }
}
Result prepare_legacy_source(fs::path const& source,fs::path const& expected_executable,bool request_quit) noexcept {
    Result result;
    try {
        require(fs::is_regular_file(source/L"settings.json"),"The old profile is missing settings.json");no_link(source);auto pipe_name=legacy_pipe(source);
        if(!WaitNamedPipeW(pipe_name.c_str(),100)){DWORD error=GetLastError();if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND){result.ok=true;return result;}require(false,"The old instance channel is busy or inaccessible; close the old version normally");}
        require(request_quit,"The previous version is still running; close it before migration");
        unique_handle pipe(CreateFileW(pipe_name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr));require(bool(pipe),"Cannot connect to the old instance");ULONG pid{};require(GetNamedPipeServerProcessId(pipe.get(),&pid)&&pid&&pid!=GetCurrentProcessId(),"Cannot verify the old instance process");
        unique_handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid));require(bool(process),"Cannot inspect the old instance process");wchar_t image[LB_PATH]{};DWORD length=LB_PATH;require(QueryFullProcessImageNameW(process.get(),0,image,&length),"Cannot read the old executable identity");require(lower(fs::weakly_canonical(image).wstring())==lower(fs::weakly_canonical(expected_executable).wstring()),"Old channel executable does not match this installation; close it manually");
        unique_handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));require(bool(event),"Cannot create shutdown event");OVERLAPPED operation{};operation.hEvent=event.get();DWORD written{};BOOL sent=WriteFile(pipe.get(),"quit",4,&written,&operation);if(!sent&&GetLastError()==ERROR_IO_PENDING){auto waited=WaitForSingleObject(event.get(),1500);if(waited!=WAIT_OBJECT_0){CancelIoEx(pipe.get(),&operation);GetOverlappedResult(pipe.get(),&operation,&written,TRUE);require(false,"Old instance did not accept a normal exit request");}sent=GetOverlappedResult(pipe.get(),&operation,&written,FALSE);}require(sent&&written==4,"Could not request a normal exit from the old version");require(WaitForSingleObject(process.get(),10000)==WAIT_OBJECT_0,"The old version is still saving or restoring the desktop; wait for it to exit and retry");result.ok=true;
    }catch(std::exception const& e){try{result.error=sql::wide(e.what());}catch(...){result.error=L"Cannot verify the old instance.";}}catch(...){result.error=L"Cannot verify the old instance.";}return result;
}
Result upgrade(fs::path const& source_arg,fs::path const& destination_arg,fs::path const& assets_arg) noexcept {
    Result result;fs::path stage;
    try {
        auto source=fs::absolute(source_arg).lexically_normal(),destination=fs::absolute(destination_arg).lexically_normal(),assets=fs::absolute(assets_arg).lexically_normal();require(lower(source.wstring())!=lower(destination.wstring()),"Source and destination profiles must be separate");no_link(source);no_link(destination);require(fs::exists(source/L"settings.json"),"The old profile has no settings.json");
        auto mutex_name=L"Local\\LightBlockDesktop.Migration."+digest_text(destination.wstring()).substr(0,24);unique_handle mutex(CreateMutexW(nullptr,FALSE,mutex_name.c_str()));require(bool(mutex),"Cannot create migration lock");DWORD wait=WaitForSingleObject(mutex.get(),0);require(wait==WAIT_OBJECT_0||wait==WAIT_ABANDONED,"Another upgrade is migrating this profile");scope_exit unlock([&]{ReleaseMutex(mutex.get());});
        if(fs::exists(destination)){auto complete=read_json(destination/L"migration-complete.json",128*1024);require(number(complete,0,"schema",0)==1&&lower(str(complete,0,"source"))==lower(source.wstring()),"Destination is not a completed migration; existing data was not changed");require(fs::exists(destination/L"settings.ini"),"Migrated configuration is missing");result.ok=true;return result;}
        auto stopped=prepare_legacy_source(source,assets.parent_path()/L"轻块桌面.exe",true);require(stopped.ok,sql::utf8(stopped.error).c_str());
        unique_handle source_lock(CreateFileW((source/L"settings.json").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));require(bool(source_lock),"The old settings are busy; close the previous version before upgrading");
        auto before=digest(source/L"settings.json");stage=destination.parent_path()/(destination.filename().wstring()+L".staging-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));require(!fs::exists(stage),"Staging path already exists");fs::create_directories(stage);Context c(source,stage,destination,assets);
        for(auto file:{L"settings.json",L"collections.json",L"collections.backup.json",L"desktop-icon-state.json"})c.backup(file);
        settings(c);library(c);collections(c);auto database=source/L"workbench.sqlite3";if(fs::exists(database)){no_link(database);LegacySql old(database);old.validate();old.backup(stage/L"legacy-backup"/L"workbench.sqlite3");LegacySql backup(stage/L"legacy-backup"/L"workbench.sqlite3");notes(c,backup);clipboard(c,backup);focus(c,backup);}
        online(c);original_wallpaper(c);c.ini.save(stage/L"settings.ini");require(before==digest(source/L"settings.json"),"Old profile changed during migration");std::wstring report=L"{\"schema\":1,\"source\":"+quote(source.wstring())+L",\"source_settings_sha256\":"+quote(before)+L",\"warnings\":[";bool first=true;for(auto const& warning:c.warnings){if(!first)report+=L",";first=false;report+=quote(warning);}report+=L"],\"backup\":[";first=true;for(auto const& entry:fs::recursive_directory_iterator(stage/L"legacy-backup"))if(entry.is_regular_file()){if(!first)report+=L",";first=false;report+=L"{\"path\":"+quote(fs::relative(entry.path(),stage).wstring())+L",\"sha256\":"+quote(digest(entry.path()))+L"}";}report+=L"]}\n";write(stage/L"migration-complete.json",report);
        require(MoveFileExW(stage.c_str(),destination.c_str(),MOVEFILE_WRITE_THROUGH),"Could not commit migration; the old profile is untouched");stage.clear();result.ok=true;result.migrated=true;
    }catch(std::exception const& e){try{result.error=sql::wide(e.what());}catch(...){result.error=L"Profile migration failed.";}}catch(...){result.error=L"Profile migration failed; the old profile was not changed.";}
    // Failed staging is retained for diagnosis; never recursively delete an
    // untrusted path or silently launch from partial output.
    if(!stage.empty())result.error+=L"\nStaging: "+stage.wstring();return result;
}
bool tests(fs::path const& root) {
    try {
        require(!fs::exists(root),"Migration test directory must be new");fs::create_directories(root);auto source=root/L"source",destination=root/L"profile",assets=root/L"assets";fs::create_directories(source);fs::create_directories(assets);
        require(legacy_casefold(L"StraßeİΣςẞ𐐀")==L"strassei\u0307σσss𐐨","Unicode casefold differs from Python");
        write(source/L"wallpapers"/L"demo"/L"中文.jpg",L"Synthetic image bytes; never displayed");
        auto image_hash=digest(source/L"wallpapers"/L"demo"/L"中文.jpg");
        auto managed=source/L"organizer_files"/L"custom"/L"keep.txt";write(managed,L"Managed user content stays in place");auto managed_hash=digest(managed);
        write(source/L"settings.json",L"{\"version\":\"1.0.0\",\"library\":[{\"id\":\"wallpaper-id\",\"name\":\"我的风景\",\"kind\":\"image\",\"path\":\"wallpapers/demo/中文.jpg\",\"online_id\":\"fixture-image\"}],\"active_id\":\"wallpaper-id\",\"organizer_groups\":[],\"organizer_pending\":[],\"organizer_loose\":[],\"settings_language\":\"en\",\"settings_theme\":\"light\",\"capture_shortcut\":\"Ctrl+Alt+A\",\"taskbar_opacity\":51,\"organizer_icon_color\":\"#123456\"}");auto original=digest(source/L"settings.json");
        write(source/L"collections.json",L"{\"version\":1,\"groups\":[{\"id\":\"user-group\",\"auto_category\":\"documents\",\"name\":\"我的文档\",\"color\":\"#335577\",\"entries\":[{\"id\":\"e1\",\"name\":\"keep\",\"target\":"+quote(managed.wstring())+L",\"managed\":true}]}],\"loose\":[],\"pending\":[]}");auto collection_hash=digest(source/L"collections.json");
        wchar_t sha1[41]{};require(hash_file((source/L"wallpapers"/L"demo"/L"中文.jpg").c_str(),L"SHA1",sha1,41),"Fixture hash failed");write(assets/L"online-wallpapers.json",L"{\"entries\":[{\"id\":\"fixture-image\",\"filename\":\"image.jpg\",\"sha1\":"+quote(sha1)+L",\"bytes\":"+std::to_wstring(fs::file_size(source/L"wallpapers"/L"demo"/L"中文.jpg"))+L"}]}");
        { LegacySql db(source/L"workbench.sqlite3",true);db.exec("CREATE TABLE notes(id TEXT,title TEXT,body TEXT,color TEXT,done INTEGER,due TEXT,notified INTEGER,pinned INTEGER,updated TEXT,archived INTEGER,pin_x INTEGER,pin_y INTEGER,pin_w INTEGER,pin_h INTEGER);INSERT INTO notes VALUES('note-id','中文标题, ''quoted''','Body 中文','mint',1,'2026-11-13T11:22:33+00:00',0,1,'2026-11-12T10:20:30.123456+00:00',0,100,120,420,300);CREATE TABLE clipboard(id INTEGER,kind TEXT,text TEXT,payload BLOB,created TEXT,pinned INTEGER);INSERT INTO clipboard VALUES(4,'text','Synthetic only',X'','2026-11-13T10:00:00+00:00',1);CREATE TABLE profiles(id TEXT,name TEXT,data TEXT,updated TEXT);INSERT INTO profiles VALUES('p','Desk','{\"wallpaper\":\"wallpaper-id\",\"organizer_enabled\":true,\"group_order\":[\"user-group\"]}','2026-11-13');CREATE TABLE focus_sessions(id TEXT,started TEXT,duration INTEGER,deadline TEXT,state TEXT);INSERT INTO focus_sessions VALUES('f','2026-11-13T09:00:00+00:00',1500,'2026-11-13T09:25:00+00:00','completed')");}
        auto db_hash=digest(source/L"workbench.sqlite3");auto r=upgrade(source,destination,assets);require(r.ok&&r.migrated,sql::utf8(r.error.empty()?L"Fixture migration failed":r.error).c_str());
        require(digest(source/L"settings.json")==original&&digest(source/L"collections.json")==collection_hash&&digest(source/L"workbench.sqlite3")==db_hash,"Source metadata modified");require(digest(managed)==managed_hash,"Managed desktop content changed");require(digest(source/L"wallpapers"/L"demo"/L"中文.jpg")==image_hash,"Original wallpaper changed");
        require(GetPrivateProfileIntW(L"appearance",L"language",0,(destination/L"settings.ini").c_str())==2,"Language migration failed");require(GetPrivateProfileIntW(L"taskbar",L"taskbar_opacity",0,(destination/L"settings.ini").c_str())==51,"Opacity migration failed");require(GetPrivateProfileIntW(L"capture",L"capture_key",0,(destination/L"settings.ini").c_str())=='A',"Shortcut migration failed");require(GetPrivateProfileIntW(L"colors",L"icon_color",0,(destination/L"settings.ini").c_str())==RGB(0x12,0x34,0x56),"COLORREF migration failed");
        auto library_index=read_json(destination/L"migrated-library.json");auto items=array(library_index,0,"entries");require(items.size()==1&&str(library_index,items[0],"title")==L"我的风景"&&digest(str(library_index,items[0],"path"))==image_hash,"Library identity or bytes lost");require(fs::exists(destination/L"online"/L"image.jpg.verified"),"Verified online receipt lost");
        auto group_index=read_json(destination/L"organizer.json");auto entries=array(group_index,0,"entries");require(entries.size()==1&&str(group_index,entries[0],"group")==L"documents"&&flag(group_index,entries[0],"external")&&str(group_index,entries[0],"path")==managed.wstring(),"Managed organizer reference lost");
        auto note=destination/L"notes"/(L"note-"+digest_text(L"note-id").substr(0,24)+L".txt");std::unique_ptr<wchar_t,decltype(&free)> body(read_utf8_file(note.c_str(),1024),free);require(body&&std::wstring(body.get())==L"Body 中文","Unicode note body lost");require(GetPrivateProfileIntW(L"note",L"width",0,(note.wstring()+L".ini").c_str())==420,"Note geometry lost");
        {sql::Database db(destination/L"clipboard"/L"history.sqlite3");sql::Statement row(db,"SELECT text,pinned,created FROM clipboard WHERE id=4");require(row.next()&&row.text(0)==L"Synthetic only"&&row.integer(1)==1&&row.integer(2)>epoch_ms,"Clipboard conversion failed");}
        {sql::Database db(destination/L"focus.db");sql::Statement row(db,"SELECT duration,completed FROM focus_sessions");require(row.next()&&row.integer(0)==1500&&row.integer(1)==1,"Focus conversion failed");sql::Statement profile(db,"SELECT group_order,path FROM workspaces");require(profile.next()&&profile.text(0)==L"documents"&&profile.text(1)==str(library_index,items[0],"path"),"Workspace references were not remapped");}
        auto again=upgrade(source,destination,assets);require(again.ok&&!again.migrated,"Migration is not idempotent");
        auto broken=root/L"broken";fs::create_directories(broken);write(broken/L"settings.json",L"{bad");auto fail=upgrade(broken,root/L"rejected",assets);require(!fail.ok&&!fs::exists(root/L"rejected")&&fs::exists(broken/L"settings.json"),"Corrupt migration was not rejected atomically");
        auto occupied=root/L"occupied";fs::create_directories(occupied);write(occupied/L"settings.ini",L"keep");auto occupied_hash=digest(occupied/L"settings.ini");fail=upgrade(source,occupied,assets);require(!fail.ok&&digest(occupied/L"settings.ini")==occupied_hash,"Existing profile was overwritten");
        auto pending=root/L"pending";fs::create_directories(pending);write(pending/L"settings.json",L"{\"library\":[],\"organizer_pending\":[{\"entry\":{}}]}");fail=upgrade(pending,root/L"pending-output",assets);require(!fail.ok&&!fs::exists(root/L"pending-output"),"Unfinished file transaction was silently accepted");
        auto escape=root/L"escape";fs::create_directories(escape);write(escape/L"settings.json",L"{\"library\":[{\"id\":\"x\",\"name\":\"x\",\"path\":\"../source/wallpapers/demo/中文.jpg\"}]}");fail=upgrade(escape,root/L"escape-output",assets);require(!fail.ok&&!fs::exists(root/L"escape-output"),"Managed wallpaper path traversal was accepted");
        write(root/L"result.json",L"{\"migration\":true,\"source_unchanged\":true,\"idempotent\":true,\"unicode_casefold\":true,\"settings_and_hotkey\":true,\"wallpaper_bytes_titles\":true,\"online_verified_receipt\":true,\"organizer_file_references\":true,\"note_body_geometry\":true,\"clipboard_history\":true,\"workspace_ids\":true,\"focus_history\":true,\"corrupt_source_refused\":true,\"existing_profile_not_overwritten\":true,\"pending_move_refused\":true,\"path_traversal_refused\":true}\n");return true;
    }catch(std::exception const& e){std::fprintf(stderr,"Migration fixture failed: %s\n",e.what());return false;}catch(...){return false;}
}
}
