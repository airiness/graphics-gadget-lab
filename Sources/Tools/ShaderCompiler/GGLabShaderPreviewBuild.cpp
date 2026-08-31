#include "GGLabShaderPreviewBuild.h"

#include "GGLabRuntimeShaderBuild.h"
#include "ShaderCompilerProcessFactory.h"
#include "Artifact/ShaderRuntimeArtifactPublication.h"
#include "Compiler/ShaderCompiler.h"
#include "GGLabFoundation/Hash/Sha256.h"
#include "GGLabFoundation/IO/PathUtils.h"
#include "GGLabFoundation/Platform/Win/Win32NamedMutex.h"
#include "GGLabFoundation/Platform/Win/Win32StringUtils.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"
#include "ShaderArtifactRuntime/ShaderArtifactStore.h"
#include "ShaderArtifactRuntime/ShaderLooseArtifactIO.h"
#include "ShaderArtifactRuntime/ShaderPreviewLooseIO.h"
#include "Targets/DX12ShaderTarget.h"
#include "Targets/Vulkan13ShaderTarget.h"

#include <process.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace gglab
{
	namespace
	{
		constexpr std::wstring_view PreviewExternalSourceDefine =
			L"GGLAB_SHADER_GRAPH_PREVIEW_EXTERNAL_SOURCE";

		[[nodiscard]] ShaderCompileTarget MakeTarget(
			ShaderTargetProfile profile) noexcept
		{
			return profile == ShaderTargetProfile::GGLabVulkan13
				? MakeVulkan13CompileTarget(ShaderStage::Pixel)
				: MakeDX12CompileTarget(ShaderStage::Pixel);
		}

		[[nodiscard]] ShaderArtifactCompatibilityRequest MakeCompatibilityRequest(
			ShaderTargetProfile profile) noexcept
		{
			const ShaderCompileTarget target = MakeTarget(profile);
			return {
				.m_TargetProfile = profile,
				.m_BinaryFormat = target.m_BinaryFormat,
				.m_SpirVTargetEnvironment = target.m_SpirVTargetEnvironment,
				.m_BindingABIRevision = target.m_BindingABIRevision,
				.m_CoordinateOptions = target.m_CoordinateOptions,
				.m_Stage = ShaderStage::Pixel,
			};
		}

		[[nodiscard]] std::filesystem::path ResolveAdapterSourcePath(
			const ShaderProgramRef& programRef) noexcept
		{
			using namespace shader_programs;
			if (programRef == ShaderGraphPreviewSurfaceV1Pixel)
			{
				return L"Programs/ShaderGraphPreview/ShaderGraphPreviewSurfaceV1.hlsl";
			}
			if (programRef == ShaderGraphPreviewSurfaceV2Pixel)
			{
				return L"Programs/ShaderGraphPreview/ShaderGraphPreviewSurfaceV2.hlsl";
			}
			return {};
		}

		[[nodiscard]] bool ReadGeneratedSource(
			const std::filesystem::path& path,
			std::vector<std::byte>& outBytes) noexcept
		{
			try
			{
				std::error_code errorCode;
				const uintmax_t fileSize = std::filesystem::file_size(path, errorCode);
				if (errorCode || fileSize == 0 ||
					fileSize > MaxShaderPreviewGeneratedSourceSize)
				{
					return false;
				}
				outBytes.resize(static_cast<size_t>(fileSize));
				std::ifstream input(path, std::ios::binary);
				if (!input)
				{
					return false;
				}
				input.read(
					reinterpret_cast<char*>(outBytes.data()),
					static_cast<std::streamsize>(outBytes.size()));
				return input.gcount() == static_cast<std::streamsize>(outBytes.size()) &&
					input.peek() == std::char_traits<char>::eof();
			}
			catch (...)
			{
				return false;
			}
		}

		[[nodiscard]] bool FileEquals(
			const std::filesystem::path& path,
			std::span<const std::byte> expectedBytes) noexcept
		{
			std::vector<std::byte> bytes;
			return ReadGeneratedSource(path, bytes) && bytes.size() == expectedBytes.size() &&
				std::ranges::equal(bytes, expectedBytes);
		}

		[[nodiscard]] std::filesystem::path MakeUniqueTempPath(
			const std::filesystem::path& destination)
		{
			static std::atomic_uint64_t counter = 0;
			return destination.wstring() + L".tmp." +
				std::to_wstring(static_cast<uint32_t>(::_getpid())) + L"." +
				std::to_wstring(counter.fetch_add(1, std::memory_order_relaxed));
		}

		void RemoveFileBestEffort(const std::filesystem::path& path) noexcept
		{
			std::error_code ignored;
			std::filesystem::remove(path, ignored);
		}

		[[nodiscard]] std::optional<std::filesystem::path> StageGeneratedSource(
			const std::filesystem::path& cacheRoot,
			const Sha256Digest& identity,
			std::span<const std::byte> bytes) noexcept
		{
			try
			{
				const std::filesystem::path sourcePath =
					cacheRoot / "shader-preview-source" / Sha256DigestToHex(identity) /
					"Generated" / "SurfaceGenerated.hlsli";
				if (FileEquals(sourcePath, bytes))
				{
					return sourcePath.parent_path().parent_path();
				}
				if (!utils::CreateParentDirectoryIfNotExist(sourcePath))
				{
					return std::nullopt;
				}
				const std::filesystem::path tempPath = MakeUniqueTempPath(sourcePath);
				if (!utils::WriteFileBinary(tempPath, bytes) ||
					!FileEquals(tempPath, bytes))
				{
					RemoveFileBestEffort(tempPath);
					return std::nullopt;
				}
				const BOOL replaced = ::MoveFileExW(
					tempPath.c_str(), sourcePath.c_str(),
					MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
				RemoveFileBestEffort(tempPath);
				if (replaced == FALSE || !FileEquals(sourcePath, bytes))
				{
					return std::nullopt;
				}
				return sourcePath.parent_path().parent_path();
			}
			catch (...)
			{
				return std::nullopt;
			}
		}

		[[nodiscard]] bool ValidateRequest(
			const GGLabShaderPreviewBuildRequest& request,
			const ShaderGraphPreviewInputContractProjection*& outProjection) noexcept
		{
			outProjection = ResolveShaderGraphPreviewInputContract(
				request.m_PreviewInputContractId);
			return IsKnownShaderTargetProfile(request.m_TargetProfile) &&
				request.m_SourceRoot.is_absolute() &&
				request.m_GeneratedSourcePath.is_absolute() &&
				request.m_CacheRoot.is_absolute() &&
				request.m_ArtifactRoot.is_absolute() &&
				IsValidShaderPreviewSessionId(request.m_SessionId) &&
				IsKnownShaderGraphPreviewProgramDescriptorIdentity(
					request.m_PreviewProgramDescriptorIdentity) &&
				request.m_GeneratedSourceIdentity.IsValid() && outProjection &&
				outProjection->m_ProgramRef &&
				request.m_ProfileId == ShaderGraphPreviewSurfaceProfileId &&
				request.m_ProfileVersion == outProjection->m_ProfileVersion &&
				!ResolveAdapterSourcePath(*outProjection->m_ProgramRef).empty();
		}
	}

	GGLabShaderPreviewBuildResult BuildGGLabShaderPreview(
		const GGLabShaderPreviewBuildRequest& request) noexcept
	{
		GGLabShaderPreviewBuildResult result{
			.m_AttemptSequence = request.m_AttemptSequence,
		};
		const ShaderGraphPreviewInputContractProjection* projection = nullptr;
		if (!ValidateRequest(request, projection))
		{
			result.m_Status = GGLabShaderPreviewBuildStatus::InvalidInput;
			result.m_Error = "Preview build request does not match the supported contract.";
			return result;
		}

		try
		{
			std::vector<std::byte> generatedSource;
			if (!ReadGeneratedSource(request.m_GeneratedSourcePath, generatedSource))
			{
				result.m_Status =
					GGLabShaderPreviewBuildStatus::GeneratedSourceUnavailable;
				result.m_Error = "Generated Preview source could not be read.";
				return result;
			}
			if (ComputeSha256(generatedSource) != request.m_GeneratedSourceIdentity)
			{
				result.m_Status =
					GGLabShaderPreviewBuildStatus::GeneratedSourceIdentityMismatch;
				result.m_Error = "Generated Preview source identity does not match its bytes.";
				return result;
			}

			const std::wstring mutexName =
				MakeGGLabShaderArtifactWriterMutexName(request.m_ArtifactRoot);
			win32::NamedMutex writerMutex(mutexName);
			win32::NamedMutexGuard writerLease = writerMutex.Acquire(120'000);
			if (!writerLease.IsAcquired())
			{
				result.m_Status = GGLabShaderPreviewBuildStatus::WriterUnavailable;
				result.m_Error =
					"Timed out waiting for the artifact-root shader writer lease.";
				return result;
			}

			ShaderLooseActiveProgramRegistryReader activeRegistryReader{
				ShaderLooseActiveProgramRegistryLocator(
					request.m_ArtifactRoot, request.m_TargetProfile)
			};
			const ActiveShaderProgramRegistryReadResult activeRegistry =
				activeRegistryReader.Read();
			ShaderLooseProgramRegistryArtifactReader registryReader{
				ShaderLooseProgramRegistryArtifactLocator(request.m_ArtifactRoot)
			};
			const ShaderProgramRegistryArtifactReadResult baseRegistry =
				activeRegistry.IsSuccess()
				? registryReader.ReadArtifact(activeRegistry.m_RegistryRef)
				: ShaderProgramRegistryArtifactReadResult{};
			if (!baseRegistry.IsSuccess())
			{
				result.m_Status = GGLabShaderPreviewBuildStatus::BaseRegistryUnavailable;
				result.m_Error =
					"The target's ordinary active Program Registry is unavailable.";
				return result;
			}
			result.m_BaseRegistryRef = activeRegistry.m_RegistryRef;

			const std::optional<std::filesystem::path> stagedSourceRoot =
				StageGeneratedSource(
					request.m_CacheRoot,
					request.m_GeneratedSourceIdentity,
					generatedSource);
			if (!stagedSourceRoot)
			{
				result.m_Status =
					GGLabShaderPreviewBuildStatus::GeneratedSourceUnavailable;
				result.m_Error = "Generated Preview source staging failed.";
				return result;
			}

			std::unique_ptr<ShaderCompiler> compiler = CreateShaderCompilerForProcess(
				request.m_SourceRoot, request.m_CacheRoot);
			ShaderDesc defaults{};
			defaults.m_IncludeDirs = { request.m_SourceRoot, *stagedSourceRoot };
			compiler->SetDefaultShaderConfig(defaults);
			ShaderDesc desc{
				.m_SourcePath = ResolveAdapterSourcePath(*projection->m_ProgramRef),
				.m_Stage = ShaderStage::Pixel,
				.m_Entry = L"PSMain",
			};
			desc.m_Target = MakeTarget(request.m_TargetProfile);
			desc.m_Target.m_Flags = ShaderCompileFlags::Optimization;
			desc.m_Defines.push_back({
				.m_Name = std::wstring(PreviewExternalSourceDefine),
				.m_Value = L"1",
			});
			const ShaderResolvedRecipe recipe = compiler->Resolve(desc);
			const ShaderCompileResult compiled = recipe.IsSuccess()
				? compiler->CompileOrLoad(recipe)
				: ShaderCompileResult{
					.m_Status = recipe.m_Diagnostics.m_Status,
					.m_Diagnostics = recipe.m_Diagnostics,
				};
			if (!compiled.IsSuccess())
			{
				result.m_Status = compiled.m_Status == ShaderCompileStatus::CompilerUnavailable
					? GGLabShaderPreviewBuildStatus::CompilerUnavailable
					: GGLabShaderPreviewBuildStatus::CompileFailed;
				result.m_Error = utils::ToString(compiled.m_Diagnostics.m_Message);
				return result;
			}

			const ShaderRuntimeArtifactPublicationResult shaderPublication =
				PublishShaderRuntimeArtifact(request.m_ArtifactRoot, compiled.m_Artifact);
			if (!shaderPublication.IsSuccess())
			{
				result.m_Status =
					GGLabShaderPreviewBuildStatus::ArtifactPublicationFailed;
				result.m_Error = "Failed to publish the Preview Runtime Shader Artifact.";
				return result;
			}
			result.m_ShaderArtifactRef = shaderPublication.m_ArtifactRef;

			const ShaderPreviewRegistryOverlayBuildResult overlay =
				BuildShaderPreviewRegistryOverlay(
					baseRegistry.m_Artifact,
					*projection->m_ProgramRef,
					request.m_TargetProfile,
					shaderPublication.m_ArtifactRef);
			if (!overlay.IsSuccess())
			{
				result.m_Status = GGLabShaderPreviewBuildStatus::RegistryBuildFailed;
				result.m_Error = "Failed to compose the complete Preview Registry overlay.";
				return result;
			}
			const ShaderProgramRegistryArtifactPublicationResult registryPublication =
				PublishShaderProgramRegistryArtifact(
					request.m_ArtifactRoot, overlay.m_Artifact);
			if (!registryPublication.IsSuccess())
			{
				result.m_Status =
					GGLabShaderPreviewBuildStatus::RegistryPublicationFailed;
				result.m_Error = "Failed to publish the Preview Registry Artifact.";
				return result;
			}
			result.m_PreviewRegistryRef = registryPublication.m_RegistryRef;

			const ShaderRuntimeArtifact runtimeArtifact =
				BuildShaderRuntimeArtifact(compiled.m_Artifact);
			const ShaderPreviewPublicationBuildResult publicationBuild =
				BuildShaderPreviewPublicationArtifact({
					.m_PreviewProgramDescriptorIdentity =
						request.m_PreviewProgramDescriptorIdentity,
					.m_PreviewInputContractId = request.m_PreviewInputContractId,
					.m_ProfileId = request.m_ProfileId,
					.m_ProfileVersion = request.m_ProfileVersion,
					.m_GeneratedSourceIdentity = request.m_GeneratedSourceIdentity,
					.m_TargetProfile = request.m_TargetProfile,
					.m_ProgramRef = *projection->m_ProgramRef,
					.m_ShaderArtifactRef = shaderPublication.m_ArtifactRef,
					.m_BaseRegistryRef = activeRegistry.m_RegistryRef,
					.m_PreviewRegistryRef = registryPublication.m_RegistryRef,
				});
			if (!publicationBuild.IsSuccess())
			{
				result.m_Status =
					GGLabShaderPreviewBuildStatus::PublicationBuildFailed;
				result.m_Error = "Failed to build the immutable Preview Publication.";
				return result;
			}
			if (ValidateShaderPreviewPublicationLinks(
					publicationBuild.m_Artifact,
					runtimeArtifact.m_Manifest,
					baseRegistry.m_Artifact,
					overlay.m_Artifact) !=
				ShaderPreviewPublicationLinkValidationStatus::Valid)
			{
				result.m_Status =
					GGLabShaderPreviewBuildStatus::PublicationValidationFailed;
				result.m_Error = "Preview Publication cross-link validation failed.";
				return result;
			}

			const ShaderPreviewPublicationArtifactPublicationResult publication =
				PublishShaderPreviewPublicationArtifact(
					request.m_ArtifactRoot, publicationBuild.m_Artifact);
			if (!publication.IsSuccess())
			{
				result.m_Status =
					GGLabShaderPreviewBuildStatus::PublicationPublicationFailed;
				result.m_Error = "Failed to publish the immutable Preview Publication.";
				return result;
			}
			result.m_PublicationRef = publication.m_PublicationRef;

			ShaderLooseArtifactReader shaderReader{
				ShaderLooseArtifactLocator(request.m_ArtifactRoot)
			};
			ShaderArtifactStore shaderStore(shaderReader);
			const ShaderArtifactLoadResult observedShader = shaderStore.LoadArtifact(
				shaderPublication.m_ArtifactRef,
				MakeCompatibilityRequest(request.m_TargetProfile));
			const ShaderProgramRegistryArtifactReadResult observedRegistry =
				registryReader.ReadArtifact(registryPublication.m_RegistryRef);
			ShaderLoosePreviewPublicationReader previewReader{
				ShaderLoosePreviewPublicationLocator(request.m_ArtifactRoot)
			};
			const ShaderPreviewPublicationReadResult observedPublication =
				previewReader.ReadArtifact(publication.m_PublicationRef);
			if (!observedShader.IsSuccess() || !observedRegistry.IsSuccess() ||
				!observedPublication.IsSuccess() ||
				ValidateShaderPreviewPublicationLinks(
					observedPublication.m_Artifact,
					observedShader.m_Artifact.m_Manifest,
					baseRegistry.m_Artifact,
					observedRegistry.m_Artifact) !=
					ShaderPreviewPublicationLinkValidationStatus::Valid)
			{
				result.m_Status =
					GGLabShaderPreviewBuildStatus::PublicationValidationFailed;
				result.m_Error = "Published Preview products failed compiler-free re-read.";
				return result;
			}

			const ShaderPreviewActivePublicationPublicationResult activePublication =
				PublishShaderPreviewActivePublication(
					request.m_ArtifactRoot,
					request.m_SessionId,
					ShaderPreviewActivePublication{
						.m_AttemptSequence = request.m_AttemptSequence,
						.m_PublicationRef = publication.m_PublicationRef,
					});
			if (!activePublication.IsSuccess())
			{
				result.m_Status = activePublication.m_Status ==
					ShaderPreviewActivePublicationPublicationStatus::NotNewer
					? GGLabShaderPreviewBuildStatus::StaleAttempt
					: GGLabShaderPreviewBuildStatus::ActivePublicationFailed;
				result.m_Error = result.m_Status ==
					GGLabShaderPreviewBuildStatus::StaleAttempt
					? "Preview attempt is not newer than the active session attempt."
					: "Failed to publish the active Preview session pointer.";
				return result;
			}

			result.m_Status = GGLabShaderPreviewBuildStatus::Succeeded;
			return result;
		}
		catch (...)
		{
			result.m_Status = GGLabShaderPreviewBuildStatus::Failed;
			result.m_Error = "GGLab Preview shader build failed unexpectedly.";
			return result;
		}
	}
}
