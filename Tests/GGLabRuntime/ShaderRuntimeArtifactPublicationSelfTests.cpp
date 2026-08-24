#include "ShaderRuntimeArtifactPublicationSelfTests.h"

#include "Artifact/ShaderRuntimeArtifactPublication.h"
#include "GGLabFoundation/Hash/Sha256.h"
#include "GGLabFoundation/IO/PathUtils.h"
#include "ShaderArtifactRuntime/ShaderArtifactStore.h"
#include "ShaderArtifactRuntime/ShaderLooseArtifactIO.h"

#include <process.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <system_error>
#include <thread>
#include <vector>

namespace gglab
{
	namespace
	{
		[[nodiscard]] ShaderBinary MakeDxilBinary(std::byte tail = std::byte{}) noexcept
		{
			ShaderBinary binary(20);
			std::memset(binary.Data(), 0, binary.SizeInBytes());
			std::memcpy(binary.Data(), "DXBC", 4);
			static_cast<std::byte*>(binary.Data())[19] = tail;
			return binary;
		}

		[[nodiscard]] ShaderBinary MakeSpirVBinary() noexcept
		{
			constexpr std::array<uint32_t, 5> Header{
				0x07230203u,
				0x00010600u,
				0u,
				1u,
				0u,
			};
			ShaderBinary binary(sizeof(Header));
			std::memcpy(binary.Data(), Header.data(), sizeof(Header));
			return binary;
		}

		void RefreshDigest(ShaderArtifact& artifact) noexcept
		{
			artifact.m_Manifest.m_BinaryContentDigest.m_Digest = ComputeSha256(std::span(
				static_cast<const std::byte*>(artifact.m_Binary.Data()),
				artifact.m_Binary.SizeInBytes()));
		}

		[[nodiscard]] ShaderArtifact MakeDxilArtifact(
			std::byte tail = std::byte{}) noexcept
		{
			ShaderArtifact artifact{};
			artifact.m_Manifest.m_TargetProfile = ShaderTargetProfile::GGLabDX12;
			artifact.m_Manifest.m_BinaryFormat = ShaderBinaryFormat::Dxil;
			artifact.m_Manifest.m_SpirVTargetEnvironment =
				ShaderSpirVTargetEnvironment::None;
			artifact.m_Manifest.m_BindingABIRevision = 0;
			artifact.m_Manifest.m_CoordinateOptions = ShaderCoordinateOptions::None;
			artifact.m_Manifest.m_Stage = ShaderStage::Vertex;
			artifact.m_Manifest.m_EntryPoint = L"VSMain";
			artifact.m_Binary = MakeDxilBinary(tail);
			RefreshDigest(artifact);
			return artifact;
		}

		[[nodiscard]] ShaderArtifact MakeSpirVArtifact() noexcept
		{
			ShaderArtifact artifact{};
			artifact.m_Manifest.m_TargetProfile = ShaderTargetProfile::GGLabVulkan13;
			artifact.m_Manifest.m_BinaryFormat = ShaderBinaryFormat::SpirV;
			artifact.m_Manifest.m_SpirVTargetEnvironment =
				ShaderSpirVTargetEnvironment::Vulkan1_3;
			artifact.m_Manifest.m_BindingABIRevision = 1;
			artifact.m_Manifest.m_CoordinateOptions = ShaderCoordinateOptions::InvertY;
			artifact.m_Manifest.m_Stage = ShaderStage::Vertex;
			artifact.m_Manifest.m_EntryPoint = L"VSMain";
			artifact.m_Binary = MakeSpirVBinary();
			RefreshDigest(artifact);
			return artifact;
		}

		[[nodiscard]] ShaderArtifactCompatibilityRequest MakeCompatibilityRequest(
			const ShaderRuntimeArtifactManifest& manifest) noexcept
		{
			return {
				.m_TargetProfile = manifest.m_TargetProfile,
				.m_BinaryFormat = manifest.m_BinaryFormat,
				.m_SpirVTargetEnvironment = manifest.m_SpirVTargetEnvironment,
				.m_BindingABIRevision = manifest.m_BindingABIRevision,
				.m_CoordinateOptions = manifest.m_CoordinateOptions,
				.m_Stage = manifest.m_Stage,
			};
		}

		[[nodiscard]] bool BinaryEquals(
			const ShaderBinary& left, const ShaderBinary& right) noexcept
		{
			return left.SizeInBytes() == right.SizeInBytes() &&
				std::memcmp(left.Data(), right.Data(), left.SizeInBytes()) == 0;
		}

