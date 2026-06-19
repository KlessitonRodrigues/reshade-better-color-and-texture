////////////////////////////////////////////////////////////////////////////////////////////////
//
// DH_Ambient_Remove 0.3.0
//
// This shader is free, if you paid for it, you have been ripped and should ask for a refund.
//
// This shader is developed by AlucardDH (Damien Hembert)
//
// Get more here : https://alucarddh.github.io
// Join my Discord server for news, request, bug reports or help : https://discord.gg/V9HgyBRgMW
//
////////////////////////////////////////////////////////////////////////////////////////////////
#include "Reshade.fxh"

// MACROS /////////////////////////////////////////////////////////////////
#define getColor(c) tex2Dlod(ReShade::BackBuffer,float4((c).xy,0,0))
#define maxOf3(a) max(max(a.x,a.y),a.z)
#define minOf3(a) min(min(a.x,a.y),a.z)
//////////////////////////////////////////////////////////////////////////////

namespace DH_Ambient_Remove_030 {

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

// FUNCTIONS ////////////////////////////////////////////////////////////////////////

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

    void PS_Filter(in float4 position : SV_Position, in float2 coords : TEXCOORD, out float4 outColor : SV_Target) {
        float4 color = getColor(coords);
        color.rgb = filterAmbiantLight(color.rgb);
        outColor = color;
    }

// TECHNIQUES

    technique DH_Ambient_Remove <
        ui_label = "DH_Ambient_Remove 0.3.0";
        ui_tooltip =
            "_____________ DH_Ambient_Remove _____________\n"
            "\n"
            "         version 0.3.0 by AlucardDH\n"
            "\n"
            "_____________________________________________";
    > {
        pass {
            VertexShader = PostProcessVS;
            PixelShader = PS_Filter;
        }
    }
}
