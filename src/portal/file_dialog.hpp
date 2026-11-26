#pragma once

#include "app.hpp"
#include "files.hpp"
#include "icons.hpp"
#include "window.hpp"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace tetra::portal {

// A portal "choice": a combo box (options), or a check box (no options;
// "true"/"false").
struct Choice {
    std::string id;
    std::string label;
    std::vector<std::pair<std::string, std::string>> options; // id, label
    std::string selected;
};

struct DialogOptions {
    enum class Mode { Open, Save, SaveFiles };
    Mode mode = Mode::Open;
    std::string title;
    std::string accept_label;
    bool multiple = false;  // Open: several files
    bool directory = false; // Open: pick folders
    std::vector<Filter> filters;
    std::size_t filter = 0; // the one in use, if any
    std::vector<Choice> choices;
    std::string current_name;       // Save: the suggested name
    fs::path folder;                // where to start
    std::vector<std::string> files; // SaveFiles: the names to save
};

struct DialogResult {
    bool accepted = false;
    std::vector<fs::path> paths;
    std::optional<Filter> filter; // the one in use
    std::vector<Choice> choices;
};

// The file chooser window (one per portal request): places, a path bar,
// the folder's files, and Open/Save. `done` runs once, when the user
// accepts or cancels (not after cancel()); it may destroy the dialog only
// from a later turn of the event loop.
class FileDialog {
public:
    FileDialog(ui::App& app, const MimeGlobs& mime, DialogOptions options, std::function<void(DialogResult)> done);
    ~FileDialog();

    FileDialog(const FileDialog&) = delete;
    FileDialog& operator=(const FileDialog&) = delete;
    FileDialog(FileDialog&&) = delete;
    FileDialog& operator=(FileDialog&&) = delete;

    // The app withdrew the request: close without calling `done`.
    void cancel();
    [[nodiscard]] const fs::path& folder() const noexcept { return folder_; }

private:
    void draw();
    void draw_header(float width);
    void draw_places(float width, float height);
    void draw_list(float height);
    void draw_footer();
    void draw_popups();
    void handle_keys();
    bool handle_key(const ui::KeyEvent& key);

    void navigate(const fs::path& dir, bool remember = true);
    void reload();
    void go_back();
    void go_forward();
    void go_up();
    void open(const Entry& entry); // double click / Enter on one entry
    void accept();
    void accept_paths(std::vector<fs::path> paths); // after the replace check
    void finish(bool accepted, std::vector<fs::path> paths);
    [[nodiscard]] bool visible(const Entry& entry, const std::string& type) const;
    [[nodiscard]] const Entry* single_selection() const;
    void select_only(const std::string& name);

    ui::App& app_;
    ui::IconCache icons_;
    const MimeGlobs& mime_;
    DialogOptions options_;
    std::function<void(DialogResult)> done_;
    std::unique_ptr<ui::ToplevelWindow> window_;
    bool finished_ = false;

    std::vector<Place> places_;
    fs::path folder_;
    std::vector<Entry> entries_;
    std::vector<std::string> types_; // MIME type per entry
    std::vector<std::size_t> shown_; // entries in view, in order
    std::string error_;
    std::vector<fs::path> back_;
    std::vector<fs::path> forward_;
    std::set<std::string> selected_;
    std::string anchor_; // where a Shift+click range starts
    bool scroll_to_selection_ = false;
    bool show_hidden_ = false;

    // Text fields.
    std::array<char, 256> name_{};        // Save: the file name
    std::array<char, 4096> location_{};   // Ctrl+L, or typing in Open
    bool location_mode_ = false;
    bool focus_location_ = false;
    bool focus_name_ = true;
    bool name_selected_once_ = false; // the stem is selected when the field first gets focus
    std::string typed_;               // typed before the location field had the keyboard
    bool text_active_ = false;

    // Pop-ups.
    std::optional<std::vector<fs::path>> replace_; // waiting for "Replace?"
    bool new_folder_ = false;
    std::array<char, 256> new_folder_name_{};
};

} // namespace tetra::portal
