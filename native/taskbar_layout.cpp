// Windows 11 Explorer taskbar layout for Light Block Desktop.
#include <windows.h>
#include <ocidl.h>
#include <xamlom.h>
#undef GetCurrentTime
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Media.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace winrt;
using namespace Windows::UI;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using Windows::UI::Core::CoreDispatcher;
using Windows::UI::Core::CoreDispatcherPriority;

namespace {
constexpr CLSID layoutId{0x1024237c, 0xb66a, 0x4f25, {0xba,0x99,0xfb,0x65,0xe9,0x4e,0x76,0x03}};
constexpr double sectionGap = 5;

struct Handle {
    HANDLE value{};
    ~Handle() { if (value) CloseHandle(value); }
};

Handle event_handle(wchar_t const* suffix, DWORD access) {
    auto name = L"Local\\Qingkuai.TaskbarLayout." + std::to_wstring(GetCurrentProcessId()) + L"." + suffix;
    return {OpenEventW(access, FALSE, name.c_str())};
}

FrameworkElement child_named(DependencyObject const& node, std::wstring_view name) {
    for (int i=0; i<Media::VisualTreeHelper::GetChildrenCount(node); ++i) {
        auto element = Media::VisualTreeHelper::GetChild(node, i).try_as<FrameworkElement>();
        if (element && element.Name() == name) return element;
    }
    return nullptr;
}

bool chinese() { return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE; }

bool sleep_now() {
    Handle token;
    if (!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY|TOKEN_ADJUST_PRIVILEGES,&token.value)) return false;
    TOKEN_PRIVILEGES enable{1}, previous{};
    if (!LookupPrivilegeValueW(nullptr,SE_SHUTDOWN_NAME,&enable.Privileges[0].Luid)) return false;
    enable.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    DWORD size{};
    if (!AdjustTokenPrivileges(token.value,FALSE,&enable,sizeof(previous),&previous,&size) ||
        GetLastError() == ERROR_NOT_ALL_ASSIGNED) return false;
    bool suspended = false;
    if (auto module = LoadLibraryExW(L"powrprof.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        using Suspend = BOOLEAN (WINAPI*)(BOOLEAN, BOOLEAN, BOOLEAN);
        if (auto suspend = reinterpret_cast<Suspend>(GetProcAddress(module, "SetSuspendState"))) {
            suspended = suspend(FALSE,FALSE,FALSE);
        }
        FreeLibrary(module);
    }
    AdjustTokenPrivileges(token.value,FALSE,&previous,0,nullptr,nullptr);
    return suspended;
}

void power_action(int action) {
    if (action == 0) {
        LockWorkStation();
        return;
    }
    if (action == 1) {
        if (!sleep_now()) MessageBoxW(nullptr,chinese()?L"Windows 无法进入睡眠。":L"Windows couldn't enter sleep.",
                                      L"Qingkuai",MB_OK|MB_ICONWARNING);
        return;
    }
    auto prompt = chinese() ? (action == 2 ? L"立即重启电脑？" : L"立即关闭电脑？")
                            : (action == 2 ? L"Restart now?" : L"Shut down now?");
    if (MessageBoxW(nullptr, prompt, L"Qingkuai", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
    wchar_t system[MAX_PATH]{};
    if (!GetSystemDirectoryW(system, MAX_PATH)) return;
    auto program = std::wstring(system) + L"\\shutdown.exe";
    auto command = L"\"" + program + (action == 2 ? L"\" /r /t 0" : L"\" /s /t 0");
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION child{};
    if (CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                       nullptr, nullptr, &startup, &child)) {
        CloseHandle(child.hThread);
        CloseHandle(child.hProcess);
    }
}

struct Layout {
    FrameworkElement frame{nullptr};
    FrameworkElement tray{nullptr};
    FrameworkElement repeater{nullptr};
    FrameworkElement start{nullptr};
    Grid panel{nullptr};
    Grid host{nullptr};
    Canvas back{nullptr};
    Border controls{nullptr}, apps{nullptr}, trayBack{nullptr};
    Button power{nullptr};
    event_token arranged{};
    std::vector<std::function<void()>> undo;
    bool observing{};

    template<typename Value>
    void set(DependencyObject const& target, DependencyProperty const& property, Value const& value) {
        auto old = target.ReadLocalValue(property);
        undo.emplace_back([target, property, old] {
            if (old == DependencyProperty::UnsetValue()) target.ClearValue(property);
            else target.SetValue(property, old);
        });
        target.SetValue(property, box_value(value));
    }

    void add(Panel const& parent, UIElement const& item, bool behind) {
        if (behind) parent.Children().InsertAt(0, item);
        else parent.Children().Append(item);
        undo.emplace_back([parent, item] {
            uint32_t index;
            if (parent.Children().IndexOf(item, index)) parent.Children().RemoveAt(index);
        });
    }

    static Border background(ElementTheme theme) {
        const auto tint = theme == ElementTheme::Light ? Color{255,235,242,249} : Color{255,31,43,56};
        Media::AcrylicBrush brush;
        brush.BackgroundSource(Media::AcrylicBackgroundSource::Backdrop);
        brush.TintColor(tint);
        brush.TintOpacity(.79);
        brush.FallbackColor(tint);
        Border border;
        border.Background(brush);
        border.CornerRadius({16,16,16,16});
        border.BorderThickness({1,1,1,1});
        border.BorderBrush(Media::SolidColorBrush(Color{66,141,169,193}));
        border.IsHitTestVisible(false);
        return border;
    }

    bool apply(FrameworkElement const& taskFrame) {
        frame = taskFrame;
        host = Media::VisualTreeHelper::GetParent(frame).try_as<Grid>();
        panel = child_named(frame,L"RootGrid").try_as<Grid>();
        if (!host || !panel) return false;
        repeater = child_named(panel,L"TaskbarFrameRepeater");
        auto originalBackground = child_named(panel,L"BackgroundControl");
        for (auto child : host.Children()) {
            if (get_class_name(child) == L"SystemTray.SystemTrayFrame") tray = child.try_as<FrameworkElement>();
        }
        if (!repeater || !tray || !originalBackground) return false;
        for (int i=0; i<Media::VisualTreeHelper::GetChildrenCount(repeater); ++i) {
            auto child = Media::VisualTreeHelper::GetChild(repeater,i).try_as<FrameworkElement>();
            if (!child) continue;
            if (Automation::AutomationProperties::GetAutomationId(child) == L"StartButton") start = child;
        }
        if (!start) return false;

        auto columns = host.ColumnDefinitions();
        std::vector<ColumnDefinition> previous;
        for (auto item : columns) previous.push_back(item);
        undo.emplace_back([columns, previous] {
            columns.Clear();
            for (auto item : previous) columns.Append(item);
        });
        columns.Clear();
        for (auto unit : {GridUnitType::Star, GridUnitType::Auto, GridUnitType::Auto, GridUnitType::Star}) {
            ColumnDefinition column;
            column.Width(GridLength{1, unit});
            columns.Append(column);
        }
        set(host, FrameworkElement::HorizontalAlignmentProperty(), HorizontalAlignment::Stretch);
        set(frame, Grid::ColumnProperty(), 1);
        set(frame, FrameworkElement::WidthProperty(), std::numeric_limits<double>::quiet_NaN());
        set(frame, FrameworkElement::HorizontalAlignmentProperty(), HorizontalAlignment::Right);
        set(frame, FrameworkElement::MarginProperty(), Thickness{0,0,sectionGap,0});
        set(tray, Grid::ColumnProperty(), 2);
        set(tray, FrameworkElement::WidthProperty(), std::numeric_limits<double>::quiet_NaN());
        set(tray, FrameworkElement::HorizontalAlignmentProperty(), HorizontalAlignment::Left);
        set(tray, FrameworkElement::MarginProperty(), Thickness{0,0,0,0});
        set(originalBackground, UIElement::VisibilityProperty(), Visibility::Collapsed);
        for (int i=0; i<Media::VisualTreeHelper::GetChildrenCount(repeater); ++i) {
            auto child = Media::VisualTreeHelper::GetChild(repeater,i).try_as<FrameworkElement>();
            if (!child) continue;
            auto id = Automation::AutomationProperties::GetAutomationId(child);
            if (id == L"WidgetsButton" || id == L"TaskViewButton" || child.Name() == L"AugmentedEntryPointButton") {
                set(child, UIElement::VisibilityProperty(), Visibility::Collapsed);
            }
        }
        set(start, UIElement::OpacityProperty(), 0.0);
        set(start, UIElement::IsHitTestVisibleProperty(), false);
        set(start, Control::IsTabStopProperty(), false);
        set(start, Automation::AutomationProperties::AccessibilityViewProperty(), Automation::Peers::AccessibilityView::Raw);

        auto theme = panel.ActualTheme();
        back = Canvas();
        back.IsHitTestVisible(false);
        add(panel, back, true);
        controls = background(theme);
        apps = background(theme);
        back.Children().Append(controls);
        back.Children().Append(apps);
        trayBack = background(theme);
        Grid::SetColumn(trayBack, 2);
        trayBack.Margin({0,3,0,3});
        add(host, trayBack, true);

        power = Button();
        power.Name(L"QingkuaiPowerButton");
        Automation::AutomationProperties::SetAutomationId(power,L"QingkuaiPowerButton");
        Automation::AutomationProperties::SetName(power,chinese() ? L"电源" : L"Power");
        ToolTipService::SetToolTip(power,box_value(chinese() ? L"电源" : L"Power"));
        FontIcon icon;
        icon.FontFamily(Media::FontFamily(L"Segoe Fluent Icons"));
        icon.Glyph(L"\uE7E8");
        icon.FontSize(17);
        power.Content(icon);
        power.Padding({0,0,0,0});
        power.BorderThickness({0,0,0,0});
        power.Background(Media::SolidColorBrush(Color{0,0,0,0}));
        power.HorizontalAlignment(HorizontalAlignment::Left);
        power.VerticalAlignment(VerticalAlignment::Center);
        power.Height(34);
        power.Click([](Windows::Foundation::IInspectable const& sender, RoutedEventArgs const&) {
            try {
                MenuFlyout menu;
                menu.Placement(Primitives::FlyoutPlacementMode::Top);
                auto labels = chinese() ? std::array{L"锁定",L"睡眠",L"重启",L"关机"} :
                                          std::array{L"Lock",L"Sleep",L"Restart",L"Shut down"};
                for (int i=0; i<4; ++i) {
                    MenuFlyoutItem entry;
                    entry.Text(labels[i]);
                    entry.Click([i](auto const&, auto const&) { power_action(i); });
                    menu.Items().Append(entry);
                }
                menu.ShowAt(sender.as<FrameworkElement>());
            } catch (...) {}
        });
        add(panel, power, false);
        arranged = panel.LayoutUpdated([this](auto const&, auto const&) { try { align(); } catch (...) {} });
        observing = true;
        align();
        return true;
    }

    void border_geometry(Border const& border, double x, double width, double height) {
        if (std::abs(Canvas::GetLeft(border)-x) > .25 || std::isnan(Canvas::GetLeft(border))) Canvas::SetLeft(border,x);
        if (std::abs(border.Width()-width) > .25 || std::isnan(border.Width())) border.Width(width);
        if (std::abs(border.Height()-height) > .25 || std::isnan(border.Height())) border.Height(height);
        if (Canvas::GetTop(border) != 3) Canvas::SetTop(border,3);
    }

    void align() {
        double width = panel.ActualWidth();
        double height = panel.ActualHeight() - 6;
        if (width < 20 || height < 10) return;
        double left = start.TransformToVisual(panel).TransformPoint({0,0}).X;
        double powerWidth = start.ActualWidth();
        if (powerWidth < 10) return;
        double seam = left + powerWidth;
        for (int i=0; i<Media::VisualTreeHelper::GetChildrenCount(repeater); ++i) {
            auto child = Media::VisualTreeHelper::GetChild(repeater,i).try_as<FrameworkElement>();
            if (child && child.Visibility() == Visibility::Visible &&
                get_class_name(child) == L"Taskbar.TaskbarExtensionElement" && child.ActualWidth() > 5) {
                auto x = child.TransformToVisual(panel).TransformPoint({0,0}).X;
                if (x >= left && x + child.ActualWidth() <= width+1) seam = std::max(seam,x+child.ActualWidth());
            }
        }
        auto appsLeft = std::min(width,seam + sectionGap / 2);
        border_geometry(controls,left,std::max(0.,seam-left-sectionGap/2),height);
        border_geometry(apps,appsLeft,std::max(0.,width-appsLeft),height);
        if (power.Margin().Left != left+3) power.Margin({left+3,0,0,0});
        if (power.Width() != powerWidth-6) power.Width(powerWidth-6);
    }

    ~Layout() {
        if (observing) try { panel.LayoutUpdated(arranged); } catch (...) {}
        for (auto it=undo.rbegin(); it!=undo.rend(); ++it) try { (*it)(); } catch (...) {}
    }
};

// Explicitly undo on the dispatcher. Explorer's thread teardown may run
// after XAML has already shut down.
thread_local Layout* current{};
thread_local weak_ref<FrameworkElement> lastFrame;

void restore_layout() { delete std::exchange(current,nullptr); }

struct Watcher : implements<Watcher,IVisualTreeServiceCallback2> {
    com_ptr<IXamlDiagnostics> diagnostics;
    std::atomic_bool active{};
    slim_mutex dispatchLock;
    CoreDispatcher dispatcher{nullptr};
    Handle start = event_handle(L"Start",SYNCHRONIZE);
    Handle stop = event_handle(L"Stop",SYNCHRONIZE);
    Handle ready = event_handle(L"Ready",EVENT_MODIFY_STATE);
    Handle restored = event_handle(L"Restored",EVENT_MODIFY_STATE);
    Handle resident = event_handle(L"Resident",EVENT_MODIFY_STATE);

    explicit Watcher(IUnknown* site) { check_hresult(site->QueryInterface(IID_PPV_ARGS(diagnostics.put()))); }

    void apply_layout() {
        if (!active) return;
        if (auto frame=lastFrame.get()) {
            if (current && current->frame == frame) return;
            restore_layout();
            auto next = std::make_unique<Layout>();
            if (next->apply(frame)) {
                current = next.release();
                SetEvent(ready.value);
            }
        }
    }

    HRESULT STDMETHODCALLTYPE OnVisualTreeChange(ParentChildRelation, VisualElement node, VisualMutationType type) noexcept override {
        try {
            if (type != Add || !node.Type || std::wstring_view(node.Type) != L"Taskbar.TaskbarFrame") return S_OK;
            auto bar = FindWindowW(L"Shell_TrayWnd",nullptr);
            if (!bar || GetWindowThreadProcessId(bar,nullptr) != GetCurrentThreadId()) return S_OK;
            Windows::Foundation::IInspectable item;
            check_hresult(diagnostics->GetIInspectableFromHandle(node.Handle,reinterpret_cast<::IInspectable**>(put_abi(item))));
            auto frame = item.as<FrameworkElement>();
            auto queue = frame.Dispatcher();
            {
                slim_lock_guard lock(dispatchLock);
                dispatcher = queue;
            }
            queue.RunAsync(CoreDispatcherPriority::Low,[self=get_strong(), weak=make_weak(frame)] {
                try {
                    if (!weak.get()) return;
                    lastFrame = weak;
                    self->apply_layout();
                } catch (...) {}
            });
        } catch (...) {}
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnElementStateChanged(InstanceHandle, VisualElementState, LPCWSTR) noexcept override { return S_OK; }

    static DWORD WINAPI run(void* value) noexcept {
        auto self = static_cast<Watcher*>(value);
        try {
            auto service = self->diagnostics.as<IVisualTreeService3>();
            check_hresult(service->AdviseVisualTreeChange(self));
            SetEvent(self->resident.value);
            // Keep a single diagnostics subscription per Explorer lifetime.
            // Toggling only changes our own XAML properties on its UI thread.
            HANDLE commands[]{self->stop.value,self->start.value};
            for (;;) {
                DWORD command = WaitForMultipleObjects(2,commands,FALSE,INFINITE);
                if (command != WAIT_OBJECT_0 && command != WAIT_OBJECT_0+1) break;
                self->active = command == WAIT_OBJECT_0+1;
                CoreDispatcher queue{nullptr};
                {
                    slim_lock_guard lock(self->dispatchLock);
                    queue = self->dispatcher;
                }
                if (queue) queue.RunAsync(CoreDispatcherPriority::Normal,[hold=self->get_strong(), command] {
                    try {
                        if (command == WAIT_OBJECT_0) {
                            restore_layout();
                            SetEvent(hold->restored.value);
                        } else hold->apply_layout();
                    } catch (...) {}
                });
                else if (command == WAIT_OBJECT_0) SetEvent(self->restored.value);
            }
        } catch (...) { SetEvent(self->restored.value); }
        self->Release();
        return 0;
    }
};

struct Site : implements<Site,IObjectWithSite> {
    com_ptr<IUnknown> site;
    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* value) noexcept override {
        try {
            site.copy_from(value);
            if (!site) return S_OK;
            auto watcher = make_self<Watcher>(value);
            if (!watcher->start.value || !watcher->stop.value || !watcher->ready.value ||
                !watcher->restored.value || !watcher->resident.value) return E_ACCESSDENIED;
            watcher->AddRef();
            Handle thread{CreateThread(nullptr,0,Watcher::run,watcher.get(),0,nullptr)};
            if (!thread.value) { watcher->Release(); return E_FAIL; }
            return S_OK;
        } catch (...) { return to_hresult(); }
    }
    HRESULT STDMETHODCALLTYPE GetSite(REFIID riid, void** output) noexcept override {
        return site ? site->QueryInterface(riid,output) : E_FAIL;
    }
};

struct Factory : implements<Factory,IClassFactory> {
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** output) noexcept override {
        if (outer) return CLASS_E_NOAGGREGATION;
        try { return make_self<Site>()->QueryInterface(iid,output); }
        catch (...) { return to_hresult(); }
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) noexcept override { return S_OK; }
};
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllGetClassObject(REFCLSID id, REFIID iid, void** output) {
    if (id != layoutId) return CLASS_E_CLASSNOTAVAILABLE;
    try { return make_self<Factory>()->QueryInterface(iid,output); }
    catch (...) { return to_hresult(); }
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllCanUnloadNow() { return S_FALSE; }
