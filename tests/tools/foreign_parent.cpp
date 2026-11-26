// tetra-foreign-parent [TITLE]: stands in for an app's main window in the
// portal tests. A plain toplevel, exported with xdg-foreign v2; prints
// "wayland:HANDLE" (a portal parent_window) on stdout once it has one.

#include "app.hpp"
#include "protocols.hpp"
#include "style.hpp"
#include "window.hpp"

#include <imgui.h>

#include <cstdio>
#include <print>
#include <span>
#include <string>

namespace {

const zxdg_exported_v2_listener kExported = {
    .handle = [](void* /*data*/, zxdg_exported_v2* /*exported*/, const char* handle) {
        std::println("wayland:{}", handle);
        std::fflush(stdout);
    },
};

} // namespace

int main(int argc, char* argv[]) {
    const auto args = std::span(argv, static_cast<std::size_t>(argc));
    const std::string title = args.size() > 1 ? args[1] : "App";
    auto created = tetra::ui::App::create("tetra-foreign-parent");
    if (!created) {
        std::println(stderr, "tetra-foreign-parent: {}", created.error());
        return 1;
    }
    tetra::ui::App& app = **created;
    tetra::ui::ToplevelWindow window(app, {.title = title, .width = 640, .height = 480, .parent = {}});
    window.on_draw = [&app, &title] {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, tetra::ui::to_imvec4(app.palette().surface));
        ImGui::Begin("##app", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        ImGui::PushFont(nullptr, 22);
        ImGui::TextUnformatted(title.c_str());
        ImGui::PopFont();
        ImGui::TextDisabled("The app that asked; the dialog belongs to this window.");
        ImGui::End();
        ImGui::PopStyleColor();
    };
    auto* exporter = static_cast<zxdg_exporter_v2*>(app.bind_global(&zxdg_exporter_v2_interface, 1));
    if (exporter == nullptr) {
        std::println(stderr, "tetra-foreign-parent: no zxdg_exporter_v2");
        return 1;
    }
    zxdg_exported_v2* exported = zxdg_exporter_v2_export_toplevel(exporter, window.surface());
    zxdg_exported_v2_add_listener(exported, &kExported, nullptr);
    const int code = app.run();
    zxdg_exported_v2_destroy(exported);
    zxdg_exporter_v2_destroy(exporter);
    return code;
}
