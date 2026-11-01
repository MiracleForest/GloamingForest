#pragma once

// Contract "client.input+movement", server side (header owned by F0, implemented by F12).
//
// Bedrock's movement is client-authoritative. With the client mod, movement modifiers run in
// gf::client::movement; for vanilla clients the server detects input edges from the player's
// auth input and applies approximations with motion packets. `validate` checks client-reported
// movement of companions against the rules of a modifier (anti-desync).
//
//     gf::movement::onInputEdge([](Player& p, gf::movement::Edge e) { if (e == Edge::JumpPressed) ... });
//     gf::movement::applyMotion(player, {0, 0.6f, 0});

#include <functional>
#include <string>

#include "mc/deps/core/math/Vec3.h"

class Player;

namespace gf::movement {

enum class Edge { JumpPressed, JumpReleased, SneakPressed, SneakReleased, SprintStarted, SprintStopped, LeftGround, Landed };

void onInputEdge(std::function<void(Player& player, Edge edge)> fn);
/// Sets the player's velocity (SetActorMotionPacket for remote players).
void applyMotion(Player& player, Vec3 const& velocity);
/// A movement rule of modifier `id`: false rejects a move from `from` to `to` (the server
/// corrects the player).
using ValidateFn = std::function<bool(Player& player, Vec3 const& from, Vec3 const& to)>;
void validate(std::string const& id, ValidateFn fn);

} // namespace gf::movement
