#include "seabattle/game.hpp"

#include <gtest/gtest.h>

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

/// Флот с ошибкой TOUCH: второй корабль касается 4-палубного.
std::vector<Ship> touch_fleet() {
    std::vector<Ship> fleet = valid_fleet();
    fleet[1] = Ship{Coord{4, 0}, 3, Orientation::HORIZONTAL};
    return fleet;
}

/// Игра, в которой оба игрока расставили флот.
Game placed_game(int first_player) {
    Game game(first_player);
    EXPECT_EQ(game.place(0, valid_fleet()).error, MoveError::NONE) << "place игрока 0: ждали NONE";
    EXPECT_EQ(game.place(1, valid_fleet()).error, MoveError::NONE) << "place игрока 1: ждали NONE";
    return game;
}

/// Игрок 0 топит все корабли: стреляет по всем клеткам поля противника.
/// После промаха игрок 1 стреляет в первую свободную клетку поля игрока 0 (не в корабли).
/// Если клетка уже была отмечена (ALREADY_SHOT), пропускаем её.
void player0_wins(Game &game) {
    for (int y = 0; y < BOARD_SIZE; ++y) {
        for (int x = 0; x < BOARD_SIZE; ++x) {
            if (game.phase() != Phase::PLAYING) {
                return;
            }
            ASSERT_EQ(game.turn(), 0) << "клетка (" << x << "," << y << "): ждали ход игрока 0";
            FireResult result = game.fire(0, Coord{x, y});
            if (result.error == MoveError::ALREADY_SHOT) {
                // Клетка уже была отмечена после потопления корабля, пропускаем
                continue;
            }
            ASSERT_EQ(result.error, MoveError::NONE) << "fire (" << x << "," << y << "): ждали NONE";
            if (result.shot.result == ShotResult::MISS) {
                // Промашенный ход: игрок 1 стреляет в первую свободную клетку поля игрока 0,
                // которая гарантированно является водой (y=1, y=3, y=5..9 в valid_fleet())
                bool found_water = false;
                for (int ry : {1, 3, 5, 6, 7, 8, 9}) {
                    for (int rx = 0; rx < BOARD_SIZE && !found_water; ++rx) {
                        FireResult reply = game.fire(1, Coord{rx, ry});
                        if (reply.error == MoveError::NONE) {
                            if (reply.shot.result == ShotResult::MISS) {
                                found_water = true;
                                EXPECT_EQ(game.turn(), 0) << "промах: ход вернулся игроку 0";
                            } else {
                                FAIL() << "ответный fire (" << rx << "," << ry << "): ждали MISS, получили " << static_cast<int>(reply.shot.result);
                            }
                        }
                    }
                }
                ASSERT_TRUE(found_water) << "не нашли свободную клетку для ответного выстрела";
            }
            if (game.phase() == Phase::FINISHED) {
                return;
            }
        }
    }
}

} // namespace

TEST(Game, InitialPhase) // начальная фаза PLACING
{
    Game game;
    EXPECT_EQ(game.phase(), Phase::PLACING) << "новая игра: ждали PLACING";
    EXPECT_FALSE(game.winner().has_value()) << "новая игра: победителя нет";
    EXPECT_FALSE(game.resigned()) << "новая игра: никто не сдавался";
    EXPECT_TRUE(game.history().empty()) << "новая игра: история пуста";
}

