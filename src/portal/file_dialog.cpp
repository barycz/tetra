#include "file_dialog.hpp"

#include "style.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <format>

namespace tetra::portal {

namespace {

constexpr float kSidebarWidth = 190;
constexpr float kPadding = 14;
constexpr float kIconSize = 16;

fs::path home_dir() {
    const char* home = std::getenv("HOME"); // NOLINT(concurrency-mt-unsafe)
    return home != nullptr && *home != '\0' ? fs::path(home) : fs::path("/");
}

fs::path config_dir() {
    const char* config = std::getenv("XDG_CONFIG_HOME"); // NOLINT(concurrency-mt-unsafe)
    return config != nullptr && *config != '\0' ? fs::path(config) : home_dir() / ".config";
}

bool is_dir(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec);
}

bool path_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(fs::symlink_status(p, ec));
}

std::string lower(std::string_view s) {
    std::string out(s);
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string utf8(uint32_t c) {
    std::string out;
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
    return out;
}

// "~/x" and relative paths against `base`.
fs::path resolve(std::string_view text, const fs::path& base) {
    std::string s(text);
    if (s == "~" || s.starts_with("~/")) {
        s = home_dir().string() + s.substr(1);
    }
    fs::path p(s);
    return (p.is_absolute() ? p : base / p).lexically_normal();
}

void copy_to(std::array<char, 256>& buffer, std::string_view text) {
    std::snprintf(buffer.data(), buffer.size(), "%.*s", static_cast<int>(text.size()), text.data());
}

} // namespace

FileDialog::FileDialog(ui::App& app, const MimeGlobs& mime, DialogOptions options,
                       std::function<void(DialogResult)> done)
    : app_(app), icons_(app), mime_(mime), options_(std::move(options)), done_(std::move(done)) {
    places_ = places(home_dir(), config_dir());
    if (options_.filter >= options_.filters.size()) {
        options_.filter = 0;
    }
    copy_to(name_, options_.current_name);
    const bool save = options_.mode != DialogOptions::Mode::Open;
    std::string title = options_.title;
    if (title.empty()) {
        title = save ? (options_.mode == DialogOptions::Mode::SaveFiles ? "Save Files" : "Save File")
                : options_.directory ? "Select Folder"
                                     : (options_.multiple ? "Open Files" : "Open File");
    }
    window_ = std::make_unique<ui::ToplevelWindow>(app_, ui::ToplevelWindow::Options{.title = title,
                                                                                     .width = 860,
                                                                                     .height = 560,
                                                                                     .parent = options_.parent});
    window_->on_draw = [this] { draw(); };
    window_->on_close = [this] { finish(false, {}); };
    window_->on_key = [this](const ui::KeyEvent& key) { return handle_key(key); };
    navigate(is_dir(options_.folder) ? options_.folder : home_dir(), false);
}

FileDialog::~FileDialog() = default;

void FileDialog::cancel() {
    finished_ = true; // no answer: the request is gone
    done_ = nullptr;
}

void FileDialog::finish(bool accepted, std::vector<fs::path> paths) {
    if (finished_) {
        return;
    }
    finished_ = true;
    DialogResult result;
    result.accepted = accepted;
    result.paths = std::move(paths);
    if (!options_.filters.empty()) {
        result.filter = options_.filters[options_.filter];
    }
    result.choices = options_.choices;
    if (done_) {
        auto done = std::move(done_);
        done_ = nullptr;
        done(std::move(result));
    }
}

// --- navigation ----------------------------------------------------------------------

void FileDialog::navigate(const fs::path& dir, bool remember) {
    const fs::path target = dir.lexically_normal();
    if (remember && target != folder_ && !folder_.empty()) {
        back_.push_back(folder_);
        forward_.clear();
    }
    const fs::path previous = folder_;
    folder_ = target;
    selected_.clear();
    anchor_.clear();
    reload();
    // Going up: the folder we came from is selected.
    if (previous.parent_path() == folder_ && !previous.empty()) {
        select_only(previous.filename().string());
    }
}

