#include <pthread.h>

#include <csignal>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

#include "api.hpp"
#include "network.hpp"
#include "options.hpp"

using namespace seabattle::server;

namespace {

/// Сколько обработчиков сразу: каждый открытый поток событий держит один.
constexpr std::size_t WORKERS = 128;
/// Через сколько удалять комнату без игроков в сети.
constexpr std::chrono::minutes ROOM_IDLE{30};

} // namespace

int main(int argc, char *argv[]) {
    Options opts;
    try {
        opts = parse_options(argc, argv);
    } catch (const std::invalid_argument &e) {
        std::cerr << e.what() << "\n\n" << usage(argv[0]);
        return 2;
    }
    if (opts.help) {
        std::cout << usage(argv[0]);
        return 0;
    }
    if (opts.version) {
        std::cout << SEABATTLE_VERSION << '\n';
        return 0;
    }
    if (!std::filesystem::is_directory(opts.web)) {
        std::cerr << "Нет каталога веб-клиента: " << opts.web << '\n';
        return 1;
    }

    // сигналы принимает отдельный поток через sigwait, остальные потоки их не получают
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &signals, nullptr);

    RoomManager rooms;
    httplib::Server server;
    server.new_task_queue = [] { return new httplib::ThreadPool(WORKERS); };
    ApiConfig api;
    api.public_url = opts.public_url;
    // в контейнере свои адреса снаружи недоступны: ссылку строит клиент или --public-url
    api.addresses = [] { return in_container() ? std::vector<std::string>{} : pick_addresses(system_interfaces()); };
    setup_api(server, rooms, api);
    server.set_mount_point("/", opts.web);

    std::mutex stop_mutex;
    std::condition_variable stop_cv;
    bool stopping = false;

    std::thread cleaner([&] {
        std::unique_lock lock(stop_mutex);
        while (!stop_cv.wait_for(lock, std::chrono::minutes(1), [&] { return stopping; })) {
            rooms.cleanup(RoomManager::Clock::now(), ROOM_IDLE);
        }
    });
    std::thread signal_waiter([&] {
        int sig = 0;
        sigwait(&signals, &sig);
        {
            std::lock_guard lock(stop_mutex);
            stopping = true;
        }
        stop_cv.notify_all();
        rooms.shutdown();
        server.stop();
    });

    std::cout << "seabattle " << SEABATTLE_VERSION << ": http://" << opts.host << ':' << opts.port << '/' << std::endl;
    if (opts.host == "0.0.0.0") {
        for (const auto &address : api.addresses()) {
            std::cout << "  в сети: http://" << address << ':' << opts.port << '/' << std::endl;
        }
    }
    const bool ok = server.listen(opts.host, opts.port);
    if (!ok) {
        std::cerr << "Не удалось слушать " << opts.host << ':' << opts.port << '\n';
        // разбудить поток сигналов, чтобы завершиться
        pthread_kill(signal_waiter.native_handle(), SIGTERM);
    }
    signal_waiter.join();
    cleaner.join();
    return ok ? 0 : 1;
}
