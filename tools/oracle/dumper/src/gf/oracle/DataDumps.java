// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Java oracle dumper (unit F13): original code, LGPL-3.0-or-later (see COPYING.LESSER).
package gf.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.mojang.serialization.DataResult;
import com.mojang.serialization.JsonOps;
import com.mojang.serialization.MapCodec;
import net.minecraft.core.Holder;
import net.minecraft.core.HolderLookup;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.core.registries.Registries;
import net.minecraft.resources.RegistryOps;
import net.minecraft.resources.ResourceKey;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.packs.resources.Resource;
import net.minecraft.util.RandomSource;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.EntityType;
import net.minecraft.world.entity.EquipmentSlot;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.ai.attributes.Attribute;
import net.minecraft.world.entity.ai.attributes.AttributeInstance;
import net.minecraft.world.entity.ai.attributes.AttributeSupplier;
import net.minecraft.world.entity.ai.attributes.DefaultAttributes;
import net.minecraft.world.inventory.ContainerLevelAccess;
import net.minecraft.world.item.Item;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.item.crafting.AbstractCookingRecipe;
import net.minecraft.world.item.crafting.CraftingInput;
import net.minecraft.world.item.crafting.CraftingRecipe;
import net.minecraft.world.item.crafting.Ingredient;
import net.minecraft.world.item.crafting.Recipe;
import net.minecraft.world.item.crafting.RecipeHolder;
import net.minecraft.world.item.crafting.ShapedRecipe;
import net.minecraft.world.item.crafting.SmithingRecipe;
import net.minecraft.world.item.crafting.SmithingRecipeInput;
import net.minecraft.world.item.enchantment.Enchantment;
import net.minecraft.world.item.enchantment.Enchantments;
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
import twilightforest.data.tags.ItemTagGenerator;
import twilightforest.inventory.UncraftingContainer;
import twilightforest.inventory.UncraftingMenu;
import twilightforest.item.recipe.travellers.TravellersGearModifierRecipe;

