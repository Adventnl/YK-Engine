#include "ui/Panels.hpp"
#include "yk/animation/AnimationController.hpp"
#include "yk/animation/AnimationSet.hpp"
#include "yk/core/FileIO.hpp"
#include "yk/core/Log.hpp"
#include "yk/scene/SceneSerializer.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>

// What the Inspector shows for a file picked in the Explorer: a preview and the file's import or
// authoring settings. Textures edit their .ykmeta sidecar, animations their clips' timing, sounds
// play, scenes and prefabs summarize themselves. Everything that writes a file says so in the
// console, and nothing is written until Apply.
namespace yk::editor::ui {
namespace {
std::string humanSize(std::uintmax_t bytes) {
    char text[32];
    if (bytes >= 1024ULL * 1024ULL)
        std::snprintf(text, sizeof text, "%.1f MB", static_cast<double>(bytes) / 1048576.0);
    else if (bytes >= 1024ULL)
        std::snprintf(text, sizeof text, "%.1f KB", static_cast<double>(bytes) / 1024.0);
    else
        std::snprintf(text, sizeof text, "%llu bytes", static_cast<unsigned long long>(bytes));
    return text;
}

Icon iconOf(AssetKind kind) {
    switch (kind) {
    case AssetKind::Scene:
        return Icon::Scene;
    case AssetKind::Prefab:
        return Icon::Prefab;
    case AssetKind::Texture:
        return Icon::Image;
    case AssetKind::Sound:
        return Icon::Sound;
    case AssetKind::Animation:
        return Icon::Animation;
    case AssetKind::Controller:
        return Icon::Code;
    case AssetKind::TextureMeta:
        return Icon::Settings;
    case AssetKind::Other:
        break;
    }
    return Icon::File;
}

const char *kindName(AssetKind kind) {
    switch (kind) {
    case AssetKind::Scene:
        return "Scene";
    case AssetKind::Prefab:
        return "Prefab";
    case AssetKind::Texture:
        return "Texture";
    case AssetKind::Sound:
        return "Sound";
    case AssetKind::Animation:
        return "Animation clips";
    case AssetKind::Controller:
        return "Animation controller";
    case AssetKind::TextureMeta:
        return "Texture import settings";
    case AssetKind::Other:
        break;
    }
    return "File";
}

// A small labelled value row.
void info(const char *name, const std::string &value) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(imColor(vs::textDim), "%s", name);
    ImGui::SameLine(112.0F);
    ImGui::TextUnformatted(value.c_str());
}

void checker(ImDrawList &list, ImVec2 min, ImVec2 max) {
    list.AddRectFilled(min, max, IM_COL32(38, 38, 38, 255));
    constexpr float cell = 10.0F;
    for (float y = min.y; y < max.y; y += cell)
        for (float x = min.x; x < max.x; x += cell)
            if ((static_cast<int>((x - min.x) / cell) + static_cast<int>((y - min.y) / cell)) % 2 ==
                0)
                list.AddRectFilled({x, y}, {std::min(x + cell, max.x), std::min(y + cell, max.y)},
                                   IM_COL32(48, 48, 48, 255));
}

ImTextureRef textureRef(EditorState &state, TextureHandle handle) {
    SDL_Texture *native = state.renderer ? state.renderer->nativeTexture(handle) : nullptr;
    return ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(native)));
}

// Draws `handle` (or the cell of a sheet) fitted into a checkerboard box; returns the image rect.
struct Fitted {
    ImVec2 min, max;
    float scale{1.0F};
};
Fitted drawFitted(EditorState &state, TextureHandle handle, Vec2 pixels, float boxHeight,
                  ImVec2 uv0 = {0.0F, 0.0F}, ImVec2 uv1 = {1.0F, 1.0F}, Vec2 sourcePixels = {}) {
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList &list = *ImGui::GetWindowDrawList();
    checker(list, origin, {origin.x + width, origin.y + boxHeight});
    const Vec2 shown = sourcePixels.x > 0.0F ? sourcePixels : pixels;
    Fitted fitted;
    if (shown.x > 0.0F && shown.y > 0.0F) {
        fitted.scale = std::min((width - 8.0F) / shown.x, (boxHeight - 8.0F) / shown.y);
        fitted.scale = std::min(fitted.scale, 8.0F);
        const ImVec2 size{shown.x * fitted.scale, shown.y * fitted.scale};
        fitted.min = {origin.x + (width - size.x) * 0.5F, origin.y + (boxHeight - size.y) * 0.5F};
        fitted.max = {fitted.min.x + size.x, fitted.min.y + size.y};
        list.AddImage(textureRef(state, handle), fitted.min, fitted.max, uv0, uv1);
        list.AddRect(fitted.min, fitted.max, IM_COL32(255, 255, 255, 40));
    }
    ImGui::Dummy({width, boxHeight});
    return fitted;
}

