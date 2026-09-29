#pragma once
#include "yk/core/Result.hpp"
#include "yk/input/Input.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace yk {
// A deterministic input recording for automated runs and captures: "D@0-120,W@60-62,Right@10-40"
// holds each key for frames [start, end). Key names are those of yk::keyName ("A", "Left", "Space").
class KeyScript {
  public:
    static Result<KeyScript> parse(std::string_view text);
    bool empty() const {
        return spans_.empty();
    }
    // The keyboard as the script defines it on `frame`: held inside a span, pressed on its first
    // frame, released on the first frame after it.
    Keyboard at(unsigned frame) const;

  private:
    struct Span {
        Key key;
        unsigned first;
        unsigned last; // Exclusive.
    };
    std::vector<Span> spans_;
};
} // namespace yk
