// Isolated XAML islands. Does not connect to or change Explorer.
#include "../taskbar_layout.cpp"
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
#include <iostream>
#include <stdexcept>

void require(bool value, char const* message) {
    if (!value) throw std::runtime_error(message);
}

void pump(unsigned milliseconds) {
    auto end=GetTickCount64()+milliseconds;
    do {
        MSG message;
        while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(5);
    } while (GetTickCount64()<end);
}

int main() {
    try {
        init_apartment(apartment_type::single_threaded);
        auto manager=Hosting::WindowsXamlManager::InitializeForCurrentThread();
        auto window=CreateWindowExW(0,L"Static",L"Qingkuai isolated layout test",WS_OVERLAPPEDWINDOW,
                                    0,0,1920,100,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check_bool(window!=nullptr);
        Hosting::DesktopWindowXamlSource island;
        auto native=island.as<IDesktopWindowXamlSourceNative>();
        check_hresult(native->AttachToWindow(window));
        HWND child{};
        check_hresult(native->get_WindowHandle(&child));
        SetWindowPos(child,nullptr,0,0,1920,48,SWP_NOZORDER|SWP_SHOWWINDOW);
        Grid host, frame, panel, tray;
        StackPanel repeater;
        Button start;
        Border original, trayContent;
        host.Width(1920);
        host.Height(48);
        frame.Width(1920);
        panel.Name(L"RootGrid");
        repeater.Name(L"TaskbarFrameRepeater");
        repeater.Orientation(Orientation::Horizontal);
        start.Width(44);
        repeater.Children().Append(start);
        for (int i=0; i<10; ++i) {
            Button icon;
            icon.Width(44);
            repeater.Children().Append(icon);
        }
        trayContent.Width(320);
        tray.Children().Append(trayContent);
        panel.Children().Append(original);
        panel.Children().Append(repeater);
        frame.Children().Append(panel);
        host.Children().Append(frame);
        host.Children().Append(tray);
        island.Content(host);
        pump(100);
        Status status{};
        LONG repaired{};
        {
            Layout layout(&status);
            layout.host=host;
            layout.frame=frame;
            layout.panel=panel;
            layout.tray=tray;
            layout.repeater=repeater;
            layout.start=start;
            require(layout.decorate(original),"could not decorate fixture");
            pump(250);
            require(layout.connected(),"fixture disconnected");
            require(status.state==healthy && frame.ActualWidth()<1000,"initial layout not compact");
            auto compact=frame.ActualWidth();

            // Same failure mode as Explorer updating taskbar metrics after startup.
            frame.Width(1920);
            repeater.Width(1920);
            Grid::SetColumn(tray,0);
            tray.HorizontalAlignment(HorizontalAlignment::Stretch);
            pump(250);
            require(!layout.exhausted && layout.repairCount==1,"overrides were not coalesced");
            require(std::isnan(frame.Width()) && std::isnan(repeater.Width()),"native width rewrite was not repaired");
            require(Grid::GetColumn(tray)==2 && frame.ActualWidth()==compact,"tray or frame stayed displaced");
            require(status.state==healthy,"repair did not settle");
            std::cout << "PASS delayed width/alignment changes recover in one repair\n";

            host.ColumnDefinitions().Clear();
            ColumnDefinition foreign;
            foreign.Width({1,GridUnitType::Star});
            host.ColumnDefinitions().Append(foreign);
            pump(250);
            require(layout.columns_match() && status.state==healthy,"column replacement was not repaired");
            std::cout << "PASS replacing grid columns recovers the centered sections\n";

            for (int i=0; i<6; ++i) {
                Button icon;
                icon.Width(44);
                repeater.Children().Append(icon);
            }
            pump(200);
            require(frame.ActualWidth()>compact && status.groupRight<1920,"added icons overflowed");
            require(layout.repairCount==2,"ordinary icon changes caused spurious repairs");
            for (int i=0; i<6; ++i) repeater.Children().RemoveAtEnd();
            pump(150);
            require(frame.ActualWidth()==compact,"removed icons left empty width");
            auto quiet=layout.repairCount;
            for (int i=0; i<10; ++i) { layout.repair(); pump(10); }
            require(layout.repairCount==quiet && !layout.scheduled,"idle layout kept repainting");
            std::cout << "PASS icon changes resize naturally and idle checks do not repaint\n";

            for (auto width : {1024.,1280.,1706.67,2560.,1920.}) {
                host.Width(width);
                pump(100);
                require(status.state==healthy && std::abs(status.groupLeft+status.groupRight-width)<=8,
                        "resolution change displaced the group");
            }
            std::cout << "PASS five logical screen widths remain centered\n";

            repeater.Children().RemoveAt(0);
            require(!layout.connected(),"replaced native Start button was not detected");
            repeater.Children().InsertAt(0,start);
            require(layout.connected(),"restored fixture stayed disconnected");
            pump(100);
            std::cout << "PASS native child replacement is detected\n";

            // A competing writer must trigger a bounded fallback, not a loop.
            layout.burstStart=GetTickCount64();
            layout.burst=0;
            for (int i=0; i<24 && !layout.exhausted; ++i) {
                frame.Width(1600+i);
                pump(150);
            }
            require(layout.exhausted && status.state==conflict,"competing writer was not bounded");
            repaired=layout.repairCount;
            std::cout << "PASS repeated external changes stop after a bounded number of repairs\n";
        }
        pump(100);
        require(frame.Width()>=1600 && original.Visibility()==Visibility::Visible,"native latest values were not restored");
        require(host.ColumnDefinitions().Size()==1,"latest external columns were not restored");
        std::cout << "PASS disabling restores latest native values and removes subscriptions\n";

        {
            Layout bindingTest;
            bindingTest.host=Grid();
            bindingTest.set(frame,FrameworkElement::WidthProperty(),std::numeric_limits<double>::quiet_NaN());
            Data::Binding binding;
            binding.Source(box_value(1280.));
            frame.SetBinding(FrameworkElement::WidthProperty(),binding);
            pump(150);
            require(std::isnan(frame.Width()),"external binding was not repaired");
        }
        require(bool(frame.GetBindingExpression(FrameworkElement::WidthProperty())) && frame.Width()==1280,
                "native binding was lost on restore");
        std::cout << "PASS external native bindings survive style removal\n";
        island.Close();
        DestroyWindow(window);
        manager.Close();
        return 0;
    } catch (hresult_error const& e) {
        std::cerr << "HRESULT " << std::hex << uint32_t(e.code()) << " " << to_string(e.message()) << '\n';
    } catch (std::exception const& e) { std::cerr << e.what() << '\n'; }
    return 1;
}