// ------------------------------------------------------------------------------------- textures
// The draft of a texture's import settings, kept while the same texture stays selected.
struct TextureDraft {
    std::string path;
    TextureMeta saved, draft;
    std::string error;
};
TextureDraft textureDraft;

std::string metaText(const TextureMeta &meta) {
    return meta.toJson().dump();
}

void loadTextureDraft(EditorState &state, const std::string &path) {
    textureDraft = {};
    textureDraft.path = path;
    if (auto sidecar = state.project->assets().readText(TextureMeta::sidecarPath(path)); sidecar)
        if (auto parsed = Json::parse(sidecar.value()); parsed)
            if (auto decoded = TextureMeta::fromJson(parsed.value()); decoded)
                textureDraft.saved = decoded.value();
    textureDraft.draft = textureDraft.saved;
}

void applyTextureDraft(EditorState &state, const std::string &path) {
    TextureDraft &d = textureDraft;
    // Round trip through the parser: what would not load again is refused here.
    if (auto valid = TextureMeta::fromJson(d.draft.toJson()); !valid) {
        d.error = valid.error();
        return;
    }
    const auto file = state.project->project().resolve(TextureMeta::sidecarPath(path));
    if (!file) {
        d.error = file.error();
        return;
    }
    if (d.draft.empty()) {
        std::error_code error;
        std::filesystem::remove(file.value(), error);
    } else if (auto written = writeTextFileAtomic(file.value(), d.draft.toJson().dump(2) + "\n");
               !written) {
        d.error = written.error();
        return;
    }
    d.saved = d.draft;
    d.error.clear();
    state.project->refresh();
    if (state.sceneRenderer && state.renderer)
        state.sceneRenderer->reload(*state.renderer, path);
    log(LogLevel::Info, "editor",
        d.draft.empty() ? "Removed the import settings of " + path
                        : "Saved the import settings of " + path);
}

