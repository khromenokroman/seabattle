#include "json_io.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace seabattle::server {

nlohmann::json ship_to_json(const Ship &ship) {
    return {{"x", ship.origin.x}, {"y", ship.origin.y}, {"len", ship.length}, {"dir", ship.orientation == Orientation::HORIZONTAL ? "h" : "v"}};
}

int get_int_field(const nlohmann::json &j, const char *key) {
    if (!j.is_object() || !j.contains(key) || !j[key].is_number_integer()) {
        throw std::invalid_argument(std::string("Нет целого поля ") + key);
    }
    const auto &v = j[key];
    const bool fits = v.is_number_unsigned()
                          ? v.get<std::uint64_t>() <= static_cast<std::uint64_t>(std::numeric_limits<int>::max())
                          : v.get<std::int64_t>() >= std::numeric_limits<int>::min() && v.get<std::int64_t>() <= std::numeric_limits<int>::max();
    if (!fits) throw std::invalid_argument(std::string("Поле ") + key + " вне допустимого диапазона");
    return static_cast<int>(v.get<std::int64_t>());
}

Ship ship_from_json(const nlohmann::json &j) {
    if (!j.is_object()) throw std::invalid_argument("Корабль должен быть объектом");
    Ship ship;
    ship.origin = {get_int_field(j, "x"), get_int_field(j, "y")};
    ship.length = get_int_field(j, "len");
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
