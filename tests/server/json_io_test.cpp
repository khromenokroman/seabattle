#include "json_io.hpp"

#include <gtest/gtest.h>

using namespace seabattle;
using namespace seabattle::server;
using nlohmann::json;

// Корабль переводится в JSON и обратно без потерь
TEST(JsonIo, ShipRoundTrip) {
    for (const auto dir : {Orientation::HORIZONTAL, Orientation::VERTICAL}) {
        const Ship ship{{3, 5}, 3, dir};
        const auto j = ship_to_json(ship);
        SCOPED_TRACE("json: " + j.dump());
        EXPECT_EQ(j["dir"], dir == Orientation::HORIZONTAL ? "h" : "v");
        EXPECT_EQ(ship_from_json(j), ship) << "после обратного разбора корабль другой";
    }
}

// Без dir корабль горизонтальный (для однопалубных клиент может не передавать)
TEST(JsonIo, DefaultDirection) {
    const auto ship = ship_from_json(json{{"x", 1}, {"y", 2}, {"len", 1}});
    EXPECT_EQ(ship.orientation, Orientation::HORIZONTAL);
}

// Неправильные корабли и флот отклоняются с invalid_argument
TEST(JsonIo, Rejects) {
    const std::vector<json> ships = {json::array(), json{{"y", 1}, {"len", 1}}, json{{"x", "1"}, {"y", 1}, {"len", 1}},
                                     json{{"x", 1.5}, {"y", 1}, {"len", 1}}, json{{"x", 1}, {"y", 1}, {"len", 2}, {"dir", "d"}}};
    for (const auto &j : ships) {
        SCOPED_TRACE("корабль: " + j.dump());
        EXPECT_THROW(ship_from_json(j), std::invalid_argument);
    }
    EXPECT_THROW(fleet_from_json(json::object()), std::invalid_argument) << "флот не массив";
    EXPECT_THROW(fleet_from_json(json::array({1})), std::invalid_argument) << "элемент флота не объект";
}

// Имена результатов выстрела по протоколу
TEST(JsonIo, ShotResultNames) {
    EXPECT_STREQ(shot_result_name(ShotResult::MISS), "miss");
    EXPECT_STREQ(shot_result_name(ShotResult::HIT), "hit");
    EXPECT_STREQ(shot_result_name(ShotResult::SUNK), "sunk");
    EXPECT_STREQ(shot_result_name(ShotResult::ALREADY_SHOT), "");
}

// Числа вне int отклоняются, а не заворачиваются (4294967299 у get<int>() стало бы 3)
TEST(JsonIo, IntRange) {
    EXPECT_EQ(get_int_field(json{{"x", -5}}, "x"), -5);
    EXPECT_EQ(get_int_field(json{{"x", 2147483647}}, "x"), 2147483647);
    const std::vector<json> bad = {json::parse(R"({"x":4294967299})"), json::parse(R"({"x":-2147483649})"),
                                   json::parse(R"({"x":18446744073709551615})"), json{{"x", 1.0}}, json{{"y", 1}}};
    for (const auto &j : bad) {
        SCOPED_TRACE("json: " + j.dump());
        EXPECT_THROW(get_int_field(j, "x"), std::invalid_argument);
    }
    EXPECT_THROW(ship_from_json(json::parse(R"({"x":4294967299,"y":0,"len":1})")), std::invalid_argument) << "x корабля вне int";
}
