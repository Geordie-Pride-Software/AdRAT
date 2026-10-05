#include "Aquila-Panel.hpp"
#include "Netscan.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <fstream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace {

GLUI_Listbox* networkTree = nullptr;
class NetworkDiagram;
NetworkDiagram* networkDiagram = nullptr;
int selection = 0;
int displayedRows = 0;

class NetworkDiagram : public GLUI_Control {
public:
    explicit NetworkDiagram(GLUI_Node* parent)
    {
        name = "Network diagram";
        w = 620;
        h = 250;
        active_type = GLUI_CONTROL_ACTIVE_MOUSEDOWN;
        parent->add_control(this);
    }

    void load(const fs::path& path)
    {
        nodes.clear();
        edges.clear();
        error.clear();

        std::ifstream input(path);
        if (!input) {
            error = "No Mermaid scan output found. Refresh network to scan.";
            redraw_window();
            return;
        }

        std::unordered_map<std::string, size_t> nodeIndexes;
        std::vector<std::pair<std::string, std::string>> edgeIds;
        std::string line;
        while (std::getline(input, line)) {
            const size_t arrow = line.find("-->");
            if (arrow != std::string::npos) {
                edgeIds.emplace_back(trim(line.substr(0, arrow)), trim(line.substr(arrow + 3)));
                continue;
            }

            const size_t labelStart = line.find("[\"");
            const size_t labelEnd = line.rfind("\"]");
            if (labelStart == std::string::npos || labelEnd == std::string::npos || labelEnd <= labelStart + 2)
                continue;

            const std::string id = trim(line.substr(0, labelStart));
            DiagramNode node;
            node.id = id;
            node.label = decodeMermaidLabel(line.substr(labelStart + 2, labelEnd - labelStart - 2));
            node.router = node.label.rfind("Router ", 0) == 0;
            nodeIndexes[id] = nodes.size();
            nodes.push_back(std::move(node));
        }

        for (const auto& edge : edgeIds) {
            const auto from = nodeIndexes.find(edge.first);
            const auto to = nodeIndexes.find(edge.second);
            if (from != nodeIndexes.end() && to != nodeIndexes.end())
                edges.emplace_back(from->second, to->second);
        }

        if (nodes.empty())
            error = "The Mermaid file contains no network nodes.";
        else
            layoutNodes();
        redraw_window();
    }

    void zoomBy(float factor)
    {
        zoom = std::clamp(zoom * factor, 0.25f, 3.0f);
        redraw_window();
    }

    void resetView()
    {
        zoom = 1.0f;
        panX = 0.0f;
        panY = 0.0f;
        redraw_window();
    }

    void draw(int, int) override
    {
        GLint viewport[4]{};
        glGetIntegerv(GL_VIEWPORT, viewport);
        glPushAttrib(GL_ENABLE_BIT | GL_SCISSOR_BIT | GL_CURRENT_BIT | GL_LINE_BIT);
        glEnable(GL_SCISSOR_TEST);
        glScissor(x_abs, viewport[3] - y_abs - h, w, h);

        glColor3f(0.10f, 0.12f, 0.14f);
        glBegin(GL_QUADS);
        glVertex2i(0, 0);
        glVertex2i(w, 0);
        glVertex2i(w, h);
        glVertex2i(0, h);
        glEnd();

        if (!error.empty()) {
            drawLabel(error, 10, 20);
            glPopAttrib();
            return;
        }

        glPushMatrix();
        glTranslatef(w * 0.5f + panX, h * 0.5f + panY, 0.0f);
        glScalef(zoom, zoom, 1.0f);
        glTranslatef(-contentWidth * 0.5f, -contentHeight * 0.5f, 0.0f);

        glLineWidth(2.0f);
        glColor3f(0.52f, 0.62f, 0.68f);
        glBegin(GL_LINES);
        for (const auto& edge : edges) {
            const DiagramNode& from = nodes[edge.first];
            const DiagramNode& to = nodes[edge.second];
            glVertex2f(from.x + NODE_WIDTH, from.y + NODE_HEIGHT * 0.5f);
            glVertex2f(to.x, to.y + NODE_HEIGHT * 0.5f);
        }
        glEnd();

        for (const DiagramNode& node : nodes)
            drawNode(node);

        glPopMatrix();
        glPopAttrib();
    }

    int mouse_down_handler(int x, int y) override
    {
        dragging = true;
        lastMouseX = x;
        lastMouseY = y;
        glutSetCursor(GLUT_CURSOR_INFO);
        return true;
    }

