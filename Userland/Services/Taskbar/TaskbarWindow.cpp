/*
 * Copyright (c) 2018-2021, Andreas Kling <kling@serenityos.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "TaskbarWindow.h"
#include "ClockWidget.h"
#include "TaskbarButton.h"
#include <AK/Debug.h>
#include <LibCore/ConfigFile.h>
#include <LibCore/StandardPaths.h>
#include <LibDesktop/AppFile.h>
#include <LibGUI/BoxLayout.h>
#include <LibGUI/Button.h>
#include <LibGUI/Desktop.h>
#include <LibGUI/Frame.h>
#include <LibGUI/Icon.h>
#include <LibGUI/Menu.h>
#include <LibGUI/Painter.h>
#include <LibGUI/Window.h>
#include <LibGUI/WindowServerConnection.h>
#include <LibGfx/FontDatabase.h>
#include <LibGfx/Palette.h>
#include <serenity.h>
#include <stdio.h>

class TaskbarWidget final : public GUI::Widget {
    C_OBJECT(TaskbarWidget);

public:
    virtual ~TaskbarWidget() override { }

private:
    TaskbarWidget() { }

    virtual void paint_event(GUI::PaintEvent& event) override
    {
        GUI::Painter painter(*this);
        painter.add_clip_rect(event.rect());
        painter.fill_rect(rect(), palette().button());
        painter.draw_line({ 0, 1 }, { width() - 1, 1 }, palette().threed_highlight());
    }

    virtual void did_layout() override
    {
        WindowList::the().for_each_window([&](auto& window) {
            if (auto* button = window.button())
                static_cast<TaskbarButton*>(button)->update_taskbar_rect();
        });
    }
};

TaskbarWindow::TaskbarWindow(NonnullRefPtr<GUI::Menu> start_menu)
    : m_start_menu(move(start_menu))
{
    set_window_type(GUI::WindowType::Taskbar);
    set_title("Taskbar");

    on_screen_rect_change(GUI::Desktop::the().rect());

    auto& main_widget = set_main_widget<TaskbarWidget>();
    main_widget.set_layout<GUI::HorizontalBoxLayout>();
    main_widget.layout()->set_margins({ 3, 3, 3, 1 });

    auto& start_button = main_widget.add<GUI::Button>("Serenity");
    start_button.set_font(Gfx::FontDatabase::default_bold_font());
    start_button.set_icon_spacing(0);
    start_button.set_fixed_size(80, 22);
    auto app_icon = GUI::Icon::default_icon("ladybug");
    start_button.set_icon(app_icon.bitmap_for_size(16));

    start_button.on_click = [&](auto) {
        m_start_menu->popup(start_button.screen_relative_rect().top_left());
    };

    create_quick_launch_bar();

    m_task_button_container = main_widget.add<GUI::Widget>();
    m_task_button_container->set_layout<GUI::HorizontalBoxLayout>();
    m_task_button_container->layout()->set_spacing(3);

    m_default_icon = Gfx::Bitmap::load_from_file("/res/icons/16x16/window.png");

    m_applet_area_container = main_widget.add<GUI::Frame>();
    m_applet_area_container->set_frame_thickness(1);
    m_applet_area_container->set_frame_shape(Gfx::FrameShape::Box);
    m_applet_area_container->set_frame_shadow(Gfx::FrameShadow::Sunken);

    main_widget.add<Taskbar::ClockWidget>();
}

TaskbarWindow::~TaskbarWindow()
{
}

void TaskbarWindow::create_quick_launch_bar()
{
    auto& quick_launch_bar = main_widget()->add<GUI::Frame>();
    quick_launch_bar.set_layout<GUI::HorizontalBoxLayout>();
    quick_launch_bar.layout()->set_spacing(0);
    quick_launch_bar.layout()->set_margins({ 3, 0, 3, 0 });
    quick_launch_bar.set_frame_thickness(0);

    int total_width = 6;
    bool first = true;

    auto config = Core::ConfigFile::get_for_app("Taskbar");
    constexpr const char* quick_launch = "QuickLaunch";

    // FIXME: Core::ConfigFile does not keep the order of the entries.
    for (auto& name : config->keys(quick_launch)) {
        auto af_name = config->read_entry(quick_launch, name);
        auto af_path = String::formatted("{}/{}", Desktop::AppFile::APP_FILES_DIRECTORY, af_name);
        auto af = Desktop::AppFile::open(af_path);
        if (!af->is_valid())
            continue;
        auto app_executable = af->executable();
        const int button_size = 24;
        auto& button = quick_launch_bar.add<GUI::Button>();
        button.set_fixed_size(button_size, button_size);
        button.set_button_style(Gfx::ButtonStyle::CoolBar);
        button.set_icon(af->icon().bitmap_for_size(16));
        button.set_tooltip(af->name());
        button.on_click = [app_executable](auto) {
            pid_t pid = fork();
            if (pid < 0) {
                perror("fork");
            } else if (pid == 0) {
                if (chdir(Core::StandardPaths::home_directory().characters()) < 0) {
                    perror("chdir");
                    exit(1);
                }
                execl(app_executable.characters(), app_executable.characters(), nullptr);
                perror("execl");
                VERIFY_NOT_REACHED();
            } else {
                if (disown(pid) < 0)
                    perror("disown");
            }
        };

        if (!first)
            total_width += quick_launch_bar.layout()->spacing();
        first = false;
        total_width += button_size;
    }

    quick_launch_bar.set_fixed_size(total_width, 24);
}

void TaskbarWindow::on_screen_rect_change(const Gfx::IntRect& rect)
{
    Gfx::IntRect new_rect { rect.x(), rect.bottom() - taskbar_height() + 1, rect.width(), taskbar_height() };
    set_rect(new_rect);
    update_applet_area();
}

void TaskbarWindow::update_applet_area()
{
    // NOTE: Widget layout is normally lazy, but here we have to force it right away so we can tell
    //       WindowServer where to place the applet area window.
    if (!main_widget())
        return;
    main_widget()->do_layout();
    Gfx::IntRect new_rect { {}, m_applet_area_size };
    new_rect.center_within(m_applet_area_container->screen_relative_rect());
    GUI::WindowServerConnection::the().send_sync<Messages::WindowServer::WM_SetAppletAreaPosition>(new_rect.location());
}

NonnullRefPtr<GUI::Button> TaskbarWindow::create_button(const WindowIdentifier& identifier)
{
    auto& button = m_task_button_container->add<TaskbarButton>(identifier);
    button.set_min_size(20, 22);
    button.set_max_size(140, 22);
    button.set_text_alignment(Gfx::TextAlignment::CenterLeft);
    button.set_icon(*m_default_icon);
    return button;
}

void TaskbarWindow::add_window_button(::Window& window, const WindowIdentifier& identifier)
{
    if (window.button())
        return;
    window.set_button(create_button(identifier));
    auto* button = window.button();
    button->on_click = [window = &window, identifier, button](auto) {
        // We need to look at the button's checked state here to figure
        // out if the application is active or not. That's because this
        // button's window may not actually be active when a modal window
        // is displayed, in which case window->is_active() would return
        // false because window is the modal window's owner (which is not
        // active)
        if (window->is_minimized() || !button->is_checked()) {
            GUI::WindowServerConnection::the().post_message(Messages::WindowServer::WM_SetActiveWindow(identifier.client_id(), identifier.window_id()));
        } else {
            GUI::WindowServerConnection::the().post_message(Messages::WindowServer::WM_SetWindowMinimized(identifier.client_id(), identifier.window_id(), true));
        }
    };
}

void TaskbarWindow::remove_window_button(::Window& window, bool was_removed)
{
    auto* button = window.button();
    if (!button)
        return;
    if (!was_removed)
        static_cast<TaskbarButton*>(button)->clear_taskbar_rect();
    window.set_button(nullptr);
    button->remove_from_parent();
}

void TaskbarWindow::update_window_button(::Window& window, bool show_as_active)
{
    auto* button = window.button();
    if (!button)
        return;
    button->set_text(window.title());
    button->set_tooltip(window.title());
    button->set_checked(show_as_active);
}

::Window* TaskbarWindow::find_window_owner(::Window& window) const
{
    if (!window.is_modal())
        return &window;

    ::Window* parent = nullptr;
    auto* current_window = &window;
    while (current_window) {
        parent = WindowList::the().find_parent(*current_window);
        if (!parent || !parent->is_modal())
            break;
        current_window = parent;
    }
    return parent;
}

void TaskbarWindow::wm_event(GUI::WMEvent& event)
{
    WindowIdentifier identifier { event.client_id(), event.window_id() };
    switch (event.type()) {
    case GUI::Event::WM_WindowRemoved: {
#if EVENT_DEBUG
        auto& removed_event = static_cast<GUI::WMWindowRemovedEvent&>(event);
        dbgln("WM_WindowRemoved: client_id={}, window_id={}",
            removed_event.client_id(),
            removed_event.window_id());
#endif
        if (auto* window = WindowList::the().window(identifier))
            remove_window_button(*window, true);
        WindowList::the().remove_window(identifier);
        update();
        break;
    }
    case GUI::Event::WM_WindowRectChanged: {
#if EVENT_DEBUG
        auto& changed_event = static_cast<GUI::WMWindowRectChangedEvent&>(event);
        dbgln("WM_WindowRectChanged: client_id={}, window_id={}, rect={}",
            changed_event.client_id(),
            changed_event.window_id(),
            changed_event.rect());
#endif
        break;
    }

    case GUI::Event::WM_WindowIconBitmapChanged: {
        auto& changed_event = static_cast<GUI::WMWindowIconBitmapChangedEvent&>(event);
        if (auto* window = WindowList::the().window(identifier)) {
            if (window->button())
                window->button()->set_icon(changed_event.bitmap());
        }
        break;
    }

    case GUI::Event::WM_WindowStateChanged: {
        auto& changed_event = static_cast<GUI::WMWindowStateChangedEvent&>(event);
#if EVENT_DEBUG
        dbgln("WM_WindowStateChanged: client_id={}, window_id={}, title={}, rect={}, is_active={}, is_minimized={}",
            changed_event.client_id(),
            changed_event.window_id(),
            changed_event.title(),
            changed_event.rect(),
            changed_event.is_active(),
            changed_event.is_minimized());
#endif
        if (changed_event.window_type() != GUI::WindowType::Normal || changed_event.is_frameless()) {
            if (auto* window = WindowList::the().window(identifier))
                remove_window_button(*window, false);
            break;
        }
        auto& window = WindowList::the().ensure_window(identifier);
        window.set_parent_identifier({ changed_event.parent_client_id(), changed_event.parent_window_id() });
        if (!window.is_modal())
            add_window_button(window, identifier);
        else
            remove_window_button(window, false);
        window.set_title(changed_event.title());
        window.set_rect(changed_event.rect());
        window.set_modal(changed_event.is_modal());
        window.set_active(changed_event.is_active());
        window.set_minimized(changed_event.is_minimized());
        window.set_progress(changed_event.progress());

        auto* window_owner = find_window_owner(window);
        if (window_owner == &window) {
            update_window_button(window, window.is_active());
        } else if (window_owner) {
            // check the window owner's button if the modal's window button
            // would have been checked
            VERIFY(window.is_modal());
            update_window_button(*window_owner, window.is_active());
        }
        break;
    }
    case GUI::Event::WM_AppletAreaSizeChanged: {
        auto& changed_event = static_cast<GUI::WMAppletAreaSizeChangedEvent&>(event);
        m_applet_area_size = changed_event.size();
        m_applet_area_container->set_fixed_size(changed_event.size().width() + 8, 22);
        update_applet_area();
        break;
    }
    default:
        break;
    }
}

void TaskbarWindow::screen_rect_change_event(GUI::ScreenRectChangeEvent& event)
{
    on_screen_rect_change(event.rect());
}
