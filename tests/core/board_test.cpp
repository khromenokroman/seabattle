#include "seabattle/board.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>
#include <vector>

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

/// Флот с однопалубным в углу (9,9), для проверки marked.
std::vector<Ship> corner_fleet() {
    std::vector<Ship> fleet = valid_fleet();
    fleet.back() = Ship{Coord{9, 9}, 1, Orientation::HORIZONTAL};
    return fleet;
}

} // namespace

TEST(Board, ConstructorRejectsInvalidFleet) // конструктор бросает invalid_argument на неправильном флоте
{
    auto fleet = valid_fleet();
    fleet[0] = Ship{Coord{7, 0}, 4, Orientation::HORIZONTAL}; // за краем
    try {
        Board board(fleet);
        FAIL() << "флот с кораблём за краем: ждали std::invalid_argument";
    } catch (const std::invalid_argument &e) {
        EXPECT_STREQ(e.what(), fleet_error_text(FleetError::OUT_OF_BOUNDS)) << "what() должно быть текстом ошибки OUT_OF_BOUNDS";
    }
    try {
        Board board(valid_fleet());
    } catch (const std::exception &e) {
        FAIL() << "правильный флот: не ждали исключения: " << e.what();
    }
}

TEST(Board, MissShot) // выстрел в воду даёт MISS и клетка становится MISS
{
    Board board(valid_fleet());
    ShotOutcome outcome = board.shoot(Coord{4, 1});
    EXPECT_EQ(outcome.result, ShotResult::MISS) << "выстрел (4,1) в воду: ждали MISS";
    EXPECT_FALSE(outcome.sunk.has_value()) << "промах: sunk не должен быть заполнен";
    EXPECT_TRUE(outcome.marked.empty()) << "промах: marked должен быть пуст";
    EXPECT_EQ(board.cell(Coord{4, 1}), Cell::MISS) << "клетка (4,1) после промаха: ждали MISS";
}

TEST(Board, HitShot) // выстрел в палубу не последнего ранения даёт HIT
{
    Board board(valid_fleet());
    ShotOutcome outcome = board.shoot(Coord{0, 0});
    EXPECT_EQ(outcome.result, ShotResult::HIT) << "выстрел (0,0) в 4-палубный: ждали HIT";
    EXPECT_FALSE(outcome.sunk.has_value()) << "HIT: sunk не должен быть заполнен";
    EXPECT_TRUE(outcome.marked.empty()) << "HIT: marked должен быть пуст";
    EXPECT_EQ(board.cell(Coord{0, 0}), Cell::HIT) << "клетка (0,0) после попадания: ждали HIT";
    EXPECT_FALSE(board.is_sunk(0)) << "4-палубный после одного попадания: ждали не потоплен";
}

TEST(Board, SunkCornerShipMarksNeighbors) // потопленный однопалубный в углу (9,9): точный marked
{
    Board board(corner_fleet());
    ShotOutcome outcome = board.shoot(Coord{9, 9});
    EXPECT_EQ(outcome.result, ShotResult::SUNK) << "выстрел (9,9) в однопалубный: ждали SUNK";
    ASSERT_TRUE(outcome.sunk.has_value()) << "SUNK: sunk должен быть заполнен";
    EXPECT_EQ(*outcome.sunk, (Ship{Coord{9, 9}, 1, Orientation::HORIZONTAL})) << "sunk: ждали корабль (9,9) len 1 HORIZONTAL";
    std::vector<Coord> expected = {Coord{8, 8}, Coord{9, 8}, Coord{8, 9}};
    EXPECT_EQ(outcome.marked, expected) << "marked для угла (9,9): ждали (8,8), (9,8), (8,9)";
    for (const Coord &c : expected) {
        SCOPED_TRACE("клетка " + std::to_string(c.x) + "," + std::to_string(c.y));
        EXPECT_EQ(board.cell(c), Cell::MISS) << "сосед потопленного: ждали MISS";
    }
}

