#pragma once

#include "../Window.hpp"

namespace DockerTemplate {

enum WindowAction { ACT_FLOAT = 0, ACT_LEFT, ACT_RIGHT, ACT_BOTTOM, ACT_CLOSE };

inline void addWindowControls(GLUI* glui, int panelIndex, Docker::DockState where,
    bool dockable, GLUI_Update_CB callback)
{
    const int base = panelIndex * 10;
    GLUI_Panel* controls = glui->add_panel("Window");
    if (dockable) {
        if (where != Docker::FLOATING)
            glui->add_button_to_panel(controls, "Float", base + ACT_FLOAT, callback);
        if (where != Docker::DOCK_LEFT)
            glui->add_button_to_panel(controls, "Dock left", base + ACT_LEFT, callback);
        if (where != Docker::DOCK_RIGHT)
            glui->add_button_to_panel(controls, "Dock right", base + ACT_RIGHT, callback);
        if (where != Docker::DOCK_BOTTOM)
            glui->add_button_to_panel(controls, "Dock bottom", base + ACT_BOTTOM, callback);
    }
    glui->add_button_to_panel(controls, "Close", base + ACT_CLOSE, callback);
}

}