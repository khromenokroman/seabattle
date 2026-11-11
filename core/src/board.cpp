#include "seabattle/board.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <vector>

namespace seabattle {

namespace {

/// Компаратор для std::set<Coord>: по y, затем по x.
struct CoordLess {
    bool operator()(const Coord &a, const Coord &b) const {
        if (a.y != b.y) {
            return a.y < b.y;
        }
        return a.x < b.x;
    }
};

const int DIRS[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

/// Индекс корабля, занимающего клетку, или -1.
int ship_at(const std::vector<Ship> &ships, Coord c) {
    for (std::size_t i = 0; i < ships.size(); ++i) {
        const std::vector<Coord> cells = ship_cells(ships[i]);
        if (std::find(cells.begin(), cells.end(), c) != cells.end()) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace

Board::Board(std::vector<Ship> fleet) {
    FleetError err = validate_fleet(fleet);
    if (err != FleetError::NONE) {
        throw std::invalid_argument(fleet_error_text(err));
    }
    m_ships = std::move(fleet);
    m_hits.assign(m_ships.size(), 0);
    for (const Ship &ship : m_ships) {
        for (const Coord &c : ship_cells(ship)) {
            m_cells[c.y][c.x] = Cell::SHIP;
        }
    }
}

ShotOutcome Board::shoot(Coord c) {
    ShotOutcome outcome;
    if (!in_bounds(c)) {
        outcome.result = ShotResult::OUT_OF_BOUNDS;
        return outcome;
    }
    Cell state = m_cells[c.y][c.x];
    if (state == Cell::MISS || state == Cell::HIT) {
        outcome.result = ShotResult::ALREADY_SHOT;
        return outcome;
    }
    if (state == Cell::WATER) {
        m_cells[c.y][c.x] = Cell::MISS;
        outcome.result = ShotResult::MISS;
        return outcome;
    }
    // state == Cell::SHIP
    int index = ship_at(m_ships, c);
    m_cells[c.y][c.x] = Cell::HIT;
    ++m_hits[index];
    if (m_hits[index] < m_ships[static_cast<std::size_t>(index)].length) {
        outcome.result = ShotResult::HIT;
        return outcome;
    }
    // Последняя палуба подбита: корабль потоплен.
    outcome.result = ShotResult::SUNK;
    outcome.sunk = m_ships[static_cast<std::size_t>(index)];
    std::vector<Coord> candidates;
    for (const Coord &deck : ship_cells(outcome.sunk.value())) {
        for (const auto &d : DIRS) {
            Coord n{deck.x + d[0], deck.y + d[1]};
            if (in_bounds(n) && m_cells[n.y][n.x] == Cell::WATER) {
                candidates.push_back(n);
            }
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Coord &a, const Coord &b) {
        if (a.y != b.y) {
            return a.y < b.y;
        }
        return a.x < b.x;
    });
    std::set<Coord, CoordLess> seen;
    for (const Coord &n : candidates) {
        if (seen.insert(n).second) {
            m_cells[n.y][n.x] = Cell::MISS;
            outcome.marked.push_back(n);
        }
    }
    return outcome;
}

Cell Board::cell(Coord c) const {
    if (!in_bounds(c)) {
        throw std::out_of_range("Клетка вне поля");
    }
    return m_cells[c.y][c.x];
}

bool Board::is_sunk(std::size_t index) const {
    if (index >= m_ships.size()) {
        throw std::out_of_range("Индекс вне ships()");
    }
    return m_hits[index] == m_ships[index].length;
}

bool Board::all_sunk() const {
    for (std::size_t i = 0; i < m_ships.size(); ++i) {
        if (m_hits[i] < m_ships[i].length) {
            return false;
        }
    }
    return true;
}

std::vector<Ship> Board::sunk_ships() const {
    std::vector<Ship> result;
    for (std::size_t i = 0; i < m_ships.size(); ++i) {
        if (m_hits[i] == m_ships[i].length) {
            result.push_back(m_ships[i]);
        }
    }
    return result;
}

} // namespace seabattle
