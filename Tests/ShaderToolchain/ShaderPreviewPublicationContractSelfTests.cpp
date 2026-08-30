#include "ShaderPreviewPublicationContractSelfTests.h"

#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"
#include "ShaderArtifactRuntime/ShaderPreviewLooseIO.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <utility>

namespace gglab
{
	namespace
	{
		[[nodiscard]] Sha256Digest MakeDigest(uint8_t seed) noexcept
		{
			Sha256Digest digest{};
			for (size_t index = 0; index < digest.m_Value.size(); ++index)
			{
				digest.m_Value[index] = static_cast<std::byte>(seed + index);
			}
			return digest;
		}

		[[nodiscard]] uint8_t HexValue(char character) noexcept
		{
			return character >= '0' && character <= '9'
				? static_cast<uint8_t>(character - '0')
				: static_cast<uint8_t>(character - 'a' + 10);
		}

		[[nodiscard]] Sha256Digest ParseDigest(std::string_view lowerHex) noexcept
		{
			Sha256Digest digest{};
			if (lowerHex.size() != digest.m_Value.size() * 2)
			{
				return digest;
			}
			for (size_t index = 0; index < digest.m_Value.size(); ++index)
			{
				digest.m_Value[index] = static_cast<std::byte>(
					(HexValue(lowerHex[index * 2]) << 4u) |
					HexValue(lowerHex[index * 2 + 1]));
			}
			return digest;
		}

		[[nodiscard]] ShaderArtifactRef MakeArtifactRef(uint8_t seed) noexcept
		{
			return {
				.m_ArtifactId = ShaderArtifactId{
					.m_DurableDigest = MakeDigest(seed),
				},
			};
		}

		[[nodiscard]] ShaderRuntimeArtifactManifest MakePreviewManifest() noexcept
		{
			ShaderRuntimeArtifactManifest manifest{
				.m_TargetProfile = ShaderTargetProfile::GGLabDX12,
				.m_BinaryFormat = ShaderBinaryFormat::Dxil,
				.m_SpirVTargetEnvironment = ShaderSpirVTargetEnvironment::None,
				.m_BindingABIRevision = 0,
				.m_CoordinateOptions = ShaderCoordinateOptions::None,
				.m_Stage = ShaderStage::Pixel,
				.m_EntryPoint = "PSMain",
				.m_BinaryContentDigest = BinaryContentDigest{
					.m_Digest = MakeDigest(91),
				},
			};
			manifest.m_ArtifactId = ComputeShaderArtifactId(manifest);
			return manifest;
		}

		[[nodiscard]] ShaderProgramRegistryArtifact BuildBaseRegistry() noexcept
		{
			const std::array entries{
				ShaderProgramRegistryEntry{
					.m_ProgramRef = shader_programs::ForwardCoverageVertex,
					.m_TargetProfile = ShaderTargetProfile::GGLabDX12,
					.m_ArtifactRef = MakeArtifactRef(1),
				},
				ShaderProgramRegistryEntry{
					.m_ProgramRef = shader_programs::ShaderGraphPreviewSurfaceV1Pixel,
					.m_TargetProfile = ShaderTargetProfile::GGLabDX12,
					.m_ArtifactRef = MakeArtifactRef(2),
				},
				ShaderProgramRegistryEntry{
					.m_ProgramRef = shader_programs::ShaderGraphPreviewSurfaceV2Pixel,
					.m_TargetProfile = ShaderTargetProfile::GGLabDX12,
					.m_ArtifactRef = MakeArtifactRef(3),
				},
			};
			return BuildShaderProgramRegistryArtifact(entries).m_Artifact;
		}

		[[nodiscard]] ShaderPreviewPublicationBuildResult BuildPublication(
			const ShaderRuntimeArtifactManifest& manifest,
			const ShaderProgramRegistryArtifact& baseRegistry,
			const ShaderProgramRegistryArtifact& previewRegistry) noexcept
		{
			return BuildShaderPreviewPublicationArtifact(
				ShaderPreviewPublicationArtifact{
					.m_PreviewProgramDescriptorIdentity = ParseDigest(
						ShaderGraphPreviewProgramDescriptorIdentity),
					.m_PreviewInputContractId =
						std::string(ShaderGraphPreviewNumericInputContractId),
					.m_ProfileId = std::string(ShaderGraphPreviewSurfaceProfileId),
					.m_ProfileVersion = 1,
					.m_GeneratedSourceIdentity = MakeDigest(71),
					.m_TargetProfile = ShaderTargetProfile::GGLabDX12,
					.m_ProgramRef =
						shader_programs::ShaderGraphPreviewSurfaceV1Pixel,
					.m_ShaderArtifactRef = ShaderArtifactRef{
						.m_ArtifactId = manifest.m_ArtifactId,
					},
					.m_BaseRegistryRef = ShaderProgramRegistryArtifactRef{
						.m_RegistryId = baseRegistry.m_RegistryId,
					},
					.m_PreviewRegistryRef = ShaderProgramRegistryArtifactRef{
						.m_RegistryId = previewRegistry.m_RegistryId,
					},
				});
		}

