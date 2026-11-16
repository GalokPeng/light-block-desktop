cmake_minimum_required(VERSION 3.24)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/src/version.hpp" RELEASE_VERSION_LINE REGEX "^#define LB_VERSION_UTF8")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" RELEASE_VERSION "${RELEASE_VERSION_LINE}")
project(LightBlockDesktopPreview VERSION ${RELEASE_VERSION} LANGUAGES CXX RC)
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Windows x64 is required. For LLVM-MinGW pass -DCMAKE_CXX_COMPILER_TARGET=x86_64-w64-mingw32.")
endif()
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
file(GLOB SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_executable(LightBlockDesktopNative WIN32 ${SOURCES} resources/app.rc)
set_target_properties(LightBlockDesktopNative PROPERTIES OUTPUT_NAME "轻块桌面测试版")
target_compile_definitions(LightBlockDesktopNative PRIVATE WINVER=0x0A00 _WIN32_WINNT=0x0A00)
target_compile_definitions(LightBlockDesktopNative PRIVATE UNICODE _UNICODE)
target_include_directories(LightBlockDesktopNative PRIVATE resources "${CMAKE_SOURCE_DIR}/build")
target_link_libraries(LightBlockDesktopNative PRIVATE user32 gdi32 shell32 comctl32 shlwapi dwmapi ole32 oleaut32 windowscodecs uuid mfuuid propsys msimg32 comdlg32 psapi advapi32 winhttp bcrypt runtimeobject gdiplus)
if(MINGW)
  target_link_options(LightBlockDesktopNative PRIVATE -municode -static)
endif()
if(MSVC)
  target_compile_options(LightBlockDesktopNative PRIVATE /utf-8 /W4)
endif()
add_custom_command(TARGET LightBlockDesktopNative POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:LightBlockDesktopNative>/assets)
file(SHA256 "${CMAKE_SOURCE_DIR}/native/taskbar_layout.cpp" LAYOUT_HASH)
string(SUBSTRING "${LAYOUT_HASH}" 0 12 LAYOUT_BUILD)
add_library(NativeTaskbar MODULE native/taskbar_layout.cpp)
set_target_properties(NativeTaskbar PROPERTIES PREFIX "" OUTPUT_NAME "LightBlockNativeTaskbar-${LAYOUT_BUILD}")
target_compile_definitions(NativeTaskbar PRIVATE UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00 LB_NATIVE_LAYOUT_BUILD=0x${LAYOUT_BUILD}ULL)
target_link_libraries(NativeTaskbar PRIVATE advapi32 ole32 oleaut32 runtimeobject uuid shell32)
if(MINGW)
  target_link_options(NativeTaskbar PRIVATE -static)
  target_compile_options(NativeTaskbar PRIVATE -Wno-dll-attribute-on-redeclaration)
endif()
add_dependencies(LightBlockDesktopNative NativeTaskbar)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/taskbar-current.txt" CONTENT "$<TARGET_FILE_NAME:NativeTaskbar>\n")
find_program(POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
add_custom_target(LightBlockRecordingRuntime
  COMMAND ${POWERSHELL_EXECUTABLE} -NoProfile -File "${CMAKE_SOURCE_DIR}/tools/stage_recording.ps1"
    -PythonPath "${Python3_EXECUTABLE}" -DestinationDirectory "$<TARGET_FILE_DIR:LightBlockDesktopNative>/runtime/recording"
  VERBATIM)
add_dependencies(LightBlockDesktopNative LightBlockRecordingRuntime)
add_executable(QingkuaiUpdate native/update_launcher.cpp)
target_compile_definitions(QingkuaiUpdate PRIVATE UNICODE _UNICODE)
target_link_libraries(QingkuaiUpdate PRIVATE bcrypt shell32)
if(MINGW)
  target_link_options(QingkuaiUpdate PRIVATE -municode -static)
endif()
add_dependencies(LightBlockDesktopNative QingkuaiUpdate)
option(LIGHT_BLOCK_BUILD_QT_UI "Build and stage the default C++ Qt Widgets frontend" ON)
set(LIGHT_BLOCK_QT_ROOT "${CMAKE_SOURCE_DIR}/build/qt-sdk" CACHE PATH "Pinned Qt 6.8.3 / GCC 13.1 SDK root")
if(LIGHT_BLOCK_BUILD_QT_UI)
  # The native backend uses LLVM-MinGW libc++; Qt uses its matching GCC ABI.
  # Build in a separate compiler invocation and cross only the ui_api.h C ABI.
  add_custom_target(LightBlockQtUi
    COMMAND ${POWERSHELL_EXECUTABLE} -NoProfile -File "${CMAKE_SOURCE_DIR}/tools/build_ui.ps1"
      -QtRoot "${LIGHT_BLOCK_QT_ROOT}" -DestinationDirectory "$<TARGET_FILE_DIR:LightBlockDesktopNative>"
    VERBATIM)
  add_dependencies(LightBlockDesktopNative LightBlockQtUi)
endif()
add_custom_command(TARGET LightBlockDesktopNative POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:LightBlockDesktopNative>/runtime
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:NativeTaskbar> $<TARGET_FILE_DIR:LightBlockDesktopNative>/runtime/
  COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_BINARY_DIR}/taskbar-current.txt" $<TARGET_FILE_DIR:LightBlockDesktopNative>/runtime/
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:QingkuaiUpdate> $<TARGET_FILE_DIR:LightBlockDesktopNative>/runtime/QingkuaiUpdate.exe
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/licenses" $<TARGET_FILE_DIR:LightBlockDesktopNative>/licenses)
