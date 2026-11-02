// GloamingForest import tool (contract "import", unit F1): a small, order-preserving JSON DOM with a
// deterministic writer, so the generated tree is byte-identical on every machine and .NET version.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Text.Json;

namespace GfImport
{
    public enum JKind { Null, Bool, Number, String, Array, Object }

    /// <summary>A JSON value. Objects keep their key order; numbers keep their source text.</summary>
    public sealed class J
    {
        public JKind Kind;
        public bool B;
        public string S;          // string value, or the number's text
        public List<J> A;
        public List<KeyValuePair<string, J>> O;

        public static readonly J NullValue = new J { Kind = JKind.Null };

        public static J Obj() { return new J { Kind = JKind.Object, O = new List<KeyValuePair<string, J>>() }; }
        public static J Arr() { return new J { Kind = JKind.Array, A = new List<J>() }; }
        public static J Arr(IEnumerable<J> items) { var a = Arr(); a.A.AddRange(items); return a; }
        public static J Str(string s) { return new J { Kind = JKind.String, S = s }; }
        public static J Bool(bool b) { return new J { Kind = JKind.Bool, B = b }; }
        public static J Num(long v) { return new J { Kind = JKind.Number, S = v.ToString(CultureInfo.InvariantCulture) }; }
        public static J Num(double v)
        {
            if (double.IsNaN(v) || double.IsInfinity(v)) throw new ArgumentException("non-finite number");
            if (v == Math.Floor(v) && Math.Abs(v) < 1e15) return Num((long)v);
            return new J { Kind = JKind.Number, S = v.ToString("R", CultureInfo.InvariantCulture) };
        }
        public static J Strs(IEnumerable<string> items) { return Arr(items.Select(Str)); }

        public bool IsObj { get { return Kind == JKind.Object; } }
        public bool IsArr { get { return Kind == JKind.Array; } }
        public bool IsStr { get { return Kind == JKind.String; } }
        public bool IsNum { get { return Kind == JKind.Number; } }

        public J this[string key]
        {
            get
            {
                if (Kind != JKind.Object) return null;
                for (int i = 0; i < O.Count; i++) if (O[i].Key == key) return O[i].Value;
                return null;
            }
            set { Set(key, value); }
        }

        public J Set(string key, J value)
        {
            if (Kind != JKind.Object) throw new InvalidOperationException("not an object");
            for (int i = 0; i < O.Count; i++)
                if (O[i].Key == key) { O[i] = new KeyValuePair<string, J>(key, value); return this; }
            O.Add(new KeyValuePair<string, J>(key, value));
            return this;
        }
        public J Set(string key, string value) { return Set(key, Str(value)); }
        public J Set(string key, long value) { return Set(key, Num(value)); }
        public J Set(string key, double value) { return Set(key, Num(value)); }
        public J Set(string key, bool value) { return Set(key, Bool(value)); }

        public bool Remove(string key)
        {
            if (Kind != JKind.Object) return false;
            int n = O.RemoveAll(kv => kv.Key == key);
            return n > 0;
        }

        public J Add(J v) { A.Add(v); return this; }
        public J Add(string v) { A.Add(Str(v)); return this; }

        public bool Has(string key) { return this[key] != null; }
        public IEnumerable<string> Keys { get { return Kind == JKind.Object ? O.Select(kv => kv.Key) : Enumerable.Empty<string>(); } }
        public int Count { get { return Kind == JKind.Array ? A.Count : Kind == JKind.Object ? O.Count : 0; } }

        public string Str() { return Kind == JKind.String ? S : null; }
        public string AsString(string fallback = null) { return Kind == JKind.String ? S : fallback; }
        public double AsDouble(double fallback = 0)
        {
            double d;
            if (Kind == JKind.Number && double.TryParse(S, NumberStyles.Float, CultureInfo.InvariantCulture, out d)) return d;
            return fallback;
        }
        public long AsLong(long fallback = 0) { return (long)AsDouble(fallback); }

