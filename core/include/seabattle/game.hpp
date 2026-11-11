#pragma once

#include <array>
#include <optional>
#include <vector>

#include "seabattle/board.hpp"

/**
 * @file game.hpp
 * @brief Партия на двоих: расстановка, очередь ходов, конец игры. Без сети и без времени.
 */

namespace seabattle {

/// Фаза партии.
enum class Phase {
    PLACING, ///< игроки расставляют флот
    PLAYING, ///< стрельба
    FINISHED ///< есть победитель
};

/// Почему действие отклонено.
enum class MoveError {
    NONE,           ///< принято
    BAD_PLAYER,     ///< номер игрока не 0 и не 1
    WRONG_PHASE,    ///< действие не для текущей фазы
    ALREADY_PLACED, ///< игрок уже расставил флот
    INVALID_FLEET,  ///< расстановка не по правилам, подробности в PlaceResult::fleet_error
    NOT_YOUR_TURN,  ///< сейчас ход противника
    OUT_OF_BOUNDS,  ///< клетка вне поля
    ALREADY_SHOT    ///< по клетке уже стреляли (или она отмечена вокруг потопленного)
};

/// Итог расстановки.
struct PlaceResult {
    MoveError error = MoveError::NONE;
    FleetError fleet_error = FleetError::NONE; ///< только при INVALID_FLEET
};

/// Итог выстрела в партии.
struct FireResult {
    MoveError error = MoveError::NONE; ///< при ошибке партия не меняется, shot не заполнен
    ShotOutcome shot;                  ///< результат MISS, HIT или SUNK
};

/// Запись истории выстрелов.
struct ShotRecord {
    int player = 0; ///< кто стрелял
    Coord at;
    ShotResult result = ShotResult::MISS; ///< MISS, HIT или SUNK
};

/**
 * @brief Партия по правилам: попал - стреляешь ещё, промахнулся - ход переходит.
 *
 * Игроки 0 и 1. Поле игрока - его корабли; игрок стреляет по полю противника.
 */
class Game {
   public:
    /**
     * @brief Новая партия в фазе PLACING.
     * @param first_player кто стреляет первым (0 или 1)
     * @throw std::invalid_argument если first_player не 0 и не 1
     */
    explicit Game(int first_player = 0);

    /// Противник игрока: 1 - player.
    static int opponent(int player) { return 1 - player; }

    /**
     * @brief Расстановка флота игрока.
     *
     * Ошибки по порядку: BAD_PLAYER, WRONG_PHASE (фаза не PLACING), ALREADY_PLACED, INVALID_FLEET.
     * Когда расставили оба, фаза PLAYING, ход у first_player.
     * @param player игрок
     * @param fleet корабли
     */
    PlaceResult place(int player, std::vector<Ship> fleet);

    /**
     * @brief Выстрел игрока по полю противника.
     *
     * Ошибки по порядку: BAD_PLAYER, WRONG_PHASE (фаза не PLAYING), NOT_YOUR_TURN, OUT_OF_BOUNDS, ALREADY_SHOT.
     * MISS передаёт ход противнику, HIT и SUNK оставляют ход. Если потоплен последний корабль - фаза FINISHED,
     * победитель player. Принятый выстрел добавляется в history().
     * @param player игрок
     * @param at клетка поля противника
     */
    FireResult fire(int player, Coord at);

    /**
     * @brief Игрок сдаётся: фаза FINISHED, победитель противник.
     *
     * Ошибки: BAD_PLAYER, WRONG_PHASE (уже FINISHED). Сдаться можно и во время расстановки.
     */
    MoveError resign(int player);

    Phase phase() const { return m_phase; }

    /// Чей ход. Имеет смысл в PLAYING; в FINISHED - чей был последний ход.
    int turn() const { return m_turn; }

    /// Победитель, только в FINISHED.
    std::optional<int> winner() const { return m_winner; }

    /// Партия закончилась сдачей.
    bool resigned() const { return m_resigned; }

    /**
     * @brief Расставил ли игрок флот.
     * @throw std::out_of_range если player не 0 и не 1
     */
    bool placed(int player) const;

    /**
     * @brief Поле игрока (его корабли и выстрелы противника по ним).
     * @throw std::out_of_range если player не 0 и не 1
     * @throw std::logic_error если игрок ещё не расставил флот
     */
    const Board &board(int player) const;

    /// Принятые выстрелы в порядке хода.
    const std::vector<ShotRecord> &history() const { return m_history; }

   private:
    std::array<std::optional<Board>, 2> m_boards;
    std::vector<ShotRecord> m_history;
    std::optional<int> m_winner;
    Phase m_phase = Phase::PLACING;
    int m_turn = 0;
    bool m_resigned = false;
};

/**
 * @brief Текст ошибки действия для игрока.
 * @return строка на русском, для NONE - пустая
 */
const char *move_error_text(MoveError err);

} // namespace seabattle
