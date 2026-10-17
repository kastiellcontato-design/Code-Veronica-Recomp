/* Shared HLSL for RGBA8 surfaces reinterpreted as two UNORM16 channels.
 * Decode each stored texel exactly, then filter in float. A second integer
 * quantization is not part of UNORM texture sampling and loses sub-LSB data.
 */
#ifndef RSX_PACKED16_FILTER_H
#define RSX_PACKED16_FILTER_H
#define RSX_PACKED16_FILTER_HLSL \
    "float2 rsx_pair16(float4 v) {\n" \
    "    float4 b = round(saturate(v) * 255.0);\n" \
    "    return float2(b.a * 256.0 + b.r, b.g * 256.0 + b.b);\n" \
    "}\n" \
    "float2 rsx_filter_pair16(Texture2D t, float2 c) {\n" \
    "    uint W, H; t.GetDimensions(W, H);\n" \
    "    float2 p = c * float2(W, H) - 0.5; float2 f = p - floor(p);\n" \
    "    int2 i = (int2)floor(p); int2 hi = int2(W, H) - 1;\n" \
    "    float2 a = rsx_pair16(t.Load(int3(clamp(i, 0, hi), 0)));\n" \
    "    float2 b = rsx_pair16(t.Load(int3(clamp(i + int2(1, 0), 0, hi), 0)));\n" \
    "    float2 d = rsx_pair16(t.Load(int3(clamp(i + int2(0, 1), 0, hi), 0)));\n" \
    "    float2 e = rsx_pair16(t.Load(int3(clamp(i + int2(1, 1), 0, hi), 0)));\n" \
    "    return lerp(lerp(a, b, f.x), lerp(d, e, f.x), f.y) / 65535.0;\n" \
    "}\n"
#endif
