#include "monitor.hpp"

#include "style.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <format>
#include <functional>
#include <unistd.h>
#include <unordered_map>

namespace tetra::monitor {

namespace {

enum Column : ImGuiID { Name, Pid, User, Cpu, Memory, Read, Write, Status, Threads, Nice, Command };

const char* state_name(char state) {
    switch (state) {
    case 'R':
        return "Running";
    case 'S':
        return "Sleeping";
    case 'D':
        return "Waiting";
    case 'T':
        return "Stopped";
    case 't':
        return "Traced";
    case 'Z':
        return "Zombie";
    case 'I':
        return "Idle";
    case 'X':
    case 'x':
        return "Dead";
    default:
        return "Unknown";
    }
}

std::string lower(std::string_view text) {
    std::string out(text);
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Text right-aligned in the current table cell; dimmed if `dim`.
void right_text(const std::string& text, bool dim, const theme::Palette& p) {
    const float width = ImGui::CalcTextSize(text.c_str()).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, ImGui::GetContentRegionAvail().x - width));
    if (dim) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(p.text_dim));
    }
    ImGui::TextUnformatted(text.c_str());
    if (dim) {
        ImGui::PopStyleColor();
    }
}

int compare(const ProcessRow& a, const ProcessRow& b, ImGuiID column) {
    const auto cmp = [](const auto& x, const auto& y) { return x < y ? -1 : (y < x ? 1 : 0); };
    switch (column) {
    case Name: {
        const int c = cmp(lower(a.process.name), lower(b.process.name));
        return c != 0 ? c : cmp(a.process.pid, b.process.pid);
    }
    case Pid:
        return cmp(a.process.pid, b.process.pid);
    case User:
        return cmp(a.user, b.user);
    case Cpu:
        return cmp(a.cpu, b.cpu);
    case Memory:
        return cmp(a.process.memory, b.process.memory);
    case Read:
        return cmp(a.read_rate.value_or(-1), b.read_rate.value_or(-1));
    case Write:
        return cmp(a.write_rate.value_or(-1), b.write_rate.value_or(-1));
    case Status:
        return cmp(std::string_view(state_name(a.process.state)), std::string_view(state_name(b.process.state)));
    case Threads:
        return cmp(a.process.threads, b.process.threads);
    case Nice:
        return cmp(a.process.nice, b.process.nice);
    case Command:
        return cmp(a.process.command, b.process.command);
    default:
        return 0;
    }
}

} // namespace

bool Monitor::matches(const ProcessRow& row) const {
    if (!all_users_ && row.process.uid != ::getuid()) {
        return false;
    }
    const std::string needle = lower(search_.data());
    if (needle.empty()) {
        return true;
    }
    return lower(row.process.name).contains(needle) || lower(row.process.command).contains(needle) ||
           lower(row.user).contains(needle) || std::to_string(row.process.pid) == needle;
}

const ProcessRow* Monitor::selected_row() const {
    if (!selected_) {
        return nullptr;
    }
    for (const auto& row : sampler_.processes()) {
        if (row.process.pid == selected_->pid && row.process.start_time == selected_->start_time) {
            return &row;
        }
    }
    return nullptr;
}

void Monitor::send_signal(const Pick& target, int signal) {
    const bool alive = std::ranges::any_of(sampler_.processes(), [&](const ProcessRow& row) {
        return row.process.pid == target.pid && row.process.start_time == target.start_time;
    });
    if (!alive) {
        show_message(std::format("“{}” has already exited.", target.name));
        return;
    }
    if (::kill(target.pid, signal) != 0) {
        const int error = errno;
        if (error == EPERM) {
            show_message(std::format("Not allowed: “{}” (PID {}) belongs to another user.", target.name,
                                     target.pid));
        } else if (error == ESRCH) {
            show_message(std::format("“{}” has already exited.", target.name));
        } else {
            show_message(std::format("Could not signal “{}”: {}", target.name, std::strerror(error)));
        }
        return;
    }
    // Show the result soon, without waiting for the next update.
    app_.add_timer(std::chrono::milliseconds(250), [this] { tick(); });
}

void Monitor::page_processes() {
    process_toolbar();
    ImGui::Dummy({0, 2});
    const float footer = ImGui::GetTextLineHeight() + 16;
    process_table(ImGui::GetContentRegionAvail().y - footer);
    process_footer();
    confirm_popup();
}

