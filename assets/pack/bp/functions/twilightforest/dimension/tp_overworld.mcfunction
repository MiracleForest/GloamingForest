# TF function twilightforest:tp_overworld (data/twilightforest/function/tp_overworld.mcfunction):
#     execute in minecraft:overworld run tp @p ~ ~ ~
# Java's "execute in" scales X and Z into the overworld (divided by 8 from the Twilight Forest);
# Bedrock's keeps them, so the mod runs the function natively (src/feature/dimension/Functions.cpp).
scriptevent twilightforest:tp_overworld execute in minecraft:overworld
