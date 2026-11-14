# upstream function gloamingforest:tp_gloaming (data/gloamingforest/function/tp_gloaming.mcfunction):
#     execute in gloamingforest:gloaming_forest run tp @p ~ ~ ~
# Java's "execute in" scales X and Z into the Gloaming Forest (times 8); Bedrock's keeps them, so the
# mod runs the function natively (src/feature/dimension/Functions.cpp).
scriptevent gloamingforest:tp_gloaming execute in gloamingforest:gloaming_forest
