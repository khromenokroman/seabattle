#pragma once

#include <array>
#include <random>
#include <vector>

/**
 * @file fleet.hpp
 * @brief Координаты, корабли и правила расстановки флота.
 */

namespace seabattle {

/// Размер поля по каждой стороне.
constexpr int BOARD_SIZE = 10;

/// Длины кораблей стандартного флота: один 4-палубный, два 3-палубных, три 2-палубных, четыре 1-палубных.
constexpr std::array<int, 10> FLEET = {4, 3, 3, 2, 2, 2, 1, 1, 1, 1};

/// Клетка поля: x - столбец 0..9 (буквы А..К), y - строка 0..9 (цифры 1..10).
struct Coord {
    int x = 0;
    int y = 0;
    bool operator==(const Coord &) const = default;
};

/// Направление корабля от начальной клетки: вправо (x растёт) или вниз (y растёт).
enum class Orientation { HORIZONTAL, VERTICAL };

/// Корабль: начальная клетка (левая для HORIZONTAL, верхняя для VERTICAL), длина и направление.
struct Ship {
    Coord origin;
    int length = 1;
    Orientation orientation = Orientation::HORIZONTAL;
    bool operator==(const Ship &) const = default;
};

/// Причина, по которой расстановка флота не принимается.
enum class FleetError {
    NONE,              ///< расстановка правильная
    WRONG_COMPOSITION, ///< набор длин не совпадает с FLEET (в любом порядке) или длина вне 1..4
    OUT_OF_BOUNDS,     ///< клетка корабля за пределами поля
    OVERLAP,           ///< два корабля занимают одну клетку
    TOUCH              ///< корабли касаются сторонами или углами
};

/**
 * @brief Клетка внутри поля.
 * @param c клетка
 * @return true, если 0 <= x < BOARD_SIZE и 0 <= y < BOARD_SIZE
 */
bool in_bounds(Coord c);

/**
 * @brief Клетки корабля от начальной по направлению.
 * @param ship корабль, границы поля не проверяются
 * @return ship.length клеток, первая - ship.origin
 */
std::vector<Coord> ship_cells(const Ship &ship);

/**
 * @brief Проверка расстановки по правилам.
 *
 * Проверки идут по порядку, возвращается первая сработавшая: WRONG_COMPOSITION, OUT_OF_BOUNDS,
 * OVERLAP, TOUCH. Для однопалубного корабля направление не важно.
 * @param fleet корабли в любом порядке
 * @return FleetError::NONE, если расстановка правильная
 */
FleetError validate_fleet(const std::vector<Ship> &fleet);

/**
 * @brief Текст ошибки расстановки для игрока.
 * @param err ошибка
 * @return строка на русском, для NONE - пустая
 */
const char *fleet_error_text(FleetError err);

/**
 * @brief Случайная правильная расстановка флота FLEET.
 *
 * Результат всегда проходит validate_fleet. Корабли в порядке FLEET (сначала 4-палубный).
 * При одном и том же состоянии rng результат одинаковый.
 * @param rng генератор
 * @return 10 кораблей
 */
std::vector<Ship> random_fleet(std::mt19937 &rng);

} // namespace seabattle
