#pragma once
#include <stdint.h>
#include <wchar.h>
// No STL, Qt, ownership-bearing strings or HWNDs cross the compiler boundary.
enum { LB_FILES_ABI=2, LB_FILES_PATH=4096, LB_FILES_ICON_SIZE=64 };
enum { LB_FILES_SCAN, LB_FILES_PREVIEW, LB_FILES_HASH, LB_FILES_PLAN, LB_FILES_APPLY, LB_FILES_UNDO, LB_FILES_RECOVER };
enum { LB_FILES_NONE, LB_FILES_IMAGE, LB_FILES_TEXT, LB_FILES_FOLDER, LB_FILES_UNSUPPORTED };
struct FileToolsRequest {
    int action,count;wchar_t const* root;wchar_t const* const* paths;wchar_t const* pattern;uint64_t plan_serial;
};
struct FileToolsStatus {
    uint64_t serial,plan_serial,size,modified;int busy,action,ok,cancelled,count,plan_count,kind,width,height,truncated;
    wchar_t root[LB_FILES_PATH],path[LB_FILES_PATH],message[2048];
};
struct FileToolsEntry { uint64_t size,modified;int directory;wchar_t path[LB_FILES_PATH],name[512]; };
struct FileToolsPair { wchar_t source[LB_FILES_PATH],target[LB_FILES_PATH]; };
struct FileToolsUiApi {
    uint32_t version,size;
    void* (*create)(wchar_t const* data_directory);
    void (*release)(void*);
    uint64_t (*start)(void*,FileToolsRequest const*);
    void (*cancel)(void*);
    int (*poll)(void*,FileToolsStatus*);
    int (*entry)(void*,int,FileToolsEntry*);
    int (*pair)(void*,int,FileToolsPair*);
    int (*text)(void*,wchar_t*,int);
    int (*pixels)(void*,uint32_t*,int);
    void (*invalidate_plan)(void*);
    // Ready: 64*64 premultiplied BGRA pixels. 0: queued. -1: stale/invalid.
    // Identity is checked so a cancelled folder scan cannot rebind an old row.
    int (*icon)(void*,int,wchar_t const* expected_path,uint64_t modified,uint32_t*,int);
};
