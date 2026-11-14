// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Java oracle dumper (unit F13): original code, LGPL-3.0-or-later (see COPYING.LESSER).
package gf.oracle;

import net.minecraft.core.Registry;
import net.minecraft.core.registries.Registries;
import net.minecraft.world.level.levelgen.GenerationStep;
import net.minecraft.world.level.levelgen.structure.Structure;

import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.stream.Collectors;

/** Seed-independent dumps of the Structure registry. */
final class StructureDumps {
	private StructureDumps() {
	}

	/** The registry id of a structure (it must be registered). */
	private static String id(Registry<Structure> registry, Structure structure) {
		return registry.getResourceKey(structure).map(Ctx::id)
			.orElseThrow(() -> new IllegalStateException("a structure of the registry stream has no key: " + structure));
	}

	// -----------------------------------------------------------------------------------------
	// structure_steps

	/**
	 * The Structure registry the way ChunkGenerator#applyBiomeDecoration walks it: grouped by
	 * {@code structure.step().ordinal()} with {@code Collectors.groupingBy}, each group in registry
	 * stream order. applyBiomeDecoration calls {@code setFeatureSeed(decorationSeed, l, step)} before
	 * the l-th structure of a step, so a structure's position in its group is its seed salt.
	 */
	static Object structureSteps(Ctx ctx) {
		// the registry the Gloaming Forest's chunk generator decorates with (a WorldGenRegion's
		// registryAccess is its level's, the server's)
		Registry<Structure> registry = ctx.gloaming().registryAccess().registryOrThrow(Registries.STRUCTURE);
		// exactly applyBiomeDecoration's grouping
		Map<Integer, List<Structure>> byStep = registry.stream().collect(Collectors.groupingBy(s -> s.step().ordinal()));

		GenerationStep.Decoration[] decorations = GenerationStep.Decoration.values();
		List<Object> stepNames = Json.arr();
		List<Object> steps = Json.arr();
		Set<String> seen = new HashSet<>();
		for (GenerationStep.Decoration d : decorations) {
			stepNames.add(d.getSerializedName());
			List<Object> ids = Json.arr();
			for (Structure s : byStep.getOrDefault(d.ordinal(), Collections.emptyList())) {
				String id = id(registry, s);
				if (!seen.add(id)) throw new IllegalStateException("structure " + id + " is listed twice");
				ids.add(id);
			}
			steps.add(ids);
		}
		List<Object> order = Json.arr();
		registry.stream().forEach(s -> order.add(id(registry, s)));
		// the groups partition the stream (a structure at an ordinal outside GenerationStep.Decoration
		// would be left out of every group)
		if (order.size() != registry.size() || seen.size() != order.size()) {
			throw new IllegalStateException("the registry stream has " + order.size() + " structures, the groups of GenerationStep.Decoration " + seen.size()
				+ " (step ordinals present: " + byStep.keySet() + "), Registry#size " + registry.size());
		}

		Map<String, Object> out = Json.obj();
		out.put("note", "The Structure registry (registryAccess().registryOrThrow(Registries.STRUCTURE)) as ChunkGenerator#applyBiomeDecoration walks it. "
			+ "steps[o]: the ids of the structures with step().ordinal() == o, in registry stream order (empty when none), for every GenerationStep.Decoration o "
			+ "(stepNames[o] = getSerializedName()); applyBiomeDecoration calls setFeatureSeed(decorationSeed, index, o) before the starts of steps[o][index]. "
			+ "order: every id in registry stream order; count: Registry#size. Every enabled data pack's structures: Minecraft's and upstream's.");
		out.put("count", registry.size());
		out.put("stepNames", stepNames);
		out.put("steps", steps);
		out.put("order", order);
		return out;
	}
}
