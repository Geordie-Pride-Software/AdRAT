#include "Aquila-Panel.hpp"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

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
}

void buildAquilaPanel(GLUI* glui, GLUI_Panel* body)
{
    glui->add_statictext_to_panel(body, "TA tools");

    static int selection = 0;
    glui->add_statictext_to_panel(body, "Network tree");

    GLUI_Listbox* tree = glui->add_listbox_to_panel(body, "Map", &selection);
    const fs::path root = findNetworkRoot();
    const std::vector<std::string> rows = loadNetworkTreeLines(root);

    if (rows.empty()) {
        tree->add_item(0, "No scan output found");
    } else {
        for (size_t i = 0; i < rows.size(); ++i)
            tree->add_item(static_cast<int>(i), rows[i].c_str());
    }
    tree->set_int_val(0);

    glui->add_statictext_to_panel(body, "Loaded from the folder Netscan.cpp exports.");
}