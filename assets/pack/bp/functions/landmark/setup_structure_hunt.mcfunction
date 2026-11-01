# The Twilight Forest: twilightforest:setup_structure_hunt (TF 4.8, LGPL-2.1-or-later).
# TF: execute in twilightforest:twilight_forest run tp @p ~ ~ ~ / gamemode spectator @p /
# effect give @p minecraft:night_vision infinite. Bedrock functions cannot name the Twilight Forest
# dimension: the tag makes GloamingForest send the player there at the same x / z.
tag @p add gf_landmark_structure_hunt
gamemode spectator @p
effect @p night_vision infinite 0 true
