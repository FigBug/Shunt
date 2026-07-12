#pragma once

#include "Track.h"
#include <optional>
#include <vector>

namespace game
{

enum class CarColour { red, blue, green, yellow, count };

// Throttle top speed (matches GameState::kTrainSpeed); used to normalise engine
// speed for the audio layer.
inline constexpr float kEngineTopSpeed = 6.0f;

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
    enum State { idle, seekingCar, returningHome, leavingDropOff };
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
    int  waitCount = 0;     // rethinks spent waiting; times out so we never
                            // defer forever to an idle engine that won't clear
    float backoffTimer = 0.0f;   // reversing to break an engine-vs-engine jam
    int   backoffDir = 0;
    float bestDistToTarget = 1.0e9f;  // closest we've got to the goal so far
    int   noProgress = 0;    // rethinks without getting closer (a shoving match)
    int dirSign = 0;        // last commanded travel direction (+1/-1)
    float dirTimer = 0.0f;  // lockout before the direction may flip again
    bool wantBoost = false; // AI spends a full boost reserve in bursts
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
    int deliveryStreak = 0;      // cars delivered back-to-back — a scoring multiplier
    float deliveryTimer = 0.0f;  // time left before the streak resets
    CarColour carryColour = CarColour::red;
    bool hasColourLock = false;

    int totalCars() const noexcept { return (int) frontCars.size() + (int) rearCars.size(); }

    bool prevToggleFwd  = false;   // edge detect for the Y (throw switch) button
    bool prevUncouple   = false;
    bool hornHeld       = false;   // horn button currently held (sustained sound)
    bool recoupleLock       = false;
    float recoupleLockDist  = 0.0f;
    float collisionCooldown = 0.0f;   // gap before this engine can sound a crash again
    float ramCooldown       = 0.0f;   // gap before a ram can knock another car off this consist
    float smokeAccum = 0.0f;          // fractional puffs accrued for smoke spawning

    // The switch this train is lined up to throw (next one in its direction of
    // travel), for the on-track ring indicator. -1 = none. ringFlippable is true
    // when it can actually be thrown right now (drawn solid vs. half-alpha).
    int   ringSwitch = -1;
    bool  ringFlippable = false;

    float boost = 1.0f;   // 0..1 boost reserve; horn spends it for extra top speed
    bool  boosting = false;   // spending boost this frame (darker smoke, horn)

    std::optional<AiBrain> ai;
};

} // namespace game