void FileDialog::reload() {
    error_.clear();
    entries_.clear();
    types_.clear();
    if (auto listed = list_directory(folder_)) {
        entries_ = std::move(*listed);
    } else {
        error_ = std::format("Cannot open {}: {}", folder_.string(), listed.error());
    }
    types_.reserve(entries_.size());
    for (const Entry& e : entries_) {
        types_.push_back(e.directory ? std::string("inode/directory") : mime_.type_of(e.name));
    }
    std::erase_if(selected_, [&](const std::string& name) {
        return std::ranges::none_of(entries_, [&](const Entry& e) { return e.name == name; });
    });
    window_->request_redraw();
}

void FileDialog::go_back() {
    if (!back_.empty()) {
        forward_.push_back(folder_);
        const fs::path to = back_.back();
        back_.pop_back();
        navigate(to, false);
    }
}

void FileDialog::go_forward() {
    if (!forward_.empty()) {
        back_.push_back(folder_);
        const fs::path to = forward_.back();
        forward_.pop_back();
        navigate(to, false);
    }
}

void FileDialog::go_up() {
    if (folder_.has_parent_path() && folder_.parent_path() != folder_) {
        navigate(folder_.parent_path());
    }
}

bool FileDialog::visible(const Entry& entry, const std::string& /*type*/) const {
    if (entry.hidden && !show_hidden_) {
        return false;
    }
    if (entry.directory) {
        return true;
    }
    if (options_.mode == DialogOptions::Mode::SaveFiles || (options_.mode == DialogOptions::Mode::Open &&
                                                            options_.directory)) {
        return false; // picking a folder
    }
    return options_.filters.empty() || matches(options_.filters[options_.filter], entry.name, mime_);
}

const Entry* FileDialog::single_selection() const {
    if (selected_.size() != 1) {
        return nullptr;
    }
    for (const Entry& e : entries_) {
        if (e.name == *selected_.begin()) {
            return &e;
        }
    }
    return nullptr;
}

void FileDialog::select_only(const std::string& name) {
    selected_ = {name};
    anchor_ = name;
    scroll_to_selection_ = true;
    if (options_.mode == DialogOptions::Mode::Save) {
        for (const Entry& e : entries_) {
            if (e.name == name && !e.directory) {
                copy_to(name_, name);
            }
        }
    }
}

void FileDialog::open(const Entry& entry) {
    if (entry.directory) {
        navigate(folder_ / entry.name);
    } else if (options_.mode == DialogOptions::Mode::Open && !options_.directory) {
        accept_paths({folder_ / entry.name});
    } else if (options_.mode == DialogOptions::Mode::Save) {
        copy_to(name_, entry.name);
        accept();
    }
}

// --- accepting -----------------------------------------------------------------------------

void FileDialog::accept() {
    error_.clear();
    if (location_mode_ && location_[0] != '\0') {
        const fs::path path = resolve(location_.data(), folder_);
        if (is_dir(path)) {
            location_[0] = '\0';
            navigate(path);
            if (!(options_.mode == DialogOptions::Mode::Open && options_.directory)) {
                return;
            }
            accept_paths({path});
            return;
        }
        if (options_.mode == DialogOptions::Mode::Open) {
            if (!path_exists(path)) {
                error_ = std::format("{} does not exist.", path.string());
                return;
            }
            accept_paths({path});
            return;
        }
        // Save: a path typed in the location field names the file.
        if (!is_dir(path.parent_path())) {
            error_ = std::format("The folder {} does not exist.", path.parent_path().string());
            return;
        }
        accept_paths({path});
        return;
    }

    switch (options_.mode) {
    case DialogOptions::Mode::Open: {
        if (options_.directory) {
            const Entry* one = single_selection();
            accept_paths({one != nullptr && one->directory ? folder_ / one->name : folder_});
            return;
        }
        if (const Entry* one = single_selection(); one != nullptr && one->directory) {
            navigate(folder_ / one->name);
            return;
        }
        std::vector<fs::path> paths;
        for (std::size_t i : shown_) {
            if (selected_.contains(entries_[i].name) && !entries_[i].directory) {
                paths.push_back(folder_ / entries_[i].name);
            }
        }
        if (!paths.empty()) {
            accept_paths(std::move(paths));
        }
        return;
    }
    case DialogOptions::Mode::Save: {
        const std::string name = name_.data();
        if (name.empty()) {
            if (const Entry* one = single_selection(); one != nullptr && one->directory) {
                navigate(folder_ / one->name);
            }
            return;
        }
        const fs::path path = resolve(name, folder_);
        if (is_dir(path)) {
            name_[0] = '\0';
            navigate(path);
            return;
        }
        if (!is_dir(path.parent_path())) {
            error_ = std::format("The folder {} does not exist.", path.parent_path().string());
            return;
        }
        accept_paths({path});
        return;
    }
    case DialogOptions::Mode::SaveFiles: {
        fs::path dir = folder_;
        if (const Entry* one = single_selection(); one != nullptr && one->directory) {
            dir = folder_ / one->name;
        }
        std::vector<fs::path> paths;
        for (const auto& file : options_.files) {
            paths.push_back(dir / fs::path(file).filename());
        }
        accept_paths(std::move(paths));
        return;
    }
    }
}

