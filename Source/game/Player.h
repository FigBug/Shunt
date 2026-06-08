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
    enum State { idle, seekingCar, returningHome };
    State state = idle;
    int targetCarId = -1;
    float thinkTimer = 0.0f;
    float switchCooldown = 0.0f;
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

    bool prevToggle   = false;
    bool prevUncouple = false;

    std::optional<AiBrain> ai;
};

} // namespace game