void textureInspector(EditorState &state, const std::string &path) {
    if (textureDraft.path != path)
        loadTextureDraft(state, path);
    TextureDraft &d = textureDraft;
    TextureMeta &meta = d.draft;
    const ResolvedTexture resolved = resolve(state.project->project().textures, meta);
    const auto texture = state.sceneRenderer && state.renderer
                             ? state.sceneRenderer->loadedTexture(*state.renderer, path)
                             : std::nullopt;
    if (!texture) {
        ImGui::TextColored(imColor(vs::error), "This image cannot be loaded.");
    } else {
        const Fitted image = drawFitted(state, texture->handle, texture->pixels, 220.0F);
        ImDrawList &list = *ImGui::GetWindowDrawList();
        // The sprite-sheet grid and nine-slice borders over the picture.
        for (int c = 1; c < resolved.columns; ++c) {
            const float x = image.min.x + (image.max.x - image.min.x) * static_cast<float>(c) /
                                              static_cast<float>(resolved.columns);
            list.AddLine({x, image.min.y}, {x, image.max.y}, IM_COL32(0, 200, 255, 140));
        }
        for (int r = 1; r < resolved.rows; ++r) {
            const float y = image.min.y + (image.max.y - image.min.y) * static_cast<float>(r) /
                                              static_cast<float>(resolved.rows);
            list.AddLine({image.min.x, y}, {image.max.x, y}, IM_COL32(0, 200, 255, 140));
        }
        if (resolved.hasBorder() && texture->pixels.x > 0.0F) {
            const float sx = (image.max.x - image.min.x) / texture->pixels.x;
            const float sy = (image.max.y - image.min.y) / texture->pixels.y;
            const auto &b = resolved.border; // Left, top, right, bottom.
            const ImU32 guide = IM_COL32(255, 196, 64, 200);
            list.AddLine({image.min.x + static_cast<float>(b[0]) * sx, image.min.y},
                         {image.min.x + static_cast<float>(b[0]) * sx, image.max.y}, guide);
            list.AddLine({image.max.x - static_cast<float>(b[2]) * sx, image.min.y},
                         {image.max.x - static_cast<float>(b[2]) * sx, image.max.y}, guide);
            list.AddLine({image.min.x, image.min.y + static_cast<float>(b[1]) * sy},
                         {image.max.x, image.min.y + static_cast<float>(b[1]) * sy}, guide);
            list.AddLine({image.min.x, image.max.y - static_cast<float>(b[3]) * sy},
                         {image.max.x, image.max.y - static_cast<float>(b[3]) * sy}, guide);
        }
        char text[96];
        std::snprintf(text, sizeof text, "%d x %d pixels", static_cast<int>(texture->pixels.x),
                      static_cast<int>(texture->pixels.y));
        info("Size", text);
        std::snprintf(text, sizeof text, "%.2f x %.2f units",
                      static_cast<double>(texture->pixels.x / resolved.pixelsPerUnit),
                      static_cast<double>(texture->pixels.y / resolved.pixelsPerUnit));
        info("In the world", text);
        if (resolved.columns * resolved.rows > 1) {
            std::snprintf(text, sizeof text, "%d x %d cells of %d x %d pixels", resolved.columns,
                          resolved.rows, static_cast<int>(texture->pixels.x) / resolved.columns,
                          static_cast<int>(texture->pixels.y) / resolved.rows);
            info("Sheet", text);
        }
    }

    ImGui::Spacing();
    ImGui::PushFont(fonts().semibold, 13.0F);
    ImGui::TextUnformatted("Import settings");
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_Text, imColor(vs::textDim));
    ImGui::TextWrapped(
        "Saved next to the picture as %s. Unchecked settings use the project's defaults.",
        std::filesystem::path(TextureMeta::sidecarPath(path)).filename().string().c_str());
    ImGui::PopStyleColor();
    ImGui::Spacing();

    bool has = meta.pixelsPerUnit.has_value();
    if (ImGui::Checkbox("Pixels per unit", &has))
        meta.pixelsPerUnit =
            has ? std::optional<float>(state.project->project().textures.pixelsPerUnit)
                : std::nullopt;
    markItem("asset/inspector/ppu/override");
    if (meta.pixelsPerUnit) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        float ppu = *meta.pixelsPerUnit;
        if (ImGui::DragFloat("##ppu", &ppu, 1.0F, 1.0F, 4096.0F, "%.0f"))
            meta.pixelsPerUnit = std::clamp(ppu, 1.0F, 4096.0F);
        markItem("asset/inspector/ppu");
    }
    has = meta.filter.has_value();
    if (ImGui::Checkbox("Filter", &has))
        meta.filter = has ? std::optional<TextureFilter>(state.project->project().textures.filter)
                          : std::nullopt;
    markItem("asset/inspector/filter/override");
    if (meta.filter) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##filter", *meta.filter == TextureFilter::Nearest
                                              ? "Nearest (crisp)"
                                              : "Linear (smooth)")) {
            if (ImGui::Selectable("Linear (smooth)", *meta.filter == TextureFilter::Linear))
                meta.filter = TextureFilter::Linear;
            if (ImGui::Selectable("Nearest (crisp)", *meta.filter == TextureFilter::Nearest))
                meta.filter = TextureFilter::Nearest;
            ImGui::EndCombo();
        }
        markItem("asset/inspector/filter");
    }
    has = meta.columns.has_value() || meta.rows.has_value();
    if (ImGui::Checkbox("Sprite sheet", &has)) {
        meta.columns = has ? std::optional<int>(1) : std::nullopt;
        meta.rows = has ? std::optional<int>(1) : std::nullopt;
    }
    markItem("asset/inspector/sheet/override");
    if (has) {
        ImGui::SameLine();
        int grid[2] = {meta.columns.value_or(1), meta.rows.value_or(1)};
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::DragInt2("##grid", grid, 0.2F, 1, 256, "%d")) {
            meta.columns = std::clamp(grid[0], 1, 256);
            meta.rows = std::clamp(grid[1], 1, 256);
        }
        markItem("asset/inspector/sheet");
        tooltip("Columns and rows of the picture, so sprites can show one cell of it.");
    }
    has = meta.border.has_value();
    if (ImGui::Checkbox("Slice border", &has))
        meta.border =
            has ? std::optional<std::array<int, 4>>(std::array<int, 4>{8, 8, 8, 8}) : std::nullopt;
    markItem("asset/inspector/border/override");
    if (meta.border) {
        ImGui::SameLine();
        int border[4] = {(*meta.border)[0], (*meta.border)[1], (*meta.border)[2],
                         (*meta.border)[3]};
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::DragInt4("##border", border, 0.2F, 0, 4096, "%d"))
            for (int i = 0; i < 4; ++i)
                (*meta.border)[static_cast<std::size_t>(i)] = std::max(0, border[i]);
        markItem("asset/inspector/border");
        tooltip(
            "Left, top, right and bottom edge in pixels that a Sliced sprite keeps at their size; "
            "the middle stretches or tiles.");
    }
    ImGui::Spacing();
    const bool changed = metaText(meta) != metaText(d.saved);
    ImGui::BeginDisabled(!changed);
    if (ImGui::Button("Apply", {96.0F, 0.0F}))
        applyTextureDraft(state, path);
    markItem("asset/inspector/apply");
    ImGui::SameLine();
    if (ImGui::Button("Revert", {96.0F, 0.0F})) {
        meta = d.saved;
        d.error.clear();
    }
    markItem("asset/inspector/revert");
    ImGui::EndDisabled();
    if (!d.error.empty())
        ImGui::TextColored(imColor(vs::error), "%s", d.error.c_str());
}

