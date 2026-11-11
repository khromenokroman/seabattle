#include "rooms.hpp"

#include <gtest/gtest.h>

#include "test_fleet.hpp"

using namespace seabattle;
using namespace seabattle::server;
using namespace std::chrono_literals;
using nlohmann::json;

namespace {

/// Ожидание RoomError с кодом.
#define EXPECT_ROOM_ERROR(stmt, code)                                                     \
    do {                                                                                  \
        try {                                                                             \
            stmt;                                                                         \
            ADD_FAILURE() << #stmt << ": ждали RoomError " << code << ", ошибки не было"; \
        } catch (const RoomError &e) {                                                    \
            EXPECT_EQ(e.status(), code) << #stmt << ": текст «" << e.what() << "»";       \
        }                                                                                 \
    } while (0)

/// Комната с двумя игроками.
struct Pair {
    RoomManager rooms{42};
    Joined a;
    Joined b;

    Pair() {
        a = rooms.create_room("Рома");
        b = rooms.join_room(a.room, "Лиза");
    }

    const std::string &token(int player) const { return player == a.player ? a.token : b.token; }

    void place_both() {
        rooms.place(a.token, test_fleet());
        rooms.place(b.token, test_fleet());
    }

    /// Тот, чей ход, топит весь флот противника. Возвращает победителя.
    int play_to_win() {
        const int shooter = rooms.snapshot(a.token)["turn"] == "you" ? a.player : b.player;
        for (const auto &c : test_fleet_cells()) rooms.fire(token(shooter), c);
        return shooter;
    }
};

} // namespace