TEST(Game, PlaceErrorsInOrder) // ошибки place по порядку: BAD_PLAYER, WRONG_PHASE, ALREADY_PLACED, INVALID_FLEET
{
    {
        Game game;
        PlaceResult result = game.place(2, valid_fleet());
        EXPECT_EQ(result.error, MoveError::BAD_PLAYER) << "place(2): ждали BAD_PLAYER";
    }
    {
        Game game = placed_game(0);
        ASSERT_EQ(game.phase(), Phase::PLAYING) << "после двух place: ждали PLAYING";
        PlaceResult result = game.place(0, valid_fleet());
        EXPECT_EQ(result.error, MoveError::WRONG_PHASE) << "place после начала игры: ждали WRONG_PHASE";
    }
    {
        Game game;
        game.place(0, valid_fleet());
        PlaceResult result = game.place(0, valid_fleet());
        EXPECT_EQ(result.error, MoveError::ALREADY_PLACED) << "повторный place игрока 0: ждали ALREADY_PLACED";
    }
    {
        Game game;
        game.place(0, valid_fleet());
        PlaceResult result = game.place(1, touch_fleet());
        EXPECT_EQ(result.error, MoveError::INVALID_FLEET) << "place флота с TOUCH: ждали INVALID_FLEET";
        EXPECT_EQ(result.fleet_error, FleetError::TOUCH) << "fleet_error: ждали TOUCH";
        EXPECT_FALSE(game.placed(1)) << "игрок 1 не расставил";
    }
}

TEST(Game, TurnAfterPlace) // после двух place фаза PLAYING и ход first_player
{
    for (int first : {0, 1}) {
        SCOPED_TRACE("first_player " + std::to_string(first));
        Game game = placed_game(first);
        EXPECT_EQ(game.phase(), Phase::PLAYING) << "фаза: ждали PLAYING";
        EXPECT_EQ(game.turn(), first) << "первый ход: ждали игрока " << first;
        EXPECT_TRUE(game.placed(0)) << "игрок 0 расставил: ждали true";
        EXPECT_TRUE(game.placed(1)) << "игрок 1 расставил: ждали true";
        EXPECT_EQ(game.board(0).ships().size(), std::size_t{10}) << "поле игрока 0: 10 кораблей";
    }
}

TEST(Game, ConstructorRejectsBadFirstPlayer) // конструктор с first_player 2 бросает invalid_argument
{
    EXPECT_THROW(Game(2), std::invalid_argument) << "first_player 2: ждали std::invalid_argument";
    Game game0(0), game1(1);
    EXPECT_EQ(game0.phase(), Phase::PLACING) << "first_player 0: фаза PLACING";
    EXPECT_EQ(game1.phase(), Phase::PLACING) << "first_player 1: фаза PLACING";
}

TEST(Game, FireErrorsInOrder) // ошибки fire по порядку: BAD_PLAYER, WRONG_PHASE, NOT_YOUR_TURN, OUT_OF_BOUNDS, ALREADY_SHOT
{
    {
        Game game;
        FireResult result = game.fire(2, Coord{0, 0});
        EXPECT_EQ(result.error, MoveError::BAD_PLAYER) << "fire(2): ждали BAD_PLAYER";
    }
    {
        Game game;
        FireResult result = game.fire(0, Coord{0, 0});
        EXPECT_EQ(result.error, MoveError::WRONG_PHASE) << "fire в PLACING: ждали WRONG_PHASE";
        EXPECT_TRUE(game.history().empty()) << "отказ: история не изменилась";
    }
    {
        Game game = placed_game(0);
        FireResult result = game.fire(1, Coord{0, 0});
        EXPECT_EQ(result.error, MoveError::NOT_YOUR_TURN) << "fire не своим ходом: ждали NOT_YOUR_TURN";
        EXPECT_EQ(game.turn(), 0) << "отказ: ход не изменился";
        result = game.fire(0, Coord{-1, 0});
        EXPECT_EQ(result.error, MoveError::OUT_OF_BOUNDS) << "fire вне поля: ждали OUT_OF_BOUNDS";
        EXPECT_TRUE(game.history().empty()) << "отказ: история не изменилась";
        result = game.fire(0, Coord{0, 1});
        ASSERT_EQ(result.error, MoveError::NONE) << "fire (0,1) в воду: ждали NONE";
        result = game.fire(0, Coord{0, 1});
        EXPECT_EQ(result.error, MoveError::NOT_YOUR_TURN) << "промах передал ход: ждали NOT_YOUR_TURN";
        result = game.fire(1, Coord{0, 0});
        ASSERT_EQ(result.error, MoveError::NONE) << "fire (0,0) игроком 1: ждали NONE";
        EXPECT_EQ(result.shot.result, ShotResult::HIT) << "fire (0,0): ждали HIT";
        result = game.fire(0, Coord{0, 1});
        EXPECT_EQ(result.error, MoveError::NOT_YOUR_TURN) << "попадание: ход не передался, ждали NOT_YOUR_TURN";
        result = game.fire(1, Coord{1, 0});
        ASSERT_EQ(result.error, MoveError::NONE) << "fire (1,0) игроком 1: ждали NONE";
        EXPECT_EQ(result.shot.result, ShotResult::HIT) << "fire (1,0): ждали HIT";
        result = game.fire(1, Coord{1, 0});
        EXPECT_EQ(result.error, MoveError::ALREADY_SHOT) << "повторный fire в HIT: ждали ALREADY_SHOT";
        EXPECT_EQ(game.history().size(), std::size_t{3}) << "отказ: история не изменилась";
    }
}

