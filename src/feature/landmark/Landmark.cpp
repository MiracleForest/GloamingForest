// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// The "landmark" contract (feature/landmark/Landmark.h) is implemented by F6 across this unit
// (the F0 stub that was here is replaced):
//
//   descriptor                                       Descriptors.cpp
//   registerGenerator, startAt, startsIntersecting,
//   isConquered, setConquered, generateAt,
//   forceStart, addTerraformer, decorationAllowed    Starts.cpp
//   isProtectedFor, enforcedProgression              Protection.cpp
//   trySpawnHint                                     Hints.cpp
//
// The pure grid (core/landmark/Landmark.h) is in src/core/landmark/Landmark.cpp; the contract
// extension other units need (terraforming, feature clearance, spawns, locate, client boxes) is
// declared in src/feature/ext/landmark/LandmarkExt.h.
#include "feature/landmark/Landmark.h"
