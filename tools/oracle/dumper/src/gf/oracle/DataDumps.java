// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Java oracle dumper (unit F13): original code, LGPL-3.0-or-later (see COPYING.LESSER).
package gf.oracle;

import com.mojang.serialization.JsonOps;
import net.minecraft.core.Holder;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.core.registries.Registries;
import net.minecraft.resources.ResourceKey;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.packs.resources.Resource;
import net.minecraft.util.RandomSource;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.EntityType;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.ai.attributes.Attribute;
import net.minecraft.world.entity.ai.attributes.AttributeInstance;
import net.minecraft.world.entity.ai.attributes.AttributeSupplier;
import net.minecraft.world.entity.ai.attributes.DefaultAttributes;
import net.minecraft.world.inventory.ContainerLevelAccess;
import net.minecraft.world.item.Item;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.item.crafting.Ingredient;
import net.minecraft.world.item.crafting.Recipe;
import net.minecraft.world.item.crafting.RecipeHolder;
import net.minecraft.world.item.crafting.ShapedRecipe;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.storage.loot.LootParams;
import net.minecraft.world.level.storage.loot.LootTable;
import net.minecraft.world.level.storage.loot.parameters.LootContextParam;
import net.minecraft.world.level.storage.loot.parameters.LootContextParamSet;
import net.minecraft.world.level.storage.loot.parameters.LootContextParamSets;
import net.minecraft.world.level.storage.loot.parameters.LootContextParams;
import net.minecraft.world.phys.Vec3;
import net.neoforged.neoforge.common.util.FakePlayer;
import net.neoforged.neoforge.common.util.FakePlayerFactory;
import twilightforest.config.TFConfig;
import twilightforest.inventory.UncraftingContainer;
import twilightforest.inventory.UncraftingMenu;

import java.util.ArrayList;
import java.util.IdentityHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.TreeMap;
import java.util.TreeSet;

/** Seed-independent dumps: mob attributes, loot statistics, recipes, uncrafting table outputs. */
final class DataDumps {
	private DataDumps() {
	}

	static final String TF = "twilightforest";

	/** Whether a resource comes from TF's own data pack (the mod jar). */
	static boolean fromTf(Resource r) {
		return r.sourcePackId().contains(TF);
	}

	/** Ids of the TF data pack's files under data/<ns>/<dir>/ (all namespaces), sorted. */
	static TreeMap<String, ResourceLocation> tfData(MinecraftServer server, String dir, TreeSet<String> packs) {
		TreeMap<String, ResourceLocation> out = new TreeMap<>();
		Map<ResourceLocation, Resource> res = server.getResourceManager().listResources(dir, loc -> loc.getPath().endsWith(".json"));
		for (Map.Entry<ResourceLocation, Resource> e : res.entrySet()) {
			if (!fromTf(e.getValue())) continue;
			packs.add(e.getValue().sourcePackId());
			String path = e.getKey().getPath();
			ResourceLocation id = ResourceLocation.fromNamespaceAndPath(e.getKey().getNamespace(), path.substring(dir.length() + 1, path.length() - 5));
			out.put(id.toString(), id);
		}
		return out;
	}

	static String itemId(Item item) {
		return BuiltInRegistries.ITEM.getKey(item).toString();
	}

	/**
	 * A fake player of the overworld. Never of the Twilight Forest level: making a ServerPlayer
	 * picks a spawn position near (0, 0) of its level and generates the chunks there, which would
	 * leak features into the surface dump.
	 */
	static FakePlayer fakePlayer(Ctx ctx) {
		return FakePlayerFactory.getMinecraft(ctx.overworld());
	}

	// -----------------------------------------------------------------------------------------
	// mobs

