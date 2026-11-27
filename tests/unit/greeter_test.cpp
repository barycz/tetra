// Unit tests for the greeter's pieces (greetd framing and messages, the
// user list, the remembered state) and the desktop-file lists it and the
// compositor use (sessions, XDG autostart).

#include "desktop_entry.hpp"
#include "protocol.hpp"
#include "users.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <print>
#include <source_location>
#include <string>

#include <unistd.h>

namespace {

int failures = 0;

void check(bool ok, std::string_view what, std::source_location where = std::source_location::current()) {
    if (!ok) {
        ++failures;
        std::println(stderr, "FAIL greeter_test.cpp line {}: {}", where.line(), what);
    }
}

void write(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

using namespace tetra::greeter;

void framing() {
    std::string stream = frame(R"({"type":"success"})") + frame(R"({"type":"error"})");
    const std::string second_half = stream.substr(stream.size() - 5);
    stream.resize(stream.size() - 5); // the second message is incomplete
    auto messages = unframe(stream);
    check(messages.size() == 1 && messages[0] == R"({"type":"success"})", "first message");
    check(stream.size() == 4 + 16 - 5, "partial message kept");
    stream += second_half;
    messages = unframe(stream);
    check(messages.size() == 1 && messages[0] == R"({"type":"error"})" && stream.empty(), "completed");
    uint32_t length = 0;
    const std::string framed = frame("abc");
    std::memcpy(&length, framed.data(), 4);
    check(length == 3 && framed.substr(4) == "abc", "native-endian length");
}

void replies() {
    Reply r = parse_reply(R"({"type":"auth_message","auth_message_type":"secret","auth_message":"Password:"})");
    check(r.type == Reply::Type::AuthMessage && r.prompt == Reply::Prompt::Secret && r.message == "Password:",
          "secret prompt");
    r = parse_reply(R"({"type":"auth_message","auth_message_type":"visible","auth_message":"Code:"})");
    check(r.prompt == Reply::Prompt::Visible, "visible prompt");
    r = parse_reply(R"({"type":"error","error_type":"auth_error","description":"bad"})");
    check(r.type == Reply::Type::Error && r.error_type == "auth_error" && r.description == "bad", "error");
    check(parse_reply(R"({"type":"success"})").type == Reply::Type::Success, "success");
    check(parse_reply("not json").type == Reply::Type::Invalid, "garbage");
    check(parse_reply(R"({"type":"auth_message","auth_message_type":"odd"})").type == Reply::Type::Invalid,
          "unknown prompt type");

    check(create_session("alice") == R"({"type":"create_session","username":"alice"})", "create_session");
    check(post_auth_message_response(std::nullopt) == R"({"type":"post_auth_message_response"})",
          "acknowledgement has no response");
    check(post_auth_message_response("p\"w") == R"({"response":"p\"w","type":"post_auth_message_response"})",
          "response escaped");
    check(start_session({"tetra"}, {"A=b"}) == R"({"cmd":["tetra"],"env":["A=b"],"type":"start_session"})",
          "start_session");
}

void users(const std::filesystem::path& root) {
    write(root / "login.defs", "# comment\nUID_MIN\t\t 2000\nUID_MAX 3000\nOTHER 1\n");
    const UidRange range = read_uid_range(root / "login.defs");
    check(range.min == 2000 && range.max == 3000, "UID range");
    check(read_uid_range(root / "missing").min == 1000, "default range");

    write(root / "passwd", "root:x:0:0:root:/root:/bin/bash\n"
                           "zed:x:2001:2001::/home/zed:/bin/bash\n"
                           "amy:x:2002:2002:Amy Pond,,,:/home/amy:/bin/zsh\n"
                           "svc:x:2003:2003:Service:/srv:/usr/sbin/nologin\n"
                           "off:x:2004:2004::/:/bin/false\n"
                           "big:x:4000:4000::/home/big:/bin/bash\n"
                           "broken line\n");
    const auto list = read_users(root / "passwd", range);
    check(list.size() == 2, "people only");
    check(list.size() == 2 && list[0].name == "amy" && list[0].display_name == "Amy Pond" && list[0].uid == 2002,
          "GECOS name, sorted");
    check(list.size() == 2 && list[1].display_name == "zed", "no GECOS: login name");
}

