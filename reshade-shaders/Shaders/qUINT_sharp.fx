/*=============================================================================

    ReShade 4 effect file
    github.com/martymcmodding

	Support me:
   		paypal.me/mcflypg
   		patreon.com/mcflypg

    Depth enhanced local contrast sharpen

    by Marty McFly / P.Gilcher
    part of qUINT shader library for ReShade 4

    Copyright (c) Pascal Gilcher / Marty McFly. All rights reserved.

=============================================================================*/

/*=============================================================================
	UI Uniforms
=============================================================================*/

uniform float SHARP_STRENGTH <
    ui_type = "slider";
    ui_label = "Sharpen Strength";
    ui_min = 0.0;
    ui_max = 1.0;
> = 0.7;

uniform bool RMS_MASK_ENABLE <
    ui_label = "Use Local Contrast Enhancer";
> = true;

/*=============================================================================
	Textures, Samplers, Globals
=============================================================================*/

#define RESHADE_QUINT_COMMON_VERSION_REQUIRE    202
#include "qUINT_common.fxh"

/*=============================================================================
	Vertex Shader
=============================================================================*/

struct VSOUT
{
	float4                  vpos        : SV_Position;
    float2                  uv          : TEXCOORD0;
};

VSOUT VS_Sharp(in uint id : SV_VertexID)
{
    VSOUT o;

    o.uv.x = (id == 2) ? 2.0 : 0.0;
    o.uv.y = (id == 1) ? 2.0 : 0.0;

    o.vpos = float4(o.uv.xy * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

/*=============================================================================
	Functions
=============================================================================*/

float color_to_lum(float3 color)
{
    return dot(color, float3(0.3, 0.59, 0.11));
}

float3 blend_overlay(float3 a, float3 b)
{
    float3 c = 1.0 - a;
    float3 d = 1.0 - b;

    return a < 0.5 ? (2.0 * a * b) : (1.0 - 2.0 * c * d);
}

float3 fetch_color(float2 uv)
{
    return tex2D(qUINT::sBackBufferTex, uv).rgb;
}

/*=============================================================================
	Pixel Shaders
=============================================================================*/

void PS_Sharp(in VSOUT i, out float3 o : SV_Target0)
{
    //  A B C
    //  D E F
    //  G H I

    float3 A, B, C, D, E, F, G, H, I;

    float3 offsets = float3(1, 0, -1);

    A = fetch_color(i.uv + offsets.zz * qUINT::PIXEL_SIZE);
    B = fetch_color(i.uv + offsets.yz * qUINT::PIXEL_SIZE);
    C = fetch_color(i.uv + offsets.xz * qUINT::PIXEL_SIZE);
    D = fetch_color(i.uv + offsets.zy * qUINT::PIXEL_SIZE);
    E = fetch_color(i.uv + offsets.yy * qUINT::PIXEL_SIZE);
    F = fetch_color(i.uv + offsets.xy * qUINT::PIXEL_SIZE);
    G = fetch_color(i.uv + offsets.zx * qUINT::PIXEL_SIZE);
    H = fetch_color(i.uv + offsets.yx * qUINT::PIXEL_SIZE);
    I = fetch_color(i.uv + offsets.xx * qUINT::PIXEL_SIZE);

    float3 corners    = (A + C) + (G + I);
    float3 neighbours = (B + D) + (F + H);
    float3 center     = E;

    float3 edge    = corners + 2.0 * neighbours - 12.0 * center;
    float3 sharpen = edge;

    //measures root mean square as local contrast measurement
    //to adjust the intensity of the sharpener at edges with
    //high local contrast to restrict sharpen to texture detail
    //only while leaving object and detail outlines mostly alone.
    [branch]
    if(RMS_MASK_ENABLE)
    {
        float3 mean = (corners + neighbours + center) / 9.0;
        float3 RMS = (mean - A) * (mean - A);
        RMS += (mean - B) * (mean - B);
        RMS += (mean - C) * (mean - C);
        RMS += (mean - D) * (mean - D);
        RMS += (mean - E) * (mean - E);
        RMS += (mean - F) * (mean - F);
        RMS += (mean - G) * (mean - G);
        RMS += (mean - H) * (mean - H);
        RMS += (mean - I) * (mean - I);

        sharpen *= rsqrt(RMS + 0.001) * 0.1;
    }

    sharpen = color_to_lum(sharpen);

    sharpen = -sharpen * SHARP_STRENGTH * 0.1;
    //smooth falloff, cheaper than pow() with little error
    sharpen = sign(sharpen) * log(abs(sharpen) * 10.0 + 1.0)*0.3;

    o = blend_overlay(center, (0.5 + sharpen));
}

/*=============================================================================
	Techniques
=============================================================================*/

technique DELC_Sharpen
< ui_tooltip = "                     >> qUINT::DELCS <<\n\n"
			   "DELCS is an advanced sharpen filter made to enhance texture detail.\n"
               "It offers a local contrast detection method and allows to suppress\n"
               "oversharpening on depth edges to combat common sharpen artifacts.\n"
               "get access to more functionality.\n"
               "\nDELCS is best positioned after most shaders but before film grain or such.\n"
               "\DELCS is written by Marty McFly / Pascal Gilcher"; >
{
    pass
	{
		VertexShader = VS_Sharp;
		PixelShader  = PS_Sharp;
	}
}