TEST(Game, MissPassesTurnHitKeeps) // промах передаёт ход, попадание оставляет
{
    Game game = placed_game(0);
    FireResult result = game.fire(0, Coord{0, 1});
    ASSERT_EQ(result.error, MoveError::NONE) << "fire (0,1): ждали NONE";
    EXPECT_EQ(result.shot.result, ShotResult::MISS) << "fire (0,1): ждали MISS";
    EXPECT_EQ(game.turn(), 1) << "промах: ход передали игроку 1";
    result = game.fire(1, Coord{1, 0});
    ASSERT_EQ(result.error, MoveError::NONE) << "fire (1,0): ждали NONE";
    EXPECT_EQ(result.shot.result, ShotResult::HIT) << "fire (1,0): ждали HIT";
    EXPECT_EQ(game.turn(), 1) << "попадание: ход остался у игрока 1";
}

TEST(Game, FullGameUntilVictory) // полная партия до победы: игрок 0 топит все корабли
{
    Game game = placed_game(0);
    player0_wins(game);
    EXPECT_EQ(game.phase(), Phase::FINISHED) << "после потопления флота: ждали FINISHED";
    EXPECT_TRUE(game.winner().has_value()) << "после конца: победитель есть";
    EXPECT_EQ(game.winner().value(), 0) << "победитель: ждали игрока 0";
    EXPECT_FALSE(game.resigned()) << "партия закончена победой, не сдачей";
    FireResult result = game.fire(0, Coord{0, 0});
    EXPECT_EQ(result.error, MoveError::WRONG_PHASE) << "fire после конца: ждали WRONG_PHASE";
    result = game.fire(1, Coord{0, 0});
    EXPECT_EQ(result.error, MoveError::WRONG_PHASE) << "fire игрока 1 после конца: ждали WRONG_PHASE";
}

TEST(Game, ResignInPlacing) // resign во время расстановки
{
    Game game;
    MoveError err = game.resign(0);
    EXPECT_EQ(err, MoveError::NONE) << "resign(0) в PLACING: ждали NONE";
    EXPECT_EQ(game.phase(), Phase::FINISHED) << "после resign: ждали FINISHED";
    EXPECT_EQ(game.winner().value(), 1) << "победитель: ждали игрока 1";
    EXPECT_TRUE(game.resigned()) << "партия закончилась сдачей";
    EXPECT_EQ(game.resign(1), MoveError::WRONG_PHASE) << "resign после конца: ждали WRONG_PHASE";
    EXPECT_EQ(game.resign(2), MoveError::BAD_PLAYER) << "resign(2): ждали BAD_PLAYER";
}

