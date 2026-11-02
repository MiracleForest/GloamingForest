// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Translated from The Twilight Forest 4.8 (TeamTwilight/twilightforest @ 9e40a757):
//   src/main/java/twilightforest/world/components/structures/TFMaze.java
//   src/main/java/twilightforest/world/components/structures/HedgeMazeComponent.java
//   src/main/java/twilightforest/world/components/structures/minotaurmaze/MinotaurMazeComponent.java
// Copyright (c) 2012-2017 Ben Mazur / Benimatic and the TeamTwilight contributors.
// Licensed under the GNU Lesser General Public License v2.1 or later; this translation is part
// of GloamingForest and distributed under LGPL-3.0-or-later (see COPYING.LESSER and NOTICE).
//
// Java oracle of tests/structure/MazeTest.cpp: TFMaze's grid code (verbatim apart from the
// Minecraft types: RandomSource -> java.util.Random, which LegacyRandomSource reproduces bit for
// bit) driven like the labyrinth, the hedge maze and the dark tower drive it. Regenerate the
// goldens with
//   javac -d out tests/structure/oracle/MazeOracle.java && java -cp out MazeOracle > tests/structure/MazeGolden.inc
import java.util.Random;

public class MazeOracle {
	static final int OUT_OF_BOUNDS = Integer.MIN_VALUE;
	static final int OOB = OUT_OF_BOUNDS;
	static final int ROOM = 5;
	static final int DOOR = 6;

	final int width, depth, rawWidth, rawDepth;
	final int[] storage;
	float doorRarity = 0F;
	float torchRarity = 0.75F;

	MazeOracle(int cellsWidth, int cellsDepth) {
		this.width = cellsWidth;
		this.depth = cellsDepth;
		this.rawWidth = width * 2 + 1;
		this.rawDepth = depth * 2 + 1;
		storage = new int[rawWidth * rawDepth];
	}

	int getCell(int x, int z) { return getRaw(x * 2 + 1, z * 2 + 1); }
	void putCell(int x, int z, int value) { putRaw(x * 2 + 1, z * 2 + 1, value); }
	boolean cellEquals(int x, int z, int value) { return getCell(x, z) == value; }

	void putWall(int sx, int sz, int dx, int dz, int value) {
		if (dx == sx + 1 && dz == sz) putRaw(sx * 2 + 2, sz * 2 + 1, value);
		if (dx == sx - 1 && dz == sz) putRaw(sx * 2, sz * 2 + 1, value);
		if (dx == sx && dz == sz + 1) putRaw(sx * 2 + 1, sz * 2 + 2, value);
		if (dx == sx && dz == sz - 1) putRaw(sx * 2 + 1, sz * 2, value);
	}

	void putRaw(int rawx, int rawz, int value) {
		if (rawx >= 0 && rawx < rawWidth && rawz >= 0 && rawz < rawDepth) storage[rawz * rawWidth + rawx] = value;
	}

	int getRaw(int rawx, int rawz) {
		if (rawx < 0 || rawx >= rawWidth || rawz < 0 || rawz >= rawDepth) return OUT_OF_BOUNDS;
		return storage[rawz * rawWidth + rawx];
	}

	boolean allCellsNonZero() {
		for (int x = 0; x < width; x++) for (int z = 0; z < depth; z++) if (getCell(x, z) == 0) return false;
		return true;
	}

	void resetCells() { java.util.Arrays.fill(storage, 0); }

	boolean shouldTorch(int rx, int rz, Random rand) {
		if (getRaw(rx + 1, rz) == OOB || getRaw(rx - 1, rz) == OOB || getRaw(rx, rz + 1) == OOB || getRaw(rx, rz - 1) == OOB) return false;
		if ((getRaw(rx + 1, rz) == 0 && getRaw(rx - 1, rz) == 0) || (getRaw(rx, rz + 1) == 0 && getRaw(rx, rz - 1) == 0)) return false;
		return rand.nextFloat() <= this.torchRarity;
	}

	boolean shouldPillar(int rx, int rz) {
		if (getRaw(rx + 1, rz) == OOB || getRaw(rx - 1, rz) == OOB || getRaw(rx, rz + 1) == OOB || getRaw(rx, rz - 1) == OOB) return false;
		return (getRaw(rx + 1, rz) != 0 || getRaw(rx - 1, rz) != 0) && (getRaw(rx, rz + 1) != 0 || getRaw(rx, rz - 1) != 0);
	}