// ------------------------------------------------------------------------------------ animations
struct AnimationDraft {
    std::string path;
    Json document;
    std::string savedText;
    std::string error;
    std::string clip;
    double time{};
    bool playing{true};
};
AnimationDraft animationDraft;

// The cell of `clip` showing at `seconds` since it started.
int cellAt(const AnimationClip &clip, double seconds) {
    const int length = clip.length();
    if (length <= 0)
        return 0;
    const double total = static_cast<double>(clip.totalSeconds());
    double t = seconds;
    if (total > 0.0)
        t = clip.loop ? std::fmod(seconds, total) : std::min(seconds, total - 1e-6);
    int index = 0;
    for (; index < length - 1; ++index) {
        const double duration = static_cast<double>(clip.duration(index));
        if (t < duration)
            break;
        t -= duration;
    }
    return clip.cell(index);
}

void loadAnimationDraft(EditorState &state, const std::string &path) {
    animationDraft = {};
    animationDraft.path = path;
    if (auto text = state.project->assets().readText(path); text) {
        animationDraft.savedText = text.value();
        if (auto parsed = Json::parse(text.value()); parsed)
            animationDraft.document = std::move(parsed.value());
        else
            animationDraft.error = parsed.error();
    } else {
        animationDraft.error = text.error();
    }
}