void Monitor::process_toolbar() {
    const auto& p = app_.palette();
    // Search; typing anywhere on the page lands here.
    ImGui::SetNextItemWidth(320);
    if (focus_search_) {
        ImGui::SetKeyboardFocusHere();
        focus_search_ = false;
    }
    ImGui::InputTextWithHint(
        "##search", "Search processes", search_.data(), search_.size(), ImGuiInputTextFlags_CallbackAlways,
        [](ImGuiInputTextCallbackData* data) {
            auto* self = static_cast<Monitor*>(data->UserData);
            if (!self->typed_.empty()) {
                data->ClearSelection();
                data->CursorPos = data->BufTextLen;
                data->InsertChars(data->CursorPos, self->typed_.c_str());
                self->typed_.clear();
            }
            return 0;
        },
        this);
    // Escape empties the search (ImGui's Escape only undoes this edit).
    const bool escaped = ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
                         !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (escaped) {
        search_[0] = '\0';
    }
    search_active_ = ImGui::IsItemActive();

    // Whose processes.
    const auto segment = [&](const char* label, bool on) {
        ImGui::SameLine(0, 0);
        ImGui::PushStyleColor(ImGuiCol_Button, ui::to_imvec4(on ? p.accent : p.surface));
        ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(on ? p.accent_text : p.text));
        const bool clicked = ImGui::Button(label, {110, 0});
        ImGui::PopStyleColor(2);
        return clicked;
    };
    ImGui::SameLine(0, 16);
    ImGui::Dummy({0, 0});
    if (segment("All users", all_users_)) {
        all_users_ = true;
    }
    if (segment("Mine", !all_users_)) {
        all_users_ = false;
    }
    ImGui::SameLine(0, 16);
    ImGui::Checkbox("Tree", &tree_);

    // End the selected one.
    const ProcessRow* row = selected_row();
    const char* label = "End process";
    const float button = ImGui::CalcTextSize(label).x + (ImGui::GetStyle().FramePadding.x * 2) + 8;
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, ImGui::GetContentRegionAvail().x - button));
    ImGui::BeginDisabled(row == nullptr);
    if (ImGui::Button(label, {button, 0}) && row != nullptr) {
        confirm_ = Confirm{*selected_, SIGTERM};
    }
    ImGui::EndDisabled();
}

