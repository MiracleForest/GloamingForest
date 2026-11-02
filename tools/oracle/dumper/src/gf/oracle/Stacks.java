// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Java oracle dumper (unit F13): original code, LGPL-3.0-or-later (see COPYING.LESSER).
package gf.oracle;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import com.mojang.serialization.JsonOps;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.resources.RegistryOps;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.server.MinecraftServer;
import net.minecraft.world.item.Item;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;

/**
 * Item stacks in the goldens and in spec.json. A stack is written as "" (empty), as its item id
 * (one item without components), or as ItemStack.CODEC's JSON {id, count, components} with sorted
 * keys (components are the patch on top of the item's defaults, e.g. minecraft:damage).
 */
final class Stacks {
	private Stacks() {
	}

	static RegistryOps<JsonElement> ops(MinecraftServer server) {
		return server.registryAccess().createSerializationContext(JsonOps.INSTANCE);
	}

	static Object encode(RegistryOps<JsonElement> ops, ItemStack stack) {
		if (stack.isEmpty()) return "";
		if (stack.getCount() == 1 && stack.getComponentsPatch().isEmpty()) return BuiltInRegistries.ITEM.getKey(stack.getItem()).toString();
		return Json.fromGson(ItemStack.CODEC.encodeStart(ops, stack).getOrThrow(IllegalStateException::new));
	}

	/** A stack of spec.json: null or "" (empty), an item id, or {id, count?, components?}. */
	static ItemStack parse(RegistryOps<JsonElement> ops, JsonElement spec) {
		if (spec == null || spec.isJsonNull()) return ItemStack.EMPTY;
		if (spec.isJsonPrimitive()) {
			String id = spec.getAsString();
			if (id.isEmpty()) return ItemStack.EMPTY;
			Item item = BuiltInRegistries.ITEM.getOptional(ResourceLocation.parse(id))
				.orElseThrow(() -> new IllegalArgumentException("unknown item " + id));
			if (item == Items.AIR) throw new IllegalArgumentException("air is no stack: " + id);
			return new ItemStack(item);
		}
		JsonObject o = spec.getAsJsonObject().deepCopy();
		if (!o.has("count")) o.add("count", new JsonPrimitive(1));
		return ItemStack.CODEC.parse(ops, o).getOrThrow(msg -> new IllegalArgumentException("bad stack " + spec + ": " + msg));
	}
}
