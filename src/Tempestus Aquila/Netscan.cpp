// ADRAT network discovery (Windows, C++17)
//
// 1. Finds the default gateway and traces the route out to the internet.
//    The last private router on that route is the "top router".
// 2. Looks for other routers by probing likely private subnets and tracing
//    the path to each one that answers. The paths are merged into a graph of
//    routers.
// 3. Sweeps the network behind every router found.
// 4. Walks the graph depth first from the top router, building a tree of
//    routers, each with the devices on its network.
// 5. Writes that tree out as real folders and files (default folder
//    "adrat_network", or the path given as the first argument) so a file
//    browser such as GLUI_FileBrowser can show it.
//
// Build with MSVC:   cl /EHsc /std:c++17 netscan.cpp
// Build with MinGW:  g++ -std=c++17 netscan.cpp -o netscan.exe -liphlpapi -lws2_32

#include "Netscan.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

// All addresses are kept as host byte order uint32_t inside the program and
// converted with htonl/ntohl only when talking to Windows.

static const char* kTraceTarget = "8.8.8.8";  // only used as a destination for the route trace
static const int kMaxHops = 30;
static const size_t kMaxSweepHosts = 1024;

struct EchoResult {
    bool got = false;
    DWORD status = 0;
    uint32_t addr = 0;
};

// ---------------------------------------------------------------- helpers

static uint32_t parseIp(const char* s) {
    in_addr a{};
    if (inet_pton(AF_INET, s, &a) != 1) return 0;
    return ntohl(a.s_addr);
}

static std::string ipStr(uint32_t h) {
    in_addr a{};
    a.s_addr = htonl(h);
    char buf[16];
    inet_ntop(AF_INET, &a, buf, sizeof buf);
    return buf;
}

static std::string macStr(const unsigned char* m, size_t n = 6) {
    std::string out;
    char b[4];
    for (size_t i = 0; i < n; ++i) {
        std::snprintf(b, sizeof b, i ? ":%02x" : "%02x", m[i]);
        out += b;
    }
    return out;
}

static bool isPrivate(uint32_t h) {
    return (h >> 24) == 10 ||        // 10.0.0.0/8
           (h >> 20) == 0xAC1 ||     // 172.16.0.0/12
           (h >> 16) == 0xC0A8;      // 192.168.0.0/16
}

static std::string networkStr(const Subnet& s) {
    int prefix = 0;
    for (uint32_t m = s.second; m & 0x80000000u; m <<= 1) ++prefix;
    return ipStr(s.first) + "/" + std::to_string(prefix);
}

template <class F>
static void parallelFor(size_t n, size_t workers, F f) {
    std::atomic<size_t> next{0};
    std::vector<std::thread> pool;
    workers = std::min(workers, n);
    for (size_t w = 0; w < workers; ++w) {
        pool.emplace_back([&] {
            size_t i;
            while ((i = next++) < n) f(i);
        });
    }
    for (auto& t : pool) t.join();
}

// ---------------------------------------------------------------- gateway

// Ask Windows which route it would use to reach the internet. That gives the
// default gateway and the adapter, from which we read our own IP and mask.
static bool getLocalInfo(LocalInfo& out) {
    MIB_IPFORWARDROW row{};
    if (GetBestRoute(htonl(parseIp(kTraceTarget)), 0, &row) != NO_ERROR) return false;
    out.gateway = ntohl(row.dwForwardNextHop);
    DWORD ifIndex = row.dwForwardIfIndex;

    ULONG size = 0;
    GetAdaptersInfo(nullptr, &size);
    std::vector<unsigned char> buf(size);
    auto* adapter = reinterpret_cast<IP_ADAPTER_INFO*>(buf.data());
    if (GetAdaptersInfo(adapter, &size) != NO_ERROR) return false;

    for (; adapter; adapter = adapter->Next) {
        if (adapter->Index != ifIndex) continue;
        for (IP_ADDR_STRING* s = &adapter->IpAddressList; s; s = s->Next) {
            uint32_t ip = parseIp(s->IpAddress.String);
            if (ip) {
                out.ip = ip;
                out.mask = parseIp(s->IpMask.String);
                break;
            }
        }
        out.mac = macStr(adapter->Address, adapter->AddressLength);
        return out.ip != 0 && out.mask != 0;
    }
    return false;
}

