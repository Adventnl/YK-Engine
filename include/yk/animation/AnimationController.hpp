#pragma once
#include "yk/animation/AnimationSet.hpp"
#include "yk/core/Json.hpp"
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace yk {
// A data-driven animation state machine, loaded from a ".ykctl" asset. Gameplay publishes generic
// parameters (speed, grounded, a "jumped" trigger...); this file says which clip plays for which
// combination, so no code ever names a clip:
//
//   {"format":"yk.animator","version":1,
//    "parameters":[{"name":"speed","type":"float"},{"name":"grounded","type":"bool","default":true},
//                  {"name":"jumped","type":"trigger"}],
//    "entry":"Idle",
//    "states":[{"name":"Idle","clip":"idle"},
//              {"name":"Run","clip":"run","speedParameter":"speedRatio"},
//              {"name":"Jump","clip":"jump"}],
//    "transitions":[
//      {"from":"Idle","to":"Run","when":[{"parameter":"speed","op":">","value":0.2}]},
//      {"from":"*","to":"Jump","when":[{"parameter":"jumped"}]},
//      {"from":"Land","to":"Idle","exitTime":1.0}]}
//
// Transitions from "*" (any state) are checked first, then those of the current state, each group in
// file order; the first whose conditions all hold (and whose exit time has been reached) fires. A
// condition with no "op" means a bool or trigger parameter is true. A transition with "exitTime"
// waits until the state's clip has played that fraction (1 = finished; a looping clip counts a whole
// cycle).
enum class ParameterType { Float, Bool, Trigger };
struct AnimatorParameter {
    std::string name;
    ParameterType type{ParameterType::Float};
    double initial{};
};
enum class Comparison { Greater, GreaterEqual, Less, LessEqual, Equal, NotEqual };
struct TransitionCondition {
    std::string parameter;
    Comparison comparison{Comparison::Greater};
    double value{}; // Bools and triggers compare against 1 (true) or 0 (false).
};
struct AnimatorState {
    std::string name;
    std::string clip;           // Clip of the AnimationSet this state plays.
    float speed{1.0F};          // Playback speed multiplier.
    std::string speedParameter; // Optional: also multiplies the speed by this parameter (>= 0).
};
struct AnimatorTransition {
    std::string from; // A state name, or "*" for any state.
    std::string to;
    std::vector<TransitionCondition> conditions; // All must hold.
    bool hasExitTime{};
    float exitTime{1.0F};
};

struct AnimationController {
    static constexpr const char *formatName = "yk.animator";
    std::vector<AnimatorParameter> parameters;
    std::vector<AnimatorState> states;
    std::vector<AnimatorTransition> transitions;
    std::string entry; // Starting state; the first one when empty.

    const AnimatorParameter *parameter(const std::string &name) const;
    const AnimatorState *state(const std::string &name) const;
    // Names unique, transitions and conditions refer to things that exist.
    Status validate() const;
    // Every state's clip exists in `set`.
    Status validateAgainst(const AnimationSet &set) const;

    Json toJson() const;
    static Result<AnimationController> fromJson(const Json &json);
};

// Runs an AnimationSet, optionally through an AnimationController, for one animated thing. It owns
// the parameter values (which may be set at any time, even before start), the clip playback and the
// current state. Time advances only through update(), so it is deterministic under fixed stepping.
class AnimationPlayer {
  public:
    // Without a controller the player just plays clips by name (playClip). Fails when the
    // controller does not fit the set.
    Status start(std::shared_ptr<const AnimationSet> set,
                 std::shared_ptr<const AnimationController> controller = nullptr);
    bool ready() const {
        return set_ != nullptr;
    }
    const AnimationSet *set() const {
        return set_.get();
    }

    void setFloat(const std::string &name, double value);
    void setBool(const std::string &name, bool value);
    // A trigger holds for exactly one update(); a transition that uses it consumes it.
    void trigger(const std::string &name);
    double value(const std::string &name) const;

    // Direct clip playback (no controller, or forcing a clip); ignored when the clip is unknown.
    void playClip(const std::string &name);
    // Advances by `seconds` of animation time, then follows transitions.
    void update(float seconds);

    int frame() const;                // Sheet cell to show.
    const std::string &state() const; // Current state (or clip when there is no controller).
    const std::string &clip() const;  // Current clip.
    std::vector<ClipEvent> takeEvents();

  private:
    bool conditionHolds(const TransitionCondition &condition) const;
    bool ready(const AnimatorTransition &transition) const;
    void enterState(const std::string &name);
    void followTransitions();

    std::shared_ptr<const AnimationSet> set_;
    std::shared_ptr<const AnimationController> controller_;
    Animator animator_;
    std::map<std::string, double> values_;
    std::set<std::string> triggers_; // Names set through trigger(), cleared after each update.
    std::string state_;
    float speedMultiplier_{1.0F};
};
} // namespace yk
