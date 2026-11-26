#pragma once

#include "app.hpp"
#include "access_dialog.hpp"
#include "appearance.hpp"
#include "file_dialog.hpp"
#include "files.hpp"
#include "screencast.hpp"

#include <sdbus-c++/sdbus-c++.h>

#include <map>
#include <memory>
#include <string>

namespace tetra::portal {

// tetra-portal: the xdg-desktop-portal backend
// (org.freedesktop.impl.portal.desktop.tetra) for
//   Settings     appearance from config.json (appearance.hpp), live
//   Screenshot   taken by tetra-shell (org.tetra.Shell1), as Print does
//   FileChooser  tetra's own file dialog (file_dialog.hpp)
//   Access       allow/deny questions (access_dialog.hpp; xdg-desktop-portal
//                offers Screenshot only with an Access backend)
//   ScreenCast   a display as a PipeWire video stream (screencast.hpp)
// xdg-desktop-portal finds it through tetra.portal and tetra-portals.conf.
class Portal {
public:
    Portal(ui::App& app, sdbus::IConnection& bus);
    ~Portal();

    Portal(const Portal&) = delete;
    Portal& operator=(const Portal&) = delete;
    Portal(Portal&&) = delete;
    Portal& operator=(Portal&&) = delete;

    static constexpr const char* kBusName = "org.freedesktop.impl.portal.desktop.tetra";

private:
    using Options = std::map<std::string, sdbus::Variant>;
    using Response = sdbus::Result<uint32_t, Options>;
    using Settings = std::map<std::string, std::map<std::string, sdbus::Variant>>;

    // One portal call in progress, with its org.freedesktop.impl.portal.Request.
    struct Request {
        std::unique_ptr<sdbus::IObject> object;
        std::shared_ptr<Response> reply;
        std::unique_ptr<FileDialog> dialog;
        std::unique_ptr<AccessDialog> access;
        std::function<void()> on_close; // the app gave up (Request.Close)
        bool answered = false;
    };

    void add_request(const sdbus::ObjectPath& handle, Response&& reply);
    // Replies (once) and drops the request on the next turn of the loop.
    void answer(const std::string& handle, uint32_t response, Options results = {});

    void open_dialog(Response&& reply, const sdbus::ObjectPath& handle, const std::string& title,
                     const std::string& parent, const Options& options, DialogOptions::Mode mode);
    void screenshot(Response&& reply, const sdbus::ObjectPath& handle, const Options& options);
    void pick_color(Response&& reply, const sdbus::ObjectPath& handle);
    void access_dialog(Response&& reply, const sdbus::ObjectPath& handle, AccessOptions access,
                       const Options& options);

    [[nodiscard]] Settings settings() const;
    void config_changed();

    ui::App& app_;
    sdbus::IConnection& bus_;
    std::unique_ptr<sdbus::IObject> object_;
    std::unique_ptr<sdbus::IProxy> shell_;
    MimeGlobs mime_;
    Appearance appearance_;
    fs::path last_folder_; // where the last dialog ended
    std::map<std::string, std::unique_ptr<Request>> requests_;
    std::unique_ptr<ScreenCast> screencast_;
};

} // namespace tetra::portal
