#pragma once
#include "yk/rules/Rules.hpp"
#include "yk/scene/Registry.hpp"
#include <string>
#include <vector>

// A sequence is a short timeline of cues for a cutscene, a scripted event or an intro: fade the
// screen, move the camera, walk a character, play an animation, start a conversation, wait for
// something, raise an event. It is deliberately small: a list of cues at times, not a track editor.
//
//   {"lockInput": true,
//    "cues": [{"time": 0,   "type": "Fade", "to": 1, "seconds": 0},
//             {"time": 0,   "type": "CameraMove", "to": [12, 5], "seconds": 0},
//             {"time": 0.5, "type": "Fade", "to": 0, "seconds": 1},
//             {"time": 1,   "type": "MoveEntity", "entity": "name:Guard", "to": [14, 5],
//              "seconds": 3, "wait": true},
//             {"time": 4,   "type": "StartDialogue", "entity": "name:Guard", "wait": true},
//             {"time": 4.5, "type": "SetVariable", "name": "intro_seen", "value": true},
//             {"time": 5,   "type": "CameraRelease"}]}
//
// Cues are applied in order of `time` (seconds on the sequence's own clock; a cue without one is at
// 0, and cues at the same time keep their order in the file). A cue with "wait": true holds the
// clock until it has finished (the walk is over, the conversation closed), so what comes after
// happens after it; timed cues without it run alongside. Any rule action (PlaySound, EnableEntity,
// EmitEvent, TriggerAnimation, GiveItem, StartQuest...) is a cue too. `time` and `wait` are the
// only keys a cue reserves for itself; the rest are the cue's own arguments.
//
// The cues the player carries out itself:
//   Wait            seconds
//   WaitForEvent    event (name or pattern), source, other, data (what the payload must carry),
//                   timeout (seconds, 0: wait for ever)
//   Fade            to (0 clear .. 1 black), seconds (0: at once), ease
//   CameraMove      to [x, y] or entity (it is followed from then on), seconds (0: a cut), height
//                   (visible height, 0: the camera's own), ease
//   CameraRelease   gives the camera back to what it follows
//   MoveEntity      entity, to [x, y] or toEntity, seconds or speed (units per second), ease
// Wait and WaitForEvent always hold the clock. Eases: linear, smooth, in, out.
namespace yk {
class Camera;
class GameContext;

struct SequenceCue {
    enum class Kind { Action, Wait, WaitForEvent, Fade, CameraMove, CameraRelease, MoveEntity };
    std::size_t number{0}; // Its place in the file, counting from 1, for messages.
    double time{0.0};
    Kind kind{Kind::Action};
    std::string type;
    Json args; // The whole cue object.
    bool wait{false};
    std::vector<Action>
        action;           // One action when the cue is a rule action, none for the player's own.
    EventTrigger trigger; // WaitForEvent: what it waits for.
};

struct SequenceDefinition {
    bool lockInput{true};
    bool skippable{true};
    std::vector<SequenceCue> cues; // In order of `time`.

    // Refuses a file whose cues cannot be carried out (a fade with nowhere to fade to, a walk
    // without a destination); softer problems become warnings.
    static Result<SequenceDefinition> fromJson(const Json &json,
                                               std::vector<std::string> &warnings);
    Json toJson() const;
    // When the last cue starts (what is waited for in between is not counted).
    double length() const;
    // The cues that are rule actions, for the validator.
    void visitRules(const std::string &file, const RuleSourceVisitor &visit) const;
};

// The cue types the player carries out itself; every other type must be a rule action.
const std::vector<std::string> &sequenceCueTypes();
const std::vector<std::string> &sequenceEaseNames();

// Plays a sequence. Raises sequence.started and sequence.finished (source: this entity; data:
// sequence, skipped, and stopped for one that was cut short) and holds an input lock named after it
// while it plays (unless the file says not to). A cue that names an entity that is not there is
// logged and skipped. When it ends the camera is handed back; the screen stays as the last Fade
// left it (a sequence that ends in black can change the scene from there).
class SequencePlayer final : public Component {
  public:
    AssetRef sequence;
    bool playOnStart{false};
    std::string skipSet{"Player1"};
    std::string
        skipAction; // A named action that skips the sequence; empty: the player cannot skip.
    static void describe(TypeBuilder<SequencePlayer> &type);

    void onStart(GameContext &context) override;
    void onFixedUpdate(GameContext &context, float seconds) override;
    void onDestroy(GameContext &context) override;

    // Starts it (the sequence file is read now) and carries out the cues that are due at once.
    // `actor` is who it concerns, for the rules in cues. False when one is already playing or the
    // file cannot be used.
    bool play(GameContext &context, EntityId actor = {});
    // Jumps to the end: instant cues that have not happened yet happen, timed ones finish in their
    // end state, a conversation it started is closed.
    void skip(GameContext &context);
    // Ends it where it stands: the camera and the input lock are given back, the screen goes back
    // to what it was when the sequence began.
    void stop(GameContext &context);
    bool playing() const {
        return playing_;
    }
    // Seconds of the sequence's own clock (it stands still while a cue it waits for runs).
    double clock() const {
        return clock_;
    }

  private:
    struct Timed {
        enum class Kind { Wait, WaitForEvent, Fade, CameraMove, MoveEntity, Dialogue };
        Kind kind{Kind::Wait};
        EntityId entity{}; // What moves, what the camera goes to, whose conversation is awaited.
        Vec2 from{}, to{};
        float fromValue{0.0F}, toValue{0.0F}; // A fade's amounts; a camera move's visible heights.
        double duration{0.0};                 // A WaitForEvent's timeout (0: none).
        double elapsed{0.0};
        std::string ease;
        bool blocking{false};
        bool done{false};
        EventTrigger trigger;
    };
    void step(GameContext &context, float seconds);
    // Carries a cue out. `skipping` applies its end state at once, whatever its duration.
    void begin(GameContext &context, const SequenceCue &cue, bool skipping);
    void advance(GameContext &context, Timed &timed, float seconds);
    void conclude(GameContext &context, Timed &timed, bool skipped);
    void finish(GameContext &context, bool skipped, bool stopped);
    void trackCamera(GameContext &context);
    void releaseCamera(GameContext &context);
    Camera *camera(GameContext &context) const;
    RuleContext rules(GameContext &context) const;
    std::string lockName() const;

    SequenceDefinition definition_;
    std::string source_;
    std::vector<Timed> timed_;
    std::size_t next_{0};
    double clock_{0.0};
    bool playing_{false};
    bool ending_{false}; // skip() or stop() is under way: a cue that asks for either is ignored.
    EntityId actor_{};
    EventBus::Subscription subscription_{};
    EntityId heldCamera_{};
    EntityId follow_{}; // What the camera keeps looking at once it has gone there.
    float followHeight_{0.0F};
    float startFade_{0.0F};
};

void registerSequenceRules(RuleCatalog &catalog);
void registerSequenceComponents(ComponentRegistry &registry);
} // namespace yk
