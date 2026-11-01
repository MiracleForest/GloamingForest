// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Java oracle dumper (unit F13): original code, LGPL-3.0-or-later (see COPYING.LESSER).
package gf.oracle;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import net.minecraft.core.Holder;
import net.minecraft.core.registries.Registries;
import net.minecraft.resources.ResourceKey;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.level.Level;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;
import java.util.Objects;

/** One dump call: the server, the run spec (gf-oracle-spec.json next to the server) and helpers. */
public final class Ctx {
	public static final String SPEC_FILE = "gf-oracle-spec.json";
	public static final ResourceKey<Level> TWILIGHT = ResourceKey.create(Registries.DIMENSION,
		ResourceLocation.fromNamespaceAndPath("twilightforest", "twilight_forest"));

	public final MinecraftServer server;
	public final String kind;
	public final JsonObject spec;

	Ctx(MinecraftServer server, String kind) throws IOException {
		this.server = server;
		this.kind = kind;
		Path p = Path.of(SPEC_FILE);
		this.spec = Files.exists(p) ? JsonParser.parseString(Files.readString(p, StandardCharsets.UTF_8)).getAsJsonObject() : new JsonObject();
	}

	public long seed() {
		return this.server.getWorldData().worldGenOptions().seed();
	}

	public ServerLevel twilight() {
		return Objects.requireNonNull(this.server.getLevel(TWILIGHT), "the twilightforest:twilight_forest level is not loaded");
	}

	public ServerLevel overworld() {
		return this.server.overworld();
	}

	/** The spec section of this dump ({} when absent). */
	public JsonObject section() {
		JsonElement e = this.spec.get(this.kind);
		return e != null && e.isJsonObject() ? e.getAsJsonObject() : new JsonObject();
	}

	public static int getInt(JsonObject o, String key, int def) {
		JsonElement e = o.get(key);
		return e != null && e.isJsonPrimitive() ? e.getAsInt() : def;
	}

	public static long getLong(JsonObject o, String key, long def) {
		JsonElement e = o.get(key);
		return e != null && e.isJsonPrimitive() ? e.getAsLong() : def;
	}

	public static JsonObject getObj(JsonObject o, String key) {
		JsonElement e = o.get(key);
		return e != null && e.isJsonObject() ? e.getAsJsonObject() : new JsonObject();
	}

	/** Whether the dump depends on the world seed (it is then recorded in the header). */
	public boolean seedDependent() {
		return switch (this.kind) {
			case "biomes", "heights", "surface", "density", "landmarks" -> true;
			default -> false;
		};
	}

	/** The common header: format, kind, the pinned sources, the seed of seed-dependent dumps. */
	Map<String, Object> wrap(Object body) {
		Map<String, Object> out = Json.obj();
		out.put("format", 1);
		out.put("kind", this.kind);
		Map<String, Object> source = Json.obj();
		JsonObject s = getObj(this.spec, "source");
		for (String k : new String[]{"twilightforest", "commit", "minecraft", "neoforge"}) {
			JsonElement e = s.get(k);
			source.put(k, e != null && e.isJsonPrimitive() ? e.getAsString() : null);
		}
		out.put("source", source);
		if (this.seedDependent()) out.put("seed", this.seed());
		if (body instanceof Map<?, ?> m) {
			for (Map.Entry<?, ?> e : m.entrySet()) out.put(String.valueOf(e.getKey()), e.getValue());
		} else {
			out.put("data", body);
		}
		return out;
	}

	public static String id(Holder<?> holder) {
		return holder.unwrapKey().map(k -> k.location().toString()).orElse("?");
	}

	public static String id(ResourceKey<?> key) {
		return key.location().toString();
	}

	/** Deterministic sample coordinates: java.util.Random seeded from the dump kind and a salt. */
	public static java.util.Random sampler(String what) {
		return new java.util.Random(0x6F7261636C65L ^ what.hashCode());
	}
}
