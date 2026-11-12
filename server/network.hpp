#pragma once

#include <string>
#include <vector>

/**
 * @file network.hpp
 * @brief Адреса сервера в локальной сети для ссылки-приглашения.
 */

namespace seabattle::server {

/// IPv4-адрес сетевого интерфейса.
struct InterfaceAddress {
    std::string name;    ///< имя интерфейса: eth0, wlan0
    std::string address; ///< адрес в точечной записи
    bool up = false;     ///< интерфейс поднят
    bool loopback = false;
};

/**
 * @brief Адреса, по которым к серверу можно подключиться из сети.
 *
 * Отбрасываются опущенные интерфейсы, loopback и мосты docker, libvirt и veth; повторы убираются.
 * Частные сети (10/8, 172.16/12, 192.168/16) идут первыми, внутри групп порядок исходный.
 */
std::vector<std::string> pick_addresses(const std::vector<InterfaceAddress> &interfaces);

/**
 * @brief IPv4-адреса интерфейсов системы через getifaddrs.
 * @return пустой список при ошибке getifaddrs
 */
std::vector<InterfaceAddress> system_interfaces();

/// Сервер запущен в контейнере (есть /.dockerenv).
bool in_container();

} // namespace seabattle::server
