// GloamingForest - The Gloaming Forest for Minecraft: Bedrock Edition.
// Java oracle dumper (unit F13): original code, LGPL-3.0-or-later (see COPYING.LESSER).
package gf.oracle;

import com.mojang.brigadier.CommandDispatcher;
import com.mojang.brigadier.arguments.StringArgumentType;
import com.mojang.brigadier.context.CommandContext;
import net.minecraft.commands.CommandSourceStack;
import net.minecraft.commands.Commands;
import net.minecraft.network.chat.Component;
import net.neoforged.fml.common.Mod;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.event.RegisterCommandsEvent;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.nio.file.Path;
import java.util.Locale;
import java.util.Map;

/**
 * The GloamingForest oracle dumper: a server-side NeoForge mod that adds
 * {@code /gforacle dump <kind> <file>} (operators / RCON only). tools/oracle/run.ps1 drives it over
 * RCON on a local dedicated server running The Gloaming Forest and writes the derived facts it
 * dumps (never assets) to tests/golden.
 */
@Mod(OracleMod.MOD_ID)
public final class OracleMod {
	public static final String MOD_ID = "gf_oracle";
	public static final Logger LOG = LoggerFactory.getLogger("gf_oracle");

	public OracleMod() {
		NeoForge.EVENT_BUS.addListener(RegisterCommandsEvent.class, e -> register(e.getDispatcher()));
	}

	@FunctionalInterface
	interface Dump {
		Object run(Ctx ctx) throws Exception;
	}

	static final Map<String, Dump> DUMPS = Map.ofEntries(
		Map.entry("rng", RngDump::dump),
		Map.entry("noise", NoiseDump::dump),
		Map.entry("biomes", WorldDumps::biomes),
		Map.entry("heights", WorldDumps::heights),
		Map.entry("surface", WorldDumps::surface),
		Map.entry("density", WorldDumps::density),
		Map.entry("landmarks", WorldDumps::landmarks),
		Map.entry("mobs", DataDumps::mobs),
		Map.entry("loot", DataDumps::loot),
		Map.entry("recipes", DataDumps::recipes),
		Map.entry("uncrafting", DataDumps::uncrafting),
		Map.entry("structure_steps", StructureDumps::structureSteps)
	);

	private static void register(CommandDispatcher<CommandSourceStack> dispatcher) {
		dispatcher.register(Commands.literal("gforacle")
			.requires(src -> src.hasPermission(4))
			.then(Commands.literal("ping").executes(c -> reply(c, "OK pong")))
			.then(Commands.literal("dump")
				.then(Commands.argument("kind", StringArgumentType.word())
					.then(Commands.argument("file", StringArgumentType.greedyString())
						.executes(OracleMod::dump)))));
	}

	private static int reply(CommandContext<CommandSourceStack> c, String text) {
		c.getSource().sendSuccess(() -> Component.literal(text), false);
		return 1;
	}

	private static int dump(CommandContext<CommandSourceStack> c) {
		String kind = StringArgumentType.getString(c, "kind").toLowerCase(Locale.ROOT);
		String file = StringArgumentType.getString(c, "file").trim();
		Dump d = DUMPS.get(kind);
		if (d == null) return reply(c, "ERR unknown kind " + kind + " (known: " + String.join(", ", DUMPS.keySet().stream().sorted().toList()) + ")");
		long start = System.nanoTime();
		try {
			Ctx ctx = new Ctx(c.getSource().getServer(), kind);
			Object body = d.run(ctx);
			Json.write(Path.of(file), ctx.wrap(body));
			long ms = (System.nanoTime() - start) / 1_000_000L;
			LOG.info("gf_oracle: dumped {} to {} in {} ms", kind, file, ms);
			return reply(c, "OK " + kind + " " + ms + " ms");
		} catch (Throwable t) {
			LOG.error("gf_oracle: dump {} failed", kind, t);
			return reply(c, "ERR " + kind + ": " + t);
		}
	}
}