		void RunCodecAndLocatorTests(
			SelfTestContext& context,
			const std::filesystem::path& root,
			const ShaderRuntimeArtifact& runtimeArtifact) noexcept
		{
			const SerializedShaderRuntimeArtifactManifest serialized =
				SerializeShaderRuntimeArtifactManifest(runtimeArtifact.m_Manifest);
			const std::optional<ShaderRuntimeArtifactManifest> roundTrip =
				DeserializeShaderRuntimeArtifactManifest(serialized);
			context.Check(
				roundTrip.has_value() && *roundTrip == runtimeArtifact.m_Manifest,
				"Runtime manifest binary codec round-trips executable metadata");
			context.Check(
				static_cast<uint8_t>(ShaderTargetProfile::GGLabDX12) == 0 &&
					static_cast<uint8_t>(ShaderTargetProfile::GGLabVulkan13) == 1 &&
					static_cast<uint8_t>(ShaderBinaryFormat::Dxil) == 1 &&
					static_cast<uint8_t>(ShaderBinaryFormat::SpirV) == 2 &&
					static_cast<uint8_t>(ShaderSpirVTargetEnvironment::Vulkan1_3) == 1 &&
					static_cast<uint32_t>(ShaderStage::Vertex) == 0 &&
					static_cast<uint32_t>(ShaderStage::Compute) == 6,
				"Persisted shader vocabulary has pinned wire values");
			context.Check(
				serialized.size() == SerializedShaderRuntimeArtifactManifestFixedSize +
					runtimeArtifact.m_Manifest.m_EntryPoint.size() &&
					serialized[48] == static_cast<std::byte>(
						runtimeArtifact.m_Manifest.m_TargetProfile) &&
					serialized[49] == static_cast<std::byte>(
						runtimeArtifact.m_Manifest.m_BinaryFormat) &&
					serialized[50] == static_cast<std::byte>(
						runtimeArtifact.m_Manifest.m_SpirVTargetEnvironment) &&
					serialized[51] == static_cast<std::byte>(
						runtimeArtifact.m_Manifest.m_CoordinateOptions) &&
					serialized[56] == static_cast<std::byte>(
						static_cast<uint32_t>(runtimeArtifact.m_Manifest.m_Stage)) &&
					serialized[64] == static_cast<std::byte>(
						runtimeArtifact.m_Manifest.m_EntryPoint.front()),
				"Runtime manifest encoder writes the pinned ABI and entry point explicitly");

			SerializedShaderRuntimeArtifactManifest corruptMagic = serialized;
			corruptMagic[0] ^= std::byte{ 1 };
			context.Check(
				!DeserializeShaderRuntimeArtifactManifest(corruptMagic).has_value() &&
					!DeserializeShaderRuntimeArtifactManifest(
						std::span(serialized).first(serialized.size() - 1)).has_value(),
				"Runtime manifest codec rejects corrupt magic and truncated documents");

			SerializedShaderRuntimeArtifactManifest embeddedNullEntryPoint = serialized;
			embeddedNullEntryPoint[64] = std::byte{};
			context.Check(
				!DeserializeShaderRuntimeArtifactManifest(embeddedNullEntryPoint).has_value(),
				"Runtime manifest codec rejects invalid executable entry-point metadata");

			SerializedShaderRuntimeArtifactManifest unknownFileVersion = serialized;
			unknownFileVersion[8] = std::byte{ 3 };
			context.Check(
				!DeserializeShaderRuntimeArtifactManifest(unknownFileVersion).has_value(),
				"Runtime manifest codec rejects unknown file-format versions");

			const ShaderArtifactRef artifactRef{
				.m_ArtifactId = runtimeArtifact.m_Manifest.m_ArtifactId,
			};
			const std::string artifactId =
				Sha256DigestToHex(artifactRef.m_ArtifactId.m_DurableDigest);
			const ShaderLooseArtifactPaths paths =
				ShaderLooseArtifactLocator(root).GetPaths(artifactRef);
			context.Check(
				paths.m_BinaryPath.parent_path().filename() == artifactId.substr(2, 2) &&
					paths.m_BinaryPath.parent_path().parent_path().filename() ==
						artifactId.substr(0, 2) &&
					paths.m_BinaryPath.filename() == artifactId + ".ggsh.bin" &&
					paths.m_ManifestPath.filename() == artifactId + ".ggsh.manifest",
				"Loose locator derives both immutable paths only from the full ArtifactId");
		}

