#pragma once

#include <vector>

#include "seabattle/fleet.hpp"

/// Правильный флот для тестов: все корабли горизонтальные.
inline std::vector<seabattle::Ship> test_fleet() {
    using seabattle::Orientation;
    const int rows[][3] = {{0, 0, 4}, {5, 0, 3}, {0, 2, 3}, {4, 2, 2}, {7, 2, 2}, {0, 4, 2}, {3, 4, 1}, {5, 4, 1}, {7, 4, 1}, {9, 4, 1}};
    std::vector<seabattle::Ship> fleet;
    for (const auto &r : rows) fleet.push_back({{r[0], r[1]}, r[2], Orientation::HORIZONTAL});
    return fleet;
}

/// Все палубы флота test_fleet().
inline std::vector<seabattle::Coord> test_fleet_cells() {
    std::vector<seabattle::Coord> cells;
    for (const auto &ship : test_fleet()) {
        for (const auto &c : seabattle::ship_cells(ship)) cells.push_back(c);
    }
    return cells;
}
