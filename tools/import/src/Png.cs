// GloamingForest import tool (contract "import", unit F1): PNG reading and a deterministic PNG writer
// (own DEFLATE encoder: the platform zlib differs between .NET versions, and -Check needs the same
// bytes everywhere).
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Text;

namespace GfImport
{
    public sealed class Image
    {
        public int W, H;
        public uint[] Px; // 0xAARRGGBB

        public Image(int w, int h) { W = w; H = h; Px = new uint[w * h]; }
        public uint Get(int x, int y) { return Px[y * W + x]; }
        public void Set(int x, int y, uint c) { Px[y * W + x] = c; }
    }

    public static class Png
    {
        static readonly byte[] Signature = { 137, 80, 78, 71, 13, 10, 26, 10 };

        /// <summary>Width and height from the IHDR chunk, without decoding.</summary>
        public static bool TrySize(byte[] data, out int w, out int h)
        {
            w = h = 0;
            if (data.Length < 24) return false;
            for (int i = 0; i < 8; i++) if (data[i] != Signature[i]) return false;
            w = BigEndian(data, 16);
            h = BigEndian(data, 20);
            return true;
        }

        static int BigEndian(byte[] d, int o) { return (d[o] << 24) | (d[o + 1] << 16) | (d[o + 2] << 8) | d[o + 3]; }

        public static Image Decode(byte[] data)
        {
            for (int i = 0; i < 8; i++) if (data[i] != Signature[i]) throw new Exception("not a PNG");
            int pos = 8, w = 0, h = 0, depth = 0, ctype = 0, interlace = 0;
            byte[] palette = null, trns = null;
            var idat = new MemoryStream();
            while (pos + 8 <= data.Length)
            {
                int len = BigEndian(data, pos);
                string type = Encoding.ASCII.GetString(data, pos + 4, 4);
                int body = pos + 8;
                if (type == "IHDR")
                {
                    w = BigEndian(data, body); h = BigEndian(data, body + 4);
                    depth = data[body + 8]; ctype = data[body + 9]; interlace = data[body + 12];
                }
                else if (type == "PLTE") { palette = new byte[len]; Array.Copy(data, body, palette, 0, len); }
                else if (type == "tRNS") { trns = new byte[len]; Array.Copy(data, body, trns, 0, len); }
                else if (type == "IDAT") idat.Write(data, body, len);
                else if (type == "IEND") break;
                pos = body + len + 4;
            }
            byte[] raw;
            idat.Position = 0;
            using (var z = new ZLibStream(idat, CompressionMode.Decompress))
            using (var o = new MemoryStream()) { z.CopyTo(o); raw = o.ToArray(); }

            int channels = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : 4;
            var img = new Image(w, h);
            int rp = 0;
            if (interlace == 0)
            {
                DecodePass(raw, ref rp, img, 0, 0, 1, 1, w, h, channels, ctype, depth, palette, trns);
                return img;
            }
            // Adam7
            int[] sx = { 0, 4, 0, 2, 0, 1, 0 }, sy = { 0, 0, 4, 0, 2, 0, 1 }, dx = { 8, 8, 4, 4, 2, 2, 1 }, dy = { 8, 8, 8, 4, 4, 2, 2 };
            for (int p = 0; p < 7; p++)
            {
                int pw = (w - sx[p] + dx[p] - 1) / dx[p], ph = (h - sy[p] + dy[p] - 1) / dy[p];
                if (pw <= 0 || ph <= 0) continue;
                DecodePass(raw, ref rp, img, sx[p], sy[p], dx[p], dy[p], pw, ph, channels, ctype, depth, palette, trns);
            }
            return img;
        }

        static void DecodePass(byte[] raw, ref int rp, Image img, int x0, int y0, int dx, int dy, int pw, int ph, int channels, int ctype, int depth, byte[] palette, byte[] trns)
        {
            int bpp = Math.Max(1, channels * depth / 8);
            int stride = (pw * channels * depth + 7) / 8;
            var prev = new byte[stride];
            var cur = new byte[stride];
            for (int y = 0; y < ph; y++)
            {
                int filter = raw[rp++];
                Array.Copy(raw, rp, cur, 0, stride);
                rp += stride;
                for (int i = 0; i < stride; i++)
                {
                    int a = i >= bpp ? cur[i - bpp] : 0, b = prev[i], c = i >= bpp ? prev[i - bpp] : 0;
                    int v = cur[i];
                    switch (filter)
                    {
                        case 1: v += a; break;
                        case 2: v += b; break;
                        case 3: v += (a + b) >> 1; break;
                        case 4: v += Paeth(a, b, c); break;
                    }
                    cur[i] = (byte)v;
                }
                for (int x = 0; x < pw; x++) img.Set(x0 + x * dx, y0 + y * dy, Pixel(cur, x, ctype, depth, palette, trns));
                var t = prev; prev = cur; cur = t;
            }
        }

        static int Paeth(int a, int b, int c)
        {
            int p = a + b - c, pa = Math.Abs(p - a), pb = Math.Abs(p - b), pc = Math.Abs(p - c);
            return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
        }

