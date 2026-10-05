#include "Window.hpp"
#include "Template/WindowTemplate.hpp"

#include <algorithm>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
constexpr int SPLITTER_THICKNESS = 6;
constexpr int MIN_VIEWPORT_SIZE = 120;
constexpr int MIN_DOCK_SIZE = 120;

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

int Docker::addPanel(const std::string& title, ContentFn content, bool dockable,
    WindowTemplateFn windowTemplate)
{
    Panel p;
    p.title   = title;
    p.content = std::move(content);
    p.windowTemplate = windowTemplate;
    p.dockable = dockable;
    panels_.push_back(std::move(p));
    return static_cast<int>(panels_.size()) - 1;
}

void Docker::buildPanel(int index, DockState where)
{
    Panel& p = panels_[index];
    GLUI* g;

    if (!p.dockable)
        where = FLOATING;

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

    const WindowTemplateFn addWindowControls = p.windowTemplate
        ? p.windowTemplate
        : DockerTemplate::addWindowControls;
    addWindowControls(g, index, where, p.dockable, Docker::panelCb);

    if (where == DOCK_LEFT || where == DOCK_RIGHT) {
        if (p.dockWidth > 0) {
            g->set_subwindow_width(p.dockWidth);
        } else {
            const int previous = glutGetWindow();
            glutSetWindow(g->get_glut_window_id());
            p.dockWidth = glutGet(GLUT_WINDOW_WIDTH);
            if (previous > 0)
                glutSetWindow(previous);
        }
    } else if (where == DOCK_BOTTOM) {
        if (p.dockHeight > 0) {
            g->set_subwindow_height(p.dockHeight);
        } else {
            const int previous = glutGetWindow();
            glutSetWindow(g->get_glut_window_id());
            p.dockHeight = glutGet(GLUT_WINDOW_HEIGHT);
            if (previous > 0)
                glutSetWindow(previous);
        }
    }

    p.glui  = g;
    p.state = where;
    if (where == DOCK_LEFT || where == DOCK_RIGHT || where == DOCK_BOTTOM)
        createSplitter(index);
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

        destroySplitter(p);
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
    updateSplitters();
    glutPostRedisplay();
}

void Docker::createSplitter(int index)
{
    Panel& panel = panels_[index];
    if (panel.splitterWindow || panel.state == FLOATING || panel.state == CLOSED)
        return;

    const int previous = glutGetWindow();
    glutSetWindow(mainWindow_);
    panel.splitterWindow = glutCreateSubWindow(mainWindow_, 0, 0,
        SPLITTER_THICKNESS, SPLITTER_THICKNESS);
    splitterPanels_[panel.splitterWindow] = index;

    glutDisplayFunc(Docker::splitterDisplayCb);
    glutMouseFunc(Docker::splitterMouseCb);
    glutMotionFunc(Docker::splitterMotionCb);
    glutSetCursor(panel.state == DOCK_BOTTOM
        ? GLUT_CURSOR_UP_DOWN : GLUT_CURSOR_LEFT_RIGHT);

    if (previous > 0)
        glutSetWindow(previous);
    updateSplitters();
}

void Docker::destroySplitter(Panel& panel)
{
    if (!panel.splitterWindow)
        return;

    const int splitterWindow = panel.splitterWindow;
    const int previous = glutGetWindow();
    const auto owner = splitterPanels_.find(splitterWindow);
    if (owner != splitterPanels_.end() && draggingPanel_ == owner->second)
        draggingPanel_ = -1;
    splitterPanels_.erase(splitterWindow);
    panel.splitterWindow = 0;

    glutSetWindow(splitterWindow);
    glutDestroyWindow(splitterWindow);
    if (previous > 0 && previous != splitterWindow)
        glutSetWindow(previous);
}

void Docker::updateSplitters()
{
    const int previous = glutGetWindow();
    for (Panel& panel : panels_) {
        if (!panel.splitterWindow || !panel.glui)
            continue;

        glutSetWindow(panel.glui->get_glut_window_id());
        const int panelX = glutGet(GLUT_WINDOW_X);
        const int panelY = glutGet(GLUT_WINDOW_Y);
        const int panelWidth = glutGet(GLUT_WINDOW_WIDTH);
        const int panelHeight = glutGet(GLUT_WINDOW_HEIGHT);

        int splitterX = panelX;
        int splitterY = panelY;
        int splitterWidth = SPLITTER_THICKNESS;
        int splitterHeight = panelHeight;
        if (panel.state == DOCK_LEFT) {
            splitterX = panelX + panelWidth - SPLITTER_THICKNESS / 2;
        } else if (panel.state == DOCK_RIGHT) {
            splitterX = panelX - SPLITTER_THICKNESS / 2;
        } else {
            splitterY = panelY - SPLITTER_THICKNESS / 2;
            splitterWidth = panelWidth;
            splitterHeight = SPLITTER_THICKNESS;
        }

        glutSetWindow(panel.splitterWindow);
        glutPositionWindow(splitterX, splitterY);
        glutReshapeWindow(splitterWidth, splitterHeight);
        glutPopWindow();
    }
    if (previous > 0)
        glutSetWindow(previous);
}