void Monitor::process_table(float height) {
    const auto& p = app_.palette();

    // Keys: Delete ends (Shift: kills) the selection; Up/Down move it (also
    // from the search field, which they leave).
    const bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (!popup && selected_ && !search_active_ && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        confirm_ = Confirm{*selected_, ImGui::GetIO().KeyShift ? SIGKILL : SIGTERM};
    }
    if (!popup && !order_.empty()) {
        const int step = ImGui::IsKeyPressed(ImGuiKey_DownArrow) ? 1 : ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? -1 : 0;
        if (step != 0) {
            auto it = selected_ ? std::ranges::find(order_, *selected_) : order_.end();
            std::ptrdiff_t index = it == order_.end() ? (step > 0 ? 0 : std::ssize(order_) - 1)
                                                      : std::clamp<std::ptrdiff_t>(
                                                            (it - order_.begin()) + step, 0, std::ssize(order_) - 1);
            selected_ = order_[static_cast<std::size_t>(index)];
            scroll_to_selected_ = true;
            if (search_active_) {
                ImGui::ClearActiveID(); // from the search into the list: Delete now ends
                search_active_ = false;
            }
        }
    }

    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
                                       ImGuiTableFlags_Hideable | ImGuiTableFlags_Sortable |
                                       ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                                       ImGuiTableFlags_BordersOuter | ImGuiTableFlags_SizingFixedFit;
    ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ui::to_imvec4(ui::mix(p.window, p.hover)));
    ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, ui::to_imvec4(p.separator));
    ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, ui::to_imvec4(p.surface));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {8, 4});
    if (!ImGui::BeginTable("##processes", 11, kFlags, {0, height})) {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        return;
    }
    const auto fixed = ImGuiTableColumnFlags_WidthFixed;
    const auto numeric = fixed | ImGuiTableColumnFlags_PreferSortDescending;
    ImGui::TableSetupScrollFreeze(0, 1);
    // Name takes the room left; the command line (hidden at first, as it
    // is long) shares it when shown.
    const auto stretch = ImGuiTableColumnFlags_WidthStretch;
    ImGui::TableSetupColumn("Name", stretch | ImGuiTableColumnFlags_NoHide, 1, Name);
    ImGui::TableSetupColumn("PID", fixed, 56, Pid);
    ImGui::TableSetupColumn("User", fixed, 76, User);
    ImGui::TableSetupColumn("CPU %", numeric | ImGuiTableColumnFlags_DefaultSort, 56, Cpu);
    ImGui::TableSetupColumn("Memory", numeric, 72, Memory);
    ImGui::TableSetupColumn("Disk read", numeric, 76, Read);
    ImGui::TableSetupColumn("Disk write", numeric, 76, Write);
    ImGui::TableSetupColumn("Status", fixed | ImGuiTableColumnFlags_DefaultHide, 72, Status);
    ImGui::TableSetupColumn("Threads", numeric | ImGuiTableColumnFlags_DefaultHide, 56, Threads);
    ImGui::TableSetupColumn("Nice", fixed | ImGuiTableColumnFlags_DefaultHide, 40, Nice);
    ImGui::TableSetupColumn("Command", stretch | ImGuiTableColumnFlags_DefaultHide, 1.5F, Command);
    ImGui::TableHeadersRow();

    // Sorted, filtered rows.
    ImGuiID sort_column = Cpu;
    bool descending = true;
    if (const ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs != nullptr && specs->SpecsCount > 0) {
        sort_column = specs->Specs[0].ColumnUserID;
        descending = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
    }
    std::vector<const ProcessRow*> rows;
    for (const auto& row : sampler_.processes()) {
        if (matches(row)) {
            rows.push_back(&row);
        }
    }
    std::ranges::stable_sort(rows, [&](const ProcessRow* a, const ProcessRow* b) {
        const int c = compare(*a, *b, sort_column);
        return c != 0 ? (descending ? c > 0 : c < 0) : a->process.pid < b->process.pid;
    });

    order_.clear();
    // Draws one row; true if it is an open tree node (TreePop after its children).
    const auto draw_row = [&](const ProcessRow& row, bool tree, bool has_children) -> bool {
        const Pick pick{row.process.pid, row.process.start_time, row.process.name};
        const bool selected = selected_ && *selected_ == pick;
        order_.push_back(pick);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID(row.process.pid);
        bool open = false;
        if (tree) {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_OpenOnArrow |
                                       ImGuiTreeNodeFlags_DefaultOpen;
            if (!has_children) {
                flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            }
            if (selected) {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            open = ImGui::TreeNodeEx("##row", flags, "%s", row.process.name.c_str()) && has_children;
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                selected_ = pick;
            }
        } else if (ImGui::Selectable(row.process.name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns) ||
                   ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            selected_ = pick;
        }
        if (selected && scroll_to_selected_) {
            ImGui::SetScrollHereY();
            scroll_to_selected_ = false;
        }
        if (ImGui::BeginPopupContextItem("##menu")) {
            process_menu(row);
            ImGui::EndPopup();
        }
        ImGui::PopID();
        const auto& proc = row.process;
        if (ImGui::TableNextColumn()) {
            right_text(std::to_string(proc.pid), false, p);
        }
        if (ImGui::TableNextColumn()) {
            ImGui::TextUnformatted(row.user.c_str());
        }
        if (ImGui::TableNextColumn()) {
            right_text(row.cpu < 0.05 ? "0" : std::format("{:.1f}", row.cpu), row.cpu < 0.05, p);
        }
        if (ImGui::TableNextColumn()) {
            right_text(proc.memory == 0 ? "-" : format_bytes(static_cast<double>(proc.memory)), proc.memory == 0, p);
        }
        for (const auto& rate : {row.read_rate, row.write_rate}) {
            if (ImGui::TableNextColumn()) {
                right_text(!rate ? "-" : *rate < 1 ? "0" : format_rate(*rate), !rate || *rate < 1, p);
            }
        }
        if (ImGui::TableNextColumn()) {
            ImGui::TextUnformatted(state_name(proc.state));
        }
        if (ImGui::TableNextColumn()) {
            right_text(std::to_string(proc.threads), false, p);
        }
        if (ImGui::TableNextColumn()) {
            right_text(std::to_string(proc.nice), proc.nice == 0, p);
        }
        if (ImGui::TableNextColumn()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(p.text_dim));
            ImGui::TextUnformatted(proc.command.c_str());
            ImGui::PopStyleColor();
        }
        return open;
    };

    if (tree_ && search_[0] == '\0') {
        // Children under their parent; a process whose parent is not shown
        // is a root.
        std::unordered_map<int, std::vector<const ProcessRow*>> children;
        std::unordered_map<int, bool> shown;
        for (const ProcessRow* row : rows) {
            shown[row->process.pid] = true;
        }
        std::vector<const ProcessRow*> roots;
        for (const ProcessRow* row : rows) {
            if (row->process.ppid != row->process.pid && shown.contains(row->process.ppid)) {
                children[row->process.ppid].push_back(row);
            } else {
                roots.push_back(row);
            }
        }
        std::function<void(const ProcessRow&)> draw_tree = [&](const ProcessRow& row) {
            auto it = children.find(row.process.pid);
            const bool has_children = it != children.end();
            if (draw_row(row, true, has_children)) {
                for (const ProcessRow* child : it->second) {
                    draw_tree(*child);
                }
                ImGui::TreePop();
            }
        };
        for (const ProcessRow* row : roots) {
            draw_tree(*row);
        }
    } else {
        // A flat list (a search shows its matches that way, also in tree mode).
        // Clipping would skip rows, so not when a row must be scrolled to.
        if (scroll_to_selected_) {
            for (const ProcessRow* row : rows) {
                draw_row(*row, false, false);
            }
        } else {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(rows.size()));
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    draw_row(*rows[static_cast<std::size_t>(i)], false, false);
                }
            }
            // The keys need every row's place in the order.
            order_.clear();
            for (const ProcessRow* row : rows) {
                order_.push_back({row->process.pid, row->process.start_time, row->process.name});
            }
        }
    }
    ImGui::EndTable();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    shown_ = static_cast<int>(rows.size());
}