        static int Sample(byte[] row, int index, int depth)
        {
            if (depth == 8) return row[index];
            if (depth == 16) return row[index * 2]; // high byte
            int perByte = 8 / depth;
            int b = row[index / perByte];
            int shift = 8 - depth * (index % perByte + 1);
            return (b >> shift) & ((1 << depth) - 1);
        }

        static int Scale(int v, int depth) { return depth >= 8 ? v : v * 255 / ((1 << depth) - 1); }

        static uint Pixel(byte[] row, int x, int ctype, int depth, byte[] pal, byte[] trns)
        {
            int r, g, b, a = 255;
            switch (ctype)
            {
                case 0:
                    {
                        int raw = Sample(row, x, depth);
                        r = g = b = Scale(raw, depth);
                        if (trns != null && trns.Length >= 2 && ((trns[0] << 8) | trns[1]) == raw) a = 0;
                        break;
                    }
                case 2:
                    r = Sample(row, x * 3, depth); g = Sample(row, x * 3 + 1, depth); b = Sample(row, x * 3 + 2, depth);
                    // tRNS: the one RGB colour (16-bit samples, compared at the image depth) that is transparent
                    if (trns != null && trns.Length >= 6)
                    {
                        int tr = (trns[0] << 8) | trns[1], tg = (trns[2] << 8) | trns[3], tb = (trns[4] << 8) | trns[5];
                        if (depth == 16 ? (r == tr >> 8 && g == tg >> 8 && b == tb >> 8) : (r == tr && g == tg && b == tb)) a = 0;
                    }
                    break;
                case 3:
                    {
                        int i = Sample(row, x, depth);
                        r = pal[i * 3]; g = pal[i * 3 + 1]; b = pal[i * 3 + 2];
                        if (trns != null && i < trns.Length) a = trns[i];
                        break;
                    }
                case 4:
                    r = g = b = Sample(row, x * 2, depth); a = Sample(row, x * 2 + 1, depth);
                    break;
                default:
                    r = Sample(row, x * 4, depth); g = Sample(row, x * 4 + 1, depth); b = Sample(row, x * 4 + 2, depth); a = Sample(row, x * 4 + 3, depth);
                    break;
            }
            return ((uint)a << 24) | ((uint)r << 16) | ((uint)g << 8) | (uint)b;
        }

        // ---- writer --------------------------------------------------------------------------------

        public static byte[] Encode(Image img)
        {
            int stride = img.W * 4;
            var raw = new byte[(stride + 1) * img.H];
            var prevRow = new byte[stride];
            var row = new byte[stride];
            var best = new byte[stride];
            int o = 0;
            for (int y = 0; y < img.H; y++)
            {
                for (int x = 0; x < img.W; x++)
                {
                    uint c = img.Get(x, y);
                    row[x * 4] = (byte)(c >> 16); row[x * 4 + 1] = (byte)(c >> 8); row[x * 4 + 2] = (byte)c; row[x * 4 + 3] = (byte)(c >> 24);
                }
                // adaptive filter: the smallest sum of absolute residuals (ties: lowest filter type)
                int bestFilter = 0; long bestSum = long.MaxValue;
                var tmp = new byte[stride];
                for (int f = 0; f < 5; f++)
                {
                    long sum = 0;
                    for (int i = 0; i < stride; i++)
                    {
                        int a = i >= 4 ? row[i - 4] : 0, b = prevRow[i], c = i >= 4 ? prevRow[i - 4] : 0;
                        int p = f == 0 ? 0 : f == 1 ? a : f == 2 ? b : f == 3 ? (a + b) >> 1 : Paeth(a, b, c);
                        byte v = (byte)(row[i] - p);
                        tmp[i] = v;
                        sum += v < 128 ? v : 256 - v;
                    }
                    if (sum < bestSum) { bestSum = sum; bestFilter = f; Array.Copy(tmp, best, stride); }
                }
                raw[o++] = (byte)bestFilter;
                Array.Copy(best, 0, raw, o, stride);
                o += stride;
                var t = prevRow; prevRow = row; row = t;
            }
            var ms = new MemoryStream();
            ms.Write(Signature, 0, 8);
            var ihdr = new byte[13];
            PutBE(ihdr, 0, img.W); PutBE(ihdr, 4, img.H);
            ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
            WriteChunk(ms, "IHDR", ihdr);
            WriteChunk(ms, "IDAT", Zlib(raw));
            WriteChunk(ms, "IEND", new byte[0]);
            return ms.ToArray();
        }

        static void PutBE(byte[] d, int o, int v) { d[o] = (byte)(v >> 24); d[o + 1] = (byte)(v >> 16); d[o + 2] = (byte)(v >> 8); d[o + 3] = (byte)v; }

