#pragma once

// Docker: a GLUI top menu bar (Tools, Help, ...), each with a real
// click-to-open dropdown, plus content panels that can float or be docked
// to the left, right or bottom edge of the main window.

#include <GL/glut.h>
#include <GL/freeglut_ext.h>  // glutCloseFunc, GLUT_ACTION_ON_WINDOW_CLOSE
#include <GL/glui.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

class Docker {
public:
    enum DockState { FLOATING, DOCK_LEFT, DOCK_RIGHT, DOCK_BOTTOM, CLOSED };

    // Builds the controls of a panel. 'body' is the titled GLUI panel that
    // the controls should be added to.
    using ContentFn = std::function<void(GLUI* glui, GLUI_Panel* body)>;

    // Called whenever the area left over for rendering changes.
    using ViewportFn = std::function<void(int x, int y, int w, int h)>;

    static Docker& instance();

    // Call once after glutCreateWindow. Creates the top bar and hooks the
    // reshape callback through GLUI_Master. Also makes sure closing a
    // floating panel (or an open menu) with its native close button only
    // destroys that window, instead of quitting the whole app.
    void init(int mainWindow, ViewportFn onViewport);

    // Registers a content panel. Returns its index, used with openPanel.
    int  addPanel(const std::string& title, ContentFn content);

    // Opens a panel. 'where' defaults to floating; pass DOCK_LEFT/RIGHT/
    // BOTTOM to have it appear already docked.
    void openPanel(int index, DockState where = FLOATING);

    // A top-level menu (e.g. "Tools"). Returns its index, used with addMenuItem.
    int addMenu(const std::string& title);
    void addMenuItem(int menuIndex, const std::string& label, std::function<void()> action);

    const std::string& status() const { return status_; }
    void setStatus(const std::string& text);

    Docker(const Docker&) = delete;
    Docker& operator=(const Docker&) = delete;

private:
    Docker() = default;

    struct Panel {
        std::string title;
        ContentFn   content;
        GLUI*       glui       = nullptr;
        DockState   state      = CLOSED;
        DockState   pending    = CLOSED;
        bool        hasPending = false;
    };

    struct MenuItem {
        std::string label;
        std::function<void()> action;
    };

    struct Menu {
        std::string title;
        std::vector<MenuItem> items;
    };

    void buildTopBar();
    void rebuildNativeMenu();
    void buildPanel(int index, DockState where);
    void requestChange(int index, DockState where);
    void applyPending();
    void relayout();
    void handleReshape();

    // Runs 'onClose' when the GLUT window with id 'glutWinId' is closed via
    // its native close button, instead of letting GLUT's default behaviour
    // (which can otherwise exit the whole app) run unchecked.
    void hookWindowClose(int glutWinId, std::function<void()> onClose);

    // GLUI and GLUT only take plain function pointers
    static void panelCb(int id);
    static void timerCb(int);
    static void reshapeCb(int w, int h);
    static void windowCloseCb();

    std::vector<Panel> panels_;
    std::vector<Menu>  menus_;
    ViewportFn onViewport_;
    std::string status_ = "Select a panel from the Tools menu.";

    int   mainWindow_ = 0;
    std::map<int, std::function<void()>> closeHandlers_;
};
