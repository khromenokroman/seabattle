#include "options.hpp"

#include <charconv>
#include <stdexcept>
#include <string_view>

namespace seabattle::server {

Options parse_options(int argc, const char *const argv[]) {
    Options opts;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const auto value = [&]() -> std::string {
            if (i + 1 >= argc) throw std::invalid_argument("Не задано значение для " + std::string(arg));
            return argv[++i];
        };
        if (arg == "--help" || arg == "-h") {
            opts.help = true;
        } else if (arg == "--version" || arg == "-v") {
            opts.version = true;
        } else if (arg == "--host") {
            opts.host = value();
        } else if (arg == "--web") {
            opts.web = value();
        } else if (arg == "--port") {
            const auto text = value();
            int port = 0;
            const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), port);
            if (ec != std::errc() || end != text.data() + text.size() || port < 1 || port > 65535) {
                throw std::invalid_argument("Порт должен быть числом от 1 до 65535: " + text);
            }
            opts.port = port;
        } else {
            throw std::invalid_argument("Неизвестный параметр " + std::string(arg));
        }
    }
    return opts;
}

std::string usage(const std::string &program) {
    return "Использование: " + program +
           " [параметры]\n"
           "Сервер игры «Морской бой»: веб-клиент и API для двух игроков в комнате.\n\n"
           "  --host АДРЕС  адрес для приёма соединений (по умолчанию 0.0.0.0)\n"
           "  --port ПОРТ   порт HTTP (по умолчанию 8090)\n"
           "  --web КАТАЛОГ файлы веб-клиента (по умолчанию " SEABATTLE_WEB_DIR
           ")\n"
           "  --version     версия\n"
           "  --help        эта справка\n";
}

} // namespace seabattle::server
