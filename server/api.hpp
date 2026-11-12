#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include "httplib.h"
#include "rooms.hpp"

/**
 * @file api.hpp
 * @brief HTTP API и поток событий SSE по docs/protocol.md.
 */

namespace seabattle::server {

/// Настройки API.
struct ApiConfig {
    /// Интервал строки-комментария `: ping` в потоке событий.
    std::chrono::milliseconds ping{15000};
    /// Внешний адрес из --public-url для ссылки-приглашения, пустой - не задан.
    std::string public_url;
    /// Адреса сервера в сети для GET /api/share; не задано - пустой список.
    std::function<std::vector<std::string>()> addresses;
};

/**
 * @brief Зарегистрировать обработчики /api/... на сервере.
 * @param server сервер
 * @param rooms комнаты, живут дольше сервера
 */
void setup_api(httplib::Server &server, RoomManager &rooms, ApiConfig config = {});

} // namespace seabattle::server
