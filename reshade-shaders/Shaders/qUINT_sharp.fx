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

/*
	changelog:

	2.0.0:	added frame count parameter
			added versioning system
			removed common textures - should only be declared if needed
			flipped reversed depth buffer switch by default as most games use this format
	2.0.1:  added more depth scaling parameters to match ReShade.fxh
	2.0.2:  added UI for depth linearization when loaded by NVIDIA FreeStyle/Ansel
			with cue given by effect if depth is required
	2.0.3:  fixed scaling (  "-define" resulted in "--define" if define was negative)
			split depth buffer retrieval into 2 functions, so that nonlinearized
			depth can also be taken and a given depth be linearized manually
	2.0.4:  splitted get_depth function into submodules that correct UV scaling and alignment
			and a function that samples depth using this corrected UV
	2.0.5:  renamed linear_depth(depth) to linearize_depth(depth). Polymorphism is cool, but unintuitive.
			Perfect time to do this change now as no filter uses that function in this way yet.
	2.0.6:  added new pixel offsets to depth buffer and reorganized the existing defines
*/

/*=============================================================================
	Version checks
=============================================================================*/

#ifndef RESHADE_QUINT_COMMON_VERSION
 #define RESHADE_QUINT_COMMON_VERSION 206
#endif

#if RESHADE_QUINT_COMMON_VERSION_REQUIRE > RESHADE_QUINT_COMMON_VERSION
 #error "qUINT_common.fxh outdated."
 #error "Please download update from github.com/martymcmodding/qUINT"
#endif

#if !defined(RESHADE_QUINT_COMMON_VERSION_REQUIRE)
 #error "Incompatible qUINT_common.fxh and shaders."
 #error "Do not mix different file versions."
#endif

#if !defined(__RESHADE__) || __RESHADE__ < 40000
	#error "ReShade 4.4+ is required to use this header file"
#endif

/*=============================================================================
	Define defaults
=============================================================================*/

//depth buffer
#ifndef RESHADE_DEPTH_INPUT_IS_UPSIDE_DOWN
	#define RESHADE_DEPTH_INPUT_IS_UPSIDE_DOWN 0
#endif
#ifndef RESHADE_DEPTH_INPUT_IS_REVERSED
	#define RESHADE_DEPTH_INPUT_IS_REVERSED 1
#endif
#ifndef RESHADE_DEPTH_INPUT_IS_LOGARITHMIC
	#define RESHADE_DEPTH_INPUT_IS_LOGARITHMIC 0
#endif
#ifndef RESHADE_DEPTH_LINEARIZATION_FAR_PLANE
	#define RESHADE_DEPTH_LINEARIZATION_FAR_PLANE 1000.0
#endif

//new compatibility flags
#ifndef RESHADE_DEPTH_MULTIPLIER
	#define RESHADE_DEPTH_MULTIPLIER 1	//mcfly: probably not a good idea, many shaders depend on having depth range 0-1
#endif
#ifndef RESHADE_DEPTH_INPUT_X_SCALE
	#define RESHADE_DEPTH_INPUT_X_SCALE 1
#endif
#ifndef RESHADE_DEPTH_INPUT_Y_SCALE
	#define RESHADE_DEPTH_INPUT_Y_SCALE 1
#endif
// An offset to add to the X coordinate, (+) = move right, (-) = move left
#ifndef RESHADE_DEPTH_INPUT_X_OFFSET
	#define RESHADE_DEPTH_INPUT_X_OFFSET 0
#endif
// An offset to add to the Y coordinate, (+) = move up, (-) = move down
#ifndef RESHADE_DEPTH_INPUT_Y_OFFSET
	#define RESHADE_DEPTH_INPUT_Y_OFFSET 0
#endif
// An offset to add to the X coordinate, (+) = move right, (-) = move left
#ifndef RESHADE_DEPTH_INPUT_X_PIXEL_OFFSET
	#define RESHADE_DEPTH_INPUT_X_PIXEL_OFFSET 0
#endif
// An offset to add to the Y coordinate, (+) = move up, (-) = move down
#ifndef RESHADE_DEPTH_INPUT_Y_PIXEL_OFFSET
	#define RESHADE_DEPTH_INPUT_Y_PIXEL_OFFSET 0
#endif

/*=============================================================================
	Depth UI
=============================================================================*/

#if defined(__RESHADE_FXC__)
//if using FreeStyle or Ansel and effect requires depth, make UI toggle
//available. If not, replace with dummy which is unused anyways.
#if defined(RESHADE_QUINT_EFFECT_DEPTH_REQUIRE)
	uniform bool UI_RESHADE_DEPTH_INPUT_IS_REVERSED <
	    ui_type = "bool";
	    ui_label = "Depth input is reversed";
	> = RESHADE_DEPTH_INPUT_IS_REVERSED; //use default preprocessor setting