void Docker::resizeDockedPanel(int index, int pointerX, int pointerY)
{
    if (index < 0 || index >= static_cast<int>(panels_.size()))
        return;

    Panel& panel = panels_[index];
    if (!panel.glui || panel.state == FLOATING || panel.state == CLOSED)
        return;

    const int pointerCoordinate = panel.state == DOCK_BOTTOM ? pointerY : pointerX;
    const int delta = panel.state == DOCK_LEFT
        ? pointerCoordinate - dragOriginCoordinate_
        : dragOriginCoordinate_ - pointerCoordinate;
    const bool horizontalDock = panel.state == DOCK_LEFT || panel.state == DOCK_RIGHT;

    const int previous = glutGetWindow();
    glutSetWindow(mainWindow_);
    const int availableSize = glutGet(horizontalDock ? GLUT_WINDOW_WIDTH : GLUT_WINDOW_HEIGHT);
    if (previous > 0)
        glutSetWindow(previous);

    int otherPanelsSize = 0;
    for (size_t i = 0; i < panels_.size(); ++i) {
        if (static_cast<int>(i) == index)
            continue;
        const Panel& other = panels_[i];
        if (horizontalDock && (other.state == DOCK_LEFT || other.state == DOCK_RIGHT))
            otherPanelsSize += other.dockWidth;
        else if (!horizontalDock && other.state == DOCK_BOTTOM)
            otherPanelsSize += other.dockHeight;
    }

    const int maximumSize = std::max(MIN_DOCK_SIZE,
        availableSize - otherPanelsSize - MIN_VIEWPORT_SIZE);
    const int minimumSize = std::min(MIN_DOCK_SIZE, maximumSize);
    const int dockSize = std::clamp(dragStartSize_ + delta, minimumSize, maximumSize);

    if (panel.state == DOCK_BOTTOM) {
        panel.dockHeight = dockSize;
        panel.glui->set_subwindow_height(dockSize);
    } else {
        panel.dockWidth = dockSize;
        panel.glui->set_subwindow_width(dockSize);
    }

    relayout();
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
        case DockerTemplate::ACT_FLOAT:  d.requestChange(index, FLOATING);    break;
        case DockerTemplate::ACT_LEFT:   d.requestChange(index, DOCK_LEFT);   break;
        case DockerTemplate::ACT_RIGHT:  d.requestChange(index, DOCK_RIGHT);  break;
        case DockerTemplate::ACT_BOTTOM: d.requestChange(index, DOCK_BOTTOM); break;
        case DockerTemplate::ACT_CLOSE:  d.requestChange(index, CLOSED);      break;
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

void Docker::splitterDisplayCb()
{
    glClearColor(0.42f, 0.44f, 0.47f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glutSwapBuffers();
}

void Docker::splitterMouseCb(int button, int state, int x, int y)
{
    if (button != GLUT_LEFT_BUTTON)
        return;

    Docker& docker = instance();
    const auto owner = docker.splitterPanels_.find(glutGetWindow());
    if (owner == docker.splitterPanels_.end())
        return;

    if (state == GLUT_DOWN) {
        const int index = owner->second;
        const Panel& panel = docker.panels_[index];
        docker.draggingPanel_ = index;
        docker.dragStartSize_ = panel.state == DOCK_BOTTOM
            ? panel.dockHeight : panel.dockWidth;
        docker.dragOriginCoordinate_ = panel.state == DOCK_BOTTOM
            ? glutGet(GLUT_WINDOW_Y) + y
            : glutGet(GLUT_WINDOW_X) + x;
    } else if (state == GLUT_UP) {
        docker.draggingPanel_ = -1;
    }
}

void Docker::splitterMotionCb(int x, int y)
{
    Docker& docker = instance();
    if (docker.draggingPanel_ < 0)
        return;

    const int pointerX = glutGet(GLUT_WINDOW_X) + x;
    const int pointerY = glutGet(GLUT_WINDOW_Y) + y;
    docker.resizeDockedPanel(docker.draggingPanel_, pointerX, pointerY);
}
