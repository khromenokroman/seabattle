#include "seabattle/game.hpp"

#include <stdexcept>

namespace seabattle {

Game::Game(int first_player) {
    if (first_player != 0 && first_player != 1) {
        throw std::invalid_argument("first_player должен быть 0 или 1");
    }
    m_turn = first_player;
}

PlaceResult Game::place(int player, std::vector<Ship> fleet) {
    PlaceResult result;
    if (player != 0 && player != 1) {
        result.error = MoveError::BAD_PLAYER;
        return result;
    }
    if (m_phase != Phase::PLACING) {
        result.error = MoveError::WRONG_PHASE;
        return result;
    }
    if (m_boards[player].has_value()) {
        result.error = MoveError::ALREADY_PLACED;
        return result;
    }
    FleetError fleet_error = validate_fleet(fleet);
    if (fleet_error != FleetError::NONE) {
        result.error = MoveError::INVALID_FLEET;
        result.fleet_error = fleet_error;
        return result;
    }
    m_boards[player] = Board(std::move(fleet));
    if (m_boards[0].has_value() && m_boards[1].has_value()) {
        m_phase = Phase::PLAYING;
    }
    return result;
}

FireResult Game::fire(int player, Coord at) {
    FireResult result;
    if (player != 0 && player != 1) {
        result.error = MoveError::BAD_PLAYER;
        return result;
    }
    if (m_phase != Phase::PLAYING) {
        result.error = MoveError::WRONG_PHASE;
        return result;
    }
    if (player != m_turn) {
        result.error = MoveError::NOT_YOUR_TURN;
        return result;
    }
    ShotOutcome shot = m_boards[1 - player]->shoot(at);
    if (shot.result == ShotResult::OUT_OF_BOUNDS) {
        result.error = MoveError::OUT_OF_BOUNDS;
        return result;
    }
    if (shot.result == ShotResult::ALREADY_SHOT) {
        result.error = MoveError::ALREADY_SHOT;
        return result;
    }
    result.shot = shot;
    m_history.push_back(ShotRecord{player, at, shot.result});
    if (shot.result == ShotResult::MISS) {
        m_turn = 1 - m_turn;
    }
    if (m_boards[1 - player]->all_sunk()) {
        m_phase = Phase::FINISHED;
        m_winner = player;
    }
    return result;
}

MoveError Game::resign(int player) {
    if (player != 0 && player != 1) {
        return MoveError::BAD_PLAYER;
    }
    if (m_phase == Phase::FINISHED) {
        return MoveError::WRONG_PHASE;
    }
    m_phase = Phase::FINISHED;
    m_winner = 1 - player;
    m_resigned = true;
    return MoveError::NONE;
}

bool Game::placed(int player) const {
    if (player != 0 && player != 1) {
        throw std::out_of_range("Игрок должен быть 0 или 1");
    }
    return m_boards[player].has_value();
}

const Board &Game::board(int player) const {
    if (player != 0 && player != 1) {
        throw std::out_of_range("Игрок должен быть 0 или 1");
    }
    if (!m_boards[player].has_value()) {
        throw std::logic_error("Игрок ещё не расставил флот");
    }
    return *m_boards[player];
}

const char *move_error_text(MoveError err) {
    switch (err) {
        case MoveError::NONE:
            return "";
        case MoveError::BAD_PLAYER:
            return "Неизвестный игрок";
        case MoveError::WRONG_PHASE:
            return "Действие нельзя выполнить в этой фазе";
        case MoveError::ALREADY_PLACED:
            return "Флот уже расставлен";
        case MoveError::INVALID_FLEET:
            return "Расстановка не по правилам";
        case MoveError::NOT_YOUR_TURN:
            return "Сейчас ход противника";
        case MoveError::OUT_OF_BOUNDS:
            return "Клетка вне поля";
        case MoveError::ALREADY_SHOT:
            return "По этой клетке уже стреляли";
    }
    return "";
}

} // namespace seabattle
