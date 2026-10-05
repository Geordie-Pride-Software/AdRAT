#include "Window.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
// Button actions, encoded into panel callback ids as (panelIndex * 10 + action)
enum { ACT_FLOAT = 0, ACT_LEFT, ACT_RIGHT, ACT_BOTTOM, ACT_CLOSE };

#ifdef _WIN32
constexpr UINT MENU_COMMAND_BASE = 0x4000;
HWND nativeWindow = nullptr;
WNDPROC previousWindowProc = nullptr;
HMENU nativeMenu = nullptr;
HBRUSH nativeMenuBackground = nullptr;
std::map<UINT, std::function<void()>> nativeActions;
std::map<ULONG_PTR, std::string> nativeMenuLabels;

LRESULT CALLBACK dockerWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_MEASUREITEM) {
        auto* item = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
        if (item && item->CtlType == ODT_MENU) {
            auto label = nativeMenuLabels.find(item->itemData);
            if (label != nativeMenuLabels.end()) {
                HDC dc = GetDC(window);
                SIZE textSize{};
                GetTextExtentPoint32A(dc, label->second.c_str(),
                    static_cast<int>(label->second.size()), &textSize);
                ReleaseDC(window, dc);
                item->itemWidth = static_cast<UINT>(textSize.cx + 24);
                item->itemHeight = 30;
                return TRUE;
            }
        }
    }

    if (message == WM_DRAWITEM) {
        auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (item && item->CtlType == ODT_MENU) {
            auto label = nativeMenuLabels.find(item->itemData);
            if (label != nativeMenuLabels.end()) {
                const bool selected = (item->itemState & ODS_SELECTED) != 0;
                const bool topLevel = item->itemData < MENU_COMMAND_BASE;
                COLORREF fill = RGB(200, 200, 200);
                if (selected)
                    fill = topLevel ? RGB(170, 170, 170) : RGB(150, 150, 150);
                HBRUSH background = CreateSolidBrush(fill);
                FillRect(item->hDC, &item->rcItem, background);
                DeleteObject(background);

                SetBkMode(item->hDC, TRANSPARENT);
                SetTextColor(item->hDC, RGB(0, 0, 0));
                RECT textRect = item->rcItem;
                if (topLevel) {
                    DrawTextA(item->hDC, label->second.c_str(), -1, &textRect,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                } else {
                    textRect.left += 12;
                    DrawTextA(item->hDC, label->second.c_str(), -1, &textRect,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                }
                return TRUE;
            }
        }
    }

    if (message == WM_COMMAND && HIWORD(wParam) == 0) {
        auto action = nativeActions.find(LOWORD(wParam));
        if (action != nativeActions.end()) {
            action->second();
            return 0;
        }
    }

    return CallWindowProc(previousWindowProc, window, message, wParam, lParam);
}
#endif
}

Docker& Docker::instance()
{
    static Docker d;
    return d;
}

// ------------------------------------------------------------------ setup

void Docker::init(int mainWindow, ViewportFn onViewport)
{
    mainWindow_ = mainWindow;
    onViewport_ = std::move(onViewport);

    // Without this, clicking the native close button on a floating panel
    // (or an open dropdown) ends the whole app instead of just destroying
    // that one window.
    glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_CONTINUE_EXECUTION);

    GLUI_Master.set_glutReshapeFunc(Docker::reshapeCb);
    buildTopBar();
    relayout();
}

void Docker::buildTopBar()
{
#ifdef _WIN32
    glutSetWindow(mainWindow_);
    HDC deviceContext = wglGetCurrentDC();
    nativeWindow = deviceContext ? WindowFromDC(deviceContext) : GetActiveWindow();
    if (nativeWindow) {
        previousWindowProc = reinterpret_cast<WNDPROC>(SetWindowLongPtr(
            nativeWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(dockerWindowProc)));
    }
#endif
}

// --------------------------------------------------------------- menu bar

int Docker::addMenu(const std::string& title)
{
    Menu m;
    m.title = title;
    menus_.push_back(std::move(m));
    const int idx = static_cast<int>(menus_.size()) - 1;
    rebuildNativeMenu();
    return idx;
}

