#include "Graphics/Shader/ShaderProgramCatalogPrivate.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace gglab
{
	namespace
	{
		struct ShaderProgramBuildRecord final
		{
			const ShaderProgramRef* m_ProgramRef = nullptr;
			std::wstring_view m_SourcePath;
			ShaderStage m_Stage = ShaderStage::Pixel;
			std::wstring_view m_EntryPoint;
		};

		using namespace shader_programs;
		const std::array BuildRecords{
			ShaderProgramBuildRecord{ &ForwardCoverageVertex, L"Passes/PassForwardCoverage.hlsl", ShaderStage::Vertex, L"VSMain" },
			ShaderProgramBuildRecord{ &ForwardPBRLegacyPixel, L"Passes/PassForwardPBR.hlsl", ShaderStage::Pixel, L"PSMain" },
			ShaderProgramBuildRecord{ &ForwardPBRForwardPlusPixel, L"Passes/PassForwardPBR.hlsl", ShaderStage::Pixel, L"PSMain" },
			ShaderProgramBuildRecord{ &ForwardPBRForwardPlusValidationPixel, L"Passes/PassForwardPBR.hlsl", ShaderStage::Pixel, L"PSMain" },
			ShaderProgramBuildRecord{ &ForwardPBRLegacyGTAOPixel, L"Passes/PassForwardPBR.hlsl", ShaderStage::Pixel, L"PSMain" },
			ShaderProgramBuildRecord{ &ForwardPBRForwardPlusGTAOPixel, L"Passes/PassForwardPBR.hlsl", ShaderStage::Pixel, L"PSMain" },
			ShaderProgramBuildRecord{ &ForwardPBRForwardPlusValidationGTAOPixel, L"Passes/PassForwardPBR.hlsl", ShaderStage::Pixel, L"PSMain" },
			ShaderProgramBuildRecord{ &DepthPrepassAlphaTestPixel, L"Passes/PassDepthPrepass.hlsl", ShaderStage::Pixel, L"PSAlphaTest" },
			ShaderProgramBuildRecord{ &ForwardPlusCullCompute, L"Passes/PassForwardPlusCull.hlsl", ShaderStage::Compute, L"CSMain" },
			ShaderProgramBuildRecord{ &ForwardPlusCullDiagnosticsCompute, L"Passes/PassForwardPlusCull.hlsl", ShaderStage::Compute, L"CSMain" },
			ShaderProgramBuildRecord{ &ForwardPlusValidationTilesCompute, L"Passes/PassForwardPlusValidation.hlsl", ShaderStage::Compute, L"CSReduceTiles" },
			ShaderProgramBuildRecord{ &ForwardPlusValidationFrameCompute, L"Passes/PassForwardPlusValidation.hlsl", ShaderStage::Compute, L"CSReduceFrame" },
			ShaderProgramBuildRecord{ &GTAOEvaluateCompute, L"Passes/PassGTAO.hlsl", ShaderStage::Compute, L"CSMain" },
			ShaderProgramBuildRecord{ &GTAOEvaluateDiagnosticsCompute, L"Passes/PassGTAO.hlsl", ShaderStage::Compute, L"CSMain" },
			ShaderProgramBuildRecord{ &GTAODenoiseXCompute, L"Passes/PassGTAO.hlsl", ShaderStage::Compute, L"CSMain" },
			ShaderProgramBuildRecord{ &GTAODenoiseYCompute, L"Passes/PassGTAO.hlsl", ShaderStage::Compute, L"CSMain" },
			ShaderProgramBuildRecord{ &GTAOUpsampleCompute, L"Passes/PassGTAO.hlsl", ShaderStage::Compute, L"CSMain" },
			ShaderProgramBuildRecord{ &DirectionalShadowMapVertex, L"Passes/PassDirectionalShadowMap.hlsl", ShaderStage::Vertex, L"VSMain" },
			ShaderProgramBuildRecord{ &DirectionalShadowMapPixel, L"Passes/PassDirectionalShadowMap.hlsl", ShaderStage::Pixel, L"PSMain" },
			ShaderProgramBuildRecord{ &ShadowMapPreviewVertex, L"Passes/PassShadowMapPreview.hlsl", ShaderStage::Vertex, L"VSMain" },
			ShaderProgramBuildRecord{ &ShadowMapPreviewPixel, L"Passes/PassShadowMapPreview.hlsl", ShaderStage::Pixel, L"PSMain" },
			ShaderProgramBuildRecord{ &FinalColorVertex, L"Passes/PassFinalColor.hlsl", ShaderStage::Vertex, L"VSMain" },
			ShaderProgramBuildRecord{ &FinalColorPixel, L"Passes/PassFinalColor.hlsl", ShaderStage::Pixel, L"PSMain" },
			ShaderProgramBuildRecord{ &BloomVertex, L"Passes/PassBloom.hlsl", ShaderStage::Vertex, L"VSMain" },
			ShaderProgramBuildRecord{ &BloomPixel, L"Passes/PassBloom.hlsl", ShaderStage::Pixel, L"PSMain" },
			ShaderProgramBuildRecord{ &PostProcessPreviewVertex, L"Passes/PassPostProcessPreview.hlsl", ShaderStage::Vertex, L"VSMain" },
			ShaderProgramBuildRecord{ &PostProcessPreviewPixel, L"Passes/PassPostProcessPreview.hlsl", ShaderStage::Pixel, L"PSMain" },
			ShaderProgramBuildRecord{ &DebugDrawVertex, L"Passes/PassDebugDraw.hlsl", ShaderStage::Vertex, L"VSMain" },
			ShaderProgramBuildRecord{ &DebugDrawPixel, L"Passes/PassDebugDraw.hlsl", ShaderStage::Pixel, L"PSMain" },
			ShaderProgramBuildRecord{ &SkyboxVertex, L"Passes/PassSkybox.hlsl", ShaderStage::Vertex, L"VSMain" },
			ShaderProgramBuildRecord{ &SkyboxPixel, L"Passes/PassSkybox.hlsl", ShaderStage::Pixel, L"PSMain" },
			ShaderProgramBuildRecord{ &IBLEnvironmentVertex, L"Passes/PassIBLEnvironment.hlsl", ShaderStage::Vertex, L"VSMain" },
			ShaderProgramBuildRecord{ &IBLEnvironmentPixel, L"Passes/PassIBLEnvironment.hlsl", ShaderStage::Pixel, L"PSMain" },
			ShaderProgramBuildRecord{ &IBLEnvironmentMipVertex, L"Passes/PassIBLEnvironmentMip.hlsl", ShaderStage::Vertex, L"VSMain" },
			ShaderProgramBuildRecord{ &IBLEnvironmentMipPixel, L"Passes/PassIBLEnvironmentMip.hlsl", ShaderStage::Pixel, L"PSMain" },
			ShaderProgramBuildRecord{ &IBLIrradianceVertex, L"Passes/PassIBLIrradiance.hlsl", ShaderStage::Vertex, L"VSMain" },
			ShaderProgramBuildRecord{ &IBLIrradiancePixel, L"Passes/PassIBLIrradiance.hlsl", ShaderStage::Pixel, L"PSMain" },
			ShaderProgramBuildRecord{ &IBLPrefilteredSpecularVertex, L"Passes/PassIBLPrefilteredSpecular.hlsl", ShaderStage::Vertex, L"VSMain" },
			ShaderProgramBuildRecord{ &IBLPrefilteredSpecularPixel, L"Passes/PassIBLPrefilteredSpecular.hlsl", ShaderStage::Pixel, L"PSMain" },
			ShaderProgramBuildRecord{ &IBLBrdfLUTVertex, L"Passes/PassIBLBrdfLUT.hlsl", ShaderStage::Vertex, L"VSMain" },
			ShaderProgramBuildRecord{ &IBLBrdfLUTPixel, L"Passes/PassIBLBrdfLUT.hlsl", ShaderStage::Pixel, L"PSMain" },
			ShaderProgramBuildRecord{ &IBLCubemapPreviewVertex, L"Passes/PassIBLCubemapPreview.hlsl", ShaderStage::Vertex, L"VSMain" },
			ShaderProgramBuildRecord{ &IBLCubemapPreviewPixel, L"Passes/PassIBLCubemapPreview.hlsl", ShaderStage::Pixel, L"PSMain" },
			ShaderProgramBuildRecord{ &CoordinateGeometryVertex, L"Passes/PassCoordinateConformance.hlsl", ShaderStage::Vertex, L"VSGeometry" },
			ShaderProgramBuildRecord{ &CoordinateFullscreenVertex, L"Passes/PassCoordinateConformance.hlsl", ShaderStage::Vertex, L"VSFullscreen" },
			ShaderProgramBuildRecord{ &CoordinateMarkerPixel, L"Passes/PassCoordinateConformance.hlsl", ShaderStage::Pixel, L"PSMarker" },
			ShaderProgramBuildRecord{ &CoordinateConformancePixel, L"Passes/PassCoordinateConformance.hlsl", ShaderStage::Pixel, L"PSConformance" },
			ShaderProgramBuildRecord{ &RenderGraphComputeWrite, L"Passes/PassRenderGraphComputeSmoke.hlsl", ShaderStage::Compute, L"CSWrite" },
			ShaderProgramBuildRecord{ &RenderGraphComputeReadWrite, L"Passes/PassRenderGraphComputeSmoke.hlsl", ShaderStage::Compute, L"CSReadWrite" },
			ShaderProgramBuildRecord{ &RenderGraphComputePreviewVertex, L"Passes/PassRenderGraphComputeSmoke.hlsl", ShaderStage::Vertex, L"VSMain" },
			ShaderProgramBuildRecord{ &RenderGraphComputePreviewPixel, L"Passes/PassRenderGraphComputeSmoke.hlsl", ShaderStage::Pixel, L"PSMain" },
			ShaderProgramBuildRecord{ &NapaVoxelVertex, L"Passes/PassNapaVoxel.hlsl", ShaderStage::Vertex, L"VSMain" },
			ShaderProgramBuildRecord{ &NapaVoxelPixel, L"Passes/PassNapaVoxel.hlsl", ShaderStage::Pixel, L"PSMain" },
		};

		void AddDefine(ShaderDesc& desc, const wchar_t* name)
		{
			desc.m_Defines.push_back({ .m_Name = name, .m_Value = L"1" });
		}
	}

	std::optional<ShaderDesc> ResolveTransitionalShaderProgramBuild(
		const ShaderProgramRef& programRef) noexcept
	{
		try
		{
			const auto iterator = std::ranges::find_if(BuildRecords,
				[&programRef](const ShaderProgramBuildRecord& record) noexcept
				{ return *record.m_ProgramRef == programRef; });
			if (iterator == BuildRecords.end())
			{
				return std::nullopt;
			}

			ShaderDesc desc{
				.m_SourcePath = iterator->m_SourcePath,
				.m_Stage = iterator->m_Stage,
				.m_Entry = std::wstring(iterator->m_EntryPoint),
			};
			if (programRef == ForwardPBRLegacyGTAOPixel ||
				programRef == ForwardPBRForwardPlusGTAOPixel ||
				programRef == ForwardPBRForwardPlusValidationGTAOPixel)
			{
				AddDefine(desc, L"GGLAB_GTAO_CONTRIBUTION_OUTPUT");
			}
			if (programRef == ForwardPBRForwardPlusPixel ||
				programRef == ForwardPBRForwardPlusValidationPixel ||
				programRef == ForwardPBRForwardPlusGTAOPixel ||
				programRef == ForwardPBRForwardPlusValidationGTAOPixel)
			{
				AddDefine(desc, L"GGLAB_FORWARD_PLUS");
			}
			if (programRef == ForwardPBRForwardPlusValidationPixel ||
				programRef == ForwardPBRForwardPlusValidationGTAOPixel)
			{
				AddDefine(desc, L"GGLAB_FORWARD_PLUS_VALIDATION");
			}
			if (programRef == ForwardPlusCullDiagnosticsCompute)
			{
				AddDefine(desc, L"GGLAB_FORWARD_PLUS_DIAGNOSTICS");
			}
			if (programRef == ForwardPlusValidationTilesCompute)
			{
				AddDefine(desc, L"GGLAB_FORWARD_PLUS_VALIDATION_REDUCE_TILES");
			}
			if (programRef == ForwardPlusValidationFrameCompute)
			{
				AddDefine(desc, L"GGLAB_FORWARD_PLUS_VALIDATION_REDUCE_FRAME");
			}
			if (programRef == GTAOEvaluateDiagnosticsCompute)
			{
				AddDefine(desc, L"GGLAB_GTAO_DIAGNOSTICS");
			}
			if (programRef == GTAODenoiseXCompute)
			{
				AddDefine(desc, L"GGLAB_GTAO_DENOISE_X");
			}
			if (programRef == GTAODenoiseYCompute)
			{
				AddDefine(desc, L"GGLAB_GTAO_DENOISE_Y");
			}
			if (programRef == GTAOUpsampleCompute)
			{
				AddDefine(desc, L"GGLAB_GTAO_UPSAMPLE");
			}
			return desc;
		}
		catch (...)
		{
			return std::nullopt;
		}
	}
}