static Subnet subnetOf(const LocalInfo& local, uint32_t ip) {
    if ((ip & local.mask) == (local.ip & local.mask)) return {local.ip & local.mask, local.mask};
    return {ip & 0xFFFFFF00u, 0xFFFFFF00u};
}

// ------------------------------------------------------------------- ICMP

static EchoResult echo(uint32_t dst, UCHAR ttl, DWORD timeoutMs) {
    EchoResult r;
    HANDLE h = IcmpCreateFile();
    if (h == INVALID_HANDLE_VALUE) return r;

    char data[32] = "ADRAT";
    IP_OPTION_INFORMATION opt{};
    opt.Ttl = ttl;
    alignas(8) char reply[sizeof(ICMP_ECHO_REPLY) + sizeof(data) + 8 + 64];

    DWORD n = IcmpSendEcho(h, htonl(dst), data, sizeof data, &opt, reply, sizeof reply, timeoutMs);
    if (n > 0) {
        auto* e = reinterpret_cast<ICMP_ECHO_REPLY*>(reply);
        r.got = true;
        r.status = e->Status;
        r.addr = ntohl(e->Address);
    }
    IcmpCloseHandle(h);
    return r;
}

static bool ping(uint32_t ip) {
    EchoResult r = echo(ip, 128, 500);
    return r.got && r.status == IP_SUCCESS;
}

// Walk towards `target` one TTL at a time, recording all responding hops and
// the private routers separately. Stops at the target or at an error.
static TraceResult trace(uint32_t target, int attempts) {
    TraceResult t;
    for (int ttl = 1; ttl <= kMaxHops; ++ttl) {
        EchoResult r;
        for (int a = 0; a < attempts && !r.got; ++a) r = echo(target, (UCHAR)ttl, 1000);
        if (!r.got) continue;
        if (r.status != IP_TTL_EXPIRED_TRANSIT && r.status != IP_SUCCESS) return t;

        if (t.routeHops.empty() || t.routeHops.back() != r.addr) t.routeHops.push_back(r.addr);
        if (!isPrivate(r.addr)) {
            if (!t.publicHop) t.publicHop = r.addr;
        } else if (t.hops.empty() || t.hops.back() != r.addr) {
            t.hops.push_back(r.addr);
        }
        if (r.status == IP_SUCCESS) {
            t.reached = true;
            return t;
        }
    }
    return t;
}

// ------------------------------------------------------------------ sweeps

static std::vector<uint32_t> hostsOf(const Subnet& s) {
    uint32_t first = s.first + 1, last = (s.first | ~s.second) - 1;
    std::vector<uint32_t> hosts;
    if (last < first || (size_t)(last - first) + 1 > kMaxSweepHosts) return hosts;
    for (uint32_t ip = first; ip <= last; ++ip) hosts.push_back(ip);
    return hosts;
}

// Our own subnet: ask for each address with ARP. Devices cannot block ARP, so
// this also finds machines that ignore pings, and it gives us MAC addresses.
static std::map<uint32_t, std::string> sweepArp(const std::vector<uint32_t>& hosts, uint32_t self) {
    std::map<uint32_t, std::string> found;
    std::mutex mu;
    parallelFor(hosts.size(), 256, [&](size_t i) {
        uint32_t ip = hosts[i];
        if (ip == self) return;
        ULONG mac[2] = {0, 0};
        ULONG len = 6;
        if (SendARP(htonl(ip), 0, mac, &len) == NO_ERROR && len == 6) {
            std::lock_guard<std::mutex> lock(mu);
            found[ip] = macStr(reinterpret_cast<unsigned char*>(mac));
        }
    });
    return found;
}

