#include "yk/core/GameFoundation.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
namespace yk::game {
namespace {
bool validId(const std::string &s) {
    return !s.empty() && s.find_first_not_of(
                             "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") ==
                             std::string::npos;
}
bool overlap(Rect a, Rect b) {
    return a.position.x < b.position.x + b.size.x && a.position.x + a.size.x > b.position.x &&
           a.position.y < b.position.y + b.size.y && a.position.y + a.size.y > b.position.y;
}
float approach(float v, float target, float amount) {
    return v < target ? std::min(v + amount, target) : std::max(v - amount, target);
}
} // namespace
Tile Level::tile(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height)
        return Tile::Solid;
    return collision[static_cast<std::size_t>(y * width + x)];
}
Vec2 Level::tileToWorld(int x, int y) const {
    return {static_cast<float>(x) * tileSize, static_cast<float>(y) * tileSize};
}
Result<Level> loadLevel(const std::filesystem::path &path) {
    std::ifstream in(path);
    if (!in)
        return Error{"Open level '" + path.string() + "'"};
    Level l;
    std::string word;
    if (!(in >> word >> l.id >> l.width >> l.height >> l.tileSize) || word != "level" ||
        !validId(l.id) || l.width < 4 || l.height < 4 || l.width > 256 || l.height > 256 ||
        !std::isfinite(l.tileSize) || l.tileSize < 8 || l.tileSize > 256)
        return Error{"Invalid level header in '" + path.string() + "'"};
    if (!(in >> word) || word != "collision")
        return Error{"Missing collision layer in level '" + l.id + "'"};
    std::string row;
    for (int y = 0; y < l.height; ++y) {
        if (!(in >> row) || static_cast<int>(row.size()) != l.width)
            return Error{"Invalid collision row " + std::to_string(y) + " in level '" + l.id + "'"};
        for (char c : row) {
            if (std::string(".#=/\\!").find(c) == std::string::npos)
                return Error{"Unsupported collision tile in level '" + l.id + "'"};
            l.collision.push_back(static_cast<Tile>(c));
        }
    }
    std::set<std::string> ids;
    while (in >> word) {
        if (word == "spawn") {
            std::string id;
            Vec2 p;
            if (!(in >> id >> p.x >> p.y) || !validId(id) || !finite(p) ||
                !l.spawns.emplace(id, p).second)
                return Error{"Invalid or duplicate spawn in level '" + l.id + "'"};
        } else if (word == "entity") {
            std::string kind;
            EntityDef e;
            if (!(in >> kind >> e.id >> e.bounds.position.x >> e.bounds.position.y >>
                  e.bounds.size.x >> e.bounds.size.y) ||
                !validId(e.id) || !ids.insert(e.id).second || !finite(e.bounds.position) ||
                !finite(e.bounds.size) || e.bounds.size.x <= 0 || e.bounds.size.y <= 0)
                return Error{"Invalid or duplicate entity in level '" + l.id + "'"};
            if (kind == "key")
                e.kind = EntityKind::Key;
            else if (kind == "door")
                e.kind = EntityKind::Door;
            else if (kind == "checkpoint")
                e.kind = EntityKind::Checkpoint;
            else if (kind == "transition") {
                e.kind = EntityKind::Transition;
                if (!(in >> e.target >> e.destination.x >> e.destination.y))
                    return Error{"Invalid transition '" + e.id + "'"};
            } else if (kind == "platform") {
                e.kind = EntityKind::MovingPlatform;
                if (!(in >> e.destination.x >> e.destination.y))
                    return Error{"Invalid platform '" + e.id + "'"};
            } else if (kind == "decoration")
                e.kind = EntityKind::Decoration;
            else
                return Error{"Unknown entity kind '" + kind + "' in level '" + l.id + "'"};
            l.entities.push_back(e);
        } else
            return Error{"Unknown directive '" + word + "' in level '" + l.id + "'"};
    }
    if (l.spawns.empty())
        return Error{"Level '" + l.id + "' has no spawn"};
    for (const auto &[id, p] : l.spawns) {
        const int x = static_cast<int>(std::floor(p.x / l.tileSize));
        const int y = static_cast<int>(std::floor((p.y - 1.F) / l.tileSize));
        if (l.tile(x, y) == Tile::Solid)
            return Error{"Spawn '" + id + "' embedded in solid geometry"};
    }
    return l;
}
Status validateLinks(const std::map<std::string, Level> &levels) {
    for (const auto &[id, l] : levels)
        for (const auto &e : l.entities)
            if (e.kind == EntityKind::Transition) {
                auto found = levels.find(e.target);
                if (found == levels.end())
                    return Error{"Transition '" + e.id + "' in '" + id + "' has missing level '" +
                                 e.target + "'"};
                bool spawn = false;
                for (const auto &[name, p] : found->second.spawns)
                    if (std::abs(p.x - e.destination.x) < 0.1F &&
                        std::abs(p.y - e.destination.y) < 0.1F)
                        spawn = true;
                if (!spawn)
                    return Error{"Transition '" + e.id + "' has missing destination spawn"};
            }
    return success();
}
void InputBuffer::push(InputFrame f) {
    pending_.move = f.move;
    pending_.jump |= f.jump;
    pending_.jumpReleased |= f.jumpReleased;
    pending_.drop |= f.drop;
    pending_.interact |= f.interact;
    pending_.pause |= f.pause;
}
InputFrame InputBuffer::consumeTick() {
    auto out = pending_;
    pending_.jump = pending_.jumpReleased = pending_.drop = pending_.interact = pending_.pause =
        false;
    return out;
}
void InputBuffer::clear() {
    pending_ = {};
}
void CharacterController::reset(PlayerState &p, Vec2 spawn) const {
    p = {};
    p.position = p.previous = spawn;
}
void CharacterController::tick(PlayerState &p, const Level &l, InputFrame input, float dt) const {
    p.previous = p.position;
    p.coyote = std::max(0.F, p.coyote - dt);
    p.jumpBuffer = std::max(0.F, p.jumpBuffer - dt);
    p.drop = std::max(0.F, p.drop - dt);
    if (input.jump)
        p.jumpBuffer = config_.jumpBufferSeconds;
    if (input.drop)
        p.drop = config_.dropSeconds;
    if (p.grounded)
        p.coyote = config_.coyoteSeconds;
    const float target = input.move * config_.speed;
    p.velocity.x =
        approach(p.velocity.x, target,
                 (p.grounded ? (input.move == 0 ? config_.deceleration : config_.groundAcceleration)
                             : config_.airAcceleration) *
                     dt);
    if (p.jumpBuffer > 0 && p.coyote > 0) {
        p.velocity.y = -config_.jumpSpeed;
        p.grounded = false;
        p.coyote = p.jumpBuffer = 0;
    }
    if (input.jumpReleased && p.velocity.y < 0)
        p.velocity.y *= 0.48F;
    p.velocity.y = std::min(config_.maximumFallSpeed, p.velocity.y + config_.gravity * dt);
    constexpr float w = 18, h = 30;
    p.position.x += p.velocity.x * dt;
    Rect body{{p.position.x - w / 2, p.position.y - h}, {w, h}};
    int minx = static_cast<int>(std::floor(body.position.x / l.tileSize)),
        maxx = static_cast<int>(std::floor((body.position.x + w - 0.01F) / l.tileSize));
    int miny = static_cast<int>(std::floor(body.position.y / l.tileSize)),
        maxy = static_cast<int>(std::floor((body.position.y + h - 0.01F) / l.tileSize));
    for (int y = miny; y <= maxy; ++y)
        for (int x = minx; x <= maxx; ++x)
            if (l.tile(x, y) == Tile::Solid) {
                Rect t{l.tileToWorld(x, y), {l.tileSize, l.tileSize}};
                if (overlap(body, t)) {
                    if (p.velocity.x > 0)
                        p.position.x = t.position.x - w / 2;
                    else if (p.velocity.x < 0)
                        p.position.x = t.position.x + t.size.x + w / 2;
                    p.velocity.x = 0;
                    body.position.x = p.position.x - w / 2;
                }
            }
    const float oldFeet = p.position.y;
    p.position.y += p.velocity.y * dt;
    p.grounded = false;
    body = {{p.position.x - w / 2, p.position.y - h}, {w, h}};
    minx = static_cast<int>(std::floor(body.position.x / l.tileSize));
    maxx = static_cast<int>(std::floor((body.position.x + w - 0.01F) / l.tileSize));
    miny = static_cast<int>(std::floor(body.position.y / l.tileSize));
    maxy = static_cast<int>(std::floor((body.position.y + h - 0.01F) / l.tileSize));
    for (int y = miny; y <= maxy; ++y)
        for (int x = minx; x <= maxx; ++x) {
            auto tile = l.tile(x, y);
            Rect t{l.tileToWorld(x, y), {l.tileSize, l.tileSize}};
            bool one = tile == Tile::OneWay && p.drop <= 0 && p.velocity.y >= 0 &&
                       oldFeet <= t.position.y + 1;
            if ((tile == Tile::Solid || one) && overlap(body, t)) {
                if (p.velocity.y > 0) {
                    p.position.y = t.position.y;
                    p.grounded = true;
                } else if (p.velocity.y < 0)
                    p.position.y = t.position.y + t.size.y + h;
                p.velocity.y = 0;
                body.position.y = p.position.y - h;
            }
        }
    // Supported 45-degree slopes use the player's feet as the contact sample.
    int sx = static_cast<int>(std::floor(p.position.x / l.tileSize)),
        sy = static_cast<int>(std::floor(p.position.y / l.tileSize));
    auto st = l.tile(sx, sy);
    if (p.velocity.y >= 0 && (st == Tile::SlopeUp || st == Tile::SlopeDown)) {
        float local = p.position.x - static_cast<float>(sx) * l.tileSize;
        float surface = static_cast<float>(sy) * l.tileSize +
                        (st == Tile::SlopeUp ? l.tileSize - local : local);
        if (p.position.y >= surface - 4 && oldFeet <= surface + config_.maximumFallSpeed * dt) {
            p.position.y = surface;
            p.velocity.y = 0;
            p.grounded = true;
        }
    }
}
Status Animator::define(AnimationClip c) {
    if (!validId(c.name) || c.firstFrame < 0 || c.frameCount <= 0 ||
        !std::isfinite(c.frameSeconds) || c.frameSeconds <= 0)
        return Error{"Invalid animation clip"};
    clips_.insert_or_assign(c.name, std::move(c));
    return success();
}
Status Animator::play(const std::string &n) {
    auto i = clips_.find(n);
    if (i == clips_.end())
        return Error{"Unknown animation '" + n + "'"};
    if (active_ != &i->second) {
        active_ = &i->second;
        elapsed_ = 0;
        index_ = 0;
        completed_ = emitted_ = false;
    }
    return success();
}
void Animator::tick(float s) {
    if (!active_ || s <= 0)
        return;
    elapsed_ += s;
    while (elapsed_ >= active_->frameSeconds) {
        elapsed_ -= active_->frameSeconds;
        if (index_ + 1 < active_->frameCount)
            ++index_;
        else if (active_->loop)
            index_ = 0;
        else {
            completed_ = true;
            index_ = active_->frameCount - 1;
            break;
        }
    }
}
int Animator::frame() const {
    return active_ ? active_->firstFrame + index_ : 0;
}
bool Animator::takeCompletion() {
    if (completed_ && !emitted_) {
        emitted_ = true;
        return true;
    }
    return false;
}
Result<Progress> loadProgress(const std::filesystem::path &p) {
    std::ifstream in(p);
    if (!in)
        return Error{"Open save '" + p.string() + "'"};
    Progress out;
    std::string k;
    if (!(in >> k >> out.version) || k != "version" || out.version != 1)
        return Error{"Unsupported save version"};
    while (in >> k) {
        if (k == "level")
            in >> out.level >> out.spawn;
        else if (k == "keys")
            in >> out.keys;
        else if (k == "collected" || k == "unlocked" || k == "checkpoint") {
            std::string id;
            if (!(in >> id) || !validId(id))
                return Error{"Invalid save identifier"};
            (k == "collected"  ? out.collected
             : k == "unlocked" ? out.unlocked
                               : out.checkpoints)
                .insert(id);
        } else
            return Error{"Invalid save field '" + k + "'"};
    }
    if (!validId(out.level) || !validId(out.spawn) || out.keys < 0 || out.keys > 99)
        return Error{"Invalid save values"};
    return out;
}
Status saveProgress(const std::filesystem::path &p, const Progress &s) {
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    if (ec)
        return Error{"Create save directory: " + ec.message()};
    auto tmp = p;
    tmp += ".tmp";
    std::ofstream out(tmp, std::ios::trunc);
    if (!out)
        return Error{"Open temporary save"};
    out << "version 1\nlevel " << s.level << ' ' << s.spawn << "\nkeys " << s.keys << '\n';
    for (auto &i : s.collected)
        out << "collected " << i << '\n';
    for (auto &i : s.unlocked)
        out << "unlocked " << i << '\n';
    for (auto &i : s.checkpoints)
        out << "checkpoint " << i << '\n';
    out.close();
    if (!out)
        return Error{"Write temporary save"};
    std::filesystem::rename(tmp, p, ec);
    if (ec) {
        std::filesystem::remove(p, ec);
        ec.clear();
        std::filesystem::rename(tmp, p, ec);
    }
    if (ec)
        return Error{"Replace save: " + ec.message()};
    return success();
}
} // namespace yk::game
