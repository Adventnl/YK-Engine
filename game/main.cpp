#include "yk/core/Application.hpp"
#include "yk/core/GameFoundation.hpp"
#include "yk/core/Log.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <string_view>
#include <vector>
using namespace yk;
using namespace yk::game;
namespace {
struct AudioDeleter {
    void operator()(SDL_AudioStream *p) const {
        SDL_DestroyAudioStream(p);
    }
};
class Audio {
  public:
    Audio() {
        SDL_AudioSpec spec{SDL_AUDIO_F32, 1, 48000};
        stream_.reset(
            SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr));
        if (stream_)
            SDL_ResumeAudioStreamDevice(stream_.get());
    }
    void beep(float frequency, float volume = .18F) {
        if (!stream_ || muted_)
            return;
        std::vector<float> samples(2400);
        for (std::size_t i = 0; i < samples.size(); ++i) {
            float fade = 1.F - static_cast<float>(i) / static_cast<float>(samples.size());
            samples[i] = std::sin(static_cast<float>(i) * frequency * 6.2831853F / 48000.F) *
                         volume * master_ * effects_ * fade;
        }
        SDL_PutAudioStreamData(stream_.get(), samples.data(),
                               static_cast<int>(samples.size() * sizeof(float)));
    }

  private:
    std::unique_ptr<SDL_AudioStream, AudioDeleter> stream_;
    float master_{1}, effects_{1};
    bool muted_{};
};
bool overlaps(Rect a, Rect b) {
    return a.position.x < b.position.x + b.size.x && a.position.x + a.size.x > b.position.x &&
           a.position.y < b.position.y + b.size.y && a.position.y + a.size.y > b.position.y;
}
std::filesystem::path basePath() {
    const char *p = SDL_GetBasePath();
    return p ? std::filesystem::path(p) : std::filesystem::current_path();
}
std::filesystem::path savePath() {
    char *p = SDL_GetPrefPath("YK", "Elemental Escape");
    std::filesystem::path result = p ? p : basePath();
    SDL_free(p);
    return result / "progress.yks";
}
class Demo final : public ApplicationLayer {
  public:
    explicit Demo(std::filesystem::path root) : root_(std::move(root)) {}
    Status initialize(Renderer &r) override {
        for (auto name : {"prison", "courtyard"}) {
            auto level = loadLevel(root_ / "levels" / (std::string(name) + ".ykl"));
            if (!level)
                return Error{level.error()};
            levels_.emplace(name, std::move(level.value()));
        }
        if (auto linked = validateLinks(levels_); !linked)
            return linked;
        auto png = r.loadPng(root_ / "sprites/player.png");
        if (!png)
            return Error{png.error()};
        playerTexture_ = png.value();
        std::vector<Color> white(1, Color{255, 255, 255, 255});
        auto pixel = r.createTexture(1, 1, white);
        if (!pixel)
            return Error{pixel.error()};
        pixel_ = pixel.value();
        animator_.define({"idle", 0, 1, .2F, true});
        animator_.define({"run", 1, 2, .1F, true});
        animator_.define({"rise", 3, 1, .1F, false});
        animator_.define({"fall", 4, 1, .1F, false});
        animator_.define({"land", 0, 1, .08F, false});
        auto saved = loadProgress(savePath());
        if (saved) {
            progress_ = saved.value();
            continueAvailable_ = levels_.contains(progress_.level);
        }
        return success();
    }
    bool update(const FrameContext &f) override {
        InputFrame in;
        in.move = (f.keyboard.state(Key::D).held || f.keyboard.state(Key::Right).held ? 1.F : 0.F) -
                  (f.keyboard.state(Key::A).held || f.keyboard.state(Key::Left).held ? 1.F : 0.F);
        in.jump = f.keyboard.state(Key::Space).pressed;
        in.jumpReleased = f.keyboard.state(Key::Space).released;
        in.drop = (f.keyboard.state(Key::S).held || f.keyboard.state(Key::Down).held) && in.jump;
        in.interact = f.keyboard.state(Key::E).pressed;
        in.pause = f.keyboard.state(Key::Escape).pressed;
        if (mode_ == Mode::Title) {
            if (in.jump || in.interact) {
                if (!continueAvailable_)
                    progress_ = {};
                if (!activate(progress_.level, progress_.spawn))
                    return true;
                mode_ = Mode::Playing;
            }
            if (f.keyboard.state(Key::Q).pressed)
                return false;
            return true;
        }
        if (in.pause) {
            mode_ = mode_ == Mode::Playing ? Mode::Paused : Mode::Playing;
            buffer_.clear();
            accumulator_ = 0;
            return true;
        }
        if (mode_ == Mode::Paused) {
            if (f.keyboard.state(Key::Q).pressed) {
                mode_ = Mode::Title;
                saveProgress(savePath(), progress_);
            }
            return true;
        }
        buffer_.push(in);
        accumulator_ += std::min(f.delta.seconds, .25F);
        int steps = 0;
        while (accumulator_ >= tick_ && steps < 8) {
            fixedUpdate(buffer_.consumeTick());
            accumulator_ -= tick_;
            ++steps;
        }
        if (steps == 8 && accumulator_ >= tick_)
            accumulator_ = std::fmod(accumulator_, tick_);
        return true;
    }
    Camera2D camera() const override {
        Camera2D result;
        result.position = {480, 272};
        return result;
    }
    Status render(Renderer &r) override {
        if (mode_ == Mode::Title) {
            text(r, "ELEMENTAL ESCAPE", {255, 160}, 4, {250, 190, 60, 255});
            text(r, continueAvailable_ ? "SPACE CONTINUE" : "SPACE NEW GAME", {300, 260}, 2,
                 {255, 255, 255, 255});
            text(r, "Q QUIT", {410, 320}, 2, {170, 190, 220, 255});
            return success();
        }
        auto &l = levels_.at(active_);
        for (int y = 0; y < l.height; ++y)
            for (int x = 0; x < l.width; ++x) {
                auto t = l.tile(x, y);
                if (t == Tile::Empty)
                    continue;
                Color c = t == Tile::Hazard                            ? Color{220, 60, 50, 255}
                          : t == Tile::OneWay                          ? Color{200, 180, 90, 255}
                          : t == Tile::SlopeUp || t == Tile::SlopeDown ? Color{100, 150, 95, 255}
                                                                       : Color{80, 90, 105, 255};
                rect(r, {l.tileToWorld(x, y), {l.tileSize, l.tileSize}}, c, 0);
            }
        for (const auto &e : l.entities) {
            if (e.kind == EntityKind::Key && progress_.collected.contains(e.id))
                continue;
            if (e.kind == EntityKind::Door && progress_.unlocked.contains(e.id))
                continue;
            Color c = e.kind == EntityKind::Key              ? Color{255, 210, 40, 255}
                      : e.kind == EntityKind::Door           ? Color{135, 75, 40, 255}
                      : e.kind == EntityKind::Checkpoint     ? Color{70, 230, 160, 255}
                      : e.kind == EntityKind::MovingPlatform ? Color{100, 180, 230, 255}
                                                             : Color{90, 100, 120, 255};
            Rect b = e.bounds;
            if (e.kind == EntityKind::MovingPlatform)
                b.position = platformPosition(e);
            rect(r, b, c, 2);
        }
        Sprite s{playerTexture_,
                 {{player_.position.x, player_.position.y}, {1, 1}, 0},
                 {32, 48},
                 {.5F, 1},
                 {255, 255, 255, 255},
                 Rect{{static_cast<float>(animator_.frame() * 16), 0}, {16, 24}},
                 facingLeft_,
                 5,
                 0};
        if (auto st = r.submit(s); !st)
            return st;
        text(r, "KEYS " + std::to_string(progress_.keys), {18, 18}, 2, {255, 230, 120, 255});
        text(r, "E INTERACT  ESC PAUSE", {650, 18}, 1, {235, 235, 245, 255});
        if (!message_.empty())
            text(r, message_, {250, 65}, 2, {255, 255, 255, 255});
        if (mode_ == Mode::Paused) {
            rect(r, {{245, 150}, {470, 240}}, {10, 15, 25, 220}, 50);
            text(r, "PAUSED", {390, 190}, 4, {255, 255, 255, 255}, 51);
            text(r, "ESC RESUME", {355, 270}, 2, {255, 220, 90, 255}, 51);
            text(r, "Q TITLE AND SAVE", {310, 315}, 2, {255, 255, 255, 255}, 51);
        }
        return success();
    }

