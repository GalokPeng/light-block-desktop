#include "network.h"
#include <winhttp.h>
#include <bcrypt.h>
struct Hash { BCRYPT_ALG_HANDLE algorithm;BCRYPT_HASH_HANDLE hash;DWORD length; };
static void hash_close(Hash *h) { if(h->hash) BCryptDestroyHash(h->hash);if(h->algorithm) BCryptCloseAlgorithmProvider(h->algorithm,0);ZeroMemory(h,sizeof *h); }
static BOOL hash_open(Hash *h,const wchar_t *algorithm) {
    ZeroMemory(h,sizeof *h);if(wcscmp(algorithm,BCRYPT_SHA256_ALGORITHM) && wcscmp(algorithm,BCRYPT_SHA1_ALGORITHM)) return FALSE;
    DWORD written=0;if(BCryptOpenAlgorithmProvider(&h->algorithm,algorithm,NULL,0)<0) return FALSE;
    if(BCryptGetProperty(h->algorithm,BCRYPT_HASH_LENGTH,(BYTE*)&h->length,sizeof h->length,&written,0)<0 || BCryptCreateHash(h->algorithm,&h->hash,NULL,0,NULL,0,0)<0) { hash_close(h);return FALSE; }return TRUE;
}
static BOOL hash_end(Hash *h,wchar_t *out,size_t capacity) {
    BYTE bytes[64];if(h->length>sizeof bytes || capacity<h->length*2+1 || BCryptFinishHash(h->hash,bytes,h->length,0)<0) return FALSE;
    for(DWORD n=0;n<h->length;++n) { static const wchar_t hex[]=L"0123456789abcdef";out[n*2]=hex[bytes[n]>>4];out[n*2+1]=hex[bytes[n]&15]; }out[h->length*2]=0;return TRUE;
}
BOOL hash_file(const wchar_t *path,const wchar_t *algorithm,wchar_t *out,size_t capacity,std::atomic_bool const* cancel) {
    Hash hash;if(!hash_open(&hash,algorithm)) return FALSE;HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN,NULL);if(file==INVALID_HANDLE_VALUE) { hash_close(&hash);return FALSE; }
    BYTE buffer[65536];DWORD count;BOOL ok=TRUE;for(;;) { if(cancel&&*cancel) { ok=FALSE;break; }if(!ReadFile(file,buffer,sizeof buffer,&count,NULL)) { ok=FALSE;break; }if(!count) break;if(BCryptHashData(hash.hash,buffer,count,0)<0) { ok=FALSE;break; } }
    CloseHandle(file);if(ok) ok=hash_end(&hash,out,capacity);hash_close(&hash);return ok;
}
BOOL hash_memory(const void *data,size_t size,const wchar_t *algorithm,wchar_t *out,size_t capacity) {
    if(size>MAXDWORD||(!data&&size)) return FALSE;Hash hash{};if(!hash_open(&hash,algorithm)) return FALSE;lb::scope_exit cleanup([&]{hash_close(&hash);});return BCryptHashData(hash.hash,const_cast<BYTE*>(static_cast<BYTE const*>(data)),DWORD(size),0)>=0&&hash_end(&hash,out,capacity);
}
BOOL safe_filename(const wchar_t *name) {
    if(!name || !*name || wcslen(name)>=180 || !wcscmp(name,L".") || !wcscmp(name,L"..") || wcspbrk(name,L"\\/:*?\"<>|\r\n") || name[wcslen(name)-1]==L'.' || name[wcslen(name)-1]==L' ') return FALSE;
    for(const wchar_t *p=name;*p;++p) if(*p<32 || *p==127) return FALSE;
    wchar_t stem[180];StringCchCopyW(stem,180,name);wchar_t *dot=wcschr(stem,L'.');if(dot) *dot=0;
    if(!_wcsicmp(stem,L"CON")||!_wcsicmp(stem,L"PRN")||!_wcsicmp(stem,L"AUX")||!_wcsicmp(stem,L"NUL")) return FALSE;
    if(wcslen(stem)==4 && (!_wcsnicmp(stem,L"COM",3)||!_wcsnicmp(stem,L"LPT",3)) && stem[3]>=L'1' && stem[3]<=L'9') return FALSE;return TRUE;
}
BOOL safe_https_url(const wchar_t *url) {
    if(!url || wcslen(url)>8000 || _wcsnicmp(url,L"https://",8)) return FALSE;
    for(const wchar_t *p=url;*p;++p) if(*p<32 || *p==127 || *p==L'\\') return FALSE;
    URL_COMPONENTS parts={};parts.dwStructSize=sizeof parts;parts.dwHostNameLength=(DWORD)-1;parts.dwUserNameLength=(DWORD)-1;parts.dwPasswordLength=(DWORD)-1;
    return WinHttpCrackUrl(url,0,0,&parts) && parts.nScheme==INTERNET_SCHEME_HTTPS && parts.dwHostNameLength && !parts.dwUserNameLength && !parts.dwPasswordLength;
}
HRESULT download_https(const wchar_t *url,const wchar_t *destination,uint64_t maximum,uint64_t expected,const wchar_t *algorithm,const wchar_t *digest,volatile LONG *cancel,DownloadProgress progress,void *context,DownloadStatus status_callback) {
    if(!safe_https_url(url) || !destination || !maximum || expected>maximum) return E_INVALIDARG;
    if(status_callback)status_callback(DownloadStage::connecting,context);if(progress)progress(0,expected,context);
    wchar_t current[8192],temp[LB_PATH],actual[129];StringCchCopyW(current,ARRAYSIZE(current),url);
    if(FAILED(StringCchPrintfW(temp,LB_PATH,L"%s.partial",destination))) return E_INVALIDARG;
    if(digest && *digest && GetFileAttributesW(destination)!=INVALID_FILE_ATTRIBUTES && hash_file(destination,algorithm,actual,ARRAYSIZE(actual)) && !_wcsicmp(actual,digest)) return S_OK;
    Hash hash;BOOL hashing=digest && *digest;if(hashing && !hash_open(&hash,algorithm)) return E_FAIL;
    HRESULT hr=E_FAIL;HINTERNET session=NULL,connection=NULL,request=NULL;HANDLE file=INVALID_HANDLE_VALUE;ULONGLONG started=GetTickCount64(),last_progress=0;uint64_t received=0,total=expected;
    session=WinHttpOpen(L"LightBlockDesktopNative/0.1 (local test)",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);if(!session) goto done;
    WinHttpSetTimeouts(session,10000,15000,15000,15000);
    for(int redirect=0;redirect<6;++redirect) {
        URL_COMPONENTS parts={};parts.dwStructSize=sizeof parts;parts.dwHostNameLength=parts.dwUrlPathLength=parts.dwExtraInfoLength=(DWORD)-1;
        if(!safe_https_url(current) || !WinHttpCrackUrl(current,0,0,&parts)) { hr=E_INVALIDARG;goto done; }
        wchar_t host[512],path[8192];if(parts.dwHostNameLength>=ARRAYSIZE(host) || parts.dwUrlPathLength+parts.dwExtraInfoLength>=ARRAYSIZE(path)) { hr=E_INVALIDARG;goto done; }
        StringCchCopyNW(host,ARRAYSIZE(host),parts.lpszHostName,parts.dwHostNameLength);StringCchCopyNW(path,ARRAYSIZE(path),parts.lpszUrlPath,parts.dwUrlPathLength);if(parts.dwExtraInfoLength) StringCchCatNW(path,ARRAYSIZE(path),parts.lpszExtraInfo,parts.dwExtraInfoLength);if(!*path) StringCchCopyW(path,ARRAYSIZE(path),L"/");
        connection=WinHttpConnect(session,host,parts.nPort,0);if(!connection) goto done;
        request=WinHttpOpenRequest(connection,L"GET",path,NULL,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);if(!request) goto done;
        DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof policy);
        if((cancel && InterlockedCompareExchange(cancel,0,0)) || GetTickCount64()-started>120000) { hr=HRESULT_FROM_WIN32(ERROR_CANCELLED);goto done; }
        if(!WinHttpSendRequest(request,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0) || !WinHttpReceiveResponse(request,NULL)) goto done;
        DWORD status=0,length=sizeof status;if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&length,WINHTTP_NO_HEADER_INDEX)) goto done;
        if(status==301 || status==302 || status==303 || status==307 || status==308) {
            wchar_t location[8192];length=sizeof location;if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_LOCATION,WINHTTP_HEADER_NAME_BY_INDEX,location,&length,WINHTTP_NO_HEADER_INDEX) || !safe_https_url(location)) { hr=E_ACCESSDENIED;goto done; }
            StringCchCopyW(current,ARRAYSIZE(current),location);WinHttpCloseHandle(request);request=NULL;WinHttpCloseHandle(connection);connection=NULL;continue;
        }
        if(status!=200) { hr=HRESULT_FROM_WIN32(ERROR_WINHTTP_INVALID_SERVER_RESPONSE);goto done; }
        wchar_t size[40];length=sizeof size;if(WinHttpQueryHeaders(request,WINHTTP_QUERY_CONTENT_LENGTH,WINHTTP_HEADER_NAME_BY_INDEX,size,&length,WINHTTP_NO_HEADER_INDEX)) { uint64_t bytes=_wcstoui64(size,NULL,10);if(bytes>maximum || (expected && bytes!=expected)) { hr=HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);goto done; }if(bytes) total=bytes; }
        file=CreateFileW(temp,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);if(file==INVALID_HANDLE_VALUE) goto done;
        if(status_callback)status_callback(DownloadStage::receiving,context);
        BYTE buffer[8192];
        for(;;) {
            if(cancel && InterlockedCompareExchange(cancel,0,0)) { hr=HRESULT_FROM_WIN32(ERROR_CANCELLED);goto done; }
            if(GetTickCount64()-started>120000) { hr=HRESULT_FROM_WIN32(ERROR_TIMEOUT);goto done; }
            DWORD available=0;if(!WinHttpQueryDataAvailable(request,&available))goto done;if(!available)break;
            DWORD count=0;if(!WinHttpReadData(request,buffer,std::min<DWORD>(available,sizeof buffer),&count)) goto done;if(!count) break;
            if(count>maximum-received) { hr=HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);goto done; }
            DWORD written=0;if(!WriteFile(file,buffer,count,&written,NULL) || written!=count) goto done;
            if(hashing && BCryptHashData(hash.hash,buffer,count,0)<0) goto done;received+=count;
            ULONGLONG now=GetTickCount64();if(progress && now-last_progress>=80) { progress(received,total,context);last_progress=now; }
        }
        if(status_callback)status_callback(DownloadStage::verifying,context);if(progress)progress(received,total,context);
        if(!received || (expected && received!=expected)) { hr=HRESULT_FROM_WIN32(ERROR_INVALID_DATA);goto done; }
        if(hashing && (!hash_end(&hash,actual,ARRAYSIZE(actual)) || _wcsicmp(actual,digest))) { hr=HRESULT_FROM_WIN32(ERROR_CRC);goto done; }
        if(!FlushFileBuffers(file)) goto done;CloseHandle(file);file=INVALID_HANDLE_VALUE;
        if(!MoveFileExW(temp,destination,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) goto done;
        if(progress) progress(received,received,context);hr=S_OK;goto done;
    }
    hr=HRESULT_FROM_WIN32(ERROR_TOO_MANY_LINKS);
done:
    if(FAILED(hr) && hr==E_FAIL) { DWORD error=GetLastError();if(error) hr=HRESULT_FROM_WIN32(error); }
    if(file!=INVALID_HANDLE_VALUE) CloseHandle(file);if(FAILED(hr)) DeleteFileW(temp);
    if(request) WinHttpCloseHandle(request);if(connection) WinHttpCloseHandle(connection);if(session) WinHttpCloseHandle(session);if(hashing) hash_close(&hash);return hr;
}
