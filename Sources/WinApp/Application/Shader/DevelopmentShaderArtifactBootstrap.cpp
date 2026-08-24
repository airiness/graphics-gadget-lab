#include "Application/Shader/DevelopmentShaderArtifactBootstrap.h"
#include "Artifact/ShaderRuntimeArtifactPublication.h"
#include "Compiler/ShaderCompiler.h"
#include "GGLabFoundation/Platform/Win/Win32StringUtils.h"
#include "Graphics/Shader/ShaderProgramCatalog.h"
#include "Targets/DX12ShaderTarget.h"
#include "Targets/Vulkan13ShaderTarget.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gglab
{
	namespace
	{
		struct ShaderProgramBuildRecord final
		{
			const ShaderProgramRef* m_ProgramRef = nullptr;
			std::wstring_view m_SourcePath;
			std::wstring_view m_EntryPoint;
		};

		using namespace shader_programs;
		const std::array BuildRecords{
			ShaderProgramBuildRecord{ &ForwardCoverageVertex, L"Passes/PassForwardCoverage.hlsl", L"VSMain" },
			ShaderProgramBuildRecord{ &ForwardPBRLegacyPixel, L"Passes/PassForwardPBR.hlsl", L"PSMain" },
			ShaderProgramBuildRecord{ &ForwardPBRForwardPlusPixel, L"Passes/PassForwardPBR.hlsl", L"PSMain" },
			ShaderProgramBuildRecord{ &ForwardPBRForwardPlusValidationPixel, L"Passes/PassForwardPBR.hlsl", L"PSMain" },
			ShaderProgramBuildRecord{ &ForwardPBRLegacyGTAOPixel, L"Passes/PassForwardPBR.hlsl", L"PSMain" },
			ShaderProgramBuildRecord{ &ForwardPBRForwardPlusGTAOPixel, L"Passes/PassForwardPBR.hlsl", L"PSMain" },
			ShaderProgramBuildRecord{ &ForwardPBRForwardPlusValidationGTAOPixel, L"Passes/PassForwardPBR.hlsl", L"PSMain" },
			ShaderProgramBuildRecord{ &DepthPrepassAlphaTestPixel, L"Passes/PassDepthPrepass.hlsl", L"PSAlphaTest" },
			ShaderProgramBuildRecord{ &ForwardPlusCullCompute, L"Passes/PassForwardPlusCull.hlsl", L"CSMain" },
			ShaderProgramBuildRecord{ &ForwardPlusCullDiagnosticsCompute, L"Passes/PassForwardPlusCull.hlsl", L"CSMain" },
			ShaderProgramBuildRecord{ &ForwardPlusValidationTilesCompute, L"Passes/PassForwardPlusValidation.hlsl", L"CSReduceTiles" },
			ShaderProgramBuildRecord{ &ForwardPlusValidationFrameCompute, L"Passes/PassForwardPlusValidation.hlsl", L"CSReduceFrame" },
			ShaderProgramBuildRecord{ &GTAOEvaluateCompute, L"Passes/PassGTAO.hlsl", L"CSMain" },
			ShaderProgramBuildRecord{ &GTAOEvaluateDiagnosticsCompute, L"Passes/PassGTAO.hlsl", L"CSMain" },
			ShaderProgramBuildRecord{ &GTAODenoiseXCompute, L"Passes/PassGTAO.hlsl", L"CSMain" },
			ShaderProgramBuildRecord{ &GTAODenoiseYCompute, L"Passes/PassGTAO.hlsl", L"CSMain" },
			ShaderProgramBuildRecord{ &GTAOUpsampleCompute, L"Passes/PassGTAO.hlsl", L"CSMain" },
			ShaderProgramBuildRecord{ &DirectionalShadowMapVertex, L"Passes/PassDirectionalShadowMap.hlsl", L"VSMain" },
			ShaderProgramBuildRecord{ &DirectionalShadowMapPixel, L"Passes/PassDirectionalShadowMap.hlsl", L"PSMain" },
			ShaderProgramBuildRecord{ &ShadowMapPreviewVertex, L"Passes/PassShadowMapPreview.hlsl", L"VSMain" },
			ShaderProgramBuildRecord{ &ShadowMapPreviewPixel, L"Passes/PassShadowMapPreview.hlsl", L"PSMain" },
			ShaderProgramBuildRecord{ &FinalColorVertex, L"Passes/PassFinalColor.hlsl", L"VSMain" },
			ShaderProgramBuildRecord{ &FinalColorPixel, L"Passes/PassFinalColor.hlsl", L"PSMain" },
			ShaderProgramBuildRecord{ &BloomVertex, L"Passes/PassBloom.hlsl", L"VSMain" },
			ShaderProgramBuildRecord{ &BloomPixel, L"Passes/PassBloom.hlsl", L"PSMain" },
			ShaderProgramBuildRecord{ &PostProcessPreviewVertex, L"Passes/PassPostProcessPreview.hlsl", L"VSMain" },
			ShaderProgramBuildRecord{ &PostProcessPreviewPixel, L"Passes/PassPostProcessPreview.hlsl", L"PSMain" },
			ShaderProgramBuildRecord{ &DebugDrawVertex, L"Passes/PassDebugDraw.hlsl", L"VSMain" },
			ShaderProgramBuildRecord{ &DebugDrawPixel, L"Passes/PassDebugDraw.hlsl", L"PSMain" },
			ShaderProgramBuildRecord{ &SkyboxVertex, L"Passes/PassSkybox.hlsl", L"VSMain" },
			ShaderProgramBuildRecord{ &SkyboxPixel, L"Passes/PassSkybox.hlsl", L"PSMain" },
			ShaderProgramBuildRecord{ &IBLEnvironmentVertex, L"Passes/PassIBLEnvironment.hlsl", L"VSMain" },
			ShaderProgramBuildRecord{ &IBLEnvironmentPixel, L"Passes/PassIBLEnvironment.hlsl", L"PSMain" },
			ShaderProgramBuildRecord{ &IBLEnvironmentMipVertex, L"Passes/PassIBLEnvironmentMip.hlsl", L"VSMain" },
			ShaderProgramBuildRecord{ &IBLEnvironmentMipPixel, L"Passes/PassIBLEnvironmentMip.hlsl", L"PSMain" },
			ShaderProgramBuildRecord{ &IBLIrradianceVertex, L"Passes/PassIBLIrradiance.hlsl", L"VSMain" },
			ShaderProgramBuildRecord{ &IBLIrradiancePixel, L"Passes/PassIBLIrradiance.hlsl", L"PSMain" },
			ShaderProgramBuildRecord{ &IBLPrefilteredSpecularVertex, L"Passes/PassIBLPrefilteredSpecular.hlsl", L"VSMain" },
			ShaderProgramBuildRecord{ &IBLPrefilteredSpecularPixel, L"Passes/PassIBLPrefilteredSpecular.hlsl", L"PSMain" },
			ShaderProgramBuildRecord{ &IBLBrdfLUTVertex, L"Passes/PassIBLBrdfLUT.hlsl", L"VSMain" },
			ShaderProgramBuildRecord{ &IBLBrdfLUTPixel, L"Passes/PassIBLBrdfLUT.hlsl", L"PSMain" },
			ShaderProgramBuildRecord{ &IBLCubemapPreviewVertex, L"Passes/PassIBLCubemapPreview.hlsl", L"VSMain" },
			ShaderProgramBuildRecord{ &IBLCubemapPreviewPixel, L"Passes/PassIBLCubemapPreview.hlsl", L"PSMain" },
			ShaderProgramBuildRecord{ &CoordinateGeometryVertex, L"Passes/PassCoordinateConformance.hlsl", L"VSGeometry" },
			ShaderProgramBuildRecord{ &CoordinateFullscreenVertex, L"Passes/PassCoordinateConformance.hlsl", L"VSFullscreen" },
			ShaderProgramBuildRecord{ &CoordinateMarkerPixel, L"Passes/PassCoordinateConformance.hlsl", L"PSMarker" },
			ShaderProgramBuildRecord{ &CoordinateConformancePixel, L"Passes/PassCoordinateConformance.hlsl", L"PSConformance" },
			ShaderProgramBuildRecord{ &RenderGraphComputeWrite, L"Passes/PassRenderGraphComputeSmoke.hlsl", L"CSWrite" },
			ShaderProgramBuildRecord{ &RenderGraphComputeReadWrite, L"Passes/PassRenderGraphComputeSmoke.hlsl", L"CSReadWrite" },
			ShaderProgramBuildRecord{ &RenderGraphComputePreviewVertex, L"Passes/PassRenderGraphComputeSmoke.hlsl", L"VSMain" },
			ShaderProgramBuildRecord{ &RenderGraphComputePreviewPixel, L"Passes/PassRenderGraphComputeSmoke.hlsl", L"PSMain" },
			ShaderProgramBuildRecord{ &NapaVoxelVertex, L"Passes/PassNapaVoxel.hlsl", L"VSMain" },
			ShaderProgramBuildRecord{ &NapaVoxelPixel, L"Passes/PassNapaVoxel.hlsl", L"PSMain" },
		};

		void AddDefine(ShaderDesc& desc, const wchar_t* name)
		{
			desc.m_Defines.push_back({ .m_Name = name, .m_Value = L"1" });
		}

		[[nodiscard]] std::optional<ShaderDesc> MakeBuildDesc(
			const ShaderProgramBuildRecord& record) noexcept
		{
			try
			{
				const ShaderProgramRef& programRef = *record.m_ProgramRef;
				ShaderDesc desc{
					.m_SourcePath = record.m_SourcePath,
					.m_Stage = programRef.m_Stage,
					.m_Entry = std::wstring(record.m_EntryPoint),
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

		[[nodiscard]] ShaderCompileTarget MakeActiveTarget(
			RHIBackendType activeBackend, ShaderStage stage) noexcept
		{
			return activeBackend == RHIBackendType::Vulkan
				? MakeVulkan13CompileTarget(stage)
				: MakeDX12CompileTarget(stage);
		}
	}

	DevelopmentShaderArtifactBootstrapResult PrepareDevelopmentShaderArtifacts(
		RHIBackendType activeBackend,
		const std::filesystem::path& shaderSourceRoot,
		const std::filesystem::path& shaderCacheRoot,
		const std::filesystem::path& artifactRoot) noexcept
	{
		if (activeBackend == RHIBackendType::Unknown || shaderSourceRoot.empty() ||
			shaderCacheRoot.empty() || artifactRoot.empty() ||
			!shaderSourceRoot.is_absolute() || !shaderCacheRoot.is_absolute() ||
			!artifactRoot.is_absolute())
		{
			return {
				.m_Status = DevelopmentShaderArtifactBootstrapStatus::InvalidInput,
				.m_Error = "Development shader artifact bootstrap requires absolute roots and a known backend.",
			};
		}

		try
		{
			ShaderCompiler compiler(shaderSourceRoot, shaderCacheRoot);
			ShaderDesc defaults{};
			defaults.m_Target.m_Flags |= IsDebuggerPresent() != FALSE
				? ShaderCompileFlags::Debug
				: ShaderCompileFlags::None;
			defaults.m_IncludeDirs = { shaderSourceRoot };
			compiler.SetDefaultShaderConfig(defaults);

			std::vector<ShaderProgramRegistryEntry> entries;
			entries.reserve(BuildRecords.size());
			const ShaderTargetProfile targetProfile = activeBackend == RHIBackendType::Vulkan
				? ShaderTargetProfile::GGLabVulkan13
				: ShaderTargetProfile::GGLabDX12;
			for (const ShaderProgramBuildRecord& record : BuildRecords)
			{
				std::optional<ShaderDesc> desc = MakeBuildDesc(record);
				if (!desc || !record.m_ProgramRef)
				{
					return {
						.m_Status = DevelopmentShaderArtifactBootstrapStatus::UnknownProgram,
						.m_Error = "The desktop shader build catalog contains an invalid program.",
					};
				}
				desc->m_Target = MakeActiveTarget(activeBackend, desc->m_Stage);
				const ShaderResolvedRecipe recipe = compiler.Resolve(*desc);
				const ShaderCompileResult compiled = compiler.CompileOrLoad(recipe);
				if (!compiled.IsSuccess() || !compiled.m_Artifact.m_Binary.IsValid())
				{
					return {
						.m_Status = compiled.m_Status ==
							ShaderCompileStatus::CompilerUnavailable
								? DevelopmentShaderArtifactBootstrapStatus::CompilerUnavailable
								: DevelopmentShaderArtifactBootstrapStatus::CompileFailed,
						.m_Error = std::format("Failed to build {}::{}: {}",
							record.m_ProgramRef->m_ProgramId,
							record.m_ProgramRef->m_VariantId,
							utils::ToString(compiled.m_Diagnostics.m_Message)),
					};
				}

				const ShaderRuntimeArtifactPublicationResult published =
					PublishShaderRuntimeArtifact(artifactRoot, compiled.m_Artifact);
				if (!published.IsSuccess())
				{
					return {
						.m_Status =
							DevelopmentShaderArtifactBootstrapStatus::ArtifactPublicationFailed,
						.m_Error = std::format("Failed to publish {}::{} (status={}).",
							record.m_ProgramRef->m_ProgramId,
							record.m_ProgramRef->m_VariantId,
							static_cast<uint32_t>(published.m_Status)),
					};
				}
				entries.push_back({
					.m_ProgramRef = *record.m_ProgramRef,
					.m_TargetProfile = targetProfile,
					.m_ArtifactRef = published.m_ArtifactRef,
				});
			}

			ShaderProgramRegistryArtifactBuildResult registryBuild =
				BuildShaderProgramRegistryArtifact(entries);
			if (!registryBuild.IsSuccess())
			{
				return {
					.m_Status = DevelopmentShaderArtifactBootstrapStatus::RegistryBuildFailed,
					.m_Error = std::format("Failed to build the Program Registry Artifact (status={}).",
						static_cast<uint32_t>(registryBuild.m_Status)),
				};
			}
			const ShaderProgramRegistryArtifactPublicationResult registryPublication =
				PublishShaderProgramRegistryArtifact(artifactRoot, registryBuild.m_Artifact);
			if (!registryPublication.IsSuccess())
			{
				return {
					.m_Status =
						DevelopmentShaderArtifactBootstrapStatus::RegistryPublicationFailed,
					.m_Error = std::format("Failed to publish the Program Registry Artifact (status={}).",
						static_cast<uint32_t>(registryPublication.m_Status)),
				};
			}
			return {
				.m_Status = DevelopmentShaderArtifactBootstrapStatus::Succeeded,
				.m_RegistryRef = registryPublication.m_RegistryRef,
			};
		}
		catch (...)
		{
			return {
				.m_Status = DevelopmentShaderArtifactBootstrapStatus::Failed,
				.m_Error = "Development shader artifact bootstrap failed unexpectedly.",
			};
		}
	}
}