TEST(Board, SingleDeckHitIsSunk) // выстрел в однопалубный даёт SUNK с точным marked, HIT не тонит
{
    Board board(valid_fleet());
    ShotOutcome outcome = board.shoot(Coord{3, 4});
    ASSERT_EQ(outcome.result, ShotResult::SUNK) << "выстрел (3,4) в однопалубный: ждали SUNK";
    ASSERT_TRUE(outcome.sunk.has_value()) << "SUNK: sunk должен быть заполнен";
    EXPECT_EQ(*outcome.sunk, (Ship{Coord{3, 4}, 1, Orientation::HORIZONTAL})) << "sunk: ждали однопалубный (3,4)";
    // Соседи (3,4) в пределах поля и не занятые: y=3: x=2..4, y=4: x=2 и x=4, y=5: x=2..4.
    std::vector<Coord> expected = {
        Coord{2, 3}, Coord{3, 3}, Coord{4, 3}, Coord{2, 4}, Coord{4, 4}, Coord{2, 5}, Coord{3, 5}, Coord{4, 5},
    };
    EXPECT_EQ(outcome.marked, expected) << "marked вокруг (3,4): ждали 8 клеток";
    EXPECT_TRUE(board.is_sunk(6)) << "однопалубный (3,4): ждали is_sunk == true";

    Board board2(valid_fleet());
    ShotOutcome second = board2.shoot(Coord{0, 0});
    EXPECT_EQ(second.result, ShotResult::HIT) << "выстрел (0,0) в 4-палубный: ждали HIT";
    EXPECT_FALSE(board2.is_sunk(0)) << "подбита 1 из 4 палуб: ждали is_sunk == false";
}

TEST(Board, SunkShipFillsSunkAndMarked) // потопленный многопалубный корабль: sunk и marked без палуб
{
    Board board(valid_fleet());
    ShotOutcome outcome;
    // Топим 2-палубный (4,2)-(5,2).
    EXPECT_EQ(board.shoot(Coord{4, 2}).result, ShotResult::HIT) << "выстрел (4,2): ждали HIT";
    outcome = board.shoot(Coord{5, 2});
    ASSERT_EQ(outcome.result, ShotResult::SUNK) << "выстрел (5,2): ждали SUNK";
    EXPECT_EQ(outcome.sunk.value(), (Ship{Coord{4, 2}, 2, Orientation::HORIZONTAL})) << "sunk: ждали 2-палубный (4,2)";
    // Соседи: y=1: x=3..6, y=2: x=3 и x=6, y=3: x=3..6, в пределах поля.
    std::vector<Coord> expected = {
        Coord{3, 1}, Coord{4, 1}, Coord{5, 1}, Coord{6, 1}, Coord{3, 2}, Coord{6, 2}, Coord{3, 3}, Coord{4, 3}, Coord{5, 3}, Coord{6, 3},
    };
    EXPECT_EQ(outcome.marked, expected) << "marked вокруг 2-палубного (4,2): ждали 10 клеток";
    // Палубы самого корабля не входят в marked и остаются HIT.
    for (const Coord &c : {Coord{4, 2}, Coord{5, 2}}) {
        EXPECT_EQ(board.cell(c), Cell::HIT) << "палуба (" << c.x << "," << c.y << "): ждали HIT";
    }
    EXPECT_FALSE(std::find(outcome.marked.begin(), outcome.marked.end(), Coord{4, 2}) != outcome.marked.end())
        << "marked не должен включать палубы самого корабля";
}

TEST(Board, MarkedSkipsAlreadyShotCells) // marked не включает уже обстрелянные клетки
{
    Board board(corner_fleet());
    EXPECT_EQ(board.shoot(Coord{8, 8}).result, ShotResult::MISS) << "предварительный промах (8,8)";
    ShotOutcome outcome = board.shoot(Coord{9, 9});
    ASSERT_EQ(outcome.result, ShotResult::SUNK) << "выстрел (9,9): ждали SUNK";
    std::vector<Coord> expected = {Coord{9, 8}, Coord{8, 9}}; // (8,8) уже MISS
    EXPECT_EQ(outcome.marked, expected) << "marked: (8,8) уже обстреляна, ждали только (9,8), (8,9)";
    EXPECT_EQ(board.cell(Coord{8, 8}), Cell::MISS) << "клетка (8,8) осталась MISS";
}

TEST(Board, AlreadyShotDoesNotChangeState) // повторный выстрел и выстрел в отмеченную клетку
{
    Board board(corner_fleet());
    EXPECT_EQ(board.shoot(Coord{0, 0}).result, ShotResult::HIT) << "выстрел (0,0): ждали HIT";
    ShotOutcome outcome = board.shoot(Coord{9, 9});
    ASSERT_EQ(outcome.result, ShotResult::SUNK) << "выстрел (9,9): ждали SUNK";
    ASSERT_EQ(outcome.marked.size(), std::size_t{3}) << "marked: ждали 3 клетки";
    std::vector<Cell> cells_before(100);
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 10; ++x) {
            cells_before[y * 10 + x] = board.cell(Coord{x, y});
        }
    }
    // Повторный выстрел в HIT.
    outcome = board.shoot(Coord{0, 0});
    EXPECT_EQ(outcome.result, ShotResult::ALREADY_SHOT) << "повторный выстрел (0,0): ждали ALREADY_SHOT";
    EXPECT_EQ(board.cell(Coord{0, 0}), Cell::HIT) << "состояние (0,0) не изменилось";
    // Выстрел в клетку из marked.
    outcome = board.shoot(Coord{8, 9});
    EXPECT_EQ(outcome.result, ShotResult::ALREADY_SHOT) << "выстрел в отмеченную клетку: ждали ALREADY_SHOT";
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 10; ++x) {
            EXPECT_EQ(board.cell(Coord{x, y}), cells_before[y * 10 + x]) << "клетка (" << x << "," << y << "): поле не должно меняться";
        }
    }
}