void Monitor::process_menu(const ProcessRow& row) {
    const Pick pick{row.process.pid, row.process.start_time, row.process.name};
    ImGui::TextDisabled("%s (PID %d)", row.process.name.c_str(), row.process.pid);
    ImGui::Separator();
    if (ImGui::MenuItem("End", "Delete")) {
        confirm_ = Confirm{pick, SIGTERM};
    }
    if (ImGui::MenuItem("Kill", "Shift+Delete")) {
        confirm_ = Confirm{pick, SIGKILL};
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Stop", nullptr, false, row.process.state != 'T')) {
        send_signal(pick, SIGSTOP);
    }
    if (ImGui::MenuItem("Continue", nullptr, false, row.process.state == 'T')) {
        send_signal(pick, SIGCONT);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Copy command line", nullptr, false, !row.process.command.empty())) {
        ImGui::SetClipboardText(row.process.command.c_str());
    }
}

void Monitor::process_footer() {
    const auto& p = app_.palette();
    const auto total = sampler_.processes().size();
    std::string text = shown_ == static_cast<int>(total) ? std::format("{} processes", total)
                                                         : std::format("{} of {} processes", shown_, total);
    text += std::format("   ·   CPU {:.0f}%", sampler_.cpu().last());
    if (const auto& m = sampler_.memory()) {
        text += std::format("   ·   Memory {} of {}", format_bytes(static_cast<double>(m->used())),
                            format_bytes(static_cast<double>(m->total)));
    }
    if (interval_ == 0) {
        text += "   ·   Paused";
    }
    ImGui::Dummy({0, 4});
    ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(p.text_dim));
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
}

void Monitor::confirm_popup() {
    const auto& p = app_.palette();
    if (confirm_ && !ImGui::IsPopupOpen("##confirm")) {
        ImGui::OpenPopup("##confirm");
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({viewport->Pos.x + (viewport->Size.x / 2), viewport->Pos.y + (viewport->Size.y / 2)},
                            ImGuiCond_Always, {0.5F, 0.5F});
    ImGui::SetNextWindowSize({420, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {20, 18});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10);
    if (!ImGui::BeginPopupModal("##confirm", nullptr,
                                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
        ImGui::PopStyleVar(2);
        return;
    }
    ImGui::PopStyleVar(2);
    if (!confirm_) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const bool kill = confirm_->signal == SIGKILL;
    ImGui::PushFont(nullptr, 17);
    ImGui::TextWrapped(kill ? "Kill “%s”?" : "End “%s”?", confirm_->target.name.c_str());
    ImGui::PopFont();
    ImGui::Dummy({0, 2});
    ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(p.text_dim));
    ImGui::TextWrapped(kill ? "Process %d stops at once. Anything unsaved in it is lost."
                            : "Process %d is asked to quit. Anything unsaved in it may be lost.",
                       confirm_->target.pid);
    ImGui::PopStyleColor();
    ImGui::Dummy({0, 8});

    const char* action = kill ? "Kill process" : "End process";
    const float width = 120;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - (2 * width) - 8);
    bool cancel = ImGui::Button("Cancel", {width, 0}) || ImGui::IsKeyPressed(ImGuiKey_Escape);
    ImGui::SameLine(0, 8);
    ImGui::PushStyleColor(ImGuiCol_Button, ui::to_imvec4(p.accent));
    ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(p.accent_text));
    const bool confirmed = ImGui::Button(action, {width, 0}) || ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                           ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
    ImGui::PopStyleColor(2);
    if (confirmed) {
        const Confirm c = *confirm_;
        send_signal(c.target, c.signal);
        cancel = true;
    }
    if (cancel) {
        confirm_.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace tetra::monitor