void FileDialog::accept_paths(std::vector<fs::path> paths) {
    if (options_.mode != DialogOptions::Mode::Open) {
        const bool clash = std::ranges::any_of(paths, [](const fs::path& p) { return path_exists(p) && !is_dir(p); });
        if (clash && !replace_) {
            replace_ = std::move(paths);
            return;
        }
    }
    finish(true, std::move(paths));
}

// --- input -----------------------------------------------------------------------------

bool FileDialog::handle_key(const ui::KeyEvent& key) {
    // In Open, typing starts the location field (as in GTK).
    if (options_.mode == DialogOptions::Mode::Open && !text_active_ && !replace_ && !new_folder_ && !key.ctrl &&
        !key.alt && !key.super && key.codepoint >= 0x20 && key.codepoint != 0x7F) {
        if (!location_mode_) {
            location_mode_ = true;
            location_[0] = '\0';
        }
        typed_ += utf8(key.codepoint);
        focus_location_ = true;
        window_->request_redraw();
        return true;
    }
    return false;
}

void FileDialog::handle_keys() {
    if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_L, false)) {
        location_mode_ = true;
        if (location_[0] == '\0') {
            std::snprintf(location_.data(), location_.size(), "%s/",
                          folder_ == "/" ? "" : folder_.string().c_str());
        }
        focus_location_ = true;
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_H, false)) {
        show_hidden_ = !show_hidden_;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false) || (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_R, false))) {
        reload();
    }
    if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        go_up();
    } else if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
        go_back();
    } else if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
        go_forward();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (location_mode_) {
            location_mode_ = false;
            location_[0] = '\0';
            ImGui::ClearActiveID();
        } else {
            finish(false, {});
        }
        return;
    }
    if (text_active_) {
        return; // the field has the other keys
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
        go_up();
    }
    if ((ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))) {
        if (const Entry* one = single_selection(); one != nullptr && one->directory &&
                                                   !(options_.mode == DialogOptions::Mode::Open &&
                                                     options_.directory)) {
            navigate(folder_ / one->name);
        } else {
            accept();
        }
    }
    const int step = ImGui::IsKeyPressed(ImGuiKey_DownArrow) && !io.KeyAlt ? 1
                     : ImGui::IsKeyPressed(ImGuiKey_UpArrow) && !io.KeyAlt  ? -1
                                                                            : 0;
    if (step != 0 && !shown_.empty()) {
        std::ptrdiff_t at = -1;
        for (std::size_t i = 0; i < shown_.size(); ++i) {
            if (entries_[shown_[i]].name == anchor_) {
                at = static_cast<std::ptrdiff_t>(i);
            }
        }
        at = at < 0 ? (step > 0 ? 0 : std::ssize(shown_) - 1)
                    : std::clamp<std::ptrdiff_t>(at + step, 0, std::ssize(shown_) - 1);
        select_only(entries_[shown_[static_cast<std::size_t>(at)]].name);
    }
}

// --- drawing ---------------------------------------------------------------------------------

