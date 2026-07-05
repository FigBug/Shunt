#pragma once

#include "Track.h"
#include <optional>
#include <vector>

namespace game
{

enum class CarColour { red, blue, green, yellow, count };

// Throttle top speed (matches GameState::kTrainSpeed); used to normalise engine
// speed for the audio layer.
inline constexpr float kEngineTopSpeed = 8.0f;

struct Car
{
    int id = 0;
    CarColour colour = CarColour::red;
    TrackPos pos;
    int dir = 1;
    bool free = true;
    int bodyId = -1;   // physics body while free, -1 when coupled
};

struct AiBrain
{
    enum State { idle, seekingCar, returningHome, leavingDropOff, dumping };
    State state = idle;
    int targetCarId = -1;
    TrackPos targetPos;
    float thinkTimer = 0.0f;
    float switchCooldown = 0.0f;
    juce::Point<float> lastPos;
    int stuckCount = 0;
    std::vector<int> path;
    int pathDir = 1;
    int lastSeg = -1;
    bool waiting = false;   // holding position (e.g. the drop-off is blocked)
    int dirSign = 0;        // last commanded travel direction (+1/-1)
    float dirTimer = 0.0f;  // lockout before the direction may flip again
    int homeStuck = 0;      // rethinks parked at the drop-off without scoring
    int lastCarCount = 0;   // consist size last rethink, to detect deliveries
};

struct Player
{
    int controllerIndex = -1;
    int slot = 0;
    juce::Colour colour;

    TrackPos pos;
    int dir = 1;
    int bodyId = -1;
    int prevSegment = -1;   // engine segment last frame, for detecting crossings
    float speed = 0.0f;     // engine's actual speed this frame (from physics), for audio

    std::vector<int> frontCars;
    std::vector<int> rearCars;
    int score = 0;
    CarColour carryColour = CarColour::red;
    bool hasColourLock = false;

    int totalCars() const noexcept { return (int) frontCars.size() + (int) rearCars.size(); }

    bool prevToggleFwd  = false;
    bool prevToggleBack = false;
    bool prevUncouple   = false;
    bool prevHorn       = false;
    bool recoupleLock       = false;
    float recoupleLockDist  = 0.0f;

    std::optional<AiBrain> ai;
};

} // namespace game
