// Copyright (c) 2016-2018, bacondither
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the conditions of the license
// are met.
//
// Colourfulness V3 - natural palette limiter.
// Based on Colourfulness + Vibrance (Colourfulness_v2.fx) by bacondither.
//
// Instead of only pushing colours apart, V3 compresses highly saturated
// pixels toward a believable saturation ceiling. Muted colours stay largely
// untouched, while neon and oversaturated colours are pulled back into a
// range that reads as roughly real-world. Hue and luma are kept as stable as
// possible so the image keeps its character without looking washed out.
//
// Controls:
//   Vibrance       - gentle lift for muted colours.
//   Max Saturation - the "real life" saturation ceiling.
//   Softness       - how gradually oversaturated colours are compressed.
//   Strength       - blend between the source and the limited image.

uniform float vibrance <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 1.0;
    ui_step = 0.01;
    ui_label = "Vibrance";
    ui_tooltip = "Gentle boost for muted colours. Does not raise the saturation ceiling.";
> = 0.15;

uniform float max_saturation <
    ui_type = "slider";
    ui_min = 0.1; ui_max = 1.0;
    ui_step = 0.01;
    ui_label = "Max Saturation";
    ui_tooltip = "Saturation ceiling treated as the limit of realistic colour.";
> = 0.62;

uniform float softness <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 1.0;
    ui_step = 0.01;
    ui_label = "Softness";
    ui_tooltip = "How early and gradually oversaturated colours are compressed. 0 = hard ceiling.";
> = 0.55;

uniform float strength <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 1.0;
    ui_step = 0.01;
    ui_label = "Strength";
    ui_tooltip = "Blend between the original image and the palette-limited result.";
> = 1.0;

#ifndef fast_luma
    #define fast_luma 1 // Rapid approx of sRGB gamma, small difference in quality
#endif

#include "ReShade.fxh"

#define maxRGB(c)      ( max((c).r, max((c).g, (c).b)) )
#define minRGB(c)      ( min((c).r, min((c).g, (c).b)) )

#define lumacoeff      float3(0.2558, 0.6511, 0.0931)

// Identity below the knee, approaching maxValue asymptotically above it.
float softCeiling(float v, float knee, float maxValue)
{
    float range = max(maxValue - knee, 1e-5);
    return (v <= knee) ? v : knee + range * (1.0 - exp(-(v - knee) / range));
}

float3 Colourfulness(float4 vpos : SV_Position, float2 tex : TEXCOORD) : SV_Target
{
#if (fast_luma == 1)
    float3 c0 = saturate(tex2D(ReShade::BackBuffer, tex).rgb);
    float luma = sqrt(dot(c0 * c0, lumacoeff));
#else
    float3 c0 = saturate(tex2D(ReShade::BackBuffer, tex).rgb);
    float luma = pow(dot(pow(c0 + 0.06, 2.4), lumacoeff), 1.0 / 2.4) - 0.06;
#endif

    float3 chroma = c0 - luma;
    float saturation = maxRGB(chroma) - minRGB(chroma);

    if (saturation <= 1e-5)
        return c0;

    float muted = 1.0 - saturate(saturation);
    float target = saturation * (1.0 + vibrance * muted * muted);

    float knee = max_saturation * (1.0 - softness);
    target = softCeiling(target, knee, max_saturation);

    float3 limited = luma + chroma * (target / saturation);

    return saturate(lerp(c0, limited, strength));
}

technique ColourfulnessV3 <
    ui_label = "Colourfulness V3 (natural palette)";
    ui_tooltip =
        "_____________ Colourfulness V3 _____________\n"
        "\n"
        "        natural palette limiter\n"
        "\n"
        "____________________________________________";
>
{
    pass
    {
        VertexShader = PostProcessVS;
        PixelShader = Colourfulness;
    }
}
