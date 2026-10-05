#include "About.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string trim(const std::string& value)
{
	const size_t first = value.find_first_not_of(" \t\r\n");
	if (first == std::string::npos)
		return {};

	const size_t last = value.find_last_not_of(" \t\r\n");
	return value.substr(first, last - first + 1);
}

fs::path findOptionsFile()
{
	std::vector<fs::path> candidates;
	fs::path directory = fs::current_path();
	for (int depth = 0; depth < 5 && !directory.empty(); ++depth) {
		candidates.push_back(directory / "options.yaml");
		candidates.push_back(directory / "src" / "options.yaml");
		const fs::path parent = directory.parent_path();
		if (parent == directory)
			break;
		directory = parent;
	}

	candidates.push_back(fs::path(__FILE__).parent_path().parent_path() / "options.yaml");
	for (const auto& candidate : candidates) {
		if (fs::is_regular_file(candidate))
			return candidate;
	}
	return {};
}

std::vector<std::pair<std::string, std::string>> loadOptions()
{
	const fs::path optionsFile = findOptionsFile();
	if (optionsFile.empty())
		return {{"", "Could not find options.yaml."}};

	std::ifstream file(optionsFile);
	if (!file)
		return {{"", "Could not read options.yaml."}};

	std::vector<std::pair<std::string, std::string>> options;
	std::string line;
	while (std::getline(file, line)) {
		line = trim(line);
		if (line.empty() || line[0] == '#')
			continue;

		const size_t separator = line.find(':');
		if (separator == std::string::npos)
			continue;

		const std::string key = trim(line.substr(0, separator));
		std::string value = trim(line.substr(separator + 1));
		if (key.empty() || value.empty())
			continue;

		if (value.back() == ';')
			value = trim(value.substr(0, value.size() - 1));
		if (value.size() >= 2 &&
			((value.front() == '"' && value.back() == '"') ||
			 (value.front() == '\'' && value.back() == '\'')))
			value = value.substr(1, value.size() - 2);

		options.emplace_back(key, value);
	}

	return options;
}

}

std::string applicationWindowTitle()
{
	for (const auto& option : loadOptions()) {
		if (option.first == "Name")
			return option.second;
	}
	return "AdRAT";
}

void buildAboutPanel(GLUI* glui, GLUI_Panel* body)
{
	const auto options = loadOptions();
	if (options.empty()) {
		glui->add_statictext_to_panel(body, "No settings found in options.yaml.");
		return;
	}

	for (const auto& option : options) {
		const std::string detail = option.first.empty()
			? option.second
			: option.first + ": " + option.second;
		glui->add_statictext_to_panel(body, detail.c_str());
	}
}
