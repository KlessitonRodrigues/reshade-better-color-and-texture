//////////////////////////////////////////////////////////////////////////////////////////////
//
// DH_Ambient_Remove 0.4.0
//
// This shader is free, if you paid for it, you have been ripped and should ask for a refund.
//
// This shader is developed by AlucardDH (Damien Hembert)
//
// Get more here : https://alucarddh.github.io
// Join my Discord server for news, request, bug reports or help : https://discord.gg/V9HgyBRgMW
//
//////////////////////////////////////////////////////////////////////////////////////////////
#include "Reshade.fxh"

// MACROS /////////////////////////////////////////////////////////////////
#define getColor(c) tex2Dlod(ReShade::BackBuffer,float4((c).xy,0,0))
#define maxOf3(a) max(max(a.x,a.y),a.z)
#define minOf3(a) min(min(a.x,a.y),a.z)
//////////////////////////////////////////////////////////////////////////////

namespace DH_Ambient_Remove_V3 {

// Parameters

/// REMOVE
    uniform float3 cSourceAmbientLightColor <
        ui_type = "color";
        ui_category = "Remove ambient light";
        ui_label = "Source Ambient light color";
    > = float3(31.0,44.0,42.0)/255.0;

    uniform float fSourceAmbientIntensity <
        ui_type = "slider";
        ui_category = "Remove ambient light";
        ui_label = "Strength";
        ui_min = 0; ui_max = 1.0;
        ui_step = 0.001;
    > = 0.750;

    uniform float fContrast <
        ui_type = "slider";
        ui_category = "Realism";
        ui_label = "Contrast";
        ui_min = 0; ui_max = 1.0;
        ui_step = 0.001;
    > = 0.15;

/// ANTI-OVERSATURATION
    uniform float fMaxSaturation <
        ui_type = "slider";
        ui_category = "Anti-oversaturation";
        ui_label = "Max saturation";
        ui_tooltip = "Colors with chroma above this value are compressed back to it.\n"
                     "1.0 disables the saturation cap.";
        ui_min = 0; ui_max = 1.0;
        ui_step = 0.001;
    > = 0.850;

    uniform float fSatGuardStrength <
        ui_type = "slider";
        ui_category = "Anti-oversaturation";
        ui_label = "Guard strength";
        ui_tooltip = "How strongly oversaturated colors are pulled back to the cap.\n"
                     "0 disables the guard.";
        ui_min = 0; ui_max = 1.0;
        ui_step = 0.001;
    > = 0.500;

    uniform float fSatGuardKnee <
        ui_type = "slider";
        ui_category = "Anti-oversaturation";
        ui_label = "Guard knee width";
        ui_tooltip = "Softness of the transition around the saturation cap.\n"
                     "Higher values compress saturation more gradually before the cap.";
        ui_min = 0; ui_max = 0.5;
        ui_step = 0.001;
    > = 0.100;

// FUNCTIONS ////////////////////////////////////////////////////////////////////////

    float3 applyContrast(float3 color) {
        float3 curved = color*color*(3.0-2.0*color);
        return lerp(color, curved, fContrast);
    }

    float3 RGBtoHSV(float3 c) {
        float4 K = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
        float4 p = lerp(float4(c.bg, K.wz), float4(c.gb, K.xy), step(c.b, c.g));
        float4 q = lerp(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r));

        float d = q.x - min(q.w, q.y);
        float e = 1.0e-10;
        return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
    }

    float3 filterAmbiantLight(float3 sourceColor) {
        float3 color = sourceColor;
        float3 colorHSV = RGBtoHSV(color);
        float3 removed = cSourceAmbientLightColor;
        float3 removedTint = removed - minOf3(removed);
        float3 sourceTint = color - minOf3(color);

        float hueDist = maxOf3(abs(removedTint-sourceTint));

        float removal = saturate(1.0-hueDist*saturate(colorHSV.y+colorHSV.z));
        color -= removedTint*removal;
        color = saturate(color);

        color = lerp(sourceColor, color, fSourceAmbientIntensity);

        return color;
    }

    // Limits chroma while preserving luminance, so hues stay intact and only
    // over-saturated colors are pulled back toward the cap.
    float3 guardSaturation(float3 color) {
        float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
        float3 chroma = color - luma;
        float sat = maxOf3(abs(chroma));

        // Chroma level above which compression starts (soft knee around the cap)
        float kneeStart = max(fMaxSaturation - fSatGuardKnee, 0.0);
        float kneeRange = max(fMaxSaturation - kneeStart, 1.0e-5);

        // 0 below the knee, rising smoothly to 1 at the cap
        float t = saturate((sat - kneeStart) / kneeRange);
        float compress = t * t * (3.0 - 2.0 * t);

        // Target chroma: untouched below the knee, capped at fMaxSaturation above it
        float targetSat = min(sat, lerp(sat, fMaxSaturation, compress));
        float factor = sat > 1.0e-5 ? targetSat / sat : 1.0;

        return luma + chroma * lerp(1.0, factor, fSatGuardStrength);
    }

    void PS_Filter(in float4 position : SV_Position, in float2 coords : TEXCOORD, out float4 outColor : SV_Target) {
        float4 color = getColor(coords);
        color.rgb = applyContrast(filterAmbiantLight(color.rgb));
        color.rgb = guardSaturation(color.rgb);
        outColor = saturate(color);
    }

// TECHNIQUES

    technique DH_Ambient_Remove_V3 <
        ui_label = "DH_Ambient_Remove V3 (contrast + saturation guard)";
        ui_tooltip =
            "_____________ DH_Ambient_Remove _____________\n"
            "\n"
            "         version 0.4.0 by AlucardDH\n"
            "\n"
            "_____________________________________________";
    > {
        pass {
            VertexShader = PostProcessVS;
            PixelShader = PS_Filter;
        }
    }
}
