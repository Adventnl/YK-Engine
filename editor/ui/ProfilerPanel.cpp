#include "ui/Panels.hpp"
#include <algorithm>
#include <cstdio>
#include <numeric>

namespace yk::editor::ui {
namespace {
void statRow(const char *name, const std::string &value) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextColored(imColor(vs::textDim), "%s", name);
    ImGui::TableSetColumnIndex(1);
    ImGui::PushFont(fonts().mono, 12.5F);
    ImGui::TextUnformatted(value.c_str());
    ImGui::PopFont();
}
std::string number(std::size_t value) {
    return std::to_string(value);
}
} // namespace

void profilerPanel(EditorState &state) {
    const std::vector<float> &samples = state.frameMilliseconds;
    float latest = 0.0F, average = 0.0F, worst = 0.0F;
    if (!samples.empty()) {
        latest = samples.back();
        average = std::accumulate(samples.begin(), samples.end(), 0.0F) /
                  static_cast<float>(samples.size());
        worst = *std::max_element(samples.begin(), samples.end());
    }
    const float graphWidth = std::max(120.0F, ImGui::GetContentRegionAvail().x * 0.55F);
    const float graphHeight = std::max(50.0F, ImGui::GetContentRegionAvail().y - 6.0F);
    ImGui::BeginGroup();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImDrawList &list = *ImGui::GetWindowDrawList();
    list.AddRectFilled(at, {at.x + graphWidth, at.y + graphHeight}, packed(vs::editorBg));
    const float top = std::max(20.0F, worst * 1.2F);
    // The 60 fps budget.
    const float budgetY = at.y + graphHeight * (1.0F - 16.7F / top);
    if (budgetY > at.y)
        list.AddLine({at.x, budgetY}, {at.x + graphWidth, budgetY}, IM_COL32(115, 201, 145, 90));
    if (samples.size() > 1) {
        const float step = graphWidth / static_cast<float>(state.frameMillisecondsCapacity - 1);
        const float startX = at.x + graphWidth - step * static_cast<float>(samples.size() - 1);
        for (std::size_t i = 1; i < samples.size(); ++i) {
            const auto y = [&](float ms) {
                return at.y + graphHeight * (1.0F - std::min(ms, top) / top);
            };
            list.AddLine({startX + step * static_cast<float>(i - 1), y(samples[i - 1])},
                         {startX + step * static_cast<float>(i), y(samples[i])}, packed(vs::focus),
                         1.5F);
        }
    }
    char text[64];
    std::snprintf(text, sizeof text, "%.1f ms  (avg %.1f, worst %.1f)", static_cast<double>(latest),
                  static_cast<double>(average), static_cast<double>(worst));
    list.AddText({at.x + 8.0F, at.y + 4.0F}, packed(vs::text), text);
    ImGui::Dummy({graphWidth, graphHeight});
    ImGui::EndGroup();
    markItem("profiler/graph");
    ImGui::SameLine(0.0F, 12.0F);

    ImGui::BeginChild("##profilerstats", {0.0F, 0.0F}, ImGuiChildFlags_None);
    if (ImGui::BeginTable("##stats", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, 210.0F);
        ImGui::TableSetupColumn("value");
        std::snprintf(text, sizeof text, "%.0f fps", static_cast<double>(state.framesPerSecond));
        statRow("Editor", text);
        const SceneRenderStats &draw = state.sceneRenderer->stats();
        statRow("Sprites drawn / culled", number(draw.sprites) + " / " + number(draw.culled));
        statRow("Quads / particles", number(draw.quads) + " / " + number(draw.particles));
        if (state.renderer) {
            const auto frame = state.renderer->lastFrameStats();
            statRow("Renderer sprites/lines/passes", number(frame.sprites) + " / " +
                                                         number(frame.lines) + " / " +
                                                         number(frame.passes));
        }
        if (const Scene *scene = state.visibleScene())
            statRow("Entities", number(scene->size()));
        if (state.play) {
            const auto physics = state.play->runtime().physics().stats();
            statRow("Game tick",
                    std::to_string(static_cast<unsigned long long>(state.play->runtime().tick())));
            statRow("Bodies / shapes / awake", number(physics.bodies) + " / " +
                                                   number(physics.shapes) + " / " +
                                                   std::to_string(physics.awakeBodies));
            statRow("Game time", std::to_string(state.play->runtime().time()).substr(0, 6) + " s");
        } else {
            statRow("Game", "not running");
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}
} // namespace yk::editor::ui
