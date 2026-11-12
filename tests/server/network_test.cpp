#include "network.hpp"

#include <gtest/gtest.h>

using namespace seabattle::server;

namespace {

std::string join(const std::vector<std::string> &items) {
    std::string text;
    for (const auto &item : items) text += item + ' ';
    return text;
}

} // namespace

// Отбрасываются loopback, опущенные интерфейсы и мосты docker, libvirt, veth
TEST(Network, SkipsLoopbackDownAndBridges) {
    const std::vector<InterfaceAddress> interfaces = {
        {"lo", "127.0.0.1", true, true},          {"eth0", "192.168.88.20", true, false},   {"docker0", "172.17.0.1", true, false},
        {"br-1a2b3c", "172.18.0.1", true, false}, {"veth12ab", "169.254.3.3", true, false}, {"virbr0", "192.168.122.1", true, false},
        {"vnet0", "10.9.0.1", true, false},       {"wlan0", "10.0.0.7", false, false},
    };
    const auto result = pick_addresses(interfaces);
    EXPECT_EQ(result, std::vector<std::string>{"192.168.88.20"}) << "получили: " << join(result);
}

// Частные сети первыми, внутри групп исходный порядок, повторы убираются
TEST(Network, PrivateFirstAndUnique) {
    const std::vector<InterfaceAddress> interfaces = {
        {"eth0", "203.0.113.5", true, false}, {"eth1", "172.20.1.2", true, false},   {"wg0", "10.8.0.2", true, false},
        {"eth2", "172.32.0.1", true, false},  {"eth3", "192.168.1.10", true, false}, {"eth3", "192.168.1.10", true, false},
    };
    const auto result = pick_addresses(interfaces);
    const std::vector<std::string> expected = {"172.20.1.2", "10.8.0.2", "192.168.1.10", "203.0.113.5", "172.32.0.1"};
    EXPECT_EQ(result, expected) << "получили: " << join(result);
}

// Системные интерфейсы читаются, и среди них есть loopback 127.0.0.1
TEST(Network, SystemInterfacesHaveLoopback) {
    const auto interfaces = system_interfaces();
    bool loopback = false;
    for (const auto &item : interfaces) loopback = loopback || (item.loopback && item.address == "127.0.0.1");
    EXPECT_TRUE(loopback) << "интерфейсов: " << interfaces.size();
}
