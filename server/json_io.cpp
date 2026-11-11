#include "json_io.hpp"

#include <stdexcept>

namespace seabattle::server {

nlohmann::json ship_to_json(const Ship &ship) {
    return {{"x", ship.origin.x}, {"y", ship.origin.y}, {"len", ship.length}, {"dir", ship.orientation == Orientation::HORIZONTAL ? "h" : "v"}};
}

Ship ship_from_json(const nlohmann::json &j) {
    if (!j.is_object()) throw std::invalid_argument("Корабль должен быть объектом");
    for (const char *key : {"x", "y", "len"}) {
        if (!j.contains(key) || !j[key].is_number_integer()) {
            throw std::invalid_argument(std::string("У корабля нет целого поля ") + key);
        }
    }
    Ship ship;
    ship.origin = {j["x"].get<int>(), j["y"].get<int>()};
    ship.length = j["len"].get<int>();
    const auto dir = j.value("dir", std::string("h"));
    if (dir == "h") {
        ship.orientation = Orientation::HORIZONTAL;
    } else if (dir == "v") {
        ship.orientation = Orientation::VERTICAL;
    } else {
        throw std::invalid_argument("Направление корабля должно быть h или v");
    }
    return ship;
}

std::vector<Ship> fleet_from_json(const nlohmann::json &j) {
    if (!j.is_array()) throw std::invalid_argument("Корабли должны быть массивом");
    std::vector<Ship> fleet;
    fleet.reserve(j.size());
    for (const auto &item : j) fleet.push_back(ship_from_json(item));
    return fleet;
}

const char *shot_result_name(ShotResult result) {
    switch (result) {
        case ShotResult::MISS:
            return "miss";
        case ShotResult::HIT:
            return "hit";
        case ShotResult::SUNK:
            return "sunk";
        default:
            return "";
    }
}

} // namespace seabattle::server
