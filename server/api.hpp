#pragma once

#include <chrono>

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
};

/**
 * @brief Зарегистрировать обработчики /api/... на сервере.
 * @param server сервер
 * @param rooms комнаты, живут дольше сервера
 */
void setup_api(httplib::Server &server, RoomManager &rooms, ApiConfig config = {});

} // namespace seabattle::server