	static Object mobs(Ctx ctx) {
		ServerLevel level = ctx.twilight();
		TreeMap<String, EntityType<?>> types = new TreeMap<>();
		for (EntityType<?> t : BuiltInRegistries.ENTITY_TYPE) {
			ResourceLocation id = BuiltInRegistries.ENTITY_TYPE.getKey(t);
			if (id.getNamespace().equals(TF)) types.put(id.toString(), t);
		}
		TreeMap<String, Holder<Attribute>> attributes = new TreeMap<>();
		BuiltInRegistries.ATTRIBUTE.holders().forEach(h -> attributes.put(Ctx.id(h), h));

		Map<String, Object> entities = Json.obj();
		for (Map.Entry<String, EntityType<?>> e : types.entrySet()) {
			EntityType<?> t = e.getValue();
			Map<String, Object> m = Json.obj();
			m.put("category", t.getCategory().getName());
			m.put("width", t.getWidth());
			m.put("height", t.getHeight());
			m.put("fireImmune", t.fireImmune());
			m.put("canSummon", t.canSummon());
			m.put("clientTrackingRange", t.clientTrackingRange());
			m.put("updateInterval", t.updateInterval());
			if (DefaultAttributes.hasSupplier(t)) {
				@SuppressWarnings("unchecked")
				AttributeSupplier sup = DefaultAttributes.getSupplier((EntityType<? extends LivingEntity>) t);
				Map<String, Object> base = Json.obj();
				for (Map.Entry<String, Holder<Attribute>> a : attributes.entrySet()) {
					if (sup.hasAttribute(a.getValue())) base.put(a.getKey(), sup.getBaseValue(a.getValue()));
				}
				m.put("defaultAttributes", base);
			}
			try {
				Entity entity = t.create(level);
				if (entity == null) {
					m.put("created", false);
				} else {
					m.put("created", true);
					m.put("eyeHeight", entity.getEyeHeight());
					m.put("living", entity instanceof LivingEntity);
					if (entity instanceof LivingEntity living) {
						Map<String, Object> values = Json.obj();
						for (Map.Entry<String, Holder<Attribute>> a : attributes.entrySet()) {
							AttributeInstance inst = living.getAttribute(a.getValue());
							if (inst == null) continue;
							Map<String, Object> v = Json.obj();
							v.put("base", inst.getBaseValue());
							v.put("value", inst.getValue());
							values.put(a.getKey(), v);
						}
						m.put("attributes", values);
						m.put("maxHealth", living.getMaxHealth());
						m.put("health", living.getHealth());
						m.put("armor", living.getArmorValue());
						// some rewards are random (animals: 1 + nextInt(3) on the level's random): the range
						// over many draws, which is stable where a single draw is not
						int xpMin = Integer.MAX_VALUE, xpMax = Integer.MIN_VALUE;
						for (int i = 0; i < 256; i++) {
							int xp = living.getExperienceReward(level, null);
							xpMin = Math.min(xpMin, xp);
							xpMax = Math.max(xpMax, xp);
						}
						m.put("experienceMin", xpMin);
						m.put("experienceMax", xpMax);
					}
					entity.discard();
				}
			} catch (Throwable ex) {
				m.put("error", ex.toString());
			}
			entities.put(e.getKey(), m);
		}
		Map<String, Object> out = Json.obj();
		out.put("note", "Every twilightforest entity type: EntityType properties, DefaultAttributes base values, and the attributes (base, value with modifiers), "
			+ "health, armor and experience reward range (min/max of 256 draws) of an instance made with EntityType#create (before finalizeSpawn, so no difficulty or spawn-time changes). Default TF config.");
		out.put("entities", entities);
		return out;
	}

	// -----------------------------------------------------------------------------------------
	// loot

