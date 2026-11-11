#pragma once

#include <array>
#include <optional>
#include <vector>

#include "seabattle/fleet.hpp"

/**
 * @file board.hpp
 * @brief Поле одного игрока: его корабли и выстрелы противника по ним.
 */

namespace seabattle {

/// Состояние клетки поля.
enum class Cell {
    WATER, ///< пусто, не обстреляна
    SHIP,  ///< палуба, не обстреляна
    MISS,  ///< обстреляна мимо или отмечена вокруг потопленного корабля
    HIT    ///< подбитая палуба (в том числе потопленного корабля)
};

/// Результат выстрела.
enum class ShotResult {
    MISS,         ///< мимо
    HIT,          ///< ранил
    SUNK,         ///< убил: подбита последняя палуба корабля
    ALREADY_SHOT, ///< клетка уже MISS или HIT, поле не меняется
    OUT_OF_BOUNDS ///< клетка вне поля, поле не меняется
};

/// Итог выстрела по полю.
struct ShotOutcome {
    ShotResult result = ShotResult::MISS;
    /// Потопленный корабль, только при SUNK.
    std::optional<Ship> sunk;
    /**
     * Только при SUNK: клетки вокруг корабля (8 соседей каждой палубы, в пределах поля), которые были WATER
     * и стали MISS. Без повторов, упорядочены по y, затем по x.
     */
    std::vector<Coord> marked;
};

/// Поле игрока. Создаётся только из правильной расстановки.
class Board {
   public:
    /**
     * @brief Поле с расстановкой.
     * @param fleet корабли, порядок сохраняется в ships()
     * @throw std::invalid_argument если validate_fleet(fleet) != NONE, what() = fleet_error_text
     */
    explicit Board(std::vector<Ship> fleet);

    /**
     * @brief Выстрел по клетке.
     *
     * WATER -> MISS (результат MISS), SHIP -> HIT (HIT или SUNK). При SUNK соседние клетки WATER становятся MISS
     * и перечисляются в marked.
     * @param c клетка
     * @return итог выстрела
     */
    ShotOutcome shoot(Coord c);

    /**
     * @brief Состояние клетки.
     * @param c клетка
     * @throw std::out_of_range если клетка вне поля
     */
    Cell cell(Coord c) const;

    /**
     * @brief Потоплен ли корабль.
     * @param index индекс в ships()
     * @throw std::out_of_range если индекс вне ships()
     */
    bool is_sunk(std::size_t index) const;

    /// Все корабли потоплены.
    bool all_sunk() const;

    /// Корабли в порядке расстановки.
    const std::vector<Ship> &ships() const { return m_ships; }

    /// Потопленные корабли в порядке ships().
    std::vector<Ship> sunk_ships() const;

   private:
    std::vector<Ship> m_ships;
    std::vector<int> m_hits;                                        ///< число подбитых палуб по индексу корабля
    std::array<std::array<Cell, BOARD_SIZE>, BOARD_SIZE> m_cells{}; ///< [y][x]
};

} // namespace seabattle
