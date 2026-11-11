#pragma once

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "seabattle/game.hpp"

/**
 * @file rooms.hpp
 * @brief Комнаты на двух игроков: вход по коду, токены, партия, снимки состояния и ожидание событий.
 *
 * Без HTTP: api.cpp переводит запросы в вызовы RoomManager. Все методы потокобезопасны.
 */

namespace seabattle::server {

/// Отказ с HTTP-кодом и текстом для игрока.
class RoomError : public std::runtime_error {
   public:
    RoomError(int status, const std::string &message) : std::runtime_error(message), m_status(status) {}
    int status() const { return m_status; }

   private:
    int m_status;
};

/// Игрок вошёл в комнату.
struct Joined {
    std::string room;
    std::string token;
    int player = 0;
};

/// Владелец токена.
struct TokenInfo {
    std::string room;
    std::string name;
    int player = 0;
};

/// Что вернуло ожидание события потока.
enum class StreamEvent {
    STATE,    ///< состояние изменилось, снимок в out
    PING,     ///< таймаут без изменений
    REPLACED, ///< для того же токена открыт новый поток (игра открыта в другой вкладке)
    CLOSED    ///< поток надо закрыть: токен недействителен или сервер останавливается
};

/// Комнаты и игроки.
class RoomManager {
   public:
    using Clock = std::chrono::steady_clock;

    /// Длина кода комнаты.
    static constexpr std::size_t CODE_LENGTH = 4;
    /// Максимальная длина имени в символах.
    static constexpr std::size_t MAX_NAME_LENGTH = 20;
    /// Сколько комнат может быть сразу: защита памяти от массового создания.
    static constexpr std::size_t MAX_ROOMS = 1000;

    /**
     * @param seed зерно генератора кодов комнат и очереди первого хода; токены всегда из std::random_device
     */
    explicit RoomManager(std::uint32_t seed = std::random_device{}());

    /**
     * @brief Новая комната, создатель - игрок 0.
     * @throw RoomError 400 при неправильном имени, 503 если комнат уже MAX_ROOMS
     */
    Joined create_room(const std::string &name);

    /**
     * @brief Войти в комнату по коду (без учёта регистра) на свободное место.
     * @throw RoomError 400 имя, 404 нет комнаты, 409 занято
     */
    Joined join_room(const std::string &code, const std::string &name);

    /**
     * @brief Кто владеет токеном.
     * @throw RoomError 401
     */
    TokenInfo me(const std::string &token) const;

    /**
     * @brief Расставить флот.
     * @throw RoomError 401, 409 (нет противника, не та фаза, уже расставлен), 400 (флот не по правилам)
     */
    void place(const std::string &token, std::vector<Ship> fleet);

    /**
     * @brief Выстрел по полю противника.
     * @return MISS, HIT или SUNK
     * @throw RoomError 401, 409 (не та фаза, не твой ход, уже стреляли), 400 (вне поля)
     */
    ShotResult fire(const std::string &token, Coord at);

    /// Сдаться. @throw RoomError 401, 409
    void resign(const std::string &token);

    /// Попросить ещё партию; когда попросили оба - новая партия. @throw RoomError 401, 409
    void rematch(const std::string &token);

    /// Выйти из комнаты, токен перестаёт действовать. @throw RoomError 401
    void leave(const std::string &token);

    /**
     * @brief Снимок состояния для владельца токена (формат docs/protocol.md).
     * @throw RoomError 401
     */
    nlohmann::json snapshot(const std::string &token) const;

    /**
     * @brief Открыть поток событий: игрок в сети, прежний поток того же токена закрывается.
     * @return номер потока для wait_event и close_stream
     * @throw RoomError 401
     */
    std::uint64_t open_stream(const std::string &token);

    /// Поток закрыт (клиент отключился). Неизвестный токен или старый номер игнорируются.
    void close_stream(const std::string &token, std::uint64_t stream);

    /**
     * @brief Ждать изменения комнаты.
     * @param seen версия, которую поток уже отправил (0 - ничего); при STATE обновляется
     * @param out снимок при STATE
     * @param timeout сколько ждать до PING
     */
    StreamEvent wait_event(const std::string &token, std::uint64_t stream, std::uint64_t &seen, nlohmann::json &out,
                           std::chrono::milliseconds timeout);

    /**
     * @brief Удалить комнаты, в которых никого нет в сети дольше idle.
     * @return сколько удалено
     */
    std::size_t cleanup(Clock::time_point now, std::chrono::minutes idle);

    /// Остановка сервера: все ожидающие потоки получают CLOSED.
    void shutdown();

    /// Число комнат.
    std::size_t room_count() const;

   private:
    struct Player {
        std::string name;
        std::string token;
        std::uint64_t stream = 0; ///< номер актуального потока, 0 - не в сети
        bool rematch = false;
    };

    struct Room {
        std::string code;
        Game game;
        std::array<std::optional<Player>, 2> players;
        Clock::time_point idle_since; ///< с какого момента никого нет в сети
        std::uint64_t version = 1;    ///< растёт при любом изменении
    };

    struct Seat {
        Room *room = nullptr;
        int player = 0;
    };

    Seat seat_locked(const std::string &token) const;
    void changed(Room &room);
    nlohmann::json snapshot_locked(const Room &room, int player) const;
    std::string new_code();
    std::string new_token();
    void new_game(Room &room, int first_player);

    std::map<std::string, Room> m_rooms;
    std::map<std::string, Seat> m_tokens;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::mt19937 m_rng;
    std::uint64_t m_next_stream = 1;
    bool m_stopping = false;
};

/**
 * @brief Имя игрока без пробелов по краям.
 * @throw RoomError 400, если пусто, длиннее MAX_NAME_LENGTH символов или с управляющими символами
 */
std::string normalize_name(const std::string &name);

} // namespace seabattle::server
