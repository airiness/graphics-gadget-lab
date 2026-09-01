#include "Application/Shader/ShaderPreviewRuntimeSession.h"

#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/IO/PathUtils.h"
#include "Graphics/Shader/ShaderManager.h"
#include "ShaderArtifactRuntime/ShaderArtifactStore.h"
#include "ShaderArtifactRuntime/ShaderLooseArtifactIO.h"
#include "ShaderArtifactRuntime/ShaderPreviewLooseIO.h"
#include "ShaderArtifactRuntime/VulkanShaderRuntimeABI.h"

#include <process.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace gglab
{
	namespace
	{
		constexpr std::chrono::milliseconds PreviewSessionScanInterval{ 100 };

		[[nodiscard]] constexpr bool IsSupportedBackend(RHIBackendType backend) noexcept
		{
			return backend == RHIBackendType::DX12 || backend == RHIBackendType::Vulkan;
		}

		[[nodiscard]] constexpr ShaderTargetProfile GetTargetProfile(
			RHIBackendType backend) noexcept
		{
			return backend == RHIBackendType::Vulkan
				? ShaderTargetProfile::GGLabVulkan13
				: ShaderTargetProfile::GGLabDX12;
		}

		[[nodiscard]] constexpr ShaderArtifactCompatibilityRequest
			MakePreviewCompatibilityRequest(RHIBackendType backend) noexcept
		{
			if (backend == RHIBackendType::Vulkan)
			{
				return {
					.m_TargetProfile = ShaderTargetProfile::GGLabVulkan13,
					.m_BinaryFormat = ShaderBinaryFormat::SpirV,
					.m_SpirVTargetEnvironment = ShaderSpirVTargetEnvironment::Vulkan1_3,
					.m_BindingABIRevision = GGLabVulkanShaderRuntimeABI.m_Revision,
					.m_CoordinateOptions =
						GetGGLabVulkanShaderCoordinateOptions(ShaderStage::Pixel),
					.m_Stage = ShaderStage::Pixel,
				};
			}
			return {
				.m_TargetProfile = ShaderTargetProfile::GGLabDX12,
				.m_BinaryFormat = ShaderBinaryFormat::Dxil,
				.m_SpirVTargetEnvironment = ShaderSpirVTargetEnvironment::None,
				.m_BindingABIRevision = 0,
				.m_CoordinateOptions = ShaderCoordinateOptions::None,
				.m_Stage = ShaderStage::Pixel,
			};
		}

		[[nodiscard]] ShaderPreviewRuntimeCandidateReadStatus MapActiveReadStatus(
			ShaderPreviewActivePublicationReadStatus status) noexcept
		{
			switch (status)
			{
			case ShaderPreviewActivePublicationReadStatus::NotFound:
				return ShaderPreviewRuntimeCandidateReadStatus::ActivePublicationUnavailable;
			case ShaderPreviewActivePublicationReadStatus::MalformedRecord:
				return ShaderPreviewRuntimeCandidateReadStatus::ActivePublicationInvalid;
			case ShaderPreviewActivePublicationReadStatus::IOFailure:
			default:
				return ShaderPreviewRuntimeCandidateReadStatus::IOFailure;
			}
		}

		[[nodiscard]] ShaderPreviewRuntimeBackendReadStatus MapBackendActiveReadStatus(
			ShaderPreviewActivePublicationReadStatus status) noexcept
		{
			switch (status)
			{
			case ShaderPreviewActivePublicationReadStatus::NotFound:
				return ShaderPreviewRuntimeBackendReadStatus::ActivePublicationUnavailable;
			case ShaderPreviewActivePublicationReadStatus::MalformedRecord:
				return ShaderPreviewRuntimeBackendReadStatus::ActivePublicationInvalid;
			case ShaderPreviewActivePublicationReadStatus::IOFailure:
			default:
				return ShaderPreviewRuntimeBackendReadStatus::IOFailure;
			}
		}

		[[nodiscard]] ShaderPreviewRuntimeCandidate ReadCandidateForActivePublication(
			const std::filesystem::path& artifactRoot,
			const ShaderPreviewActivePublication& activePublication,
			RHIBackendType activeBackend) noexcept
		{
			ShaderPreviewRuntimeCandidate result{};
			result.m_ActivePublication = activePublication;
			try
			{
				ShaderLoosePreviewPublicationReader publicationReader{
					ShaderLoosePreviewPublicationLocator(artifactRoot)
				};
				const ShaderPreviewPublicationReadResult publicationRead =
					publicationReader.ReadArtifact(activePublication.m_PublicationRef);
				if (!publicationRead.IsSuccess())
				{
					if (publicationRead.m_Status == ShaderPreviewPublicationReadStatus::NotFound)
					{
						result.m_Status = ShaderPreviewRuntimeCandidateReadStatus::PublicationUnavailable;
						result.m_RejectionCode = ShaderPreviewRejectionCode::PublicationUnavailable;
					}
					else if (publicationRead.m_Status ==
						ShaderPreviewPublicationReadStatus::MalformedArtifact)
					{
						result.m_Status = ShaderPreviewRuntimeCandidateReadStatus::PublicationInvalid;
						result.m_RejectionCode = ShaderPreviewRejectionCode::PublicationInvalid;
					}
					else
					{
						result.m_Status = ShaderPreviewRuntimeCandidateReadStatus::IOFailure;
						result.m_RejectionCode = ShaderPreviewRejectionCode::IOFailure;
					}
					return result;
				}

				result.m_Publication = publicationRead.m_Artifact;
				if (result.m_Publication.m_TargetProfile != GetTargetProfile(activeBackend))
				{
					result.m_Status = ShaderPreviewRuntimeCandidateReadStatus::PublicationInvalid;
					result.m_RejectionCode = ShaderPreviewRejectionCode::PublicationInvalid;
					return result;
				}

				ShaderLooseArtifactReader artifactReader{
					ShaderLooseArtifactLocator(artifactRoot)
				};
				ShaderArtifactStore artifactStore(artifactReader);
				const ShaderArtifactLoadResult shaderArtifact = artifactStore.LoadArtifact(
					result.m_Publication.m_ShaderArtifactRef,
					MakePreviewCompatibilityRequest(activeBackend));
				if (!shaderArtifact.IsSuccess())
				{
					if (shaderArtifact.m_Status == ShaderArtifactLoadStatus::NotFound)
					{
						result.m_Status = ShaderPreviewRuntimeCandidateReadStatus::ShaderArtifactUnavailable;
						result.m_RejectionCode = ShaderPreviewRejectionCode::ShaderArtifactUnavailable;
					}
					else if (shaderArtifact.m_Status == ShaderArtifactLoadStatus::ReadFailure)
					{
						result.m_Status = ShaderPreviewRuntimeCandidateReadStatus::IOFailure;
						result.m_RejectionCode = ShaderPreviewRejectionCode::IOFailure;
					}
					else
					{
						result.m_Status = ShaderPreviewRuntimeCandidateReadStatus::ShaderArtifactInvalid;
						result.m_RejectionCode = ShaderPreviewRejectionCode::ShaderArtifactInvalid;
					}
					return result;
				}

				ShaderLooseProgramRegistryArtifactReader registryReader{
					ShaderLooseProgramRegistryArtifactLocator(artifactRoot)
				};
				const ShaderProgramRegistryArtifactReadResult baseRegistry =
					registryReader.ReadArtifact(result.m_Publication.m_BaseRegistryRef);
				const ShaderProgramRegistryArtifactReadResult previewRegistry =
					registryReader.ReadArtifact(result.m_Publication.m_PreviewRegistryRef);
				if (!baseRegistry.IsSuccess() || !previewRegistry.IsSuccess())
				{
					const bool missing =
						baseRegistry.m_Status == ShaderProgramRegistryArtifactReadStatus::NotFound ||
						previewRegistry.m_Status == ShaderProgramRegistryArtifactReadStatus::NotFound;
					const bool malformed =
						baseRegistry.m_Status == ShaderProgramRegistryArtifactReadStatus::MalformedArtifact ||
						previewRegistry.m_Status == ShaderProgramRegistryArtifactReadStatus::MalformedArtifact;
					result.m_Status = missing
						? ShaderPreviewRuntimeCandidateReadStatus::RegistryUnavailable
						: malformed ? ShaderPreviewRuntimeCandidateReadStatus::RegistryInvalid
									: ShaderPreviewRuntimeCandidateReadStatus::IOFailure;
					result.m_RejectionCode = missing
						? ShaderPreviewRejectionCode::RegistryUnavailable
						: malformed ? ShaderPreviewRejectionCode::RegistryInvalid
									: ShaderPreviewRejectionCode::IOFailure;
					return result;
				}

				if (ValidateShaderPreviewPublicationLinks(result.m_Publication,
					shaderArtifact.m_Artifact.m_Manifest, baseRegistry.m_Artifact,
					previewRegistry.m_Artifact) !=
					ShaderPreviewPublicationLinkValidationStatus::Valid)
				{
					result.m_Status = ShaderPreviewRuntimeCandidateReadStatus::RegistryInvalid;
					result.m_RejectionCode = ShaderPreviewRejectionCode::RegistryInvalid;
					return result;
				}

				result.m_Status = ShaderPreviewRuntimeCandidateReadStatus::Success;
				result.m_RejectionCode = ShaderPreviewRejectionCode::None;
			}
			catch (...)
			{
				result.m_Status = ShaderPreviewRuntimeCandidateReadStatus::IOFailure;
				result.m_RejectionCode = ShaderPreviewRejectionCode::IOFailure;
			}
			return result;
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

		[[nodiscard]] bool WriteFileThrough(
			const std::filesystem::path& path, std::span<const std::byte> bytes) noexcept
		{
			if (!utils::CreateParentDirectoryIfNotExist(path) || bytes.size() > MAXDWORD)
			{
				return false;
			}
			const HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
				CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
			if (file == INVALID_HANDLE_VALUE)
			{
				return false;
			}
			DWORD bytesWritten = 0;
			const BOOL wrote = ::WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()),
				&bytesWritten, nullptr);
			const BOOL flushed = wrote != FALSE ? ::FlushFileBuffers(file) : FALSE;
			const BOOL closed = ::CloseHandle(file);
			return wrote != FALSE && bytesWritten == static_cast<DWORD>(bytes.size()) &&
				flushed != FALSE && closed != FALSE;
		}
	}

	ShaderPreviewRuntimeBackendReadResult ReadShaderPreviewRuntimeBackend(
		const std::filesystem::path& artifactRoot, std::string_view sessionId) noexcept
	{
		ShaderPreviewRuntimeBackendReadResult result{};
		if (artifactRoot.empty() || !artifactRoot.is_absolute() ||
			!IsValidShaderPreviewSessionId(sessionId))
		{
			return result;
		}
		try
		{
			ShaderLoosePreviewSessionReader sessionReader(
				ShaderLoosePreviewSessionLocator(artifactRoot, std::string(sessionId)));
			const ShaderPreviewActivePublicationReadResult active =
				sessionReader.ReadActivePublication();
			if (!active.IsSuccess())
			{
				result.m_Status = MapBackendActiveReadStatus(active.m_Status);
				return result;
			}

			ShaderLoosePreviewPublicationReader publicationReader{
				ShaderLoosePreviewPublicationLocator(artifactRoot)
			};
			const ShaderPreviewPublicationReadResult publication =
				publicationReader.ReadArtifact(active.m_ActivePublication.m_PublicationRef);
			if (!publication.IsSuccess())
			{
				if (publication.m_Status == ShaderPreviewPublicationReadStatus::NotFound)
				{
					result.m_Status =
						ShaderPreviewRuntimeBackendReadStatus::PublicationUnavailable;
				}
				else if (publication.m_Status ==
					ShaderPreviewPublicationReadStatus::MalformedArtifact)
				{
					result.m_Status = ShaderPreviewRuntimeBackendReadStatus::PublicationInvalid;
				}
				else
				{
					result.m_Status = ShaderPreviewRuntimeBackendReadStatus::IOFailure;
				}
				return result;
			}

			switch (publication.m_Artifact.m_TargetProfile)
			{
			case ShaderTargetProfile::GGLabDX12:
				result.m_Backend = RHIBackendType::DX12;
				break;
			case ShaderTargetProfile::GGLabVulkan13:
				result.m_Backend = RHIBackendType::Vulkan;
				break;
			default:
				result.m_Status = ShaderPreviewRuntimeBackendReadStatus::PublicationInvalid;
				return result;
			}
			result.m_Status = ShaderPreviewRuntimeBackendReadStatus::Success;
		}
		catch (...)
		{
			result.m_Status = ShaderPreviewRuntimeBackendReadStatus::IOFailure;
		}
		return result;
	}

	ShaderPreviewRuntimeCandidate ReadShaderPreviewRuntimeCandidate(
		const std::filesystem::path& artifactRoot, std::string_view sessionId,
		RHIBackendType activeBackend) noexcept
	{
		ShaderPreviewRuntimeCandidate result{};
		if (artifactRoot.empty() || !artifactRoot.is_absolute() ||
			!IsValidShaderPreviewSessionId(sessionId) || !IsSupportedBackend(activeBackend))
		{
			return result;
		}
		try
		{
			ShaderLoosePreviewSessionReader sessionReader(
				ShaderLoosePreviewSessionLocator(artifactRoot, std::string(sessionId)));
			const ShaderPreviewActivePublicationReadResult active =
				sessionReader.ReadActivePublication();
			if (!active.IsSuccess())
			{
				result.m_Status = MapActiveReadStatus(active.m_Status);
				result.m_RejectionCode =
					active.m_Status == ShaderPreviewActivePublicationReadStatus::IOFailure
					? ShaderPreviewRejectionCode::IOFailure
					: ShaderPreviewRejectionCode::PublicationUnavailable;
				return result;
			}
			return ReadCandidateForActivePublication(
				artifactRoot, active.m_ActivePublication, activeBackend);
		}
		catch (...)
		{
			result.m_Status = ShaderPreviewRuntimeCandidateReadStatus::IOFailure;
			result.m_RejectionCode = ShaderPreviewRejectionCode::IOFailure;
			return result;
		}
	}

	ShaderPreviewRuntimeObservationPublicationStatus
		PublishShaderPreviewRuntimeObservation(
			const std::filesystem::path& artifactRoot, std::string_view sessionId,
			const ShaderPreviewObservation& observation) noexcept
	{
		if (artifactRoot.empty() || !artifactRoot.is_absolute() ||
			!IsValidShaderPreviewSessionId(sessionId) ||
			!IsValidShaderPreviewObservation(observation))
		{
			return ShaderPreviewRuntimeObservationPublicationStatus::InvalidInput;
		}
		ShaderPreviewRuntimeObservationPublicationStatus result =
			ShaderPreviewRuntimeObservationPublicationStatus::IOFailure;
		try
		{
			const ShaderLoosePreviewSessionLocator locator(
				artifactRoot, std::string(sessionId));
			const ShaderLoosePreviewSessionPaths paths = locator.GetPaths();
			ShaderLoosePreviewSessionReader reader(locator);
			const ShaderPreviewObservationReadResult current = reader.ReadObservation();
			if (current.IsSuccess() && current.m_Observation == observation)
			{
				return ShaderPreviewRuntimeObservationPublicationStatus::AlreadyPublished;
			}
			if (current.m_Status == ShaderPreviewObservationReadStatus::MalformedRecord)
			{
				return ShaderPreviewRuntimeObservationPublicationStatus::InvalidCurrent;
			}
			if (current.m_Status == ShaderPreviewObservationReadStatus::IOFailure)
			{
				return ShaderPreviewRuntimeObservationPublicationStatus::IOFailure;
			}
			const ShaderPreviewObservationOrderingStatus ordering =
				ValidateShaderPreviewObservationOrdering(
					current.IsSuccess() ? &current.m_Observation : nullptr, observation);
			if (ordering != ShaderPreviewObservationOrderingStatus::Publishable)
			{
				return ordering == ShaderPreviewObservationOrderingStatus::NotNewer
					? ShaderPreviewRuntimeObservationPublicationStatus::NotNewer
					: ShaderPreviewRuntimeObservationPublicationStatus::InvalidInput;
			}

			const SerializedShaderPreviewObservation serialized =
				SerializeShaderPreviewObservation(observation);
			const std::filesystem::path tempPath = MakeUniqueTempPath(paths.m_ObservationPath);
			if (!WriteFileThrough(tempPath, serialized))
			{
				RemoveFileBestEffort(tempPath);
				return ShaderPreviewRuntimeObservationPublicationStatus::IOFailure;
			}
			const BOOL replaced = ::MoveFileExW(tempPath.c_str(), paths.m_ObservationPath.c_str(),
				MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
			RemoveFileBestEffort(tempPath);
			if (replaced == FALSE)
			{
				return result;
			}
			// MoveFileExW is the commit point. Keep the required post-commit read as
			// best-effort verification, but never turn an externally visible commit
			// into a false I/O failure.
			result = ShaderPreviewRuntimeObservationPublicationStatus::Published;
			const ShaderPreviewObservationReadResult observed = reader.ReadObservation();
			const bool postCommitObservationSucceeded =
				observed.IsSuccess() && observed.m_Observation == observation;
			GGLAB_UNUSED(postCommitObservationSucceeded);
		}
		catch (...)
		{
		}
		return result;
	}

	ShaderPreviewRuntimeSession::ShaderPreviewRuntimeSession(CreateInfo createInfo) noexcept :
		m_ArtifactRoot(std::move(createInfo.m_ArtifactRoot)),
		m_SessionId(std::move(createInfo.m_SessionId)),
		m_ActiveBackend(createInfo.m_ActiveBackend),
		m_InitialCandidate(std::move(createInfo.m_InitialCandidate)),
		m_ShaderManager(createInfo.m_ShaderManager),
		m_NextScan(std::chrono::steady_clock::now() + PreviewSessionScanInterval)
	{
	}

	bool ShaderPreviewRuntimeSession::IsValid() const noexcept
	{
		return !m_ArtifactRoot.empty() && m_ArtifactRoot.is_absolute() &&
			IsValidShaderPreviewSessionId(m_SessionId) && IsSupportedBackend(m_ActiveBackend) &&
			m_InitialCandidate.IsSuccess() && m_ShaderManager && m_ShaderManager->IsReady() &&
			m_InitialCandidate.m_Publication.m_TargetProfile == GetTargetProfile(m_ActiveBackend);
	}

	ShaderPreviewRuntimeSessionSnapshot ShaderPreviewRuntimeSession::GetSnapshot() const
	{
		return {
			.m_SessionId = m_SessionId,
			.m_ObservedAttemptSequence = m_LastObservedAttemptSequence,
			.m_ObservedPublicationRef = m_ObservedPublicationRef,
			.m_LoadedPublicationRef = m_LoadedPublicationRef,
			.m_State = m_State,
			.m_RejectionCode = m_RejectionCode,
			.m_ActivationError = m_LastError,
		};
	}

	ShaderPreviewRuntimeSessionUpdateStatus ShaderPreviewRuntimeSession::Update() noexcept
	{
		if (m_StartupFailed || !IsValid())
		{
			return ShaderPreviewRuntimeSessionUpdateStatus::StartupFailed;
		}

		if (!m_InitialObservationPublished)
		{
			const ShaderPreloadStatus preload = m_ShaderManager->GetPreloadStatus();
			if (preload.HasFailed())
			{
				GGLAB_UNUSED(PublishRejectedObservation(m_InitialCandidate.m_ActivePublication,
					ShaderPreviewRejectionCode::ActivationFailed));
				m_LastError = preload.m_Error.empty()
					? "Initial attached Preview shader preload was cancelled."
					: preload.m_Error;
				m_StartupFailed = true;
				return ShaderPreviewRuntimeSessionUpdateStatus::StartupFailed;
			}
			if (!preload.IsReady())
			{
				return ShaderPreviewRuntimeSessionUpdateStatus::Running;
			}
			if (!PublishLoadedObservation(m_InitialCandidate))
			{
				m_LastError = "Failed to publish the initial attached Preview Runtime observation.";
				m_StartupFailed = true;
				return ShaderPreviewRuntimeSessionUpdateStatus::StartupFailed;
			}
			m_InitialObservationPublished = true;
		}

		if (m_PendingCandidate)
		{
			ShaderPreviewRuntimeCandidate pending = std::move(*m_PendingCandidate);
			m_PendingCandidate.reset();
			ProcessCandidate(std::move(pending));
			if (m_PendingCandidate)
			{
				return ShaderPreviewRuntimeSessionUpdateStatus::Running;
			}
		}

		const auto now = std::chrono::steady_clock::now();
		if (now < m_NextScan)
		{
			return ShaderPreviewRuntimeSessionUpdateStatus::Running;
		}
		m_NextScan = now + PreviewSessionScanInterval;

		try
		{
			ShaderLoosePreviewSessionReader sessionReader(
				ShaderLoosePreviewSessionLocator(m_ArtifactRoot, m_SessionId));
			const ShaderPreviewActivePublicationReadResult active =
				sessionReader.ReadActivePublication();
			if (!active.IsSuccess())
			{
				m_LastError = "The attached Preview active-publication pointer is unreadable.";
				return ShaderPreviewRuntimeSessionUpdateStatus::Running;
			}
			if (active.m_ActivePublication.m_AttemptSequence <= m_LastObservedAttemptSequence)
			{
				return ShaderPreviewRuntimeSessionUpdateStatus::Running;
			}

			ShaderPreviewRuntimeCandidate candidate = ReadCandidateForActivePublication(
				m_ArtifactRoot, active.m_ActivePublication, m_ActiveBackend);
			if (!candidate.IsSuccess())
			{
				if (PublishRejectedObservation(
					candidate.m_ActivePublication, candidate.m_RejectionCode))
				{
					m_LastError = "Rejected an invalid attached Preview publication candidate.";
				}
				return ShaderPreviewRuntimeSessionUpdateStatus::Running;
			}
			ProcessCandidate(std::move(candidate));
		}
		catch (...)
		{
			m_LastError = "Attached Preview session polling failed unexpectedly.";
		}
		return ShaderPreviewRuntimeSessionUpdateStatus::Running;
	}

	bool ShaderPreviewRuntimeSession::PublishLoadedObservation(
		const ShaderPreviewRuntimeCandidate& candidate) noexcept
	{
		m_LoadedPublicationRef = candidate.m_ActivePublication.m_PublicationRef;
		m_LoadedPublication = candidate.m_Publication;
		const ShaderPreviewObservation observation{
			.m_ObservedAttemptSequence = candidate.m_ActivePublication.m_AttemptSequence,
			.m_ObservedPublicationRef = candidate.m_ActivePublication.m_PublicationRef,
			.m_LoadedPublicationRef = candidate.m_ActivePublication.m_PublicationRef,
			.m_Status = ShaderPreviewObservationStatus::Loaded,
			.m_RejectionCode = ShaderPreviewRejectionCode::None,
		};
		const ShaderPreviewRuntimeObservationPublicationStatus status =
			PublishShaderPreviewRuntimeObservation(m_ArtifactRoot, m_SessionId, observation);
		if (status != ShaderPreviewRuntimeObservationPublicationStatus::Published &&
			status != ShaderPreviewRuntimeObservationPublicationStatus::AlreadyPublished)
		{
			return false;
		}
		m_LastObservedAttemptSequence = observation.m_ObservedAttemptSequence;
		m_ObservedPublicationRef = observation.m_ObservedPublicationRef;
		m_State = ShaderPreviewRuntimeSessionState::Loaded;
		m_RejectionCode = ShaderPreviewRejectionCode::None;
		m_LastError.clear();
		return true;
	}

	bool ShaderPreviewRuntimeSession::PublishRejectedObservation(
		const ShaderPreviewActivePublication& activePublication,
		ShaderPreviewRejectionCode rejectionCode) noexcept
	{
		const ShaderPreviewObservation observation{
			.m_ObservedAttemptSequence = activePublication.m_AttemptSequence,
			.m_ObservedPublicationRef = activePublication.m_PublicationRef,
			.m_LoadedPublicationRef = m_LoadedPublicationRef,
			.m_Status = ShaderPreviewObservationStatus::Rejected,
			.m_RejectionCode = rejectionCode,
		};
		const ShaderPreviewRuntimeObservationPublicationStatus status =
			PublishShaderPreviewRuntimeObservation(m_ArtifactRoot, m_SessionId, observation);
		if (status != ShaderPreviewRuntimeObservationPublicationStatus::Published &&
			status != ShaderPreviewRuntimeObservationPublicationStatus::AlreadyPublished)
		{
			return false;
		}
		m_LastObservedAttemptSequence = observation.m_ObservedAttemptSequence;
		m_ObservedPublicationRef = observation.m_ObservedPublicationRef;
		m_State = ShaderPreviewRuntimeSessionState::Rejected;
		m_RejectionCode = rejectionCode;
		return true;
	}

	void ShaderPreviewRuntimeSession::ProcessCandidate(
		ShaderPreviewRuntimeCandidate candidate) noexcept
	{
		m_LastObservedAttemptSequence = candidate.m_ActivePublication.m_AttemptSequence;
		m_ObservedPublicationRef = candidate.m_ActivePublication.m_PublicationRef;
		m_State = ShaderPreviewRuntimeSessionState::Pending;
		m_RejectionCode = ShaderPreviewRejectionCode::None;
		const ShaderRegistryActivationResult activation =
			m_ShaderManager->ActivateRegistry(candidate.m_Publication.m_PreviewRegistryRef);
		if (activation.m_Status == ShaderRegistryActivationStatus::Busy)
		{
			m_PendingCandidate = std::move(candidate);
			return;
		}
		if (!activation.IsSuccess())
		{
			if (!PublishRejectedObservation(candidate.m_ActivePublication,
				ShaderPreviewRejectionCode::ActivationFailed))
			{
				m_PendingCandidate = std::move(candidate);
				m_LastError = "Failed to publish a rejected Preview activation observation.";
			}
			else
			{
				m_LastError = activation.m_Error.empty()
					? "ShaderManager rejected an attached Preview registry."
					: activation.m_Error;
			}
			return;
		}
		if (!PublishLoadedObservation(candidate))
		{
			m_PendingCandidate = std::move(candidate);
			m_LastError = "Activated a Preview registry but failed to publish its observation.";
		}
	}
}
