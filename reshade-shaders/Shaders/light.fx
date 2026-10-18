// ============================================================================
// LEVELS SHADER (CeeJay.dk, MIT License)
// ============================================================================
/**
 * Levels version 1.2
 * by Christian Cann Schuldt Jensen ~ CeeJay.dk
 * License: MIT
 */

#include "ReShade.fxh"

uniform int BlackPoint <
	ui_type = "slider";
	ui_min = 0; ui_max = 255;
	ui_label = "Black Point";
	ui_tooltip = "The black point is the new black - literally. Everything darker than this will become completely black.";
> = 16;

uniform int WhitePoint <
	ui_type = "slider";
	ui_min = 0; ui_max = 255;
	ui_label = "White Point";
	ui_tooltip = "The new white point. Everything brighter than this becomes completely white";
> = 235;

uniform bool HighlightClipping <
	ui_label = "Highlight clipping pixels";
	ui_tooltip = "Marks clipped pixels:\nRed = some highlights lost\nYellow = all highlights lost\nBlue = some shadows lost\nCyan = all shadows lost.";
> = false;

float3 LevelsPass(float4 vpos : SV_Position, float2 texcoord : TexCoord) : SV_Target
{
	const float black_point_float = BlackPoint / 255.0;

	float white_point_float;
	if (WhitePoint == BlackPoint)
		white_point_float = (255.0 / 0.00025);
	else
		white_point_float = 255.0 / (WhitePoint - BlackPoint);

	float3 color = tex2D(ReShade::BackBuffer, texcoord).rgb;
	color = color * white_point_float - (black_point_float * white_point_float);

	if (HighlightClipping)
	{
		float3 clipped_colors;

		if (any(color > saturate(color)))
			clipped_colors = float3(1.0, 0.0, 0.0);
		else
			clipped_colors = color;

		if (all(color > saturate(color)))
			clipped_colors = float3(1.0, 1.0, 0.0);

		if (any(color < saturate(color)))
			clipped_colors = float3(0.0, 0.0, 1.0);

		if (all(color < saturate(color)))
			clipped_colors = float3(0.0, 1.0, 1.0);

		color = clipped_colors;
	}

	return color;
}

technique Levels
{
	pass
	{
		VertexShader = PostProcessVS;
		PixelShader  = LevelsPass;
	}
}
