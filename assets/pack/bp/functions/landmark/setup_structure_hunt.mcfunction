# The Gloaming Forest: gloamingforest:setup_structure_hunt (upstream 4.8, LGPL-2.1-or-later).
# upstream: execute in gloamingforest:gloaming_forest run tp @p ~ ~ ~ / gamemode spectator @p /
# effect give @p minecraft:night_vision infinite. Bedrock functions cannot name the Gloaming Forest
# dimension: the tag makes GloamingForest send the player there at the same x / y / z. The effect
# shows its particles, as Java's does (hideParticles false).
tag @p add gf_landmark_structure_hunt
gamemode spectator @p
effect @p night_vision infinite 0 false
