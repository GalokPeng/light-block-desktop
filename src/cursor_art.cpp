#include "pointer.hpp"
#include <array>
namespace lb::pointer {
namespace {
uint16_t word(BYTE const* bytes) { return uint16_t(bytes[0]|(unsigned(bytes[1])<<8)); }
uint32_t dword(BYTE const* bytes) { return uint32_t(word(bytes)|(unsigned(word(bytes+2))<<16)); }
std::vector<BYTE> read(std::filesystem::path const& path) {
    unique_handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));LARGE_INTEGER size{};if(!file||!GetFileSizeEx(file.get(),&size)||size.QuadPart<6||size.QuadPart>2*1024*1024) return {};
    std::vector<BYTE> bytes(size_t(size.QuadPart));DWORD count=0;if(!ReadFile(file.get(),bytes.data(),DWORD(bytes.size()),&count,nullptr)||count!=bytes.size()) return {};return bytes;
}
bool write(std::filesystem::path const& path,std::vector<BYTE> const& bytes) {
    auto temporary=path;temporary+=L".partial";unique_handle file(CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));DWORD count=0;
    bool good=file&&WriteFile(file.get(),bytes.data(),DWORD(bytes.size()),&count,nullptr)&&count==bytes.size()&&FlushFileBuffers(file.get());file.reset();if(good) good=MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;if(!good) DeleteFileW(temporary.c_str());return good;
}
bool tint_cur(BYTE *bytes,size_t size,COLORREF color) {
    if(size<6||word(bytes)!=0||word(bytes+2)!=2) return false;unsigned count=word(bytes+4);if(!count||count>16||6+count*16>size) return false;
    int ink[]={GetBValue(color),GetGValue(color),GetRValue(color)};bool light=GetRValue(color)*299+GetGValue(color)*587+GetBValue(color)*114>125000;
    int edge[]={light?44:255,light?33:249,light?21:243},old_edge[]={44,33,21},old_ink[]={237,242,217};double norm=0;for(int c=0;c<3;++c) norm+=std::pow(old_ink[c]-old_edge[c],2);
    for(unsigned n=0;n<count;++n) {
        BYTE *entry=bytes+6+n*16;unsigned width=entry[0]?entry[0]:256,height=entry[1]?entry[1]:256,length=dword(entry+8),offset=dword(entry+12);
        if(width>256||height>256||offset>size||length>size-offset||length<40) return false;BYTE *dib=bytes+offset;
        if(dword(dib)!=40||dword(dib+4)!=width||dword(dib+8)!=height*2||word(dib+12)!=1||word(dib+14)!=32||dword(dib+16)!=0||40+width*height*4>length) return false;
        for(unsigned pixel=0;pixel<width*height;++pixel) {
            BYTE *value=dib+40+pixel*4;if(!value[3]) continue;
            if(value[3]<=120&&abs(int(value[0])-36)<3&&abs(int(value[1])-24)<3&&abs(int(value[2])-15)<3) continue;
            double mix=0;for(int c=0;c<3;++c) mix+=(int(value[c])-old_edge[c])*(old_ink[c]-old_edge[c]);mix=std::clamp(mix/norm,0.,1.);
            for(int c=0;c<3;++c) value[c]=BYTE(std::clamp(int(std::lround(edge[c]*(1-mix)+ink[c]*mix)),0,255));
        }
    }return true;
}
bool chunks(BYTE *bytes,size_t size,COLORREF color,unsigned depth) {
    if(depth>4) return false;size_t at=0;
    while(at<size) { if(size-at<8) return false;size_t length=dword(bytes+at+4),padded=length+(length&1);if(padded>size-at-8) return false;
        if(!memcmp(bytes+at,"icon",4)) { if(!tint_cur(bytes+at+8,length,color)) return false; }
        else if(!memcmp(bytes+at,"LIST",4)) { if(length<4||memcmp(bytes+at+8,"fram",4)||!chunks(bytes+at+12,length-4,color,depth+1)) return false; }
        at+=8+padded;
    }return at==size;
}
}
bool tint_cursor(std::vector<BYTE> &bytes,COLORREF color) {
    if(bytes.size()<6) return false;if(bytes.size()>=12&&!memcmp(bytes.data(),"RIFF",4)) {
        if(dword(bytes.data()+4)!=bytes.size()-8||memcmp(bytes.data()+8,"ACON",4)) return false;return chunks(bytes.data()+12,bytes.size()-12,color,0);
    }return tint_cur(bytes.data(),bytes.size(),color);
}
std::filesystem::path scheme(int theme,COLORREF color) {
    wchar_t const* themes[]={L"",L"stream",L"outline",L"pixel"};if(theme<1||theme>3) return {};
    auto source=std::filesystem::path(g_app.directory)/L"assets"/L"cursors"/themes[theme];
    if(color==RGB(217,242,237)) return source;
    auto destination=std::filesystem::path(g_app.data)/L"cursor-cache"/(std::wstring(themes[theme])+L"-"+std::to_wstring(color)+L"-v1");
    if(!ensure_directory(destination.c_str())) return {};auto marker=destination/L"ready.txt";if(GetFileAttributesW(marker.c_str())!=INVALID_FILE_ATTRIBUTES) return destination;
    std::error_code error;
    for(auto iterator=std::filesystem::directory_iterator(source,error);!error&&iterator!=std::filesystem::directory_iterator{};iterator.increment(error)) {
        auto extension=iterator->path().extension();if(extension!=L".cur"&&extension!=L".ani") continue;auto bytes=read(iterator->path());
        if(bytes.empty()||!tint_cursor(bytes,color)||!write(destination/iterator->path().filename(),bytes)) return {};
    }
    if(error||!write_utf8_atomic(marker.c_str(),L"Native C++ tint revision 1\n")) return {};return destination;
}
bool artwork_tests(std::filesystem::path const& directory) {
    if(!ensure_directory(directory.c_str())) return false;
    for(auto theme:{L"stream",L"outline",L"pixel"}) {
        auto source=std::filesystem::path(g_app.directory)/L"assets"/L"cursors"/theme;
        for(auto name:{L"normal.cur",L"help.cur",L"working.cur",L"busy.cur",L"cross.cur",L"text.cur",L"hand.cur",L"unavailable.cur",L"vertical.cur",L"horizontal.cur",L"diagonal1.cur",L"diagonal2.cur",L"move.cur",L"alternate.cur",L"pin.cur",L"person.cur",L"pen.cur",L"busy-32.ani",L"working-32.ani"}) {
            auto bytes=read(source/name);if(bytes.empty()) return false;auto original=bytes;
            if(!tint_cursor(bytes,RGB(129,89,242))||bytes==original) return false;auto target=directory/(std::wstring(theme)+L"-"+name);if(!write(target,bytes)) return false;
            HCURSOR cursor=static_cast<HCURSOR>(LoadImageW(nullptr,target.c_str(),IMAGE_CURSOR,32,32,LR_LOADFROMFILE));if(!cursor) return false;ICONINFO info{};bool good=GetIconInfo(cursor,&info)!=FALSE;if(info.hbmColor) DeleteObject(info.hbmColor);if(info.hbmMask) DeleteObject(info.hbmMask);DestroyCursor(cursor);if(!good) return false;
        }
    }
    std::vector<BYTE> invalid{0,0,2,0,255,255};return !tint_cursor(invalid,RGB(255,0,0));
}
}