    int mouse_held_down_handler(int x, int y, bool) override
    {
        if (dragging) {
            panX += static_cast<float>(x - lastMouseX);
            panY += static_cast<float>(y - lastMouseY);
            lastMouseX = x;
            lastMouseY = y;
            redraw_window();
        }
        return true;
    }

    int mouse_up_handler(int, int, bool) override
    {
        dragging = false;
        glutSetCursor(GLUT_CURSOR_LEFT_ARROW);
        return true;
    }

private:
    struct DiagramNode {
        std::string id;
        std::string label;
        float x = 0.0f;
        float y = 0.0f;
        bool router = false;
    };

    static constexpr float NODE_WIDTH = 210.0f;
    static constexpr float NODE_HEIGHT = 58.0f;

    static std::string trim(const std::string& value)
    {
        const size_t first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return {};
        const size_t last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    }

    static std::string decodeMermaidLabel(std::string value)
    {
        const std::pair<const char*, const char*> entities[] = {
            {"&quot;", "\""}, {"&lt;", "<"}, {"&gt;", ">"}, {"&amp;", "&"}
        };
        for (const auto& entity : entities) {
            size_t position = 0;
            while ((position = value.find(entity.first, position)) != std::string::npos) {
                value.replace(position, std::char_traits<char>::length(entity.first), entity.second);
                position += std::char_traits<char>::length(entity.second);
            }
        }
        return value;
    }

    void layoutNodes()
    {
        std::vector<int> levels(nodes.size(), 0);
        for (size_t pass = 0; pass < nodes.size(); ++pass) {
            for (const auto& edge : edges)
                levels[edge.second] = std::max(levels[edge.second], levels[edge.first] + 1);
        }

        std::map<int, std::vector<size_t>> columns;
        for (size_t i = 0; i < nodes.size(); ++i)
            columns[levels[i]].push_back(i);

        const float columnGap = 76.0f;
        const float rowGap = 28.0f;
        size_t maxRows = 1;
        for (const auto& column : columns)
            maxRows = std::max(maxRows, column.second.size());

        contentWidth = columns.empty() ? 0.0f
            : columns.rbegin()->first * (NODE_WIDTH + columnGap) + NODE_WIDTH;
        contentHeight = maxRows * (NODE_HEIGHT + rowGap) - rowGap;
        for (const auto& column : columns) {
            const float columnHeight = column.second.size() * (NODE_HEIGHT + rowGap) - rowGap;
            for (size_t row = 0; row < column.second.size(); ++row) {
                DiagramNode& node = nodes[column.second[row]];
                node.x = column.first * (NODE_WIDTH + columnGap);
                node.y = (contentHeight - columnHeight) * 0.5f + row * (NODE_HEIGHT + rowGap);
            }
        }
    }

    void drawLabel(const std::string& text, int x, int y)
    {
        glColor3f(0.86f, 0.88f, 0.9f);
        glRasterPos2i(x, y);
        for (unsigned char ch : text)
            glutBitmapCharacter(GLUT_BITMAP_8_BY_13, ch);
    }

