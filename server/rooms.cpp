#include "rooms.hpp"

#include <algorithm>
#include <cctype>

#include "json_io.hpp"

namespace seabattle::server {

namespace {

/// Буквы и цифры кода комнаты без похожих I, O, 0, 1.
constexpr std::string_view CODE_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

int status_for(MoveError err) {
    switch (err) {
        case MoveError::INVALID_FLEET:
        case MoveError::OUT_OF_BOUNDS:
        case MoveError::BAD_PLAYER:
            return 400;
        default:
            return 409;
    }
}

[[noreturn]] void throw_move_error(MoveError err, FleetError fleet_err = FleetError::NONE) {
    if (err == MoveError::INVALID_FLEET) throw RoomError(400, fleet_error_text(fleet_err));
    throw RoomError(status_for(err), move_error_text(err));
}

nlohmann::json marked_cells(const Board &board) {
    auto shots = nlohmann::json::array();
    for (int y = 0; y < BOARD_SIZE; ++y) {
        for (int x = 0; x < BOARD_SIZE; ++x) {
            const auto c = board.cell({x, y});
            if (c == Cell::MISS || c == Cell::HIT) shots.push_back({{"x", x}, {"y", y}, {"hit", c == Cell::HIT}});
        }
    }
    return shots;
}

} // namespace

std::string normalize_name(const std::string &name) {
    const auto first = name.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) throw RoomError(400, "Введите имя");
    const auto last = name.find_last_not_of(" \t\r\n");
    auto result = name.substr(first, last - first + 1);
    std::size_t symbols = 0;
    for (unsigned char ch : result) {
        if (ch < 0x20 || ch == 0x7f) throw RoomError(400, "Недопустимые символы в имени");
        if ((ch & 0xC0) != 0x80) ++symbols;
    }
    if (symbols > RoomManager::MAX_NAME_LENGTH) {
        throw RoomError(400, "Имя длиннее " + std::to_string(RoomManager::MAX_NAME_LENGTH) + " символов");
    }
    return result;
}

RoomManager::RoomManager(std::uint32_t seed) : m_rng(seed) {}

std::string RoomManager::new_code() {
    std::uniform_int_distribution<std::size_t> pick(0, CODE_ALPHABET.size() - 1);
    for (;;) {
        std::string code;
        for (std::size_t i = 0; i < CODE_LENGTH; ++i) code += CODE_ALPHABET[pick(m_rng)];
        if (!m_rooms.contains(code)) return code;
    }
}

std::string RoomManager::new_token() {
    static constexpr char HEX[] = "0123456789abcdef";
    std::random_device rd;
    std::string token;
    for (int i = 0; i < 8; ++i) {
        auto v = rd();
        for (int k = 0; k < 4; ++k) {
            token += HEX[v & 0xF];
            v >>= 4;
        }
    }
    return token;
}

void RoomManager::changed(Room &room) {
    ++room.version;
    m_cv.notify_all();
}

void RoomManager::new_game(Room &room, int first_player) {
    room.game = Game(first_player);
    for (auto &p : room.players) {
        if (p) p->rematch = false;
    }
}

RoomManager::Seat RoomManager::seat_locked(const std::string &token) const {
    const auto it = m_tokens.find(token);
    if (it == m_tokens.end()) throw RoomError(401, "Сессия не найдена, войдите в игру заново");
    return it->second;
}

Joined RoomManager::create_room(const std::string &name) {
    const auto clean = normalize_name(name);
    std::lock_guard lock(m_mutex);
    const auto code = new_code();
    auto &room = m_rooms[code];
    room.code = code;
    room.game = Game(static_cast<int>(m_rng() % 2));
    room.idle_since = Clock::now();
    const auto token = new_token();
    room.players[0] = Player{clean, token};
    m_tokens[token] = Seat{&room, 0};
    return {code, token, 0};
}

Joined RoomManager::join_room(const std::string &code, const std::string &name) {
    const auto clean = normalize_name(name);
    auto upper = code;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return std::toupper(c); });
    std::lock_guard lock(m_mutex);
    const auto it = m_rooms.find(upper);
    if (it == m_rooms.end()) throw RoomError(404, "Комната " + upper + " не найдена");
    auto &room = it->second;
    const int player = !room.players[0] ? 0 : !room.players[1] ? 1 : -1;
    if (player < 0) throw RoomError(409, "В комнате уже два игрока");
    const auto token = new_token();
    room.players[player] = Player{clean, token};
    m_tokens[token] = Seat{&room, player};
    changed(room);
    return {upper, token, player};
}

