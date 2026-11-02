// GloamingForest: an independent reference of TF's LegacyLandmarkPlacements (TeamTwilight/twilightforest @ 9e40a757,
// LGPL-2.1-or-later) and vanilla RandomSpread placement, written with Java semantics (BigInt longs,
// Int32 ints). It generates the gtest goldens:
//   node tests/landmark/golden/grid_reference.js > tests/landmark/golden/GridGolden.inc
'use strict';
const M64 = (1n << 64n) - 1n;
const toLong = (v) => BigInt.asIntN(64, BigInt(v));
const toInt = (v) => Number(BigInt.asIntN(32, BigInt(v)));
const i32 = (v) => v | 0;
const imul = (a, b) => Math.imul(a, b);
const jabs = (v) => (v < 0 ? i32(-v) : v);
const jmod = (a, b) => { const r = a % b; return r === 0 ? 0 : r; }; // JS % truncates like Java

class LegacyRandom {
  constructor(seed) { this.setSeed(seed); }
  setSeed(seed) { this.seed = (toLong(seed) ^ 0x5DEECE66Dn) & ((1n << 48n) - 1n); }
  next(bits) {
    this.seed = (this.seed * 0x5DEECE66Dn + 0xBn) & ((1n << 48n) - 1n);
    return toInt(this.seed >> BigInt(48 - bits));
  }
  nextInt(bound) {
    if (bound === undefined) return this.next(32);
    if ((bound & -bound) === bound) return toInt((BigInt(bound) * BigInt(this.next(31))) >> 31n);
    let bits, val;
    do { bits = this.next(31); val = bits % bound; } while (i32(bits - val + (bound - 1)) < 0);
    return val;
  }
  nextLong() { const hi = BigInt(this.next(32)) << 32n; return toLong(hi + BigInt(this.next(32))); }
  nextFloat() { return this.next(24) / 16777216; }
}

function nearestCenter(chunkX, chunkZ) {
  const regionX = i32(chunkX + 8) >> 4;
  const regionZ = i32(chunkZ + 8) >> 4;
  let seed = toLong(BigInt(regionX) * 3129871n) ^ toLong(BigInt(regionZ) * 116129781n);
  seed = toLong(toLong(seed * seed) * 42317861n + seed * 7n);
  const num = (s) => Number((seed >> BigInt(s)) & 3n);
  const centerX = 8 + num(12) - num(15);
  const centerZ = 8 + num(18) - num(21);
  const ccz = regionZ >= 0 ? i32(imul(i32(imul(regionZ, 16) + centerZ - 8), 16) + 8)
                           : i32(imul(i32(imul(regionZ, 16) + (16 - centerZ) - 8), 16) + 9);
  const ccx = regionX >= 0 ? i32(imul(i32(imul(regionX, 16) + centerX - 8), 16) + 8)
                           : i32(imul(i32(imul(regionX, 16) + (16 - centerX) - 8), 16) + 9);
  return [ccx, ccz];
}
const chunkHasCenter = (cx, cz) => { const c = nearestCenter(cx, cz); return cx === (c[0] >> 4) && cz === (c[1] >> 4); };
function jround(f) { return Math.floor(f + 0.5); } // Math.round(float) == floor(a + 0.5) exactly
const roundRegion = (chunk) => jround(Math.fround(Math.fround(chunk) / 16));

const VARIETY = [['small_hollow_hill', 6], ['medium_hollow_hill', 3], ['large_hollow_hill', 1], ['hedge_maze', 2], ['naga_courtyard', 2], ['lich_tower', 2]];
function pickVariety(seed, chunkX, chunkZ) {
  chunkX = imul(roundRegion(chunkX), 16);
  chunkZ = imul(roundRegion(chunkZ), 16);
  const ox = jabs(jmod(i32(chunkX + 64) >> 4, 8));
  const oz = jabs(jmod(i32(chunkZ + 64) >> 4, 8));
  if ((ox === 4 && oz === 5) || (ox === 4 && oz === 3)) return 'lich_tower';
  if ((ox === 5 && oz === 4) || (ox === 3 && oz === 4)) return 'naga_courtyard';
  const r = new LegacyRandom(toLong(BigInt(seed) + BigInt(chunkX) * 25117n + BigInt(chunkZ) * 151121n));
  let idx = r.nextInt(16);
  for (const [k, w] of VARIETY) { idx -= w; if (idx < 0) return k; }
  return 'small_hollow_hill';
}

function centerScanner(bx, bz, radius) {
  const xc = (bx >> 4) & ~15, zc = (bz >> 4) & ~15, sp = 16;
  let dX = 0, dZ = 0, card = -1; const out = [];
  const hasNext = () => dX + 1 <= radius;
  const gen = (a, b) => nearestCenter(i32(xc + a), i32(zc + b));
  const tick = () => { if (!hasNext()) return; if (dZ < radius) dZ++; else { dZ = 1; dX++; } };
  while (hasNext()) {
    if (card === 0) { card++; out.push(gen(dZ * sp, -dX * sp)); }
    else if (card === 1) { card++; out.push(gen(-dX * sp, -dZ * sp)); }
    else if (card === 2) { card++; out.push(gen(-dZ * sp, dX * sp)); }
    else { card = 0; const r = gen(dX * sp, dZ * sp); tick(); out.push(r); }
  }
  return out;
}