        static void WriteChunk(Stream s, string type, byte[] body)
        {
            var len = new byte[4]; PutBE(len, 0, body.Length);
            s.Write(len, 0, 4);
            var t = Encoding.ASCII.GetBytes(type);
            s.Write(t, 0, 4);
            s.Write(body, 0, body.Length);
            uint crc = Crc32(t, 0xFFFFFFFFu);
            crc = Crc32(body, crc) ^ 0xFFFFFFFFu;
            var c = new byte[4]; PutBE(c, 0, (int)crc);
            s.Write(c, 0, 4);
        }

        static uint[] crcTable;
        static uint Crc32(byte[] data, uint crc)
        {
            if (crcTable == null)
            {
                crcTable = new uint[256];
                for (uint n = 0; n < 256; n++)
                {
                    uint c = n;
                    for (int k = 0; k < 8; k++) c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                    crcTable[n] = c;
                }
            }
            foreach (byte b in data) crc = crcTable[(crc ^ b) & 0xFF] ^ (crc >> 8);
            return crc;
        }

        // zlib stream with one fixed-Huffman DEFLATE block (greedy LZ77, hash chains).
        static byte[] Zlib(byte[] data)
        {
            var w = new BitWriter();
            w.Bytes.Add(0x78); w.Bytes.Add(0x01);
            w.Bits(1, 1); // BFINAL
            w.Bits(1, 2); // BTYPE = fixed Huffman
            const int WindowSize = 32768, MaxChain = 64, HashBits = 15;
            var head = new int[1 << HashBits];
            var prev = new int[data.Length];
            for (int i = 0; i < head.Length; i++) head[i] = -1;
            int pos = 0;
            Func<int, int> hashAt = p => ((data[p] << 10) ^ (data[p + 1] << 5) ^ data[p + 2]) & ((1 << HashBits) - 1);
            Action<int> insert = p =>
            {
                if (p + 2 >= data.Length) return;
                int hsh = hashAt(p);
                prev[p] = head[hsh];
                head[hsh] = p;
            };
            while (pos < data.Length)
            {
                int bestLen = 0, bestDist = 0;
                if (pos + 2 < data.Length)
                {
                    int cand = head[hashAt(pos)];
                    int chain = 0;
                    while (cand >= 0 && pos - cand <= WindowSize && chain++ < MaxChain)
                    {
                        int l = 0;
                        while (l < 258 && pos + l < data.Length && data[cand + l] == data[pos + l]) l++;
                        if (l > bestLen) { bestLen = l; bestDist = pos - cand; if (l == 258) break; }
                        cand = prev[cand];
                    }
                }
                if (bestLen >= 3)
                {
                    WriteLength(w, bestLen);
                    WriteDistance(w, bestDist);
                    for (int i = 0; i < bestLen; i++) insert(pos + i);
                    pos += bestLen;
                }
                else
                {
                    WriteLiteral(w, data[pos]);
                    insert(pos);
                    pos++;
                }
            }
            WriteLiteral(w, 256);
            w.Flush();
            uint a = 1, b = 0;
            foreach (byte x in data) { a = (a + x) % 65521; b = (b + a) % 65521; }
            uint adler = (b << 16) | a;
            w.Bytes.Add((byte)(adler >> 24)); w.Bytes.Add((byte)(adler >> 16)); w.Bytes.Add((byte)(adler >> 8)); w.Bytes.Add((byte)adler);
            return w.Bytes.ToArray();
        }

        static readonly int[] LenBase = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
        static readonly int[] LenExtra = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
        static readonly int[] DistBase = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
        static readonly int[] DistExtra = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

        static void WriteLiteral(BitWriter w, int sym)
        {
            if (sym < 144) w.Code(0x30 + sym, 8);
            else if (sym < 256) w.Code(0x190 + sym - 144, 9);
            else if (sym < 280) w.Code(sym - 256, 7);
            else w.Code(0xC0 + sym - 280, 8);
        }

        static void WriteLength(BitWriter w, int len)
        {
            int i = LenBase.Length - 1;
            while (LenBase[i] > len) i--;
            WriteLiteral(w, 257 + i);
            if (LenExtra[i] > 0) w.Bits(len - LenBase[i], LenExtra[i]);
        }

        static void WriteDistance(BitWriter w, int dist)
        {
            int i = DistBase.Length - 1;
            while (DistBase[i] > dist) i--;
            w.Code(i, 5);
            if (DistExtra[i] > 0) w.Bits(dist - DistBase[i], DistExtra[i]);
        }

        sealed class BitWriter
        {
            public readonly List<byte> Bytes = new List<byte>();
            uint acc; int n;
            /// <summary>Raw value, least significant bit first (header fields, extra bits).</summary>
            public void Bits(int value, int count)
            {
                for (int i = 0; i < count; i++) Put((value >> i) & 1);
            }
            /// <summary>Huffman code, most significant bit first.</summary>
            public void Code(int code, int length)
            {
                for (int i = length - 1; i >= 0; i--) Put((code >> i) & 1);
            }
            void Put(int bit)
            {
                acc |= (uint)bit << n;
                if (++n == 8) { Bytes.Add((byte)acc); acc = 0; n = 0; }
            }
            public void Flush() { if (n > 0) { Bytes.Add((byte)acc); acc = 0; n = 0; } }
        }
    }
}
