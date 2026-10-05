#include "Aquila-Panel.hpp"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

GLUI_Listbox* networkTree = nullptr;
int selection = 0;
int displayedRows = 0;

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

fs::path findNetworkScanner()
{
    std::vector<fs::path> candidates;
    const fs::path current = fs::current_path();

#ifdef _WIN32
    wchar_t executablePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        const fs::path executableDirectory = fs::path(std::wstring(executablePath, length)).parent_path();
        candidates.push_back(executableDirectory / "netscan.exe");
        candidates.push_back(executableDirectory / "Tempestus Aquila" / "netscan.exe");
    }
#endif

    candidates.push_back(current / "netscan.exe");
    candidates.push_back(current / "Tempestus Aquila" / "netscan.exe");
    candidates.push_back(current / "src" / "Tempestus Aquila" / "netscan.exe");
    candidates.push_back(current.parent_path() / "Tempestus Aquila" / "netscan.exe");
    candidates.push_back(current.parent_path() / "src" / "Tempestus Aquila" / "netscan.exe");

    for (const auto& candidate : candidates) {
        if (fs::is_regular_file(candidate))
            return fs::absolute(candidate);
    }
    return {};
}

bool runNetworkScanner(const fs::path& scanner, const fs::path& outputDirectory)
{
#ifdef _WIN32
    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    std::wstring commandLine = L"\"" + scanner.wstring() + L"\" \"" +
        fs::absolute(outputDirectory).wstring() + L"\"";
    const std::wstring workingDirectory = scanner.parent_path().wstring();

    if (!CreateProcessW(scanner.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, workingDirectory.c_str(), &startupInfo, &processInfo))
        return false;

    const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, INFINITE);
    DWORD exitCode = 1;
    const bool succeeded = waitResult == WAIT_OBJECT_0 &&
        GetExitCodeProcess(processInfo.hProcess, &exitCode) && exitCode == 0;
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return succeeded;
#else
    (void)scanner;
    (void)outputDirectory;
    return false;
#endif
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
    const fs::path scanner = findNetworkScanner();
    if (scanner.empty()) {
        Docker::instance().setStatus("Network scan failed: netscan.exe was not found.");
        return;
    }

    Docker::instance().setStatus("Scanning network...");
    if (!runNetworkScanner(scanner, findNetworkRoot())) {
        Docker::instance().setStatus("Network scan failed. Check the scanner and permissions.");
        return;
    }

    reloadNetworkTree();
    Docker::instance().setStatus("Network scan complete; tree refreshed.");
}
}

void buildAquilaPanel(GLUI* glui, GLUI_Panel* body)
{
    glui->add_statictext_to_panel(body, "TA tools");

    glui->add_statictext_to_panel(body, "Network tree");
    glui->add_button_to_panel(body, "Refresh network", -1, onRefreshNetwork);

    networkTree = glui->add_listbox_to_panel(body, "Map", &selection);
    reloadNetworkTree();

    glui->add_statictext_to_panel(body, "Loaded from the folder Netscan.cpp exports.");
}