        public J Clone()
        {
            switch (Kind)
            {
                case JKind.Array: return Arr(A.Select(x => x.Clone()));
                case JKind.Object:
                    var o = Obj();
                    foreach (var kv in O) o.O.Add(new KeyValuePair<string, J>(kv.Key, kv.Value.Clone()));
                    return o;
                default: return new J { Kind = Kind, B = B, S = S };
            }
        }

        // ---- parsing -----------------------------------------------------------------------------

        static readonly JsonDocumentOptions ParseOptions = new JsonDocumentOptions
        {
            AllowTrailingCommas = true,
            CommentHandling = JsonCommentHandling.Skip,
            MaxDepth = 256,
        };

        public static J Parse(string text)
        {
            if (text.Length > 0 && text[0] == '﻿') text = text.Substring(1);
            using (var doc = JsonDocument.Parse(text, ParseOptions))
                return From(doc.RootElement);
        }

        static J From(JsonElement e)
        {
            switch (e.ValueKind)
            {
                case JsonValueKind.Object:
                    var o = Obj();
                    foreach (var p in e.EnumerateObject())
                    {
                        // duplicate keys: the last one wins (as Gson / Java readers do)
                        o.Set(p.Name, From(p.Value));
                    }
                    return o;
                case JsonValueKind.Array: return Arr(e.EnumerateArray().Select(From));
                case JsonValueKind.String: return Str(e.GetString());
                case JsonValueKind.Number: return new J { Kind = JKind.Number, S = e.GetRawText() };
                case JsonValueKind.True: return Bool(true);
                case JsonValueKind.False: return Bool(false);
                default: return NullValue;
            }
        }

        // ---- writing -----------------------------------------------------------------------------

        /// <summary>Pretty JSON: 2-space indent, "key": value, LF, trailing newline.</summary>
        public string ToText()
        {
            var sb = new StringBuilder();
            Write(sb, 0);
            sb.Append('\n');
            return sb.ToString();
        }

        /// <summary>Compact one-line JSON (no trailing newline).</summary>
        public string ToCompact()
        {
            var sb = new StringBuilder();
            WriteCompact(sb);
            return sb.ToString();
        }

        void Write(StringBuilder sb, int indent)
        {
            switch (Kind)
            {
                case JKind.Object:
                    if (O.Count == 0) { sb.Append("{}"); return; }
                    sb.Append("{\n");
                    for (int i = 0; i < O.Count; i++)
                    {
                        sb.Append(' ', indent + 2);
                        WriteString(sb, O[i].Key);
                        sb.Append(": ");
                        O[i].Value.Write(sb, indent + 2);
                        if (i + 1 < O.Count) sb.Append(',');
                        sb.Append('\n');
                    }
                    sb.Append(' ', indent).Append('}');
                    return;
                case JKind.Array:
                    if (A.Count == 0) { sb.Append("[]"); return; }
                    // short scalar arrays on one line (vectors, uv, state lists)
                    if (A.Count <= 16 && A.All(x => x.Kind != JKind.Object && x.Kind != JKind.Array) && A.Sum(x => (x.S ?? "").Length + 4) < 100)
                    {
                        sb.Append('[');
                        for (int i = 0; i < A.Count; i++) { if (i > 0) sb.Append(", "); A[i].Write(sb, indent); }
                        sb.Append(']');
                        return;
                    }
                    sb.Append("[\n");
                    for (int i = 0; i < A.Count; i++)
                    {
                        sb.Append(' ', indent + 2);
                        A[i].Write(sb, indent + 2);
                        if (i + 1 < A.Count) sb.Append(',');
                        sb.Append('\n');
                    }
                    sb.Append(' ', indent).Append(']');
                    return;
                default:
                    WriteCompact(sb);
                    return;
            }
        }