const floorDiv = (a, b) => Math.floor(a / b);
function potential(spacing, separation, triangular, salt, seed, cx, cz) {
  const i = floorDiv(cx, spacing), j = floorDiv(cz, spacing);
  const r = new LegacyRandom(0);
  r.setSeed(toLong(BigInt(i) * 341873128712n + BigInt(j) * 132897987541n + BigInt(seed) + BigInt(salt)));
  const k = spacing - separation;
  const ev = () => triangular ? Math.trunc((r.nextInt(k) + r.nextInt(k)) / 2) : r.nextInt(k);
  const l = ev(); const i1 = ev();
  return [i * spacing + l, j * spacing + i1];
}
function freqDefault(seed, salt, cx, cz, p) {
  const r = new LegacyRandom(0);
  // probabilityReducer: setLargeFeatureWithSalt(levelSeed, salt, regionX, regionZ)
  r.setSeed(toLong(BigInt(salt) * 341873128712n + BigInt(cx) * 132897987541n + BigInt(seed) + BigInt(cz)));
  return Math.fround(r.nextFloat()) < Math.fround(p);
}

// ---- emit ------------------------------------------------------------------------------------------
const lines = [];
lines.push('// Generated by tests/landmark/golden/grid_reference.js (Java-semantics reference); do not edit.');
lines.push('// Landmark grid goldens: TF LegacyLandmarkPlacements and vanilla RandomSpread placement.');
const coords = [];
for (const v of [-2147483, -100000, -4097, -1000, -257, -129, -100, -64, -33, -24, -17, -16, -9, -8, -7, -1, 0, 1, 7, 8, 9, 15, 16, 23, 24, 25, 40, 63, 64, 100, 128, 255, 256, 1000, 4096, 99999, 2147483]) coords.push(v);
lines.push('struct CenterRow { int cx, cz, x, z; bool has; int manhattan; };');
lines.push('inline constexpr CenterRow kCenterRows[] = {');
for (const a of coords) for (const b of [coords[0], -17, -8, 0, 8, 23, 1000, coords[coords.length - 1]]) {
  const c = nearestCenter(a, b);
  const has = chunkHasCenter(a, b);
  const man = Math.abs(a - (c[0] >> 4)) + Math.abs(b - (c[1] >> 4));
  lines.push(`    {${a}, ${b}, ${c[0]}, ${c[1]}, ${has}, ${man}},`);
}
lines.push('};');
lines.push('struct RoundRow { int chunk, region; };');
lines.push('inline constexpr RoundRow kRoundRows[] = {');
for (const v of [-2147483647, -16777217, -16777216, -40, -24, -8, -7, 0, 7, 8, 9, 24, 25, 40, 16777215, 16777217, 2147483647]) lines.push(`    {${v}, ${roundRegion(v)}},`);
lines.push('};');
const seeds = ['0LL', '20260521LL', '-123456789012345LL', '1234567890123456789LL'];
lines.push('struct VarietyRow { long long seed; int cx, cz; char const* key; };');
lines.push('inline constexpr VarietyRow kVarietyRows[] = {');
for (const s of seeds) {
  const sv = BigInt(s.replace('LL', ''));
  for (let cx = -80; cx <= 80; cx += 16) for (let cz = -80; cz <= 80; cz += 16) lines.push(`    {${s}, ${cx}, ${cz}, "twilightforest:${pickVariety(sv, cx, cz)}"},`);
  for (const [cx, cz] of [[-1000, 333], [4097, -4097], [123456, -654321], [7, 9], [-7, -9]]) lines.push(`    {${s}, ${cx}, ${cz}, "twilightforest:${pickVariety(sv, cx, cz)}"},`);
}
lines.push('};');
lines.push('struct ScanRow { int bx, bz, radius; std::vector<std::pair<int, int>> centers; };');
lines.push('inline std::vector<ScanRow> const kScanRows = {');
for (const [bx, bz, r] of [[0, 0, 0], [0, 0, 1], [0, 0, 2], [1000, -2000, 3], [-5000, 77, 2]]) {
  const cs = centerScanner(bx, bz, r).map((c) => `{${c[0]}, ${c[1]}}`).join(', ');
  lines.push(`    {${bx}, ${bz}, ${r}, {${cs}}},`);
}
lines.push('};');
// hollow_tree: spacing 7 separation 5 triangular salt 34481210; camp: 18/14 triangular salt 701432212; fallen_trunk 7/5 triangular 1275623845
lines.push('struct SpreadRow { int spacing, separation; bool triangular; int salt; long long seed; int cx, cz, px, pz; bool freq80; bool freq50; };');
lines.push('inline constexpr SpreadRow kSpreadRows[] = {');
for (const [sp, se, tri, salt] of [[7, 5, true, 34481210], [18, 14, true, 701432212], [7, 5, true, 1275623845], [32, 8, false, 14357617]]) {
  for (const s of seeds) {
    const sv = BigInt(s.replace('LL', ''));
    for (const [cx, cz] of [[0, 0], [5, -3], [-20, 13], [100, 100], [-777, 4242]]) {
      const p = potential(sp, se, tri, salt, sv, cx, cz);
      lines.push(`    {${sp}, ${se}, ${tri}, ${salt}, ${s}, ${cx}, ${cz}, ${p[0]}, ${p[1]}, ${freqDefault(sv, salt, p[0], p[1], 0.8)}, ${freqDefault(sv, salt, p[0], p[1], 0.5)}},`);
    }
  }
}
lines.push('};');
process.stdout.write(lines.join('\n') + '\n');