TEST(Game, ResignInPlaying) // resign в PLAYING
{
    Game game = placed_game(0);
    game.fire(0, Coord{0, 1}); // промах, ход у 1
    MoveError err = game.resign(1);
    EXPECT_EQ(err, MoveError::NONE) << "resign(1) в PLAYING: ждали NONE";
    EXPECT_EQ(game.phase(), Phase::FINISHED) << "после resign: ждали FINISHED";
    EXPECT_EQ(game.winner().value(), 0) << "победитель: ждали игрока 0";
    EXPECT_TRUE(game.resigned()) << "партия закончилась сдачей";
    EXPECT_EQ(game.turn(), 1) << "последний ход: игрок 1";
    EXPECT_EQ(game.fire(1, Coord{0, 0}).error, MoveError::WRONG_PHASE) << "fire после resign: ждали WRONG_PHASE";
}

TEST(Game, HistoryOnlyAcceptedShots) // history: только принятые выстрелы в порядке хода
{
    Game game = placed_game(0);
    ASSERT_EQ(game.fire(0, Coord{0, 1}).error, MoveError::NONE) << "fire (0,1): ждали NONE";
    game.fire(0, Coord{5, 5}); // NOT_YOUR_TURN, не в историю
    ASSERT_EQ(game.fire(1, Coord{1, 0}).error, MoveError::NONE) << "fire (1,0): ждали NONE";
    game.fire(0, Coord{2, 0}); // NOT_YOUR_TURN (попадание: ход остался у 1), не в историю
    ASSERT_EQ(game.fire(1, Coord{2, 0}).error, MoveError::NONE) << "fire (2,0): ждали NONE";
    const std::vector<ShotRecord> &history = game.history();
    ASSERT_EQ(history.size(), std::size_t{3}) << "в истории только принятые выстрелы: ждали 3 записи";
    EXPECT_EQ(history[0].player, 0) << "запись 0: стрелял игрок 0";
    EXPECT_EQ(history[0].at, (Coord{0, 1})) << "запись 0: клетка (0,1)";
    EXPECT_EQ(history[0].result, ShotResult::MISS) << "запись 0: MISS";
    EXPECT_EQ(history[1].player, 1) << "запись 1: стрелял игрок 1";
    EXPECT_EQ(history[1].at, (Coord{1, 0})) << "запись 1: клетка (1,0)";
    EXPECT_EQ(history[1].result, ShotResult::HIT) << "запись 1: HIT";
    EXPECT_EQ(history[2].player, 1) << "запись 2: стрелял игрок 1";
    EXPECT_EQ(history[2].at, (Coord{2, 0})) << "запись 2: клетка (2,0)";
    EXPECT_EQ(history[2].result, ShotResult::HIT) << "запись 2: HIT";
}

TEST(Game, BoardThrows) // board() до place бросает logic_error
{
    Game game;
    EXPECT_THROW(game.board(0), std::logic_error) << "board(0) до place: ждали std::logic_error";
    EXPECT_THROW(game.board(1), std::logic_error) << "board(1) до place: ждали std::logic_error";
    game.place(0, valid_fleet());
    EXPECT_NO_THROW(game.board(0)) << "board(0) после place: исключение не ждали";
    EXPECT_THROW(game.board(1), std::logic_error) << "board(1) без place: ждали std::logic_error";
    EXPECT_THROW(game.board(2), std::out_of_range) << "board(2): ждали std::out_of_range";
    EXPECT_THROW(game.placed(2), std::out_of_range) << "placed(2): ждали std::out_of_range";
}

TEST(Game, MoveErrorText) // тексты ошибок действий
{
    EXPECT_STREQ(move_error_text(MoveError::NONE), "") << "NONE: ждали пустую строку";
    for (MoveError err : {MoveError::BAD_PLAYER, MoveError::WRONG_PHASE, MoveError::ALREADY_PLACED, MoveError::INVALID_FLEET,
                          MoveError::NOT_YOUR_TURN, MoveError::OUT_OF_BOUNDS, MoveError::ALREADY_SHOT}) {
        SCOPED_TRACE("ошибка " + std::to_string(static_cast<int>(err)));
        EXPECT_NE(move_error_text(err), "") << "текст для ошибки " << static_cast<int>(err) << " должен быть непустым";
    }
}
