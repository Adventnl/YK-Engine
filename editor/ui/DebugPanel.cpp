#include "ui/Panels.hpp"
#include <cstdio>
#include <variant>

// The Inspector's Debug tab: what the running game is doing, read from the runtime itself. While no
// game runs it says so and offers to start one; nothing here is invented or sampled by the editor.
namespace yk::editor::ui {
namespace {
void row(const char *name, const std::string &value, Color color = vs::text) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextColored(imColor(vs::textDim), "%s", name);
    ImGui::TableSetColumnIndex(1);
    ImGui::PushFont(fonts().mono, 12.0F);
    ImGui::TextColored(imColor(color), "%s", value.c_str());
    ImGui::PopFont();
}

std::string decimal(double value, int places = 3) {
    char text[64];
    std::snprintf(text, sizeof text, "%.*f", places, value);
    std::string result = text;
    // 12.500 -> 12.5, 3.000 -> 3
    if (result.find('.') != std::string::npos) {
        while (result.back() == '0')
            result.pop_back();
        if (result.back() == '.')
            result.pop_back();
    }
    return result;
}

std::string vector(Vec2 value) {
    return decimal(static_cast<double>(value.x), 2) + ", " +
           decimal(static_cast<double>(value.y), 2);
}

bool section(const char *id, const char *title, bool &open) {
    return sectionHeader(id, title, open, true);
}

void notRunning(EditorState &state) {
    ImGui::Dummy({1.0F, dp(24.0F)});
    const float width = ImGui::GetContentRegionAvail().x;
    const auto centered = [&](const char *text, Color color) {
        const float textWidth = ImGui::CalcTextSize(text).x;
        ImGui::SetCursorPosX(std::max(0.0F, (width - textWidth) * 0.5F));
        ImGui::TextColored(imColor(color), "%s", text);
    };
    centered("The game is not running", vs::text);
    centered("Press Play to watch its variables, timing", vs::textDim);
    centered("and the selected entity here.", vs::textDim);
    ImGui::Dummy({1.0F, dp(10.0F)});
    const float button = dp(110.0F);
    ImGui::SetCursorPosX(std::max(0.0F, (width - button) * 0.5F));
    ImGui::BeginDisabled(!state.document || !state.project);
    if (ImGui::Button("Play  (F5)", {button, 0.0F}))
        state.startPlay();
    markItem("debug/play");
    ImGui::EndDisabled();
}
} // namespace

void debugPanel(EditorState &state) {
    if (!state.playing()) {
        notRunning(state);
        return;
    }
    static bool runtimeOpen = true, variablesOpen = true, entityOpen = true;
    const GameRuntime &runtime = state.play->runtime();
    const Scene &scene = state.play->runtime().scene();
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {4.0F, 3.0F});

    if (section("debug/runtime", "Runtime", runtimeOpen) &&
        ImGui::BeginTable("##runtime", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, dp(120.0F));
        ImGui::TableSetupColumn("value");
        row("State", state.play->paused() ? "Paused" : "Running",
            state.play->paused() ? vs::warning : vs::success);
        row("Scene", state.play->scenePath().empty() ? "(unsaved copy)" : state.play->scenePath());
        row("Tick", std::to_string(static_cast<unsigned long long>(runtime.tick())));
        row("Game time", decimal(runtime.time(), 2) + " s");
        row("Entities", std::to_string(scene.size()));
        const auto physics = runtime.physics().stats();
        row("Bodies (awake)",
            std::to_string(physics.bodies) + " (" + std::to_string(physics.awakeBodies) + ")");
        ImGui::EndTable();
    }

    const Blackboard &board = state.play->runtime().blackboard();
    if (section("debug/variables", "Variables", variablesOpen)) {
        if (board.values().empty()) {
            ImGui::TextColored(imColor(vs::textDim), "The game has set no variables yet.");
        } else if (ImGui::BeginTable("##variables", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, dp(120.0F));
            ImGui::TableSetupColumn("value");
            for (const auto &[name, value] : board.values()) {
                if (isNumeric(value) && !std::holds_alternative<bool>(value))
                    row(name.c_str(), decimal(toNumber(value)));
                else if (std::holds_alternative<std::string>(value))
                    row(name.c_str(), "\"" + std::get<std::string>(value) + "\"");
                else
                    row(name.c_str(), toText(value));
            }
            ImGui::EndTable();
        }
    }

    if (section("debug/entity", "Selected Entity", entityOpen)) {
        const Entity *entity = scene.find(state.playSelection);
        if (!entity) {
            ImGui::TextColored(imColor(vs::textDim), "Select an entity in the hierarchy.");
        } else if (ImGui::BeginTable("##entity", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, dp(120.0F));
            ImGui::TableSetupColumn("value");
            row("Name", entity->name());
            const Transform2D world = entity->worldTransform();
            row("World position", vector(world.position));
            row("Rotation", decimal(static_cast<double>(world.rotationDegrees), 1) + " deg");
            if (const auto body = runtime.bodyOf(entity->id())) {
                if (const auto state2 = runtime.physics().state(*body)) {
                    const physics::BodyState &physical = state2.value();
                    row("Velocity", vector(physical.linearVelocity) + " m/s");
                    row("Physics", std::string(physical.awake ? "awake" : "asleep") +
                                       (physical.enabled ? "" : ", disabled"));
                }
            } else {
                row("Physics", "no body");
            }
            const auto &touching = runtime.overlapping(entity->id());
            if (!touching.empty()) {
                std::string names;
                for (const EntityId other : touching) {
                    if (const Entity *found = scene.find(other))
                        names += (names.empty() ? "" : ", ") + found->name();
                }
                row("Overlapping", names);
            }
            ImGui::EndTable();
        }
    }
    ImGui::PopStyleVar();
}
} // namespace yk::editor::ui
