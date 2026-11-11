#include "api.hpp"

#include <iostream>
#include <memory>
#include <random>

#include "json_io.hpp"

namespace seabattle::server {

namespace {

constexpr const char *JSON_TYPE = "application/json; charset=utf-8";

void reply(httplib::Response &res, const nlohmann::json &body, int status = 200) {
    res.status = status;
    res.set_header("Cache-Control", "no-store");
    res.set_content(body.dump(), JSON_TYPE);
}

void reply_error(httplib::Response &res, int status, const std::string &message) { reply(res, {{"error", message}}, status); }

nlohmann::json parse_body(const httplib::Request &req) {
    try {
        auto body = nlohmann::json::parse(req.body);
        if (!body.is_object()) throw RoomError(400, "Тело запроса должно быть JSON-объектом");
        return body;
    } catch (const nlohmann::json::parse_error &) {
        throw RoomError(400, "Неправильный JSON в запросе");
    }
}

std::string get_string(const nlohmann::json &body, const char *key) {
    if (!body.contains(key) || !body[key].is_string()) {
        throw RoomError(400, std::string("Нет строкового поля ") + key);
    }
    return body[key].get<std::string>();
}

/// Обработчик с переводом RoomError и std::invalid_argument в ответ с ошибкой.
template <typename F>
httplib::Server::Handler guarded(F f) {
    return [f](const httplib::Request &req, httplib::Response &res) {
        try {
            f(req, res);
        } catch (const RoomError &e) {
            reply_error(res, e.status(), e.what());
        } catch (const std::invalid_argument &e) {
            reply_error(res, 400, e.what());
        }
    };
}

nlohmann::json joined_json(const Joined &j) { return {{"room", j.room}, {"token", j.token}, {"player", j.player}}; }

} // namespace

void setup_api(httplib::Server &server, RoomManager &rooms, ApiConfig config) {
    server.set_payload_max_length(64 * 1024);

    server.set_exception_handler([](const httplib::Request &, httplib::Response &res, std::exception_ptr ep) {
        std::string message = "Внутренняя ошибка сервера";
        try {
            std::rethrow_exception(ep);
        } catch (const std::exception &e) {
            message += std::string(": ") + e.what();
        } catch (...) {
        }
        reply_error(res, 500, message);
    });

    server.Get("/api/version", guarded([](const httplib::Request &, httplib::Response &res) { reply(res, {{"version", SEABATTLE_VERSION}}); }));

    server.Get("/api/random-fleet", guarded([](const httplib::Request &, httplib::Response &res) {
                   thread_local std::mt19937 rng{std::random_device{}()};
                   auto ships = nlohmann::json::array();
                   for (const auto &ship : random_fleet(rng)) ships.push_back(ship_to_json(ship));
                   reply(res, {{"ships", ships}});
               }));

    server.Post("/api/rooms", guarded([&rooms](const httplib::Request &req, httplib::Response &res) {
                    const auto body = parse_body(req);
                    reply(res, joined_json(rooms.create_room(get_string(body, "name"))));
                }));

    server.Post(R"(/api/rooms/([A-Za-z0-9]{1,16})/join)", guarded([&rooms](const httplib::Request &req, httplib::Response &res) {
                    const auto body = parse_body(req);
                    reply(res, joined_json(rooms.join_room(req.matches[1], get_string(body, "name"))));
                }));

    server.Get("/api/me", guarded([&rooms](const httplib::Request &req, httplib::Response &res) {
                   const auto info = rooms.me(req.get_param_value("token"));
                   reply(res, {{"room", info.room}, {"player", info.player}, {"name", info.name}});
               }));

    server.Post("/api/place", guarded([&rooms](const httplib::Request &req, httplib::Response &res) {
                    const auto body = parse_body(req);
                    const auto token = get_string(body, "token");
                    if (!body.contains("ships")) throw RoomError(400, "Нет поля ships");
                    rooms.place(token, fleet_from_json(body["ships"]));
                    reply(res, {{"ok", true}});
                }));

    server.Post("/api/fire", guarded([&rooms](const httplib::Request &req, httplib::Response &res) {
                    const auto body = parse_body(req);
                    const auto token = get_string(body, "token");
                    const Coord at{get_int_field(body, "x"), get_int_field(body, "y")};
                    reply(res, {{"result", shot_result_name(rooms.fire(token, at))}});
                }));

    const auto simple = [&server, &rooms](const char *path, void (RoomManager::*action)(const std::string &)) {
        server.Post(path, guarded([&rooms, action](const httplib::Request &req, httplib::Response &res) {
                        const auto body = parse_body(req);
                        (rooms.*action)(get_string(body, "token"));
                        reply(res, {{"ok", true}});
                    }));
    };
    simple("/api/resign", &RoomManager::resign);
    simple("/api/rematch", &RoomManager::rematch);
    simple("/api/leave", &RoomManager::leave);

    server.Get("/api/events", guarded([&rooms, config](const httplib::Request &req, httplib::Response &res) {
                   const auto token = req.get_param_value("token");
                   const auto stream = rooms.open_stream(token);
                   auto seen = std::make_shared<std::uint64_t>(0);
                   res.set_header("Cache-Control", "no-store");
                   res.set_header("X-Accel-Buffering", "no");
                   res.set_chunked_content_provider(
                       "text/event-stream",
                       [&rooms, token, stream, seen, config](std::size_t, httplib::DataSink &sink) {
                           // исключение из провайдера httplib не перехватывает: оно завершило бы весь процесс
                           try {
                               nlohmann::json state;
                               std::string chunk;
                               switch (rooms.wait_event(token, stream, *seen, state, config.ping)) {
                                   case StreamEvent::STATE:
                                       chunk = "event: state\ndata: " + state.dump() + "\n\n";
                                       break;
                                   case StreamEvent::PING:
                                       chunk = ": ping\n\n";
                                       break;
                                   case StreamEvent::REPLACED:
                                       chunk = "event: closed\ndata: {\"reason\":\"replaced\"}\n\n";
                                       sink.write(chunk.data(), chunk.size());
                                       sink.done();
                                       return true;
                                   case StreamEvent::CLOSED:
                                       sink.done();
                                       return true;
                               }
                               return sink.write(chunk.data(), chunk.size());
                           } catch (const std::exception &e) {
                               std::cerr << "поток событий: " << e.what() << '\n';
                               sink.done();
                               return true;
                           }
                       },
                       [&rooms, token, stream](bool) { rooms.close_stream(token, stream); });
               }));
}

} // namespace seabattle::server
