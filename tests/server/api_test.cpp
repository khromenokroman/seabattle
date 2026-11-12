#include "api.hpp"

#include <gtest/gtest.h>

#include <thread>

#include "test_fleet.hpp"

using namespace seabattle;
using namespace seabattle::server;
using nlohmann::json;

namespace {

/// Сервер API на свободном порту в отдельном потоке.
class ApiTest : public ::testing::Test {
   protected:
    void SetUp() override {
        setup_api(m_server, m_rooms, config());
        m_port = m_server.bind_to_any_port("127.0.0.1");
        ASSERT_GT(m_port, 0) << "не удалось занять порт";
        m_thread = std::thread([this] { m_server.listen_after_bind(); });
        m_server.wait_until_ready();
    }

    virtual ApiConfig config() const { return ApiConfig{std::chrono::milliseconds(50)}; }

    void TearDown() override {
        m_rooms.shutdown();
        m_server.stop();
        if (m_thread.joinable()) m_thread.join();
    }

    httplib::Client client() const {
        httplib::Client c("127.0.0.1", m_port);
        c.set_read_timeout(5);
        return c;
    }

    /// POST с JSON, возвращает код и тело.
    std::pair<int, json> post(const std::string &path, const json &body) const {
        auto res = client().Post(path, body.dump(), "application/json");
        if (!res) return {-1, json()};
        return {res->status, json::parse(res->body)};
    }

    std::pair<int, json> get(const std::string &path) const {
        auto res = client().Get(path);
        if (!res) return {-1, json()};
        return {res->status, json::parse(res->body)};
    }

    /// Первое событие state из потока.
    json first_state(const std::string &token) const {
        std::string buffer;
        json state;
        auto res = client().Get("/api/events?token=" + token, [&](const char *data, std::size_t size) {
            buffer.append(data, size);
            const auto end = buffer.find("\n\n");
            if (end == std::string::npos) return true;
            const auto pos = buffer.find("data: ");
            state = json::parse(buffer.substr(pos + 6, end - pos - 6));
            return false;
        });
        return state;
    }

    static json fleet_json() {
        auto ships = json::array();
        for (const auto &s : test_fleet()) {
            ships.push_back({{"x", s.origin.x}, {"y", s.origin.y}, {"len", s.length}, {"dir", "h"}});
        }
        return ships;
    }

    RoomManager m_rooms{7};
    httplib::Server m_server;
    std::thread m_thread;
    int m_port = 0;
};

/// Сервер с заданными --public-url и адресами в сети.
class ApiShareTest : public ApiTest {
   protected:
    ApiConfig config() const override {
        ApiConfig c{std::chrono::milliseconds(50)};
        c.public_url = "https://game.example";
        c.addresses = [] { return std::vector<std::string>{"192.168.1.10", "10.0.0.5"}; };
        return c;
    }
};

} // namespace

// GET /api/share: без настроек - пустой public_url и пустой список адресов
TEST_F(ApiTest, ShareDefaults) {
    const auto [code, body] = get("/api/share");
    ASSERT_EQ(code, 200) << "ответ: " << body.dump();
    EXPECT_EQ(body, json({{"public_url", ""}, {"addresses", json::array()}})) << "ответ: " << body.dump();
}

// GET /api/share отдаёт --public-url и адреса из ApiConfig в исходном порядке
TEST_F(ApiShareTest, Share) {
    const auto [code, body] = get("/api/share");
    ASSERT_EQ(code, 200) << "ответ: " << body.dump();
    EXPECT_EQ(body, json({{"public_url", "https://game.example"}, {"addresses", {"192.168.1.10", "10.0.0.5"}}})) << "ответ: " << body.dump();
}

// Версия и случайный флот
TEST_F(ApiTest, VersionAndRandomFleet) {
    auto [status, body] = get("/api/version");
    EXPECT_EQ(status, 200);
    EXPECT_EQ(body["version"], SEABATTLE_VERSION);

    std::tie(status, body) = get("/api/random-fleet");
    EXPECT_EQ(status, 200);
    ASSERT_TRUE(body["ships"].is_array()) << body.dump();
    EXPECT_EQ(body["ships"].size(), FLEET.size());
}

