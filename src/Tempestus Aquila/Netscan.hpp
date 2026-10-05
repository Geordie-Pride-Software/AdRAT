#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using Subnet = std::pair<uint32_t, uint32_t>;

struct LocalInfo {
    uint32_t ip = 0;
    uint32_t mask = 0;
    uint32_t gateway = 0;
    std::string mac;
};

struct Host {
    uint32_t ip = 0;
    std::string mac;
    std::string name;
    std::string role;
};

struct RouterNode {
    Host router;
    std::string network;
    std::vector<Host> devices;
    std::vector<RouterNode> children;
};

struct TraceResult {
    std::vector<uint32_t> hops;
    std::vector<uint32_t> routeHops;
    bool reached = false;
    uint32_t publicHop = 0;
};
