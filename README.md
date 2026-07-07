# Shunt

A competitive train shunting game for 2–4 players. Each player drives a
switching engine around a rail yard, collecting freight cars and delivering
them to the drop-off zone of the matching colour. Cars come in four colours,
and once you couple a car you're locked to that colour until your consist is
empty — plan your moves, work the switches, and don't let the other engines
box you in. The game ends when every car has been delivered; most deliveries
wins.

Any player slot without a connected game controller is driven by an AI
engine, so you can play solo against the computer or fill out a full yard.

## Controls

Game controllers (Xbox-style naming):

| Input | Action |
|---|---|
| Left stick ↑/↓ or D-pad ↑/↓ | Throttle forward / reverse |
| Y | Throw the switch you're lined up on (the ringed one) |
| B | Decouple all cars |
| X | Horn |

The next switch in your direction of travel is highlighted with a ring in your
colour — solid when you can throw it, faded when it's blocked. If two engines
approach the same switch, only the closer one gets the ring and may throw it.

On the title screen: LB/RB (or ←/→) change the player count, +/− adjusts
volume, and any other button starts the game.

## How it works

The yard is a 1D track graph (nodes, segments, switches, crossings) loaded
from `Assets/railgraph.json`. Vehicles are simulated by a kinematic physics
engine — momentum transfer on collision, buffer stops, coasting free cars,
and rigid consists — described in [PHYSICS_SPEC.md](PHYSICS_SPEC.md).

Built with [JUCE](https://juce.com) and
[gin](https://github.com/FigBug/Gin), included as submodules.

## Building

Requires CMake 3.24+ and a C++17 compiler. Clone with submodules:

```sh
git clone --recursive https://github.com/FigBug/Shunt.git
```

Then configure and build with the preset for your platform (`xcode`, `vs`,
or `gcc`):

```sh
cmake --preset xcode
cmake --build Builds/xcode --config Release
```

On Linux, install the dependencies listed in
[.github/workflows/build.yaml](.github/workflows/build.yaml) first.

## Tests

The physics engine has a standalone test suite that builds without JUCE:

```sh
cd Tests
c++ -std=c++17 physics_test.cpp -o physics_test
./physics_test
```

## License

[AGPL-3.0](LICENSE)
