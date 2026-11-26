#pragma once

#include "access_dialog.hpp"
#include "app.hpp"

#include <sdbus-c++/sdbus-c++.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct pw_context;
struct pw_core;
struct pw_loop;
struct wl_shm;
struct zwlr_screencopy_manager_v1;

namespace tetra::portal {

class OutputStream;

// org.freedesktop.impl.portal.ScreenCast: shares a display with an app as a
// PipeWire video stream. The user picks the display in an access dialog;
// frames come from wlr-screencopy (when the screen changes, at most 30 per
// second), with or without the pointer. A notification says the screen is
// being shared for as long as it is.
class ScreenCast {
public:
    using Options = std::map<std::string, sdbus::Variant>;
    // The Start reply: response and results ({"streams": ...}).
    using StartDone = std::function<void(uint32_t, Options)>;

    ScreenCast(ui::App& app, sdbus::IConnection& bus);
    ~ScreenCast();

    ScreenCast(const ScreenCast&) = delete;
    ScreenCast& operator=(const ScreenCast&) = delete;
    ScreenCast(ScreenCast&&) = delete;
    ScreenCast& operator=(ScreenCast&&) = delete;

    void create_session(const sdbus::ObjectPath& path, const std::string& app_id);
    // false: no such session.
    bool select_sources(const std::string& session, const Options& options);
    void start(const std::string& session, const std::string& parent, StartDone done);
    // The request behind Start was closed: drop its dialog.
    void cancel_start(const std::string& session);

    static constexpr uint32_t kMonitor = 1;
    static constexpr uint32_t kCursorHidden = 1;
    static constexpr uint32_t kCursorEmbedded = 2;

private:
    struct Session {
        std::unique_ptr<sdbus::IObject> object; // org.freedesktop.impl.portal.Session
        std::string app_id;
        bool multiple = false;
        uint32_t cursor_mode = kCursorHidden;
        bool started = false;
        std::unique_ptr<AccessDialog> dialog;
        StartDone done;
        std::vector<std::unique_ptr<OutputStream>> streams;
        std::optional<uint32_t> notification;
    };

    bool ensure_pipewire();
    void begin_streams(const std::string& session, const std::vector<const ui::OutputInfo*>& outputs);
    void stream_ready(const std::string& session);
    // Ends a session (the app closed it, or it broke): its streams, its
    // notification; `signal`: tell the app (Session.Closed).
    void close(const std::string& session, bool signal);
    void notify(Session& session, const std::string& displays);

    ui::App& app_;
    sdbus::IConnection& bus_;
    std::unique_ptr<sdbus::IProxy> notifications_;
    zwlr_screencopy_manager_v1* screencopy_ = nullptr;
    wl_shm* shm_ = nullptr;
    pw_loop* loop_ = nullptr;
    pw_context* context_ = nullptr;
    pw_core* core_ = nullptr;
    int loop_fd_ = -1;
    std::map<std::string, std::unique_ptr<Session>> sessions_;
};

} // namespace tetra::portal