		void RunPublicationIdentityAndOverlayTests(SelfTestContext& context) noexcept
		{
			const ShaderRuntimeArtifactManifest manifest = MakePreviewManifest();
			const ShaderArtifactRef previewArtifactRef{
				.m_ArtifactId = manifest.m_ArtifactId,
			};
			const ShaderProgramRegistryArtifact baseRegistry = BuildBaseRegistry();
			const ShaderPreviewRegistryOverlayBuildResult overlay =
				BuildShaderPreviewRegistryOverlay(
					baseRegistry,
					shader_programs::ShaderGraphPreviewSurfaceV1Pixel,
					ShaderTargetProfile::GGLabDX12,
					previewArtifactRef);
			context.Check(
				overlay.IsSuccess() &&
					overlay.m_Artifact.m_Entries.size() == baseRegistry.m_Entries.size() &&
					ResolveShaderProgramRegistryArtifact(
						overlay.m_Artifact,
						shader_programs::ShaderGraphPreviewSurfaceV1Pixel,
						ShaderTargetProfile::GGLabDX12) == previewArtifactRef &&
					ResolveShaderProgramRegistryArtifact(
						overlay.m_Artifact,
						shader_programs::ShaderGraphPreviewSurfaceV2Pixel,
						ShaderTargetProfile::GGLabDX12) ==
						ResolveShaderProgramRegistryArtifact(
							baseRegistry,
							shader_programs::ShaderGraphPreviewSurfaceV2Pixel,
							ShaderTargetProfile::GGLabDX12),
				"Preview Registry overlay replaces one selected binding and preserves "
				"the complete base snapshot");

			const ShaderPreviewPublicationBuildResult publication =
				BuildPublication(manifest, baseRegistry, overlay.m_Artifact);
			context.Check(
				publication.IsSuccess() &&
					ValidateShaderPreviewPublicationLinks(
						publication.m_Artifact,
						manifest,
						baseRegistry,
						overlay.m_Artifact) ==
						ShaderPreviewPublicationLinkValidationStatus::Valid,
				"Preview Publication validates exact descriptor, ProgramRef, Artifact "
				"and complete Registry cross-links");

			ShaderPreviewPublicationArtifact changedSource = publication.m_Artifact;
			changedSource.m_GeneratedSourceIdentity = MakeDigest(72);
			const ShaderPreviewPublicationBuildResult changedSourceBuild =
				BuildShaderPreviewPublicationArtifact(std::move(changedSource));
			context.Check(
				changedSourceBuild.IsSuccess() &&
					changedSourceBuild.m_Artifact.m_PublicationId !=
						publication.m_Artifact.m_PublicationId,
				"Preview Publication identity covers exact generated-source identity");

			ShaderPreviewPublicationArtifact wrongProfile = publication.m_Artifact;
			wrongProfile.m_ProfileVersion = 2;
			ShaderPreviewPublicationArtifact wrongProgram = publication.m_Artifact;
			wrongProgram.m_ProgramRef = shader_programs::ShaderGraphPreviewSurfaceV2Pixel;
			context.Check(
				BuildShaderPreviewPublicationArtifact(std::move(wrongProfile)).m_Status ==
					ShaderPreviewPublicationValidationStatus::InvalidProfile &&
					BuildShaderPreviewPublicationArtifact(std::move(wrongProgram)).m_Status ==
						ShaderPreviewPublicationValidationStatus::InvalidProgram,
				"Preview Publication rejects profile and ProgramRef projections that "
				"disagree with the input contract");

			const ShaderProgramRegistryArtifact missingBindingBase =
				BuildShaderProgramRegistryArtifact(std::span(
					baseRegistry.m_Entries.data(), 1)).m_Artifact;
			context.Check(
				BuildShaderPreviewRegistryOverlay(
					missingBindingBase,
					shader_programs::ShaderGraphPreviewSurfaceV1Pixel,
					ShaderTargetProfile::GGLabDX12,
					previewArtifactRef).m_Status ==
					ShaderPreviewRegistryOverlayBuildStatus::MissingBinding,
				"Preview Registry overlay requires the selected binding in the validated base snapshot");

			auto changedEntries = baseRegistry.m_Entries;
			changedEntries[0].m_ArtifactRef = MakeArtifactRef(81);
			const ShaderProgramRegistryArtifact changedBase =
				BuildShaderProgramRegistryArtifact(changedEntries).m_Artifact;
			const ShaderPreviewRegistryOverlayBuildResult invalidOverlay =
				BuildShaderPreviewRegistryOverlay(
					changedBase,
					shader_programs::ShaderGraphPreviewSurfaceV1Pixel,
					ShaderTargetProfile::GGLabDX12,
					previewArtifactRef);
			const ShaderPreviewPublicationBuildResult invalidPublication =
				BuildPublication(manifest, baseRegistry, invalidOverlay.m_Artifact);
			context.Check(
				invalidOverlay.IsSuccess() && invalidPublication.IsSuccess() &&
					ValidateShaderPreviewPublicationLinks(
						invalidPublication.m_Artifact,
						manifest,
						baseRegistry,
						invalidOverlay.m_Artifact) ==
						ShaderPreviewPublicationLinkValidationStatus::NonPreviewBindingChanged,
				"Preview Publication rejects a Registry overlay that changes any non-selected binding");
		}

