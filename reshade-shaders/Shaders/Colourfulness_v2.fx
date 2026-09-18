// Copyright (c) 2016-2018, bacondither
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the conditions of the license
// are met.
//
// Colourfulness + Vibrance only.
// Based on the original Colourfulness.fx by bacondither.
//
// Controls:
//   Colourfulness - base colour intensity.
//   Vibrance      - adaptive boost for less-saturated colours.

uniform float colourfulness <
    ui_type = "slider";
    ui_min = -1.0; ui_max = 2.0;
    ui_tooltip = "Base colourfulness. 0 = neutral. Positive values increase colour intensity.";
    ui_step = 0.01;
> = 0.40;

uniform float vibrance <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 1.5;
    ui_tooltip = "Adaptive colour boost. Less saturated colours receive more boost.";
    ui_step = 0.01;
> = 0.45;

#ifndef fast_luma
    #define fast_luma 1 // Rapid approx of sRGB gamma, small difference in quality
#endif

#include "ReShade.fxh"

// Smoothly limit colour changes while preserving highlight/shadow headroom.
#define soft_lim(v,s)  ( (v*s)*rcp(sqrt(s*s + v*v)) )

#define wpmean(a,b,w)  ( pow(abs(w)*sqrt(abs(a)) + abs(1-w)*sqrt(abs(b)), 2.0) )

#define maxRGB(c)      ( max((c).r, max((c).g, (c).b)) )
#define minRGB(c)      ( min((c).r, min((c).g, (c).b)) )

#define lumacoeff      float3(0.2558, 0.6511, 0.0931)

float3 Colourfulness(float4 vpos : SV_Position, float2 tex : TEXCOORD) : SV_Target
{
#if (fast_luma == 1)
    float3 c0 = tex2D(ReShade::BackBuffer, tex).rgb;
    float luma = sqrt(dot(saturate(c0 * abs(c0)), lumacoeff));
    c0 = saturate(c0);
#else
    float3 c0 = saturate(tex2D(ReShade::BackBuffer, tex).rgb);
    float luma = pow(dot(pow(c0 + 0.06, 2.4), lumacoeff), 1.0 / 2.4) - 0.06;
#endif

    // Existing saturation is used only to make vibrance adaptive:
    // highly saturated pixels get less additional boost.
    float saturation = maxRGB(c0) - minRGB(c0);
    float satFactor = 1.0 - saturate(saturation);

    // Base colourfulness + adaptive vibrance.
    float adaptiveBoost = colourfulness
                        + (colourfulness * vibrance * satFactor);

    float3 diff_luma = c0 - luma;
    float3 c_diff = diff_luma * adaptiveBoost;

    // Keep the original colourfulness headroom protection.
    if (adaptiveBoost > 0.0)
    {
        float3 rlc_diff = clamp((c_diff * 1.2) + c0, -0.0001, 1.0001) - c0;

        float poslim = (1.0002 - luma) / (abs(maxRGB(diff_luma)) + 0.0001);
        float neglim = (luma + 0.0002) / (abs(minRGB(diff_luma)) + 0.0001);
        float3 diffmax = diff_luma * min(min(poslim, neglim), 32.0) - diff_luma;

        c_diff = soft_lim(
            c_diff,
            max(wpmean(diffmax, rlc_diff, 0.75), 1e-7)
        );
    }

    return saturate(c0 + c_diff);
}

technique ColourfulnessV2
{
    pass
    {
        VertexShader = PostProcessVS;
        PixelShader = Colourfulness;
    }
}
