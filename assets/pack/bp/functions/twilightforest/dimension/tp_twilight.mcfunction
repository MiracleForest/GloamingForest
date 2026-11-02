# TF function twilightforest:tp_twilight (data/twilightforest/function/tp_twilight.mcfunction):
#     execute in twilightforest:twilight_forest run tp @p ~ ~ ~
# Java's "execute in" scales X and Z into the Twilight Forest (times 8); Bedrock's keeps them, so the
# mod runs the function natively (src/feature/dimension/Functions.cpp).
scriptevent twilightforest:tp_twilight execute in twilightforest:twilight_forest