TEST(Board, OutOfBounds) // выстрел вне поля
{
    Board board(valid_fleet());
    for (const Coord &c : {Coord{-1, 0}, Coord{10, 0}, Coord{0, -1}, Coord{0, 10}}) {
        SCOPED_TRACE("клетка " + std::to_string(c.x) + "," + std::to_string(c.y));
        ShotOutcome outcome = board.shoot(c);
        EXPECT_EQ(outcome.result, ShotResult::OUT_OF_BOUNDS) << "выстрел (" << c.x << "," << c.y << "): ждали OUT_OF_BOUNDS";
    }
    ShotOutcome outcome = board.shoot(Coord{-1, -1});
    EXPECT_EQ(outcome.result, ShotResult::OUT_OF_BOUNDS) << "выстрел (-1,-1): ждали OUT_OF_BOUNDS";
    EXPECT_EQ(board.cell(Coord{0, 0}), Cell::SHIP) << "поле не изменилось";
}

TEST(Board, CellOutOfBoundsThrows) // cell() вне поля бросает out_of_range
{
    Board board(valid_fleet());
    for (const Coord &c : {Coord{-1, 0}, Coord{10, 9}, Coord{9, 10}, Coord{-5, -5}}) {
        SCOPED_TRACE("клетка " + std::to_string(c.x) + "," + std::to_string(c.y));
        EXPECT_THROW(board.cell(c), std::out_of_range) << "cell(" << c.x << "," << c.y << "): ждали std::out_of_range";
    }
}

TEST(Board, IsSunkAndSunkShipsOrder) // is_sunk и sunk_ships в порядке ships()
{
    Board board(valid_fleet());
    // Топим корабль индекс 6 (1-палубный (3,4)) и корабль индекс 9 (1-палубный (9,4)).
    EXPECT_EQ(board.shoot(Coord{3, 4}).result, ShotResult::SUNK) << "выстрел (3,4): ждали SUNK";
    EXPECT_EQ(board.shoot(Coord{9, 4}).result, ShotResult::SUNK) << "выстрел (9,4): ждали SUNK";
    EXPECT_FALSE(board.is_sunk(0)) << "4-палубный не обстрелян: ждали false";
    EXPECT_TRUE(board.is_sunk(6)) << "корабль 6 потоплен: ждали true";
    EXPECT_TRUE(board.is_sunk(9)) << "корабль 9 потоплен: ждали true";
    std::vector<Ship> expected = {board.ships()[6], board.ships()[9]};
    EXPECT_EQ(board.sunk_ships(), expected) << "sunk_ships: ждали корабли 6 и 9 в порядке ships()";
    EXPECT_THROW(board.is_sunk(10), std::out_of_range) << "is_sunk(10): ждали std::out_of_range";
}

TEST(Board, AllSunkOnlyAfterLastDeck) // all_sunk только после последней палубы
{
    Board board(valid_fleet());
    EXPECT_FALSE(board.all_sunk()) << "никуда не стреляли: ждали false";
    // Подбиваем 3 палубы 4-палубного: false.
    for (const Coord &c : {Coord{0, 0}, Coord{1, 0}, Coord{2, 0}}) {
        EXPECT_EQ(board.shoot(c).result, ShotResult::HIT) << "выстрел (" << c.x << "," << c.y << "): ждали HIT";
        EXPECT_FALSE(board.all_sunk()) << "подбиты 3 из 17 палуб: ждали false";
    }
    // Теперь топит всё.
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 10; ++x) {
            ShotOutcome outcome = board.shoot(Coord{x, y});
            if (outcome.result == ShotResult::ALREADY_SHOT) {
                continue;
            }
        }
        if (board.all_sunk()) {
            break;
        }
    }
    EXPECT_TRUE(board.all_sunk()) << "все палубы подбиты: ждали true";
}