		void RunPublicationCodecAndLocatorTests(SelfTestContext& context) noexcept
		{
			const ShaderRuntimeArtifactManifest manifest = MakePreviewManifest();
			const ShaderProgramRegistryArtifact baseRegistry = BuildBaseRegistry();
			const ShaderPreviewRegistryOverlayBuildResult overlay =
				BuildShaderPreviewRegistryOverlay(
					baseRegistry,
					shader_programs::ShaderGraphPreviewSurfaceV1Pixel,
					ShaderTargetProfile::GGLabDX12,
					ShaderArtifactRef{ .m_ArtifactId = manifest.m_ArtifactId });
			const ShaderPreviewPublicationBuildResult publication =
				BuildPublication(manifest, baseRegistry, overlay.m_Artifact);
			const SerializedShaderPreviewPublication serialized =
				SerializeShaderPreviewPublication(publication.m_Artifact);
			const std::optional<ShaderPreviewPublicationArtifact> roundTrip =
				DeserializeShaderPreviewPublication(serialized);
			context.Check(
				publication.IsSuccess() && roundTrip.has_value() &&
					*roundTrip == publication.m_Artifact &&
					serialized.size() == SerializedShaderPreviewPublicationFixedSize +
						publication.m_Artifact.m_PreviewInputContractId.size() +
						publication.m_Artifact.m_ProfileId.size() +
						publication.m_Artifact.m_ProgramRef.m_ProgramId.size() +
						publication.m_Artifact.m_ProgramRef.m_VariantId.size(),
				"Preview Publication canonical codec round-trips the complete immutable payload");

			SerializedShaderPreviewPublication corruptIdentity = serialized;
			corruptIdentity[16] ^= std::byte{ 1 };
			SerializedShaderPreviewPublication unknownVersion = serialized;
			unknownVersion[8] = std::byte{ 2 };
			SerializedShaderPreviewPublication unknownSchema = serialized;
			unknownSchema[12] = std::byte{ 2 };
			SerializedShaderPreviewPublication trailing = serialized;
			trailing.push_back(std::byte{});
			context.Check(
				!DeserializeShaderPreviewPublication(corruptIdentity).has_value() &&
					!DeserializeShaderPreviewPublication(unknownVersion).has_value() &&
					!DeserializeShaderPreviewPublication(unknownSchema).has_value() &&
					!DeserializeShaderPreviewPublication(trailing).has_value() &&
					!DeserializeShaderPreviewPublication(
						std::span(serialized).first(serialized.size() - 1)).has_value(),
				"Preview Publication codec rejects identity corruption, unknown versions, "
				"truncation and trailing bytes");

			const std::filesystem::path root = L"D:\\shader-artifact-root";
			const ShaderPreviewPublicationRef publicationRef{
				.m_PublicationId = publication.m_Artifact.m_PublicationId,
			};
			const ShaderLoosePreviewPublicationPath path =
				ShaderLoosePreviewPublicationLocator(root).GetPath(publicationRef);
			const std::string publicationId = Sha256DigestToHex(
				publicationRef.m_PublicationId.m_DurableDigest);
			context.Check(
				path.m_Path == root / "shader-preview" / publicationId.substr(0, 2) /
					(publicationId + ".ggsh.preview") &&
					ShaderLoosePreviewPublicationLocator(L"relative-root")
						.GetPath(publicationRef).m_Path.empty(),
				"Preview Publication locator derives a contained content-addressed path "
				"only from an absolute root and valid ref");
		}