	static Object loot(Ctx ctx) {
		MinecraftServer server = ctx.server;
		ServerLevel level = ctx.twilight();
		int rolls = Ctx.getInt(ctx.section(), "rolls", 2048);
		long seed = Ctx.getLong(ctx.section(), "seed", 20121220L);
		FakePlayer player = fakePlayer(ctx);
		Vec3 origin = new Vec3(8.5, 64.0, 8.5);
		player.setPos(origin);
		TreeSet<String> packs = new TreeSet<>();
		TreeMap<String, ResourceLocation> ids = tfData(server, "loot_table", packs);

		Map<String, Object> tables = Json.obj();
		for (Map.Entry<String, ResourceLocation> e : ids.entrySet()) {
			ResourceLocation id = e.getValue();
			Map<String, Object> m = Json.obj();
			LootTable table = server.reloadableRegistries().getLootTable(ResourceKey.create(Registries.LOOT_TABLE, id));
			LootContextParamSet set = table.getParamSet();
			m.put("paramSet", LootContextParamSets.CODEC.encodeStart(JsonOps.INSTANCE, set).result().map(j -> j.getAsString()).orElse("?"));
			if (table == LootTable.EMPTY) {
				m.put("skipped", "empty table");
				tables.put(e.getKey(), m);
				continue;
			}
			Entity thisEntity = player;
			Block block = null;
			String path = id.getPath();
			if (path.startsWith("entities/")) {
				Optional<EntityType<?>> type = entityFor(id.getNamespace(), path.substring("entities/".length()));
				if (type.isPresent()) {
					Entity made = type.get().create(level);
					if (made != null) {
						made.setPos(origin);
						thisEntity = made;
					}
				}
			} else if (path.startsWith("blocks/")) {
				ResourceLocation bid = ResourceLocation.fromNamespaceAndPath(id.getNamespace(), path.substring("blocks/".length()));
				block = BuiltInRegistries.BLOCK.getOptional(bid).orElse(null);
			}
			LootParams.Builder b = new LootParams.Builder(level);
			List<String> missing = new ArrayList<>();
			for (LootContextParam<?> p : set.getAllowed()) {
				Object v = null;
				if (p == LootContextParams.ORIGIN) v = origin;
				else if (p == LootContextParams.THIS_ENTITY) v = thisEntity;
				else if (p == LootContextParams.LAST_DAMAGE_PLAYER || p == LootContextParams.ATTACKING_ENTITY || p == LootContextParams.DIRECT_ATTACKING_ENTITY) v = player;
				else if (p == LootContextParams.DAMAGE_SOURCE) v = level.damageSources().playerAttack(player);
				else if (p == LootContextParams.BLOCK_STATE && block != null) v = block.defaultBlockState();
				else if (p == LootContextParams.TOOL) v = ItemStack.EMPTY;
				boolean required = set.getRequired().contains(p);
				if (v == null) {
					if (required) missing.add(p.getName().toString());
					continue;
				}
				put(b, p, v, required);
			}
			Map<String, Object> context = Json.obj();
			context.put("thisEntity", set.getAllowed().contains(LootContextParams.THIS_ENTITY) ? BuiltInRegistries.ENTITY_TYPE.getKey(thisEntity.getType()).toString() : null);
			context.put("blockState", block != null ? BuiltInRegistries.BLOCK.getKey(block).toString() : null);
			m.put("context", context);
			if (!missing.isEmpty()) {
				m.put("skipped", "no value for required parameter(s) " + String.join(", ", missing));
				tables.put(e.getKey(), m);
				continue;
			}
			LootParams params = b.create(set);
			RandomSource random = RandomSource.create(seed ^ id.toString().hashCode());
			TreeMap<String, long[]> stats = new TreeMap<>(); // present, total, min, max
			long stacks = 0;
			int empty = 0;
			for (int i = 0; i < rolls; i++) {
				TreeMap<String, Long> roll = new TreeMap<>();
				for (ItemStack st : table.getRandomItems(params, random)) {
					if (st.isEmpty()) continue;
					stacks++;
					roll.merge(itemId(st.getItem()), (long) st.getCount(), Long::sum);
				}
				if (roll.isEmpty()) empty++;
				for (Map.Entry<String, Long> r : roll.entrySet()) {
					long[] s = stats.computeIfAbsent(r.getKey(), k -> new long[]{0, 0, Long.MAX_VALUE, 0});
					s[0]++;
					s[1] += r.getValue();
					s[2] = Math.min(s[2], r.getValue());
					s[3] = Math.max(s[3], r.getValue());
				}
			}
			m.put("emptyRolls", empty);
			m.put("stacks", stacks);
			Map<String, Object> items = Json.obj();
			for (Map.Entry<String, long[]> s : stats.entrySet()) {
				Map<String, Object> it = Json.obj();
				it.put("present", s.getValue()[0]);
				it.put("total", s.getValue()[1]);
				it.put("min", s.getValue()[2]);
				it.put("max", s.getValue()[3]);
				items.put(s.getKey(), it);
			}
			m.put("items", items);
			if (thisEntity != player) thisEntity.discard();
			tables.put(e.getKey(), m);
		}
		Map<String, Object> out = Json.obj();
		out.put("note", "Every loot table of TF's data pack rolled 'rolls' times with LootTable#getRandomItems(params, RandomSource.create(seed ^ id.hashCode())). "
			+ "Context: origin (8.5, 64, 8.5) in twilightforest:twilight_forest; this_entity = the table's entity for entities/<type>[/...] (else a fake player); "
			+ "killer, direct killer and last damage player = a fake player (player attack, no looting); block_state = the default state of blocks/<block>; tool = empty hand. "
			+ "Per item: present = rolls containing it, total = items over all rolls, min/max = per-roll count when present (statistics, not exact rules: compare with a tolerance).");
		out.put("rolls", rolls);
		out.put("seed", seed);
		out.put("packs", new ArrayList<>(packs));
		out.put("tables", tables);
		return out;
	}