void animationInspector(EditorState &state, const std::string &path) {
    if (animationDraft.path != path)
        loadAnimationDraft(state, path);
    AnimationDraft &d = animationDraft;
    if (d.document.isNull()) {
        ImGui::TextColored(imColor(vs::error), "%s", d.error.c_str());
        return;
    }
    const auto set = parseAnimationSet(d.document);
    if (!set) {
        ImGui::TextColored(imColor(vs::error), "%s", set.error().c_str());
        return;
    }
    const AnimationSet &clips = set.value();
    if (d.clip.empty() || !clips.find(d.clip))
        d.clip = clips.clips.empty() ? std::string() : clips.clips.front().name;
    const AnimationClip *current = clips.find(d.clip);

    // Preview.
    const auto texture = !clips.texture.empty() && state.sceneRenderer && state.renderer
                             ? state.sceneRenderer->loadedTexture(*state.renderer, clips.texture)
                             : std::nullopt;
    const int columns = std::max(1, clips.columns), rows = std::max(1, clips.rows);
    if (texture && current) {
        if (d.playing)
            d.time += static_cast<double>(ImGui::GetIO().DeltaTime);
        const int cell = cellAt(*current, d.time);
        const int col = cell % columns, row = std::min(cell / columns, rows - 1);
        const ImVec2 uv0{static_cast<float>(col) / static_cast<float>(columns),
                         static_cast<float>(row) / static_cast<float>(rows)};
        const ImVec2 uv1{static_cast<float>(col + 1) / static_cast<float>(columns),
                         static_cast<float>(row + 1) / static_cast<float>(rows)};
        drawFitted(state, texture->handle, texture->pixels, 170.0F, uv0, uv1,
                   {texture->pixels.x / static_cast<float>(columns),
                    texture->pixels.y / static_cast<float>(rows)});
        markItem("asset/inspector/animation/preview");
        char text[64];
        std::snprintf(text, sizeof text, "cell %d", cell);
        ImGui::TextColored(imColor(vs::textDim), "%s", text);
    } else {
        ImGui::TextColored(imColor(vs::textDim), clips.texture.empty()
                                                     ? "No sprite sheet is named in this file."
                                                     : "The sprite sheet cannot be loaded.");
    }
    ImGui::Spacing();
    ImGui::SetNextItemWidth(160.0F);
    if (ImGui::BeginCombo("##clip", d.clip.c_str())) {
        for (const AnimationClip &clip : clips.clips)
            if (ImGui::Selectable(clip.name.c_str(), clip.name == d.clip)) {
                d.clip = clip.name;
                d.time = 0.0;
            }
        ImGui::EndCombo();
    }
    markItem("asset/inspector/animation/clip");
    ImGui::SameLine();
    if (iconButton("asset/inspector/animation/play", d.playing ? Icon::Pause : Icon::Play, false,
                   d.playing ? "Pause the preview" : "Play the preview", 0,
                   ImGui::GetFrameHeight()))
        d.playing = !d.playing;
    ImGui::SameLine();
    if (iconButton("asset/inspector/animation/restart", Icon::Restart, false,
                   "Start the clip again", 0, ImGui::GetFrameHeight()))
        d.time = 0.0;

    info("Sheet", std::to_string(columns) + " x " + std::to_string(rows) + " cells");
    if (!clips.texture.empty()) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(imColor(vs::textDim), "Texture");
        ImGui::SameLine(112.0F);
        if (ImGui::SmallButton(clips.texture.c_str()))
            state.showAssetInExplorer(clips.texture);
        markItem("asset/inspector/animation/texture");
    }

    ImGui::Spacing();
    ImGui::PushFont(fonts().semibold, 13.0F);
    ImGui::TextUnformatted("Clips");
    ImGui::PopFont();
    Json &clipList = *d.document.find("clips");
    if (ImGui::BeginTable("##clips", 4,
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.6F);
        ImGui::TableSetupColumn("Frames", ImGuiTableColumnFlags_WidthStretch, 0.8F);
        ImGui::TableSetupColumn("FPS", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableSetupColumn("Loop", ImGuiTableColumnFlags_WidthFixed, 40.0F);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < clips.clips.size() && i < clipList.size(); ++i) {
            const AnimationClip &clip = clips.clips[i];
            Json &entry = clipList.at(i);
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(clip.name.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("%d", clip.length());
            ImGui::TableSetColumnIndex(2);
            if (entry.contains("fps")) {
                float fps = static_cast<float>(entry.get("fps").asNumber(10.0));
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragFloat("##fps", &fps, 0.1F, 0.5F, 120.0F, "%.1f"))
                    entry.set("fps", std::clamp(fps, 0.5F, 120.0F));
                markItem("asset/inspector/animation/fps/" + clip.name);
            } else {
                ImGui::TextDisabled("per frame");
                tooltip(
                    "This clip gives every frame its own duration; edit the file to change them.");
            }
            ImGui::TableSetColumnIndex(3);
            bool loop = clip.loop;
            if (ImGui::Checkbox("##loop", &loop))
                entry.set("loop", loop);
            markItem("asset/inspector/animation/loop/" + clip.name);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    const bool changed = d.document.dump(2) + "\n" != d.savedText;
    ImGui::BeginDisabled(!changed);
    if (ImGui::Button("Apply", {96.0F, 0.0F})) {
        const std::string text = d.document.dump(2) + "\n";
        if (auto valid = parseAnimationSet(d.document); !valid) {
            d.error = valid.error();
        } else if (auto file = state.project->project().resolve(path); !file) {
            d.error = file.error();
        } else if (auto written = writeTextFileAtomic(file.value(), text); !written) {
            d.error = written.error();
        } else {
            d.savedText = text;
            d.error.clear();
            log(LogLevel::Info, "editor", "Saved " + path);
        }
    }
    markItem("asset/inspector/apply");
    ImGui::SameLine();
    if (ImGui::Button("Revert", {96.0F, 0.0F})) {
        const double keepTime = d.time;
        const std::string keepClip = d.clip;
        loadAnimationDraft(state, path);
        animationDraft.time = keepTime;
        animationDraft.clip = keepClip;
    }
    markItem("asset/inspector/revert");
    ImGui::EndDisabled();
    if (!d.error.empty())
        ImGui::TextColored(imColor(vs::error), "%s", d.error.c_str());
}

// ------------------------------------------------------------------------------------ controllers
const char *comparisonText(Comparison comparison) {
    switch (comparison) {
    case Comparison::Greater:
        return ">";
    case Comparison::GreaterEqual:
        return ">=";
    case Comparison::Less:
        return "<";
    case Comparison::LessEqual:
        return "<=";
    case Comparison::Equal:
        return "==";
    case Comparison::NotEqual:
        return "!=";
    }
    return "?";
}

void controllerInspector(EditorState &state, const std::string &path) {
    const auto text = state.project->assets().readText(path);
    const auto parsed = text ? Json::parse(text.value()) : Result<Json>(Error{text.error()});
    const auto controller = parsed ? AnimationController::fromJson(parsed.value())
                                   : Result<AnimationController>(Error{parsed.error()});
    if (!controller) {
        ImGui::TextColored(imColor(vs::error), "%s", controller.error().c_str());
        return;
    }
    const AnimationController &c = controller.value();
    info("Starts in", c.entry.empty() && !c.states.empty() ? c.states.front().name : c.entry);
    ImGui::Spacing();
    ImGui::PushFont(fonts().semibold, 13.0F);
    ImGui::TextUnformatted("Parameters");
    ImGui::PopFont();
    for (const AnimatorParameter &parameter : c.parameters) {
        const char *type = parameter.type == ParameterType::Float  ? "float"
                           : parameter.type == ParameterType::Bool ? "bool"
                                                                   : "trigger";
        ImGui::BulletText("%s  (%s)", parameter.name.c_str(), type);
    }
    ImGui::Spacing();
    ImGui::PushFont(fonts().semibold, 13.0F);
    ImGui::TextUnformatted("States");
    ImGui::PopFont();
    for (const AnimatorState &animatorState : c.states)
        ImGui::BulletText("%s  plays '%s'", animatorState.name.c_str(), animatorState.clip.c_str());
    ImGui::Spacing();
    ImGui::PushFont(fonts().semibold, 13.0F);
    ImGui::TextUnformatted("Transitions");
    ImGui::PopFont();
    for (const AnimatorTransition &transition : c.transitions) {
        std::string when;
        for (const TransitionCondition &condition : transition.conditions) {
            if (!when.empty())
                when += " and ";
            const AnimatorParameter *parameter = c.parameter(condition.parameter);
            when += condition.parameter;
            if (!parameter || parameter->type == ParameterType::Float) {
                char value[48];
                std::snprintf(value, sizeof value, " %s %g", comparisonText(condition.comparison),
                              condition.value);
                when += value;
            }
        }
        if (transition.hasExitTime)
            when += (when.empty() ? "" : " and ") + std::string("clip has played");
        ImGui::TextWrapped("%s -> %s%s%s", transition.from == "*" ? "any" : transition.from.c_str(),
                           transition.to.c_str(), when.empty() ? "" : "  when ", when.c_str());
    }
}

// ----------------------------------------------------------------------------------------- sounds
struct WaveInfo {
    bool valid{};
    int sampleRate{}, channels{}, bits{};
    double seconds{};
    std::vector<float> peaks; // 0..1 per column.
    std::string error;
};

std::uint32_t le32(const std::string &bytes, std::size_t at) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at])) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 3])) << 24);
}
std::uint16_t le16(const std::string &bytes, std::size_t at) {
    return static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[at]) |
                                      (static_cast<unsigned char>(bytes[at + 1]) << 8));
}

