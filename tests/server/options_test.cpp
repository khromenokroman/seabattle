#include "options.hpp"

#include <gtest/gtest.h>

#include <vector>

using namespace seabattle::server;

namespace {

Options parse(std::vector<const char *> args) {
    args.insert(args.begin(), "seabattle");
    return parse_options(static_cast<int>(args.size()), args.data());
}

} // namespace

// Без параметров: значения по умолчанию
TEST(Options, Defaults) {
    const auto o = parse({});
    EXPECT_EQ(o.host, "0.0.0.0");
    EXPECT_EQ(o.port, 8090);
    EXPECT_EQ(o.web, SEABATTLE_WEB_DIR);
    EXPECT_FALSE(o.help);
    EXPECT_FALSE(o.version);
}

// Все параметры разбираются
TEST(Options, AllValues) {
    const auto o = parse({"--host", "127.0.0.1", "--port", "18090", "--web", "/tmp/web", "--help", "--version"});
    EXPECT_EQ(o.host, "127.0.0.1");
    EXPECT_EQ(o.port, 18090);
    EXPECT_EQ(o.web, "/tmp/web");
    EXPECT_TRUE(o.help);
    EXPECT_TRUE(o.version);
}

// Неправильный порт, неизвестный параметр и параметр без значения отклоняются
TEST(Options, Rejects) {
    const std::vector<std::vector<const char *>> cases = {{"--port", "0"}, {"--port", "65536"}, {"--port", "80x"},
                                                          {"--port", ""},  {"--bogus"},         {"--host"}};
    for (const auto &args : cases) {
        std::string text;
        for (const auto *a : args) text += std::string(a) + ' ';
        SCOPED_TRACE("аргументы: " + text);
        EXPECT_THROW(parse(args), std::invalid_argument) << "ждали invalid_argument";
    }
}

// Справка содержит все параметры
TEST(Options, Usage) {
    const auto text = usage("seabattle");
    for (const char *opt : {"--host", "--port", "--web", "--version", "--help"}) {
        EXPECT_NE(text.find(opt), std::string::npos) << "в справке нет " << opt;
    }
}