import java.util.ArrayList;
import java.util.IdentityHashMap;
import java.util.LinkedHashMap;
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

	/** A new stack of the item with one enchantment. */
	static ItemStack enchanted(Item item, HolderLookup.RegistryLookup<Enchantment> enchantments, ResourceKey<Enchantment> key, int level) {
		ItemStack stack = new ItemStack(item);
		stack.enchant(enchantments.getOrThrow(key), level);
		return stack;
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
			Entity entity = null;
			try {
				entity = t.create(level);
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
				}
			} catch (Throwable ex) {
				m.put("error", ex.toString());
			} finally {
				if (entity != null) entity.discard();
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
		player.setItemSlot(EquipmentSlot.MAINHAND, ItemStack.EMPTY);
		RegistryOps<JsonElement> ops = Stacks.ops(server);
		HolderLookup.RegistryLookup<Enchantment> enchantments = server.registryAccess().lookupOrThrow(Registries.ENCHANTMENT);
		// the context variants: the tool that breaks a block, the weapon that kills an entity
		Map<String, ItemStack> blockTools = new LinkedHashMap<>();
		blockTools.put("shears", new ItemStack(Items.SHEARS));
		blockTools.put("silk_touch", enchanted(Items.DIAMOND_PICKAXE, enchantments, Enchantments.SILK_TOUCH, 1));
		blockTools.put("fortune_3", enchanted(Items.DIAMOND_PICKAXE, enchantments, Enchantments.FORTUNE, 3));
		Map<String, ItemStack> entityWeapons = new LinkedHashMap<>();
		entityWeapons.put("looting_3", enchanted(Items.DIAMOND_SWORD, enchantments, Enchantments.LOOTING, 3));
		TreeSet<String> packs = new TreeSet<>();
		TreeMap<String, ResourceLocation> ids = tfData(server, "loot_table", packs);

		Map<String, Object> tables = Json.obj();
		for (Map.Entry<String, ResourceLocation> e : ids.entrySet()) {
			ResourceLocation id = e.getValue();
			Map<String, Object> m = Json.obj();
			tables.put(e.getKey(), m);
			LootTable table = server.reloadableRegistries().getLootTable(ResourceKey.create(Registries.LOOT_TABLE, id));
			LootContextParamSet set = table.getParamSet();
			m.put("paramSet", LootContextParamSets.CODEC.encodeStart(JsonOps.INSTANCE, set).result().map(j -> j.getAsString()).orElse("?"));
			if (table == LootTable.EMPTY) {
				m.put("skipped", "empty table");
				continue;
			}
			Entity made = null;
			try {
				Entity thisEntity = player;
				Block block = null;
				String path = id.getPath();
				boolean blockTable = path.startsWith("blocks/");
				Map<String, ItemStack> variants = Map.of();
				if (path.startsWith("entities/")) {
					variants = entityWeapons;
					Optional<EntityType<?>> type = entityFor(id.getNamespace(), path.substring("entities/".length()));
					if (type.isPresent()) {
						made = type.get().create(level);
						if (made != null) {
							made.setPos(origin);
							thisEntity = made;
						}
					}
				} else if (blockTable) {
					variants = blockTools;
					ResourceLocation bid = ResourceLocation.fromNamespaceAndPath(id.getNamespace(), path.substring("blocks/".length()));
					block = BuiltInRegistries.BLOCK.getOptional(bid).orElse(null);
				}
				Map<String, Object> context = Json.obj();
				context.put("thisEntity", set.getAllowed().contains(LootContextParams.THIS_ENTITY) ? BuiltInRegistries.ENTITY_TYPE.getKey(thisEntity.getType()).toString() : null);
				context.put("blockState", block != null ? BuiltInRegistries.BLOCK.getKey(block).toString() : null);
				m.put("context", context);
				List<String> missing = new ArrayList<>();
				LootParams params = lootParams(level, set, origin, thisEntity, player, block, ItemStack.EMPTY, missing);
				if (!missing.isEmpty()) {
					m.put("skipped", "no value for required parameter(s) " + String.join(", ", missing));
					continue;
				}
				long tableSeed = seed ^ id.toString().hashCode();
				Map<String, Object> stats = roll(table, params, rolls, tableSeed);
				m.putAll(stats);
				if (variants.isEmpty()) continue;
				String plain = Json.toString(stats);
				Map<String, Object> different = Json.obj();
				List<Object> same = Json.arr();
				for (Map.Entry<String, ItemStack> v : variants.entrySet()) {
					// the player holds the tool or weapon: tool = the stack for blocks, killer's main hand for entities
					player.setItemSlot(EquipmentSlot.MAINHAND, v.getValue().copy());
					ItemStack tool = blockTable ? v.getValue().copy() : ItemStack.EMPTY;
					LootParams vp = lootParams(level, set, origin, thisEntity, player, block, tool, new ArrayList<>());
					Map<String, Object> vs = roll(table, vp, rolls, tableSeed);
					player.setItemSlot(EquipmentSlot.MAINHAND, ItemStack.EMPTY);
					if (Json.toString(vs).equals(plain)) same.add(v.getKey());
					else different.put(v.getKey(), vs);
				}
				m.put("variants", different);
				m.put("variantsSameAsDefault", same);
			} finally {
				if (made != null) made.discard();
				player.setItemSlot(EquipmentSlot.MAINHAND, ItemStack.EMPTY);
			}
		}
		Map<String, Object> out = Json.obj();
		out.put("note", "Every loot table of TF's data pack rolled 'rolls' times with LootTable#getRandomItems(params, RandomSource.create(seed ^ id.hashCode())). "
			+ "Default context: origin (8.5, 64, 8.5) in twilightforest:twilight_forest; this_entity = the table's entity for entities/<type>[/...] (else a fake player); "
			+ "killer, direct killer and last damage player = a fake player (player attack) with an empty hand; block_state = the default state of blocks/<block>; tool = empty hand. "
			+ "Per item: present = rolls containing it, total = items over all rolls, min/max = per-roll count when present (statistics, not exact rules: compare with a tolerance). "
			+ "variants: the same rolls (same seed) with the fake player holding variantStacks[name] - blocks/*: shears, a silk touch pickaxe, a fortune III pickaxe "
			+ "(also passed as the tool); entities/*: a looting III sword (the killer's weapon). A variant whose statistics equal the default is listed in variantsSameAsDefault instead.");
		out.put("rolls", rolls);
		out.put("seed", seed);
		out.put("packs", new ArrayList<>(packs));
		Map<String, Object> held = Json.obj();
		for (Map.Entry<String, ItemStack> v : blockTools.entrySet()) held.put(v.getKey(), new Json.Inline(Stacks.encode(ops, v.getValue())));
		for (Map.Entry<String, ItemStack> v : entityWeapons.entrySet()) held.put(v.getKey(), new Json.Inline(Stacks.encode(ops, v.getValue())));
		out.put("variantStacks", held);
		out.put("tables", tables);
		return out;
	}

	/** The loot parameters of a table's param set; required parameters without a value go to missing. */
	private static LootParams lootParams(ServerLevel level, LootContextParamSet set, Vec3 origin, Entity thisEntity, FakePlayer player, Block block, ItemStack tool, List<String> missing) {
		LootParams.Builder b = new LootParams.Builder(level);
		for (LootContextParam<?> p : set.getAllowed()) {
			Object v = null;
			if (p == LootContextParams.ORIGIN) v = origin;
			else if (p == LootContextParams.THIS_ENTITY) v = thisEntity;
			else if (p == LootContextParams.LAST_DAMAGE_PLAYER || p == LootContextParams.ATTACKING_ENTITY || p == LootContextParams.DIRECT_ATTACKING_ENTITY) v = player;
			else if (p == LootContextParams.DAMAGE_SOURCE) v = level.damageSources().playerAttack(player);
			else if (p == LootContextParams.BLOCK_STATE && block != null) v = block.defaultBlockState();
			else if (p == LootContextParams.TOOL) v = tool;
			boolean required = set.getRequired().contains(p);
			if (v == null) {
				if (required) missing.add(p.getName().toString());
				continue;
			}
			put(b, p, v, required);
		}
		return missing.isEmpty() ? b.create(set) : null;
	}

	/** Rolls a table: emptyRolls, stacks and per-item statistics. */
	private static Map<String, Object> roll(LootTable table, LootParams params, int rolls, long seed) {
		RandomSource random = RandomSource.create(seed);
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
		Map<String, Object> m = Json.obj();
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
		return m;
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
		RegistryOps<JsonElement> ops = Stacks.ops(server);
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
			m.put("special", r.isSpecial());
			if (r instanceof ShapedRecipe s) {
				m.put("width", s.getWidth());
				m.put("height", s.getHeight());
			}
			if (r instanceof AbstractCookingRecipe c) {
				m.put("cookingTime", c.getCookingTime());
				m.put("experience", c.getExperience());
			}
			List<Object> ings = Json.arr();
			for (Ingredient ing : r.getIngredients()) ings.add(ingredient(ing));
			m.put("ingredients", ings);
			m.put("data", recipeData(ops, r));
			// what a recipe without a fixed result makes: its ingredients (the first item of each) crafted
			if (r instanceof TravellersGearModifierRecipe t && res.isEmpty()) {
				List<ItemStack> grid = new ArrayList<>();
				for (Ingredient ing : r.getIngredients()) {
					ItemStack[] items = ing.getItems();
					grid.add(items.length > 0 ? items[0].copy() : ItemStack.EMPTY);
				}
				while (grid.size() < t.getWidth() * t.getHeight()) grid.add(ItemStack.EMPTY);
				m.put("probe", new Json.Inline(probe(ctx, ops, r, t.getWidth(), t.getHeight(), grid)));
			}
			recipes.put(e.getKey(), m);
		}
		// the recipes whose result is code: crafted from the inputs of spec.json "recipes.probes"
		List<Object> probes = Json.arr();
		for (JsonElement pe : Ctx.getArr(ctx.section(), "probes")) {
			JsonObject p = pe.getAsJsonObject();
			String id = Ctx.getString(p, "recipe", "");
			RecipeHolder<?> h = holders.get(id);
			if (h == null) throw new IllegalArgumentException("spec.json recipes.probes: unknown recipe " + id);
			int w = Ctx.getInt(p, "width", 3), hgt = Ctx.getInt(p, "height", 3);
			JsonArray cells = Ctx.getArr(p, "grid");
			if (cells.size() != w * hgt) throw new IllegalArgumentException("spec.json recipes.probes " + id + ": grid has " + cells.size() + " cells, not " + w + "x" + hgt);
			List<ItemStack> grid = new ArrayList<>();
			for (JsonElement c : cells) grid.add(Stacks.parse(ops, c));
			Map<String, Object> m = Json.obj();
			m.put("recipe", id);
			m.put("why", Ctx.getString(p, "why", ""));
			m.putAll(probe(ctx, ops, h.value(), w, hgt, grid));
			probes.add(new Json.Inline(m));
		}
		List<Object> notLoaded = Json.arr();
		for (String id : tfIds.keySet()) if (!holders.containsKey(id)) notLoaded.add(id);
		Map<String, Object> out = Json.obj();
		out.put("note", "Every recipe of TF's data pack (any namespace) and every twilightforest:* recipe: type, serializer, getResultItem (id, count), isSpecial, "
			+ "width/height of shaped recipes, cookingTime/experience of cooking recipes, getIngredients() (each the item ids of Ingredient#getItems in order, tags expanded; [] = empty slot) "
			+ "and data: the recipe encoded with its serializer's codec (the recipe JSON as TF loaded it: drying time, modifier keys, scepter repair durability, ...; object keys sorted). "
			+ "probe (travellers' gear modifier recipes, whose result depends on the input): the first item of each ingredient crafted with the recipe. "
			+ "probes: recipes whose result is code, crafted from the inputs of tools/oracle/spec.json: matches, and when it matches the assembled result and the remaining items. "
			+ "Stacks: \"\" = empty, an item id = one item without components, else ItemStack's codec {count, components (the patch over the item's defaults), id}. "
			+ "notLoaded lists recipe files of TF's data pack the recipe manager did not load (their load conditions failed, e.g. compat recipes of absent mods).");
		out.put("packs", new ArrayList<>(packs));
		out.put("notLoaded", notLoaded);
		out.put("recipes", recipes);
		out.put("probes", probes);
		return out;
	}

	static List<Object> ingredient(Ingredient ing) {
		LinkedHashSet<String> ids = new LinkedHashSet<>();
		for (ItemStack st : ing.getItems()) if (!st.isEmpty()) ids.add(itemId(st.getItem()));
		return new ArrayList<>(ids);
	}

	/** The recipe encoded with its serializer's codec (one line), or the codec's error. */
	@SuppressWarnings({"unchecked", "rawtypes"})
	static Object recipeData(RegistryOps<JsonElement> ops, Recipe<?> r) {
		MapCodec codec = r.getSerializer().codec();
		DataResult<JsonElement> res = codec.codec().encodeStart(ops, r);
		if (res.result().isPresent()) return new Json.Inline(Json.fromGson(res.result().get()));
		return "codec error: " + res.error().map(DataResult.Error::message).orElse("?");
	}

	/** Crafts a grid (crafting: w x h; smithing: template, base, addition) with one recipe. */
	static Map<String, Object> probe(Ctx ctx, RegistryOps<JsonElement> ops, Recipe<?> r, int w, int h, List<ItemStack> grid) {
		ServerLevel level = ctx.twilight();
		Map<String, Object> m = Json.obj();
		m.put("width", w);
		m.put("height", h);
		List<Object> in = Json.arr();
		for (ItemStack st : grid) in.add(Stacks.encode(ops, st));
		m.put("input", in);
		boolean matches;
		ItemStack result = ItemStack.EMPTY;
		List<ItemStack> remaining = List.of();
		if (r instanceof CraftingRecipe cr) {
			CraftingInput input = CraftingInput.of(w, h, grid);
			matches = cr.matches(input, level);
			if (matches) {
				result = cr.assemble(input, level.registryAccess());
				remaining = cr.getRemainingItems(input);
			}
		} else if (r instanceof SmithingRecipe sr) {
			if (grid.size() != 3) throw new IllegalArgumentException("a smithing probe has 3 cells (template, base, addition)");
			SmithingRecipeInput input = new SmithingRecipeInput(grid.get(0), grid.get(1), grid.get(2));
			matches = sr.matches(input, level);
			if (matches) {
				result = sr.assemble(input, level.registryAccess());
				remaining = sr.getRemainingItems(input);
			}
		} else {
			throw new IllegalArgumentException("cannot probe a " + BuiltInRegistries.RECIPE_TYPE.getKey(r.getType()) + " recipe");
		}
		m.put("matches", matches);
		if (matches) {
			m.put("result", Stacks.encode(ops, result));
			List<Object> rem = Json.arr();
			for (ItemStack st : remaining) rem.add(Stacks.encode(ops, st));
			m.put("remaining", rem);
		}
		return m;
	}

	// -----------------------------------------------------------------------------------------
	// uncrafting (TF's UncraftingMenu itself)

	static Object uncrafting(Ctx ctx) {
		MinecraftServer server = ctx.server;
		ServerLevel level = ctx.twilight();
		FakePlayer player = fakePlayer(ctx);
		RegistryOps<JsonElement> ops = Stacks.ops(server);
		JsonObject s = ctx.section();
		int maxCycles = Ctx.getInt(s, "maxCycles", 64);
		IdentityHashMap<Recipe<?>, String> recipeIds = new IdentityHashMap<>();
		for (RecipeHolder<?> h : server.getRecipeManager().getRecipes()) recipeIds.put(h.value(), h.id().toString());
		TreeMap<String, Item> items = new TreeMap<>();
		for (Item item : BuiltInRegistries.ITEM) if (item != Items.AIR) items.put(itemId(item), item);
		TreeSet<String> cycleExamples = new TreeSet<>();
		for (JsonElement e : Ctx.getArr(s, "ingredientCycleItems")) cycleExamples.add(e.getAsString());

		Map<String, Object> results = Json.obj();
		List<Object> truncated = Json.arr();
		Map<String, Object> damaged = Json.obj();
		Map<String, Object> cycles = Json.obj();
		for (Map.Entry<String, Item> e : items.entrySet()) {
			String id = e.getKey();
			Item item = e.getValue();
			UncraftingMenu menu = new UncraftingMenu(0, player.getInventory(), level, ContainerLevelAccess.NULL);
			// every option (unrecipeInCycle) of a stack of 64
			List<Recipe<?>> recipes = new ArrayList<>();
			List<Object> options = Json.arr();
			boolean cut = optionsOf(menu, new ItemStack(item, 64), maxCycles, recipeIds, recipes, options, null);
			if (options.isEmpty()) continue;
			results.put(id, options);
			if (cut) truncated.add(id);

			// ingredient switching (ingredientsInCycle) for TF's items and the examples of spec.json
			if (id.startsWith(TF + ":") || cycleExamples.contains(id)) {
				List<Object> per = Json.arr();
				for (int u = 0; u < recipes.size(); u++) {
					int alternatives = alternatives(recipes.get(u));
					if (alternatives <= 1) continue;
					List<Object> grids = Json.arr();
					for (int c = 0; c < Math.min(alternatives, maxCycles); c++) {
						menu.unrecipeInCycle = u;
						menu.ingredientsInCycle = c;
						menu.slotsChanged(menu.tinkerInput);
						Map<String, Object> g = Json.obj();
						g.put("ingredientsInCycle", c);
						g.put("cost", menu.getUncraftingCost());
						g.put("grid", grid(menu));
						grids.add(g);
					}
					menu.ingredientsInCycle = 0;
					Map<String, Object> m = Json.obj();
					m.put("unrecipeInCycle", u);
					m.put("recipe", recipeIds.getOrDefault(recipes.get(u), "?"));
					m.put("alternatives", alternatives);
					m.put("truncated", alternatives > maxCycles);
					m.put("cycles", grids);
					per.add(m);
				}
				if (!per.isEmpty()) cycles.put(id, per);
			}

			// damaged inputs: the marked (damaged) parts and the cost
			ItemStack probe = new ItemStack(item);
			if (probe.isDamageableItem()) {
				int max = probe.getMaxDamage();
				TreeSet<Integer> values = new TreeSet<>(List.of(max / 4, max / 2, max - 1));
				values.removeIf(d -> d <= 0);
				List<Object> list = Json.arr();
				for (int d : values) {
					ItemStack st = new ItemStack(item);
					st.setDamageValue(d);
					Map<String, Object> head = Json.obj();
					head.put("damage", d);
					head.put("maxDamage", max);
					optionsOf(menu, st, maxCycles, recipeIds, new ArrayList<>(), list, head);
				}
				if (!list.isEmpty()) damaged.put(id, list);
			}
			menu.tinkerInput.setItem(0, ItemStack.EMPTY);
		}

		// repairing and recrafting through the assembly matrix (spec.json "uncrafting.recraft")
		List<Object> recraft = Json.arr();
		for (JsonElement ce : Ctx.getArr(s, "recraft")) {
			JsonObject c = ce.getAsJsonObject();
			UncraftingMenu menu = new UncraftingMenu(0, player.getInventory(), level, ContainerLevelAccess.NULL);
			ItemStack input = Stacks.parse(ops, c.get("input"));
			menu.unrecipeInCycle = Ctx.getInt(c, "unrecipeInCycle", 0);
			menu.tinkerInput.setItem(0, input.copy());
			menu.slotsChanged(menu.tinkerInput);
			Map<String, Object> m = Json.obj();
			m.put("why", Ctx.getString(c, "why", ""));
			m.put("input", new Json.Inline(Stacks.encode(ops, input)));
			m.put("unrecipeInCycle", menu.unrecipeInCycle);
			m.put("recipe", menu.storedGhostRecipe == null ? null : recipeIds.getOrDefault(menu.storedGhostRecipe, "?"));
			m.put("grid", grid(menu));
			m.put("uncraftingCost", menu.getUncraftingCost());
			JsonArray cells = Ctx.getArr(c, "assembly");
			if (cells.size() != 9) throw new IllegalArgumentException("spec.json uncrafting.recraft: assembly has " + cells.size() + " cells, not 9");
			List<Object> assembly = Json.arr();
			for (int i = 0; i < 9; i++) {
				ItemStack a = Stacks.parse(ops, cells.get(i));
				assembly.add(Stacks.encode(ops, a));
				if (!a.isEmpty()) menu.assemblyMatrix.setItem(i, a);
			}
			menu.slotsChanged(menu.assemblyMatrix);
			m.put("assembly", new Json.Inline(assembly));
			m.put("result", new Json.Inline(Stacks.encode(ops, menu.getSlot(1).getItem())));
			m.put("recraftingCost", menu.getRecraftingCost());
			m.put("uncraftingCostAfter", menu.getUncraftingCost());
			recraft.add(m);
			menu.assemblyMatrix.clearContent();
			menu.tinkerInput.setItem(0, ItemStack.EMPTY);
		}

		Map<String, Object> config = Json.obj();
		config.put("uncraftingXpCostMultiplier", TFConfig.uncraftingXpCostMultiplier);
		config.put("repairingXpCostMultiplier", TFConfig.repairingXpCostMultiplier);
		config.put("allowShapelessUncrafting", TFConfig.allowShapelessUncrafting);
		config.put("disableIngredientSwitching", TFConfig.disableIngredientSwitching);
		config.put("disableUncraftingRecipes", new ArrayList<>(TFConfig.disableUncraftingRecipes));
		config.put("reverseRecipeBlacklist", TFConfig.reverseRecipeBlacklist);
		config.put("blacklistedUncraftingModIds", new ArrayList<>(TFConfig.blacklistedUncraftingModIds));
		config.put("flipUncraftingModIdList", TFConfig.flipUncraftingModIdList);
		config.put("disableUncraftingOnly", TFConfig.disableUncraftingOnly);
		config.put("disableEntireTable", TFConfig.disableEntireTable);
		Map<String, Object> out = Json.obj();
		out.put("note", "TF's UncraftingMenu itself (default config, every UNCRAFTING_STUFFS value in config). items: for every item, a stack of 64 in the input slot, "
			+ "unrecipeInCycle cycled through every recipe it offers (ingredientsInCycle 0): the recipe, inputCount (items consumed), cost (XP levels, getUncraftingCost) and the "
			+ "3x3 uncrafting grid row by row (\"\" empty, \"!\" prefix = marked: damaged or problematic, cannot be taken); items without options are omitted; truncated lists the "
			+ "items whose options did not wrap around within maxCycles. ingredientCycles: for TF's items and ingredientCycleItems, every option whose ingredients have more than one "
			+ "alternative (after twilightforest:banned_uncrafting_ingredients), ingredientsInCycle 0 .. alternatives-1 (at most maxCycles; truncated otherwise). damaged: every "
			+ "damageable item with options, one item damaged by maxDamage/4, /2 and maxDamage-1, every option. recraft: the cases of tools/oracle/spec.json: the input, its uncrafting "
			+ "grid and cost, the items put into the assembly matrix, the result slot (tinkerResult), getRecraftingCost and getUncraftingCost after. "
			+ "Stacks: \"\" = empty, an item id = one item without components, else ItemStack's codec {count, components, id}.");
		out.put("config", config);
		out.put("maxCycles", maxCycles);
		out.put("items", results);
		out.put("truncated", truncated);
		out.put("ingredientCycles", cycles);
		out.put("damaged", damaged);
		out.put("recraft", recraft);
		return out;
	}

	/**
	 * Puts the stack into the menu and cycles unrecipeInCycle through its options, adding one record
	 * per option to options (prefixed with head when given) and its recipe to recipes. Returns true
	 * when maxCycles options went by without wrapping around (truncated).
	 */
	private static boolean optionsOf(UncraftingMenu menu, ItemStack stack, int maxCycles, IdentityHashMap<Recipe<?>, String> recipeIds,
									 List<Recipe<?>> recipes, List<Object> options, Map<String, Object> head) {
		menu.ingredientsInCycle = 0;
		menu.unrecipeInCycle = 0;
		menu.tinkerInput.setItem(0, stack);
		for (int cycle = 0; cycle < maxCycles; cycle++) {
			menu.unrecipeInCycle = cycle;
			menu.slotsChanged(menu.tinkerInput);
			Recipe<?> rec = menu.storedGhostRecipe;
			if (rec == null || (cycle > 0 && rec == recipes.get(0))) return false;
			recipes.add(rec);
			UncraftingContainer grid = (UncraftingContainer) menu.getSlot(2).container;
			Map<String, Object> m = Json.obj();
			if (head != null) m.putAll(head);
			m.put("recipe", recipeIds.getOrDefault(rec, "?"));
			m.put("inputCount", grid.numberOfInputItems);
			m.put("cost", menu.getUncraftingCost());
			m.put("grid", grid(menu));
			options.add(m);
		}
		return true;
	}

	/** The uncrafting grid (menu slots 2..10) row by row: "" empty, "!" prefix = marked. */
	private static List<Object> grid(UncraftingMenu menu) {
		List<Object> cells = Json.arr();
		for (int i = 0; i < 9; i++) {
			ItemStack st = menu.getSlot(2 + i).getItem();
			cells.add(st.isEmpty() ? "" : (UncraftingMenu.isMarked(st) ? "!" : "") + itemId(st.getItem()));
		}
		return cells;
	}

	/** How many ingredient switches an option has: UncraftingMenu#getIngredients' longest filtered ingredient. */
	private static int alternatives(Recipe<?> rec) {
		int max = 1;
		for (Ingredient ing : rec.getIngredients()) {
			int n = 0;
			for (ItemStack st : ing.getItems()) if (!st.is(ItemTagGenerator.BANNED_UNCRAFTING_INGREDIENTS)) n++;
			max = Math.max(max, n);
		}
		return max;
	}
}