		void RunReaderAndPublicationTests(
			SelfTestContext& context,
			const std::filesystem::path& root,
			const ShaderArtifact& buildArtifact) noexcept
		{
			const ShaderRuntimeArtifact runtimeArtifact =
				BuildShaderRuntimeArtifact(buildArtifact);
			const ShaderArtifactRef artifactRef{
				.m_ArtifactId = runtimeArtifact.m_Manifest.m_ArtifactId,
			};
			const ShaderArtifactCompatibilityRequest compatibility =
				MakeCompatibilityRequest(runtimeArtifact.m_Manifest);
			const ShaderLooseArtifactLocator locator(root);
			const ShaderLooseArtifactPaths paths = locator.GetPaths(artifactRef);
			ShaderLooseArtifactReader reader(locator);
			ShaderArtifactStore store(reader);

			context.Check(
				reader.ReadArtifact(artifactRef).m_Status == ShaderArtifactReadStatus::NotFound,
				"Loose reader treats a missing manifest commit record as NotFound");

			const ShaderLooseArtifactLocator nonFileLocator(root / L"NonFileManifest");
			const ShaderLooseArtifactPaths nonFilePaths = nonFileLocator.GetPaths(artifactRef);
			std::error_code directoryError;
			std::filesystem::create_directories(nonFilePaths.m_ManifestPath, directoryError);
			ShaderLooseArtifactReader nonFileReader(nonFileLocator);
			context.Check(
				!directoryError && nonFileReader.ReadArtifact(artifactRef).m_Status ==
					ShaderArtifactReadStatus::IOFailure,
				"Loose reader reports a non-file manifest path as IOFailure");

			const SerializedShaderRuntimeArtifactManifest serializedManifest =
				SerializeShaderRuntimeArtifactManifest(runtimeArtifact.m_Manifest);
			const bool manifestParentReady =
				utils::CreateParentDirectoryIfNotExist(paths.m_ManifestPath);
			const bool manifestOnlyWritten =
				utils::WriteFileBinary(paths.m_ManifestPath, serializedManifest);
			context.Check(
				manifestParentReady && manifestOnlyWritten &&
					reader.ReadArtifact(artifactRef).m_Status ==
						ShaderArtifactReadStatus::MalformedArtifact,
				"Loose reader rejects a commit record whose binary is missing");
			std::filesystem::remove(paths.m_ManifestPath);

			const std::array<std::byte, 4> malformedManifest{
				std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 }, std::byte{ 4 }
			};
			const bool malformedManifestWritten =
				utils::WriteFileBinary(paths.m_ManifestPath, malformedManifest);
			context.Check(
				malformedManifestWritten &&
					reader.ReadArtifact(artifactRef).m_Status ==
						ShaderArtifactReadStatus::MalformedArtifact,
				"Loose reader rejects malformed manifest commit records");
			std::filesystem::remove(paths.m_ManifestPath);

			const bool orphanParentReady =
				utils::CreateParentDirectoryIfNotExist(paths.m_BinaryPath);
			const std::array<std::byte, 4> orphanBytes{
				std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 }, std::byte{ 4 }
			};
			const bool orphanWritten =
				utils::WriteFileBinary(paths.m_BinaryPath, orphanBytes);
			context.Check(
				orphanParentReady && orphanWritten &&
					reader.ReadArtifact(artifactRef).m_Status ==
						ShaderArtifactReadStatus::NotFound,
				"Loose reader never consumes an orphan binary without its commit record");

			const ShaderRuntimeArtifactPublicationResult recoveredPublication =
				PublishShaderRuntimeArtifact(root, buildArtifact);
			const ShaderArtifactLoadResult recoveredLoad =
				store.LoadArtifact(artifactRef, compatibility);
			context.Check(
				recoveredPublication.IsSuccess() && recoveredLoad.IsSuccess() &&
					BinaryEquals(recoveredLoad.m_Artifact.m_Binary, buildArtifact.m_Binary),
				"Publisher recovers an orphan and Store loads the exact committed bytes");

			const ShaderRuntimeArtifactPublicationResult secondPublication =
				PublishShaderRuntimeArtifact(root, buildArtifact);
			context.Check(
				secondPublication.m_Status ==
					ShaderRuntimeArtifactPublicationStatus::AlreadyPresent &&
					secondPublication.m_ArtifactRef == artifactRef,
				"Publishing the same immutable ArtifactId reuses the validated winner");

			ShaderBinary corruptedBinary = buildArtifact.m_Binary;
			static_cast<std::byte*>(corruptedBinary.Data())[19] ^= std::byte{ 1 };
			const bool corruptionApplied = utils::WriteFileBinary(
				paths.m_BinaryPath,
				std::span(
					static_cast<const std::byte*>(corruptedBinary.Data()),
					corruptedBinary.SizeInBytes()));
			context.Check(
				corruptionApplied &&
					store.LoadArtifact(artifactRef, compatibility).m_Status ==
						ShaderArtifactLoadStatus::BinaryDigestMismatch,
				"Store rejects a crossed manifest/binary pair instead of consuming it");

