#pragma once

#include <string>

/**
 * @file options.hpp
 * @brief Параметры командной строки сервера.
 */

namespace seabattle::server {

/// Параметры запуска.
struct Options {
    std::string host = "0.0.0.0";
    std::string web = SEABATTLE_WEB_DIR;
    std::string public_url; ///< внешний адрес для ссылок, без завершающего /
    int port = 8090;
    bool help = false;
    bool version = false;
};

/**
 * @brief Разбор аргументов: --host, --port, --web, --public-url, --help, --version.
 * @throw std::invalid_argument с текстом для пользователя
 */
Options parse_options(int argc, const char *const argv[]);

/// Справка по запуску.
std::string usage(const std::string &program);

} // namespace seabattle::server