void Docker::addMenuItem(int menuIndex, const std::string& label, std::function<void()> action)
{
    if (menuIndex < 0 || menuIndex >= static_cast<int>(menus_.size())) return;
    menus_[menuIndex].items.push_back({label, std::move(action)});
    rebuildNativeMenu();
}

void Docker::rebuildNativeMenu()
{
#ifdef _WIN32
    if (!nativeWindow) return;

    HMENU menuBar = CreateMenu();
    if (!menuBar) return;

    if (!nativeMenuBackground)
        nativeMenuBackground = CreateSolidBrush(RGB(200, 200, 200));

    MENUINFO backgroundInfo{};
    backgroundInfo.cbSize = sizeof(backgroundInfo);
    backgroundInfo.fMask = MIM_BACKGROUND;
    backgroundInfo.hbrBack = nativeMenuBackground;
    if (nativeMenuBackground)
        SetMenuInfo(menuBar, &backgroundInfo);

    nativeActions.clear();
    nativeMenuLabels.clear();
    for (size_t menuIndex = 0; menuIndex < menus_.size(); ++menuIndex) {
        HMENU popup = CreatePopupMenu();
        if (!popup) continue;
        if (nativeMenuBackground)
            SetMenuInfo(popup, &backgroundInfo);

        const Menu& menu = menus_[menuIndex];
        for (size_t itemIndex = 0; itemIndex < menu.items.size(); ++itemIndex) {
            const UINT command = MENU_COMMAND_BASE + static_cast<UINT>(menuIndex * 100 + itemIndex);
            nativeMenuLabels[command] = menu.items[itemIndex].label;
            AppendMenuA(popup, MF_OWNERDRAW, command,
                reinterpret_cast<LPCSTR>(static_cast<ULONG_PTR>(command)));
            nativeActions[command] = menu.items[itemIndex].action;
        }
        const ULONG_PTR labelId = static_cast<ULONG_PTR>(menuIndex + 1);
        nativeMenuLabels[labelId] = menu.title;
        AppendMenuA(menuBar, MF_POPUP | MF_OWNERDRAW,
            reinterpret_cast<UINT_PTR>(popup), reinterpret_cast<LPCSTR>(labelId));
    }

    HMENU previousMenu = nativeMenu;
    if (SetMenu(nativeWindow, menuBar)) {
        nativeMenu = menuBar;
        DrawMenuBar(nativeWindow);
        if (previousMenu) DestroyMenu(previousMenu);
    } else {
        DestroyMenu(menuBar);
    }
#endif
}

// ------------------------------------------------------------- panel logic

int Docker::addPanel(const std::string& title, ContentFn content)
{
    Panel p;
    p.title   = title;
    p.content = std::move(content);
    panels_.push_back(std::move(p));
    return static_cast<int>(panels_.size()) - 1;
}

void Docker::buildPanel(int index, DockState where)
{
    Panel& p = panels_[index];
    GLUI* g;

    if (where == FLOATING) {
        g = GLUI_Master.create_glui(p.title.c_str(), 0, 120 + index * 40, 120 + index * 40);
        hookWindowClose(g->get_glut_window_id(), [this, index]() {
            panels_[index].glui       = nullptr;
            panels_[index].state      = CLOSED;
            panels_[index].hasPending = false;
            setStatus(panels_[index].title + " closed.");
        });
    } else {
        long side = GLUI_SUBWINDOW_RIGHT;
        if (where == DOCK_LEFT)   side = GLUI_SUBWINDOW_LEFT;
        if (where == DOCK_BOTTOM) side = GLUI_SUBWINDOW_BOTTOM;
        g = GLUI_Master.create_glui_subwindow(mainWindow_, side);
    }

    g->set_main_gfx_window(mainWindow_);

    GLUI_Panel* body = g->add_panel(p.title.c_str());
    if (p.content)
        p.content(g, body);

    // Docking controls, leaving out the state the panel is already in.
    // This is the only "close" for a docked panel, since a docked subwindow
    // has no native close button of its own to worry about.
    const int base = index * 10;
    GLUI_Panel* win = g->add_panel("Window");
    if (where != FLOATING)    g->add_button_to_panel(win, "Float",       base + ACT_FLOAT,  Docker::panelCb);
    if (where != DOCK_LEFT)   g->add_button_to_panel(win, "Dock left",   base + ACT_LEFT,   Docker::panelCb);
    if (where != DOCK_RIGHT)  g->add_button_to_panel(win, "Dock right",  base + ACT_RIGHT,  Docker::panelCb);
    if (where != DOCK_BOTTOM) g->add_button_to_panel(win, "Dock bottom", base + ACT_BOTTOM, Docker::panelCb);
    g->add_button_to_panel(win, "Close", base + ACT_CLOSE, Docker::panelCb);

    p.glui  = g;
    p.state = where;
}