void state(const std::filesystem::path& root) {
    const auto path = root / "state/tetra/greeter.json";
    check(load_state(path) == State{}, "no file");
    check(save_state(path, {.user = "amy", .session = "tetra"}), "saved");
    check(load_state(path) == State{.user = "amy", .session = "tetra"}, "round trip");
    write(path, "{broken");
    check(load_state(path) == State{}, "broken file");
}

void sessions_and_autostart(const std::filesystem::path& root) {
    const auto system = root / "share/wayland-sessions";
    write(system / "tetra.desktop", "[Desktop Entry]\nType=Application\nName=tetra\nExec=tetra\nDesktopNames=tetra\n");
    write(system / "sway.desktop", "[Desktop Entry]\nType=Application\nName=Sway\nExec=sway --unsupported-gpu\n"
                                   "DesktopNames=sway;wlroots\n");
    write(system / "hidden.desktop", "[Desktop Entry]\nType=Application\nName=Hidden\nExec=x\nNoDisplay=true\n");
    write(system / "gone.desktop", "[Desktop Entry]\nType=Application\nName=Gone\nExec=x\nTryExec=/nonexistent\n");
    const auto sessions = tetra::session_entries({system});
    check(sessions.size() == 2, "NoDisplay and missing TryExec left out");
    check(sessions.size() == 2 && sessions[0].id == "sway" && sessions[1].id == "tetra", "sorted by name");
    check(sessions.size() == 2 && sessions[0].desktop_names == std::vector<std::string>{"sway", "wlroots"},
          "DesktopNames");
    check(sessions.size() == 2 && tetra::exec_argv(sessions[0], "") ==
                                      std::vector<std::string>{"sway", "--unsupported-gpu"},
          "Exec split");

    const auto user = root / "config/autostart";
    const auto xdg = root / "xdg/autostart";
    write(xdg / "agent.desktop", "[Desktop Entry]\nType=Application\nName=Agent\nExec=agent\n");
    write(xdg / "masked.desktop", "[Desktop Entry]\nType=Application\nName=Masked\nExec=masked\n");
    write(user / "masked.desktop", "[Desktop Entry]\nType=Application\nName=Masked\nExec=masked\nHidden=true\n");
    write(xdg / "gnome-only.desktop", "[Desktop Entry]\nType=Application\nName=G\nExec=g\nOnlyShowIn=GNOME;\n");
    write(xdg / "for-tetra.desktop", "[Desktop Entry]\nType=Application\nName=T\nExec=t\nOnlyShowIn=GNOME;tetra;\n");
    write(xdg / "not-here.desktop", "[Desktop Entry]\nType=Application\nName=N\nExec=n\nNotShowIn=tetra;\n");
    write(xdg / "off.desktop", "[Desktop Entry]\nType=Application\nName=Off\nExec=off\nX-GNOME-Autostart-enabled=false\n");
    write(xdg / "invisible.desktop", "[Desktop Entry]\nType=Application\nName=I\nExec=i\nNoDisplay=true\n");
    auto ids = [](const std::vector<tetra::DesktopEntry>& entries) {
        std::vector<std::string> out;
        for (const auto& e : entries) {
            out.push_back(e.id);
        }
        std::ranges::sort(out);
        return out;
    };
    check(ids(tetra::autostart_entries({user, xdg})) == std::vector<std::string>{"agent", "for-tetra", "invisible"},
          "autostart: Hidden masks, OnlyShowIn/NotShowIn, disabled");
}

} // namespace

int greeter_tests() {
    const auto root = std::filesystem::temp_directory_path() / std::format("tetra-greeter-test-{}", ::getpid());
    std::filesystem::remove_all(root);
    framing();
    replies();
    users(root);
    state(root);
    sessions_and_autostart(root);
    std::filesystem::remove_all(root);
    return failures;
}