void FileDialog::draw() {
    if (finished_) {
        return;
    }
    const auto& p = app_.palette();
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard; // our own Up/Down
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::to_imvec4(p.window));
    ImGui::Begin("##files", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(3);

    handle_keys();
    if (finished_) {
        ImGui::End();
        ImGui::PopStyleColor();
        return;
    }
    draw_places(kSidebarWidth, viewport->Size.y);

    ImGui::SetCursorPos({kSidebarWidth, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {kPadding, kPadding - 4});
    ImGui::BeginChild("##main", {viewport->Size.x - kSidebarWidth, viewport->Size.y},
                      ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    text_active_ = false;
    draw_header(ImGui::GetContentRegionAvail().x);
    const float footer = (options_.mode == DialogOptions::Mode::Save ? 2 : 1) * (ImGui::GetFrameHeight() + 10) +
                         (error_.empty() ? 0 : ImGui::GetTextLineHeightWithSpacing()) + 8;
    draw_list(ImGui::GetContentRegionAvail().y - footer);
    draw_footer();
    draw_popups();
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::End();
    ImGui::PopStyleColor();
}

void FileDialog::draw_places(float width, float height) {
    const auto& p = app_.palette();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled({0, 0}, {width, height}, ui::to_imu32(p.surface));
    dl->AddLine({width - 0.5F, 0}, {width - 0.5F, height}, ui::to_imu32(p.separator));
    const float scale = ImGui::GetIO().DisplayFramebufferScale.x;
    float y = 12;
    for (std::size_t i = 0; i < places_.size(); ++i) {
        const Place& place = places_[i];
        const bool current = place.path == folder_;
        ImGui::SetCursorPos({8, y});
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::InvisibleButton("##place", {width - 16, 32})) {
            navigate(place.path);
        }
        const bool hovered = ImGui::IsItemHovered();
        if (hovered && place.label != place.path.filename().string()) {
            ImGui::SetItemTooltip("%s", place.path.c_str());
        }
        ImGui::PopID();
        if (current || hovered) {
            dl->AddRectFilled({8, y}, {width - 8, y + 32}, ui::to_imu32(current ? p.pressed : p.hover), 7);
        }
        if (const ui::Icon* icon = icons_.get(place.icon, static_cast<int>(kIconSize * scale))) {
            dl->AddImage(icon->ref(), {18, y + 8}, {18 + kIconSize, y + 8 + kIconSize}, {0, 0}, {1, 1},
                         ui::to_imu32(current ? p.text : p.text_dim));
        }
        dl->PushClipRect({0, y}, {width - 12, y + 32}, true);
        dl->AddText({44, y + 7}, ui::to_imu32(p.text), place.label.c_str());
        dl->PopClipRect();
        y += 34;
    }
}

void FileDialog::draw_header(float width) {
    const auto& p = app_.palette();
    const float scale = ImGui::GetIO().DisplayFramebufferScale.x;
    const float h = ImGui::GetFrameHeight();
    const auto icon_button = [&](const char* id, const char* icon_name, bool enabled, const char* tip) {
        ImGui::BeginDisabled(!enabled);
        bool clicked = false;
        if (const ui::Icon* icon = icons_.get(icon_name, static_cast<int>(kIconSize * scale))) {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {(h - kIconSize) / 2, (h - kIconSize) / 2});
            clicked = ImGui::ImageButton(id, icon->ref(), {kIconSize, kIconSize}, {0, 0}, {1, 1}, {0, 0, 0, 0},
                                         ui::to_imvec4(p.text));
            ImGui::PopStyleVar();
        } else {
            clicked = ImGui::Button(id, {h, h});
        }
        ImGui::SetItemTooltip("%s", tip);
        ImGui::EndDisabled();
        return clicked;
    };
    if (icon_button("##back", "go-previous-symbolic", !back_.empty(), "Back (Alt+Left)")) {
        go_back();
    }
    ImGui::SameLine(0, 4);
    if (icon_button("##forward", "go-next-symbolic", !forward_.empty(), "Forward (Alt+Right)")) {
        go_forward();
    }
    ImGui::SameLine(0, 4);
    if (icon_button("##up", "go-up-symbolic", folder_ != folder_.root_path(), "Up (Alt+Up)")) {
        go_up();
    }
    ImGui::SameLine(0, 10);

    const float right = (2 * h) + 8;
    const float path_width = width - ImGui::GetCursorPosX() + kPadding - right - 10;
    if (location_mode_) {
        ImGui::SetNextItemWidth(path_width);
        if (focus_location_) {
            ImGui::SetKeyboardFocusHere();
            focus_location_ = false;
        }
        const bool enter = ImGui::InputTextWithHint(
            "##location", "Type a file or folder name, or a path", location_.data(), location_.size(),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackAlways,
            [](ImGuiInputTextCallbackData* data) {
                auto* self = static_cast<FileDialog*>(data->UserData);
                if (!self->typed_.empty()) {
                    data->ClearSelection();
                    data->CursorPos = data->BufTextLen;
                    data->InsertChars(data->CursorPos, self->typed_.c_str());
                    self->typed_.clear();
                }
                return 0;
            },
            this);
        text_active_ = text_active_ || ImGui::IsItemActive();
        if (enter) {
            accept();
        }
    } else {
        // The path as buttons: Home or Computer, then each folder below.
        const fs::path home = home_dir();
        std::vector<std::pair<std::string, fs::path>> crumbs;
        fs::path base;
        fs::path rest;
        const auto relative = folder_.lexically_relative(home);
        if (!relative.empty() && *relative.begin() != "..") {
            crumbs.emplace_back("Home", home);
            base = home;
            rest = relative == "." ? fs::path() : relative;
        } else {
            crumbs.emplace_back("Computer", folder_.root_path());
            base = folder_.root_path();
            rest = folder_.relative_path();
        }
        for (const auto& part : rest) {
            base /= part;
            crumbs.emplace_back(part.string(), base);
        }
        // Only as many as fit, from the end.
        const ImVec2 start = ImGui::GetCursorPos();
        float used = 0;
        std::size_t first = crumbs.size();
        const float sep = ImGui::CalcTextSize("/").x + 8;
        while (first > 0) {
            const float w = ImGui::CalcTextSize(crumbs[first - 1].first.c_str()).x +
                            (ImGui::GetStyle().FramePadding.x * 2) + sep;
            if (used + w > path_width && first < crumbs.size()) {
                break;
            }
            used += w;
            --first;
        }
        ImGui::SetCursorPos(start);
        for (std::size_t i = first; i < crumbs.size(); ++i) {
            const bool last = i + 1 == crumbs.size();
            ImGui::PushID(static_cast<int>(i));
            ImGui::PushStyleColor(ImGuiCol_Button, ui::to_imvec4(last ? p.pressed : p.window));
            if (ImGui::Button(crumbs[i].first.c_str()) && !last) {
                navigate(crumbs[i].second);
            }
            ImGui::PopStyleColor();
            ImGui::PopID();
            if (!last) {
                ImGui::SameLine(0, 4);
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("/");
                ImGui::SameLine(0, 4);
            }
        }
    }

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, ImGui::GetContentRegionAvail().x - right));
    if (icon_button("##newfolder", "folder-new-symbolic", true, "New folder")) {
        new_folder_ = true;
        copy_to(new_folder_name_, "New Folder");
    }
    ImGui::SameLine(0, 8);
    if (icon_button("##hidden", show_hidden_ ? "view-reveal-symbolic" : "view-conceal-symbolic", true,
                    show_hidden_ ? "Hide hidden files (Ctrl+H)" : "Show hidden files (Ctrl+H)")) {
        show_hidden_ = !show_hidden_;
    }
    ImGui::Dummy({0, 2});
}