    void drawNode(const DiagramNode& node)
    {
        glColor3f(node.router ? 0.15f : 0.18f, node.router ? 0.35f : 0.28f,
            node.router ? 0.39f : 0.32f);
        glBegin(GL_QUADS);
        glVertex2f(node.x, node.y);
        glVertex2f(node.x + NODE_WIDTH, node.y);
        glVertex2f(node.x + NODE_WIDTH, node.y + NODE_HEIGHT);
        glVertex2f(node.x, node.y + NODE_HEIGHT);
        glEnd();

        glColor3f(0.54f, 0.67f, 0.72f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(node.x, node.y);
        glVertex2f(node.x + NODE_WIDTH, node.y);
        glVertex2f(node.x + NODE_WIDTH, node.y + NODE_HEIGHT);
        glVertex2f(node.x, node.y + NODE_HEIGHT);
        glEnd();

        const size_t separator = node.label.find(" | ");
        const std::string title = node.label.substr(0, separator);
        const std::string detail = separator == std::string::npos
            ? std::string() : node.label.substr(separator + 3);
        drawLabel(title.substr(0, 25), static_cast<int>(node.x + 8), static_cast<int>(node.y + 22));
        drawLabel(detail.substr(0, 27), static_cast<int>(node.x + 8), static_cast<int>(node.y + 43));
    }

    std::vector<DiagramNode> nodes;
    std::vector<std::pair<size_t, size_t>> edges;
    std::string error = "No Mermaid scan output found. Refresh network to scan.";
    float contentWidth = 0.0f;
    float contentHeight = 0.0f;
    float zoom = 1.0f;
    float panX = 0.0f;
    float panY = 0.0f;
    int lastMouseX = 0;
    int lastMouseY = 0;
    bool dragging = false;
};

void onDiagramAction(int id)
{
    if (!networkDiagram)
        return;
    if (id == 1)
        networkDiagram->zoomBy(1.2f);
    else if (id == 2)
        networkDiagram->zoomBy(1.0f / 1.2f);
    else
        networkDiagram->resetView();
}

fs::path findNetworkRoot()
{
    std::vector<fs::path> candidates = {
        fs::current_path() / "adrat_network",
        fs::current_path() / "Tempestus Aquila" / "adrat_network",
        fs::current_path().parent_path() / "Tempestus Aquila" / "adrat_network",
        fs::current_path().parent_path() / "adrat_network"
    };

    for (const auto& candidate : candidates) {
        if (fs::exists(candidate) && fs::is_directory(candidate))
            return candidate;
    }

    return fs::current_path() / "adrat_network";
}

std::vector<std::string> loadNetworkTreeLines(const fs::path& root)
{
    std::vector<std::string> lines;
    if (!fs::exists(root) || !fs::is_directory(root)) {
        lines.push_back("No scan output found at adrat_network");
        return lines;
    }

    std::function<void(const fs::path&, const std::string&)> walk;
    walk = [&](const fs::path& directory, const std::string& prefix) {
        std::vector<fs::directory_entry> entries;
        for (const auto& entry : fs::directory_iterator(directory))
            entries.push_back(entry);

        std::sort(entries.begin(), entries.end(),
            [](const fs::directory_entry& left, const fs::directory_entry& right) {
                return left.path().filename().string() < right.path().filename().string();
            });

        for (size_t i = 0; i < entries.size(); ++i) {
            const auto& entry = entries[i];
            const bool last = (i == entries.size() - 1);
            const std::string name = entry.path().filename().string();
            lines.push_back(prefix + (last ? "└─ " : "├─ ") + name);

            if (fs::is_directory(entry.path()))
                walk(entry.path(), prefix + (last ? "   " : "│  "));
        }
    };

    lines.push_back(root.filename().empty() ? "adrat_network" : root.filename().string());
    for (const auto& entry : fs::directory_iterator(root)) {
        if (fs::is_directory(entry.path()))
            walk(entry.path(), "");
    }
    return lines;
}

void reloadNetworkTree()
{
    if (!networkTree)
        return;

    for (int i = 0; i < displayedRows; ++i)
        networkTree->delete_item(i);

    const std::vector<std::string> rows = loadNetworkTreeLines(findNetworkRoot());
    displayedRows = static_cast<int>(rows.size());
    for (size_t i = 0; i < rows.size(); ++i)
        networkTree->add_item(static_cast<int>(i), rows[i].c_str());

    if (displayedRows > 0)
        networkTree->set_int_val(0);
}

void onRefreshNetwork(GLUI_Control*)
{
    Docker::instance().setStatus("Scanning network...");
    if (!runNetworkScan(findNetworkRoot())) {
        Docker::instance().setStatus("Network scan failed. Check the scanner and permissions.");
        return;
    }

    reloadNetworkTree();
    if (networkDiagram)
        networkDiagram->load(findNetworkRoot() / "network.mmd");
    Docker::instance().setStatus("Network scan complete; tree refreshed.");
}
}

void buildAquilaPanel(GLUI* glui, GLUI_Panel* body)
{
    glui->add_statictext_to_panel(body, "TA tools");

    glui->add_statictext_to_panel(body, "Network diagram");
    glui->add_button_to_panel(body, "Zoom in", 1, onDiagramAction);
    glui->add_button_to_panel(body, "Zoom out", 2, onDiagramAction);
    glui->add_button_to_panel(body, "Reset view", 3, onDiagramAction);
    networkDiagram = new NetworkDiagram(body);
    networkDiagram->load(findNetworkRoot() / "network.mmd");

    glui->add_statictext_to_panel(body, "Network tree");
    glui->add_button_to_panel(body, "Refresh network", -1, onRefreshNetwork);

    networkTree = glui->add_listbox_to_panel(body, "Map", &selection);
    reloadNetworkTree();

    glui->add_statictext_to_panel(body, "Loaded from the folder Netscan.cpp exports.");
}