		void RunSessionCoordinationTests(SelfTestContext& context) noexcept
		{
			const ShaderPreviewPublicationRef publicationRef{
				.m_PublicationId = ShaderPreviewPublicationId{
					.m_DurableDigest = MakeDigest(31),
				},
			};
			const ShaderPreviewPublicationRef lastGoodRef{
				.m_PublicationId = ShaderPreviewPublicationId{
					.m_DurableDigest = MakeDigest(32),
				},
			};
			const ShaderPreviewActivePublication active{
				.m_AttemptSequence = 7,
				.m_PublicationRef = publicationRef,
			};
			const SerializedShaderPreviewActivePublication serializedActive =
				SerializeShaderPreviewActivePublication(active);
			const std::optional<ShaderPreviewActivePublication> activeRoundTrip =
				DeserializeShaderPreviewActivePublication(serializedActive);
			context.Check(
				activeRoundTrip.has_value() && *activeRoundTrip == active,
				"Preview active-publication pointer codec round-trips attempt ordering and immutable ref");
			SerializedShaderPreviewActivePublication unknownActiveVersion = serializedActive;
			unknownActiveVersion[8] = std::byte{ 2 };
			SerializedShaderPreviewActivePublication unknownActiveSchema = serializedActive;
			unknownActiveSchema[12] = std::byte{ 2 };
			context.Check(
				!DeserializeShaderPreviewActivePublication(unknownActiveVersion).has_value() &&
					!DeserializeShaderPreviewActivePublication(unknownActiveSchema).has_value() &&
					!DeserializeShaderPreviewActivePublication(
						std::span(serializedActive).first(serializedActive.size() - 1)).has_value(),
				"Preview active-publication pointer codec rejects unknown versions and non-exact length");

			ShaderPreviewActivePublication sameSequence = active;
			sameSequence.m_PublicationRef = lastGoodRef;
			ShaderPreviewActivePublication newer = sameSequence;
			newer.m_AttemptSequence = 8;
			ShaderPreviewActivePublication invalidCurrent{};
			context.Check(
				ValidateShaderPreviewActivePublicationOrdering(nullptr, active) ==
					ShaderPreviewActivePublicationOrderingStatus::Publishable &&
					ValidateShaderPreviewActivePublicationOrdering(&active, sameSequence) ==
						ShaderPreviewActivePublicationOrderingStatus::NotNewer &&
					ValidateShaderPreviewActivePublicationOrdering(&active, newer) ==
						ShaderPreviewActivePublicationOrderingStatus::Publishable &&
					ValidateShaderPreviewActivePublicationOrdering(
						&invalidCurrent, newer) ==
							ShaderPreviewActivePublicationOrderingStatus::InvalidCurrent,
				"Preview active-publication ordering accepts only a strictly newer attempt sequence");

			const ShaderPreviewObservation loaded{
				.m_ObservedAttemptSequence = 7,
				.m_ObservedPublicationRef = publicationRef,
				.m_LoadedPublicationRef = publicationRef,
				.m_Status = ShaderPreviewObservationStatus::Loaded,
				.m_RejectionCode = ShaderPreviewRejectionCode::None,
			};
			const ShaderPreviewObservation rejected{
				.m_ObservedAttemptSequence = 8,
				.m_ObservedPublicationRef = lastGoodRef,
				.m_LoadedPublicationRef = publicationRef,
				.m_Status = ShaderPreviewObservationStatus::Rejected,
				.m_RejectionCode = ShaderPreviewRejectionCode::ActivationFailed,
			};
			ShaderPreviewObservation initialRejected = rejected;
			initialRejected.m_LoadedPublicationRef = {};
			const SerializedShaderPreviewObservation serializedLoaded =
				SerializeShaderPreviewObservation(loaded);
			const SerializedShaderPreviewObservation serializedRejected =
				SerializeShaderPreviewObservation(rejected);
			context.Check(
				DeserializeShaderPreviewObservation(serializedLoaded) == loaded &&
					DeserializeShaderPreviewObservation(serializedRejected) == rejected,
				"Preview observation codec represents both Current and rejected-with-LastGood "
				"states without prose");
			context.Check(
				ValidateShaderPreviewObservationOrdering(nullptr, loaded) ==
					ShaderPreviewObservationOrderingStatus::Publishable &&
					ValidateShaderPreviewObservationOrdering(nullptr, initialRejected) ==
						ShaderPreviewObservationOrderingStatus::Publishable &&
					ValidateShaderPreviewObservationOrdering(&loaded, rejected) ==
						ShaderPreviewObservationOrderingStatus::Publishable,
				"Preview observation ordering accepts a newer rejection only when it preserves LastGood");

			ShaderPreviewObservation lostLastGood = rejected;
			lostLastGood.m_LoadedPublicationRef = {};
			ShaderPreviewObservation staleObservation = rejected;
			staleObservation.m_ObservedAttemptSequence = loaded.m_ObservedAttemptSequence;
			context.Check(
				ValidateShaderPreviewObservationOrdering(&loaded, lostLastGood) ==
					ShaderPreviewObservationOrderingStatus::LastGoodChangedOnRejection &&
					ValidateShaderPreviewObservationOrdering(&loaded, staleObservation) ==
						ShaderPreviewObservationOrderingStatus::NotNewer,
				"Preview observation ordering prevents rejected or stale attempts from "
				"discarding transactional LastGood state");

			ShaderPreviewObservation invalidLoaded = loaded;
			invalidLoaded.m_RejectionCode = ShaderPreviewRejectionCode::ActivationFailed;
			ShaderPreviewObservation invalidRejected = rejected;
			invalidRejected.m_LoadedPublicationRef = lastGoodRef;
			context.Check(
				!IsValidShaderPreviewObservation(invalidLoaded) &&
					!IsValidShaderPreviewObservation(invalidRejected),
				"Preview observation contract rejects contradictory Loaded and Rejected records");

			SerializedShaderPreviewObservation unknownStatus = serializedLoaded;
			unknownStatus[88] = std::byte{ 0xff };
			SerializedShaderPreviewObservation unknownRejection = serializedRejected;
			unknownRejection[89] = std::byte{ 0xff };
			SerializedShaderPreviewObservation unknownObservationVersion = serializedLoaded;
			unknownObservationVersion[8] = std::byte{ 2 };
			SerializedShaderPreviewObservation unknownObservationSchema = serializedLoaded;
			unknownObservationSchema[12] = std::byte{ 2 };
			context.Check(
				!DeserializeShaderPreviewObservation(unknownStatus).has_value() &&
					!DeserializeShaderPreviewObservation(unknownRejection).has_value() &&
					!DeserializeShaderPreviewObservation(unknownObservationVersion).has_value() &&
					!DeserializeShaderPreviewObservation(unknownObservationSchema).has_value() &&
					!DeserializeShaderPreviewObservation(
						std::span(serializedLoaded).first(serializedLoaded.size() - 1)).has_value(),
				"Preview observation codec rejects unknown vocabulary/version and non-exact length");

			constexpr std::string_view SessionId = "0123456789abcdef0123456789abcdef";
			const std::filesystem::path root = L"D:\\shader-artifact-root";
			const ShaderLoosePreviewSessionPaths sessionPaths =
				ShaderLoosePreviewSessionLocator(root, std::string(SessionId)).GetPaths();
			context.Check(
				IsValidShaderPreviewSessionId(SessionId) &&
					!IsValidShaderPreviewSessionId("0123456789ABCDEF0123456789ABCDEF") &&
					!IsValidShaderPreviewSessionId("../0123456789abcdef0123456789abc") &&
					sessionPaths.m_ActivePublicationPath ==
						root / "shader-preview-sessions" / SessionId /
							"active.ggsh.preview-active" &&
					sessionPaths.m_ObservationPath ==
						root / "shader-preview-sessions" / SessionId /
							"observed.ggsh.preview-observed" &&
					ShaderLoosePreviewSessionLocator(
						root, "../0123456789abcdef0123456789abc").GetPaths()
						.m_ActivePublicationPath.empty(),
				"Preview session locator rejects malformed IDs before constructing "
				"contained mutable-record paths");
		}
	}

	void RunShaderPreviewPublicationContractSelfTests(SelfTestContext& context) noexcept
	{
		RunPublicationIdentityAndOverlayTests(context);
		RunPublicationCodecAndLocatorTests(context);
		RunSessionCoordinationTests(context);
	}
}