// Other networks are reached through routing, so ARP does not apply there.
// Ping is all we have, and we get no MAC addresses.
static std::set<uint32_t> sweepPing(const std::vector<uint32_t>& hosts) {
    std::set<uint32_t> found;
    std::mutex mu;
    parallelFor(hosts.size(), 128, [&](size_t i) {
        if (ping(hosts[i])) {
            std::lock_guard<std::mutex> lock(mu);
            found.insert(hosts[i]);
        }
    });
    return found;
}

// Addresses that are very likely to be a router on some private network:
// the .1 and .254 of every 192.168.x.0/24, plus a few common 10.x and 172.16.x.
static std::vector<uint32_t> candidateRouters() {
    std::vector<uint32_t> c;
    for (int i = 0; i < 256; ++i) {
        c.push_back(parseIp(("192.168." + std::to_string(i) + ".1").c_str()));
        c.push_back(parseIp(("192.168." + std::to_string(i) + ".254").c_str()));
    }
    const char* extra[] = {"10.0.0.1", "10.0.1.1", "10.1.1.1", "10.10.10.1", "10.0.0.138",
                           "172.16.0.1", "172.16.1.1", "172.16.10.1"};
    for (const char* e : extra) c.push_back(parseIp(e));
    return c;
}

// --------------------------------------------------------------- hostnames

static std::string hostname(uint32_t ip) {
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(ip);
    char host[NI_MAXHOST];
    if (getnameinfo(reinterpret_cast<sockaddr*>(&sa), sizeof sa, host, sizeof host, nullptr, 0, NI_NAMEREQD) == 0)
        return host;
    return "";
}