TokenInfo RoomManager::me(const std::string &token) const {
    std::lock_guard lock(m_mutex);
    const auto s = seat_locked(token);
    return {s.room->code, s.room->players[s.player]->name, s.player};
}

void RoomManager::place(const std::string &token, std::vector<Ship> fleet) {
    std::lock_guard lock(m_mutex);
    const auto s = seat_locked(token);
    if (!s.room->players[Game::opponent(s.player)]) throw RoomError(409, "Ждём второго игрока");
    const auto r = s.room->game.place(s.player, std::move(fleet));
    if (r.error != MoveError::NONE) throw_move_error(r.error, r.fleet_error);
    changed(*s.room);
}

ShotResult RoomManager::fire(const std::string &token, Coord at) {
    std::lock_guard lock(m_mutex);
    const auto s = seat_locked(token);
    const auto r = s.room->game.fire(s.player, at);
    if (r.error != MoveError::NONE) throw_move_error(r.error);
    changed(*s.room);
    return r.shot.result;
}

void RoomManager::resign(const std::string &token) {
    std::lock_guard lock(m_mutex);
    const auto s = seat_locked(token);
    if (!s.room->players[Game::opponent(s.player)]) throw RoomError(409, "Партия ещё не началась");
    const auto err = s.room->game.resign(s.player);
    if (err != MoveError::NONE) throw_move_error(err);
    changed(*s.room);
}

void RoomManager::rematch(const std::string &token) {
    std::lock_guard lock(m_mutex);
    const auto s = seat_locked(token);
    auto &room = *s.room;
    if (room.game.phase() != Phase::FINISHED) throw RoomError(409, "Партия ещё не закончилась");
    auto &opp = room.players[Game::opponent(s.player)];
    if (!opp) throw RoomError(409, "Противник вышел из комнаты");
    room.players[s.player]->rematch = true;
    if (opp->rematch) new_game(room, Game::opponent(*room.game.winner()));
    changed(room);
}

void RoomManager::leave(const std::string &token) {
    std::lock_guard lock(m_mutex);
    const auto s = seat_locked(token);
    auto &room = *s.room;
    m_tokens.erase(token);
    room.players[s.player].reset();
    if (!room.players[0] && !room.players[1]) {
        const auto code = room.code;
        m_rooms.erase(code);
        m_cv.notify_all();
        return;
    }
    new_game(room, static_cast<int>(m_rng() % 2));
    const auto &rest = room.players[Game::opponent(s.player)];
    if (rest->stream == 0) room.idle_since = Clock::now();
    changed(room);
}

nlohmann::json RoomManager::snapshot(const std::string &token) const {
    std::lock_guard lock(m_mutex);
    const auto s = seat_locked(token);
    return snapshot_locked(*s.room, s.player);
}

