#include "GGLabRuntimeShaderBuild.h"
#include "ShaderCompilerProcessFactory.h"
#include "Artifact/ShaderRuntimeArtifactPublication.h"
#include "Compiler/ShaderCompiler.h"
#include "GGLabFoundation/Hash/Sha256.h"
#include "GGLabFoundation/Platform/Win/Win32NamedMutex.h"
#include "GGLabFoundation/Platform/Win/Win32StringUtils.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"
#include "Targets/DX12ShaderTarget.h"
#include "Targets/Vulkan13ShaderTarget.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gglab
{
	namespace
	{
		struct ShaderProgramBuildDefine final
		{
			std::wstring_view m_Name;
			std::wstring_view m_Value{};
		};

		struct ShaderProgramBuildRecord final
		{
			const ShaderProgramRef* m_ProgramRef = nullptr;
			std::wstring_view m_SourcePath;
			std::wstring_view m_EntryPoint;
			std::span<const ShaderProgramBuildDefine> m_Defines{};
		};

		constexpr ShaderProgramBuildDefine GTAOContributionDefine{
			L"GGLAB_GTAO_CONTRIBUTION_OUTPUT" };
		constexpr ShaderProgramBuildDefine ForwardPlusDefine{ L"GGLAB_FORWARD_PLUS" };
		constexpr ShaderProgramBuildDefine ForwardPlusValidationDefine{
			L"GGLAB_FORWARD_PLUS_VALIDATION" };
		constexpr ShaderProgramBuildDefine ForwardPlusDiagnosticsDefine{
			L"GGLAB_FORWARD_PLUS_DIAGNOSTICS" };
		constexpr ShaderProgramBuildDefine ValidationReduceTilesDefine{
			L"GGLAB_FORWARD_PLUS_VALIDATION_REDUCE_TILES" };
		constexpr ShaderProgramBuildDefine ValidationReduceFrameDefine{
			L"GGLAB_FORWARD_PLUS_VALIDATION_REDUCE_FRAME" };
		constexpr ShaderProgramBuildDefine GTAODiagnosticsDefine{
			L"GGLAB_GTAO_DIAGNOSTICS" };
		constexpr ShaderProgramBuildDefine GTAODenoiseXDefine{ L"GGLAB_GTAO_DENOISE_X" };
		constexpr ShaderProgramBuildDefine GTAODenoiseYDefine{ L"GGLAB_GTAO_DENOISE_Y" };
		constexpr ShaderProgramBuildDefine GTAOUpsampleDefine{ L"GGLAB_GTAO_UPSAMPLE" };

		constexpr std::array LegacyGTAODefines{ GTAOContributionDefine };
		constexpr std::array ForwardPlusDefines{ ForwardPlusDefine };
		constexpr std::array ForwardPlusValidationDefines{
			ForwardPlusDefine, ForwardPlusValidationDefine };
		constexpr std::array ForwardPlusGTAODefines{
			GTAOContributionDefine, ForwardPlusDefine };
		constexpr std::array ForwardPlusValidationGTAODefines{
			GTAOContributionDefine, ForwardPlusDefine, ForwardPlusValidationDefine };
		constexpr std::array ForwardPlusDiagnosticsDefines{ ForwardPlusDiagnosticsDefine };
		constexpr std::array ValidationReduceTilesDefines{ ValidationReduceTilesDefine };
		constexpr std::array ValidationReduceFrameDefines{ ValidationReduceFrameDefine };
		constexpr std::array GTAODiagnosticsDefines{ GTAODiagnosticsDefine };
		constexpr std::array GTAODenoiseXDefines{ GTAODenoiseXDefine };
		constexpr std::array GTAODenoiseYDefines{ GTAODenoiseYDefine };
		constexpr std::array GTAOUpsampleDefines{ GTAOUpsampleDefine };

		using namespace shader_programs;
		const std::array BuildRecords{
			ShaderProgramBuildRecord{ &ForwardCoverageVertex, L"Passes/PassForwardCoverage.hlsl", L"VSMain" },
			ShaderProgramBuildRecord{ &ForwardPBRLegacyPixel, L"Passes/PassForwardPBR.hlsl", L"PSMain" },
			ShaderProgramBuildRecord{ &ForwardPBRForwardPlusPixel, L"Passes/PassForwardPBR.hlsl", L"PSMain", ForwardPlusDefines },
			ShaderProgramBuildRecord{ &ForwardPBRForwardPlusValidationPixel, L"Passes/PassForwardPBR.hlsl", L"PSMain", ForwardPlusValidationDefines },
			ShaderProgramBuildRecord{ &ForwardPBRLegacyGTAOPixel, L"Passes/PassForwardPBR.hlsl", L"PSMain", LegacyGTAODefines },
			ShaderProgramBuildRecord{ &ForwardPBRForwardPlusGTAOPixel, L"Passes/PassForwardPBR.hlsl", L"PSMain", ForwardPlusGTAODefines },
			ShaderProgramBuildRecord{ &ForwardPBRForwardPlusValidationGTAOPixel, L"Passes/PassForwardPBR.hlsl", L"PSMain", ForwardPlusValidationGTAODefines },
			ShaderProgramBuildRecord{ &DepthPrepassAlphaTestPixel, L"Passes/PassDepthPrepass.hlsl", L"PSAlphaTest" },
			ShaderProgramBuildRecord{ &DepthPrepassVelocityOpaquePixel, L"Passes/PassDepthPrepass.hlsl", L"PSVelocityOpaque" },
			ShaderProgramBuildRecord{ &DepthPrepassVelocityAlphaTestPixel, L"Passes/PassDepthPrepass.hlsl", L"PSVelocityAlphaTest" },
			ShaderProgramBuildRecord{ &ForwardPlusCullCompute, L"Passes/PassForwardPlusCull.hlsl", L"CSMain" },
			ShaderProgramBuildRecord{ &ForwardPlusCullDiagnosticsCompute, L"Passes/PassForwardPlusCull.hlsl", L"CSMain", ForwardPlusDiagnosticsDefines },
			ShaderProgramBuildRecord{ &ForwardPlusValidationTilesCompute, L"Passes/PassForwardPlusValidation.hlsl", L"CSReduceTiles", ValidationReduceTilesDefines },
			ShaderProgramBuildRecord{ &ForwardPlusValidationFrameCompute, L"Passes/PassForwardPlusValidation.hlsl", L"CSReduceFrame", ValidationReduceFrameDefines },
			ShaderProgramBuildRecord{ &GTAOEvaluateCompute, L"Passes/PassGTAO.hlsl", L"CSMain" },
			ShaderProgramBuildRecord{ &GTAOEvaluateDiagnosticsCompute, L"Passes/PassGTAO.hlsl", L"CSMain", GTAODiagnosticsDefines },
			ShaderProgramBuildRecord{ &GTAODenoiseXCompute, L"Passes/PassGTAO.hlsl", L"CSMain", GTAODenoiseXDefines },
			ShaderProgramBuildRecord{ &GTAODenoiseYCompute, L"Passes/PassGTAO.hlsl", L"CSMain", GTAODenoiseYDefines },
			ShaderProgramBuildRecord{ &GTAOUpsampleCompute, L"Passes/PassGTAO.hlsl", L"CSMain", GTAOUpsampleDefines },
			ShaderProgramBuildRecord{ &TemporalAAReprojectionCompute, L"Passes/PassTemporalAA.hlsl", L"CSMain" },
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
			ShaderProgramBuildRecord{ &ShaderGraphPreviewSurfaceV1Pixel, L"Programs/ShaderGraphPreview/ShaderGraphPreviewSurfaceV1.hlsl", L"PSMain" },
			ShaderProgramBuildRecord{ &ShaderGraphPreviewSurfaceV2Pixel, L"Programs/ShaderGraphPreview/ShaderGraphPreviewSurfaceV2.hlsl", L"PSMain" },
			ShaderProgramBuildRecord{ &NapaVoxelVertex, L"Passes/PassNapaVoxel.hlsl", L"VSMain" },
			ShaderProgramBuildRecord{ &NapaVoxelPixel, L"Passes/PassNapaVoxel.hlsl", L"PSMain" },
		};

		void AddDefine(ShaderDesc& desc, const ShaderProgramBuildDefine& define)
		{
			desc.m_Defines.push_back({
				.m_Name = std::wstring(define.m_Name),
				.m_Value = define.m_Value.empty() ? L"1" : std::wstring(define.m_Value),
			});
		}

		[[nodiscard]] std::optional<ShaderDesc> MakeBuildDesc(
			const ShaderProgramBuildRecord& record) noexcept
		{
			if (!record.m_ProgramRef)
			{
				return std::nullopt;
			}
			try
			{
				const ShaderProgramRef& programRef = *record.m_ProgramRef;
				ShaderDesc desc{
					.m_SourcePath = record.m_SourcePath,
					.m_Stage = programRef.m_Stage,
					.m_Entry = std::wstring(record.m_EntryPoint),
				};
				for (const ShaderProgramBuildDefine& define : record.m_Defines)
				{
					AddDefine(desc, define);
				}
				return desc;
			}
			catch (...)
			{
				return std::nullopt;
			}
		}

		[[nodiscard]] ShaderCompileTarget MakeTarget(
			ShaderTargetProfile profile, ShaderStage stage) noexcept
		{
			return profile == ShaderTargetProfile::GGLabVulkan13
				? MakeVulkan13CompileTarget(stage)
				: MakeDX12CompileTarget(stage);
		}

		[[nodiscard]] std::wstring MakeWriterMutexNameImpl(
			const std::filesystem::path& artifactRoot) noexcept
		{
			std::wstring identity = artifactRoot.lexically_normal().generic_wstring();
			std::ranges::transform(identity, identity.begin(),
				[](wchar_t character) noexcept
				{ return static_cast<wchar_t>(std::towlower(character)); });
			Sha256Builder builder;
			if (!builder.AddStringUtf8(utils::ToString(identity)))
			{
				return {};
			}
			return L"Local\\GGLab.ShaderRegistryWriter." +
				utils::ToWideString(Sha256DigestToHex(builder.Finish()));
		}
	}

	std::wstring MakeGGLabShaderArtifactWriterMutexName(
		const std::filesystem::path& artifactRoot) noexcept
	{
		return MakeWriterMutexNameImpl(artifactRoot);
	}

	GGLabRuntimeShaderBuildResult BuildGGLabRuntimeShaders(
		ShaderTargetProfile targetProfile,
		const std::filesystem::path& sourceRoot,
		const std::filesystem::path& cacheRoot,
		const std::filesystem::path& artifactRoot) noexcept
	{
		if (!IsKnownShaderTargetProfile(targetProfile) || sourceRoot.empty() ||
			cacheRoot.empty() || artifactRoot.empty() || !sourceRoot.is_absolute() ||
			!cacheRoot.is_absolute() || !artifactRoot.is_absolute())
		{
			return {
				.m_Status = GGLabRuntimeShaderBuildStatus::InvalidInput,
				.m_Error = "Runtime shader build requires a known target and absolute roots.",
			};
		}

		try
		{
			const std::wstring mutexName =
				MakeGGLabShaderArtifactWriterMutexName(artifactRoot);
			win32::NamedMutex writerMutex(mutexName);
			win32::NamedMutexGuard writerLease = writerMutex.Acquire(120'000);
			if (!writerLease.IsAcquired())
			{
				return {
					.m_Status = GGLabRuntimeShaderBuildStatus::WriterUnavailable,
					.m_Error = "Timed out waiting for the artifact-root shader writer lease.",
				};
			}

			std::unique_ptr<ShaderCompiler> compiler =
				CreateShaderCompilerForProcess(sourceRoot, cacheRoot);
			ShaderDesc defaults{};
			defaults.m_IncludeDirs = { sourceRoot };
			compiler->SetDefaultShaderConfig(defaults);

			std::vector<ShaderProgramRegistryEntry> entries;
			entries.reserve(BuildRecords.size());
			for (const ShaderProgramBuildRecord& record : BuildRecords)
			{
				std::optional<ShaderDesc> desc = MakeBuildDesc(record);
				if (!desc)
				{
					return {
						.m_Status = GGLabRuntimeShaderBuildStatus::InvalidInput,
						.m_Error = "The GGLab shader build catalog is invalid.",
					};
				}
				desc->m_Target = MakeTarget(targetProfile, desc->m_Stage);
				desc->m_Target.m_Flags = ShaderCompileFlags::Optimization;
				const ShaderResolvedRecipe recipe = compiler->Resolve(*desc);
				const ShaderCompileResult compiled = recipe.IsSuccess()
					? compiler->CompileOrLoad(recipe)
					: ShaderCompileResult{
						.m_Status = recipe.m_Diagnostics.m_Status,
						.m_Diagnostics = recipe.m_Diagnostics,
					};
				if (!compiled.IsSuccess())
				{
					return {
						.m_Status = compiled.m_Status == ShaderCompileStatus::CompilerUnavailable
							? GGLabRuntimeShaderBuildStatus::CompilerUnavailable
							: GGLabRuntimeShaderBuildStatus::CompileFailed,
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
						.m_Status = GGLabRuntimeShaderBuildStatus::ArtifactPublicationFailed,
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
					.m_Status = GGLabRuntimeShaderBuildStatus::RegistryBuildFailed,
					.m_Error = "Failed to build the GGLab Program Registry Artifact.",
				};
			}
			const ShaderProgramRegistryArtifactPublicationResult registryPublication =
				PublishShaderProgramRegistryArtifact(artifactRoot, registryBuild.m_Artifact);
			if (!registryPublication.IsSuccess())
			{
				return {
					.m_Status = GGLabRuntimeShaderBuildStatus::RegistryPublicationFailed,
					.m_Error = "Failed to publish the GGLab Program Registry Artifact.",
				};
			}
			const ActiveShaderProgramRegistryPublicationResult activePublication =
				PublishActiveShaderProgramRegistry(
					artifactRoot, targetProfile, registryPublication.m_RegistryRef);
			if (!activePublication.IsSuccess())
			{
				return {
					.m_Status = GGLabRuntimeShaderBuildStatus::ActiveRegistryPublicationFailed,
					.m_Error = "Failed to publish the active Program Registry reference.",
				};
			}
			return {
				.m_Status = GGLabRuntimeShaderBuildStatus::Succeeded,
				.m_RegistryRef = activePublication.m_RegistryRef,
				.m_ProgramCount = static_cast<uint32_t>(entries.size()),
			};
		}
		catch (...)
		{
			return {
				.m_Status = GGLabRuntimeShaderBuildStatus::Failed,
				.m_Error = "GGLab runtime shader build failed unexpectedly.",
			};
		}
	}
}
