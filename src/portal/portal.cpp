#include "portal.hpp"

#include <print>
#include <tuple>

namespace tetra::portal {

namespace {

constexpr const char* kPath = "/org/freedesktop/portal/desktop";
constexpr const char* kSettings = "org.freedesktop.impl.portal.Settings";
constexpr const char* kScreenshot = "org.freedesktop.impl.portal.Screenshot";
constexpr const char* kFileChooser = "org.freedesktop.impl.portal.FileChooser";
constexpr const char* kRequest = "org.freedesktop.impl.portal.Request";
constexpr const char* kAccess = "org.freedesktop.impl.portal.Access";
constexpr const char* kScreenCast = "org.freedesktop.impl.portal.ScreenCast";
constexpr const char* kNotFound = "org.freedesktop.portal.Error.NotFound";
constexpr const char* kAppearance = "org.freedesktop.appearance";
constexpr const char* kGnome = "org.gnome.desktop.interface";

// Portal responses.
constexpr uint32_t kSuccess = 0;
constexpr uint32_t kCancelled = 1;
constexpr uint32_t kOther = 2;

using FilterRule = sdbus::Struct<uint32_t, std::string>;
using FilterType = sdbus::Struct<std::string, std::vector<FilterRule>>;
using ChoiceOption = sdbus::Struct<std::string, std::string>;
using ChoiceType = sdbus::Struct<std::string, std::string, std::vector<ChoiceOption>, std::string>;

template <typename T>
std::optional<T> option(const std::map<std::string, sdbus::Variant>& options, const char* key) {
    auto it = options.find(key);
    if (it == options.end() || !it->second.containsValueOfType<T>()) {
        return std::nullopt;
    }
    return it->second.get<T>();
}

Filter to_filter(const FilterType& f) {
    Filter filter{.name = std::get<0>(f), .rules = {}};
    for (const auto& rule : std::get<1>(f)) {
        filter.rules.emplace_back(std::get<0>(rule), std::get<1>(rule));
    }
    return filter;
}

FilterType from_filter(const Filter& filter) {
    std::vector<FilterRule> rules;
    for (const auto& [kind, value] : filter.rules) {
        rules.emplace_back(kind, value);
    }
    return FilterType{filter.name, rules};
}

std::vector<Choice> parse_choices(const std::map<std::string, sdbus::Variant>& options) {
    std::vector<Choice> out;
    for (const auto& c : option<std::vector<ChoiceType>>(options, "choices").value_or(std::vector<ChoiceType>{})) {
        Choice choice{.id = std::get<0>(c), .label = std::get<1>(c), .options = {}, .selected = std::get<3>(c)};
        for (const auto& opt : std::get<2>(c)) {
            choice.options.emplace_back(std::get<0>(opt), std::get<1>(opt));
        }
        if (choice.options.empty() && choice.selected != "true") {
            choice.selected = "false";
        } else if (!choice.options.empty() &&
                   std::ranges::none_of(choice.options, [&](const auto& kv) { return kv.first == choice.selected; })) {
            choice.selected = choice.options.front().first;
        }
        out.push_back(std::move(choice));
    }
    return out;
}

sdbus::Variant choices_variant(const std::vector<Choice>& choices) {
    std::vector<ChoiceOption> out;
    for (const auto& c : choices) {
        out.emplace_back(c.id, c.selected);
    }
    return sdbus::Variant(out);
}

// "wayland:HANDLE" -> HANDLE (an xdg-foreign v2 handle); X11 parents and
// none -> "".
std::string wayland_parent(const std::string& parent_window) {
    constexpr std::string_view kPrefix = "wayland:";
    return parent_window.starts_with(kPrefix) ? parent_window.substr(kPrefix.size()) : std::string();
}

// "org.freedesktop.*" matches by prefix; no namespaces, or "", all of them.
bool wanted(const std::string& ns, const std::vector<std::string>& patterns) {
    if (patterns.empty()) {
        return true;
    }
    for (const auto& p : patterns) {
        if (p.empty() || p == ns || (p.ends_with(".*") && ns.starts_with(p.substr(0, p.size() - 1)))) {
            return true;
        }
    }
    return false;
}

} // namespace

Portal::Portal(ui::App& app, sdbus::IConnection& bus) : app_(app), bus_(bus) {
    mime_.load("/usr/share/mime");
    appearance_ = appearance(app_.config().theme);
    shell_ = sdbus::createProxy(bus_, sdbus::ServiceName{"org.tetra.Shell"}, sdbus::ObjectPath{"/org/tetra/Shell"});

    object_ = sdbus::createObject(bus_, sdbus::ObjectPath{kPath});
    object_
        ->addVTable(sdbus::registerMethod("ReadAll")
                        .withInputParamNames("namespaces")
                        .withOutputParamNames("value")
                        .implementedAs([this](const std::vector<std::string>& namespaces) {
                            Settings all = settings();
                            std::erase_if(all, [&](const auto& kv) { return !wanted(kv.first, namespaces); });
                            return all;
                        }),
                    sdbus::registerMethod("Read")
                        .withInputParamNames("namespace", "key")
                        .withOutputParamNames("value")
                        .implementedAs([this](const std::string& ns, const std::string& key) {
                            const Settings all = settings();
                            auto group = all.find(ns);
                            if (group != all.end()) {
                                if (auto it = group->second.find(key); it != group->second.end()) {
                                    return it->second;
                                }
                            }
                            throw sdbus::Error(sdbus::Error::Name{kNotFound}, "No such setting");
                        }),
                    sdbus::registerSignal("SettingChanged")
                        .withParameters<std::string, std::string, sdbus::Variant>("namespace", "key", "value"),
                    sdbus::registerProperty("version").withGetter([] { return uint32_t{2}; }))
        .forInterface(sdbus::InterfaceName{kSettings});

    object_
        ->addVTable(
            sdbus::registerMethod("Screenshot")
                .withInputParamNames("handle", "app_id", "parent_window", "options")
                .withOutputParamNames("response", "results")
                .implementedAs([this](Response&& reply, const sdbus::ObjectPath& handle,
                                      const std::string& /*app_id*/, const std::string& /*parent*/,
                                      const Options& options) { screenshot(std::move(reply), handle, options); }),
            sdbus::registerMethod("PickColor")
                .withInputParamNames("handle", "app_id", "parent_window", "options")
                .withOutputParamNames("response", "results")
                .implementedAs([this](Response&& reply, const sdbus::ObjectPath& handle,
                                      const std::string& /*app_id*/, const std::string& /*parent*/,
                                      const Options& /*options*/) { pick_color(std::move(reply), handle); }),
            sdbus::registerProperty("version").withGetter([] { return uint32_t{2}; }))
        .forInterface(sdbus::InterfaceName{kScreenshot});

    const auto chooser = [this](DialogOptions::Mode mode) {
        return [this, mode](Response&& reply, const sdbus::ObjectPath& handle, const std::string& /*app_id*/,
                            const std::string& parent, const std::string& title, const Options& options) {
            open_dialog(std::move(reply), handle, title, wayland_parent(parent), options, mode);
        };
    };
    object_
        ->addVTable(sdbus::registerMethod("OpenFile")
                        .withInputParamNames("handle", "app_id", "parent_window", "title", "options")
                        .withOutputParamNames("response", "results")
                        .implementedAs(chooser(DialogOptions::Mode::Open)),
                    sdbus::registerMethod("SaveFile")
                        .withInputParamNames("handle", "app_id", "parent_window", "title", "options")
                        .withOutputParamNames("response", "results")
                        .implementedAs(chooser(DialogOptions::Mode::Save)),
                    sdbus::registerMethod("SaveFiles")
                        .withInputParamNames("handle", "app_id", "parent_window", "title", "options")
                        .withOutputParamNames("response", "results")
                        .implementedAs(chooser(DialogOptions::Mode::SaveFiles)),
                    sdbus::registerProperty("version").withGetter([] { return uint32_t{4}; }))
        .forInterface(sdbus::InterfaceName{kFileChooser});

    object_
        ->addVTable(sdbus::registerMethod("AccessDialog")
                        .withInputParamNames("handle", "app_id", "parent_window", "title", "subtitle", "body",
                                             "options")
                        .withOutputParamNames("response", "results")
                        .implementedAs([this](Response&& reply, const sdbus::ObjectPath& handle,
                                              const std::string& /*app_id*/, const std::string& parent,
                                              std::string title, std::string subtitle, std::string body,
                                              const Options& options) {
                            access_dialog(std::move(reply), handle,
                                          AccessOptions{.title = std::move(title),
                                                        .subtitle = std::move(subtitle),
                                                        .body = std::move(body),
                                                        .icon = {},
                                                        .grant_label = {},
                                                        .deny_label = {},
                                                        .choices = {},
                                                        .parent = wayland_parent(parent)},
                                          options);
                        }))
        .forInterface(sdbus::InterfaceName{kAccess});

    screencast_ = std::make_unique<ScreenCast>(app_, bus_);
    object_
        ->addVTable(
            sdbus::registerMethod("CreateSession")
                .withInputParamNames("handle", "session_handle", "app_id", "options")
                .withOutputParamNames("response", "results")
                .implementedAs([this](const sdbus::ObjectPath& /*handle*/, const sdbus::ObjectPath& session,
                                      const std::string& app_id, const Options& /*options*/) {
                    screencast_->create_session(session, app_id);
                    return std::tuple<uint32_t, Options>{
                        kSuccess, Options{{"session_id", sdbus::Variant(std::string(session))}}};
                }),
            sdbus::registerMethod("SelectSources")
                .withInputParamNames("handle", "session_handle", "app_id", "options")
                .withOutputParamNames("response", "results")
                .implementedAs([this](const sdbus::ObjectPath& /*handle*/, const sdbus::ObjectPath& session,
                                      const std::string& /*app_id*/, const Options& options) {
                    const bool ok = screencast_->select_sources(session, options);
                    return std::tuple<uint32_t, Options>{ok ? kSuccess : kOther, Options{}};
                }),
            sdbus::registerMethod("Start")
                .withInputParamNames("handle", "session_handle", "app_id", "parent_window", "options")
                .withOutputParamNames("response", "results")
                .implementedAs([this](Response&& reply, const sdbus::ObjectPath& handle,
                                      const sdbus::ObjectPath& session, const std::string& /*app_id*/,
                                      const std::string& parent, const Options& /*options*/) {
                    add_request(handle, std::move(reply));
                    const std::string key = handle;
                    const std::string session_key = session;
                    requests_.at(key)->on_close = [this, session_key] { screencast_->cancel_start(session_key); };
                    screencast_->start(session_key, wayland_parent(parent),
                                       [this, key](uint32_t response, Options results) {
                                           answer(key, response, std::move(results));
                                       });
                }),
            sdbus::registerProperty("AvailableSourceTypes").withGetter([] { return ScreenCast::kMonitor; }),
            sdbus::registerProperty("AvailableCursorModes").withGetter([] {
                return ScreenCast::kCursorHidden | ScreenCast::kCursorEmbedded;
            }),
            sdbus::registerProperty("version").withGetter([] { return uint32_t{5}; }))
        .forInterface(sdbus::InterfaceName{kScreenCast});

    app_.on_config_changed = [this] { config_changed(); };
}

Portal::~Portal() {
    screencast_.reset(); // its streams and sessions, while the bus is there
    // Unanswered calls end as "other" (2): the backend is going away.
    for (auto& [handle, request] : requests_) {
        if (!request->answered) {
            request->answered = true;
            try {
                request->reply->returnResults(kOther, Options{});
            } catch (const sdbus::Error&) {
                // the bus is gone
            }
        }
    }
}

// --- requests -----------------------------------------------------------------------------

void Portal::add_request(const sdbus::ObjectPath& handle, Response&& reply) {
    auto request = std::make_unique<Request>();
    request->reply = std::make_shared<Response>(std::move(reply));
    request->object = sdbus::createObject(bus_, handle);
    const std::string key = handle;
    request->object
        ->addVTable(sdbus::registerMethod("Close").implementedAs([this, key] {
            // The app gave up: close what is open, answer "other".
            auto it = requests_.find(key);
            if (it != requests_.end() && it->second->dialog) {
                it->second->dialog->cancel();
            }
            if (it != requests_.end() && it->second->access) {
                it->second->access->cancel();
            }
            if (it != requests_.end() && it->second->on_close) {
                it->second->on_close();
            }
            answer(key, kOther);
        }))
        .forInterface(sdbus::InterfaceName{kRequest});
    requests_[key] = std::move(request);
}

void Portal::answer(const std::string& handle, uint32_t response, Options results) {
    auto it = requests_.find(handle);
    if (it == requests_.end() || it->second->answered) {
        return;
    }
    it->second->answered = true;
    it->second->reply->returnResults(response, std::move(results));
    // Not now: this may run inside the dialog's own draw callback.
    app_.add_timer(std::chrono::milliseconds(0), [this, handle] { requests_.erase(handle); });
}

// --- FileChooser --------------------------------------------------------------------------------

void Portal::open_dialog(Response&& reply, const sdbus::ObjectPath& handle, const std::string& title,
                         const std::string& parent, const Options& options, DialogOptions::Mode mode) {
    add_request(handle, std::move(reply));
    DialogOptions o;
    o.mode = mode;
    o.title = title;
    o.parent = parent;
    o.accept_label = option<std::string>(options, "accept_label").value_or("");
    o.multiple = option<bool>(options, "multiple").value_or(false);
    o.directory = option<bool>(options, "directory").value_or(false);
    for (const auto& f : option<std::vector<FilterType>>(options, "filters").value_or(std::vector<FilterType>{})) {
        o.filters.push_back(to_filter(f));
    }
    if (auto current = option<FilterType>(options, "current_filter")) {
        const Filter filter = to_filter(*current);
        auto it = std::ranges::find(o.filters, filter);
        if (it != o.filters.end()) {
            o.filter = static_cast<std::size_t>(it - o.filters.begin());
        } else if (o.filters.empty()) {
            o.filters.push_back(filter); // applied without a choice
        }
    }
    o.choices = parse_choices(options);
    o.current_name = option<std::string>(options, "current_name").value_or("");
    if (auto folder = option<std::vector<uint8_t>>(options, "current_folder")) {
        o.folder = path_from_bytes(*folder).value_or(fs::path());
    }
    if (auto file = option<std::vector<uint8_t>>(options, "current_file")) {
        if (auto path = path_from_bytes(*file)) {
            o.folder = path->parent_path();
            o.current_name = path->filename().string();
        }
    }
    for (const auto& file : option<std::vector<std::vector<uint8_t>>>(options, "files")
                                .value_or(std::vector<std::vector<uint8_t>>{})) {
        if (auto path = path_from_bytes(file)) {
            o.files.push_back(path->filename().string());
        }
    }
    if (o.folder.empty()) {
        o.folder = last_folder_;
    }

    const std::string key = handle;
    auto& request = *requests_.at(key);
    request.dialog = std::make_unique<FileDialog>(app_, mime_, std::move(o), [this, key](DialogResult result) {
        if (auto it = requests_.find(key); it != requests_.end() && it->second->dialog) {
            last_folder_ = it->second->dialog->folder();
        }
        if (!result.accepted) {
            answer(key, kCancelled);
            return;
        }
        Options results;
        std::vector<std::string> uris;
        for (const auto& path : result.paths) {
            uris.push_back(file_uri(path));
        }
        results.emplace("uris", sdbus::Variant(uris));
        if (!result.choices.empty()) {
            results.emplace("choices", choices_variant(result.choices));
        }
        if (result.filter) {
            results.emplace("current_filter", sdbus::Variant(from_filter(*result.filter)));
        }
        answer(key, kSuccess, std::move(results));
    });
}

// --- Access ------------------------------------------------------------------------------------

void Portal::access_dialog(Response&& reply, const sdbus::ObjectPath& handle, AccessOptions access,
                           const Options& options) {
    add_request(handle, std::move(reply));
    access.icon = option<std::string>(options, "icon").value_or("");
    access.grant_label = option<std::string>(options, "grant_label").value_or("");
    access.deny_label = option<std::string>(options, "deny_label").value_or("");
    access.choices = parse_choices(options);
    const std::string key = handle;
    requests_.at(key)->access = std::make_unique<AccessDialog>(
        app_, std::move(access), [this, key](bool granted, std::vector<Choice> choices) {
            Options results;
            if (!choices.empty()) {
                results.emplace("choices", choices_variant(choices));
            }
            answer(key, granted ? kSuccess : kCancelled, std::move(results));
        });
}

// --- Screenshot --------------------------------------------------------------------------------

void Portal::screenshot(Response&& reply, const sdbus::ObjectPath& handle, const Options& options) {
    add_request(handle, std::move(reply));
    const bool interactive = option<bool>(options, "interactive").value_or(false);
    const std::string key = handle;
    try {
        shell_->callMethodAsync("Screenshot")
            .onInterface(sdbus::InterfaceName{"org.tetra.Shell1"})
            .withTimeout(std::chrono::minutes(10)) // the user may take a while to pick an area
            .withArguments(interactive)
            .uponReplyInvoke([this, key](std::optional<sdbus::Error> error, std::string path) {
                if (error) {
                    const bool cancelled = error->getName() == "org.tetra.Shell1.Error.Cancelled";
                    if (!cancelled) {
                        std::println(stderr, "tetra-portal: screenshot: {}", error->getMessage());
                    }
                    answer(key, cancelled ? kCancelled : kOther);
                    return;
                }
                answer(key, kSuccess, Options{{"uri", sdbus::Variant(file_uri(path))}});
            });
    } catch (const sdbus::Error& e) {
        std::println(stderr, "tetra-portal: screenshot: {}", e.getMessage());
        answer(key, kOther);
    }
}

void Portal::pick_color(Response&& reply, const sdbus::ObjectPath& handle) {
    add_request(handle, std::move(reply));
    const std::string key = handle;
    try {
        shell_->callMethodAsync("PickColor")
            .onInterface(sdbus::InterfaceName{"org.tetra.Shell1"})
            .withTimeout(std::chrono::minutes(10))
            .uponReplyInvoke([this, key](std::optional<sdbus::Error> error, sdbus::Struct<double, double, double> c) {
                if (error) {
                    const bool cancelled = error->getName() == "org.tetra.Shell1.Error.Cancelled";
                    if (!cancelled) {
                        std::println(stderr, "tetra-portal: pick colour: {}", error->getMessage());
                    }
                    answer(key, cancelled ? kCancelled : kOther);
                    return;
                }
                answer(key, kSuccess, Options{{"color", sdbus::Variant(c)}});
            });
    } catch (const sdbus::Error& e) {
        std::println(stderr, "tetra-portal: pick colour: {}", e.getMessage());
        answer(key, kOther);
    }
}

// --- Settings ----------------------------------------------------------------------------------

Portal::Settings Portal::settings() const {
    const Appearance& a = appearance_;
    Settings all;
    all[kAppearance] = {
        {"color-scheme", sdbus::Variant(a.color_scheme)},
        {"accent-color", sdbus::Variant(sdbus::Struct<double, double, double>{a.accent[0], a.accent[1], a.accent[2]})},
        {"contrast", sdbus::Variant(uint32_t{0})},
    };
    all[kGnome] = {
        {"color-scheme", sdbus::Variant(a.gnome_color_scheme)},
        {"gtk-theme", sdbus::Variant(a.gtk_theme)},
        {"accent-color", sdbus::Variant(a.gnome_accent)},
        {"font-name", sdbus::Variant(a.font_name)},
    };
    return all;
}

void Portal::config_changed() {
    const Appearance before = appearance_;
    appearance_ = appearance(app_.config().theme);
    if (before == appearance_) {
        return;
    }
    const Settings now = settings();
    const auto emit = [&](const char* ns, const char* key) {
        object_->emitSignal("SettingChanged")
            .onInterface(sdbus::InterfaceName{kSettings})
            .withArguments(std::string(ns), std::string(key), now.at(ns).at(key));
    };
    if (before.color_scheme != appearance_.color_scheme) {
        emit(kAppearance, "color-scheme");
        emit(kGnome, "color-scheme");
    }
    if (before.accent != appearance_.accent) {
        emit(kAppearance, "accent-color");
    }
    if (before.gtk_theme != appearance_.gtk_theme) {
        emit(kGnome, "gtk-theme");
    }
    if (before.gnome_accent != appearance_.gnome_accent) {
        emit(kGnome, "accent-color");
    }
    if (before.font_name != appearance_.font_name) {
        emit(kGnome, "font-name");
    }
}

} // namespace tetra::portal