void FileDialog::draw_list(float height) {
    const auto& p = app_.palette();
    const float scale = ImGui::GetIO().DisplayFramebufferScale.x;
    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable |
                                       ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                                       ImGuiTableFlags_BordersOuter | ImGuiTableFlags_SizingFixedFit;
    ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ui::to_imvec4(ui::mix(p.window, p.hover)));
    // The selection in the accent colour: the theme's header colour is too
    // close to the alternating row shade to see.
    const auto accent = [&](float alpha) {
        config::Color c = p.accent;
        c.a = alpha;
        return ui::to_imvec4(ui::mix(p.window, c));
    };
    ImGui::PushStyleColor(ImGuiCol_Header, accent(0.35F));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, accent(0.45F));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, accent(0.5F));
    ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, ui::to_imvec4(p.separator));
    ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, ui::to_imvec4(p.surface));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {8, 4});
    if (!ImGui::BeginTable("##list", 3, kFlags, {0, height})) {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(6);
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort, 1, 0);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending,
                            84, 1);
    ImGui::TableSetupColumn("Modified",
                            ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 96, 2);
    ImGui::TableHeadersRow();

    ImGuiID column = 0;
    bool descending = false;
    if (const ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs != nullptr && specs->SpecsCount > 0) {
        column = specs->Specs[0].ColumnUserID;
        descending = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
    }
    shown_.clear();
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (visible(entries_[i], types_[i])) {
            shown_.push_back(i);
        }
    }
    std::ranges::stable_sort(shown_, [&](std::size_t a, std::size_t b) {
        const Entry& x = entries_[a];
        const Entry& y = entries_[b];
        if (x.directory != y.directory) {
            return x.directory; // folders first, whatever the order
        }
        int c = 0;
        if (column == 1 && x.size != y.size) {
            c = x.size < y.size ? -1 : 1;
        } else if (column == 2 && x.modified != y.modified) {
            c = x.modified < y.modified ? -1 : 1;
        } else {
            const auto lx = lower(x.name);
            const auto ly = lower(y.name);
            c = lx < ly ? -1 : (ly < lx ? 1 : 0);
        }
        return descending ? c > 0 : c < 0;
    });

    const auto now = static_cast<int64_t>(std::time(nullptr));
    const bool multi = options_.mode == DialogOptions::Mode::Open && options_.multiple;
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(shown_.size()));
    if (scroll_to_selection_) {
        for (std::size_t i = 0; i < shown_.size(); ++i) {
            if (entries_[shown_[i]].name == anchor_) {
                clipper.IncludeItemByIndex(static_cast<int>(i));
            }
        }
    }
    std::optional<std::size_t> open_index;
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const std::size_t index = shown_[static_cast<std::size_t>(row)];
            const Entry& e = entries_[index];
            const bool selected = selected_.contains(e.name);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(row);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##row", selected,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick,
                                  {0, kIconSize + 2})) {
                const ImGuiIO& io = ImGui::GetIO();
                if (multi && io.KeyCtrl) {
                    if (!selected_.erase(e.name)) {
                        selected_.insert(e.name);
                    }
                    anchor_ = e.name;
                } else if (multi && io.KeyShift && !anchor_.empty()) {
                    std::size_t a = row;
                    for (std::size_t i = 0; i < shown_.size(); ++i) {
                        if (entries_[shown_[i]].name == anchor_) {
                            a = i;
                        }
                    }
                    selected_.clear();
                    for (std::size_t i = std::min<std::size_t>(a, row); i <= std::max<std::size_t>(a, row); ++i) {
                        selected_.insert(entries_[shown_[i]].name);
                    }
                } else {
                    select_only(e.name);
                    scroll_to_selection_ = false;
                }
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    open_index = index;
                }
            }
            if (selected && scroll_to_selection_ && e.name == anchor_) {
                ImGui::SetScrollHereY();
                scroll_to_selection_ = false;
            }
            ImGui::PopID();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (const ui::Icon* icon = icons_.get(icon_for(e, types_[index]), static_cast<int>(kIconSize * scale))) {
                dl->AddImage(icon->ref(), {at.x, at.y + 1}, {at.x + kIconSize, at.y + 1 + kIconSize}, {0, 0}, {1, 1},
                             ui::to_imu32(e.directory ? p.accent : p.text_dim));
            }
            dl->AddText({at.x + kIconSize + 8, at.y + 1}, ui::to_imu32(e.hidden ? p.text_dim : p.text),
                        e.name.c_str());
            ImGui::TableNextColumn();
            if (!e.directory) {
                const std::string size = format_size(e.size);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x -
                                     ImGui::CalcTextSize(size.c_str()).x);
                ImGui::TextDisabled("%s", size.c_str());
            }
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", format_time(e.modified, now).c_str());
        }
    }
    if (shown_.empty() && error_.empty()) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled(entries_.empty() ? "This folder is empty." : "No files here match.");
    }
    ImGui::EndTable();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(6);
    if (open_index) {
        open(entries_[*open_index]);
    }
}

