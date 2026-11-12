#include "network.hpp"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

#include <algorithm>
#include <filesystem>
#include <string_view>

namespace seabattle::server {

namespace {

bool virtual_bridge(std::string_view name) {
    for (const std::string_view prefix : {"docker", "br-", "veth", "virbr", "vnet"}) {
        if (name.substr(0, prefix.size()) == prefix) return true;
    }
    return false;
}

bool private_address(const std::string &address) {
    in_addr addr{};
    if (inet_pton(AF_INET, address.c_str(), &addr) != 1) return false;
    const auto ip = ntohl(addr.s_addr);
    return (ip >> 24) == 10 || (ip >> 20) == ((172U << 4) | 1U) || (ip >> 16) == ((192U << 8) | 168U);
}

} // namespace

std::vector<std::string> pick_addresses(const std::vector<InterfaceAddress> &interfaces) {
    std::vector<std::string> result;
    for (const auto &item : interfaces) {
        if (!item.up || item.loopback || virtual_bridge(item.name)) continue;
        if (std::find(result.begin(), result.end(), item.address) != result.end()) continue;
        result.push_back(item.address);
    }
    std::stable_partition(result.begin(), result.end(), private_address);
    return result;
}

std::vector<InterfaceAddress> system_interfaces() {
    std::vector<InterfaceAddress> result;
    ifaddrs *list = nullptr;
    if (getifaddrs(&list) != 0) return result;
    for (const ifaddrs *it = list; it != nullptr; it = it->ifa_next) {
        if (it->ifa_addr == nullptr || it->ifa_addr->sa_family != AF_INET) continue;
        char text[INET_ADDRSTRLEN] = {};
        const auto *addr = reinterpret_cast<const sockaddr_in *>(it->ifa_addr);
        if (inet_ntop(AF_INET, &addr->sin_addr, text, sizeof(text)) == nullptr) continue;
        result.push_back({it->ifa_name, text, (it->ifa_flags & IFF_UP) != 0, (it->ifa_flags & IFF_LOOPBACK) != 0});
    }
    freeifaddrs(list);
    return result;
}

bool in_container() {
    std::error_code ec;
    return std::filesystem::exists("/.dockerenv", ec);
}

} // namespace seabattle::server
