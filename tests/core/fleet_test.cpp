#include "seabattle/fleet.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <numeric>
#include <set>

namespace {

using namespace seabattle;

/// Правильный флот из задания: все корабли горизонтальные.
std::vector<Ship> valid_fleet() {
    return {
        Ship{Coord{0, 0}, 4, Orientation::HORIZONTAL}, Ship{Coord{5, 0}, 3, Orientation::HORIZONTAL}, Ship{Coord{0, 2}, 3, Orientation::HORIZONTAL},
        Ship{Coord{4, 2}, 2, Orientation::HORIZONTAL}, Ship{Coord{7, 2}, 2, Orientation::HORIZONTAL}, Ship{Coord{0, 4}, 2, Orientation::HORIZONTAL},
        Ship{Coord{3, 4}, 1, Orientation::HORIZONTAL}, Ship{Coord{5, 4}, 1, Orientation::HORIZONTAL}, Ship{Coord{7, 4}, 1, Orientation::HORIZONTAL},
        Ship{Coord{9, 4}, 1, Orientation::HORIZONTAL},
    };
}

/// Флот из задания с последним кораблём в углу (9,9).
std::vector<Ship> corner_fleet() {
    std::vector<Ship> fleet = valid_fleet();
    fleet.back() = Ship{Coord{9, 9}, 1, Orientation::HORIZONTAL};
    return fleet;
}

} // namespace

TEST(Fleet, ValidFleetIsNone) // правильный флот из задания проходит все проверки
{
    std::vector<Ship> fleet = valid_fleet();
    FleetError err = validate_fleet(fleet);
    EXPECT_EQ(err, FleetError::NONE) << "флот из задания: ждали NONE, получено " << static_cast<int>(err);
    EXPECT_EQ(validate_fleet(corner_fleet()), FleetError::NONE) << "флот с однопалубным в углу (9,9): ждали NONE";
}

TEST(Fleet, WrongComposition) // неверный состав флота
{
    {
        auto fleet = valid_fleet();
        fleet.pop_back(); // 9 кораблей
        EXPECT_EQ(validate_fleet(fleet), FleetError::WRONG_COMPOSITION) << "9 кораблей: ждали WRONG_COMPOSITION";
    }
    {
        auto fleet = valid_fleet();
        fleet.push_back(Ship{Coord{0, 9}, 1, Orientation::HORIZONTAL}); // 11 кораблей
        EXPECT_EQ(validate_fleet(fleet), FleetError::WRONG_COMPOSITION) << "11 кораблей: ждали WRONG_COMPOSITION";
    }
    {
        auto fleet = valid_fleet();
        fleet[0].length = 5; // корабль длины 5
        fleet[1].length = 3;
        fleet[2].length = 4;
        EXPECT_EQ(validate_fleet(fleet), FleetError::WRONG_COMPOSITION) << "корабль длины 5: ждали WRONG_COMPOSITION";
    }
    {
        auto fleet = valid_fleet();
        fleet[0].length = 4; // уже 4
        fleet[5].length = 4; // второй 4-палубный вместо 2-палубного
        fleet[6].length = 2; // 2 вместо 1, чтобы суммы длин не совпали явно
        EXPECT_EQ(validate_fleet(fleet), FleetError::WRONG_COMPOSITION) << "два 4-палубных: ждали WRONG_COMPOSITION";
    }
}

TEST(Fleet, OutOfBounds) // корабль за пределами поля
{
    {
        auto fleet = valid_fleet();
        fleet[0] = Ship{Coord{7, 0}, 4, Orientation::HORIZONTAL}; // x=7, len 4: клетки 7..10
        EXPECT_EQ(validate_fleet(fleet), FleetError::OUT_OF_BOUNDS) << "горизонтальный len 4 с x=7: ждали OUT_OF_BOUNDS";
    }
    {
        auto fleet = valid_fleet();
        fleet[0] = Ship{Coord{0, 7}, 4, Orientation::VERTICAL}; // len 4: клетки (0,7)..(0,10): (0,10) за краем
        EXPECT_EQ(validate_fleet(fleet), FleetError::OUT_OF_BOUNDS) << "вертикальный len 4 с y=7: ждали OUT_OF_BOUNDS";
    }
    {
        auto fleet = valid_fleet();
        fleet[6] = Ship{Coord{-1, 4}, 1, Orientation::HORIZONTAL};
        EXPECT_EQ(validate_fleet(fleet), FleetError::OUT_OF_BOUNDS) << "корабль с x=-1: ждали OUT_OF_BOUNDS";
    }
}

TEST(Fleet, Overlap) // два корабля занимают общую клетку
{
    auto fleet = valid_fleet();
    fleet[1] = Ship{Coord{1, 0}, 3, Orientation::HORIZONTAL}; // перекрывает 4-палубный на (1,0)..(3,0)
    EXPECT_EQ(validate_fleet(fleet), FleetError::OVERLAP) << "корабли перекрываются на (1,0): ждали OVERLAP";
}

TEST(Fleet, TouchSide) // корабли касаются сторонами
{
    auto fleet = valid_fleet();
    fleet[1] = Ship{Coord{4, 0}, 3, Orientation::HORIZONTAL}; // клетки (4,0)..(6,0), (3,0) палуба 4-палубного
    EXPECT_EQ(validate_fleet(fleet), FleetError::TOUCH) << "касание стороной (3,0)-(4,0): ждали TOUCH";
}