WaveInfo readWave(const std::filesystem::path &file) {
    WaveInfo wave;
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        wave.error = "The file cannot be read.";
        return wave;
    }
    const std::string bytes((std::istreambuf_iterator<char>(stream)),
                            std::istreambuf_iterator<char>());
    if (bytes.size() < 44 || bytes.compare(0, 4, "RIFF") != 0 || bytes.compare(8, 4, "WAVE") != 0) {
        wave.error = "Not a WAVE file.";
        return wave;
    }
    std::size_t at = 12, dataAt = 0, dataSize = 0;
    int format = 0;
    while (at + 8 <= bytes.size()) {
        const std::size_t size = le32(bytes, at + 4);
        if (bytes.compare(at, 4, "fmt ") == 0 && at + 8 + 16 <= bytes.size()) {
            format = le16(bytes, at + 8);
            wave.channels = le16(bytes, at + 10);
            wave.sampleRate = static_cast<int>(le32(bytes, at + 12));
            wave.bits = le16(bytes, at + 22);
        } else if (bytes.compare(at, 4, "data") == 0) {
            dataAt = at + 8;
            dataSize = std::min(size, bytes.size() - dataAt);
            break;
        }
        at += 8 + size + (size & 1U);
    }
    if (dataAt == 0 || wave.channels <= 0 || wave.sampleRate <= 0 || wave.bits <= 0) {
        wave.error = "The WAVE header is incomplete.";
        return wave;
    }
    const std::size_t frameBytes = static_cast<std::size_t>(wave.channels * wave.bits / 8);
    const std::size_t frames = frameBytes > 0 ? dataSize / frameBytes : 0;
    wave.seconds = static_cast<double>(frames) / static_cast<double>(wave.sampleRate);
    wave.valid = true;
    if ((format == 1 && wave.bits == 16) && frames > 0) {
        constexpr std::size_t columns = 240;
        wave.peaks.assign(columns, 0.0F);
        for (std::size_t f = 0; f < frames; ++f) {
            const std::size_t bucket = std::min(columns - 1, f * columns / frames);
            const auto sample = static_cast<std::int16_t>(le16(bytes, dataAt + f * frameBytes));
            wave.peaks[bucket] =
                std::max(wave.peaks[bucket], std::abs(static_cast<float>(sample)) / 32768.0F);
        }
    }
    return wave;
}

