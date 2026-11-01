// GloamingForest - The Twilight Forest for Minecraft: Bedrock Edition.
// Java syntax for the model converter (contract "geo", unit F2): a lexer and a recursive-descent
// parser for the subset of Java that The Twilight Forest's model classes use (LayerDefinition /
// createBodyLayer builders, setupAnim). Part of GloamingForest, LGPL-3.0-or-later.
//
// The parser reads whole compilation units: package, imports, classes / interfaces / enums with
// their fields, methods and constructors. Method bodies are kept as token ranges and parsed on
// demand (ParseBody), so a construct the converter never runs cannot break the conversion.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace Gf.Models
{
    public enum TokKind { Ident, Number, String, Char, Op, EOF }

    public sealed class Token
    {
        public TokKind Kind;
        public string Text;
        public object Value; // numbers: int / long / float / double; strings and chars: the decoded value
        public int Line;
        public override string ToString() { return Kind + " '" + Text + "' @" + Line; }
    }

    public sealed class JavaSyntaxException : Exception
    {
        public JavaSyntaxException(string message) : base(message) { }
    }

    public static class Lexer
    {
        static readonly string[] Ops = {
            ">>>=", "<<=", ">>=", "...", "->", "::", "++", "--", "&&", "||", "==", "!=", "<=", ">=",
            "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=",
            "+", "-", "*", "/", "%", "=", "<", ">", "!", "~", "?", ":", ";", ",", ".", "(", ")", "[", "]", "{", "}", "&", "|", "^", "@"
        };

        public static List<Token> Tokenize(string src)
        {
            var list = new List<Token>();
            int i = 0, line = 1, n = src.Length;
            while (i < n)
            {
                char c = src[i];
                if (c == '\n') { line++; i++; continue; }
                if (char.IsWhiteSpace(c)) { i++; continue; }
                if (c == '/' && i + 1 < n && src[i + 1] == '/') { while (i < n && src[i] != '\n') i++; continue; }
                if (c == '/' && i + 1 < n && src[i + 1] == '*')
                {
                    i += 2;
                    while (i + 1 < n && !(src[i] == '*' && src[i + 1] == '/')) { if (src[i] == '\n') line++; i++; }
                    i += 2;
                    continue;
                }
                if (char.IsLetter(c) || c == '_' || c == '$')
                {
                    int s = i;
                    while (i < n && (char.IsLetterOrDigit(src[i]) || src[i] == '_' || src[i] == '$')) i++;
                    list.Add(new Token { Kind = TokKind.Ident, Text = src.Substring(s, i - s), Line = line });
                    continue;
                }
                if (char.IsDigit(c) || (c == '.' && i + 1 < n && char.IsDigit(src[i + 1])))
                {
                    int s = i;
                    list.Add(ReadNumber(src, ref i, line));
                    continue;
                }
                if (c == '"')
                {
                    if (i + 2 < n && src[i + 1] == '"' && src[i + 2] == '"')
                    {
                        // text block
                        int s = i; i += 3;
                        while (i + 2 < n && !(src[i] == '"' && src[i + 1] == '"' && src[i + 2] == '"')) { if (src[i] == '\n') line++; i++; }
                        i += 3;
                        list.Add(new Token { Kind = TokKind.String, Text = src.Substring(s, i - s), Value = "", Line = line });
                        continue;
                    }
                    var sb = new StringBuilder();
                    int st = i; i++;
                    while (i < n && src[i] != '"') { sb.Append(ReadChar(src, ref i)); }
                    i++;
                    list.Add(new Token { Kind = TokKind.String, Text = src.Substring(st, i - st), Value = sb.ToString(), Line = line });
                    continue;
                }
                if (c == '\'')
                {
                    int st = i; i++;
                    char v = ReadChar(src, ref i);
                    i++; // closing quote
                    list.Add(new Token { Kind = TokKind.Char, Text = src.Substring(st, i - st), Value = v, Line = line });
                    continue;
                }
                bool matched = false;
                foreach (var op in Ops)
                {
                    if (string.CompareOrdinal(src, i, op, 0, op.Length) == 0)
                    {
                        list.Add(new Token { Kind = TokKind.Op, Text = op, Line = line });
                        i += op.Length;
                        matched = true;
                        break;
                    }
                }
                if (!matched) throw new JavaSyntaxException("unexpected character '" + c + "' at line " + line);
            }
            list.Add(new Token { Kind = TokKind.EOF, Text = "<eof>", Line = line });
            return list;
        }

        static char ReadChar(string src, ref int i)
        {
            char c = src[i++];
            if (c != '\\') return c;
            char e = src[i++];
            switch (e)
            {
                case 'n': return '\n';
                case 't': return '\t';
                case 'r': return '\r';
                case 'b': return '\b';
                case 'f': return '\f';
                case '0': return '\0';
                case 'u':
                    while (src[i] == 'u') i++;
                    var hex = src.Substring(i, 4); i += 4;
                    return (char)int.Parse(hex, NumberStyles.HexNumber);
                default: return e;
            }
        }

        static Token ReadNumber(string src, ref int i, int line)
        {
            int s = i, n = src.Length;
            if (src[i] == '0' && i + 1 < n && (src[i + 1] == 'x' || src[i + 1] == 'X'))
            {
                i += 2;
                while (i < n && (Uri.IsHexDigit(src[i]) || src[i] == '_')) i++;
                string hex = src.Substring(s + 2, i - s - 2).Replace("_", "");
                bool isLong = i < n && (src[i] == 'L' || src[i] == 'l');
                if (isLong) i++;
                ulong v = ulong.Parse(hex, NumberStyles.HexNumber, CultureInfo.InvariantCulture);
                object val = isLong ? (object)unchecked((long)v) : (object)unchecked((int)(uint)v);
                return new Token { Kind = TokKind.Number, Text = src.Substring(s, i - s), Value = val, Line = line };
            }
            bool isFloat = false;
            while (i < n && (char.IsDigit(src[i]) || src[i] == '_')) i++;
            if (i < n && src[i] == '.' && i + 1 < n && char.IsDigit(src[i + 1])) { isFloat = true; i++; while (i < n && (char.IsDigit(src[i]) || src[i] == '_')) i++; }
            else if (i < n && src[i] == '.' && !(i + 1 < n && char.IsLetter(src[i + 1]))) { isFloat = true; i++; }
            if (i < n && (src[i] == 'e' || src[i] == 'E'))
            {
                isFloat = true; i++;
                if (src[i] == '+' || src[i] == '-') i++;
                while (i < n && char.IsDigit(src[i])) i++;
            }
            string body = src.Substring(s, i - s).Replace("_", "");
            object value;
            if (i < n && (src[i] == 'f' || src[i] == 'F')) { i++; value = float.Parse(body, NumberStyles.Float, CultureInfo.InvariantCulture); }
            else if (i < n && (src[i] == 'd' || src[i] == 'D')) { i++; value = double.Parse(body, NumberStyles.Float, CultureInfo.InvariantCulture); }
            else if (i < n && (src[i] == 'l' || src[i] == 'L')) { i++; value = long.Parse(body, CultureInfo.InvariantCulture); }
            else if (isFloat) value = double.Parse(body, NumberStyles.Float, CultureInfo.InvariantCulture);
            else
            {
                long l = long.Parse(body, CultureInfo.InvariantCulture);
                value = l <= int.MaxValue ? (object)(int)l : (object)unchecked((int)(uint)l);
            }
            return new Token { Kind = TokKind.Number, Text = src.Substring(s, i - s), Value = value, Line = line };
        }
    }

    // ---- syntax tree ---------------------------------------------------------------------------------

    public abstract class Node { public int Line; }

    public abstract class Expr : Node { }
    public sealed class LitExpr : Expr { public object Value; }
    public sealed class NameExpr : Expr { public string Name; }
    public sealed class FieldExpr : Expr { public Expr Target; public string Name; }
    public sealed class CallExpr : Expr { public Expr Target; public string Name; public List<Expr> Args = new List<Expr>(); }
    public sealed class NewExpr : Expr { public TypeRef Type; public List<Expr> Args = new List<Expr>(); public List<Expr> Dims; public ArrayInitExpr Init; public bool HasBody; }
    public sealed class ArrayInitExpr : Expr { public List<Expr> Items = new List<Expr>(); }
    public sealed class IndexExpr : Expr { public Expr Target; public Expr Index; }
    public sealed class UnaryExpr : Expr { public string Op; public Expr Operand; public bool Postfix; }
    public sealed class BinaryExpr : Expr { public string Op; public Expr Left, Right; }
    public sealed class AssignExpr : Expr { public string Op; public Expr Target, Value; }
    public sealed class CondExpr : Expr { public Expr Cond, Then, Else; }
    public sealed class CastExpr : Expr { public TypeRef Type; public Expr Operand; }
    public sealed class InstanceOfExpr : Expr { public Expr Operand; public TypeRef Type; public string Bind; }
    public sealed class LambdaExpr : Expr { public List<string> Params = new List<string>(); public Expr Body; public BlockStmt Block; }
    public sealed class MethodRefExpr : Expr { public Expr Target; public string Name; }
    public sealed class ThisExpr : Expr { }
    public sealed class SuperExpr : Expr { }
    public sealed class ClassLitExpr : Expr { public TypeRef Type; }
    public sealed class SwitchExpr : Expr { public Expr Selector; public List<SwitchCase> Cases = new List<SwitchCase>(); }

    public sealed class SwitchCase
    {
        public List<Expr> Labels = new List<Expr>(); // empty = default
        public bool Arrow;
        public Expr ArrowExpr;            // case X -> expr;
        public Stmt ArrowStmt;            // case X -> { ... } / throw
        public List<Stmt> Body = new List<Stmt>(); // old-style case X: stmts
    }

    public sealed class TypeRef
    {
        public string Name;          // simple or dotted name ("float", "PartDefinition", "Map.Entry")
        public List<TypeRef> Args = new List<TypeRef>();
        public int Dims;
        public override string ToString() { return Name + new string('[', Dims); }
        public string Simple { get { int d = Name.LastIndexOf('.'); return d < 0 ? Name : Name.Substring(d + 1); } }
    }

    public abstract class Stmt : Node { }
    public sealed class BlockStmt : Stmt { public List<Stmt> Stmts = new List<Stmt>(); }
    public sealed class LocalVarStmt : Stmt { public TypeRef Type; public List<KeyValuePair<string, Expr>> Vars = new List<KeyValuePair<string, Expr>>(); }
    public sealed class ExprStmt : Stmt { public Expr E; }
    public sealed class IfStmt : Stmt { public Expr Cond; public Stmt Then, Else; }
    public sealed class ForStmt : Stmt { public List<Stmt> Init = new List<Stmt>(); public Expr Cond; public List<Expr> Update = new List<Expr>(); public Stmt Body; }
    public sealed class ForEachStmt : Stmt { public TypeRef Type; public string Var; public Expr Iterable; public Stmt Body; }
    public sealed class WhileStmt : Stmt { public Expr Cond; public Stmt Body; public bool Do; }
    public sealed class ReturnStmt : Stmt { public Expr Value; }
    public sealed class BreakStmt : Stmt { }
    public sealed class ContinueStmt : Stmt { }
    public sealed class YieldStmt : Stmt { public Expr Value; }
    public sealed class ThrowStmt : Stmt { public Expr Value; }
    public sealed class SwitchStmt : Stmt { public Expr Selector; public List<SwitchCase> Cases = new List<SwitchCase>(); }
    public sealed class TryStmt : Stmt { public BlockStmt Body; public BlockStmt Finally; }
    public sealed class EmptyStmt : Stmt { }

    // ---- declarations ---------------------------------------------------------------------------------

    public sealed class FieldDecl
    {
        public string Name;
        public TypeRef Type;
        public bool IsStatic;
        public int InitStart = -1, InitEnd = -1; // token range of the initializer (exclusive end)
        public Expr Init;                          // parsed on demand
        public int Line;
    }

    public sealed class Param { public TypeRef Type; public string Name; public bool VarArgs; }

    public sealed class MethodDecl
    {
        public string Name;          // "<init>" for constructors
        public TypeRef ReturnType;
        public bool IsStatic;
        public List<Param> Params = new List<Param>();
        public int BodyStart = -1, BodyEnd = -1; // tokens between the braces (exclusive)
        public BlockStmt Body;                     // parsed on demand
        public ClassDecl Owner;
        public int Line;
        public string Signature
        {
            get
            {
                var sb = new StringBuilder(Name).Append('(');
                for (int i = 0; i < Params.Count; i++) { if (i > 0) sb.Append(", "); sb.Append(Params[i].Type).Append(' ').Append(Params[i].Name); }
                return sb.Append(')').ToString();
            }
        }
    }

    public sealed class ClassDecl
    {
        public string Name;
        public string Kind; // class, interface, enum, record
        public string Extends;
        public List<string> Implements = new List<string>();
        public List<FieldDecl> Fields = new List<FieldDecl>();
        public List<MethodDecl> Methods = new List<MethodDecl>();
        public List<ClassDecl> Nested = new List<ClassDecl>();
        public List<string> EnumConstants = new List<string>();
        public ClassDecl Outer;
        public CompilationUnit Unit;
        public string Package;
        public string QualifiedName { get { return Outer != null ? Outer.QualifiedName + "." + Name : (string.IsNullOrEmpty(Package) ? Name : Package + "." + Name); } }
    }

    public sealed class CompilationUnit
    {
        public string Path;
        public string Package;
        public List<string> Imports = new List<string>();
        public List<ClassDecl> Types = new List<ClassDecl>();
        public List<Token> Tokens;
        public string Source;
    }

    // ---- parser ---------------------------------------------------------------------------------------

    public sealed class Parser
    {
        readonly List<Token> t;
        int p;
        readonly int end;
        readonly string file;

        static readonly HashSet<string> Modifiers = new HashSet<string> {
            "public", "private", "protected", "static", "final", "abstract", "native", "synchronized", "transient", "volatile", "strictfp", "default", "sealed", "non-sealed"
        };
        static readonly HashSet<string> Primitives = new HashSet<string> { "boolean", "byte", "short", "int", "long", "float", "double", "char", "void" };
        static readonly HashSet<string> Keywords = new HashSet<string> {
            "if", "else", "for", "while", "do", "return", "break", "continue", "new", "switch", "case", "default", "throw", "try", "catch", "finally",
            "this", "super", "class", "interface", "enum", "instanceof", "true", "false", "null", "yield", "assert"
        };

        public Parser(List<Token> tokens, int start, int endExclusive, string fileName)
        {
            t = tokens; p = start; end = endExclusive; file = fileName;
        }

        public static CompilationUnit ParseUnit(string source, string path)
        {
            var tokens = Lexer.Tokenize(source);
            var cu = new CompilationUnit { Path = path, Tokens = tokens, Source = source };
            var ps = new Parser(tokens, 0, tokens.Count - 1, path);
            ps.ParseUnitInto(cu);
            return cu;
        }

        Token Cur { get { return p < end ? t[p] : t[t.Count - 1]; } }
        Token Peek(int k) { int i = p + k; return i < end ? t[i] : t[t.Count - 1]; }
        bool Is(string text) { var c = Cur; return (c.Kind == TokKind.Op || c.Kind == TokKind.Ident) && c.Text == text; }
        bool IsAt(int k, string text) { var c = Peek(k); return (c.Kind == TokKind.Op || c.Kind == TokKind.Ident) && c.Text == text; }
        bool Accept(string text) { if (Is(text)) { p++; return true; } return false; }
        JavaSyntaxException Error(string msg) { return new JavaSyntaxException(file + ":" + Cur.Line + ": " + msg + " (at " + Cur.Text + ")"); }
        void Expect(string text) { if (!Accept(text)) throw Error("expected '" + text + "'"); }
        string Ident()
        {
            if (Cur.Kind != TokKind.Ident) throw Error("expected an identifier");
            return t[p++].Text;
        }
        public bool AtEnd { get { return p >= end; } }

        // Skips a balanced (...) / [...] / {...} group starting at the current opening token.
        int SkipGroup()
        {
            string open = Cur.Text;
            string close = open == "(" ? ")" : open == "[" ? "]" : "}";
            int depth = 0;
            while (p < end)
            {
                var c = t[p];
                if (c.Kind == TokKind.Op && (c.Text == "(" || c.Text == "[" || c.Text == "{")) depth++;
                else if (c.Kind == TokKind.Op && (c.Text == ")" || c.Text == "]" || c.Text == "}")) { depth--; if (depth == 0) { p++; return p; } }
                p++;
            }
            throw Error("unbalanced '" + open + "'");
        }

        void SkipAnnotation()
        {
            Expect("@");
            if (Is("interface")) return; // @interface declaration: handled by the caller
            Ident();
            while (Is(".") && Peek(1).Kind == TokKind.Ident) { p++; Ident(); }
            if (Is("(")) SkipGroup();
        }

        void SkipTypeParams()
        {
            if (!Is("<")) return;
            int depth = 0;
            while (p < end)
            {
                if (Is("<")) depth++;
                else if (Is(">")) { depth--; if (depth == 0) { p++; return; } }
                else if (Is(">>")) { depth -= 2; if (depth <= 0) { p++; return; } }
                p++;
            }
        }

        void ParseUnitInto(CompilationUnit cu)
        {
            while (!AtEnd)
            {
                if (Is("@") && !IsAt(1, "interface")) { SkipAnnotation(); continue; }
                if (Accept("package")) { cu.Package = QualifiedName(); Expect(";"); continue; }
                if (Accept("import"))
                {
                    bool st = Accept("static");
                    var name = new StringBuilder(Ident());
                    while (Accept(".")) { if (Accept("*")) { name.Append(".*"); break; } name.Append('.').Append(Ident()); }
                    Expect(";");
                    cu.Imports.Add((st ? "static " : "") + name);
                    continue;
                }
                if (Accept(";")) continue;
                var cls = ParseTypeDecl(cu, null);
                if (cls != null) cu.Types.Add(cls);
            }
        }

        string QualifiedName()
        {
            var sb = new StringBuilder(Ident());
            while (Is(".") && Peek(1).Kind == TokKind.Ident) { p++; sb.Append('.').Append(Ident()); }
            return sb.ToString();
        }

        // Modifiers and annotations before a declaration. Returns true when "static" was among them.
        bool ParseModifiers()
        {
            bool st = false;
            while (true)
            {
                if (Is("@") && !IsAt(1, "interface")) { SkipAnnotation(); continue; }
                if (Cur.Kind == TokKind.Ident && Modifiers.Contains(Cur.Text))
                {
                    // "default" in an interface method; "sealed"/"non-sealed" are contextual
                    if (Cur.Text == "static") st = true;
                    p++;
                    continue;
                }
                if (Is("non") && IsAt(1, "-") && IsAt(2, "sealed")) { p += 3; continue; }
                return st;
            }
        }

        ClassDecl ParseTypeDecl(CompilationUnit cu, ClassDecl outer)
        {
            ParseModifiers();
            string kind;
            if (Is("@") && IsAt(1, "interface")) { p += 2; kind = "annotation"; }
            else if (Is("class") || Is("interface") || Is("enum") || Is("record")) { kind = Cur.Text; p++; }
            else throw Error("expected a type declaration");
            var cls = new ClassDecl { Kind = kind, Outer = outer, Unit = cu, Package = cu.Package };
            cls.Name = Ident();
            SkipTypeParams();
            if (kind == "record" && Is("(")) SkipGroup();
            while (!Is("{"))
            {
                if (Accept("extends"))
                {
                    var tr = ParseType();
                    cls.Extends = tr.Simple;
                    while (Accept(",")) { cls.Implements.Add(ParseType().Simple); } // interfaces extending several
                }
                else if (Accept("implements") || Accept("permits"))
                {
                    cls.Implements.Add(ParseType().Simple);
                    while (Accept(",")) cls.Implements.Add(ParseType().Simple);
                }
                else throw Error("unexpected token in a type header");
            }
            Expect("{");
            if (kind == "enum") ParseEnumConstants(cls);
            while (!Is("}"))
            {
                if (Accept(";")) continue;
                ParseMember(cu, cls);
            }
            Expect("}");
            return cls;
        }

        void ParseEnumConstants(ClassDecl cls)
        {
            while (!Is(";") && !Is("}"))
            {
                while (Is("@")) SkipAnnotation();
                cls.EnumConstants.Add(Ident());
                if (Is("(")) SkipGroup();
                if (Is("{")) SkipGroup();
                if (!Accept(",")) break;
            }
            Accept(";");
        }

        void ParseMember(CompilationUnit cu, ClassDecl cls)
        {
            int start = p;
            bool isStatic = ParseModifiers();
            if (Is("class") || Is("interface") || Is("enum") || Is("record") || (Is("@") && IsAt(1, "interface")))
            {
                p = start;
                var nested = ParseTypeDecl(cu, cls);
                cls.Nested.Add(nested);
                return;
            }
            if (Is("{")) { SkipGroup(); return; } // initializer block
            int line = Cur.Line;
            SkipTypeParams();
            // constructor: Name (
            if (Cur.Kind == TokKind.Ident && Cur.Text == cls.Name && IsAt(1, "("))
            {
                p++;
                var m = new MethodDecl { Name = "<init>", IsStatic = false, Owner = cls, Line = line };
                ParseParams(m);
                if (Accept("throws")) { ParseType(); while (Accept(",")) ParseType(); }
                ParseMethodBody(m);
                cls.Methods.Add(m);
                return;
            }
            var type = ParseType();
            string name = Ident();
            if (Is("("))
            {
                var m = new MethodDecl { Name = name, ReturnType = type, IsStatic = isStatic, Owner = cls, Line = line };
                ParseParams(m);
                while (Accept("[")) Expect("]");
                if (Accept("throws")) { ParseType(); while (Accept(",")) ParseType(); }
                if (Accept("default")) { SkipToSemicolon(); }
                ParseMethodBody(m);
                cls.Methods.Add(m);
                return;
            }
            // field(s)
            while (true)
            {
                var f = new FieldDecl { Name = name, Type = type, IsStatic = isStatic || cls.Kind == "interface", Line = line };
                int dims = 0;
                while (Accept("[")) { Expect("]"); dims++; }
                if (dims > 0) f.Type = new TypeRef { Name = type.Name, Args = type.Args, Dims = type.Dims + dims };
                if (Accept("="))
                {
                    f.InitStart = p;
                    SkipInitializer();
                    f.InitEnd = p;
                }
                cls.Fields.Add(f);
                if (Accept(",")) { name = Ident(); continue; }
                Expect(";");
                break;
            }
        }

        void SkipToSemicolon()
        {
            while (!Is(";")) { if (Is("(") || Is("{") || Is("[")) SkipGroup(); else p++; }
        }

        // Skips a field initializer up to the ',' or ';' at depth 0.
        void SkipInitializer()
        {
            while (p < end)
            {
                if (Is("(") || Is("{") || Is("[")) { SkipGroup(); continue; }
                if (Is(",") || Is(";")) return;
                p++;
            }
        }

        void ParseParams(MethodDecl m)
        {
            Expect("(");
            while (!Is(")"))
            {
                ParseModifiers();
                var type = ParseType();
                bool varargs = Accept("...");
                if (varargs) type = new TypeRef { Name = type.Name, Args = type.Args, Dims = type.Dims + 1 };
                string pname = Ident();
                while (Accept("[")) { Expect("]"); type = new TypeRef { Name = type.Name, Args = type.Args, Dims = type.Dims + 1 }; }
                m.Params.Add(new Param { Type = type, Name = pname, VarArgs = varargs });
                if (!Accept(",")) break;
            }
            Expect(")");
        }

        void ParseMethodBody(MethodDecl m)
        {
            if (Accept(";")) return; // abstract / interface
            if (!Is("{")) throw Error("expected a method body");
            m.BodyStart = p + 1;
            SkipGroup();
            m.BodyEnd = p - 1;
        }

        // ---- types --------------------------------------------------------------------------------------

        public TypeRef ParseType()
        {
            while (Is("@")) SkipAnnotation();
            var tr = new TypeRef();
            if (Accept("?"))
            {
                tr.Name = "Object";
                if (Accept("extends") || Accept("super")) return ParseType();
                return tr;
            }
            var sb = new StringBuilder(Ident());
            ParseTypeArgsInto(tr);
            while (Is(".") && Peek(1).Kind == TokKind.Ident)
            {
                p++;
                sb.Append('.').Append(Ident());
                ParseTypeArgsInto(tr);
            }
            tr.Name = sb.ToString();
            while (Is("[") && IsAt(1, "]")) { p += 2; tr.Dims++; }
            return tr;
        }

        void ParseTypeArgsInto(TypeRef tr)
        {
            if (!Is("<")) return;
            p++;
            if (Accept(">")) return; // diamond
            while (true)
            {
                tr.Args.Add(ParseType());
                while (Accept("&")) ParseType();
                if (Accept(",")) continue;
                if (Accept(">")) return;
                if (Is(">>")) { t[p] = new Token { Kind = TokKind.Op, Text = ">", Line = Cur.Line }; return; }
                if (Is(">>>")) { t[p] = new Token { Kind = TokKind.Op, Text = ">>", Line = Cur.Line }; return; }
                throw Error("expected '>' in type arguments");
            }
        }

        // Speculatively parses "Type name" at the current position; restores on failure.
        bool LooksLikeDeclaration()
        {
            int save = p;
            try
            {
                while (Is("final") || Is("@")) { if (Is("@")) SkipAnnotation(); else p++; }
                if (Cur.Kind != TokKind.Ident || (Keywords.Contains(Cur.Text) && Cur.Text != "var")) return false;
                ParseType();
                if (Cur.Kind != TokKind.Ident || Keywords.Contains(Cur.Text)) return false;
                p++;
                return Is("=") || Is(";") || Is(",") || Is(":") || Is("[");
            }
            catch (JavaSyntaxException) { return false; }
            finally { p = save; }
        }

        // ---- statements ---------------------------------------------------------------------------------

        public BlockStmt ParseBlockContents()
        {
            var b = new BlockStmt { Line = Cur.Line };
            while (!AtEnd && !Is("}")) b.Stmts.Add(ParseStatement());
            return b;
        }

        BlockStmt ParseBlock()
        {
            int line = Cur.Line;
            Expect("{");
            var b = new BlockStmt { Line = line };
            while (!Is("}")) b.Stmts.Add(ParseStatement());
            Expect("}");
            return b;
        }

        Stmt ParseStatement()
        {
            int line = Cur.Line;
            if (Is("{")) return ParseBlock();
            if (Accept(";")) return new EmptyStmt { Line = line };
            if (Cur.Kind == TokKind.Ident)
            {
                switch (Cur.Text)
                {
                    case "if":
                    {
                        p++; Expect("(");
                        var c = ParseExpr(); Expect(")");
                        var th = ParseStatement();
                        Stmt el = null;
                        if (Accept("else")) el = ParseStatement();
                        return new IfStmt { Cond = c, Then = th, Else = el, Line = line };
                    }
                    case "for":
                    {
                        p++; Expect("(");
                        if (LooksLikeForEach())
                        {
                            ParseModifiers();
                            var type = ParseType();
                            string v = Ident();
                            Expect(":");
                            var it = ParseExpr(); Expect(")");
                            return new ForEachStmt { Type = type, Var = v, Iterable = it, Body = ParseStatement(), Line = line };
                        }
                        var fs = new ForStmt { Line = line };
                        if (!Is(";"))
                        {
                            if (LooksLikeDeclaration()) fs.Init.Add(ParseLocalVar(false));
                            else { fs.Init.Add(new ExprStmt { E = ParseExpr(), Line = line }); while (Accept(",")) fs.Init.Add(new ExprStmt { E = ParseExpr(), Line = line }); }
                        }
                        Expect(";");
                        if (!Is(";")) fs.Cond = ParseExpr();
                        Expect(";");
                        if (!Is(")")) { fs.Update.Add(ParseExpr()); while (Accept(",")) fs.Update.Add(ParseExpr()); }
                        Expect(")");
                        fs.Body = ParseStatement();
                        return fs;
                    }
                    case "while":
                    {
                        p++; Expect("(");
                        var c = ParseExpr(); Expect(")");
                        return new WhileStmt { Cond = c, Body = ParseStatement(), Line = line };
                    }
                    case "do":
                    {
                        p++;
                        var body = ParseStatement();
                        Expect("while"); Expect("(");
                        var c = ParseExpr(); Expect(")"); Expect(";");
                        return new WhileStmt { Cond = c, Body = body, Do = true, Line = line };
                    }
                    case "return":
                    {
                        p++;
                        Expr v = Is(";") ? null : ParseExpr();
                        Expect(";");
                        return new ReturnStmt { Value = v, Line = line };
                    }
                    case "break": p++; if (Cur.Kind == TokKind.Ident) p++; Expect(";"); return new BreakStmt { Line = line };
                    case "continue": p++; if (Cur.Kind == TokKind.Ident) p++; Expect(";"); return new ContinueStmt { Line = line };
                    case "throw": { p++; var v = ParseExpr(); Expect(";"); return new ThrowStmt { Value = v, Line = line }; }
                    case "yield":
                        if (!IsAt(1, "=") && !IsAt(1, "(") && !IsAt(1, "."))
                        {
                            p++; var v = ParseExpr(); Expect(";"); return new YieldStmt { Value = v, Line = line };
                        }
                        break;
                    case "switch":
                    {
                        p++;
                        var sw = new SwitchStmt { Line = line };
                        Expect("(");
                        sw.Selector = ParseExpr();
                        Expect(")");
                        sw.Cases = ParseSwitchBody();
                        return sw;
                    }
                    case "try":
                    {
                        p++;
                        if (Is("(")) SkipGroup();
                        var ts = new TryStmt { Body = ParseBlock(), Line = line };
                        while (Accept("catch")) { SkipGroup(); ParseBlock(); }
                        if (Accept("finally")) ts.Finally = ParseBlock();
                        return ts;
                    }
                    case "assert": p++; SkipToSemicolon(); Expect(";"); return new EmptyStmt { Line = line };
                    case "class": case "interface": case "enum": case "record":
                        throw Error("local type declarations are not supported");
                }
                // labeled statement
                if (Peek(1).Kind == TokKind.Op && Peek(1).Text == ":" && !Keywords.Contains(Cur.Text)) { p += 2; return ParseStatement(); }
            }
            if (LooksLikeDeclaration())
            {
                var lv = ParseLocalVar(true);
                return lv;
            }
            var e = ParseExpr();
            Expect(";");
            return new ExprStmt { E = e, Line = line };
        }

        bool LooksLikeForEach()
        {
            int save = p;
            try
            {
                ParseModifiers();
                ParseType();
                if (Cur.Kind != TokKind.Ident) return false;
                p++;
                return Is(":");
            }
            catch (JavaSyntaxException) { return false; }
            finally { p = save; }
        }

        LocalVarStmt ParseLocalVar(bool semicolon)
        {
            int line = Cur.Line;
            ParseModifiers();
            var type = ParseType();
            var lv = new LocalVarStmt { Type = type, Line = line };
            while (true)
            {
                string name = Ident();
                while (Accept("[")) { Expect("]"); lv.Type = new TypeRef { Name = type.Name, Args = type.Args, Dims = type.Dims + 1 }; }
                Expr init = null;
                if (Accept("=")) init = Is("{") ? ParseArrayInit() : ParseExpr();
                lv.Vars.Add(new KeyValuePair<string, Expr>(name, init));
                if (!Accept(",")) break;
            }
            if (semicolon) Expect(";");
            return lv;
        }

        ArrayInitExpr ParseArrayInit()
        {
            var a = new ArrayInitExpr { Line = Cur.Line };
            Expect("{");
            while (!Is("}"))
            {
                a.Items.Add(Is("{") ? ParseArrayInit() : ParseExpr());
                if (!Accept(",")) break;
            }
            Expect("}");
            return a;
        }

        List<SwitchCase> ParseSwitchBody()
        {
            var cases = new List<SwitchCase>();
            Expect("{");
            while (!Is("}"))
            {
                var sc = new SwitchCase();
                if (Accept("default")) { }
                else
                {
                    Expect("case");
                    while (true)
                    {
                        // enum constants are plain names; patterns are not supported
                        sc.Labels.Add(ParseTernary());
                        if (!Accept(",")) break;
                    }
                }
                if (Accept("->"))
                {
                    sc.Arrow = true;
                    if (Is("{")) sc.ArrowStmt = ParseBlock();
                    else if (Is("throw")) sc.ArrowStmt = ParseStatement();
                    else { sc.ArrowExpr = ParseExpr(); Expect(";"); }
                }
                else
                {
                    Expect(":");
                    while (!Is("case") && !Is("default") && !Is("}")) sc.Body.Add(ParseStatement());
                }
                cases.Add(sc);
            }
            Expect("}");
            return cases;
        }

        // ---- expressions --------------------------------------------------------------------------------

        public Expr ParseExpr()
        {
            int line = Cur.Line;
            if (IsLambdaStart()) return ParseLambda();
            var left = ParseTernary();
            if (Cur.Kind == TokKind.Op)
            {
                switch (Cur.Text)
                {
                    case "=": case "+=": case "-=": case "*=": case "/=": case "%=": case "&=": case "|=": case "^=": case "<<=": case ">>=": case ">>>=":
                        string op = t[p++].Text;
                        var right = ParseExpr();
                        return new AssignExpr { Op = op, Target = left, Value = right, Line = line };
                }
            }
            return left;
        }

        bool IsLambdaStart()
        {
            if (Cur.Kind == TokKind.Ident && IsAt(1, "->")) return true;
            if (!Is("(")) return false;
            int save = p;
            try { SkipGroup(); return Is("->"); }
            catch (JavaSyntaxException) { return false; }
            finally { p = save; }
        }

        Expr ParseLambda()
        {
            var l = new LambdaExpr { Line = Cur.Line };
            if (Cur.Kind == TokKind.Ident) l.Params.Add(Ident());
            else
            {
                Expect("(");
                while (!Is(")"))
                {
                    // (a, b) or (Type a, Type b)
                    if (Cur.Kind == TokKind.Ident && (IsAt(1, ",") || IsAt(1, ")"))) l.Params.Add(Ident());
                    else { ParseModifiers(); ParseType(); l.Params.Add(Ident()); }
                    if (!Accept(",")) break;
                }
                Expect(")");
            }
            Expect("->");
            if (Is("{")) l.Block = ParseBlock();
            else l.Body = ParseExpr();
            return l;
        }

        Expr ParseTernary()
        {
            int line = Cur.Line;
            var c = ParseBinary(0);
            if (Accept("?"))
            {
                var a = IsLambdaStart() ? ParseLambda() : ParseTernary();
                Expect(":");
                var b = IsLambdaStart() ? ParseLambda() : ParseTernary();
                return new CondExpr { Cond = c, Then = a, Else = b, Line = line };
            }
            return c;
        }

        static readonly string[][] Levels = {
            new[] { "||" }, new[] { "&&" }, new[] { "|" }, new[] { "^" }, new[] { "&" },
            new[] { "==", "!=" }, new[] { "<", ">", "<=", ">=", "instanceof" }, new[] { "<<", ">>", ">>>" },
            new[] { "+", "-" }, new[] { "*", "/", "%" }
        };

        Expr ParseBinary(int level)
        {
            if (level >= Levels.Length) return ParseUnary();
            var left = ParseBinary(level + 1);
            while (true)
            {
                int line = Cur.Line;
                string op = MatchBinaryOp(level);
                if (op == null) return left;
                if (op == "instanceof")
                {
                    Accept("final");
                    var type = ParseType();
                    string bind = null;
                    if (Cur.Kind == TokKind.Ident && !Keywords.Contains(Cur.Text)) bind = Ident();
                    left = new InstanceOfExpr { Operand = left, Type = type, Bind = bind, Line = line };
                    continue;
                }
                var right = ParseBinary(level + 1);
                left = new BinaryExpr { Op = op, Left = left, Right = right, Line = line };
            }
        }

        string MatchBinaryOp(int level)
        {
            var c = Cur;
            // shifts arrive as separate '<' '<' / '>' '>' tokens (the lexer never joins them, for generics)
            if (level == 7 && c.Kind == TokKind.Op)
            {
                if (c.Text == "<" && IsAt(1, "<") && Peek(1).Line == c.Line) { p += 2; return "<<"; }
                if (c.Text == ">" && IsAt(1, ">") && IsAt(2, ">")) { p += 3; return ">>>"; }
                if (c.Text == ">" && IsAt(1, ">")) { p += 2; return ">>"; }
                return null;
            }
            if (level == 6 && c.Kind == TokKind.Op && (c.Text == "<" || c.Text == ">") && (IsAt(1, "<") || IsAt(1, ">"))) return null;
            foreach (var op in Levels[level])
            {
                if ((c.Kind == TokKind.Op || (op == "instanceof" && c.Kind == TokKind.Ident)) && c.Text == op) { p++; return op; }
            }
            return null;
        }

        Expr ParseUnary()
        {
            int line = Cur.Line;
            if (Cur.Kind == TokKind.Op)
            {
                switch (Cur.Text)
                {
                    case "+": case "-": case "!": case "~":
                    {
                        string op = t[p++].Text;
                        var operand = ParseUnary();
                        // fold negative literals so "-2147483648" and "-1.0F" stay literals
                        if (op == "-" && operand is LitExpr lit)
                        {
                            object v = lit.Value;
                            if (v is int i) return new LitExpr { Value = -i, Line = line };
                            if (v is long l) return new LitExpr { Value = -l, Line = line };
                            if (v is float f) return new LitExpr { Value = -f, Line = line };
                            if (v is double d) return new LitExpr { Value = -d, Line = line };
                        }
                        return new UnaryExpr { Op = op, Operand = operand, Line = line };
                    }
                    case "++": case "--":
                    {
                        string op = t[p++].Text;
                        return new UnaryExpr { Op = op, Operand = ParseUnary(), Line = line };
                    }
                    case "(":
                        if (IsCast())
                        {
                            p++;
                            var type = ParseType();
                            while (Accept("&")) ParseType();
                            Expect(")");
                            var operand = IsLambdaStart() ? ParseLambda() : ParseUnary();
                            return new CastExpr { Type = type, Operand = operand, Line = line };
                        }
                        break;
                }
            }
            return ParsePostfix(ParsePrimary());
        }

        bool IsCast()
        {
            int save = p;
            try
            {
                p++; // (
                if (Cur.Kind != TokKind.Ident) return false;
                bool primitive = Primitives.Contains(Cur.Text);
                ParseType();
                while (Accept("&")) ParseType();
                if (!Accept(")")) return false;
                if (primitive) return true;
                var c = Cur;
                if (c.Kind == TokKind.Ident) return !(c.Text == "instanceof");
                if (c.Kind == TokKind.Number || c.Kind == TokKind.String || c.Kind == TokKind.Char) return true;
                if (c.Kind == TokKind.Op && (c.Text == "(" || c.Text == "!" || c.Text == "~")) return true;
                return false;
            }
            catch (JavaSyntaxException) { return false; }
            finally { p = save; }
        }

        Expr ParsePrimary()
        {
            var c = Cur;
            int line = c.Line;
            switch (c.Kind)
            {
                case TokKind.Number: p++; return new LitExpr { Value = c.Value, Line = line };
                case TokKind.String: p++; return new LitExpr { Value = c.Value, Line = line };
                case TokKind.Char: p++; return new LitExpr { Value = c.Value, Line = line };
                case TokKind.Op:
                    if (c.Text == "(")
                    {
                        p++;
                        var e = ParseExpr();
                        Expect(")");
                        return e;
                    }
                    if (c.Text == "@") { SkipAnnotation(); return ParsePrimary(); }
                    throw Error("unexpected operator");
                case TokKind.Ident:
                    switch (c.Text)
                    {
                        case "true": p++; return new LitExpr { Value = true, Line = line };
                        case "false": p++; return new LitExpr { Value = false, Line = line };
                        case "null": p++; return new LitExpr { Value = null, Line = line };
                        case "this":
                        case "super":
                        {
                            p++;
                            Expr self = c.Text == "this" ? (Expr)new ThisExpr { Line = line } : new SuperExpr { Line = line };
                            if (Is("("))
                            {
                                // an explicit constructor call: this(...) / super(...)
                                var ctorCall = new CallExpr { Target = self, Name = "<init>", Line = line };
                                ParseArgs(ctorCall.Args);
                                return ctorCall;
                            }
                            return self;
                        }
                        case "new": return ParseNew();
                        case "switch":
                        {
                            p++;
                            var sw = new SwitchExpr { Line = line };
                            Expect("(");
                            sw.Selector = ParseExpr();
                            Expect(")");
                            sw.Cases = ParseSwitchBody();
                            return sw;
                        }
                    }
                    if (Primitives.Contains(c.Text))
                    {
                        // int.class, float[]::new
                        var type = ParseType();
                        if (Accept("::")) { Ident(); return new MethodRefExpr { Target = new NameExpr { Name = type.Name, Line = line }, Name = "new", Line = line }; }
                        Expect("."); Expect("class");
                        return new ClassLitExpr { Type = type, Line = line };
                    }
                    p++;
                    if (Is("("))
                    {
                        var call = new CallExpr { Target = null, Name = c.Text, Line = line };
                        ParseArgs(call.Args);
                        return call;
                    }
                    return new NameExpr { Name = c.Text, Line = line };
            }
            throw Error("unexpected token");
        }

        Expr ParseNew()
        {
            int line = Cur.Line;
            Expect("new");
            var type = ParseTypeNoDims();
            if (Is("["))
            {
                var ne = new NewExpr { Type = type, Dims = new List<Expr>(), Line = line };
                int dims = 0;
                while (Is("["))
                {
                    p++;
                    if (Accept("]")) { dims++; continue; }
                    ne.Dims.Add(ParseExpr());
                    Expect("]");
                    dims++;
                }
                ne.Type = new TypeRef { Name = type.Name, Args = type.Args, Dims = dims };
                if (Is("{")) ne.Init = ParseArrayInit();
                return ne;
            }
            var n = new NewExpr { Type = type, Line = line };
            ParseArgs(n.Args);
            if (Is("{")) { SkipGroup(); n.HasBody = true; }
            return n;
        }

        TypeRef ParseTypeNoDims()
        {
            var tr = new TypeRef();
            var sb = new StringBuilder(Ident());
            ParseTypeArgsInto(tr);
            while (Is(".") && Peek(1).Kind == TokKind.Ident)
            {
                p++;
                sb.Append('.').Append(Ident());
                ParseTypeArgsInto(tr);
            }
            tr.Name = sb.ToString();
            return tr;
        }

        void ParseArgs(List<Expr> args)
        {
            Expect("(");
            while (!Is(")"))
            {
                args.Add(ParseExpr());
                if (!Accept(",")) break;
            }
            Expect(")");
        }

        Expr ParsePostfix(Expr e)
        {
            while (true)
            {
                int line = Cur.Line;
                if (Is("."))
                {
                    p++;
                    if (Is("<")) SkipTypeParams(); // explicit generic call: List.<T>of()
                    if (Accept("new")) { var ne = (NewExpr)ParseNewAfter(line); e = ne; continue; }
                    if (Accept("class")) { e = new ClassLitExpr { Type = new TypeRef { Name = NameOf(e) }, Line = line }; continue; }
                    if (Accept("this")) { e = new ThisExpr { Line = line }; continue; }
                    string name = Ident();
                    if (Is("("))
                    {
                        var call = new CallExpr { Target = e, Name = name, Line = line };
                        ParseArgs(call.Args);
                        e = call;
                    }
                    else e = new FieldExpr { Target = e, Name = name, Line = line };
                    continue;
                }
                if (Is("::"))
                {
                    p++;
                    string name = Accept("new") ? "new" : Ident();
                    e = new MethodRefExpr { Target = e, Name = name, Line = line };
                    continue;
                }
                if (Is("["))
                {
                    if (IsAt(1, "]"))
                    {
                        // Type[]::new or Type[].class
                        int dims = 0;
                        while (Is("[") && IsAt(1, "]")) { p += 2; dims++; }
                        if (Accept("::")) { Ident(); e = new MethodRefExpr { Target = e, Name = "new", Line = line }; continue; }
                        Expect("."); Expect("class");
                        e = new ClassLitExpr { Type = new TypeRef { Name = NameOf(e), Dims = dims }, Line = line };
                        continue;
                    }
                    p++;
                    var idx = ParseExpr();
                    Expect("]");
                    e = new IndexExpr { Target = e, Index = idx, Line = line };
                    continue;
                }
                if (Is("++") || Is("--"))
                {
                    string op = t[p++].Text;
                    e = new UnaryExpr { Op = op, Operand = e, Postfix = true, Line = line };
                    continue;
                }
                if (Is("<") && e is NameExpr && LooksLikeGenericTypeBeforeMethodRef())
                {
                    // Type<Arg>::new
                    SkipTypeParams();
                    continue;
                }
                return e;
            }
        }

        bool LooksLikeGenericTypeBeforeMethodRef()
        {
            int save = p;
            try { SkipTypeParams(); return Is("::"); }
            catch (JavaSyntaxException) { return false; }
            finally { p = save; }
        }

        Expr ParseNewAfter(int line)
        {
            // outer.new Inner(...)
            var type = ParseTypeNoDims();
            var n = new NewExpr { Type = type, Line = line };
            ParseArgs(n.Args);
            if (Is("{")) { SkipGroup(); n.HasBody = true; }
            return n;
        }

        static string NameOf(Expr e)
        {
            if (e is NameExpr ne) return ne.Name;
            if (e is FieldExpr fe) return NameOf(fe.Target) + "." + fe.Name;
            return "?";
        }

        // ---- entry points for on-demand parsing -------------------------------------------------------

        public static BlockStmt ParseBody(CompilationUnit cu, MethodDecl m)
        {
            if (m.Body != null) return m.Body;
            if (m.BodyStart < 0) return null;
            var ps = new Parser(cu.Tokens, m.BodyStart, m.BodyEnd, cu.Path);
            m.Body = ps.ParseBlockContents();
            if (!ps.AtEnd) throw ps.Error("unparsed tokens at the end of " + m.Name);
            return m.Body;
        }

        public static Expr ParseFieldInit(CompilationUnit cu, FieldDecl f)
        {
            if (f.Init != null) return f.Init;
            if (f.InitStart < 0) return null;
            var ps = new Parser(cu.Tokens, f.InitStart, f.InitEnd, cu.Path);
            f.Init = ps.Is("{") ? ps.ParseArrayInit() : ps.ParseExpr();
            if (!ps.AtEnd) throw ps.Error("unparsed tokens in the initializer of " + f.Name);
            return f.Init;
        }

        public static Expr ParseExpression(string source)
        {
            var tokens = Lexer.Tokenize(source);
            var ps = new Parser(tokens, 0, tokens.Count - 1, "<expr>");
            var e = ps.ParseExpr();
            if (!ps.AtEnd) throw ps.Error("unparsed tokens");
            return e;
        }

        public static BlockStmt ParseStatements(string source)
        {
            var tokens = Lexer.Tokenize(source);
            var ps = new Parser(tokens, 0, tokens.Count - 1, "<stmts>");
            var b = ps.ParseBlockContents();
            if (!ps.AtEnd) throw ps.Error("unparsed tokens");
            return b;
        }

        // Source text of a token range (for reports).
        public static string SourceOf(CompilationUnit cu, int startTok, int endTok)
        {
            var sb = new StringBuilder();
            for (int i = startTok; i < endTok; i++)
            {
                if (i > startTok) sb.Append(' ');
                sb.Append(cu.Tokens[i].Text);
            }
            return sb.ToString();
        }
    }
}
