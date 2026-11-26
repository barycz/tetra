#include "monitor.hpp"

#include "style.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace tetra::monitor {

namespace {

constexpr float kCardPadding = 18;

struct Card {
    ImVec2 start;
    float width = 0;
};
Card g_card; // one card at a time

// The top of a rate graph's scale: 1, 2 or 5 times a power of ten, at least 10 kB/s.
double nice_max(double value) {
    double scale = 10'000;
    for (;;) {
        for (double f : {1.0, 2.0, 5.0}) {
            if (value <= scale * f) {
                return scale * f;
            }
        }
        scale *= 10;
    }
}

void page_title(const char* title) {
    ImGui::PushFont(nullptr, 24);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
}

} // namespace

void Monitor::begin_card(const char* title, const std::string& summary) {
    const auto& p = app_.palette();
    ImGui::Dummy({0, 10});
    g_card.start = ImGui::GetCursorScreenPos();
    g_card.width = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);
    ImGui::Indent(kCardPadding);
    ImGui::Dummy({0, 10});
    ImGui::PushFont(nullptr, 17);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    if (!summary.empty()) {
        ImGui::SameLine();
        const float width = ImGui::CalcTextSize(summary.c_str()).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, ImGui::GetContentRegionAvail().x - width -
                                                                          kCardPadding));
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(p.text_dim));
        ImGui::TextUnformatted(summary.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Dummy({0, 2});
}

void Monitor::end_card() {
    const auto& p = app_.palette();
    ImGui::Dummy({0, 6});
    ImGui::Unindent(kCardPadding);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSetCurrent(0);
    const ImVec2 end{g_card.start.x + g_card.width, ImGui::GetCursorScreenPos().y};
    dl->AddRectFilled(g_card.start, end, ui::to_imu32(p.surface), 10);
    dl->AddRect(g_card.start, end, ui::to_imu32(p.separator), 10);
    dl->ChannelsMerge();
}

void Monitor::legend(ImU32 color, const char* label, const std::string& value) {
    const auto& p = app_.palette();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddRectFilled({pos.x, pos.y + (h / 2) - 5}, {pos.x + 10, pos.y + (h / 2) + 5},
                                              color, 3);
    ImGui::SetCursorScreenPos({pos.x + 18, pos.y});
    ImGui::TextUnformatted(label);
    ImGui::SameLine(0, 10);
    ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(p.text_dim));
    ImGui::TextUnformatted(value.c_str());
    ImGui::PopStyleColor();
}

void Monitor::graph(const char* id, ImVec2 size, const std::vector<Series>& series, double max,
                    const char* top_label) {
    const auto& p = app_.palette();
    ImGui::PushID(id);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImGui::PopID();
    const ImVec2 end{pos.x + size.x, pos.y + size.y};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, end, ui::to_imu32(p.window), 6);
    dl->PushClipRect(pos, end, true);
    if (size.y >= 60) {
        for (int i = 1; i < 4; ++i) {
            const float y = std::round(pos.y + (size.y * static_cast<float>(i) / 4)) + 0.5F;
            dl->AddLine({pos.x, y}, {end.x, y}, ui::to_imu32(p.separator));
        }
    }
    const float step = size.x / static_cast<float>(History::kLength - 1);
    const float base = end.y;
    for (const auto& s : series) {
        const auto& values = s.history->values();
        if (values.size() < 2 || max <= 0) {
            continue;
        }
        std::vector<ImVec2> points;
        points.reserve(values.size());
        for (std::size_t i = 0; i < values.size(); ++i) {
            const auto from_end = static_cast<float>(values.size() - 1 - i);
            const auto fraction = static_cast<float>(std::clamp(values[i] / max, 0.0, 1.0));
            points.push_back({end.x - (from_end * step), base - (fraction * (size.y - 2)) - 1});
        }
        // The area under the line, as quads (AA off: no seams between them).
        const ImU32 fill = (s.color & ~IM_COL32_A_MASK) | (IM_COL32(0, 0, 0, 56) & IM_COL32_A_MASK);
        const ImDrawFlags flags = dl->Flags;
        dl->Flags &= ~ImDrawFlags_AAFill;
        for (std::size_t i = 1; i < points.size(); ++i) {
            dl->AddQuadFilled({points[i - 1].x, base}, points[i - 1], points[i], {points[i].x, base}, fill);
        }
        dl->Flags = flags;
        dl->AddPolyline(points.data(), static_cast<int>(points.size()), s.color, 1.5F);
    }
    if (top_label != nullptr) {
        ImGui::PushFont(nullptr, 12);
        dl->AddText({pos.x + 6, pos.y + 4}, ui::to_imu32(p.text_dim), top_label);
        ImGui::PopFont();
    }
    dl->PopClipRect();
    dl->AddRect(pos, end, ui::to_imu32(p.separator), 6);
}

