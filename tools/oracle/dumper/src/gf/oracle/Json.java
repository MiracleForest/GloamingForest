// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Java oracle dumper (unit F13): original code, LGPL-3.0-or-later (see COPYING.LESSER).
package gf.oracle;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Collection;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * A tiny deterministic JSON writer for the golden files: objects keep insertion order, LF line
 * endings, two-space indentation; arrays of scalars and small flat objects stay on one line, so
 * grids are one row per line and records one per line (diff-friendly).
 *
 * <p>Numbers: integers as they are; doubles with {@link Double#toString(double)} (the shortest
 * decimal that round-trips, so a C++ strtod reads back the exact double); floats are widened to
 * double first (exact), so a reader gets the float back by narrowing. NaN and infinities become
 * the strings "NaN", "Infinity", "-Infinity".
 */
public final class Json {
	private Json() {
	}

	public static Map<String, Object> obj() {
		return new LinkedHashMap<>();
	}

	public static List<Object> arr() {
		return new ArrayList<>();
	}

	@SafeVarargs
	public static <T> List<Object> arr(T... values) {
		List<Object> out = new ArrayList<>(values.length);
		for (T v : values) out.add(v);
		return out;
	}

	public static void write(Path path, Object value) throws IOException {
		StringBuilder sb = new StringBuilder(1 << 16);
		write(sb, value, 0);
		sb.append('\n');
		Path parent = path.toAbsolutePath().getParent();
		if (parent != null) Files.createDirectories(parent);
		Files.writeString(path, sb.toString(), StandardCharsets.UTF_8);
	}

	public static String toString(Object value) {
		StringBuilder sb = new StringBuilder();
		write(sb, value, 0);
		return sb.toString();
	}

	private static boolean isScalar(Object v) {
		return v == null || v instanceof Number || v instanceof Boolean || v instanceof CharSequence || v instanceof Character;
	}

	private static boolean isInlineObject(Object v) {
		if (!(v instanceof Map<?, ?> m)) return false;
		if (m.size() > 16) return false;
		for (Object e : m.values()) {
			if (isScalar(e)) continue;
			if (e instanceof Collection<?> c && c.size() <= 16 && c.stream().allMatch(Json::isScalar)) continue;
			return false;
		}
		return true;
	}

	private static void indent(StringBuilder sb, int level) {
		for (int i = 0; i < level; i++) sb.append("  ");
	}

	private static void write(StringBuilder sb, Object v, int level) {
		write(sb, v, level, false);
	}

	private static void write(StringBuilder sb, Object v, int level, boolean inline) {
		if (isScalar(v)) {
			scalar(sb, v);
		} else if (v instanceof Map<?, ?> m) {
			if (m.isEmpty()) {
				sb.append("{}");
				return;
			}
			if (inline || isInlineObject(v)) {
				sb.append('{');
				boolean first = true;
				for (Map.Entry<?, ?> e : m.entrySet()) {
					if (!first) sb.append(", ");
					first = false;
					string(sb, String.valueOf(e.getKey()));
					sb.append(": ");
					write(sb, e.getValue(), level + 1, true);
				}
				sb.append('}');
				return;
			}
			sb.append("{\n");
			int i = 0;
			for (Map.Entry<?, ?> e : m.entrySet()) {
				indent(sb, level + 1);
				string(sb, String.valueOf(e.getKey()));
				sb.append(": ");
				write(sb, e.getValue(), level + 1);
				if (++i < m.size()) sb.append(',');
				sb.append('\n');
			}
			indent(sb, level);
			sb.append('}');
		} else if (v instanceof Collection<?> c) {
			writeArray(sb, new ArrayList<>(c), level, inline);
		} else if (v instanceof int[] a) {
			List<Object> l = new ArrayList<>(a.length);
			for (int x : a) l.add(x);
			writeArray(sb, l, level, inline);
		} else if (v instanceof long[] a) {
			List<Object> l = new ArrayList<>(a.length);
			for (long x : a) l.add(x);
			writeArray(sb, l, level, inline);
		} else if (v instanceof double[] a) {
			List<Object> l = new ArrayList<>(a.length);
			for (double x : a) l.add(x);
			writeArray(sb, l, level, inline);
		} else if (v instanceof boolean[] a) {
			List<Object> l = new ArrayList<>(a.length);
			for (boolean x : a) l.add(x);
			writeArray(sb, l, level, inline);
		} else if (v instanceof Object[] a) {
			writeArray(sb, List.of(a), level, inline);
		} else {
			throw new IllegalArgumentException("not JSON: " + v.getClass().getName());
		}
	}

	/** Longest one-line rendering of a scalar array that holds strings (number rows are always one line). */
	private static final int INLINE_STRINGS = 160;

	private static void writeArray(StringBuilder sb, List<?> l, int level, boolean inline) {
		if (l.isEmpty()) {
			sb.append("[]");
			return;
		}
		if (l.stream().allMatch(Json::isScalar)) {
			StringBuilder line = new StringBuilder();
			line.append('[');
			for (int i = 0; i < l.size(); i++) {
				if (i > 0) line.append(", ");
				scalar(line, l.get(i));
			}
			line.append(']');
			boolean numbers = l.stream().noneMatch(x -> x instanceof CharSequence || x instanceof Character);
			if (inline || numbers || line.length() <= INLINE_STRINGS) {
				sb.append(line);
				return;
			}
		} else if (inline) {
			sb.append('[');
			for (int i = 0; i < l.size(); i++) {
				if (i > 0) sb.append(", ");
				write(sb, l.get(i), level + 1, true);
			}
			sb.append(']');
			return;
		}
		sb.append("[\n");
		for (int i = 0; i < l.size(); i++) {
			indent(sb, level + 1);
			write(sb, l.get(i), level + 1, false);
			if (i + 1 < l.size()) sb.append(',');
			sb.append('\n');
		}
		indent(sb, level);
		sb.append(']');
	}

	private static void scalar(StringBuilder sb, Object v) {
		if (v == null) {
			sb.append("null");
		} else if (v instanceof Boolean b) {
			sb.append(b ? "true" : "false");
		} else if (v instanceof Double d) {
			number(sb, d);
		} else if (v instanceof Float f) {
			number(sb, (double) f);
		} else if (v instanceof Number n) {
			sb.append(n.toString());
		} else {
			string(sb, v.toString());
		}
	}

	private static void number(StringBuilder sb, double d) {
		if (Double.isNaN(d)) sb.append("\"NaN\"");
		else if (Double.isInfinite(d)) sb.append(d > 0 ? "\"Infinity\"" : "\"-Infinity\"");
		else sb.append(Double.toString(d));
	}

	private static void string(StringBuilder sb, String s) {
		sb.append('"');
		for (int i = 0; i < s.length(); i++) {
			char c = s.charAt(i);
			switch (c) {
				case '"' -> sb.append("\\\"");
				case '\\' -> sb.append("\\\\");
				case '\n' -> sb.append("\\n");
				case '\r' -> sb.append("\\r");
				case '\t' -> sb.append("\\t");
				default -> {
					if (c < 0x20) sb.append(String.format("\\u%04x", (int) c));
					else sb.append(c);
				}
			}
		}
		sb.append('"');
	}
}