nlohmann::json RoomManager::snapshot_locked(const Room &room, int player) const {
    const auto &game = room.game;
    const int other = Game::opponent(player);
    const auto &me = *room.players[player];
    const auto &opp = room.players[other];
    const auto who = [player](int p) { return p == player ? "you" : "opponent"; };

    nlohmann::json j;
    j["room"] = room.code;
    j["you"] = {{"name", me.name}, {"player", player}};
    j["opponent"] = opp ? nlohmann::json{{"name", opp->name}, {"connected", opp->stream != 0}} : nlohmann::json();

    const auto phase = game.phase();
    if (!opp) {
        j["phase"] = "waiting";
    } else {
        j["phase"] = phase == Phase::PLACING ? "placing" : phase == Phase::PLAYING ? "playing" : "finished";
    }
    j["placed"] = {{"you", game.placed(player)}, {"opponent", opp.has_value() && game.placed(other)}};
    j["turn"] = opp && phase == Phase::PLAYING ? nlohmann::json(who(game.turn())) : nlohmann::json();
    const bool finished = opp && phase == Phase::FINISHED;
    j["winner"] = finished ? nlohmann::json(who(*game.winner())) : nlohmann::json();
    j["resigned"] = finished && game.resigned();
    j["rematch"] = {{"you", finished && me.rematch}, {"opponent", finished && opp->rematch}};
    j["fleet"] = FLEET;

    auto own_ships = nlohmann::json::array();
    auto own_shots = nlohmann::json::array();
    if (game.placed(player)) {
        const auto &board = game.board(player);
        for (std::size_t i = 0; i < board.ships().size(); ++i) {
            auto ship = ship_to_json(board.ships()[i]);
            ship["sunk"] = board.is_sunk(i);
            own_ships.push_back(std::move(ship));
        }
        own_shots = marked_cells(board);
    }
    j["own"] = {{"ships", own_ships}, {"shots", own_shots}};

    auto enemy_shots = nlohmann::json::array();
    auto enemy_sunk = nlohmann::json::array();
    auto enemy_ships = nlohmann::json::array();
    if (opp && game.placed(other)) {
        const auto &board = game.board(other);
        enemy_shots = marked_cells(board);
        for (const auto &ship : board.sunk_ships()) enemy_sunk.push_back(ship_to_json(ship));
        if (finished) {
            for (const auto &ship : board.ships()) enemy_ships.push_back(ship_to_json(ship));
        }
    }
    j["enemy"] = {{"shots", enemy_shots}, {"sunk", enemy_sunk}, {"ships", enemy_ships}};

    if (!opp || game.history().empty()) {
        j["last_shot"] = nullptr;
    } else {
        const auto &last = game.history().back();
        j["last_shot"] = {{"by", who(last.player)}, {"x", last.at.x}, {"y", last.at.y}, {"result", shot_result_name(last.result)}};
    }
    return j;
}

std::uint64_t RoomManager::open_stream(const std::string &token) {
    std::lock_guard lock(m_mutex);
    const auto s = seat_locked(token);
    const auto id = m_next_stream++;
    s.room->players[s.player]->stream = id;
    changed(*s.room);
    return id;
}

void RoomManager::close_stream(const std::string &token, std::uint64_t stream) {
    std::lock_guard lock(m_mutex);
    const auto it = m_tokens.find(token);
    if (it == m_tokens.end()) return;
    auto &room = *it->second.room;
    auto &player = *room.players[it->second.player];
    if (player.stream != stream) return;
    player.stream = 0;
    const bool online = std::any_of(room.players.begin(), room.players.end(), [](const auto &p) { return p && p->stream != 0; });
    if (!online) room.idle_since = Clock::now();
    changed(room);
}

StreamEvent RoomManager::wait_event(const std::string &token, std::uint64_t stream, std::uint64_t &seen, nlohmann::json &out,
                                    std::chrono::milliseconds timeout) {
    std::unique_lock lock(m_mutex);
    const auto check = [&]() -> std::optional<StreamEvent> {
        if (m_stopping) return StreamEvent::CLOSED;
        const auto it = m_tokens.find(token);
        if (it == m_tokens.end()) return StreamEvent::CLOSED;
        const auto &room = *it->second.room;
        if (room.players[it->second.player]->stream != stream) return StreamEvent::CLOSED;
        if (room.version != seen) return StreamEvent::STATE;
        return std::nullopt;
    };
    m_cv.wait_for(lock, timeout, [&] { return check().has_value(); });
    const auto event = check();
    if (!event) return StreamEvent::PING;
    if (*event == StreamEvent::STATE) {
        const auto &s = m_tokens.at(token);
        out = snapshot_locked(*s.room, s.player);
        seen = s.room->version;
    }
    return *event;
}

std::size_t RoomManager::cleanup(Clock::time_point now, std::chrono::minutes idle) {
    std::lock_guard lock(m_mutex);
    std::size_t removed = 0;
    for (auto it = m_rooms.begin(); it != m_rooms.end();) {
        auto &room = it->second;
        const bool online = std::any_of(room.players.begin(), room.players.end(), [](const auto &p) { return p && p->stream != 0; });
        if (online || now - room.idle_since < idle) {
            ++it;
            continue;
        }
        for (const auto &p : room.players) {
            if (p) m_tokens.erase(p->token);
        }
        it = m_rooms.erase(it);
        ++removed;
    }
    if (removed) m_cv.notify_all();
    return removed;
}

void RoomManager::shutdown() {
    std::lock_guard lock(m_mutex);
    m_stopping = true;
    m_cv.notify_all();
}

std::size_t RoomManager::room_count() const {
    std::lock_guard lock(m_mutex);
    return m_rooms.size();
}

} // namespace seabattle::server