	@SuppressWarnings({"unchecked", "rawtypes"})
	private static void put(LootParams.Builder b, LootContextParam p, Object v, boolean required) {
		if (required) b.withParameter(p, v);
		else b.withOptionalParameter(p, v);
	}

	/** entities/<type>[/<variant>] or entities/<type>_<suffix>: the entity type of the table. */
	private static Optional<EntityType<?>> entityFor(String ns, String rest) {
		String name = rest.contains("/") ? rest.substring(0, rest.indexOf('/')) : rest;
		while (true) {
			Optional<EntityType<?>> t = BuiltInRegistries.ENTITY_TYPE.getOptional(ResourceLocation.fromNamespaceAndPath(ns, name));
			if (t.isPresent()) return t;
			int u = name.lastIndexOf('_');
			if (u <= 0) return Optional.empty();
			name = name.substring(0, u);
		}
	}

	// -----------------------------------------------------------------------------------------
	// recipes

	static Object recipes(Ctx ctx) {
		MinecraftServer server = ctx.server;
		TreeSet<String> packs = new TreeSet<>();
		TreeMap<String, ResourceLocation> tfIds = tfData(server, "recipe", packs);
		TreeMap<String, RecipeHolder<?>> holders = new TreeMap<>();
		for (RecipeHolder<?> h : server.getRecipeManager().getRecipes()) {
			String id = h.id().toString();
			if (tfIds.containsKey(id) || h.id().getNamespace().equals(TF)) holders.put(id, h);
		}
		Map<String, Object> recipes = Json.obj();
		for (Map.Entry<String, RecipeHolder<?>> e : holders.entrySet()) {
			Recipe<?> r = e.getValue().value();
			Map<String, Object> m = Json.obj();
			m.put("type", String.valueOf(BuiltInRegistries.RECIPE_TYPE.getKey(r.getType())));
			m.put("serializer", String.valueOf(BuiltInRegistries.RECIPE_SERIALIZER.getKey(r.getSerializer())));
			ItemStack res = r.getResultItem(server.registryAccess());
			m.put("result", res.isEmpty() ? null : itemId(res.getItem()));
			m.put("count", res.getCount());
			if (r instanceof ShapedRecipe s) {
				m.put("width", s.getWidth());
				m.put("height", s.getHeight());
			}
			List<Object> ings = Json.arr();
			for (Ingredient ing : r.getIngredients()) ings.add(ingredient(ing));
			m.put("ingredients", ings);
			recipes.put(e.getKey(), m);
		}
		List<Object> notLoaded = Json.arr();
		for (String id : tfIds.keySet()) if (!holders.containsKey(id)) notLoaded.add(id);
		Map<String, Object> out = Json.obj();
		out.put("note", "Every recipe of TF's data pack (any namespace) and every twilightforest:* recipe: type, serializer, getResultItem (id, count), "
			+ "width/height of shaped recipes and getIngredients() (each the item ids of Ingredient#getItems in order, tags expanded; [] = empty slot). "
			+ "notLoaded lists recipe files of TF's data pack the recipe manager did not load (their load conditions failed, e.g. compat recipes of absent mods).");
		out.put("packs", new ArrayList<>(packs));
		out.put("notLoaded", notLoaded);
		out.put("recipes", recipes);
		return out;
	}