std::map<std::string, WaveInfo> &waveCache() {
    static std::map<std::string, WaveInfo> cache;
    return cache;
}

void soundInspector(EditorState &state, const std::string &path) {
    auto &cache = waveCache();
    auto found = cache.find(path);
    if (found == cache.end()) {
        const auto file = state.project->project().resolve(path);
        found = cache
                    .emplace(path, file ? readWave(file.value())
                                        : WaveInfo{false, 0, 0, 0, 0.0, {}, file.error()})
                    .first;
    }
    const WaveInfo &wave = found->second;
    if (!wave.valid) {
        ImGui::TextColored(imColor(vs::error), "%s", wave.error.c_str());
        return;
    }
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList &list = *ImGui::GetWindowDrawList();
    list.AddRectFilled(origin, {origin.x + width, origin.y + 80.0F}, IM_COL32(38, 38, 38, 255));
    if (!wave.peaks.empty()) {
        const float step = width / static_cast<float>(wave.peaks.size());
        for (std::size_t i = 0; i < wave.peaks.size(); ++i) {
            const float h = std::max(1.0F, wave.peaks[i] * 36.0F);
            const float x = origin.x + static_cast<float>(i) * step;
            list.AddRectFilled({x, origin.y + 40.0F - h},
                               {x + std::max(1.0F, step - 0.5F), origin.y + 40.0F + h},
                               packed(Color{200, 140, 230, 255}));
        }
    }
    ImGui::Dummy({width, 80.0F});
    char text[64];
    std::snprintf(text, sizeof text, "%.2f seconds", wave.seconds);
    info("Length", text);
    std::snprintf(text, sizeof text, "%d Hz, %d-bit, %s", wave.sampleRate, wave.bits,
                  wave.channels == 1   ? "mono"
                  : wave.channels == 2 ? "stereo"
                                       : "multichannel");
    info("Format", text);
    ImGui::Spacing();
    ImGui::BeginDisabled(!state.audio);
    if (ImGui::Button("Play", {96.0F, 0.0F}) && state.audio)
        state.audio->play(path);
    markItem("asset/inspector/sound/play");
    ImGui::SameLine();
    if (ImGui::Button("Stop", {96.0F, 0.0F}) && state.audio)
        state.audio->stopAll();
    markItem("asset/inspector/sound/stop");
    ImGui::EndDisabled();
    if (!state.audio)
        ImGui::TextColored(imColor(vs::textDim), "No audio device is available.");
}

// --------------------------------------------------------------------------------- scenes, prefabs
void sceneAssetInspector(EditorState &state, const std::string &path) {
    const auto text = state.project->assets().readText(path);
    const auto parsed = text ? Json::parse(text.value()) : Result<Json>(Error{text.error()});
    if (parsed) {
        const Json &settings = parsed.value().get("settings");
        info("Entities", std::to_string(parsed.value().get("entities").size()));
        if (settings.get("name").isString())
            info("Name", settings.get("name").asString());
        if (settings.get("gravity").size() == 2) {
            char gravity[64];
            std::snprintf(gravity, sizeof gravity, "%g, %g",
                          settings.get("gravity").at(0).asNumber(),
                          settings.get("gravity").at(1).asNumber());
            info("Gravity", gravity);
        }
    } else {
        ImGui::TextColored(imColor(vs::error), "%s", parsed.error().c_str());
    }
    info("Start scene", state.project->project().startScene == path ? "yes" : "no");
    ImGui::Spacing();
    ImGui::BeginDisabled(state.playing());
    if (ImGui::Button("Open", {96.0F, 0.0F}))
        state.openScene(path);
    markItem("asset/inspector/open");
    ImGui::SameLine();
    ImGui::BeginDisabled(state.project->project().startScene == path);
    if (ImGui::Button("Set as Start", {110.0F, 0.0F})) {
        state.project->project().startScene = path;
        if (auto saved = state.project->save(); !saved)
            log(LogLevel::Error, "editor", saved.error());
        else
            log(LogLevel::Info, "editor", "Start scene is now " + path);
    }
    markItem("asset/inspector/setstart");
    ImGui::EndDisabled();
    ImGui::EndDisabled();
}