	boolean shouldTree(int rx, int rz, Random rand) {
		if ((rx == 0 || rx == rawWidth - 1) && (getRaw(rx, rz + 1) != 0 || getRaw(rx, rz - 1) != 0)) return true;
		if ((rz == 0 || rz == rawDepth - 1) && (getRaw(rx + 1, rz) != 0 || getRaw(rx - 1, rz) != 0)) return true;
		return rand.nextInt(50) == 0;
	}

	void carveRoom1(int cx, int cz) {
		int rx = cx * 2 + 1;
		int rz = cz * 2 + 1;
		for (int i = -2; i <= 2; i++) for (int j = -2; j <= 2; j++) putRaw(rx + i, rz + j, ROOM);
		putCell(rx, rz + 1, 0);
		putCell(rx, rz - 1, 0);
		putCell(rx + 1, rz, 0);
		putCell(rx - 1, rz, 0);
		if (getRaw(rx, rz + 4) != OUT_OF_BOUNDS) putRaw(rx, rz + 3, ROOM);
		if (getRaw(rx, rz - 4) != OUT_OF_BOUNDS) putRaw(rx, rz - 3, ROOM);
		if (getRaw(rx + 4, rz) != OUT_OF_BOUNDS) putRaw(rx + 3, rz, ROOM);
		if (getRaw(rx - 4, rz) != OUT_OF_BOUNDS) putRaw(rx - 3, rz, ROOM);
	}

	void add4Exits() {
		int hx = rawWidth / 2 + 1;
		int hz = rawDepth / 2 + 1;
		putRaw(hx, 0, ROOM);
		putRaw(hx, rawDepth - 1, ROOM);
		putRaw(0, hz, ROOM);
		putRaw(rawWidth - 1, hz, ROOM);
	}

	void rbGen(int sx, int sz, Random rand) {
		putCell(sx, sz, 1);
		int unvisited = 0;
		if (cellEquals(sx + 1, sz, 0)) unvisited++;
		if (cellEquals(sx - 1, sz, 0)) unvisited++;
		if (cellEquals(sx, sz + 1, 0)) unvisited++;
		if (cellEquals(sx, sz - 1, 0)) unvisited++;
		if (unvisited == 0) return;
		int rn = rand.nextInt(unvisited);
		int dx, dz;
		dx = dz = 0;
		if (cellEquals(sx + 1, sz, 0)) { if (rn == 0) { dx = sx + 1; dz = sz; } rn--; }
		if (cellEquals(sx - 1, sz, 0)) { if (rn == 0) { dx = sx - 1; dz = sz; } rn--; }
		if (cellEquals(sx, sz + 1, 0)) { if (rn == 0) { dx = sx; dz = sz + 1; } rn--; }
		if (cellEquals(sx, sz - 1, 0)) { if (rn == 0) { dx = sx; dz = sz - 1; } }
		if (rand.nextFloat() <= this.doorRarity) putWall(sx, sz, dx, dz, DOOR);
		else putWall(sx, sz, dx, dz, 2);
		rbGen(dx, dz, rand);
		rbGen(sx, sz, rand);
		rbGen(sx, sz, rand);
	}

	String dump() {
		StringBuilder sb = new StringBuilder();
		for (int z = 0; z < rawDepth; z++) {
			for (int x = 0; x < rawWidth; x++) {
				int v = getRaw(x, z);
				sb.append(v == 0 ? '#' : v == 1 ? '.' : v == 2 ? ',' : v == ROOM ? 'R' : v == DOOR ? 'D' : '?');
			}
			sb.append('/');
		}
		return sb.toString();
	}

	// The decoration pass of copyToStructure: which even/even posts get a torch / a pillar / a tree
	String decorations(Random rand) {
		StringBuilder sb = new StringBuilder();
		for (int x = 0; x < rawWidth; x++) {
			for (int z = 0; z < rawDepth; z++) {
				if (getRaw(x, z) == 0 && x % 2 == 0 && z % 2 == 0) {
					sb.append(shouldTree(x, z, rand) ? 'T' : '-');
					sb.append(shouldPillar(x, z) ? 'P' : '-');
					sb.append(shouldTorch(x, z, rand) ? 'L' : '-');
				}
			}
		}
		return sb.toString();
	}

	// MinotaurMazeComponent#isNearRoom
	static boolean isNearRoom(int dx, int dz, int[] rcoords, int range) {
		if (dx == 1 && dz == 1) return true;
		for (int i = 0; i < rcoords.length / 2; i++) {
			int rx = rcoords[i * 2];
			int rz = rcoords[i * 2 + 1];
			if (rx == 0 && rz == 0) continue;
			if (Math.abs(dx - rx) < range && Math.abs(dz - rz) < range) return true;
		}
		return false;
	}