void FileDialog::draw_footer() {
    const auto& p = app_.palette();
    ImGui::Dummy({0, 2});
    if (!error_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(p.close_hover));
        ImGui::TextUnformatted(error_.c_str());
        ImGui::PopStyleColor();
    }
    const bool save = options_.mode == DialogOptions::Mode::Save;
    if (save) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Name");
        ImGui::SameLine(0, 12);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (focus_name_) {
            ImGui::SetKeyboardFocusHere();
            focus_name_ = false;
        }
        const bool enter = ImGui::InputText(
            "##name", name_.data(), name_.size(), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackAlways,
            [](ImGuiInputTextCallbackData* data) {
                auto* self = static_cast<FileDialog*>(data->UserData);
                if (!self->name_selected_once_) {
                    // The name without its extension, ready to be typed over.
                    self->name_selected_once_ = true;
                    const std::string_view name(data->Buf, static_cast<std::size_t>(data->BufTextLen));
                    const auto dot = name.rfind('.');
                    const int stem = dot == std::string_view::npos || dot == 0 ? data->BufTextLen
                                                                               : static_cast<int>(dot);
                    data->SelectionStart = 0;
                    data->SelectionEnd = stem;
                    data->CursorPos = stem;
                }
                return 0;
            },
            this);
        text_active_ = text_active_ || ImGui::IsItemActive();
        if (enter) {
            accept();
        }
        ImGui::Dummy({0, 2});
    }

    // Filters and choices on the left, the buttons on the right.
    if (!options_.filters.empty()) {
        ImGui::SetNextItemWidth(220);
        if (ImGui::BeginCombo("##filter", options_.filters[options_.filter].name.c_str())) {
            for (std::size_t i = 0; i < options_.filters.size(); ++i) {
                if (ImGui::Selectable(options_.filters[i].name.c_str(), i == options_.filter)) {
                    options_.filter = i;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine(0, 12);
    }
    for (auto& choice : options_.choices) {
        ImGui::PushID(choice.id.c_str());
        if (choice.options.empty()) {
            bool on = choice.selected == "true";
            if (ImGui::Checkbox(choice.label.c_str(), &on)) {
                choice.selected = on ? "true" : "false";
            }
        } else {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(choice.label.c_str());
            ImGui::SameLine();
            std::string current = choice.selected;
            for (const auto& [id, label] : choice.options) {
                if (id == choice.selected) {
                    current = label;
                }
            }
            ImGui::SetNextItemWidth(160);
            if (ImGui::BeginCombo("##choice", current.c_str())) {
                for (const auto& [id, label] : choice.options) {
                    if (ImGui::Selectable(label.c_str(), id == choice.selected)) {
                        choice.selected = id;
                    }
                }
                ImGui::EndCombo();
            }
        }
        ImGui::PopID();
        ImGui::SameLine(0, 12);
    }

    std::string accept_label = options_.accept_label;
    if (accept_label.empty()) {
        accept_label = options_.mode != DialogOptions::Mode::Open ? "Save" : options_.directory ? "Select" : "Open";
    }
    // GTK mnemonics ("_Open") are not ours to show.
    std::erase(accept_label, '_');
    constexpr float kButton = 110;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                         std::max(0.0F, ImGui::GetContentRegionAvail().x - (2 * kButton) - 8));
    if (ImGui::Button("Cancel", {kButton, 0})) {
        finish(false, {});
        return;
    }
    ImGui::SameLine(0, 8);
    bool can_accept = true;
    if (options_.mode == DialogOptions::Mode::Open && !options_.directory && !location_mode_) {
        can_accept = !selected_.empty();
    } else if (save && !location_mode_) {
        can_accept = name_[0] != '\0';
    }
    ImGui::BeginDisabled(!can_accept);
    ImGui::PushStyleColor(ImGuiCol_Button, ui::to_imvec4(p.accent));
    ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(p.accent_text));
    if (ImGui::Button(accept_label.c_str(), {kButton, 0})) {
        accept();
    }
    ImGui::PopStyleColor(2);
    ImGui::EndDisabled();
}

