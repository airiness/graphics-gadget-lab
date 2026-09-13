#pragma once

#include "GGLabRuntime/Graphics/RHI/RHITypes.h"
#include "ShaderArtifactRuntime/ShaderPreviewPublication.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace gglab
{
	class ShaderManager;

	enum class ShaderPreviewRuntimeBackendReadStatus : uint8_t
	{
		Success,
		InvalidInput,
		ActivePublicationUnavailable,
		ActivePublicationInvalid,
		PublicationUnavailable,
		PublicationInvalid,
		IOFailure,
	};

	struct ShaderPreviewRuntimeBackendReadResult final
	{
		ShaderPreviewRuntimeBackendReadStatus m_Status =
			ShaderPreviewRuntimeBackendReadStatus::InvalidInput;
		RHIBackendType m_Backend = RHIBackendType::Unknown;

		[[nodiscard]] constexpr bool IsSuccess() const noexcept
		{
			return m_Status == ShaderPreviewRuntimeBackendReadStatus::Success;
		}
	};

	// Reads the initial immutable publication far enough to select the matching
	// main-owned Runtime backend. Full artifact and Registry validation remain
	// the candidate reader's startup gate after platform composition.
	[[nodiscard]] ShaderPreviewRuntimeBackendReadResult ReadShaderPreviewRuntimeBackend(
		const std::filesystem::path& artifactRoot, std::string_view sessionId) noexcept;

	enum class ShaderPreviewRuntimeCandidateReadStatus : uint8_t
	{
		Success,
		InvalidInput,
		ActivePublicationUnavailable,
		ActivePublicationInvalid,
		PublicationUnavailable,
		PublicationInvalid,
		ShaderArtifactUnavailable,
		ShaderArtifactInvalid,
		RegistryUnavailable,
		RegistryInvalid,
		IOFailure,
	};

	struct ShaderPreviewRuntimeCandidate final
	{
		ShaderPreviewRuntimeCandidateReadStatus m_Status =
			ShaderPreviewRuntimeCandidateReadStatus::InvalidInput;
		ShaderPreviewActivePublication m_ActivePublication{};
		ShaderPreviewPublicationArtifact m_Publication{};
		ShaderPreviewRejectionCode m_RejectionCode =
			ShaderPreviewRejectionCode::PublicationUnavailable;

		[[nodiscard]] constexpr bool IsSuccess() const noexcept
		{
			return m_Status == ShaderPreviewRuntimeCandidateReadStatus::Success;
		}
	};

	// Resolves and fully cross-validates the active Preview candidate without a
	// compiler or source checkout. Attached startup calls this before composing
	// ShaderManager so no empty-registry or standalone fallback can be created.
	[[nodiscard]] ShaderPreviewRuntimeCandidate ReadShaderPreviewRuntimeCandidate(
		const std::filesystem::path& artifactRoot, std::string_view sessionId,
		RHIBackendType activeBackend) noexcept;

	enum class ShaderPreviewRuntimeObservationPublicationStatus : uint8_t
	{
		Published,
		AlreadyPublished,
		InvalidInput,
		InvalidCurrent,
		NotNewer,
		IOFailure,
	};

	[[nodiscard]] ShaderPreviewRuntimeObservationPublicationStatus
		PublishShaderPreviewRuntimeObservation(
			const std::filesystem::path& artifactRoot, std::string_view sessionId,
			const ShaderPreviewObservation& observation) noexcept;

	enum class ShaderPreviewRuntimeSessionUpdateStatus : uint8_t
	{
		Running,
		StartupFailed,
	};

	enum class ShaderPreviewRuntimeSessionState : uint8_t
	{
		WaitingForInitialLoad,
		Pending,
		Loaded,
		Rejected,
	};

	struct ShaderPreviewRuntimeSessionSnapshot final
	{
		std::string m_SessionId{};
		uint64_t m_ObservedAttemptSequence = 0;
		ShaderPreviewPublicationRef m_ObservedPublicationRef{};
		ShaderPreviewPublicationRef m_LoadedPublicationRef{};
		ShaderPreviewRuntimeSessionState m_State =
			ShaderPreviewRuntimeSessionState::WaitingForInitialLoad;
		ShaderPreviewRejectionCode m_RejectionCode = ShaderPreviewRejectionCode::None;
		std::string m_ActivationError{};
	};

	class ShaderPreviewRuntimeSession final
	{
	public:
		struct CreateInfo final
		{
			std::filesystem::path m_ArtifactRoot{};
			std::string m_SessionId{};
			RHIBackendType m_ActiveBackend = RHIBackendType::Unknown;
			ShaderPreviewRuntimeCandidate m_InitialCandidate{};
			ShaderManager* m_ShaderManager = nullptr;
		};

		explicit ShaderPreviewRuntimeSession(CreateInfo createInfo) noexcept;

		[[nodiscard]] bool IsValid() const noexcept;
		[[nodiscard]] ShaderPreviewRuntimeSessionUpdateStatus Update() noexcept;

		[[nodiscard]] bool HasLoadedPublication() const noexcept
		{
			return m_LoadedPublicationRef.IsValid();
		}
		[[nodiscard]] const ShaderPreviewPublicationArtifact&
			GetLoadedPublication() const noexcept
		{
			return m_LoadedPublication;
		}
		[[nodiscard]] const std::string& GetLastError() const noexcept
		{
			return m_LastError;
		}
		[[nodiscard]] ShaderPreviewRuntimeSessionSnapshot GetSnapshot() const;

	private:
		[[nodiscard]] bool PublishLoadedObservation(
			const ShaderPreviewRuntimeCandidate& candidate) noexcept;
		[[nodiscard]] bool PublishRejectedObservation(
			const ShaderPreviewActivePublication& activePublication,
			ShaderPreviewRejectionCode rejectionCode) noexcept;
		void ProcessCandidate(ShaderPreviewRuntimeCandidate candidate) noexcept;

		std::filesystem::path m_ArtifactRoot{};
		std::string m_SessionId{};
		RHIBackendType m_ActiveBackend = RHIBackendType::Unknown;
		ShaderPreviewRuntimeCandidate m_InitialCandidate{};
		ShaderManager* m_ShaderManager = nullptr;
		std::optional<ShaderPreviewRuntimeCandidate> m_PendingCandidate{};
		ShaderPreviewPublicationRef m_LoadedPublicationRef{};
		ShaderPreviewPublicationRef m_ObservedPublicationRef{};
		ShaderPreviewPublicationArtifact m_LoadedPublication{};
		uint64_t m_LastObservedAttemptSequence = 0;
		ShaderPreviewRuntimeSessionState m_State =
			ShaderPreviewRuntimeSessionState::WaitingForInitialLoad;
		ShaderPreviewRejectionCode m_RejectionCode = ShaderPreviewRejectionCode::None;
		std::chrono::steady_clock::time_point m_NextScan{};
		std::string m_LastError{};
		bool m_InitialObservationPublished = false;
		bool m_StartupFailed = false;
	};
}
