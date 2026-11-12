// Isolated XAML islands. Does not connect to or change Explorer.
#include "../taskbar_layout.cpp"
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
#include <winrt/Windows.UI.Xaml.Automation.Peers.h>
#include <winrt/Windows.UI.Xaml.Automation.Provider.h>
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

int main(int argc, char** argv) {
    try {
        bool preview=argc>1 && std::string(argv[1])=="--preview";
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
        Button start, search;
        Border original, trayContent;
        host.Width(1920);
        host.Height(48);
        frame.Width(1920);
        panel.Name(L"RootGrid");
        repeater.Name(L"TaskbarFrameRepeater");
        repeater.Orientation(Orientation::Horizontal);
        start.Width(44);
        repeater.Children().Append(start);
        search.Width(160);
        Automation::AutomationProperties::SetAutomationId(search,L"SearchButton");
        repeater.Children().Append(search);
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
            std::vector<int> tools;
            Layout layout(&status);
            if (!preview) layout.launchTool=[&tools](int tool) { tools.push_back(tool); };
            layout.host=host;
            layout.frame=frame;
            layout.panel=panel;
            layout.tray=tray;
            layout.repeater=repeater;
            layout.start=start;
            require(layout.decorate(original),"could not decorate fixture");
            pump(250);
            if (preview) {
                ShowWindow(window,SW_SHOWNOACTIVATE);
                auto deadline=GetTickCount64()+120000;
                while (IsWindow(window) && GetTickCount64()<deadline) pump(50);
                return 0;
            }
            require(layout.connected(),"fixture disconnected");
            require(status.state==healthy && frame.ActualWidth()<1000,"initial layout not compact");
            auto compact=frame.ActualWidth();
            auto firstApp=repeater.Children().GetAt(2).as<FrameworkElement>();
            auto searchRight=[&] { return search.TransformToVisual(panel).TransformPoint({0,0}).X+search.ActualWidth(); };
            auto rightGap=[&] { return firstApp.TransformToVisual(panel).TransformPoint({0,0}).X-searchRight(); };
            require(start.ActualWidth()==commandSlotWidth && std::abs(rightGap())<.1,
                    "blank gutters remain around native search");
            require(layout.commands.Margin().Left==commandInset && layout.commands.Children().Size()==3,
                    "compact command strip is missing");
            for (auto button : {layout.power,layout.settingsButton,layout.taskManagerButton})
                require(button.ActualWidth()==commandSize && button.ActualHeight()==32,"command button was stretched");
            auto invoke=[&](Button const& button) {
                Automation::Peers::ButtonAutomationPeer peer(button);
                peer.GetPattern(Automation::Peers::PatternInterface::Invoke).as<Automation::Provider::IInvokeProvider>().Invoke();
                pump(80);
            };
            invoke(layout.settingsButton);
            invoke(layout.taskManagerButton);
            require(tools==std::vector<int>{0,1},"native button invocation did not dispatch the correct tools");
            require(firstApp.Margin()==Thickness{0,0,0,0},"application spacing was changed");
            std::cout << "PASS compact command buttons invoke taskbar settings and Task Manager; no gutters remain\n";

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
            require(std::abs(rightGap())<.1 && start.ActualWidth()==commandSlotWidth,
                    "new applications changed the compact command strip");
            for (int i=0; i<6; ++i) repeater.Children().RemoveAtEnd();
            pump(150);
            require(frame.ActualWidth()==compact,"removed icons left empty width");
            require(std::abs(rightGap())<.1,"removed icons changed native search spacing");
            search.Visibility(Visibility::Collapsed);
            pump(100);
            require(start.ActualWidth()==commandSlotWidth && layout.commands.ActualWidth()==commandContentWidth,
                    "hiding native search stretched command buttons");
            search.Visibility(Visibility::Visible);
            pump(100);
            require(frame.ActualWidth()==compact && std::abs(rightGap())<.1,
                    "restoring native search displaced the compact commands");
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
        require(start.Width()==44 && search.ReadLocalValue(UIElement::RenderTransformProperty())==DependencyProperty::UnsetValue(),
                "native control size or transform was not restored");
        std::cout << "PASS disabling restores latest native values and removes subscriptions\n";

        for (int cycle=0; cycle<3; ++cycle) {
            start.Width(176);
            pump(40);
            start.Width(44); // ActualWidth still belongs to the previous layout.
            {
                Layout next;
                next.host=host; next.frame=frame; next.panel=panel;
                next.tray=tray; next.repeater=repeater; next.start=start;
                require(next.decorate(original),"could not re-enable compact commands");
                pump(100);
                require(start.ActualWidth()==commandSlotWidth && next.power.ActualWidth()==commandSize &&
                        next.commands.ActualWidth()==commandContentWidth,"stale native width stretched the commands");
            }
            require(start.Width()==44,"disabling lost the native Start width");
        }
        std::cout << "PASS repeated enable/restore ignores stale ActualWidth and never grows the power button\n";

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
        {
            Layout fractional;
            fractional.host=Grid();
            Button button;
            fractional.set(button,FrameworkElement::WidthProperty(),88.+2./3.);
            button.Width(88.66667175292969); // XAML's subpixel float coercion.
            pump(120);
            require(!fractional.scheduled && fractional.repairCount==0,"subpixel coercion caused repeated repairs");
        }
        std::cout << "PASS subpixel width coercion does not schedule redundant layout repairs\n";
        {
            Status appearance{};
            Grid colors, mainFrame, nativeTray;
            mainFrame.RequestedTheme(ElementTheme::Light);
            nativeTray.RequestedTheme(ElementTheme::Light);
            {
            Layout styled(&appearance);
            styled.host=colors; styled.frame=mainFrame; styled.tray=nativeTray;
            styled.controls=Border(); styled.apps=Border(); styled.trayBack=Border();
            for (auto border : {styled.controls,styled.apps,styled.trayBack})
                border.Background(Media::AcrylicBrush());
            appearance.appearanceFlags=1;
            appearance.appearanceColor=0x162738;
            appearance.appearanceOpacity=73;
            appearance.appearanceRevision=1;
            styled.apply_appearance();
            auto leases=styled.properties.size();
            require(mainFrame.RequestedTheme()==ElementTheme::Dark,"dark tint left native text dark");
            for (auto border : {styled.controls,styled.apps,styled.trayBack}) {
                auto brush=border.Background().as<Media::AcrylicBrush>();
                require(brush.TintColor().R==0x16 && std::abs(brush.TintOpacity()-.73)<.0001,
                        "background appearance did not reach all three sections");
                require(std::abs(border.Opacity()-.73)<.0001,"fallback background ignored opacity");
            }
            for (int i=0; i<100; ++i) {
                appearance.appearanceColor=i%2 ? 0xffffff : 0x101010;
                appearance.appearanceOpacity=i%2 ? 100 : 0;
                ++appearance.appearanceRevision;
                styled.apply_appearance();
                require(styled.controls.Opacity()==(i%2 ? 1. : 0.),"zero opacity did not remove only the background");
                require(mainFrame.Opacity()==1 && nativeTray.Opacity()==1,"native content was faded");
            }
            require(styled.properties.size()==leases,"appearance changes accumulated property leases");
            require(mainFrame.RequestedTheme()==ElementTheme::Light,"light tint lost readable native text");
            auto revision=styled.appearanceRevision;
            styled.apply_appearance();
            require(styled.appearanceRevision==revision && !styled.scheduled,"idle appearance scheduled repairs");
            }
            require(mainFrame.RequestedTheme()==ElementTheme::Light && nativeTray.RequestedTheme()==ElementTheme::Light,
                    "original native theme was not restored");
        }
        std::cout << "PASS three-section appearance, contrast, bounded leases and original theme restoration\n";
        island.Close();
        DestroyWindow(window);
        manager.Close();
        return 0;
    } catch (hresult_error const& e) {
        std::cerr << "HRESULT " << std::hex << uint32_t(e.code()) << " " << to_string(e.message()) << '\n';
    } catch (std::exception const& e) { std::cerr << e.what() << '\n'; }
    return 1;
}