void FileDialog::draw_popups() {
    const auto& p = app_.palette();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const auto center = [&] {
        ImGui::SetNextWindowPos({viewport->Pos.x + (viewport->Size.x / 2), viewport->Pos.y + (viewport->Size.y / 2)},
                                ImGuiCond_Always, {0.5F, 0.5F});
        ImGui::SetNextWindowSize({420, 0});
    };
    const auto buttons = [&](const char* action) {
        constexpr float kWidth = 120;
        ImGui::Dummy({0, 8});
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - (2 * kWidth) - 8);
        // Not on the frame the box appears: the Enter that opened it ("Save"
        // on an existing file) must not also answer it.
        const bool keys = !ImGui::IsWindowAppearing();
        bool cancel = ImGui::Button("Cancel", {kWidth, 0}) || (keys && ImGui::IsKeyPressed(ImGuiKey_Escape, false));
        ImGui::SameLine(0, 8);
        ImGui::PushStyleColor(ImGuiCol_Button, ui::to_imvec4(p.accent));
        ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(p.accent_text));
        const bool ok = ImGui::Button(action, {kWidth, 0}) ||
                        (keys && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                                  ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)));
        ImGui::PopStyleColor(2);
        return ok ? 1 : (cancel ? -1 : 0);
    };
    constexpr ImGuiWindowFlags kModal = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;

    if (replace_ && !ImGui::IsPopupOpen("##replace")) {
        ImGui::OpenPopup("##replace");
    }
    center();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {20, 18});
    if (ImGui::BeginPopupModal("##replace", nullptr, kModal)) {
        std::vector<std::string> names;
        for (const auto& path : replace_.value_or(std::vector<fs::path>{})) {
            if (path_exists(path)) {
                names.push_back(path.filename().string());
            }
        }
        ImGui::PushFont(nullptr, 17);
        if (names.size() == 1) {
            ImGui::TextWrapped("Replace “%s”?", names[0].c_str());
        } else {
            ImGui::TextWrapped("Replace %zu files?", names.size());
        }
        ImGui::PopFont();
        ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(p.text_dim));
        ImGui::TextWrapped("%s already in %s. Replacing overwrites what is there now.",
                           names.size() == 1 ? "A file with this name is" : "Files with these names are",
                           replace_ && !replace_->empty() ? replace_->front().parent_path().c_str() : "");
        ImGui::PopStyleColor();
        const int answer = buttons("Replace");
        if (answer != 0) {
            auto paths = std::move(*replace_);
            replace_.reset();
            ImGui::CloseCurrentPopup();
            if (answer > 0) {
                finish(true, std::move(paths));
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();

    if (new_folder_ && !ImGui::IsPopupOpen("##newfolder")) {
        ImGui::OpenPopup("##newfolder");
    }
    center();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {20, 18});
    if (ImGui::BeginPopupModal("##newfolder", nullptr, kModal)) {
        ImGui::PushFont(nullptr, 17);
        ImGui::TextUnformatted("New Folder");
        ImGui::PopFont();
        ImGui::Dummy({0, 4});
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##foldername", new_folder_name_.data(), new_folder_name_.size(),
                         ImGuiInputTextFlags_AutoSelectAll);
        const std::string name = new_folder_name_.data();
        const fs::path path = folder_ / name;
        const bool valid = !name.empty() && name != "." && name != ".." && name.find('/') == std::string::npos;
        if (!name.empty() && path_exists(path)) {
            ImGui::TextDisabled("Something with this name is already here.");
        }
        const int answer = buttons("Create");
        if (answer < 0) {
            new_folder_ = false;
            ImGui::CloseCurrentPopup();
        } else if (answer > 0 && valid && !path_exists(path)) {
            std::error_code ec;
            fs::create_directory(path, ec);
            new_folder_ = false;
            ImGui::CloseCurrentPopup();
            if (ec) {
                error_ = std::format("Cannot create {}: {}", name, ec.message());
            } else {
                reload();
                select_only(name);
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}

} // namespace tetra::portal