	static void emit(String name, String value) {
		System.out.println("{\"" + name + "\", \"" + value + "\"},");
	}

	public static void main(String[] args) {
		// java.util.Random / LegacyRandomSource vectors
		{
			Random r = new Random(0);
			StringBuilder sb = new StringBuilder();
			sb.append(r.nextInt()).append(' ').append(r.nextInt(10)).append(' ').append(r.nextInt(16)).append(' ').append(r.nextInt(1000)).append(' ')
				.append(r.nextLong()).append(' ').append(r.nextBoolean()).append(' ').append(Float.floatToIntBits(r.nextFloat())).append(' ')
				.append(Double.doubleToLongBits(r.nextDouble())).append(' ').append(r.nextInt(Integer.MAX_VALUE));
			emit("random0", sb.toString());
			Random g = new Random(42);
			sb = new StringBuilder();
			for (int i = 0; i < 4; i++) sb.append(Double.doubleToLongBits(g.nextGaussian())).append(' ');
			emit("gaussian42", sb.toString().trim());
			Random big = new Random(-8_123_456_789_012L);
			sb = new StringBuilder();
			for (int i = 0; i < 16; i++) sb.append(big.nextInt(3 + i * 977)).append(' ');
			emit("bounded", sb.toString().trim());
		}
		{
			StringBuilder sb = new StringBuilder();
			int[][] ps = {{0, 0, 0}, {1, 2, 3}, {-1, 64, -1}, {123456, -60, -987654}, {30000000, 319, -30000000}};
			for (int[] p : ps) {
				long i = (long) (p[0] * 3129871) ^ (long) p[2] * 116129781L ^ (long) p[1];
				i = i * i * 42317861L + i * 11L;
				sb.append(i >> 16).append(' ');
			}
			emit("mthGetSeed", sb.toString().trim());
		}

		// The labyrinth level (MinotaurMazeComponent, maze size 22, rooms, entrance 11, 11)
		for (long seed : new long[]{12345L, -4242L, 90342903L}) {
			Random random = new Random(seed);
			MazeOracle maze = new MazeOracle(22, 22);
			int nrooms = 7;
			int[] rcoords = new int[nrooms * 2];
			rcoords[0] = 11;
			rcoords[1] = 11;
			maze.carveRoom1(11, 11);
			for (int i = 1; i < nrooms; i++) {
				int rx, rz;
				do {
					rx = random.nextInt(22 - 2) + 1;
					rz = random.nextInt(22 - 2) + 1;
				} while (isNearRoom(rx, rz, rcoords, i == 1 ? 7 : 4));
				maze.carveRoom1(rx, rz);
				rcoords[i * 2] = rx;
				rcoords[i * 2 + 1] = rz;
			}
			maze.rbGen(0, 0, random);
			emit("labyrinth_" + seed, maze.dump());
			emit("labyrinth_deco_" + seed, maze.decorations(random));
		}

		// The hedge maze (HedgeMazeComponent: 16 cells, oddBias 2, rooms until every cell is reached)
		for (long seed : new long[]{7L, 1234567890123L}) {
			Random rand = new Random(seed);
			MazeOracle maze = new MazeOracle(16, 16);
			int nrooms = 16 / 3;
			while (!maze.allCellsNonZero()) {
				maze.resetCells();
				int[] rcoords = new int[nrooms * 2];
				for (int i = 0; i < nrooms; i++) {
					int rx, rz;
					do {
						rx = rand.nextInt(16 - 2) + 1;
						rz = rand.nextInt(16 - 2) + 1;
					} while (isNearRoom(rx, rz, rcoords, 3)); // HedgeMazeComponent#isNearRoom: range 3
					maze.carveRoom1(rx, rz);
					rcoords[i * 2] = rx;
					rcoords[i * 2 + 1] = rz;
				}
				maze.rbGen(0, 0, rand);
			}
			maze.add4Exits();
			emit("hedge_" + seed, maze.dump());
			emit("hedge_deco_" + seed, maze.decorations(rand));
		}

		// A dark tower floor maze (doors: doorRarity 0.3 as the tower's decoration rooms use)
		{
			Random rand = new Random(555L);
			MazeOracle maze = new MazeOracle(8, 8);
			maze.doorRarity = 0.3F;
			maze.rbGen(0, 5, rand);
			emit("doors_555", maze.dump());
		}
	}
}
