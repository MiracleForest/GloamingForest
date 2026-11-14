# upstream function gloamingforest:tp_overworld (data/gloamingforest/function/tp_overworld.mcfunction):
#     execute in minecraft:overworld run tp @p ~ ~ ~
# Java's "execute in" scales X and Z into the overworld (divided by 8 from the Gloaming Forest);
# Bedrock's keeps them, so the mod runs the function natively (src/feature/dimension/Functions.cpp).
scriptevent gloamingforest:tp_overworld execute in minecraft:overworld
