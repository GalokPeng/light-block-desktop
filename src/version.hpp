#pragma once
// The only release-version source. Packaging and Windows resources read this file.
#define LB_VERSION_UTF8 "1.1.0-beta.2"
#define LB_VERSION_NUMERIC_UTF8 "1.1.0.0"
#define LB_WIDEN_IMPL(value) L##value
#define LB_WIDEN(value) LB_WIDEN_IMPL(value)
#define LB_VERSION LB_WIDEN(LB_VERSION_UTF8)
#define LB_VERSION_COMMA 1,1,0,0
#define LB_PREVIEW_BUILD 1
#define LB_PRODUCT_NAME L"轻块桌面测试版（极度优化）"
#define LB_PREVIEW_LABEL L"测试版（极度优化）"
#define LB_PRODUCT_ENGLISH L"Light Block Desktop Preview"
#define LB_PRODUCT_EXE L"轻块桌面测试版.exe"
#define LB_INSTALL_DIRECTORY L"Light Block Desktop Preview"
#define LB_PROFILE_DIRECTORY L"LightBlockDesktopPreview"
#define LB_AUTORUN_KEY L"LightBlockDesktopPreview"
#define LB_DESKTOP_MENU_PREFIX L"LightBlockDesktopPreview"
#define LB_INSTALLER_APP_ID "{C07C418A-08FE-4E9B-8136-348822397B6B}"
#define LB_INSTALL_APP_ID L"{C07C418A-08FE-4E9B-8136-348822397B6B}"