void Monitor::page_resources() {
    const auto& p = app_.palette();
    const ImU32 first = ui::to_imu32(p.accent);
    const ImU32 second = app_.config().theme.dark ? IM_COL32(0xF0, 0xA0, 0x48, 0xFF) : IM_COL32(0xD0, 0x70, 0x20, 0xFF);
    const std::string span = std::format("{} seconds",
                                         (History::kLength - 1) * static_cast<std::size_t>(std::max(interval_, 500)) /
                                             1000);
    page_title("Resources");

    // CPU: the whole machine, then each core.
    const auto& cores = sampler_.cores();
    begin_card("CPU", std::format("{:.0f}%   ·   {} cores", sampler_.cpu().last(), cores.size()));
    const float width = ImGui::GetContentRegionAvail().x - kCardPadding;
    graph("cpu", {width, 140}, {{&sampler_.cpu(), first}}, 100, "100%");
    if (!cores.empty()) {
        constexpr float kGap = 10;
        const int columns = std::clamp(static_cast<int>((width + kGap) / (150 + kGap)), 1, 8);
        const float tile = (width - (kGap * static_cast<float>(columns - 1))) / static_cast<float>(columns);
        ImGui::PushFont(nullptr, 13);
        for (std::size_t i = 0; i < cores.size(); ++i) {
            if (i % static_cast<std::size_t>(columns) != 0) {
                ImGui::SameLine(0, kGap);
            } else {
                ImGui::Dummy({0, 2});
            }
            ImGui::BeginGroup();
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const std::string value = std::format("{:.0f}%", cores[i].last());
            ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(p.text_dim));
            ImGui::Text("CPU %zu", i);
            ImGui::PopStyleColor();
            ImGui::GetWindowDrawList()->AddText({at.x + tile - ImGui::CalcTextSize(value.c_str()).x, at.y},
                                                ui::to_imu32(p.text), value.c_str());
            graph(std::format("core{}", i).c_str(), {tile, 40}, {{&cores[i], first}}, 100, nullptr);
            ImGui::EndGroup();
        }
        ImGui::PopFont();
    }
    end_card();

    // Memory and swap.
    if (const auto& m = sampler_.memory()) {
        begin_card("Memory and swap", span);
        const auto percent = [](uint64_t part, uint64_t whole) {
            return whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole);
        };
        legend(first, "Memory",
               std::format("{} of {} ({:.0f}%)   ·   cache {}", format_bytes(static_cast<double>(m->used())),
                           format_bytes(static_cast<double>(m->total)), percent(m->used(), m->total),
                           format_bytes(static_cast<double>(m->cache))));
        legend(second, "Swap",
               m->swap_total == 0 ? std::string("none")
                                  : std::format("{} of {} ({:.0f}%)", format_bytes(static_cast<double>(m->swap_used())),
                                                format_bytes(static_cast<double>(m->swap_total)),
                                                percent(m->swap_used(), m->swap_total)));
        ImGui::Dummy({0, 2});
        graph("memory", {width, 120}, {{&sampler_.memory_history(), first}, {&sampler_.swap_history(), second}}, 100,
              "100%");
        end_card();
    }

    // Network and disks: rates, scaled to the busiest moment shown.
    const auto rates = [&](const char* title, const char* id, const IoRates& io, const char* in_label,
                           const char* out_label, const char* in_total, const char* out_total) {
        begin_card(title, span);
        legend(first, in_label,
               std::format("{}   ·   {} {}", format_rate(io.in), format_bytes(static_cast<double>(io.total.in)),
                           in_total));
        legend(second, out_label,
               std::format("{}   ·   {} {}", format_rate(io.out), format_bytes(static_cast<double>(io.total.out)),
                           out_total));
        ImGui::Dummy({0, 2});
        const double max = nice_max(std::max(io.in_history.max(), io.out_history.max()));
        graph(id, {width, 120}, {{&io.in_history, first}, {&io.out_history, second}}, max,
              format_rate(max).c_str());
        end_card();
    };
    rates("Network", "network", sampler_.network(), "Receiving", "Sending", "received since boot",
          "sent since boot");
    rates("Disks", "disks", sampler_.disks(), "Reading", "Writing", "read since boot", "written since boot");
}

void Monitor::page_filesystems() {
    const auto& p = app_.palette();
    page_title("File systems");
    ImGui::Dummy({0, 10});
    const auto& mounts = sampler_.mounts();
    if (mounts.empty()) {
        ImGui::TextDisabled("No file systems found.");
        return;
    }
    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
                                       ImGuiTableFlags_BordersOuter | ImGuiTableFlags_SizingFixedFit;
    ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ui::to_imvec4(ui::mix(p.window, p.hover)));
    ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, ui::to_imvec4(p.separator));
    ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, ui::to_imvec4(p.surface));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {8, 6});
    if (ImGui::BeginTable("##filesystems", 6, kFlags)) {
        ImGui::TableSetupColumn("Folder", ImGuiTableColumnFlags_WidthStretch, 1);
        ImGui::TableSetupColumn("Device", ImGuiTableColumnFlags_WidthStretch, 1);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 52);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 72);
        ImGui::TableSetupColumn("Available", ImGuiTableColumnFlags_WidthFixed, 72);
        ImGui::TableSetupColumn("Used", ImGuiTableColumnFlags_WidthFixed, 190);
        ImGui::TableHeadersRow();
        for (const auto& m : mounts) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(m.mount_point.c_str());
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, ui::to_imvec4(p.text_dim));
            ImGui::TextUnformatted(m.device.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(m.type.c_str());
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(format_bytes(static_cast<double>(m.total)).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(format_bytes(static_cast<double>(m.available)).c_str());
            ImGui::TableNextColumn();
            // Used: what is not free (blocks reserved for root count as used
            // for everyone else, as df shows).
            const uint64_t used = m.total - std::min(m.total, m.free);
            const uint64_t usable = used + m.available;
            const float fraction =
                usable == 0 ? 0.0F : static_cast<float>(static_cast<double>(used) / static_cast<double>(usable));
            const std::string label =
                std::format("{}   {:.0f}%", format_bytes(static_cast<double>(used)), fraction * 100);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                                  ui::to_imvec4(fraction > 0.9F ? p.close_hover : p.accent));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ui::to_imvec4(p.window));
            ImGui::ProgressBar(fraction, {-1, 0}, label.c_str());
            ImGui::PopStyleColor(2);
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
}

} // namespace tetra::monitor
