#pragma once

#include <nlohmann/json.hpp>
#include <vector>

#include "seabattle/board.hpp"

/**
 * @file json_io.hpp
 * @brief Корабли и результаты выстрелов в JSON по docs/protocol.md.
 */

namespace seabattle::server {

/**
 * @brief Целое поле объекта в пределах int.
 *
 * nlohmann::json::get<int>() не проверяет диапазон: 4294967299 молча становится 3, поэтому значение
 * читается как 64-битное и сверяется с границами int.
 * @throw std::invalid_argument если поля нет, оно не целое или не помещается в int
 */
int get_int_field(const nlohmann::json &j, const char *key);

/**
 * @brief Корабль в JSON: {"x", "y", "len", "dir": "h" | "v"}.
 */
nlohmann::json ship_to_json(const Ship &ship);

/**
 * @brief Корабль из JSON.
 * @throw std::invalid_argument текст для игрока, если нет полей или неверные типы
 */
Ship ship_from_json(const nlohmann::json &j);

/**
 * @brief Флот из JSON-массива кораблей. Правила расстановки не проверяются.
 * @throw std::invalid_argument если не массив или корабль неправильный
 */
std::vector<Ship> fleet_from_json(const nlohmann::json &j);

/**
 * @brief Имя результата выстрела: "miss", "hit", "sunk".
 * @return для ALREADY_SHOT и OUT_OF_BOUNDS пустая строка
 */
const char *shot_result_name(ShotResult result);

} // namespace seabattle::server
