#ifndef LB_NETWORK_H
#define LB_NETWORK_H
#include "lb.h"
typedef void (*DownloadProgress)(uint64_t received,uint64_t total,void *context);
HRESULT download_https(const wchar_t *url,const wchar_t *destination,uint64_t maximum,uint64_t expected,const wchar_t *algorithm,const wchar_t *digest,volatile LONG *cancel,DownloadProgress progress,void *context);
BOOL hash_file(const wchar_t *file,const wchar_t *algorithm,wchar_t *out,size_t capacity,std::atomic_bool const* cancel=nullptr);
BOOL hash_memory(const void *data,size_t size,const wchar_t *algorithm,wchar_t *out,size_t capacity);
BOOL safe_https_url(const wchar_t *url);
BOOL safe_filename(const wchar_t *name);
#endif