void prefabAssetInspector(EditorState &state, const std::string &path) {
    const auto loaded = state.project->loadPrefab(path);
    if (!loaded) {
        ImGui::TextColored(imColor(vs::error), "%s", loaded.error().c_str());
        return;
    }
    const Json &entities = loaded.value().get("entities");
    info("Entities", std::to_string(entities.size()));
    const std::string root = loaded.value().get("root").asString();
    std::map<std::string, int> components;
    for (const Json &entity : entities.items()) {
        if (entity.get("id").asString() == root)
            info("Root", entity.get("name").asString());
        for (const Json &component : entity.get("components").items())
            ++components[component.get("type").asString()];
    }
    ImGui::Spacing();
    ImGui::PushFont(fonts().semibold, 13.0F);
    ImGui::TextUnformatted("Components");
    ImGui::PopFont();
    for (const auto &[type, count] : components)
        ImGui::BulletText("%s%s", type.c_str(),
                          count > 1 ? (" x" + std::to_string(count)).c_str() : "");
    ImGui::Spacing();
    ImGui::BeginDisabled(!state.document || state.playing());
    if (ImGui::Button("Add to Scene", {120.0F, 0.0F}))
        state.instantiatePrefab(path, defaultSpawnPoint(state));
    markItem("asset/inspector/add");
    ImGui::EndDisabled();
}
} // namespace

void assetInspector(EditorState &state) {
    if (!state.project)
        return;
    std::string path = state.selectedAsset;
    AssetKind kind = classifyAsset(path);
    // A .ykmeta file is edited through the texture it belongs to.
    if (kind == AssetKind::TextureMeta && path.ends_with(TextureMeta::extension)) {
        path.resize(path.size() - std::strlen(TextureMeta::extension));
        kind = classifyAsset(path);
    }
    std::error_code error;
    const auto absolute = state.project->project().resolve(path);
    const std::uintmax_t size = absolute ? std::filesystem::file_size(absolute.value(), error) : 0;
    if (error) {
        ImGui::TextColored(imColor(vs::warning), "'%s' is not in the project any more.",
                           path.c_str());
        return;
    }

    // Header: the file's name and where it is, and a way back to the entity selection.
    ImDrawList &list = *ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    drawIcon(list, iconOf(kind), {at.x + 12.0F, at.y + 12.0F}, 20.0F, packed(vs::focus));
    list.AddText(fonts().semibold, 14.0F, {at.x + 30.0F, at.y + 1.0F}, packed(vs::textBright),
                 std::filesystem::path(path).filename().string().c_str());
    ImGui::PushFont(fonts().mono, 11.5F);
    list.AddText({at.x + 30.0F, at.y + 20.0F}, packed(vs::textDim),
                 (std::string(kindName(kind)) + "  " + humanSize(size)).c_str());
    ImGui::PopFont();
    ImGui::SetCursorScreenPos({at.x + width - 24.0F, at.y});
    if (iconButton("asset/inspector/close", Icon::Cross, false, "Close (back to the selection)", 0,
                   22.0F))
        state.selectedAsset.clear();
    ImGui::SetCursorScreenPos({at.x, at.y + 42.0F});
    ImGui::Separator();
    ImGui::Spacing();
    markItem("asset/inspector");

    switch (kind) {
    case AssetKind::Texture:
        textureInspector(state, path);
        break;
    case AssetKind::Animation:
        animationInspector(state, path);
        break;
    case AssetKind::Controller:
        controllerInspector(state, path);
        break;
    case AssetKind::Sound:
        soundInspector(state, path);
        break;
    case AssetKind::Scene:
        sceneAssetInspector(state, path);
        break;
    case AssetKind::Prefab:
        prefabAssetInspector(state, path);
        break;
    case AssetKind::TextureMeta:
    case AssetKind::Other:
        info("Path", path);
        break;
    }
}
} // namespace yk::editor::ui