// Changes are applied from a GLUT timer so that a panel is never destroyed
// from inside one of its own button callbacks.
void Docker::requestChange(int index, DockState where)
{
    Panel& p = panels_[index];
    p.pending    = where;
    p.hasPending = true;
    glutTimerFunc(0, Docker::timerCb, 0);
}

void Docker::applyPending()
{
    for (size_t i = 0; i < panels_.size(); ++i) {
        Panel& p = panels_[i];
        if (!p.hasPending) continue;
        p.hasPending = false;

        if (p.glui) {
            closeHandlers_.erase(p.glui->get_glut_window_id());
            p.glui->close();
            p.glui = nullptr;
        }
        p.state = CLOSED;

        if (p.pending != CLOSED)
            buildPanel(static_cast<int>(i), p.pending);
    }
    relayout();
}

void Docker::openPanel(int index, DockState where)
{
    if (index < 0 || index >= static_cast<int>(panels_.size())) return;

    Panel& p = panels_[index];
    if (p.state != CLOSED || p.hasPending) {
        setStatus(p.title + " is already open.");
        return;
    }
    setStatus("Opened " + p.title + ".");
    requestChange(index, where);
}

void Docker::setStatus(const std::string& text)
{
    status_ = text;
    if (mainWindow_) {
        glutSetWindow(mainWindow_);
        glutPostRedisplay();
    }
}

// ------------------------------------------------------------- window close

void Docker::hookWindowClose(int glutWinId, std::function<void()> onClose)
{
    closeHandlers_[glutWinId] = std::move(onClose);
    const int prev = glutGetWindow();
    glutSetWindow(glutWinId);
    glutCloseFunc(Docker::windowCloseCb);
    if (prev) glutSetWindow(prev);
}

void Docker::windowCloseCb()
{
    Docker& d = instance();
    const int wid = glutGetWindow();
    auto it = d.closeHandlers_.find(wid);
    if (it != d.closeHandlers_.end()) {
        auto fn = it->second;
        d.closeHandlers_.erase(it);
        if (fn) fn();
    }
}

// ----------------------------------------------------------------- layout

void Docker::handleReshape()
{
    int x, y, w, h;
    GLUI_Master.get_viewport_area(&x, &y, &w, &h);
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    if (onViewport_)
        onViewport_(x, y, w, h);
    glutPostRedisplay();
}

// Re-run layout after a subwindow was added or removed
void Docker::relayout()
{
    glutSetWindow(mainWindow_);
    GLUI_Master.reshape();
    handleReshape();
}

// -------------------------------------------------------------- callbacks

void Docker::panelCb(int id)
{
    Docker& d  = instance();
    const int index  = id / 10;
    const int action = id % 10;

    switch (action) {
        case ACT_FLOAT:  d.requestChange(index, FLOATING);    break;
        case ACT_LEFT:   d.requestChange(index, DOCK_LEFT);   break;
        case ACT_RIGHT:  d.requestChange(index, DOCK_RIGHT);  break;
        case ACT_BOTTOM: d.requestChange(index, DOCK_BOTTOM); break;
        case ACT_CLOSE:  d.requestChange(index, CLOSED);      break;
    }
}

void Docker::timerCb(int)
{
    instance().applyPending();
}

void Docker::reshapeCb(int, int)
{
    instance().handleReshape();
}