        void WriteCompact(StringBuilder sb)
        {
            switch (Kind)
            {
                case JKind.Null: sb.Append("null"); return;
                case JKind.Bool: sb.Append(B ? "true" : "false"); return;
                case JKind.Number: sb.Append(S); return;
                case JKind.String: WriteString(sb, S); return;
                case JKind.Array:
                    sb.Append('[');
                    for (int i = 0; i < A.Count; i++) { if (i > 0) sb.Append(','); A[i].WriteCompact(sb); }
                    sb.Append(']');
                    return;
                case JKind.Object:
                    sb.Append('{');
                    for (int i = 0; i < O.Count; i++)
                    {
                        if (i > 0) sb.Append(',');
                        WriteString(sb, O[i].Key);
                        sb.Append(':');
                        O[i].Value.WriteCompact(sb);
                    }
                    sb.Append('}');
                    return;
            }
        }

        public static void WriteString(StringBuilder sb, string s)
        {
            sb.Append('"');
            foreach (char c in s)
            {
                switch (c)
                {
                    case '"': sb.Append("\\\""); break;
                    case '\\': sb.Append("\\\\"); break;
                    case '\n': sb.Append("\\n"); break;
                    case '\r': sb.Append("\\r"); break;
                    case '\t': sb.Append("\\t"); break;
                    case '\b': sb.Append("\\b"); break;
                    case '\f': sb.Append("\\f"); break;
                    default:
                        if (c < 0x20 || c == (char)0x2028 || c == (char)0x2029) sb.Append("\\u").Append(((int)c).ToString("x4"));
                        else sb.Append(c);
                        break;
                }
            }
            sb.Append('"');
        }

        public override string ToString() { return ToCompact(); }

        // ---- JSON pointer patches (RFC 6901 / a subset of RFC 6902) -------------------------------

        static string Unescape(string token) { return token.Replace("~1", "/").Replace("~0", "~"); }

        /// <summary>Applies {"op": add|replace|remove, "path": "/a/b", "value": ...} operations.</summary>
        public static J ApplyPatch(J doc, J ops, string where)
        {
            foreach (var op in ops.A)
            {
                string kind = op["op"].AsString();
                string ptr = op["path"].AsString() ?? "";
                J value = op["value"];
                if (ptr == "")
                {
                    if (kind == "remove") throw new Exception(where + ": cannot remove the document root");
                    doc = value.Clone();
                    continue;
                }
                var tokens = ptr.Substring(1).Split('/').Select(Unescape).ToList();
                J parent = doc;
                for (int i = 0; i < tokens.Count - 1; i++)
                {
                    J next = parent.IsArr ? parent.A[int.Parse(tokens[i], CultureInfo.InvariantCulture)] : parent[tokens[i]];
                    if (next == null)
                    {
                        if (kind != "add") throw new Exception(where + ": path " + ptr + " does not exist");
                        next = Obj();
                        parent.Set(tokens[i], next);
                    }
                    parent = next;
                }
                string last = tokens[tokens.Count - 1];
                if (parent.IsArr)
                {
                    int idx = last == "-" ? parent.A.Count : int.Parse(last, CultureInfo.InvariantCulture);
                    if (kind == "remove") parent.A.RemoveAt(idx);
                    else if (kind == "add") parent.A.Insert(idx, value.Clone());
                    else if (kind == "replace") parent.A[idx] = value.Clone();
                    else throw new Exception(where + ": unsupported op " + kind);
                }
                else
                {
                    if (kind == "remove") { if (!parent.Remove(last)) throw new Exception(where + ": path " + ptr + " does not exist"); }
                    else if (kind == "add") parent.Set(last, value.Clone());
                    else if (kind == "replace") { if (!parent.Has(last)) throw new Exception(where + ": path " + ptr + " does not exist"); parent.Set(last, value.Clone()); }
                    else throw new Exception(where + ": unsupported op " + kind);
                }
            }
            return doc;
        }
    }
}