TEST(Fleet, TouchCorner) // корабли касаются углами
{
    auto fleet = valid_fleet();
    fleet[1] = Ship{Coord{5, 1}, 3, Orientation::HORIZONTAL}; // (5,1) диагонально к (4,0)
    EXPECT_EQ(validate_fleet(fleet), FleetError::TOUCH) << "касание углом (4,0)-(5,1): ждали TOUCH";
}

TEST(Fleet, CheckOrder) // порядок проверок: OUT_OF_BOUNDS раньше OVERLAP
{
    auto fleet = valid_fleet();
    // (1,0) len 4 H: клетки 1..4 пересекаются с 4-палубным (0..3).
    // (8,8) len 3 H: клетки 8,9,10 выходят за край. OUT_OF_BOUNDS раньше OVERLAP.
    fleet[0] = Ship{Coord{1, 0}, 4, Orientation::HORIZONTAL}; // len 4: клетки 1..4 пересекаются с 4-палубным
    fleet[2] = Ship{Coord{8, 8}, 3, Orientation::HORIZONTAL}; // len 3: клетки 8,9,10 выходят за край
    EXPECT_EQ(validate_fleet(fleet), FleetError::OUT_OF_BOUNDS) << "флот и пересечается, и выходит за край: ждали OUT_OF_BOUNDS";
}

TEST(Fleet, ShipCellsHorizontal) // ship_cells для горизонтального корабля
{
    std::vector<Coord> cells = ship_cells(Ship{Coord{2, 3}, 3, Orientation::HORIZONTAL});
    std::vector<Coord> expected = {Coord{2, 3}, Coord{3, 3}, Coord{4, 3}};
    EXPECT_EQ(cells, expected) << "корабль (2,3) len 3 HORIZONTAL: ждали " << expected.size() << " клеток от начала по x";
}

TEST(Fleet, ShipCellsVertical) // ship_cells для вертикального корабля
{
    std::vector<Coord> cells = ship_cells(Ship{Coord{2, 3}, 3, Orientation::VERTICAL});
    std::vector<Coord> expected = {Coord{2, 3}, Coord{2, 4}, Coord{2, 5}};
    EXPECT_EQ(cells, expected) << "корабль (2,3) len 3 VERTICAL: ждали клетки по y";
}

TEST(Fleet, InBounds) // проверка границ поля
{
    EXPECT_TRUE(in_bounds(Coord{0, 0})) << "клетка (0,0): ждали в пределах";
    EXPECT_TRUE(in_bounds(Coord{9, 9})) << "клетка (9,9): ждали в пределах";
    EXPECT_FALSE(in_bounds(Coord{-1, 0})) << "клетка (-1,0): ждали вне поля";
    EXPECT_FALSE(in_bounds(Coord{10, 0})) << "клетка (10,0): ждали вне поля";
    EXPECT_FALSE(in_bounds(Coord{0, -1})) << "клетка (0,-1): ждали вне поля";
    EXPECT_FALSE(in_bounds(Coord{0, 10})) << "клетка (0,10): ждали вне поля";
}

TEST(Fleet, ErrorText) // тексты ошибок расстановки
{
    EXPECT_STREQ(fleet_error_text(FleetError::NONE), "") << "NONE: ждали пустую строку";
    for (FleetError err : {FleetError::WRONG_COMPOSITION, FleetError::OUT_OF_BOUNDS, FleetError::OVERLAP, FleetError::TOUCH}) {
        SCOPED_TRACE("ошибка " + std::to_string(static_cast<int>(err)));
        EXPECT_NE(fleet_error_text(err), "") << "текст для ошибки " << static_cast<int>(err) << " должен быть непустым";
    }
}

TEST(Fleet, RandomFleetValid) // random_fleet для 1000 seed проходит validate_fleet
{
    std::vector<int> expected_lengths(FLEET.begin(), FLEET.end());
    std::sort(expected_lengths.begin(), expected_lengths.end());
    for (int seed = 0; seed < 1000; ++seed) {
        SCOPED_TRACE("seed " + std::to_string(seed));
        std::mt19937 rng(seed);
        std::vector<Ship> fleet = random_fleet(rng);
        ASSERT_EQ(fleet.size(), FLEET.size()) << "ждали 10 кораблей, получено " << fleet.size();
        std::vector<int> lengths;
        for (const Ship &ship : fleet) {
            lengths.push_back(ship.length);
        }
        std::sort(lengths.begin(), lengths.end());
        EXPECT_EQ(lengths, expected_lengths) << "длины кораблей должны совпадать с FLEET по порядку";
        EXPECT_EQ(validate_fleet(fleet), FleetError::NONE) << "случайный флот должен проходить validate_fleet";
    }
}

TEST(Fleet, RandomFleetDeterministic) // один и тот же seed даёт одинаковую расстановку
{
    std::mt19937 rng1(42), rng2(42);
    std::vector<Ship> fleet1 = random_fleet(rng1);
    std::vector<Ship> fleet2 = random_fleet(rng2);
    EXPECT_EQ(fleet1, fleet2) << "seed 42: ждали одинаковую расстановку";
}