// Создание, вход, проверка токена и ошибки с кодами
TEST_F(ApiTest, RoomsAndErrors) {
    auto [status, a] = post("/api/rooms", {{"name", "Рома"}});
    ASSERT_EQ(status, 200) << a.dump();
    const std::string room = a["room"];
    auto [s2, b] = post("/api/rooms/" + room + "/join", {{"name", "Лиза"}});
    ASSERT_EQ(s2, 200) << b.dump();
    EXPECT_EQ(b["player"], 1);

    auto [s3, me] = get("/api/me?token=" + std::string(b["token"]));
    EXPECT_EQ(s3, 200);
    EXPECT_EQ(me, json({{"room", room}, {"player", 1}, {"name", "Лиза"}}));

    struct Case {
        const char *what;
        std::pair<int, json> got;
        int status;
    };
    auto client_raw = client().Post("/api/rooms", "{не json", "application/json");
    ASSERT_TRUE(client_raw);
    const std::vector<Case> cases = {
        {"нет имени", post("/api/rooms", json::object()), 400},
        {"имя не строка", post("/api/rooms", {{"name", 5}}), 400},
        {"комната занята", post("/api/rooms/" + room + "/join", {{"name", "Третий"}}), 409},
        {"нет комнаты", post("/api/rooms/ZZZZ/join", {{"name", "Кто-то"}}), 404},
        {"неизвестный токен", get("/api/me?token=bad"), 401},
        {"выстрел до расстановки", post("/api/fire", {{"token", a["token"]}, {"x", 0}, {"y", 0}}), 409},
        {"выстрел без координат", post("/api/fire", {{"token", a["token"]}}), 400},
        {"расстановка без ships", post("/api/place", {{"token", a["token"]}}), 400},
        {"x вне int", post("/api/fire", {{"token", a["token"]}, {"x", 4294967299LL}, {"y", 0}}), 400},
    };
    EXPECT_EQ(client_raw->status, 400) << "неправильный JSON";
    for (const auto &c : cases) {
        SCOPED_TRACE(c.what);
        EXPECT_EQ(c.got.first, c.status) << c.got.second.dump();
        EXPECT_TRUE(c.got.second["error"].is_string()) << "в ответе есть текст ошибки";
    }
}

// Партия по HTTP: расстановка, выстрелы, снимок в потоке событий, сдача
TEST_F(ApiTest, GameFlow) {
    const auto a = post("/api/rooms", {{"name", "Рома"}}).second;
    const auto b = post("/api/rooms/" + std::string(a["room"]) + "/join", {{"name", "Лиза"}}).second;
    EXPECT_EQ(post("/api/place", {{"token", a["token"]}, {"ships", fleet_json()}}).first, 200);
    EXPECT_EQ(post("/api/place", {{"token", b["token"]}, {"ships", fleet_json()}}).first, 200);

    const auto state = first_state(a["token"]);
    ASSERT_EQ(state["phase"], "playing") << state.dump();
    const auto shooter = state["turn"] == "you" ? a["token"] : b["token"];

    auto [status, shot] = post("/api/fire", {{"token", shooter}, {"x", 9}, {"y", 9}});
    EXPECT_EQ(status, 200);
    EXPECT_EQ(shot, json({{"result", "miss"}}));
    EXPECT_EQ(post("/api/fire", {{"token", shooter}, {"x", 8}, {"y", 9}}).first, 409) << "ход перешёл";

    EXPECT_EQ(post("/api/resign", {{"token", shooter}}).first, 200);
    const auto other = shooter == a["token"] ? b["token"] : a["token"];
    const auto final_state = first_state(other);
    EXPECT_EQ(final_state["winner"], "you") << final_state.dump();
    EXPECT_EQ(post("/api/rematch", {{"token", other}}).first, 200);
    EXPECT_EQ(post("/api/leave", {{"token", other}}).first, 200);
    EXPECT_EQ(get("/api/me?token=" + std::string(other)).first, 401);
}

// Поток событий: неизвестный токен 401, заголовки и ping
TEST_F(ApiTest, Events) {
    auto res = client().Get("/api/events?token=bad");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 401);

    const auto a = post("/api/rooms", {{"name", "Рома"}}).second;
    std::string buffer;
    std::string content_type;
    res = client().Get(
        "/api/events?token=" + std::string(a["token"]),
        [&](const httplib::Response &r) {
            content_type = r.get_header_value("Content-Type");
            return true;
        },
        [&](const char *data, std::size_t size) {
            buffer.append(data, size);
            return buffer.find(": ping") == std::string::npos;
        });
    EXPECT_EQ(content_type, "text/event-stream");
    EXPECT_EQ(buffer.rfind("event: state\ndata: ", 0), 0u) << "первым идёт снимок: " << buffer;
    EXPECT_NE(buffer.find(": ping\n\n"), std::string::npos) << "через 50 мс ping: " << buffer;
}

// Второй поток того же токена: первый получает event: closed с reason replaced и закрывается
TEST_F(ApiTest, EventsReplaced) {
    const auto a = post("/api/rooms", {{"name", "Рома"}}).second;
    const std::string token = a["token"];
    std::string buffer;
    std::thread second;
    client().Get("/api/events?token=" + token, [&](const char *data, std::size_t size) {
        buffer.append(data, size);
        if (!second.joinable() && buffer.find("event: state") != std::string::npos) {
            second = std::thread([this, token] { first_state(token); });
        }
        return buffer.find("event: closed") == std::string::npos;
    });
    if (second.joinable()) second.join();
    EXPECT_NE(buffer.find("event: closed\ndata: {\"reason\":\"replaced\"}"), std::string::npos) << buffer;
}
