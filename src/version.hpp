#pragma once
// The only release-version source. Packaging and Windows resources read this file.
#define LB_VERSION_UTF8 "2.0.0"
#define LB_VERSION_NUMERIC_UTF8 "2.0.0.0"
#define LB_WIDEN_IMPL(value) L##value
#define LB_WIDEN(value) LB_WIDEN_IMPL(value)
#define LB_VERSION LB_WIDEN(LB_VERSION_UTF8)
#define LB_VERSION_COMMA 2,0,0,0
#define LB_PREVIEW_BUILD 0
#define LB_PRODUCT_NAME L"轻块桌面"
#define LB_PREVIEW_LABEL L""
#define LB_PRODUCT_ENGLISH L"Light Block Desktop"
#define LB_PRODUCT_EXE L"轻块桌面.exe"
#define LB_INSTALL_DIRECTORY L"Light Block Desktop"
#define LB_PROFILE_DIRECTORY L"StillWallpaper"
#define LB_AUTORUN_KEY L"StillWallpaper"
#define LB_DESKTOP_MENU_PREFIX L"LightBlockDesktop"
#define LB_INSTALLER_APP_ID "{3CD16B31-077E-4CE9-BB01-578642D94876}"
#define LB_INSTALL_APP_ID L"{3CD16B31-077E-4CE9-BB01-578642D94876}"
