#pragma once
#include <stdint.h>
#include <wchar.h>
enum { LB_ONLINE_CATALOG_ABI=1 };
struct UiCatalogApi {
    uint32_t version,size;
    wchar_t const* (*path)();
    int (*install)(wchar_t const* downloaded_file);
};
UiCatalogApi const* online_catalog_api();
bool online_catalog_refresh_tests(wchar_t const* directory);