namespace gglab::shader_programs
{
	std::span<const ShaderProgramRef> GetRendererInitialShaderProgramDemand() noexcept
	{
		static const std::array Programs{
			ForwardCoverageVertex,
			ForwardPBRLegacyPixel,
			DepthPrepassAlphaTestPixel,
			ForwardPlusCullCompute,
			DirectionalShadowMapVertex,
			DirectionalShadowMapPixel,
			ShadowMapPreviewVertex,
			ShadowMapPreviewPixel,
			FinalColorVertex,
			FinalColorPixel,
			BloomVertex,
			BloomPixel,
			PostProcessPreviewVertex,
			PostProcessPreviewPixel,
			DebugDrawVertex,
			DebugDrawPixel,
			SkyboxVertex,
			SkyboxPixel,
			IBLEnvironmentVertex,
			IBLEnvironmentPixel,
			IBLEnvironmentMipVertex,
			IBLEnvironmentMipPixel,
			IBLIrradianceVertex,
			IBLIrradiancePixel,
			IBLPrefilteredSpecularVertex,
			IBLPrefilteredSpecularPixel,
			IBLBrdfLUTVertex,
			IBLBrdfLUTPixel,
			IBLCubemapPreviewVertex,
			IBLCubemapPreviewPixel,
		};
		return Programs;
	}
}
