#include "Application/SelfTest/ShaderPreviewRuntimeSessionSelfTests.h"

#include "Application/Shader/ShaderPreviewRuntimeSession.h"
#include "GGLabFoundation/Hash/Sha256.h"
#include "GGLabFoundation/IO/PathUtils.h"
#include "ShaderArtifactRuntime/ShaderPreviewLooseIO.h"

#include <process.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace gglab
{
	namespace
	{
		[[nodiscard]] ShaderPreviewPublicationRef MakePublicationRef(
			std::string_view identity) noexcept
		{
			Sha256Builder builder;
			GGLAB_UNUSED(builder.AddStringUtf8(identity));
			return {
				.m_PublicationId = {
					.m_DurableDigest = builder.Finish(),
				},
			};
		}
	}

	void RunShaderPreviewRuntimeSessionSelfTests(SelfTestContext& context) noexcept
	{
		const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() /
			("GGLabShaderPreviewRuntimeSession-" + std::to_string(::_getpid()));
		std::error_code errorCode;
		std::filesystem::remove_all(tempRoot, errorCode);
		errorCode.clear();
		std::filesystem::create_directories(tempRoot, errorCode);
		context.Check(!errorCode && tempRoot.is_absolute(),
			"Shader Preview Runtime session test resolves an isolated absolute artifact root");

		constexpr std::string_view SessionId = "0123456789abcdef0123456789abcdef";
		const ShaderPreviewPublicationRef publication1 = MakePublicationRef("publication-1");
		const ShaderPreviewPublicationRef publication2 = MakePublicationRef("publication-2");
		const ShaderPreviewPublicationRef publication3 = MakePublicationRef("publication-3");

		const ShaderPreviewRuntimeCandidate missingActive =
			ReadShaderPreviewRuntimeCandidate(tempRoot, SessionId, RHIBackendType::DX12);
		context.Check(missingActive.m_Status ==
			ShaderPreviewRuntimeCandidateReadStatus::ActivePublicationUnavailable,
			"Attached startup rejects a missing active publication before Runtime composition");

		const ShaderLoosePreviewSessionLocator sessionLocator(
			tempRoot, std::string(SessionId));
		const ShaderLoosePreviewSessionPaths sessionPaths = sessionLocator.GetPaths();
		const ShaderPreviewActivePublication active{
			.m_AttemptSequence = 1,
			.m_PublicationRef = publication1,
		};
		const SerializedShaderPreviewActivePublication serializedActive =
			SerializeShaderPreviewActivePublication(active);
		context.Check(utils::WriteFileBinary(
			sessionPaths.m_ActivePublicationPath, serializedActive),
			"Preview Runtime session fixture writes one valid active pointer");
		const ShaderPreviewRuntimeCandidate missingPublication =
			ReadShaderPreviewRuntimeCandidate(tempRoot, SessionId, RHIBackendType::DX12);
		context.Check(missingPublication.m_Status ==
			ShaderPreviewRuntimeCandidateReadStatus::PublicationUnavailable &&
			missingPublication.m_ActivePublication == active,
			"Attached startup resolves the active pointer and rejects its missing immutable publication");

		const ShaderPreviewObservation loaded1{
			.m_ObservedAttemptSequence = 1,
			.m_ObservedPublicationRef = publication1,
			.m_LoadedPublicationRef = publication1,
			.m_Status = ShaderPreviewObservationStatus::Loaded,
			.m_RejectionCode = ShaderPreviewRejectionCode::None,
		};
		context.Check(PublishShaderPreviewRuntimeObservation(
			tempRoot, SessionId, loaded1) ==
			ShaderPreviewRuntimeObservationPublicationStatus::Published,
			"WinApp atomically publishes the initial Loaded Preview observation");
		ShaderLoosePreviewSessionReader sessionReader(sessionLocator);
		const ShaderPreviewObservationReadResult observedLoaded1 =
			sessionReader.ReadObservation();
		context.Check(observedLoaded1.IsSuccess() && observedLoaded1.m_Observation == loaded1,
			"The compiler-free session reader observes the exact WinApp publication");
		context.Check(PublishShaderPreviewRuntimeObservation(
			tempRoot, SessionId, loaded1) ==
			ShaderPreviewRuntimeObservationPublicationStatus::AlreadyPublished,
			"Repeating the exact Runtime observation is idempotent");

		const ShaderPreviewObservation staleLoaded{
			.m_ObservedAttemptSequence = 0,
			.m_ObservedPublicationRef = publication2,
			.m_LoadedPublicationRef = publication2,
			.m_Status = ShaderPreviewObservationStatus::Loaded,
			.m_RejectionCode = ShaderPreviewRejectionCode::None,
		};
		context.Check(PublishShaderPreviewRuntimeObservation(
			tempRoot, SessionId, staleLoaded) ==
			ShaderPreviewRuntimeObservationPublicationStatus::NotNewer,
			"WinApp observation publication rejects stale attempt ordering");

		const ShaderPreviewObservation rejected2{
			.m_ObservedAttemptSequence = 2,
			.m_ObservedPublicationRef = publication2,
			.m_LoadedPublicationRef = publication1,
			.m_Status = ShaderPreviewObservationStatus::Rejected,
			.m_RejectionCode = ShaderPreviewRejectionCode::ActivationFailed,
		};
		context.Check(PublishShaderPreviewRuntimeObservation(
			tempRoot, SessionId, rejected2) ==
			ShaderPreviewRuntimeObservationPublicationStatus::Published,
			"Rejected Runtime observation preserves the exact loaded last-good publication");

		ShaderPreviewObservation invalidLastGood = rejected2;
		invalidLastGood.m_ObservedAttemptSequence = 3;
		invalidLastGood.m_ObservedPublicationRef = publication3;
		invalidLastGood.m_LoadedPublicationRef = publication2;
		context.Check(PublishShaderPreviewRuntimeObservation(
			tempRoot, SessionId, invalidLastGood) ==
			ShaderPreviewRuntimeObservationPublicationStatus::InvalidInput,
			"Rejected Runtime observation cannot transactionally replace last-good state");

		const ShaderPreviewObservation loaded3{
			.m_ObservedAttemptSequence = 3,
			.m_ObservedPublicationRef = publication3,
			.m_LoadedPublicationRef = publication3,
			.m_Status = ShaderPreviewObservationStatus::Loaded,
			.m_RejectionCode = ShaderPreviewRejectionCode::None,
		};
		context.Check(PublishShaderPreviewRuntimeObservation(
			tempRoot, SessionId, loaded3) ==
			ShaderPreviewRuntimeObservationPublicationStatus::Published,
			"A newer successfully loaded publication transactionally advances Runtime observation");
		context.Check(PublishShaderPreviewRuntimeObservation(
			tempRoot, "invalid", loaded3) ==
			ShaderPreviewRuntimeObservationPublicationStatus::InvalidInput,
			"Malformed session identity is rejected before observation path construction");

		std::filesystem::remove_all(tempRoot, errorCode);
	}
}
