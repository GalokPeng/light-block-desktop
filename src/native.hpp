#pragma once
#include <windows.h>
#include <memory>
#include <utility>
#include <string>
#include <vector>
#include <atomic>
#include <new>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include <winrt/base.h>

namespace lb {
class unique_handle {
    HANDLE value_{};
public:
    unique_handle() noexcept=default;
    explicit unique_handle(HANDLE value) noexcept:value_(value) {}
    ~unique_handle() { reset(); }
    unique_handle(unique_handle const&)=delete;
    unique_handle& operator=(unique_handle const&)=delete;
    unique_handle(unique_handle&& other) noexcept:value_(other.release()) {}
    unique_handle& operator=(unique_handle&& other) noexcept { if(this!=&other) reset(other.release());return *this; }
    HANDLE get() const noexcept { return value_; }
    explicit operator bool() const noexcept { return value_ && value_!=INVALID_HANDLE_VALUE; }
    HANDLE release() noexcept { return std::exchange(value_,nullptr); }
    void reset(HANDLE value=nullptr) noexcept { if(value_ && value_!=INVALID_HANDLE_VALUE) CloseHandle(value_);value_=value; }
    unique_handle& operator=(HANDLE value) noexcept { reset(value);return *this; }
};
template<class T> class unique_gdi {
    T value_{};
public:
    unique_gdi() noexcept=default;
    explicit unique_gdi(T value) noexcept:value_(value) {}
    ~unique_gdi() { reset(); }
    unique_gdi(unique_gdi const&)=delete;
    unique_gdi& operator=(unique_gdi const&)=delete;
    unique_gdi(unique_gdi&& other) noexcept:value_(other.release()) {}
    unique_gdi& operator=(unique_gdi&& other) noexcept { if(this!=&other) reset(other.release());return *this; }
    T get() const noexcept { return value_; }
    explicit operator bool() const noexcept { return value_!=nullptr; }
    T release() noexcept { return std::exchange(value_,nullptr); }
    void reset(T value=nullptr) noexcept { if(value_) DeleteObject(value_);value_=value; }
};
template<class F> class scope_exit {
    F function_;bool active_{true};
public:
    explicit scope_exit(F function):function_(std::move(function)) {}
    ~scope_exit() noexcept { if(active_) function_(); }
    scope_exit(scope_exit const&)=delete;
    scope_exit& operator=(scope_exit const&)=delete;
    void release() noexcept { active_=false; }
};
template<class F> scope_exit(F)->scope_exit<F>;
class com_apartment {
    HRESULT result_;
public:
    explicit com_apartment(DWORD model=COINIT_APARTMENTTHREADED) noexcept:result_(CoInitializeEx(nullptr,model)) {}
    ~com_apartment() { if(SUCCEEDED(result_)) CoUninitialize(); }
    HRESULT result() const noexcept { return result_; }
    com_apartment(com_apartment const&)=delete;
    com_apartment& operator=(com_apartment const&)=delete;
};
}