#else
 	#define UI_RESHADE_DEPTH_INPUT_IS_REVERSED RESHADE_DEPTH_INPUT_IS_REVERSED
#endif

#endif

/*=============================================================================
	Uniforms
=============================================================================*/

namespace qUINT
{
    uniform float FRAME_TIME < source = "frametime"; >;
	uniform int FRAME_COUNT < source = "framecount"; >;

#if defined(__RESHADE_FXC__)
	float2 get_aspect_ratio() 	{ return float2(1.0, BUFFER_WIDTH * BUFFER_RCP_HEIGHT); }
	float2 get_pixel_size() 	{ return float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT); }
	float2 get_screen_size() 	{ return float2(BUFFER_WIDTH, BUFFER_HEIGHT); }
	#define ASPECT_RATIO 		get_aspect_ratio()
	#define PIXEL_SIZE 			get_pixel_size()
	#define SCREEN_SIZE 		get_screen_size()
#else
    static const float2 ASPECT_RATIO 	= float2(1.0, BUFFER_WIDTH * BUFFER_RCP_HEIGHT);
	static const float2 PIXEL_SIZE 		= float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT);
	static const float2 SCREEN_SIZE 	= float2(BUFFER_WIDTH, BUFFER_HEIGHT);
#endif

	// Global textures and samplers
	texture BackBufferTex : COLOR;
	texture DepthBufferTex : DEPTH;

	sampler sBackBufferTex 	{ Texture = BackBufferTex; 	};
	sampler sDepthBufferTex { Texture = DepthBufferTex; };

	float2 depthtex_uv(float2 uv)
	{
#if RESHADE_DEPTH_INPUT_IS_UPSIDE_DOWN
        uv.y = 1.0 - uv.y;
#endif
        uv.x /= RESHADE_DEPTH_INPUT_X_SCALE;
		uv.y /= RESHADE_DEPTH_INPUT_Y_SCALE;
#if RESHADE_DEPTH_INPUT_X_PIXEL_OFFSET
		uv.x -= RESHADE_DEPTH_INPUT_X_PIXEL_OFFSET * BUFFER_RCP_WIDTH;
#else // Do not check RESHADE_DEPTH_INPUT_X_OFFSET, since it may be a decimal number, which the preprocessor cannot handle
		uv.x -= RESHADE_DEPTH_INPUT_X_OFFSET / 2.000000001;
#endif
#if RESHADE_DEPTH_INPUT_Y_PIXEL_OFFSET
		uv.y += RESHADE_DEPTH_INPUT_Y_PIXEL_OFFSET * BUFFER_RCP_HEIGHT;
#else
		uv.y += RESHADE_DEPTH_INPUT_Y_OFFSET / 2.000000001;
#endif
        return uv;
	}

	float get_depth(float2 uv)
	{
        float depth = tex2Dlod(sDepthBufferTex, float4(depthtex_uv(uv), 0, 0)).x;
        return depth;
	}

	float linearize_depth(float depth)
	{
	    depth *= RESHADE_DEPTH_MULTIPLIER;
#if RESHADE_DEPTH_INPUT_IS_LOGARITHMIC
	    const float C = 0.01;
	    depth = (exp(depth * log(C + 1.0)) - 1.0) / C;
#endif
#if defined(__RESHADE_FXC__)
	    depth = UI_RESHADE_DEPTH_INPUT_IS_REVERSED ? 1.0 - depth : depth;
#else
#if RESHADE_DEPTH_INPUT_IS_REVERSED
	    depth = 1.0 - depth;
#endif
#endif
	    const float N = 1.0;
	    depth /= RESHADE_DEPTH_LINEARIZATION_FAR_PLANE - depth * (RESHADE_DEPTH_LINEARIZATION_FAR_PLANE - N);

	    return saturate(depth);
	}

	//standard linear depth fetch
	float linear_depth(float2 uv)
	{
	    float depth = get_depth(uv);
	    depth = linearize_depth(depth);
	    return depth;
	}
}

// Vertex shader generating a triangle covering the entire screen
void PostProcessVS(in uint id : SV_VertexID, out float4 vpos : SV_Position, out float2 uv : TEXCOORD)
{
	uv.x = (id == 2) ? 2.0 : 0.0;
	uv.y = (id == 1) ? 2.0 : 0.0;
	vpos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

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