			const ShaderRuntimeArtifactPublicationResult repairedPublication =
				PublishShaderRuntimeArtifact(root, buildArtifact);
			context.Check(
				repairedPublication.IsSuccess() &&
					store.LoadArtifact(artifactRef, compatibility).IsSuccess(),
				"Publisher repairs persistently corrupt derived Runtime artifact files");
		}

		void RunContentAddressAndConcurrencyTests(
			SelfTestContext& context,
			const std::filesystem::path& root) noexcept
		{
			const ShaderArtifact first = MakeDxilArtifact(std::byte{ 1 });
			const ShaderArtifact second = MakeDxilArtifact(std::byte{ 2 });
			const ShaderRuntimeArtifactPublicationResult firstResult =
				PublishShaderRuntimeArtifact(root, first);
			const ShaderRuntimeArtifactPublicationResult secondResult =
				PublishShaderRuntimeArtifact(root, second);
			context.Check(
				firstResult.IsSuccess() && secondResult.IsSuccess() &&
					firstResult.m_ArtifactRef != secondResult.m_ArtifactRef &&
					firstResult.m_Paths.m_BinaryPath != secondResult.m_Paths.m_BinaryPath &&
					std::filesystem::is_regular_file(firstResult.m_Paths.m_ManifestPath) &&
					std::filesystem::is_regular_file(secondResult.m_Paths.m_ManifestPath),
				"Changed binary content publishes a new ArtifactId without mutating the old one");

			const std::filesystem::path concurrentRoot = root / L"Concurrent";
			constexpr size_t ProducerCount = 8;
			std::array<ShaderRuntimeArtifactPublicationResult, ProducerCount> results{};
			std::vector<std::thread> producers;
			producers.reserve(ProducerCount);
			for (size_t producerIndex = 0; producerIndex < ProducerCount; ++producerIndex)
			{
				producers.emplace_back([&, producerIndex]()
					{
						results[producerIndex] =
							PublishShaderRuntimeArtifact(concurrentRoot, first);
					});
			}
			for (std::thread& producer : producers)
			{
				producer.join();
			}
			const bool allSucceeded = std::ranges::all_of(results,
				[](const ShaderRuntimeArtifactPublicationResult& result)
				{
					return result.IsSuccess();
				});
			const ShaderRuntimeArtifact runtimeArtifact = BuildShaderRuntimeArtifact(first);
			ShaderLooseArtifactReader reader{ ShaderLooseArtifactLocator(concurrentRoot) };
			ShaderArtifactStore store(reader);
			const ShaderArtifactRef artifactRef{
				.m_ArtifactId = runtimeArtifact.m_Manifest.m_ArtifactId,
			};
			context.Check(
				allSucceeded && store.LoadArtifact(
					artifactRef,
					MakeCompatibilityRequest(runtimeArtifact.m_Manifest)).IsSuccess(),
				"Concurrent producers converge on one Store-valid immutable artifact");
		}
	}

	void RunShaderRuntimeArtifactPublicationSelfTests(SelfTestContext& context) noexcept
	{
		std::error_code errorCode;
		const std::filesystem::path tempRoot =
			std::filesystem::temp_directory_path(errorCode) /
			(L"GGLabRuntimeArtifactPublication-" +
				std::to_wstring(static_cast<uint32_t>(::_getpid())));
		context.Check(!errorCode, "Runtime artifact publication test resolves a temporary root");
		if (errorCode)
		{
			return;
		}
		std::filesystem::remove_all(tempRoot, errorCode);
		errorCode.clear();

		const ShaderArtifact dxilArtifact = MakeDxilArtifact();
		const ShaderRuntimeArtifact runtimeDxil = BuildShaderRuntimeArtifact(dxilArtifact);
		RunCodecAndLocatorTests(context, tempRoot / L"Codec", runtimeDxil);
		RunReaderAndPublicationTests(context, tempRoot / L"Recovery", dxilArtifact);
		RunContentAddressAndConcurrencyTests(context, tempRoot / L"Identity");

		const ShaderArtifact spirVArtifact = MakeSpirVArtifact();
		const ShaderRuntimeArtifact runtimeSpirV = BuildShaderRuntimeArtifact(spirVArtifact);
		const ShaderRuntimeArtifactPublicationResult spirVPublication =
			PublishShaderRuntimeArtifact(tempRoot / L"SpirV", spirVArtifact);
		ShaderLooseArtifactReader spirVReader{
			ShaderLooseArtifactLocator(tempRoot / L"SpirV")
		};
		ShaderArtifactStore spirVStore(spirVReader);
		context.Check(
			spirVPublication.IsSuccess() && spirVStore.LoadArtifact(
				spirVPublication.m_ArtifactRef,
				MakeCompatibilityRequest(runtimeSpirV.m_Manifest)).IsSuccess(),
			"Toolchain publication and Runtime Store agree on a SPIR-V artifact");

		std::filesystem::remove_all(tempRoot, errorCode);
	}
}