// Создание и вход: коды, номера игроков, вход без учёта регистра
TEST(Rooms, CreateAndJoin) {
    RoomManager rooms(1);
    const auto a = rooms.create_room("  Рома  ");
    EXPECT_EQ(a.player, 0);
    EXPECT_EQ(a.room.size(), RoomManager::CODE_LENGTH) << "код " << a.room;
    EXPECT_EQ(a.token.size(), 32u) << "токен " << a.token;
    EXPECT_EQ(rooms.me(a.token).name, "Рома") << "имя без пробелов по краям";

    std::string lower = a.room;
    for (auto &ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    const auto b = rooms.join_room(lower, "Лиза");
    EXPECT_EQ(b.player, 1);
    EXPECT_EQ(b.room, a.room) << "вход по коду " << lower;
    EXPECT_NE(a.token, b.token);

    EXPECT_ROOM_ERROR(rooms.join_room(a.room, "Третий"), 409);
    EXPECT_ROOM_ERROR(rooms.join_room("ZZZZ", "Кто-то"), 404);
    EXPECT_ROOM_ERROR(rooms.me("нет-такого"), 401);
}

// Проверка имени: пусто, длиннее 20 символов (считаются символы, не байты), управляющие символы
TEST(Rooms, Names) {
    EXPECT_EQ(normalize_name("Абвгдеёжзийклмнопрст"), "Абвгдеёжзийклмнопрст") << "20 кириллических символов";
    for (const std::string bad : {"", "   ", "Абвгдеёжзийклмнопрсту",
                                  "a\x01"
                                  "b"}) {
        SCOPED_TRACE("имя «" + bad + "»");
        EXPECT_ROOM_ERROR(normalize_name(bad), 400);
    }
}

// Снимок до входа второго: фаза waiting, противника нет, расставлять нельзя
TEST(Rooms, WaitingSnapshot) {
    RoomManager rooms(1);
    const auto a = rooms.create_room("Рома");
    const auto s = rooms.snapshot(a.token);
    EXPECT_EQ(s["phase"], "waiting") << s.dump();
    EXPECT_TRUE(s["opponent"].is_null());
    EXPECT_EQ(s["fleet"], json(FLEET));
    EXPECT_ROOM_ERROR(rooms.place(a.token, test_fleet()), 409);
}

// Расстановка: ошибки и переход в playing
TEST(Rooms, Place) {
    Pair p;
    EXPECT_EQ(p.rooms.snapshot(p.a.token)["phase"], "placing");
    auto bad = test_fleet();
    bad.pop_back();
    EXPECT_ROOM_ERROR(p.rooms.place(p.a.token, bad), 400);

    p.rooms.place(p.a.token, test_fleet());
    EXPECT_ROOM_ERROR(p.rooms.place(p.a.token, test_fleet()), 409);
    const auto sb = p.rooms.snapshot(p.b.token);
    EXPECT_EQ(sb["placed"], json({{"you", false}, {"opponent", true}})) << sb.dump();
    EXPECT_TRUE(sb["own"]["ships"].empty()) << "у Лизы ещё нет кораблей";

    p.rooms.place(p.b.token, test_fleet());
    const auto sa = p.rooms.snapshot(p.a.token);
    EXPECT_EQ(sa["phase"], "playing") << sa.dump();
    EXPECT_EQ(sa["own"]["ships"].size(), 10u);
    EXPECT_TRUE(sa["enemy"]["ships"].empty()) << "корабли противника не видны до конца партии";
    EXPECT_NE(sa["turn"], p.rooms.snapshot(p.b.token)["turn"]) << "ход у одного из двух";
}

// Выстрелы: очередь, ошибки, отметки на обоих полях, last_shot
TEST(Rooms, Fire) {
    Pair p;
    EXPECT_ROOM_ERROR(p.rooms.fire(p.a.token, {0, 0}), 409);
    p.place_both();
    const bool a_first = p.rooms.snapshot(p.a.token)["turn"] == "you";
    const auto &shooter = a_first ? p.a.token : p.b.token;
    const auto &waiter = a_first ? p.b.token : p.a.token;

    EXPECT_ROOM_ERROR(p.rooms.fire(waiter, {0, 0}), 409);
    EXPECT_ROOM_ERROR(p.rooms.fire(shooter, {10, 0}), 400);
    EXPECT_EQ(p.rooms.fire(shooter, {0, 0}), ShotResult::HIT) << "палуба 4-палубного";
    EXPECT_ROOM_ERROR(p.rooms.fire(shooter, {0, 0}), 409);
    EXPECT_EQ(p.rooms.fire(shooter, {3, 4}), ShotResult::SUNK) << "однопалубный (3,4)";
    EXPECT_EQ(p.rooms.fire(shooter, {9, 9}), ShotResult::MISS);

    const auto s = p.rooms.snapshot(shooter);
    EXPECT_EQ(s["turn"], "opponent") << "после промаха ход переходит";
    EXPECT_EQ(s["last_shot"], json({{"by", "you"}, {"x", 9}, {"y", 9}, {"result", "miss"}}));
    EXPECT_EQ(s["enemy"]["sunk"], json::array({{{"x", 3}, {"y", 4}, {"len", 1}, {"dir", "h"}}}));
    // 2 попадания, промах и 8 отметок вокруг однопалубного (3,4)
    EXPECT_EQ(s["enemy"]["shots"].size(), 11u) << s["enemy"]["shots"].dump();

    const auto w = p.rooms.snapshot(waiter);
    EXPECT_EQ(w["turn"], "you");
    EXPECT_EQ(w["own"]["shots"], s["enemy"]["shots"]) << "у противника те же отметки на его поле";
    EXPECT_EQ(w["last_shot"]["by"], "opponent");
    bool sunk_flag = false;
    for (const auto &ship : w["own"]["ships"]) {
        if (ship["x"] == 3 && ship["y"] == 4) sunk_flag = ship["sunk"];
    }
    EXPECT_TRUE(sunk_flag) << "свой потопленный корабль помечен sunk";
}

// Партия до конца: победитель, корабли противника открыты, после конца стрелять нельзя
TEST(Rooms, Finish) {
    Pair p;
    p.place_both();
    const int winner = p.play_to_win();
    const auto &win_token = p.token(winner);
    const auto &lose_token = p.token(Game::opponent(winner));
    const auto s = p.rooms.snapshot(win_token);
    EXPECT_EQ(s["phase"], "finished") << s.dump();
    EXPECT_EQ(s["winner"], "you");
    EXPECT_FALSE(s["resigned"]);
    EXPECT_EQ(s["turn"], nullptr);
    EXPECT_EQ(s["enemy"]["ships"].size(), 10u) << "в конце видны все корабли противника";
    EXPECT_EQ(p.rooms.snapshot(lose_token)["winner"], "opponent");
    EXPECT_ROOM_ERROR(p.rooms.fire(win_token, {9, 9}), 409);
}

// Сдача: в расстановке и в бою; без противника нельзя
TEST(Rooms, Resign) {
    RoomManager single(1);
    const auto alone = single.create_room("Один");
    EXPECT_ROOM_ERROR(single.resign(alone.token), 409);

    Pair p;
    p.rooms.resign(p.a.token);
    const auto s = p.rooms.snapshot(p.b.token);
    EXPECT_EQ(s["phase"], "finished");
    EXPECT_EQ(s["winner"], "you");
    EXPECT_TRUE(s["resigned"]);
    EXPECT_ROOM_ERROR(p.rooms.resign(p.b.token), 409);
}

// Ещё партия: только после конца, новая партия когда попросили оба, первым ходит проигравший
TEST(Rooms, Rematch) {
    Pair p;
    EXPECT_ROOM_ERROR(p.rooms.rematch(p.a.token), 409);
    p.place_both();
    const int winner = p.play_to_win();
    const auto &win_token = p.token(winner);
    const auto &lose_token = p.token(Game::opponent(winner));

    p.rooms.rematch(win_token);
    auto s = p.rooms.snapshot(lose_token);
    EXPECT_EQ(s["phase"], "finished");
    EXPECT_EQ(s["rematch"], json({{"you", false}, {"opponent", true}})) << s.dump();

    p.rooms.rematch(lose_token);
    s = p.rooms.snapshot(lose_token);
    EXPECT_EQ(s["phase"], "placing") << s.dump();
    EXPECT_TRUE(s["own"]["ships"].empty());
    EXPECT_EQ(s["last_shot"], nullptr);
    EXPECT_EQ(s["rematch"], json({{"you", false}, {"opponent", false}}));

    p.place_both();
    EXPECT_EQ(p.rooms.snapshot(lose_token)["turn"], "you") << "первым стреляет проигравший";
}

// Выход: противник ждёт нового игрока, место занимает вошедший, пустая комната удаляется
TEST(Rooms, Leave) {
    Pair p;
    p.place_both();
    p.rooms.leave(p.a.token);
    EXPECT_ROOM_ERROR(p.rooms.me(p.a.token), 401);
    auto s = p.rooms.snapshot(p.b.token);
    EXPECT_EQ(s["phase"], "waiting") << s.dump();
    EXPECT_TRUE(s["opponent"].is_null());
    EXPECT_TRUE(s["own"]["ships"].empty()) << "партия начинается заново";

    const auto c = p.rooms.join_room(p.b.room, "Костя");
    EXPECT_EQ(c.player, p.a.player) << "вошедший занимает освободившееся место";
    EXPECT_EQ(p.rooms.snapshot(p.b.token)["phase"], "placing");

    p.rooms.leave(p.b.token);
    p.rooms.leave(c.token);
    EXPECT_EQ(p.rooms.room_count(), 0u);
}

// Поток событий: первый снимок сразу, без изменений PING, изменение будит поток
TEST(Rooms, StreamEvents) {
    Pair p;
    const auto stream = p.rooms.open_stream(p.a.token);
    std::uint64_t seen = 0;
    json state;
    ASSERT_EQ(p.rooms.wait_event(p.a.token, stream, seen, state, 10ms), StreamEvent::STATE);
    EXPECT_EQ(state["room"], p.a.room);
    EXPECT_EQ(p.rooms.wait_event(p.a.token, stream, seen, state, 10ms), StreamEvent::PING);

    std::thread other([&] {
        std::this_thread::sleep_for(20ms);
        p.rooms.place(p.b.token, test_fleet());
    });
    const auto event = p.rooms.wait_event(p.a.token, stream, seen, state, 5000ms);
    other.join();
    EXPECT_EQ(event, StreamEvent::STATE) << "изменение будит ожидание";
    EXPECT_EQ(state["placed"]["opponent"], true) << state.dump();
}

// Признак connected у противника и закрытие старого потока при новом
TEST(Rooms, StreamConnected) {
    Pair p;
    EXPECT_EQ(p.rooms.snapshot(p.b.token)["opponent"]["connected"], false);
    const auto first = p.rooms.open_stream(p.a.token);
    EXPECT_EQ(p.rooms.snapshot(p.b.token)["opponent"]["connected"], true);

    const auto second = p.rooms.open_stream(p.a.token);
    std::uint64_t seen = 0;
    json state;
    EXPECT_EQ(p.rooms.wait_event(p.a.token, first, seen, state, 10ms), StreamEvent::CLOSED) << "старый поток";
    p.rooms.close_stream(p.a.token, first);
    EXPECT_EQ(p.rooms.snapshot(p.b.token)["opponent"]["connected"], true) << "закрытие старого не трогает новый";

    p.rooms.close_stream(p.a.token, second);
    EXPECT_EQ(p.rooms.snapshot(p.b.token)["opponent"]["connected"], false);
    EXPECT_ROOM_ERROR(p.rooms.open_stream("нет-такого"), 401);
}

// После выхода и остановки сервера поток закрывается
TEST(Rooms, StreamClosed) {
    Pair p;
    const auto sa = p.rooms.open_stream(p.a.token);
    const auto sb = p.rooms.open_stream(p.b.token);
    std::uint64_t seen = 0;
    json state;
    p.rooms.leave(p.a.token);
    EXPECT_EQ(p.rooms.wait_event(p.a.token, sa, seen, state, 10ms), StreamEvent::CLOSED) << "после leave";
    p.rooms.shutdown();
    seen = 0;
    EXPECT_EQ(p.rooms.wait_event(p.b.token, sb, seen, state, 10ms), StreamEvent::CLOSED) << "после shutdown";
}

// Очистка: удаляются только комнаты без игроков в сети дольше idle
TEST(Rooms, Cleanup) {
    RoomManager rooms(1);
    const auto idle = rooms.create_room("Ушёл");
    const auto online = rooms.create_room("Здесь");
    rooms.open_stream(online.token);
    const auto now = RoomManager::Clock::now();
    EXPECT_EQ(rooms.cleanup(now, 30min), 0u) << "30 минут ещё не прошло";
    EXPECT_EQ(rooms.cleanup(now + 31min, 30min), 1u);
    EXPECT_ROOM_ERROR(rooms.me(idle.token), 401);
    EXPECT_EQ(rooms.me(online.token).name, "Здесь");
}
