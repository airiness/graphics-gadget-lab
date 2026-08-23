#pragma once
#include "ShaderArtifactRuntime/ShaderProgramRegistry.h"

#include <span>

namespace gglab::shader_programs
{
	inline const ShaderProgramRef ForwardCoverageVertex{ "gglab.shader.forward-coverage", "vertex" };
	inline const ShaderProgramRef ForwardPBRLegacyPixel{ "gglab.shader.forward-pbr", "pixel.legacy" };
	inline const ShaderProgramRef ForwardPBRForwardPlusPixel{ "gglab.shader.forward-pbr", "pixel.forward-plus" };
	inline const ShaderProgramRef ForwardPBRForwardPlusValidationPixel{
		"gglab.shader.forward-pbr", "pixel.forward-plus-validation" };
	inline const ShaderProgramRef ForwardPBRLegacyGTAOPixel{
		"gglab.shader.forward-pbr", "pixel.legacy-gtao" };
	inline const ShaderProgramRef ForwardPBRForwardPlusGTAOPixel{
		"gglab.shader.forward-pbr", "pixel.forward-plus-gtao" };
	inline const ShaderProgramRef ForwardPBRForwardPlusValidationGTAOPixel{
		"gglab.shader.forward-pbr", "pixel.forward-plus-validation-gtao" };
	inline const ShaderProgramRef DepthPrepassAlphaTestPixel{
		"gglab.shader.depth-prepass", "pixel.alpha-test" };
	inline const ShaderProgramRef ForwardPlusCullCompute{
		"gglab.shader.forward-plus-cull", "compute" };
	inline const ShaderProgramRef ForwardPlusCullDiagnosticsCompute{
		"gglab.shader.forward-plus-cull", "compute.diagnostics" };
	inline const ShaderProgramRef ForwardPlusValidationTilesCompute{
		"gglab.shader.forward-plus-validation", "compute.tiles" };
	inline const ShaderProgramRef ForwardPlusValidationFrameCompute{
		"gglab.shader.forward-plus-validation", "compute.frame" };
	inline const ShaderProgramRef GTAOEvaluateCompute{ "gglab.shader.gtao", "compute.evaluate" };
	inline const ShaderProgramRef GTAOEvaluateDiagnosticsCompute{
		"gglab.shader.gtao", "compute.evaluate-diagnostics" };
	inline const ShaderProgramRef GTAODenoiseXCompute{ "gglab.shader.gtao", "compute.denoise-x" };
	inline const ShaderProgramRef GTAODenoiseYCompute{ "gglab.shader.gtao", "compute.denoise-y" };
	inline const ShaderProgramRef GTAOUpsampleCompute{ "gglab.shader.gtao", "compute.upsample" };

	inline const ShaderProgramRef DirectionalShadowMapVertex{
		"gglab.shader.directional-shadow-map", "vertex" };
	inline const ShaderProgramRef DirectionalShadowMapPixel{
		"gglab.shader.directional-shadow-map", "pixel.alpha-test" };
	inline const ShaderProgramRef ShadowMapPreviewVertex{
		"gglab.shader.shadow-map-preview", "vertex" };
	inline const ShaderProgramRef ShadowMapPreviewPixel{
		"gglab.shader.shadow-map-preview", "pixel" };
	inline const ShaderProgramRef FinalColorVertex{ "gglab.shader.final-color", "vertex" };
	inline const ShaderProgramRef FinalColorPixel{ "gglab.shader.final-color", "pixel" };
	inline const ShaderProgramRef BloomVertex{ "gglab.shader.bloom", "vertex" };
	inline const ShaderProgramRef BloomPixel{ "gglab.shader.bloom", "pixel" };
	inline const ShaderProgramRef PostProcessPreviewVertex{
		"gglab.shader.post-process-preview", "vertex" };
	inline const ShaderProgramRef PostProcessPreviewPixel{
		"gglab.shader.post-process-preview", "pixel" };
	inline const ShaderProgramRef DebugDrawVertex{ "gglab.shader.debug-draw", "vertex" };
	inline const ShaderProgramRef DebugDrawPixel{ "gglab.shader.debug-draw", "pixel" };
	inline const ShaderProgramRef SkyboxVertex{ "gglab.shader.skybox", "vertex" };
	inline const ShaderProgramRef SkyboxPixel{ "gglab.shader.skybox", "pixel" };
	inline const ShaderProgramRef IBLEnvironmentVertex{
		"gglab.shader.ibl-environment", "vertex" };
	inline const ShaderProgramRef IBLEnvironmentPixel{
		"gglab.shader.ibl-environment", "pixel" };
	inline const ShaderProgramRef IBLEnvironmentMipVertex{
		"gglab.shader.ibl-environment-mip", "vertex" };
	inline const ShaderProgramRef IBLEnvironmentMipPixel{
		"gglab.shader.ibl-environment-mip", "pixel" };
	inline const ShaderProgramRef IBLIrradianceVertex{
		"gglab.shader.ibl-irradiance", "vertex" };
	inline const ShaderProgramRef IBLIrradiancePixel{
		"gglab.shader.ibl-irradiance", "pixel" };
	inline const ShaderProgramRef IBLPrefilteredSpecularVertex{
		"gglab.shader.ibl-prefiltered-specular", "vertex" };
	inline const ShaderProgramRef IBLPrefilteredSpecularPixel{
		"gglab.shader.ibl-prefiltered-specular", "pixel" };
	inline const ShaderProgramRef IBLBrdfLUTVertex{
		"gglab.shader.ibl-brdf-lut", "vertex" };
	inline const ShaderProgramRef IBLBrdfLUTPixel{
		"gglab.shader.ibl-brdf-lut", "pixel" };
	inline const ShaderProgramRef IBLCubemapPreviewVertex{
		"gglab.shader.ibl-cubemap-preview", "vertex" };
	inline const ShaderProgramRef IBLCubemapPreviewPixel{
		"gglab.shader.ibl-cubemap-preview", "pixel" };

	inline const ShaderProgramRef CoordinateGeometryVertex{
		"gglab.shader.coordinate-conformance", "vertex.geometry" };
	inline const ShaderProgramRef CoordinateFullscreenVertex{
		"gglab.shader.coordinate-conformance", "vertex.fullscreen" };
	inline const ShaderProgramRef CoordinateMarkerPixel{
		"gglab.shader.coordinate-conformance", "pixel.marker" };
	inline const ShaderProgramRef CoordinateConformancePixel{
		"gglab.shader.coordinate-conformance", "pixel.conformance" };
	inline const ShaderProgramRef RenderGraphComputeWrite{
		"gglab.shader.render-graph-compute", "compute.write" };
	inline const ShaderProgramRef RenderGraphComputeReadWrite{
		"gglab.shader.render-graph-compute", "compute.read-write" };
	inline const ShaderProgramRef RenderGraphComputePreviewVertex{
		"gglab.shader.render-graph-compute", "vertex.preview" };
	inline const ShaderProgramRef RenderGraphComputePreviewPixel{
		"gglab.shader.render-graph-compute", "pixel.preview" };
	inline const ShaderProgramRef NapaVoxelVertex{ "gglab.shader.napa-voxel", "vertex" };
	inline const ShaderProgramRef NapaVoxelPixel{ "gglab.shader.napa-voxel", "pixel" };

	[[nodiscard]] std::span<const ShaderProgramRef>
		GetRendererInitialShaderProgramDemand() noexcept;
}
