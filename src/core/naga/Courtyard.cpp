// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/courtyard/StructureMazeGenerator.java
//   src/main/java/twilightforest/world/components/structures/courtyard/CourtyardMain.java
//   src/main/java/twilightforest/enums/Diagonals.java
//   src/main/java/twilightforest/util/BoundingBoxUtils.java (getComponentToAddBoundingBox)
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
#include "core/naga/Courtyard.h"

namespace gf::core::naga {

namespace {

constexpr int kWidth  = kRowOfCells; // widthInCellCount
constexpr int kHeight = kRowOfCells; // heightInCellCount

/// StructureMazeGenerator.WallFacing (EAST, SOUTH, WEST, NORTH).
struct WallFacing {
    int bite, opposite, inverted, invertedOpposite, xOffset, zOffset;
    [[nodiscard]] bool unpackAndTest(int directions) const { return (bite & directions) == bite; }
};
constexpr WallFacing kFacings[4] = {
    {0b0001, 0b0100, 0b1110, 0b1011, 1, 0},  // EAST
    {0b0010, 0b1000, 0b1101, 0b0111, 0, 1},  // SOUTH
    {0b0100, 0b0001, 0b1011, 0b1110, -1, 0}, // WEST
    {0b1000, 0b0010, 0b0111, 0b1101, 0, -1}, // NORTH
};
enum Facing { East = 0, South = 1, West = 2, North = 3 };

/// Diagonals (TOP_RIGHT, BOTTOM_RIGHT, BOTTOM_LEFT, TOP_LEFT).
struct Diagonal {
    bool invertX, invertY, top, left;
    [[nodiscard]] int convertX(int n, int range) const { return invertX ? range - n : n; }
    [[nodiscard]] int convertY(int n, int range) const { return invertY ? range - n : n; }
};
constexpr Diagonal kDiagonals[4] = {
    {true, false, true, false},  // TOP_RIGHT
    {true, true, false, false},  // BOTTOM_RIGHT
    {false, true, false, true},  // BOTTOM_LEFT
    {false, false, true, true},  // TOP_LEFT
};

/// BoundingBoxUtils#getComponentToAddBoundingBox(x, y, z, minX, minY, minZ, spanX, spanY, spanZ, NORTH, false).
BlockBox northBox(int x, int y, int z, int minX, int minY, int minZ, int spanX, int spanY, int spanZ) {
    return BlockBox{x - spanX - minX, y + minY, z - spanZ - minZ, x - minX, y + spanY + minY, z - minZ};
}

// Rotation.values() indices
constexpr int kNone = 0, kCw90 = 1, kCw180 = 2, kCcw90 = 3;

} // namespace

char const* pieceTypeId(PieceKind kind) {
    switch (kind) {
    case PieceKind::HedgeCap:
        return "twilightforest:TFNCCp";
    case PieceKind::HedgeCapPillar:
        return "twilightforest:TFNCCpP";
    case PieceKind::HedgeCorner:
        return "twilightforest:TFNCCr";
    case PieceKind::HedgeT:
        return "twilightforest:TFNCT";
    case PieceKind::HedgeLine:
        return "twilightforest:TFNCLn";
    case PieceKind::HedgeIntersection:
        return "twilightforest:TFNCIs";
    case PieceKind::HedgePadder:
        return "twilightforest:TFNCPd";
    case PieceKind::Terrace:
        return "twilightforest:TFNCTe";
    case PieceKind::TerraceHedge:
        return "twilightforest:TFNCHe";
    case PieceKind::Path:
        return "twilightforest:TFNCPa";
    case PieceKind::Wall:
        return "twilightforest:TFNCWl";
    case PieceKind::WallPadder:
        return "twilightforest:TFNCWP";
    case PieceKind::WallCornerOuter:
        return "twilightforest:TFNCWC";
    case PieceKind::WallCornerInner:
        return "twilightforest:TFNCWA";
    }
    return "";
}

BlockBox mainBox(int x, int y, int z) { return northBox(x, y, z, -kRadius / 2, -1, -kRadius / 2, kRadius, 10, kRadius); }
BlockBox sizeConstraints(int x, int y, int z) { return northBox(x, y, z, -kRadius, -1, -kRadius, kRadius * 2, 10, kRadius * 2); }

void generateMaze(Maze& maze, CornerClipping& cornerClippings, jrand::RandomSource& random) {
    constexpr int maximumClipping = 2;
    int           rotations[kMazeSize][kMazeSize]{};

    for (int x = 0; x < kWidth - 1; x++) {
        for (int y = 0; y < kHeight - 1; y++) {
            rotations[x][y] = random.nextInt(4);
            maze[x][y] |= kFacings[rotations[x][y]].bite;
        }
    }

    // Java's maze.clone() is shallow: mazeLocal shares the rows of maze (the zero tests below read
    // the live values)
    Maze& mazeLocal = maze;

    int const halfWayPointX = (kWidth / 2) - 1;
    int const halfWayPointY = (kHeight / 2) - 1;

    for (int y = 0; y < kHeight - 1; y++) {
        for (int x = 0; x < kWidth - 1; x++) {
            if (x == halfWayPointX && y == halfWayPointY) continue;
            WallFacing const& r = kFacings[rotations[x][y]];

            if (rotations[x][y] == West && x > 0) {
                if (!r.unpackAndTest(maze[x - 1][y])) maze[x - 1][y] |= r.opposite;
                else {
                    maze[x][y] &= r.inverted;
                    maze[x - 1][y] &= kFacings[rotations[x - 1][y]].invertedOpposite;
                }
            }
            if (rotations[x][y] == North && y > 0) {
                if (!r.unpackAndTest(maze[x][y - 1])) maze[x][y - 1] |= r.opposite;
                else {
                    maze[x][y] &= r.inverted;
                    maze[x][y - 1] &= kFacings[rotations[x][y - 1]].invertedOpposite;
                }
            }
            if (rotations[x][y] == East && x < kWidth - 2) {
                if (!r.unpackAndTest(maze[x + 1][y])) maze[x + 1][y] |= r.opposite;
                else {
                    maze[x][y] &= r.inverted;
                    maze[x + 1][y] &= kFacings[rotations[x + 1][y]].invertedOpposite;
                }
            }
            if (rotations[x][y] == South && y < kHeight - 2) {
                if (!r.unpackAndTest(maze[x][y + 1])) maze[x][y + 1] |= r.opposite;
                else {
                    maze[x][y] &= r.inverted;
                    maze[x][y + 1] &= kFacings[rotations[x][y + 1]].invertedOpposite;
                }
            }
        }
    }

    for (auto const& facing : kFacings) maze[halfWayPointX + facing.xOffset][halfWayPointY + facing.zOffset] &= facing.invertedOpposite;

    maze[halfWayPointX][halfWayPointY] = kExcluded;

    for (int x = 1; x < kMazeSize; x++) {
        for (int y = 1; y < kMazeSize; y++) {
            if (mazeLocal[x][y] == 0) {
                if (mazeLocal[x - 1][y] == 0) {
                    maze[x][y] |= kFacings[West].bite;
                    maze[x - 1][y] |= kFacings[West].opposite;
                }
                if (mazeLocal[x][y - 1] == 0) {
                    maze[x][y] |= kFacings[North].bite;
                    maze[x][y - 1] |= kFacings[North].opposite;
                }
            }
        }
    }

    for (int d = 0; d < 4; d++) {
        cornerClippings[d][0] = random.nextInt(maximumClipping) + 1;
        cornerClippings[d][1] = random.nextInt(maximumClipping) + 1;
        for (int y = 0; y < cornerClippings[d][0]; y++)
            for (int x = 0; x < cornerClippings[d][1]; x++)
                maze[kDiagonals[d].convertX(x, kWidth - 2)][kDiagonals[d].convertY(y, kHeight - 2)] |= kExcluded;
    }
}

std::vector<PieceRecord> layoutPieces(Maze const& maze, CornerClipping const& cornerClipping, BlockBox const& sc, jrand::RandomSource& random) {
    std::vector<PieceRecord> list;
    constexpr int            offset = 6;
    auto add = [&](PieceKind kind, int depth, int x, int y, int z, int rotation, std::string terrace = {}) {
        list.push_back(PieceRecord{kind, depth, x, y, z, rotation, std::move(terrace)});
    };

    // ---- processInnerWallsAndFloor
    for (int x = 0; x < kWidth - 1; x++) {
        for (int y = 0; y < kHeight - 1; y++) {
            bool const xCenter = x == (kWidth / 2) - 1;
            bool const yCenter = y == (kHeight / 2) - 1;
            // -------- HEDGE
            if ((!(xCenter || yCenter)) && (maze[x][y] & kExcluded) == kExcluded) continue;

            int rotation = 0;
            int xBB      = sc.minX + (x * 12) + offset;
            int yBB      = sc.minY + 1;
            int zBB      = sc.minZ + (y * 12) + offset;

            if (!(xCenter && yCenter)) {
                int const m = maze[x][y];
                switch (m & 0b1111) {
                case 0b0010: // FACE SOUTH
                    rotation++;
                    [[fallthrough]];
                case 0b0001: // FACE EAST
                    rotation++;
                    [[fallthrough]];
                case 0b1000: // FACE NORTH
                    rotation++;
                    [[fallthrough]];
                case 0b0100: // FACE WEST
                    if (random.nextBoolean()) add(PieceKind::HedgeCap, (x * kWidth) + y, xBB, yBB, zBB, rotation);
                    else add(PieceKind::HedgeCapPillar, (x * kWidth) + y, xBB, yBB, zBB, rotation);
                    break;
                case 0b1001: // NORTH EAST
                    rotation++;
                    [[fallthrough]];
                case 0b1100: // NORTH WEST
                    rotation++;
                    [[fallthrough]];
                case 0b0110: // SOUTH WEST
                    rotation++;
                    [[fallthrough]];
                case 0b0011: // SOUTH EAST
                    add(PieceKind::HedgeCorner, m, xBB, yBB, zBB, rotation);
                    break;
                case 0b1101: // NOT SOUTH
                    rotation++;
                    [[fallthrough]];
                case 0b1110: // NOT EAST
                    rotation++;
                    [[fallthrough]];
                case 0b0111: // NOT NORTH
                    rotation++;
                    [[fallthrough]];
                case 0b1011: // NOT WEST
                    add(PieceKind::HedgeT, m, xBB, yBB, zBB, rotation);
                    break;
                case 0b1010: // NORTH AND SOUTH
                    rotation++;
                    [[fallthrough]];
                case 0b0101: // EAST AND WEST
                    add(PieceKind::HedgeLine, m, xBB, yBB, zBB, rotation);
                    break;
                case 0b1111:
                    add(PieceKind::HedgeIntersection, m, xBB, yBB, zBB, kNone);
                    break;
                default:
                    if (random.nextInt(150) == 0) {
                        add(PieceKind::Terrace, m, xBB - 6, yBB - 3, zBB - 6, kNone, "twilightforest:courtyard/terrace_statue");
                    } else {
                        switch (random.nextInt(5)) {
                        case 1:
                            add(PieceKind::Terrace, m, xBB - 6, yBB - 3, zBB - 6, kNone, "twilightforest:courtyard/terrace_duct");
                            break;
                        case 2:
                            add(PieceKind::Terrace, m, xBB - 6, yBB - 3, zBB - 6, kNone, "twilightforest:courtyard/terrace_channel");
                            break;
                        case 3:
                            add(PieceKind::Terrace, m, xBB - 6, yBB - 3, zBB - 6, kNone, "twilightforest:courtyard/terrace_reservoir");
                            break;
                        case 4:
                            add(PieceKind::TerraceHedge, m, xBB - 6, yBB - 3, zBB - 6, kNone, "twilightforest:courtyard/terrace_hedge");
                            break;
                        default:
                            add(PieceKind::Terrace, m, xBB - 6, yBB - 3, zBB - 6, kNone, "twilightforest:courtyard/terrace_fire");
                        }
                    }
                    break;
                }
            }

            // -------- Hedge Connectors
            xBB = sc.minX + (x * 12) + offset;
            zBB = sc.minZ + (y * 12) + offset;

            int const  m            = maze[x][y];
            bool const connectWest  = kFacings[West].unpackAndTest(m);
            bool const connectNorth = kFacings[North].unpackAndTest(m);
            bool const connectEast  = kFacings[East].unpackAndTest(m);
            bool const connectSouth = kFacings[South].unpackAndTest(m);

            if (connectWest) {
                add(PieceKind::HedgePadder, m, xBB - 1, yBB, zBB, kNone);
                if (x > 0 && (maze[x - 1][y] & kExcluded) != kExcluded) add(PieceKind::HedgePadder, m, xBB - 7, yBB, zBB, kNone);
                add(PieceKind::HedgeLine, m, xBB - 6, yBB, zBB, kNone);
            }
            if (connectNorth) {
                add(PieceKind::HedgePadder, m, xBB + 4, yBB, zBB - 1, kCw90);
                if (y > 0 && (maze[x][y - 1] & kExcluded) != kExcluded) add(PieceKind::HedgePadder, m, xBB + 4, yBB, zBB - 7, kCw90);
                add(PieceKind::HedgeLine, m, xBB, yBB, zBB - 6, kCw90);
            }
            if ((x >= kWidth - 2 || (maze[x + 1][y] & kExcluded) == kExcluded) && connectEast) {
                add(PieceKind::HedgePadder, m, xBB + 5, yBB, zBB, kNone);
                add(PieceKind::HedgeLine, m, xBB + 6, yBB, zBB, kNone);
            }
            if ((y >= kHeight - 2 || (maze[x][y + 1] & kExcluded) == kExcluded) && connectSouth) {
                add(PieceKind::HedgePadder, m, xBB + 4, yBB, zBB + 5, kCw90);
                add(PieceKind::HedgeLine, m, xBB, yBB, zBB + 6, kCw90);
            }

            bool const hasNoTerrace = (m & 0b1111) != 0;

            bool const westHasNoTerraceOrIsSafe  = (x == 0 || (maze[x - 1][y] & kExcluded) == kExcluded || (maze[x - 1][y] & 0b1111) != 0);
            bool const northHasNoTerraceOrIsSafe = (y == 0 || (maze[x][y - 1] & kExcluded) == kExcluded || (maze[x][y - 1] & 0b1111) != 0);
            bool const eastHasNoTerraceOrIsSafe  = (x == kWidth - 2 || (maze[x + 1][y] & kExcluded) == kExcluded);
            bool const southHasNoTerraceOrIsSafe = (y == kHeight - 2 || (maze[x][y + 1] & kExcluded) == kExcluded);

            bool const westNorthHasNoTerraceOrIsSafe = (x == 0 || y == 0 || maze[x - 1][y - 1] != 0);
            bool const westSouthHasNoTerraceOrIsSafe = (x == 0 || y >= kHeight - 2 || maze[x - 1][y + 1] != 0);
            bool const eastNorthHasNoTerraceOrIsSafe = (x >= kWidth - 2 || y == 0 || maze[x + 1][y - 1] != 0);
            bool const eastSouthHasNoTerraceOrIsSafe = (x >= kWidth - 2 || y >= kHeight - 2 || maze[x + 1][y + 1] != 0);

            // -------- PATHS - cardinal
            if (xCenter && yCenter) add(PieceKind::Path, m, xBB - 1, yBB - 1, zBB - 1, kNone);
            if (hasNoTerrace && westHasNoTerraceOrIsSafe && !connectWest) add(PieceKind::Path, m, xBB - 7, yBB - 1, zBB - 1, kNone);
            if (hasNoTerrace && northHasNoTerraceOrIsSafe && !connectNorth) add(PieceKind::Path, m, xBB - 1, yBB - 1, zBB - 7, kNone);
            if (hasNoTerrace && eastHasNoTerraceOrIsSafe) add(PieceKind::Path, m, xBB + 5, yBB - 1, zBB - 1, kNone);
            if (hasNoTerrace && southHasNoTerraceOrIsSafe) add(PieceKind::Path, m, xBB - 1, yBB - 1, zBB + 5, kNone);

            // -------- PATHS - Diagonal
            if (hasNoTerrace && westHasNoTerraceOrIsSafe && northHasNoTerraceOrIsSafe && westNorthHasNoTerraceOrIsSafe)
                add(PieceKind::Path, m, xBB - 7, yBB - 1, zBB - 7, kNone);
            if (hasNoTerrace && westHasNoTerraceOrIsSafe && southHasNoTerraceOrIsSafe && westSouthHasNoTerraceOrIsSafe)
                add(PieceKind::Path, m, xBB - 7, yBB - 1, zBB + 5, kNone);
            if (hasNoTerrace && eastHasNoTerraceOrIsSafe && northHasNoTerraceOrIsSafe && eastNorthHasNoTerraceOrIsSafe)
                add(PieceKind::Path, m, xBB + 5, yBB - 1, zBB - 7, kNone);
            if (hasNoTerrace && eastHasNoTerraceOrIsSafe && southHasNoTerraceOrIsSafe && eastSouthHasNoTerraceOrIsSafe)
                add(PieceKind::Path, m, xBB + 5, yBB - 1, zBB + 5, kNone);
        }
    }

    // ---- processOuterWalls
    int const y1 = sc.minY + 1;
    for (int d = 0; d < 4; d++) {
        Diagonal const& diagonal = kDiagonals[d];
        // Walls at corner notches going with X Axis, crossing Z Axis. Sideways.
        int zBoundX = diagonal.top ? sc.minZ + (cornerClipping[d][0] * 12) - 3 : sc.maxZ - (cornerClipping[d][0] * 12) + 1;
        add(PieceKind::WallPadder, (cornerClipping[d][1] * 2) + 1, diagonal.left ? sc.minX + 2 : sc.maxX - 2, y1, zBoundX, kNone);

        int xPadOffset = diagonal.left ? 11 : -1;
        for (int i = 0; i < cornerClipping[d][1] - 1; i++) {
            int xBound = diagonal.left ? sc.minX + (i * 12) + 3 : sc.maxX - (i * 12) - 13;
            add(PieceKind::Wall, i * 2, xBound, y1, zBoundX, kNone);
            add(PieceKind::WallPadder, (i * 2) + 1, xBound + xPadOffset, y1, zBoundX, kNone);
        }

        // Walls at corner notches going with Z Axis, crossing X Axis. Up/Down.
        int xBoundZ = diagonal.left ? sc.minX + (cornerClipping[d][1] * 12) - 1 : sc.maxX - (cornerClipping[d][1] * 12) + 3;
        add(PieceKind::WallPadder, (cornerClipping[d][1] * 2) + 1, xBoundZ, y1, diagonal.top ? sc.minZ + 2 : sc.maxZ - 2, kCw90);

        int zPadOffset = diagonal.top ? 11 : -1;
        for (int i = 0; i < cornerClipping[d][0] - 1; i++) {
            int zBound = diagonal.top ? sc.minZ + (i * 12) + 3 : sc.maxZ - (i * 12) - 13;
            add(PieceKind::Wall, i * 2, xBoundZ, y1, zBound, kCw90);
            add(PieceKind::WallPadder, (i * 2) + 1, xBoundZ, y1, zBound + zPadOffset, kCw90);
        }

        // WALL CORNERS
        int wallCornerInnerX = sc.minX + (diagonal.convertX(cornerClipping[d][1], kWidth - 1) * 12);
        int wallCornerInnerZ = sc.minZ + (diagonal.convertY(cornerClipping[d][0], kHeight - 1) * 12);

        int  cornerRotation = d % 4;
        bool shiftX         = cornerRotation == kCw180 || cornerRotation == kCcw90;
        bool shiftZ         = cornerRotation == kCw90 || cornerRotation == kCw180;

        // These touch upper/lower borders
        add(PieceKind::WallCornerOuter, d * 3, wallCornerInnerX + (shiftZ ? (shiftX ? 1 : 7) : (shiftX ? -3 : 3)), y1,
            (diagonal.top ? sc.minZ : sc.maxZ - 1) + (shiftZ ? (shiftX ? 4 : 0) : (shiftX ? 1 : -3)), cornerRotation);
        // These touch side borders
        add(PieceKind::WallCornerOuter, (d * 3) + 1, (diagonal.left ? sc.minX : sc.maxX - 1) + (shiftZ ? (shiftX ? 1 : 4) : (shiftX ? -3 : 0)), y1,
            wallCornerInnerZ + (shiftZ ? (shiftX ? 7 : 3) : (shiftX ? 1 : -3)), cornerRotation);
        // These are inner corners
        add(PieceKind::WallCornerInner, (d * 3) + 3, wallCornerInnerX + (shiftZ ? (shiftX ? -1 : 13) : (shiftX ? -9 : 5)), y1,
            wallCornerInnerZ + (shiftZ ? (shiftX ? 13 : 5) : (shiftX ? -1 : -9)), cornerRotation);
    }

    // Top / North
    for (int i = cornerClipping[3][1]; i < (kWidth - 1) - cornerClipping[0][1]; i++) {
        add(PieceKind::Wall, i, sc.minX + (i * 12) + offset - 3, y1, sc.minZ - 3, kNone);
        add(PieceKind::WallPadder, i, sc.minX + (i * 12) + offset - 4, y1, sc.minZ - 3, kNone);
    }
    {
        int i = (kWidth - 1) - cornerClipping[0][1];
        add(PieceKind::WallPadder, i, sc.minX + (i * 12) + offset - 4, y1, sc.minZ - 3, kNone);
    }
    // Bottom / South
    for (int i = cornerClipping[2][1]; i < (kWidth - 1) - cornerClipping[1][1]; i++) {
        add(PieceKind::Wall, i, sc.minX + (i * 12) + offset - 3, y1, sc.maxZ + 1, kNone);
        add(PieceKind::WallPadder, i, sc.minX + (i * 12) + offset - 4, y1, sc.maxZ + 1, kNone);
    }
    {
        int i = (kWidth - 1) - cornerClipping[1][1];
        add(PieceKind::WallPadder, i, sc.minX + (i * 12) + offset - 4, y1, sc.maxZ + 1, kNone);
    }
    // Left / West
    for (int i = cornerClipping[3][0]; i < (kHeight - 1) - cornerClipping[2][0]; i++) {
        add(PieceKind::Wall, i, sc.minX - 1, y1, sc.minZ + (i * 12) + offset - 3, kCw90);
        add(PieceKind::WallPadder, i, sc.minX - 1, y1, sc.minZ + (i * 12) + offset - 4, kCw90);
    }
    {
        int i = (kHeight - 1) - cornerClipping[2][0];
        add(PieceKind::WallPadder, i, sc.minX - 1, y1, sc.minZ + (i * 12) + offset - 4, kCw90);
    }
    // Right / East
    for (int i = cornerClipping[0][0]; i < (kHeight - 1) - cornerClipping[1][0]; i++) {
        add(PieceKind::Wall, i, sc.maxX + 3, y1, sc.minZ + (i * 12) + offset - 3, kCw90);
        add(PieceKind::WallPadder, i, sc.maxX + 3, y1, sc.minZ + (i * 12) + offset - 4, kCw90);
    }
    {
        int i = (kHeight - 1) - cornerClipping[1][0];
        add(PieceKind::WallPadder, i, sc.maxX + 3, y1, sc.minZ + (i * 12) + offset - 4, kCw90);
    }
    return list;
}

std::string facingString(int directions) {
    switch (directions & 0b1111) {
    case 0b0010:
        return " ╷ ";
    case 0b0001:
        return " ╶─";
    case 0b1000:
        return " ╵ ";
    case 0b0100:
        return "─╴ ";
    case 0b1001:
        return " └─";
    case 0b1100:
        return "─┘ ";
    case 0b0110:
        return "─┐ ";
    case 0b0011:
        return " ┌─";
    case 0b1101:
        return "─┴─";
    case 0b1110:
        return "─┤ ";
    case 0b0111:
        return "─┬─";
    case 0b1011:
        return " ├─";
    case 0b1010:
        return " │ ";
    case 0b0101:
        return "───";
    case 0b1111:
        return "─┼─";
    default:
        return " • ";
    }
}

} // namespace gf::core::naga
