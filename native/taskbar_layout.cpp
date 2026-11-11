// Windows 11 Explorer taskbar layout for Light Block Desktop.
#include <windows.h>
#include <shellapi.h>
#include <ocidl.h>
#include <xamlom.h>
#undef GetCurrentTime
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Data.h>
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
#include <type_traits>
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
constexpr double commandSize = 28;
constexpr double commandGap = 2;
constexpr double commandInset = 3;
constexpr double commandContentWidth = 3*commandSize+2*commandGap;
constexpr double commandSlotWidth = commandContentWidth+2*commandInset;
#ifndef QINGKUAI_LAYOUT_BUILD
#define QINGKUAI_LAYOUT_BUILD 0ULL
#endif
std::wstring component_channel() {
    return L"Local\\Qingkuai.TaskbarLayout.v3." + std::to_wstring(QINGKUAI_LAYOUT_BUILD) + L".";
}

// Geometry and counters only. Never include application names, paths or icons.
struct Status {
    LONG version, sequence, state, repairs, rebuilds, error;
    LONG changes, reserved;
    double hostWidth, frameWidth, trayWidth, groupLeft, groupRight, scale;
};
static_assert(sizeof(Status) == 80);
enum State { waiting, healthy, repairing, incompatible, conflict };

void publish(Status* status, State state, LONG repairs=0, LONG rebuilds=0, LONG error=0,
             double host=0, double frame=0, double tray=0, double left=0, double right=0, LONG changes=0, LONG gutters=0) {
    if (!status) return;
    InterlockedIncrement(&status->sequence);
    status->version = 3;
    status->state = state;
    status->repairs = repairs;
    status->rebuilds = rebuilds;
    status->error = error;
    status->changes = changes;
    status->reserved = gutters;
    status->hostWidth = host;
    status->frameWidth = frame;
    status->trayWidth = tray;
    status->groupLeft = left;
    status->groupRight = right;
    auto bar = FindWindowW(L"Shell_TrayWnd",nullptr);
    status->scale = bar ? GetDpiForWindow(bar)/96. : 1.;
    InterlockedIncrement(&status->sequence);
}

struct Handle {
    HANDLE value{};
    ~Handle() { if (value) CloseHandle(value); }
};