// Look everything up at the same time and give up on slow ones.
static std::map<uint32_t, std::string> resolveNames(const std::vector<uint32_t>& ips, int timeoutMs = 3000) {
    struct State {
        std::mutex mu;
        std::map<uint32_t, std::string> names;
    };
    auto state = std::make_shared<State>();
    auto done = std::make_shared<std::atomic<size_t>>(0);

    for (uint32_t ip : ips) {
        std::thread([state, done, ip] {
            std::string n = hostname(ip);
            {
                std::lock_guard<std::mutex> lock(state->mu);
                state->names[ip] = n;
            }
            ++*done;
        }).detach();
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (done->load() < ips.size() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::lock_guard<std::mutex> lock(state->mu);
    return state->names;
}

// -------------------------------------------------------------- tree search

struct Context {
    LocalInfo local;
    uint32_t top = 0;
    std::set<uint32_t> nodes;                          // every router found
    std::map<uint32_t, std::set<uint32_t>> adj;        // links between routers
    std::map<Subnet, std::map<uint32_t, std::string>> subnets;  // subnet -> (ip -> mac) of live hosts
    std::set<Subnet> claimed;                          // subnets already listed under a router
    std::set<uint32_t> visited;
    std::mutex mu;
};

// A trace gives a chain of routers, each linked to the next.
static void addChain(Context& c, const std::vector<uint32_t>& hops) {
    std::lock_guard<std::mutex> lock(c.mu);
    for (size_t i = 0; i < hops.size(); ++i) {
        c.nodes.insert(hops[i]);
        c.adj[hops[i]];
        if (i > 0) {
            c.adj[hops[i - 1]].insert(hops[i]);
            c.adj[hops[i]].insert(hops[i - 1]);
        }
    }
}

// Depth first search from the top router. Each router lists the devices on its
// own network, then the search goes down into each router linked to it that
// has not been visited yet.
static RouterNode build(Context& c, uint32_t ip) {
    c.visited.insert(ip);

    RouterNode n;
    n.router.ip = ip;
    Subnet s = subnetOf(c.local, ip);
    n.network = networkStr(s);

    if (ip == c.top && ip == c.local.gateway) n.router.role = "top router, your gateway";
    else if (ip == c.top) n.router.role = "top router";
    else if (ip == c.local.gateway) n.router.role = "your gateway";
    else n.router.role = "router";

    auto it = c.subnets.find(s);
    if (it != c.subnets.end()) {
        auto self = it->second.find(ip);
        if (self != it->second.end()) n.router.mac = self->second;

        if (!c.claimed.count(s)) {
            c.claimed.insert(s);
            for (auto& [dip, mac] : it->second) {
                if (c.nodes.count(dip)) continue;  // routers appear as tree nodes instead
                Host h;
                h.ip = dip;
                h.mac = mac;
                if (dip == c.local.ip) h.role = "this machine";
                n.devices.push_back(h);
            }
        }
    }

    for (uint32_t next : c.adj[ip])
        if (!c.visited.count(next)) n.children.push_back(build(c, next));
    return n;
}

static void collectIps(const RouterNode& n, std::vector<uint32_t>& out) {
    out.push_back(n.router.ip);
    for (auto& d : n.devices) out.push_back(d.ip);
    for (auto& ch : n.children) collectIps(ch, out);
}

static void fillNames(RouterNode& n, const std::map<uint32_t, std::string>& names) {
    auto look = [&](uint32_t ip) {
        auto it = names.find(ip);
        return it == names.end() ? std::string() : it->second;
    };
    n.router.name = look(n.router.ip);
    for (auto& d : n.devices) d.name = look(d.ip);
    for (auto& ch : n.children) fillNames(ch, names);
}

static bool scan(RouterNode& root) {
    Context c;
    if (!getLocalInfo(c.local)) {
        std::fprintf(stderr, "Could not find a default gateway. Are you connected?\n");
        return false;
    }

    // Step 1: the route out to the internet. The last private hop is the top router.
    TraceResult out = trace(parseIp(kTraceTarget), 2);
    std::vector<uint32_t> chain = out.hops;
    if (chain.empty() || chain.front() != c.local.gateway) chain.insert(chain.begin(), c.local.gateway);
    c.top = chain.back();
    addChain(c, chain);

    std::printf("You            : %s\n", ipStr(c.local.ip).c_str());
    std::printf("Local network  : %s\n", networkStr(subnetOf(c.local, c.local.ip)).c_str());
    std::vector<uint32_t> route = out.routeHops;
    if (route.empty() || route.front() != c.local.gateway) route.insert(route.begin(), c.local.gateway);
    std::printf("Route out      : ");
    for (size_t i = 0; i < route.size(); ++i) std::printf("%s%s", i ? " -> " : "", ipStr(route[i]).c_str());
    std::printf("\nTop router     : %s\n", ipStr(c.top).c_str());
    std::printf("First public   : %s\n\n", out.publicHop ? ipStr(out.publicHop).c_str() : "not found");

    // Step 2: look for other routers and trace the path to each one.
    std::printf("Looking for other routers...\n");
    std::fflush(stdout);
    std::set<uint32_t> responders = sweepPing(candidateRouters());
    std::vector<uint32_t> targets(responders.begin(), responders.end());
    parallelFor(targets.size(), 16, [&](size_t i) {
        TraceResult t = trace(targets[i], 2);
        if (t.reached) addChain(c, t.hops);
    });

    // Step 3: sweep the network behind every router.
    std::printf("Sweeping %zu network(s), this takes a few seconds...\n", c.nodes.size());
    std::fflush(stdout);
    std::set<Subnet> need;
    Subnet localSubnet = subnetOf(c.local, c.local.ip);
    need.insert(localSubnet);
    for (uint32_t n : c.nodes) need.insert(subnetOf(c.local, n));

    for (const Subnet& s : need) {
        auto hosts = hostsOf(s);
        if (hosts.empty()) {
            std::printf("Skipping %s, too many addresses to sweep.\n", networkStr(s).c_str());
            continue;
        }
        std::map<uint32_t, std::string> found;
        if (s == localSubnet) {
            found = sweepArp(hosts, c.local.ip);
            found[c.local.ip] = c.local.mac;
        } else {
            for (uint32_t ip : sweepPing(hosts)) found[ip] = "";
        }
        for (uint32_t n : c.nodes)
            if (subnetOf(c.local, n) == s) found.emplace(n, "");  // routers we traced are alive by definition
        c.subnets[s] = found;
    }

    // Step 4: depth first search from the top router, then look up names.
    root = build(c, c.top);
    std::vector<uint32_t> ips;
    collectIps(root, ips);
    fillNames(root, resolveNames(ips));
    return true;
}

// ------------------------------------------------------------------ output

static void printTree(const RouterNode& n, int depth) {
    std::string pad(depth * 4, ' ');
    std::printf("\n%sRouter %-16s %-18s %-26s %s\n", pad.c_str(), ipStr(n.router.ip).c_str(),
                n.router.mac.c_str(), n.router.name.c_str(), n.router.role.c_str());
    if (!n.devices.empty()) {
        std::printf("%s  Devices on %s\n", pad.c_str(), n.network.c_str());
        for (auto& d : n.devices)
            std::printf("%s    %-16s %-18s %-26s %s\n", pad.c_str(), ipStr(d.ip).c_str(), d.mac.c_str(),
                        d.name.c_str(), d.role.c_str());
    }
    for (auto& ch : n.children) printTree(ch, depth + 1);
}

// ----------------------------------------------------------- file hierarchy

// Each router becomes a folder. The devices on its network are files inside
// that folder, and the routers found further out are subfolders. The folder
// can be opened with any file browser, including GLUI_FileBrowser.

static std::string labelOf(const Host& h) {
    return ipStr(h.ip) + (h.name.empty() ? std::string() : " (" + h.name + ")");
}

static std::string detailsOf(const Host& h, const std::string& network) {
    std::string s;
    s += "IP:       " + ipStr(h.ip) + "\n";
    s += "MAC:      " + (h.mac.empty() ? std::string("unknown") : h.mac) + "\n";
    s += "Hostname: " + (h.name.empty() ? std::string("unknown") : h.name) + "\n";
    s += "Role:     " + (h.role.empty() ? std::string("device") : h.role) + "\n";
    s += "Network:  " + network + "\n";
    return s;
}

// Windows does not allow some characters in file names, and hostnames can contain odd ones.
static std::string safeName(std::string s) {
    for (char& ch : s)
        if (std::strchr("<>:\"/\\|?*", ch) || (unsigned char)ch < 32) ch = '_';
    while (!s.empty() && (s.back() == '.' || s.back() == ' ')) s.pop_back();
    return s.empty() ? "unnamed" : s;
}

static void writeText(const fs::path& p, const std::string& text) {
    std::ofstream f(p);
    f << text;
}

static void writeNode(const RouterNode& n, const fs::path& dir) {
    fs::create_directories(dir);
    writeText(dir / "_router.txt", detailsOf(n.router, n.network));
    for (auto& d : n.devices) writeText(dir / (safeName(labelOf(d)) + ".txt"), detailsOf(d, n.network));
    for (auto& ch : n.children) writeNode(ch, dir / safeName("Router " + labelOf(ch.router)));
}

// The output folder is replaced on every run. To avoid deleting anything that
// is not ours, an existing non-empty folder is only replaced if it holds the
// marker file this tool leaves behind.
static bool exportTree(const RouterNode& root, const fs::path& base) {
    try {
        const fs::path marker = base / ".adrat_tree";
        if (fs::exists(base)) {
            if (!fs::exists(marker) && !fs::is_empty(base)) {
                std::fprintf(stderr, "%s already exists and was not made by this tool, so it was left alone.\n",
                             base.string().c_str());
                return false;
            }
            fs::remove_all(base);
        }
        fs::create_directories(base);
        writeText(marker, "Made by ADRAT netscan. This folder is replaced on every run.\n");
        SetFileAttributesA(marker.string().c_str(), FILE_ATTRIBUTE_HIDDEN);
        writeNode(root, base / safeName("Router " + labelOf(root.router)));
        return true;
    } catch (const fs::filesystem_error& e) {
        std::fprintf(stderr, "Could not write the folder tree: %s\n", e.what());
        return false;
    }
}

int main(int argc, char** argv) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::fprintf(stderr, "WSAStartup failed.\n");
        return 1;
    }

    RouterNode root;
    if (scan(root)) {
        printTree(root, 0);
        fs::path out = argc > 1 ? fs::path(argv[1]) : fs::path("adrat_network");
        if (exportTree(root, out))
            std::printf("\nFile hierarchy written to %s\n", fs::absolute(out).string().c_str());
    }

    WSACleanup();
    return 0;
}