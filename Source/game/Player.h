#pragma once

#include "Track.h"
#include <optional>
#include <vector>

namespace game
{

enum class CarColour { red, blue, green, yellow, count };

struct Car
{
    int id = 0;
    CarColour colour = CarColour::red;
    TrackPos pos;
    int dir = 1;
    bool free = true;
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
};

struct Player
{
    int controllerIndex = -1;
    int slot = 0;
    juce::Colour colour;

    TrackPos pos;
    int dir = 1;

    int facing = 1;
    int lastMoveDir = 1;

    std::vector<int> frontCars;
    std::vector<int> rearCars;
    int score = 0;
    CarColour carryColour = CarColour::red;
    bool hasColourLock = false;

    int totalCars() const noexcept { return (int) frontCars.size() + (int) rearCars.size(); }

    bool prevToggleFwd  = false;
    bool prevToggleBack = false;
    bool prevUncouple   = false;
    bool recoupleLock       = false;
    float recoupleLockDist  = 0.0f;

    std::optional<AiBrain> ai;
};

} // namespace game