Handle event_handle(wchar_t const* suffix, DWORD access) {
    auto name = component_channel() + std::to_wstring(GetCurrentProcessId()) + L"." + suffix;
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

void launch_taskbar_tool(int tool) {
    std::wstring target;
    if (tool==0) {
        target=L"ms-settings:taskbar";
    } else if (tool==1) {
        wchar_t system[MAX_PATH]{};
        if (GetSystemDirectoryW(system,MAX_PATH)) target=std::wstring(system)+L"\\Taskmgr.exe";
    }
    bool opened=!target.empty() && reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",target.c_str(),nullptr,nullptr,SW_SHOWNORMAL))>32;
    if (!opened) MessageBoxW(nullptr,chinese()?L"Windows 无法打开此工具。":L"Windows couldn't open this tool.",
                             L"Qingkuai",MB_OK|MB_ICONWARNING);
}

Button command_button(wchar_t const* id, wchar_t const* label, UIElement const& icon) {
    Button button;
    button.Name(id);
    Automation::AutomationProperties::SetAutomationId(button,id);
    Automation::AutomationProperties::SetName(button,label);
    ToolTipService::SetToolTip(button,box_value(label));
    button.Content(icon);
    button.Padding({0,0,0,0});
    button.BorderThickness({0,0,0,0});
    button.Background(Media::SolidColorBrush(Color{0,0,0,0}));
    button.CornerRadius({7,7,7,7});
    button.MinWidth(0);
    button.MinHeight(0);
    button.Width(commandSize);
    button.Height(32);
    return button;
}

FontIcon command_glyph(wchar_t const* glyph) {
    FontIcon icon;
    icon.FontFamily(Media::FontFamily(L"Segoe Fluent Icons"));
    icon.Glyph(glyph);
    icon.FontSize(17);
    return icon;
}

PathIcon task_manager_icon() {
    // Native vector geometry also works on older Windows 11 icon fonts.
    Media::GeometryGroup chart;
    for (auto bounds : {Windows::Foundation::Rect{0,7,3,9}, {6,0,3,16}, {12,4,3,12}}) {
        Media::RectangleGeometry bar;
        bar.Rect(bounds);
        chart.Children().Append(bar);
    }
    PathIcon icon;
    icon.Data(chart);
    icon.Width(16);
    icon.Height(16);
    return icon;
}

struct Layout {
    struct Property {
        DependencyObject target{nullptr};
        DependencyProperty id{nullptr};
        Windows::Foundation::IInspectable original{nullptr}, wanted{nullptr};
        Data::BindingBase binding{nullptr};
        std::function<bool()> matches;
        int64_t token{};
        LONG mask{};
        bool watching{};

        void remember() {
            original = target.ReadLocalValue(id);
            binding = nullptr;
            if (auto element=target.try_as<FrameworkElement>()) {
                if (auto expression=element.GetBindingExpression(id)) binding = expression.ParentBinding();
            }
        }

        void restore() {
            if (binding) target.as<FrameworkElement>().SetBinding(id,binding);
            else if (original==DependencyProperty::UnsetValue()) target.ClearValue(id);
            else target.SetValue(id,original);
        }
    };
    FrameworkElement frame{nullptr};
    FrameworkElement tray{nullptr};
    FrameworkElement repeater{nullptr};
    FrameworkElement start{nullptr};
    Grid panel{nullptr};
    Grid host{nullptr};
    Canvas back{nullptr};
    Border controls{nullptr}, apps{nullptr}, trayBack{nullptr};
    Button power{nullptr}, settingsButton{nullptr}, taskManagerButton{nullptr};
    StackPanel commands{nullptr};
    std::function<void(int)> launchTool=launch_taskbar_tool;
    std::vector<FrameworkElement> nativeControls;
    event_token arranged{};
    std::vector<std::function<void()>> undo;
    std::vector<std::shared_ptr<Property>> properties;
    std::vector<ColumnDefinition> originalColumns, ourColumns;
    DispatcherTimer repairTimer;
    event_token repairTick{};
    Status* status{};
    LONG repairCount{}, rebuildCount{}, changes{};
    ULONGLONG burstStart{};
    unsigned burst{};
    bool writing{}, scheduled{}, exhausted{};
    bool observing{};

    explicit Layout(Status* status=nullptr, LONG repairs=0, LONG rebuilds=0, LONG changed=0)
        : status(status), repairCount(repairs), rebuildCount(rebuilds), changes(changed) {
        repairTimer.Interval(Windows::Foundation::TimeSpan{800000});
        repairTick=repairTimer.Tick([this](auto const&, auto const&) {
            repairTimer.Stop();
            scheduled=false;
            try { repair(); } catch (hresult_error const& e) { failure(e.code()); } catch (...) { failure(E_FAIL); }
        });
    }

    void failure(HRESULT code) {
        exhausted=true;
        publish(status,conflict,repairCount,rebuildCount,code,0,0,0,0,0,changes);
    }

    void schedule() {
        if (writing || scheduled || exhausted) return;
        scheduled=true;
        repairTimer.Start();
    }

    template<typename Value>
    void set(DependencyObject const& target, DependencyProperty const& property, Value const& value) {
        auto lease=std::make_shared<Property>();
        lease->target=target;
        lease->id=property;
        lease->wanted=box_value(value);
        lease->matches=[target,property,value] {
            auto actual=unbox_value<Value>(target.GetValue(property));
            if constexpr (std::is_same_v<Value,double>) {
                if (std::isnan(actual) && std::isnan(value)) return true;
                if (std::isfinite(actual) && std::isfinite(value)) return std::abs(actual-value)<.01;
            }
            return actual==value;
        };
        lease->remember();
        properties.push_back(lease);
        target.SetValue(property,lease->wanted);
        LONG mask=32;
        if (property==FrameworkElement::WidthProperty()) mask=target==frame ? 1 : target==tray ? 2 : target==start ? 128 : 4;
        else if (property==UIElement::RenderTransformProperty()) mask=256;
        else if (property==Grid::ColumnProperty() || property==Grid::ColumnSpanProperty()) mask=8;
        else if (property==FrameworkElement::HorizontalAlignmentProperty() || property==FrameworkElement::MarginProperty()) mask=16;
        lease->mask=mask;
        lease->token=target.RegisterPropertyChangedCallback(property,[this, mask, weak=std::weak_ptr(lease)](auto const&,auto const&) {
            try {
                auto property=weak.lock();
                if (!property || writing || property->matches()) return;
                // Keep Explorer's latest native value for disabling the style.
                property->remember();
                changes|=mask;
                schedule();
            } catch (...) { failure(E_FAIL); }
        });
        lease->watching=true;
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

        return decorate(originalBackground);
    }

    bool decorate(FrameworkElement const& originalBackground) {
        auto columns = host.ColumnDefinitions();
        for (auto item : columns) originalColumns.push_back(item);
        columns.Clear();
        for (auto unit : {GridUnitType::Star, GridUnitType::Auto, GridUnitType::Auto, GridUnitType::Star}) {
            ColumnDefinition column;
            column.Width(GridLength{1, unit});
            columns.Append(column);
            ourColumns.push_back(column);
        }
        set(host, FrameworkElement::HorizontalAlignmentProperty(), HorizontalAlignment::Stretch);
        set(frame, Grid::ColumnProperty(), 1);
        set(frame, Grid::ColumnSpanProperty(), 1);
        set(frame, FrameworkElement::WidthProperty(), std::numeric_limits<double>::quiet_NaN());
        set(frame, FrameworkElement::HorizontalAlignmentProperty(), HorizontalAlignment::Right);
        set(frame, FrameworkElement::MarginProperty(), Thickness{0,0,sectionGap,0});
        set(tray, Grid::ColumnProperty(), 2);
        set(tray, Grid::ColumnSpanProperty(), 1);
        set(tray, FrameworkElement::WidthProperty(), std::numeric_limits<double>::quiet_NaN());
        set(tray, FrameworkElement::HorizontalAlignmentProperty(), HorizontalAlignment::Left);
        set(tray, FrameworkElement::MarginProperty(), Thickness{0,0,0,0});
        set(repeater, FrameworkElement::WidthProperty(), std::numeric_limits<double>::quiet_NaN());
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
        // A compact, fixed slot for three actual commands, with no blank gutters.
        // Do not derive it from ActualWidth: that can still reflect the previous
        // component while XAML is completing its restore layout pass.
        set(start, FrameworkElement::WidthProperty(), commandSlotWidth);
        nativeControls=search_controls();

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

        power=command_button(L"QingkuaiPowerButton",chinese()?L"电源":L"Power",command_glyph(L"\uE7E8"));
        settingsButton=command_button(L"QingkuaiTaskbarSettingsButton",chinese()?L"任务栏设置":L"Taskbar settings",command_glyph(L"\uE713"));
        taskManagerButton=command_button(L"QingkuaiTaskManagerButton",chinese()?L"任务管理器":L"Task Manager",task_manager_icon());
        settingsButton.Click([action=launchTool](auto const&,auto const&) { action(0); });
        taskManagerButton.Click([action=launchTool](auto const&,auto const&) { action(1); });
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
        commands=StackPanel();
        commands.Orientation(Orientation::Horizontal);
        commands.Spacing(commandGap);
        commands.Width(commandContentWidth);
        commands.Height(32);
        commands.HorizontalAlignment(HorizontalAlignment::Left);
        commands.VerticalAlignment(VerticalAlignment::Center);
        commands.Children().Append(power);
        commands.Children().Append(settingsButton);
        commands.Children().Append(taskManagerButton);
        add(panel, commands, false);
        arranged = panel.LayoutUpdated([this](auto const&, auto const&) {
            try {
                if (!columns_match()) schedule();
                align();
            } catch (hresult_error const& e) { failure(e.code()); } catch (...) { failure(E_FAIL); }
        });
        observing = true;
        align();
        return true;
    }

    bool columns_match() const {
        auto columns=host.ColumnDefinitions();
        if (columns.Size()!=ourColumns.size()) return false;
        for (unsigned i=0; i<ourColumns.size(); ++i) {
            if (columns.GetAt(i)!=ourColumns[i]) return false;
            auto width=columns.GetAt(i).Width();
            auto unit=(i==0 || i==3) ? GridUnitType::Star : GridUnitType::Auto;
            if (width.Value!=1 || width.GridUnitType!=unit) return false;
        }
        return true;
    }

    void repair() {
        if (exhausted) return;
        auto columns=host.ColumnDefinitions();
        bool changed=!columns_match();
        if (changed) changes|=64;
        for (auto const& property : properties) if (!property->matches()) {
            changed=true;
            changes|=property->mask;
        }
        if (!changed) return;
        auto now=GetTickCount64();
        if (now-burstStart>5000) { burstStart=now; burst=0; }
        // Do not continually fight another taskbar customization component.
        if (++burst>12) { failure(E_ABORT); return; }
        publish(status,repairing,++repairCount,rebuildCount,0,0,0,0,0,0,changes);
        writing=true;
        try {
            if (!columns_match()) {
                bool foreign=columns.Size()!=ourColumns.size();
                for (unsigned i=0; !foreign && i<columns.Size(); ++i) foreign=columns.GetAt(i)!=ourColumns[i];
                if (foreign) {
                    originalColumns.clear();
                    for (auto column : columns) originalColumns.push_back(column);
                }
                columns.Clear();
                for (unsigned i=0; i<ourColumns.size(); ++i) {
                    ourColumns[i].Width({1,(i==0 || i==3) ? GridUnitType::Star : GridUnitType::Auto});
                    columns.Append(ourColumns[i]);
                }
            }
            for (auto const& property : properties) if (!property->matches()) {
                // Some shell builds don't notify every layout-property change.
                property->remember();
                property->target.SetValue(property->id,property->wanted);
            }
        } catch (...) { writing=false; throw; }
        writing=false;
    }

    bool connected() const {
        return Media::VisualTreeHelper::GetParent(frame)==host && child_named(frame,L"RootGrid")==panel &&
               child_named(panel,L"TaskbarFrameRepeater")==repeater && Media::VisualTreeHelper::GetParent(tray)==host &&
               Media::VisualTreeHelper::GetParent(start)==repeater && search_controls()==nativeControls;
    }

    std::vector<FrameworkElement> search_controls() const {
        std::vector<FrameworkElement> result;
        for (int i=0; i<Media::VisualTreeHelper::GetChildrenCount(repeater); ++i) {
            auto child=Media::VisualTreeHelper::GetChild(repeater,i).try_as<FrameworkElement>();
            if (!child) continue;
            auto id=Automation::AutomationProperties::GetAutomationId(child);
            if (get_class_name(child)==L"Taskbar.TaskbarExtensionElement" || id==L"SearchButton" || id==L"SearchBoxButton")
                result.push_back(child);
        }
        return result;
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
        if (exhausted || scheduled || width < 20 || height < 10) return;
        double left = start.TransformToVisual(panel).TransformPoint({0,0}).X;
        double powerWidth = start.ActualWidth();
        if (powerWidth < 10) return;
        double seam = left + powerWidth;
        double contentRight=seam;
        for (int i=0; i<Media::VisualTreeHelper::GetChildrenCount(repeater); ++i) {
            auto child = Media::VisualTreeHelper::GetChild(repeater,i).try_as<FrameworkElement>();
            if (child && child.Visibility()==Visibility::Visible && child.ActualWidth()>0) {
                auto x=child.TransformToVisual(panel).TransformPoint({0,0}).X;
                if (x>=left && x+child.ActualWidth()<=width+1) contentRight=std::max(contentRight,x+child.ActualWidth());
            }
            if (child && child.Visibility() == Visibility::Visible &&
                std::find(nativeControls.begin(),nativeControls.end(),child)!=nativeControls.end() && child.ActualWidth() > 5) {
                auto x = child.TransformToVisual(panel).TransformPoint({0,0}).X;
                if (x >= left && x + child.ActualWidth() <= width+1) seam = std::max(seam,x+child.ActualWidth());
            }
        }
        auto appsLeft = std::min(contentRight,seam + sectionGap / 2);
        border_geometry(controls,left,std::max(0.,seam-left-sectionGap/2),height);
        // A transient full-width frame must not paint an empty screen-wide pill.
        border_geometry(apps,appsLeft,std::max(0.,contentRight-appsLeft),height);
        if (std::abs(commands.Margin().Left-(left+commandInset))>.01) commands.Margin({left+commandInset,0,0,0});
        auto origin=frame.TransformToVisual(host).TransformPoint({0,0}).X;
        auto trayX=tray.TransformToVisual(host).TransformPoint({0,0}).X;
        auto end=trayX+tray.ActualWidth();
        auto commandLeft=commands.TransformToVisual(panel).TransformPoint({0,0}).X;
        auto commandRight=commandLeft+commands.ActualWidth();
        double controlsRight=commandRight;
        bool controlsFit=std::abs(powerWidth-commandSlotWidth)<1 &&
                         std::abs(commands.ActualWidth()-commandContentWidth)<1;
        for (auto const& control : nativeControls) if (control.Visibility()==Visibility::Visible && control.ActualWidth()>5) {
            auto x=control.TransformToVisual(panel).TransformPoint({0,0}).X;
            controlsFit=controlsFit && x>=commandRight-1;
            controlsRight=std::max(controlsRight,x+control.ActualWidth());
        }
        auto leftGutter=commandLeft-left;
        auto rightGutter=seam-sectionGap/2-controlsRight;
        auto valid=origin>=-1 && trayX>=origin+contentRight-1 && end<=host.ActualWidth()+1 &&
                   std::abs((origin+left+end)-host.ActualWidth())<=8 && controlsFit &&
                   leftGutter>=0 && leftGutter<=6 && rightGutter>=-3 && rightGutter<=6;
        LONG gutters=(static_cast<LONG>(std::clamp(std::round(rightGutter*100),0.,32767.))<<16) |
                     static_cast<LONG>(std::clamp(std::round(leftGutter*100),0.,32767.));
        publish(status,valid ? healthy : repairing,repairCount,rebuildCount,0,host.ActualWidth(),width,
                tray.ActualWidth(),origin+left,end,changes,gutters);
    }

    ~Layout() {
        writing=true;
        repairTimer.Stop();
        try { repairTimer.Tick(repairTick); } catch (...) {}
        if (observing) try { panel.LayoutUpdated(arranged); } catch (...) {}
        for (auto const& property : properties) if (property->watching) {
            try { property->target.UnregisterPropertyChangedCallback(property->id,property->token); } catch (...) {}
        }
        for (auto it=undo.rbegin(); it!=undo.rend(); ++it) try { (*it)(); } catch (...) {}
        for (auto it=properties.rbegin(); it!=properties.rend(); ++it) try { (*it)->restore(); } catch (...) {}
        if (host && !ourColumns.empty()) try {
            auto columns=host.ColumnDefinitions();
            // Leave an external replacement alone when it hasn't been repaired.
            bool ours=columns.Size()==ourColumns.size();
            for (unsigned i=0; ours && i<columns.Size(); ++i) ours=columns.GetAt(i)==ourColumns[i];
            if (ours) {
                columns.Clear();
                for (auto column : originalColumns) columns.Append(column);
            }
        } catch (...) {}
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
    std::atomic_bool queued{};
    slim_mutex dispatchLock;
    CoreDispatcher dispatcher{nullptr};
    Handle start = event_handle(L"Start",SYNCHRONIZE);
    Handle stop = event_handle(L"Stop",SYNCHRONIZE);
    Handle ready = event_handle(L"Ready",EVENT_MODIFY_STATE);
    Handle restored = event_handle(L"Restored",EVENT_MODIFY_STATE);
    Handle resident = event_handle(L"Resident",EVENT_MODIFY_STATE);
    Handle mapping;
    Status* status{};
    LONG repairs{}, rebuilds{}, changes{};
    unsigned missing{}, badGeometry{};
    bool blocked{};

    explicit Watcher(IUnknown* site) {
        check_hresult(site->QueryInterface(IID_PPV_ARGS(diagnostics.put())));
        auto name=component_channel()+std::to_wstring(GetCurrentProcessId())+L".Status";
        mapping.value=OpenFileMappingW(FILE_MAP_WRITE,FALSE,name.c_str());
        if (mapping.value) status=static_cast<Status*>(MapViewOfFile(mapping.value,FILE_MAP_WRITE,0,0,sizeof(Status)));
    }

    ~Watcher() { if (status) UnmapViewOfFile(status); }

    void apply_layout() {
        if (!active || blocked) return;
        if (auto frame=lastFrame.get()) {
            if (current) {
                repairs=current->repairCount;
                rebuilds=current->rebuildCount;
                changes=current->changes;
                if (current->exhausted) {
                    restore_layout();
                    blocked=true;
                    ResetEvent(ready.value);
                    publish(status,conflict,repairs,rebuilds,E_ABORT,0,0,0,0,0,changes);
                    return;
                }
                if (current->frame==frame && current->connected()) {
                    current->repair();
                    current->align();
                    if (status && status->state==healthy) SetEvent(ready.value);
                    else ResetEvent(ready.value);
                    if (status && status->state==repairing && !current->scheduled) {
                        if (++badGeometry>=4) {
                            restore_layout();
                            blocked=true;
                            ResetEvent(ready.value);
                            publish(status,incompatible,repairs,rebuilds,0,0,0,0,0,0,changes);
                        }
                    } else badGeometry=0;
                    return;
                }
                ++rebuilds;
            }
            restore_layout();
            ResetEvent(ready.value);
            auto next = std::make_unique<Layout>(status,repairs,rebuilds,changes);
            if (next->apply(frame)) {
                current = next.release();
                missing=badGeometry=0;
                if (status && status->state==healthy) SetEvent(ready.value);
            } else if (++missing>=6) {
                blocked=true;
                publish(status,incompatible,repairs,rebuilds);
            }
        } else {
            restore_layout();
            ResetEvent(ready.value);
            publish(status,waiting,repairs,rebuilds);
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
                DWORD command = WaitForMultipleObjects(2,commands,FALSE,self->active ? 2000 : INFINITE);
                bool tick=command==WAIT_TIMEOUT;
                if (!tick && command != WAIT_OBJECT_0 && command != WAIT_OBJECT_0+1) break;
                if (!tick) self->active = command == WAIT_OBJECT_0+1;
                CoreDispatcher queue{nullptr};
                {
                    slim_lock_guard lock(self->dispatchLock);
                    queue = self->dispatcher;
                }
                if (tick && self->queued.exchange(true)) continue;
                if (queue) queue.RunAsync(CoreDispatcherPriority::Low,[hold=self->get_strong(), command, tick] {
                    try {
                        if (command == WAIT_OBJECT_0) {
                            restore_layout();
                            hold->blocked=false;
                            hold->missing=hold->badGeometry=0;
                            publish(hold->status,waiting,hold->repairs,hold->rebuilds);
                            SetEvent(hold->restored.value);
                        } else hold->apply_layout();
                    } catch (hresult_error const& e) {
                        restore_layout();
                        hold->blocked=true;
                        ResetEvent(hold->ready.value);
                        publish(hold->status,incompatible,hold->repairs,hold->rebuilds,e.code());
                    } catch (...) {}
                    if (tick) hold->queued=false;
                });
                else {
                    if (tick) self->queued=false;
                    if (command == WAIT_OBJECT_0) SetEvent(self->restored.value);
                }
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
extern "C" __declspec(dllexport) unsigned long long WINAPI QingkuaiLayoutBuild() { return QINGKUAI_LAYOUT_BUILD; }