	static List<Object> ingredient(Ingredient ing) {
		LinkedHashSet<String> ids = new LinkedHashSet<>();
		for (ItemStack st : ing.getItems()) if (!st.isEmpty()) ids.add(itemId(st.getItem()));
		return new ArrayList<>(ids);
	}

	// -----------------------------------------------------------------------------------------
	// uncrafting (TF's UncraftingMenu itself, for every item)

	static Object uncrafting(Ctx ctx) {
		MinecraftServer server = ctx.server;
		ServerLevel level = ctx.twilight();
		FakePlayer player = fakePlayer(ctx);
		IdentityHashMap<Recipe<?>, String> recipeIds = new IdentityHashMap<>();
		for (RecipeHolder<?> h : server.getRecipeManager().getRecipes()) recipeIds.put(h.value(), h.id().toString());
		TreeMap<String, Item> items = new TreeMap<>();
		for (Item item : BuiltInRegistries.ITEM) if (item != Items.AIR) items.put(itemId(item), item);

		Map<String, Object> results = Json.obj();
		for (Map.Entry<String, Item> e : items.entrySet()) {
			UncraftingMenu menu = new UncraftingMenu(0, player.getInventory(), level, ContainerLevelAccess.NULL);
			menu.tinkerInput.setItem(0, new ItemStack(e.getValue(), 64));
			List<Object> options = Json.arr();
			Recipe<?> first = null;
			for (int cycle = 0; cycle < 64; cycle++) {
				menu.unrecipeInCycle = cycle;
				menu.slotsChanged(menu.tinkerInput);
				Recipe<?> rec = menu.storedGhostRecipe;
				if (rec == null || (cycle > 0 && rec == first)) break;
				if (cycle == 0) first = rec;
				UncraftingContainer grid = (UncraftingContainer) menu.getSlot(2).container;
				List<Object> cells = Json.arr();
				for (int i = 0; i < 9; i++) {
					ItemStack st = menu.getSlot(2 + i).getItem();
					cells.add(st.isEmpty() ? "" : (UncraftingMenu.isMarked(st) ? "!" : "") + itemId(st.getItem()));
				}
				Map<String, Object> m = Json.obj();
				m.put("recipe", recipeIds.getOrDefault(rec, "?"));
				m.put("inputCount", grid.numberOfInputItems);
				m.put("cost", menu.getUncraftingCost());
				m.put("grid", cells);
				options.add(m);
			}
			menu.tinkerInput.setItem(0, ItemStack.EMPTY);
			if (!options.isEmpty()) results.put(e.getKey(), options);
		}
		Map<String, Object> config = Json.obj();
		config.put("uncraftingXpCostMultiplier", TFConfig.uncraftingXpCostMultiplier);
		config.put("allowShapelessUncrafting", TFConfig.allowShapelessUncrafting);
		config.put("disableUncraftingOnly", TFConfig.disableUncraftingOnly);
		config.put("flipUncraftingModIdList", TFConfig.flipUncraftingModIdList);
		config.put("disableUncraftingRecipes", new ArrayList<>(TFConfig.disableUncraftingRecipes));
		config.put("blacklistedUncraftingModIds", new ArrayList<>(TFConfig.blacklistedUncraftingModIds));
		Map<String, Object> out = Json.obj();
		out.put("note", "For every item, a stack of 64 put into TF's UncraftingMenu (default config), cycling unrecipeInCycle through every recipe it offers "
			+ "(ingredientsInCycle 0): the recipe, inputCount (items consumed), cost (XP levels, getUncraftingCost) and the 3x3 grid row by row "
			+ "(\"\" empty, \"!\" prefix = marked: damaged or problematic, cannot be taken). Items without options are omitted.");
		out.put("config", config);
		out.put("items", results);
		return out;
	}
}
