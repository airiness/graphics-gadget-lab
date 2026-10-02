#include <PBR/MaterialDiagnostics.hlsli>
#include <Common/DisplayColor.hlsli>

#ifndef GGLAB_MATERIAL_DIAGNOSTIC_TEST_CASE
#define GGLAB_MATERIAL_DIAGNOSTIC_TEST_CASE 0
#endif

bool CloseColor(float3 actual, float3 expected)
{
	return all(abs(actual - expected) <= 2.0e-6.xxx) &&
		all((asuint(actual) & 0x7f800000u) != 0x7f800000u);
}

// Compare the display result without requiring DXC to fold isfinite checks
// through its tone-map intrinsics. Raw diagnostic payloads are checked above.
bool CloseDisplayColor(float3 actual, float3 expected)
{
	return all(abs(actual - expected) <= 2.0e-6.xxx);
}

float3 BlendDiagnosticTarget(float4 source, float3 destination)
{
	return source.rgb * source.a + destination * (1.0 - source.a);
}

float4 PSMain() : SV_Target0
{
	SurfaceData surface = (SurfaceData)0;
	surface.BaseColor = float3(0.2, 0.4, 0.8);
	surface.Metallic = 0.7;
	surface.Roughness = 0.3;
	surface.Ior = 1.5;
	surface.ClearcoatFactor = 0.6;
	surface.ClearcoatRoughness = 0.1;
	surface.AnisotropyStrength = 0.5;
	surface.AnisotropyDirectionTS = float2(0.6, -0.8);
	BaseShadingState shading = (BaseShadingState)0;
	shading.NormalWS = float3(0.0, 0.0, 1.0);
	shading.AuthoredPerceptualRoughness = 0.3;
	shading.EffectivePerceptualRoughness = 0.5;
	shading.NormalVariance = 0.1;
	shading.SpecularAAKernelAlpha = 0.05;
	shading.F0 = float3(0.04, 0.2, 0.6);
	shading.FeatureFlags = 3u;
	SpecularAAResult coatAA = (SpecularAAResult)0;
	coatAA.NormalVariance = 0.2;
	coatAA.KernelAlpha = 0.07;
	coatAA.EffectivePerceptualRoughness = 0.4;
	AnisotropyShadingState anisotropy = (AnisotropyShadingState)0;
	anisotropy.TangentWS = float3(1.0, 0.0, 0.0);
	anisotropy.AlphaT = 0.2;
	anisotropy.AlphaB = 0.05;
	const float3 coatNormal = float3(0.0, 1.0, 0.0);
	const uint views[23] = { 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u,
		12u, 13u, 14u, 15u, 19u, 20u, 21u, 22u, 0u, 23u, 16u, 255u };
	const float3 expectedColors[23] = {
		float3(0.2, 0.4, 0.8), 0.7.xxx, 0.3.xxx, float3(0.5, 0.5, 1.0),
		0.3.xxx, 0.5.xxx, float3(0.04, 0.2, 0.6), float3(1.0, 1.0, 0.0),
		(1.0 / 3.0).xxx, 0.6.xxx, 0.1.xxx, float3(0.5, 1.0, 0.5), 0.5.xxx,
		float3(0.8, 0.1, 0.5), float3(1.0, 0.5, 0.5), float3(0.1, 0.2, 0.0),
		float3(0.05, 0.07, 0.2), 0.4.xxx, float3(0.2, 0.05, 0.0),
		0.0.xxx, 0.0.xxx, 0.0.xxx, 0.0.xxx
	};
	bool matches = true;
#if GGLAB_MATERIAL_DIAGNOSTIC_TEST_CASE < 23
	const uint testCase = GGLAB_MATERIAL_DIAGNOSTIC_TEST_CASE;
	float3 color;
	const bool diagnostic = TryEvaluateMaterialDiagnostic(views[testCase], surface,
		shading, coatAA, coatNormal, anisotropy, color);
	matches = diagnostic == (testCase < 19u) && CloseColor(color, expectedColors[testCase]);
#if GGLAB_MATERIAL_DIAGNOSTIC_TEST_CASE < 19
	const float scales[4] = { 1.0 / 1.2, 1.0 / (1.2 * 32768.0), 1.0e-8, 1.0e6 };
	[unroll]
	for (uint index = 0; index < 4u; ++index)
	{
		[unroll]
		for (uint storageMode = 0; storageMode < 2u; ++storageMode)
		{
			const float exposure = scales[index];
			const float preExposure = storageMode == 0u ? 1.0 : exposure;
			const float3 stored = EncodeSceneColor(float3(4.0, 8.0, 16.0), preExposure);
			const float3 actual = ResolveDisplayColor(stored, exposure / preExposure,
				color, diagnostic ? 1.0 : 0.0);
			const float3 expected = diagnostic ? LinearToSRGB(expectedColors[testCase]) :
				LinearToSRGB(ACESFitted(stored * (exposure / preExposure)));
			matches = matches && CloseDisplayColor(actual, expected);
		}
	}
#endif
#elif GGLAB_MATERIAL_DIAGNOSTIC_TEST_CASE == 23
	// Two diagnostic layers followed by a lit transparent occluder. Simulate
	// alpha blending the production MRT payloads together with scene lighting.
	const float3 background = float3(0.25, 0.5, 0.75);
	const float3 foreground = float3(0.75, 0.25, 0.5);
	const float4 first = float4(4.0, 8.0, 16.0, 1.0);
	const float4 second = float4(8.0, 16.0, 32.0, 0.5);
	const float4 lit = float4(1.0, 2.0, 3.0, 0.25);
	const MaterialDiagnosticOutput firstOutput = MakeMaterialDiagnosticOutput(first, background, true);
	const MaterialDiagnosticOutput secondOutput = MakeMaterialDiagnosticOutput(second, foreground, true);
	const MaterialDiagnosticOutput litOutput = MakeMaterialDiagnosticOutput(lit, 0.0.xxx, false);
	const float3 color = BlendDiagnosticTarget(litOutput.Color,
		BlendDiagnosticTarget(secondOutput.Color, firstOutput.Color.rgb));
	const float coverage = BlendDiagnosticTarget(litOutput.Coverage,
		BlendDiagnosticTarget(secondOutput.Coverage, firstOutput.Coverage.rgb)).r;
	const float3 lighting = BlendDiagnosticTarget(litOutput.Lighting,
		BlendDiagnosticTarget(secondOutput.Lighting, firstOutput.Lighting.rgb));
	const float3 stored = BlendDiagnosticTarget(lit, BlendDiagnosticTarget(second, first.rgb));
	matches = CloseDisplayColor(ResolveDisplayColor(stored, 0.01, color, coverage, lighting),
		LinearToSRGB(ACESFitted(lit.rgb * 0.01) * 0.25 + float3(0.375, 0.28125, 0.46875)));
#elif GGLAB_MATERIAL_DIAGNOSTIC_TEST_CASE == 24
	surface.BaseColor = float3(2.0, -1.0, 0.5);
	float3 color;
	matches = TryEvaluateMaterialDiagnostic(MaterialDebugViewBaseColor, surface,
		shading, coatAA, coatNormal, anisotropy, color) && CloseColor(color, float3(1.0, 0.0, 0.5));
#elif GGLAB_MATERIAL_DIAGNOSTIC_TEST_CASE == 25
	// A half-transparent diagnostic over black contains no ordinary lighting,
	// regardless of exposure or the storage pre-exposure used by Forward.
	const float exposures[3] = { 0.01, 1.0, 100.0 };
	[unroll]
	for (uint index = 0; index < 3u; ++index)
	{
		[unroll]
		for (uint mode = 0; mode < 2u; ++mode)
		{
			const float preExposure = mode == 0u ? 1.0 : exposures[index];
			const float4 stored = float4(EncodeSceneColor(float3(4.0, 8.0, 16.0), preExposure), 0.5);
			const MaterialDiagnosticOutput output = MakeMaterialDiagnosticOutput(stored, surface.BaseColor, true);
			matches = matches && CloseDisplayColor(ResolveDisplayColor(stored.rgb * 0.5,
				exposures[index] / preExposure, output.Color.rgb * 0.5,
				output.Coverage.r * 0.5, output.Lighting.rgb * 0.5),
				LinearToSRGB(float3(0.1, 0.2, 0.4)));
		}
	}
#elif GGLAB_MATERIAL_DIAGNOSTIC_TEST_CASE == 26
	// Preserve exactly one coverage factor on a lit background; removing
	// diagnostic lighting must not also dim the background a second time.
	const float3 background = float3(2.0, 4.0, 8.0);
	const float4 stored = float4(4.0, 8.0, 16.0, 0.5);
	const MaterialDiagnosticOutput output = MakeMaterialDiagnosticOutput(stored, surface.BaseColor, true);
	matches = CloseDisplayColor(ResolveDisplayColor(BlendDiagnosticTarget(stored, background), 0.01,
		output.Color.rgb * 0.5, output.Coverage.r * 0.5, output.Lighting.rgb * 0.5),
		LinearToSRGB(ACESFitted(background * 0.01) * 0.5 + float3(0.1, 0.2, 0.4)));
#elif GGLAB_MATERIAL_DIAGNOSTIC_TEST_CASE == 27
	// Zero opacity leaves ordinary lighting unchanged; unit opacity removes it completely.
	const float3 background = float3(2.0, 4.0, 8.0);
	const float4 invisible = float4(4.0, 8.0, 16.0, 0.0);
	const float4 opaque = float4(invisible.rgb, 1.0);
	const MaterialDiagnosticOutput zero = MakeMaterialDiagnosticOutput(invisible, surface.BaseColor, true);
	const MaterialDiagnosticOutput one = MakeMaterialDiagnosticOutput(opaque, surface.BaseColor, true);
	matches = CloseDisplayColor(ResolveDisplayColor(BlendDiagnosticTarget(invisible, background), 0.01,
		BlendDiagnosticTarget(zero.Color, 0.0.xxx), BlendDiagnosticTarget(zero.Coverage, 0.0.xxx).r,
		BlendDiagnosticTarget(zero.Lighting, 0.0.xxx)), LinearToSRGB(ACESFitted(background * 0.01))) &&
		CloseDisplayColor(ResolveDisplayColor(opaque.rgb, 100.0, one.Color.rgb, one.Coverage.r,
			one.Lighting.rgb), LinearToSRGB(surface.BaseColor));
#else
	// Multiple transparent diagnostics over lighting retain the background's
	// remaining 0.375 coverage and replace both diagnostic surfaces' lighting.
	const float3 background = float3(2.0, 4.0, 8.0);
	const float4 first = float4(4.0, 8.0, 16.0, 0.5);
	const float4 second = float4(8.0, 16.0, 32.0, 0.25);
	// Binary-exact colors let DXC eliminate the comparison through its ACES
	// IsFinite intrinsic instead of retaining a one-ULP sRGB comparison at runtime.
	const MaterialDiagnosticOutput a = MakeMaterialDiagnosticOutput(first, float3(0.25, 0.5, 1.0), true);
	const MaterialDiagnosticOutput b = MakeMaterialDiagnosticOutput(second, float3(1.0, 0.5, 0.25), true);
	const float3 stored = BlendDiagnosticTarget(second, BlendDiagnosticTarget(first, background));
	const float3 color = BlendDiagnosticTarget(b.Color, BlendDiagnosticTarget(a.Color, 0.0.xxx));
	const float coverage = BlendDiagnosticTarget(b.Coverage, BlendDiagnosticTarget(a.Coverage, 0.0.xxx)).r;
	const float3 lighting = BlendDiagnosticTarget(b.Lighting, BlendDiagnosticTarget(a.Lighting, 0.0.xxx));
	matches = CloseDisplayColor(ResolveDisplayColor(stored, 0.01, color, coverage, lighting),
		LinearToSRGB(ACESFitted(background * 0.01) * 0.375 + float3(0.34375, 0.3125, 0.4375)));
#endif
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

// Dynamic-input entry points compare the optimized production Lit path with
// its frozen exposure/ACES/sRGB expression. This avoids assuming that DXC can
// evaluate every tone-map IsFinite intrinsic at compile time.
float4 LitDisplayPS(float3 color : COLOR0, float exposure : TEXCOORD0) : SV_Target0
{
	return float4(ResolveDisplayColor(color, exposure, 0.0.xxx, 0.0), 1.0);
}

float4 LitReferencePS(float3 color : COLOR0, float exposure : TEXCOORD0) : SV_Target0
{
	return float4(LinearToSRGB(ACESFitted(color * exposure)), 1.0);
}