  private:
    enum class Mode { Title, Playing, Paused };
    Mode mode_{Mode::Title};
    std::filesystem::path root_;
    std::map<std::string, Level> levels_;
    std::string active_;
    Progress progress_;
    PlayerState player_;
    CharacterController controller_;
    InputBuffer buffer_;
    Animator animator_;
    Audio audio_;
    TextureHandle playerTexture_, pixel_;
    float accumulator_{}, time_{};
    static constexpr float tick_ = 1.F / 60.F;
    bool continueAvailable_{}, facingLeft_{};
    std::string message_;
    std::string supportPlatform_;
    bool activate(const std::string &id, const std::string &spawn) {
        auto i = levels_.find(id);
        if (i == levels_.end())
            return false;
        auto s = i->second.spawns.find(spawn);
        if (s == i->second.spawns.end())
            return false;
        active_ = id;
        progress_.level = id;
        progress_.spawn = spawn;
        controller_.reset(player_, s->second);
        buffer_.clear();
        accumulator_ = 0;
        supportPlatform_.clear();
        message_ = "";
        return true;
    }
    Vec2 platformPositionAt(const EntityDef &e, float time) const {
        float phase = (std::sin(time * .9F) + 1) * .5F;
        return e.bounds.position + (e.destination - e.bounds.position) * phase;
    }
    Vec2 platformPosition(const EntityDef &e) const {
        return platformPositionAt(e, time_);
    }
    void fixedUpdate(InputFrame in) {
        auto &l = levels_.at(active_);
        bool wasGrounded = player_.grounded;
        const float previousTime = time_;
        time_ += tick_;
        if (!supportPlatform_.empty() && !in.jump && !in.drop) {
            auto supported = std::find_if(l.entities.begin(), l.entities.end(), [&](const auto &e) {
                return e.kind == EntityKind::MovingPlatform && e.id == supportPlatform_;
            });
            if (supported != l.entities.end()) {
                const Vec2 displacement = platformPositionAt(*supported, time_) -
                                          platformPositionAt(*supported, previousTime);
                player_.position = player_.position + displacement;
                player_.previous = player_.previous + displacement;
            } else {
                supportPlatform_.clear();
            }
        }
        const float previousFeet = player_.position.y;
        controller_.tick(player_, l, in, tick_);
        if (in.jump || in.drop)
            supportPlatform_.clear();
        if (player_.velocity.y >= 0 && supportPlatform_.empty()) {
            for (const auto &entity : l.entities) {
                if (entity.kind != EntityKind::MovingPlatform)
                    continue;
                Rect platform = entity.bounds;
                platform.position = platformPosition(entity);
                const bool horizontal =
                    player_.position.x + 9 > platform.position.x &&
                    player_.position.x - 9 < platform.position.x + platform.size.x;
                if (horizontal && previousFeet <= platform.position.y + 2 &&
                    player_.position.y >= platform.position.y) {
                    player_.position.y = platform.position.y;
                    player_.velocity.y = 0;
                    player_.grounded = true;
                    supportPlatform_ = entity.id;
                    break;
                }
            }
        }
        if (in.move != 0)
            facingLeft_ = in.move < 0;
        animator_.play(!player_.grounded ? (player_.velocity.y < 0 ? "rise" : "fall")
                                         : (std::abs(player_.velocity.x) > 4 ? "run"
                                            : wasGrounded                    ? "idle"
                                                                             : "land"));
        animator_.tick(tick_);
        Rect body{{player_.position.x - 10, player_.position.y - 30}, {20, 30}};
        message_ = "";
        auto tile = l.tile(static_cast<int>(player_.position.x / l.tileSize),
                           static_cast<int>((player_.position.y - 2) / l.tileSize));
        if (tile == Tile::Hazard ||
            player_.position.y > static_cast<float>(l.height) * l.tileSize) {
            respawn();
            return;
        }
        for (const auto &e : l.entities) {
            if (!overlaps(body, e.bounds))
                continue;
            if (e.kind == EntityKind::Key && !progress_.collected.contains(e.id)) {
                progress_.collected.insert(e.id);
                ++progress_.keys;
                message_ = "KEY COLLECTED";
                audio_.beep(880);
                saveProgress(savePath(), progress_);
            } else if (e.kind == EntityKind::Checkpoint) {
                progress_.checkpoints.insert(e.id);
                progress_.spawn = nearestSpawn(l, e.bounds.position);
                message_ = "CHECKPOINT SAVED";
                audio_.beep(660);
                saveProgress(savePath(), progress_);
            } else if (e.kind == EntityKind::Door && !progress_.unlocked.contains(e.id)) {
                message_ = progress_.keys ? "PRESS E TO UNLOCK" : "A KEY IS REQUIRED";
                if (in.interact && progress_.keys) {
                    --progress_.keys;
                    progress_.unlocked.insert(e.id);
                    audio_.beep(440);
                    saveProgress(savePath(), progress_);
                }
            } else if (e.kind == EntityKind::Transition) {
                std::string spawn = nearestSpawn(levels_.at(e.target), e.destination);
                activate(e.target, spawn);
                audio_.beep(520);
                saveProgress(savePath(), progress_);
                return;
            }
        }
    }
    std::string nearestSpawn(const Level &l, Vec2 p) {
        std::string best;
        float distance = 1e30F;
        for (auto &[id, s] : l.spawns) {
            float d = std::hypot(s.x - p.x, s.y - p.y);
            if (d < distance) {
                distance = d;
                best = id;
            }
        }
        return best;
    }
    void respawn() {
        auto &l = levels_.at(active_);
        auto i = l.spawns.find(progress_.spawn);
        controller_.reset(player_, i == l.spawns.end() ? l.spawns.begin()->second : i->second);
        buffer_.clear();
        supportPlatform_.clear();
        message_ = "RESPAWNED";
    }
    Status rect(Renderer &r, Rect b, Color c, int layer) {
        Sprite s{pixel_, {b.position, {1, 1}, 0}, b.size, {0, 0}, c, std::nullopt, false, layer, 0};
        return r.submit(s);
    }
    void text(Renderer &r, const std::string &s, Vec2 p, float scale, Color c, int layer = 100) {
        float x = p.x;
        for (char ch : s) {
            if (ch == ' ') {
                x += 4 * scale;
                continue;
            }
            auto bits = glyph(ch);
            for (int row = 0; row < 5; ++row)
                for (int col = 0; col < 3; ++col)
                    if (bits & (1u << (row * 3 + col)))
                        rect(r,
                             {{x + static_cast<float>(col) * scale,
                               p.y + static_cast<float>(row) * scale},
                              {scale, scale}},
                             c, layer);
            x += 4 * scale;
        }
    }
    static unsigned glyph(char c) {
        static const std::map<char, unsigned> g = {
            {'A', 0b111101111101101}, {'C', 0b111100100100111}, {'D', 0b110101101101110},
            {'E', 0b111100110100111}, {'G', 0b111100101101111}, {'I', 0b111010010010111},
            {'K', 0b101101110101101}, {'L', 0b100100100100111}, {'M', 0b101111111101101},
            {'N', 0b101111111111101}, {'P', 0b111101111100100}, {'R', 0b110101110101101},
            {'S', 0b111100111001111}, {'T', 0b111010010010010}, {'U', 0b101101101101111},
            {'V', 0b101101101101010}, {'Y', 0b101101010010010}, {'Q', 0b111101101111001},
            {'0', 0b111101101101111}, {'1', 0b010110010010111}, {'2', 0b111001111100111},
            {'3', 0b111001111001111}, {'4', 0b101101111001001}, {'5', 0b111100111001111},
            {'6', 0b111100111101111}, {'7', 0b111001001001001}, {'8', 0b111101111101111},
            {'9', 0b111101111001111}};
        auto i = g.find(c);
        return i == g.end() ? 0b111001010000010 : i->second;
    }
};
} // namespace
int main(int argc, char **argv) {
    auto app = Application::create({"Elemental Escape", 960, 544, 960, 544});
    if (!app) {
        yk::log(yk::LogLevel::Error, "game", app.error());
        return 1;
    }
    const bool smoke = argc > 1 && std::string_view(argv[1]) == "--smoke";
    std::filesystem::path assets =
        !smoke && argc > 1 ? std::filesystem::absolute(argv[1]) : basePath() / "assets";
    Demo game(assets);
    RunOptions options;
    if (smoke) {
        options.frameLimit = 3;
        options.captureLastFrame = "elemental-escape-smoke.bmp";
    }
    auto result = app.value()->run(game, options);
    if (!result) {
        yk::log(yk::LogLevel::Error, "game", result.error());
        return 1;
    }
    return 0;
}
