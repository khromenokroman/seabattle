#include "seabattle/fleet.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace seabattle {

/// Порядок по y, затем по x: компаратор для std::set<Coord>.
struct CoordLess {
    bool operator()(const Coord &a, const Coord &b) const {
        if (a.y != b.y) {
            return a.y < b.y;
        }
        return a.x < b.x;
    }
};

bool in_bounds(Coord c) { return c.x >= 0 && c.x < BOARD_SIZE && c.y >= 0 && c.y < BOARD_SIZE; }

std::vector<Coord> ship_cells(const Ship &ship) {
    std::vector<Coord> cells;
    cells.reserve(ship.length);
    for (int i = 0; i < ship.length; ++i) {
        Coord c = ship.origin;
        if (ship.orientation == Orientation::HORIZONTAL) {
            c.x += i;
        } else {
            c.y += i;
        }
        cells.push_back(c);
    }
    return cells;
}

FleetError validate_fleet(const std::vector<Ship> &fleet) {
    // WRONG_COMPOSITION: длина вне 1..4 или набор длин не совпадает с FLEET.
    {
        std::vector<int> lengths;
        lengths.reserve(fleet.size());
        for (const Ship &ship : fleet) {
            if (ship.length < 1 || ship.length > 4) {
                return FleetError::WRONG_COMPOSITION;
            }
            lengths.push_back(ship.length);
        }
        std::sort(lengths.begin(), lengths.end());
        std::vector<int> expected(FLEET.begin(), FLEET.end());
        std::sort(expected.begin(), expected.end());
        if (lengths != expected) {
            return FleetError::WRONG_COMPOSITION;
        }
    }

    // OUT_OF_BOUNDS: клетка корабля за пределами поля.
    for (const Ship &ship : fleet) {
        for (const Coord &c : ship_cells(ship)) {
            if (!in_bounds(c)) {
                return FleetError::OUT_OF_BOUNDS;
            }
        }
    }

    // OVERLAP: два корабля занимают одну клетку.
    {
        std::set<Coord, CoordLess> occupied;
        for (const Ship &ship : fleet) {
            for (const Coord &c : ship_cells(ship)) {
                if (!occupied.insert(c).second) {
                    return FleetError::OVERLAP;
                }
            }
        }
    }

    // TOUCH: корабли касаются сторонами или углами.
    {
        const int DIRS[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
        std::vector<std::set<Coord, CoordLess>> ship_cells_sets(fleet.size());
        for (std::size_t i = 0; i < fleet.size(); ++i) {
            for (const Coord &c : ship_cells(fleet[i])) {
                ship_cells_sets[i].insert(c);
            }
        }
        for (std::size_t i = 0; i < fleet.size(); ++i) {
            for (const Coord &c : ship_cells(fleet[i])) {
                for (const auto &d : DIRS) {
                    Coord n{c.x + d[0], c.y + d[1]};
                    for (std::size_t j = 0; j < fleet.size(); ++j) {
                        if (j != i && ship_cells_sets[j].count(n) != 0) {
                            return FleetError::TOUCH;
                        }
                    }
                }
            }
        }
    }

    return FleetError::NONE;
}

const char *fleet_error_text(FleetError err) {
    switch (err) {
        case FleetError::NONE:
            return "";
        case FleetError::WRONG_COMPOSITION:
            return "Неверный состав флота";
        case FleetError::OUT_OF_BOUNDS:
            return "Корабль выходит за край поля";
        case FleetError::OVERLAP:
            return "Корабли пересекаются";
        case FleetError::TOUCH:
            return "Корабли не должны касаться друг друга";
    }
    return "";
}

namespace {

const int DIRS[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

/// Добавляет клетку и всех её соседей в набор клеток, занятых или запрещённых.
void forbid_around(std::set<Coord, CoordLess> &zone, Coord c) {
    zone.insert(c);
    for (const auto &d : DIRS) {
        zone.insert(Coord{c.x + d[0], c.y + d[1]});
    }
}

/// Пытается поставить корабль: в пределах поля, без пересечения и без касания зоны.
bool try_place(const Ship &ship, const std::set<Coord, CoordLess> &zone) {
    for (const Coord &c : ship_cells(ship)) {
        if (!in_bounds(c) || zone.count(c) != 0) {
            return false;
        }
    }
    return true;
}

/// Ставит корабли по порядку, начиная с index. zone - занятые и запрещённые клетки.
bool place_next(std::size_t index, std::vector<Ship> &fleet, std::set<Coord, CoordLess> &zone, std::mt19937 &rng) {
    if (index == FLEET.size()) {
        return true;
    }
    std::uniform_int_distribution<int> pos_dist(0, BOARD_SIZE - 1);
    std::uniform_int_distribution<int> dir_dist(0, 1);
    const int length = FLEET[index];
    for (int attempt = 0; attempt < 1000; ++attempt) {
        Ship ship;
        ship.origin = Coord{pos_dist(rng), pos_dist(rng)};
        ship.length = length;
        ship.orientation = dir_dist(rng) == 0 ? Orientation::HORIZONTAL : Orientation::VERTICAL;
        if (try_place(ship, zone)) {
            for (const Coord &c : ship_cells(ship)) {
                forbid_around(zone, c);
            }
            fleet.push_back(ship);
            return place_next(index + 1, fleet, zone, rng);
        }
    }
    return false;
}

} // namespace

std::vector<Ship> random_fleet(std::mt19937 &rng) {
    while (true) {
        std::vector<Ship> fleet;
        fleet.reserve(FLEET.size());
        std::set<Coord, CoordLess> zone;
        if (place_next(0, fleet, zone, rng)) {
            return fleet;
        }
    }
}

} // namespace seabattle
