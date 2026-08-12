#include "Application/SelfTest/AssetDataSelfTests.h"
#include "Core/Hash/Sha256.h"
#include "Graphics/Asset/DerivedData/DerivedDataKey.h"
#include "Graphics/Asset/DerivedData/LocalDerivedDataStore.h"
#include "Graphics/Asset/DerivedData/TextureArtifactCodec.h"
#include "Graphics/Asset/ModelImportArtifactCache.h"
#include "Graphics/Asset/Store/ModelStore.h"
#include "Graphics/Asset/TextureArtifactCache.h"
#include "Graphics/Asset/TextureAssetValidation.h"
#include "Graphics/RHI/DX12/Utility/DX12ResourceDescUtils.h"
#include "Graphics/RHI/DX12/Utility/DX12ViewDescUtils.h"
#include "Graphics/RHI/RHITextureValidation.h"
#include "Graphics/Utility/DXGIFormatUtils.h"

#include <cwctype>
#include <fstream>
#include <thread>

namespace gglab
{
	namespace
	{
		[[nodiscard]] bool MatchesHex(
			std::span<const std::byte> bytes, std::string_view expected) noexcept
		{
			constexpr std::string_view HexDigits = "0123456789abcdef";
			if (expected.size() != bytes.size() * 2)
			{
				return false;
			}

			for (size_t index = 0; index < bytes.size(); ++index)
			{
				const uint8_t value = std::to_integer<uint8_t>(bytes[index]);
				if (expected[index * 2] != HexDigits[value >> 4] ||
					expected[index * 2 + 1] != HexDigits[value & 0x0f])
				{
					return false;
				}
			}
			return true;
		}

		void WriteU64LittleEndian(
			std::span<std::byte> bytes, size_t offset, uint64_t value) noexcept
		{
			GGLAB_ASSERT(offset <= bytes.size() && bytes.size() - offset >= sizeof(value));
			for (size_t byteIndex = 0; byteIndex < sizeof(value); ++byteIndex)
			{
				bytes[offset + byteIndex] = static_cast<std::byte>(value & 0xffu);
				value >>= 8;
			}
		}

		[[nodiscard]] TextureAssetData MakeTextureFixture()
		{
			TextureAssetData texture{};
			texture.m_ResourceFormat = RHIFormat::R8G8B8A8Typeless;
			texture.m_ViewFormat = RHIFormat::R8G8B8A8UnormSrgb;
			texture.m_SrvDimension = RHITextureViewDimension::Texture2D;
			texture.m_Extent = { 2, 1, 1 };
			texture.m_ArraySize = 1;
			texture.m_MipLevels = 1;
			texture.m_ColorSpace = TextureColorSpace::SRGB;
			texture.m_Pixels = {
				std::byte{0x10},
				std::byte{0x11},
				std::byte{0x12},
				std::byte{0x13},
				std::byte{0x14},
				std::byte{0x15},
				std::byte{0x16},
				std::byte{0x17},
			};
			texture.m_Subresources.push_back({
				.m_DataOffset = 0,
				.m_DataSize = 8,
				.m_RowPitch = 8,
				.m_SlicePitch = 8,
				.m_Width = 2,
				.m_Height = 1,
				.m_Depth = 1,
				.m_MipLevel = 0,
				.m_ArraySlice = 0,
				});
			return texture;
		}

		[[nodiscard]] TextureAssetData MakeTwoMipTextureFixture()
		{
			TextureAssetData texture = MakeTextureFixture();
			texture.m_MipLevels = 2;
			texture.m_Pixels.insert(texture.m_Pixels.end(),
				{
					std::byte{0x20},
					std::byte{0x21},
					std::byte{0x22},
					std::byte{0x23},
				});
			texture.m_Subresources.push_back({
				.m_DataOffset = 8,
				.m_DataSize = 4,
				.m_RowPitch = 4,
				.m_SlicePitch = 4,
				.m_Width = 1,
				.m_Height = 1,
				.m_Depth = 1,
				.m_MipLevel = 1,
				.m_ArraySlice = 0,
				});
			return texture;
		}

		[[nodiscard]] TextureAssetData MakeNonCanonicalTextureFixture()
		{
			TextureAssetData texture = MakeTwoMipTextureFixture();
			std::rotate(
				texture.m_Pixels.begin(),
				texture.m_Pixels.begin() + 8,
				texture.m_Pixels.end());
			std::swap(texture.m_Subresources[0], texture.m_Subresources[1]);
			texture.m_Subresources[0].m_DataOffset = 0;
			texture.m_Subresources[1].m_DataOffset = 4;
			return texture;
		}

		[[nodiscard]] bool TextureDataMatchesFixture(const TextureAssetData& texture) noexcept
		{
			if (texture.m_ResourceFormat != RHIFormat::R8G8B8A8Typeless ||
				texture.m_ViewFormat != RHIFormat::R8G8B8A8UnormSrgb ||
				texture.m_SrvDimension != RHITextureViewDimension::Texture2D ||
				texture.m_Extent.m_Width != 2 || texture.m_Extent.m_Height != 1 ||
				texture.m_Extent.m_Depth != 1 || texture.m_ArraySize != 1 ||
				texture.m_MipLevels != 1 || texture.m_ColorSpace != TextureColorSpace::SRGB ||
				texture.m_Subresources.size() != 1 || texture.m_Pixels.size() != 8)
			{
				return false;
			}

			const TextureAssetSubresource& subresource = texture.m_Subresources.front();
			return subresource.m_DataOffset == 0 && subresource.m_DataSize == 8 &&
				subresource.m_RowPitch == 8 && subresource.m_SlicePitch == 8 &&
				subresource.m_Width == 2 && subresource.m_Height == 1 &&
				subresource.m_Depth == 1 && subresource.m_MipLevel == 0 &&
				subresource.m_ArraySlice == 0 &&
				std::ranges::equal(texture.m_Pixels, MakeTextureFixture().m_Pixels);
		}

		[[nodiscard]] ImportedModel MakeModelImportFixture()
		{
			ImportedModel model{};
			model.m_CanonicalPath = "Assets/Models/SelfTest.gltf";
			model.m_Name = "SelfTest";
			model.m_Type = ModelType::GlTF;
			model.m_TextureSources.push_back({
				.m_CanonicalPath = "Assets/Textures/SelfTest.png",
				.m_ImportSettings = MakeTextureImportSettings(TextureSemantic::BaseColor),
				.m_Semantic = TextureSemantic::BaseColor,
				});
			model.m_Meshes.push_back({
				.m_Name = "SelfTestMesh",
				.m_Vertices = {Vertex{}},
				.m_Indices = {0},
				});
			return model;
		}

		[[nodiscard]] std::vector<ResolvedModelImportTexture> MakeResolvedModelTextureFixture(
			std::byte sourceMarker = std::byte{ 0x42 }) noexcept
		{
			const ImportedModel model = MakeModelImportFixture();
			const ImportedTextureSource& source = model.m_TextureSources.front();
			TextureAssetData textureData = MakeTextureFixture();
			const AssetContentFingerprint contentFingerprint =
				ComputeTextureContentFingerprint(textureData, source.m_ImportSettings);
			TextureArtifactBuildResult built = CreateTextureArtifact(std::move(textureData));
			SourceDigest sourceDigest{};
			sourceDigest.m_Value.front() = sourceMarker;
			return {
				{
					.m_Artifact = built.Succeeded() ? std::make_shared<const TextureArtifact>(
														  std::move(built.m_Artifact))
													: TextureArtifactHandle{},
					.m_ContentFingerprint = contentFingerprint,
					.m_SourceDigest = sourceDigest,
					.m_DerivedDataKey = BuildTextureDerivedDataKey(
						sourceDigest, source.m_CanonicalPath, source.m_ImportSettings),
				},
			};
		}

		void RunSha256Tests(SelfTestContext& context) noexcept
		{
			const Sha256Hash emptyHash = ComputeSha256({});
			context.Check(MatchesHex(emptyHash.m_Value,
				"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"),
				"SHA-256 empty input matches the standard vector");

			constexpr std::string_view Input = "abc";
			const Sha256Hash abcHash = ComputeSha256(std::as_bytes(std::span{ Input }));
			context.Check(MatchesHex(abcHash.m_Value,
				"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
				"SHA-256 text input matches the standard vector");
		}

		void RunDerivedDataKeyTests(SelfTestContext& context) noexcept
		{
			constexpr std::string_view Source = "asset source";
			SourceDigest sourceDigest{};
			sourceDigest.m_Value = ComputeSha256(std::as_bytes(std::span{ Source })).m_Value;

			const TextureImportSettings settings{
				.m_Semantic = TextureSemantic::BaseColor,
				.m_MipPolicy = TextureMipPolicy::Preserve,
			};
			const DerivedDataKey key =
				BuildTextureDerivedDataKey(sourceDigest, "Textures/Fixture.PNG", settings);
			context.Check(MatchesHex(key.m_Value,
				"5889e6c301b099379dab9713ef36830489dfba6fee4c20ed6e07e7e4203c7e88"),
				"Texture derived-data key matches the stable golden vector");
		}

		void RunTextureCodecTests(SelfTestContext& context) noexcept
		{
			TextureArtifactBuildResult built = CreateTextureArtifact(MakeTextureFixture());
			const bool buildSucceeded = built.Succeeded();
			TextureArtifact artifact = std::move(built.m_Artifact);
			context.Check(
				buildSucceeded &&
				MatchesHex(artifact.m_ContentDigest.m_Value,
					"9f01361721504e531dce2e8437dd2698515b96e47718b64cd372390e242720f1"),
				"Texture artifact factory validates data and matches the stable digest vector");

			TextureAssetData invalidFixture = MakeTextureFixture();
			invalidFixture.m_Subresources.front().m_DataOffset = 1;
			const TextureArtifactBuildResult invalid =
				CreateTextureArtifact(std::move(invalidFixture));
			context.Check(
				!invalid.Succeeded() &&
				invalid.m_Error == TextureArtifactBuildError::InvalidStructure &&
				invalid.m_StructureError == TextureStructureValidationError::OutOfBounds,
				"Texture artifact factory rejects structurally invalid input before hashing");

			const TextureArtifactBuildResult nonCanonical =
				CreateTextureArtifact(MakeNonCanonicalTextureFixture());
			context.Check(!nonCanonical.Succeeded() &&
				nonCanonical.m_Error == TextureArtifactBuildError::InvalidStructure &&
				nonCanonical.m_StructureError ==
				TextureStructureValidationError::NonCanonicalSubresourceOrder,
				"Texture artifact factory rejects non-canonical subresource order");

			std::vector<std::byte> payload = TextureArtifactCodec::Serialize(artifact);
			const Sha256Hash payloadHash = ComputeSha256(payload);
			context.Check(
				payload.size() == 116 &&
				MatchesHex(payloadHash.m_Value,
					"d2a89c689bc4db351973d96ff9ff6745ac129ca3b2f8f56f210c316cb3bf6c8f"),
				"Texture artifact codec matches the stable payload layout");

			const TextureArtifactDecodeResult decoded =
				TextureArtifactCodec::Deserialize(payload, artifact.m_ContentDigest);
			context.Check(decoded.Succeeded() &&
				decoded.m_Artifact.m_ContentDigest == artifact.m_ContentDigest &&
				TextureDataMatchesFixture(decoded.m_Artifact.m_Data),
				"Texture artifact codec round-trips the fixture");

			std::vector<std::byte> oversizedDeclaration = payload;
			constexpr size_t SubresourceCountOffset = 10 * sizeof(uint32_t);
			WriteU64LittleEndian(
				oversizedDeclaration, SubresourceCountOffset, std::numeric_limits<uint64_t>::max());
			const TextureArtifactDecodeResult oversized =
				TextureArtifactCodec::Deserialize(oversizedDeclaration, artifact.m_ContentDigest);
			context.Check(!oversized.Succeeded() &&
				oversized.m_StructureError ==
				TextureStructureValidationError::ExceedsConfiguredLimit,
				"Texture artifact codec rejects oversized declarations before allocation");

			payload.back() ^= std::byte{ 0xff };
			const TextureArtifactDecodeResult corrupted =
				TextureArtifactCodec::Deserialize(payload, artifact.m_ContentDigest);
			context.Check(!corrupted.Succeeded() && !corrupted.m_Error.empty(),
				"Texture artifact codec rejects corrupted pixel data");

			payload.pop_back();
			const TextureArtifactDecodeResult truncated =
				TextureArtifactCodec::Deserialize(payload, artifact.m_ContentDigest);
			context.Check(!truncated.Succeeded() && !truncated.m_Error.empty(),
				"Texture artifact codec rejects truncated payloads");
		}

		void RunTextureStructureValidationTests(SelfTestContext& context) noexcept
		{
			const TextureAssetData valid = MakeTextureFixture();
			context.Check(ValidateTextureAssetStructure(valid).IsValid(),
				"Texture structure validation accepts the canonical fixture");
			context.Check(ValidateTextureAssetStructure(MakeTwoMipTextureFixture()).IsValid(),
				"Texture structure validation accepts canonical array-major mip order");
			context.Check(ValidateTextureAssetStructure(MakeNonCanonicalTextureFixture()).m_Error ==
				TextureStructureValidationError::NonCanonicalSubresourceOrder,
				"Texture structure validation rejects reordered mip data");

			TextureAssetData reversedPhysicalOrder = MakeTwoMipTextureFixture();
			reversedPhysicalOrder.m_Subresources[0].m_DataOffset = 4;
			reversedPhysicalOrder.m_Subresources[1].m_DataOffset = 0;
			context.Check(ValidateTextureAssetStructure(reversedPhysicalOrder).m_Error ==
				TextureStructureValidationError::NonCanonicalSubresourceOrder,
				"Texture structure validation rejects non-canonical physical mip order");

			TextureAssetData outOfBounds = valid;
			outOfBounds.m_Subresources.front().m_DataOffset = 1;
			context.Check(ValidateTextureAssetStructure(outOfBounds).m_Error ==
				TextureStructureValidationError::OutOfBounds,
				"Texture structure validation rejects out-of-bounds pixel data");

			TextureAssetData shortRow = valid;
			shortRow.m_Subresources.front().m_RowPitch = 7;
			context.Check(ValidateTextureAssetStructure(shortRow).m_Error ==
				TextureStructureValidationError::InvalidRowPitch,
				"Texture structure validation rejects a short row pitch");

			TextureAssetData wrongExtent = valid;
			wrongExtent.m_Subresources.front().m_Width = 1;
			context.Check(ValidateTextureAssetStructure(wrongExtent).m_Error ==
				TextureStructureValidationError::InvalidSubresourceExtent,
				"Texture structure validation rejects inconsistent mip extents");

			TextureAssetData duplicate = valid;
			duplicate.m_SrvDimension = RHITextureViewDimension::Texture2DArray;
			duplicate.m_ArraySize = 2;
			duplicate.m_Pixels.resize(16);
			TextureAssetSubresource duplicateSubresource = duplicate.m_Subresources.front();
			duplicateSubresource.m_DataOffset = 8;
			duplicate.m_Subresources.push_back(duplicateSubresource);
			context.Check(ValidateTextureAssetStructure(duplicate).m_Error ==
				TextureStructureValidationError::DuplicateSubresource,
				"Texture structure validation rejects duplicate subresource coordinates");

			TextureAssetValidationLimits limits{};
			limits.m_MaxPixelBytes = 7;
			context.Check(ValidateTextureAssetStructure(valid, limits).m_Error ==
				TextureStructureValidationError::ExceedsConfiguredLimit,
				"Texture structure validation applies configurable allocation limits");
		}

		void RunLocalDerivedDataStoreTests(SelfTestContext& context) noexcept
		{
			std::error_code errorCode;
			const std::filesystem::path root =
				std::filesystem::temp_directory_path(errorCode) /
				std::format("gglab.asset-data-self-test.{}.{}", GetCurrentProcessId(),
					reinterpret_cast<uintptr_t>(&context));
			if (errorCode)
			{
				context.Check(false, "Local DDC self-test resolves a temporary directory");
				return;
			}
			std::filesystem::remove_all(root, errorCode);

			constexpr std::string_view ArtifactType = "gglab.self-test";
			constexpr uint32_t SchemaVersion = 1;
			constexpr std::array<std::byte, 4> Payload{
				std::byte{0x10},
				std::byte{0x20},
				std::byte{0x30},
				std::byte{0x40},
			};
			DerivedDataKey key{};
			key.m_Value = ComputeSha256(std::as_bytes(std::span{ ArtifactType })).m_Value;
			ArtifactContentDigest artifactDigest{};
			artifactDigest.m_Value = ComputeSha256(Payload).m_Value;

			{
				const std::filesystem::path probeRoot = root / "probe";
				LocalDerivedDataStore observer(probeRoot);
				LocalDerivedDataStore externalWriter(probeRoot);
				const std::string keyText = DerivedDataKeyText(key, key.m_Value.size());
				const std::filesystem::path entryPath =
					probeRoot / keyText.substr(0, 2) / (keyText + ".ddc");
				context.Check(observer.Probe(key) == DerivedDataPresence::Missing,
					"Local DDC probe reports a missing filesystem entry");
				const bool externallyWritten =
					externalWriter.Write(key, ArtifactType, SchemaVersion, artifactDigest, Payload);
				context.Check(externallyWritten &&
					observer.Probe(key) == DerivedDataPresence::Present &&
					observer.GetStatistics().m_StoredEntryCount == 0,
					"Local DDC probe observes external creation without relying on the catalog");

				const bool reconciledCreation = observer.ReconcileCatalog();
				const LocalDerivedDataStoreStatistics createdStatistics = observer.GetStatistics();
				context.Check(
					reconciledCreation && createdStatistics.m_StoredEntryCount == 1 &&
					createdStatistics.m_StoredBytes != 0 &&
					createdStatistics.m_CatalogLastReconciledAtUnixMilliseconds != 0 &&
					createdStatistics.m_CatalogReconciliationCount >= 2 &&
					createdStatistics.m_IsCatalogApproximate,
					"Local DDC catalog reconciliation refreshes approximate diagnostics");

				errorCode.clear();
				const bool externallyRemoved = std::filesystem::remove(entryPath, errorCode);
				context.Check(externallyRemoved && !errorCode &&
					observer.Probe(key) == DerivedDataPresence::Missing &&
					observer.GetStatistics().m_StoredEntryCount == 1,
					"Local DDC probe observes external deletion before catalog reconciliation");
				const bool reconciledDeletion = observer.ReconcileCatalog();
				context.Check(
					reconciledDeletion && observer.GetStatistics().m_StoredEntryCount == 0,
					"Local DDC reconciliation removes externally deleted entries from diagnostics");

				GGLAB_UNUSED(externalWriter.Write(
					key, ArtifactType, SchemaVersion, artifactDigest, Payload));
				{
					std::ofstream corruptStream(entryPath, std::ios::binary | std::ios::trunc);
					constexpr std::array CorruptBytes{ 'b', 'a', 'd' };
					corruptStream.write(CorruptBytes.data(), CorruptBytes.size());
				}
				const uint64_t corruptionCount = observer.GetStatistics().m_CorruptionCount;
				context.Check(observer.Probe(key) == DerivedDataPresence::Present &&
					std::filesystem::exists(entryPath) &&
					observer.GetStatistics().m_CorruptionCount == corruptionCount,
					"Local DDC probe does not validate or delete a corrupt entry");
				const DerivedDataReadResult corrupt =
					observer.Read(key, ArtifactType, SchemaVersion);
				context.Check(corrupt.m_Disposition == DerivedDataReadDisposition::Corrupt &&
					!std::filesystem::exists(entryPath) &&
					observer.GetStatistics().m_CorruptionCount == corruptionCount + 1,
					"Local DDC read retains responsibility for corrupt entry disposal");
			}

			{
				LocalDerivedDataStore store(root / "read");
				const bool wrote =
					store.Write(key, ArtifactType, SchemaVersion, artifactDigest, Payload);
				const DerivedDataReadResult hit = store.Read(key, ArtifactType, SchemaVersion,
					{
						.m_MaxContainerBytes =
							ComputeLocalDerivedDataContainerByteLimit(ArtifactType, Payload.size()),
					});
				context.Check(wrote && hit.m_Disposition == DerivedDataReadDisposition::Hit &&
					hit.m_ArtifactContentDigest == artifactDigest &&
					std::ranges::equal(hit.m_Payload, Payload),
					"Local DDC bounded read accepts a container within its payload limit");

				const DerivedDataReadResult oversized =
					store.Read(key, ArtifactType, SchemaVersion, { .m_MaxContainerBytes = 1 });
				context.Check(oversized.m_Disposition == DerivedDataReadDisposition::Corrupt &&
					oversized.m_Payload.empty() && !store.Contains(key) &&
					store.GetStatistics().m_CorruptionCount == 1,
					"Local DDC rejects and discards an oversized container before payload allocation");
			}

			errorCode.clear();
			std::filesystem::remove_all(root, errorCode);
		}

		void RunLocalDerivedDataMaintenanceTests(SelfTestContext& context) noexcept
		{
			std::error_code errorCode;
			const std::filesystem::path root =
				std::filesystem::temp_directory_path(errorCode) /
				std::format("gglab.ddc-maintenance-self-test.{}.{}", GetCurrentProcessId(),
					reinterpret_cast<uintptr_t>(&context));
			if (errorCode)
			{
				context.Check(
					false, "Local DDC maintenance self-test resolves a temporary directory");
				return;
			}
			std::filesystem::remove_all(root, errorCode);

			constexpr std::string_view ArtifactType = "gglab.maintenance-self-test";
			constexpr uint32_t SchemaVersion = 1;
			constexpr std::array<std::byte, 4> Payload{
				std::byte{0x51},
				std::byte{0x52},
				std::byte{0x53},
				std::byte{0x54},
			};
			DerivedDataKey key{};
			key.m_Value = ComputeSha256(std::as_bytes(std::span{ ArtifactType })).m_Value;
			ArtifactContentDigest artifactDigest{};
			artifactDigest.m_Value = ComputeSha256(Payload).m_Value;
			const auto entryPath = [&key](const std::filesystem::path& storeRoot)
				{
					const std::string keyText = DerivedDataKeyText(key, key.m_Value.size());
					return storeRoot / keyText.substr(0, 2) / (keyText + ".ddc");
				};

			{
				const std::filesystem::path identityRoot = root / "Identity";
				std::wstring alternateText = identityRoot.wstring();
				std::ranges::transform(alternateText, alternateText.begin(),
					[](wchar_t value) noexcept
					{ return static_cast<wchar_t>(std::towupper(value)); });
				const LocalDerivedDataRootIdentity canonical =
					ResolveLocalDerivedDataRootIdentity(identityRoot / ".");
				const LocalDerivedDataRootIdentity alternate =
					ResolveLocalDerivedDataRootIdentity(std::filesystem::path(alternateText));
				context.Check(canonical.IsValid() && alternate.IsValid() &&
					canonical.m_CanonicalUtf8 == alternate.m_CanonicalUtf8 &&
					canonical.m_MutexName == alternate.m_MutexName &&
					canonical.m_MutexName.starts_with(L"Local\\gglab.ddc."),
					"Local DDC root identity normalizes absolute path spelling and case");
			}

			{
				const std::filesystem::path concurrentRoot = root / "concurrent-writers";
				LocalDerivedDataStore first(concurrentRoot);
				LocalDerivedDataStore second(concurrentRoot);
				std::array<bool, 2> writes{};
				std::jthread firstWriter(
					[&]() noexcept
					{
						writes[0] =
							first.Write(key, ArtifactType, SchemaVersion, artifactDigest, Payload);
					});
				std::jthread secondWriter(
					[&]() noexcept
					{
						writes[1] =
							second.Write(key, ArtifactType, SchemaVersion, artifactDigest, Payload);
					});
				firstWriter.join();
				secondWriter.join();
				const DerivedDataReadResult result = first.Read(key, ArtifactType, SchemaVersion);
				context.Check(writes[0] && writes[1] &&
					result.m_Disposition == DerivedDataReadDisposition::Hit &&
					std::ranges::equal(result.m_Payload, Payload),
					"Local DDC maintenance lock serializes immutable publication to one key");
			}

			{
				const std::filesystem::path raceRoot = root / "clear-writer";
				LocalDerivedDataStore writer(raceRoot);
				LocalDerivedDataStore clearer(raceRoot);
				std::atomic_bool writesSucceeded = true;
				std::atomic_bool clearsSucceeded = true;
				std::jthread writeThread(
					[&]() noexcept
					{
						for (uint32_t iteration = 0; iteration < 32; ++iteration)
						{
							if (!writer.Write(
								key, ArtifactType, SchemaVersion, artifactDigest, Payload))
							{
								writesSucceeded.store(false, std::memory_order_relaxed);
							}
						}
					});
				std::jthread clearThread(
					[&]() noexcept
					{
						for (uint32_t iteration = 0; iteration < 8; ++iteration)
						{
							if (!clearer.Clear())
							{
								clearsSucceeded.store(false, std::memory_order_relaxed);
							}
						}
					});
				writeThread.join();
				clearThread.join();
				const bool finalWrite =
					writer.Write(key, ArtifactType, SchemaVersion, artifactDigest, Payload);
				context.Check(writesSucceeded.load(std::memory_order_relaxed) &&
					clearsSucceeded.load(std::memory_order_relaxed) && finalWrite &&
					clearer.Read(key, ArtifactType, SchemaVersion).m_Disposition ==
					DerivedDataReadDisposition::Hit,
					"Local DDC Clear and writers coordinate without losing the replacement root");
			}

			{
				const std::filesystem::path raceRoot = root / "observed-corrupt";
				LocalDerivedDataStore staleReader(raceRoot);
				LocalDerivedDataStore replacementWriter(raceRoot);
				const bool wroteObserved =
					staleReader.Write(key, ArtifactType, SchemaVersion, artifactDigest, Payload);
				const DerivedDataReadResult observed =
					staleReader.Read(key, ArtifactType, SchemaVersion);

				constexpr std::array<std::byte, 5> ReplacementPayload{
					std::byte{0x61},
					std::byte{0x62},
					std::byte{0x63},
					std::byte{0x64},
					std::byte{0x65},
				};
				ArtifactContentDigest replacementDigest{};
				replacementDigest.m_Value = ComputeSha256(ReplacementPayload).m_Value;
				const bool replaced =
					replacementWriter.Clear() && replacementWriter.Write(key, ArtifactType,
						SchemaVersion, replacementDigest, Payload);
				staleReader.DiscardObservedCorrupt(key, ArtifactType, SchemaVersion,
					observed.m_ArtifactContentDigest, observed.m_PayloadDigest);
				const DerivedDataReadResult preserved =
					staleReader.Read(key, ArtifactType, SchemaVersion);
				context.Check(
					wroteObserved && observed.m_Disposition == DerivedDataReadDisposition::Hit &&
					observed.m_PayloadDigest.IsValid() && replaced &&
					preserved.m_Disposition == DerivedDataReadDisposition::Hit &&
					preserved.m_ArtifactContentDigest != observed.m_ArtifactContentDigest &&
					preserved.m_ArtifactContentDigest == replacementDigest &&
					preserved.m_PayloadDigest.m_Value == observed.m_PayloadDigest.m_Value &&
					std::ranges::equal(preserved.m_Payload, Payload) &&
					staleReader.GetStatistics().m_CorruptionCount == 0,
					"Local DDC stale corrupt disposal preserves a replacement entry");

				const uint64_t corruptionCount = staleReader.GetStatistics().m_CorruptionCount;
				staleReader.DiscardObservedCorrupt(key, ArtifactType, SchemaVersion,
					preserved.m_ArtifactContentDigest, preserved.m_PayloadDigest);
				context.Check(
					staleReader.Probe(key) == DerivedDataPresence::Missing &&
					staleReader.GetStatistics().m_CorruptionCount == corruptionCount + 1,
					"Local DDC observed corrupt disposal removes the unchanged entry");
			}

			{
				const std::filesystem::path readRoot = root / "lock-free-read";
				LocalDerivedDataStore store(readRoot);
				GGLAB_UNUSED(
					store.Write(key, ArtifactType, SchemaVersion, artifactDigest, Payload));
				std::atomic_bool stopReader = false;
				std::atomic_uint32_t corruptReads = 0;
				std::jthread reader(
					[&]() noexcept
					{
						while (!stopReader.load(std::memory_order_relaxed))
						{
							if (store.Read(key, ArtifactType, SchemaVersion).m_Disposition ==
								DerivedDataReadDisposition::Corrupt)
							{
								corruptReads.fetch_add(1, std::memory_order_relaxed);
							}
						}
					});
				for (uint32_t iteration = 0; iteration < 8; ++iteration)
				{
					if (store.Clear())
					{
						GGLAB_UNUSED(
							store.Write(key, ArtifactType, SchemaVersion, artifactDigest, Payload));
					}
				}
				stopReader.store(true, std::memory_order_relaxed);
				reader.join();
				const bool finalMaintenance =
					store.Clear() &&
					store.Write(key, ArtifactType, SchemaVersion, artifactDigest, Payload);
				context.Check(finalMaintenance &&
					corruptReads.load(std::memory_order_relaxed) == 0 &&
					store.Read(key, ArtifactType, SchemaVersion).m_Disposition ==
					DerivedDataReadDisposition::Hit,
					"Local DDC lock-free Read treats concurrent Clear as a transient miss");
			}

			{
				const std::filesystem::path abandonedRoot = root / "abandoned";
				LocalDerivedDataStore store(abandonedRoot);
				const LocalDerivedDataRootIdentity identity =
					ResolveLocalDerivedDataRootIdentity(abandonedRoot);
				const std::filesystem::path orphanTemporary =
					abandonedRoot / "orphan.ddc.tmp.crashed";
				GGLAB_UNUSED(std::filesystem::create_directories(abandonedRoot, errorCode));
				{
					std::ofstream orphanStream(orphanTemporary, std::ios::binary | std::ios::trunc);
					orphanStream.put('x');
				}
				HANDLE rawMutex = ::CreateMutexW(nullptr, FALSE, identity.m_MutexName.c_str());
				std::atomic_bool acquired = false;
				std::jthread abandoningThread(
					[&]() noexcept
					{
						if (::WaitForSingleObject(rawMutex, INFINITE) == WAIT_OBJECT_0)
						{
							acquired.store(true, std::memory_order_release);
						}
						// Exiting the owning thread without ReleaseMutex intentionally abandons it.
					});
				abandoningThread.join();
				const bool recovered =
					store.Write(key, ArtifactType, SchemaVersion, artifactDigest, Payload);
				if (rawMutex)
					::CloseHandle(rawMutex);
				context.Check(rawMutex && acquired.load(std::memory_order_acquire) && recovered &&
					!std::filesystem::exists(orphanTemporary),
					"Local DDC recovers an abandoned mutex and removes orphan temporary files");
			}

			{
				const std::filesystem::path blockedRoot = root / "rename-failure";
				LocalDerivedDataStore store(blockedRoot);
				const bool wrote =
					store.Write(key, ArtifactType, SchemaVersion, artifactDigest, Payload);
				const std::filesystem::path path = entryPath(blockedRoot);
				HANDLE reader =
					::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
						nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
				const bool clearRejected = reader != INVALID_HANDLE_VALUE && !store.Clear();
				if (reader != INVALID_HANDLE_VALUE)
					::CloseHandle(reader);
				const DerivedDataReadResult preserved =
					store.Read(key, ArtifactType, SchemaVersion);
				context.Check(wrote && clearRejected &&
					preserved.m_Disposition == DerivedDataReadDisposition::Hit,
					"Local DDC Clear preserves the active root when rename fails");
			}

			{
				const std::filesystem::path retryRoot = root / "trash-retry";
				const std::filesystem::path staleTrash =
					retryRoot.parent_path() / (retryRoot.filename().wstring() + L".trash.previous");
				GGLAB_UNUSED(std::filesystem::create_directories(staleTrash, errorCode));
				{
					std::ofstream staleFile(staleTrash / "entry.ddc", std::ios::binary);
					staleFile.put('x');
				}
				LocalDerivedDataStore store(retryRoot);
				for (uint32_t attempt = 0; attempt < 200 && std::filesystem::exists(staleTrash);
					++attempt)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
				}
				context.Check(!std::filesystem::exists(staleTrash),
					"Local DDC retries best-effort cleanup of trash left by an earlier process");
			}

			errorCode.clear();
			std::filesystem::remove_all(root, errorCode);
		}

		void RunModelImportArtifactTests(SelfTestContext& context) noexcept
		{
			{
				ModelStore store;
				const std::filesystem::path path = "Assets/Models/RuntimeRetirement.gltf";
				const ModelID retiredId = store.Create(path);
				const bool removed = store.Remove(retiredId);
				const ModelID replacementId = store.Create(path);
				context.Check(retiredId.IsValid() && removed && !store.Find(retiredId) &&
					store.FindByPath(path) == replacementId &&
					replacementId.IsValid() && replacementId != retiredId,
					"Model store retirement clears path identity without reusing IDs");
			}

			{
				TextureArtifactCache concurrentCache({ .m_BudgetBytes = 1024 * 1024 });
				std::array<TextureArtifactHandle, 4> handles;
				std::array<std::jthread, 4> workers;
				for (size_t index = 0; index < workers.size(); ++index)
				{
					workers[index] = std::jthread([&concurrentCache, &handles, index]() noexcept
						{ handles[index] = concurrentCache.CreateAndAdmit(MakeTextureFixture()); });
				}
				for (std::jthread& worker : workers)
				{
					worker.join();
				}
				context.Check(std::ranges::all_of(handles,
					[&handles](const TextureArtifactHandle& handle) noexcept
					{ return handle && handle == handles.front(); }) &&
					concurrentCache.GetStatistics().m_AdmissionCount == 1,
					"Texture artifact cache canonicalizes concurrent worker admissions");
			}

			TextureArtifactCache textureCache({ .m_BudgetBytes = 1024 * 1024 });
			ImportedModel source = MakeModelImportFixture();
			std::vector<ResolvedModelImportTexture> resolvedTextures =
				MakeResolvedModelTextureFixture();
			const std::byte* const sourcePixels =
				resolvedTextures.front().m_Artifact->m_Data.m_Pixels.data();
			ModelImportArtifactHandle artifact = CreateModelImportArtifact(
				std::move(source), std::move(resolvedTextures), textureCache);
			context.Check(artifact && artifact->IsValid() && artifact->m_Textures.size() == 1 &&
				artifact->m_Textures.front().m_Artifact->m_Data.m_Pixels.data() ==
				sourcePixels &&
				artifact->m_Textures.front().m_SourceDigest.IsValid() &&
				artifact->m_Textures.front().m_DerivedDataKey.IsValid(),
				"Model import artifacts retain resolved texture payloads and source identity");

			ModelImportArtifactHandle duplicate = CreateModelImportArtifact(
				MakeModelImportFixture(), MakeResolvedModelTextureFixture(), textureCache);
			const bool sharesCanonicalTexture =
				artifact && duplicate &&
				artifact->m_Textures.front().m_Artifact == duplicate->m_Textures.front().m_Artifact;
			context.Check(
				sharesCanonicalTexture && artifact->m_ContentDigest == duplicate->m_ContentDigest,
				"Model import artifacts reference the canonical texture allocation and digest");

			ModelImportArtifactHandle changedSource =
				CreateModelImportArtifact(MakeModelImportFixture(),
					MakeResolvedModelTextureFixture(std::byte{ 0x43 }), textureCache);
			context.Check(changedSource && artifact &&
				changedSource->m_Textures.front().m_Artifact ==
				artifact->m_Textures.front().m_Artifact &&
				changedSource->m_ContentDigest != artifact->m_ContentDigest,
				"Model artifact identity includes the resolved texture derived-data key");
			changedSource.reset();

			const uint64_t textureBytes =
				artifact ? artifact->m_Textures.front().m_Artifact->GetAllocatedBytes() : 0;
			const ArtifactCacheCoreStatistics textureStatistics = textureCache.GetStatistics();
			context.Check(textureBytes != 0 && textureStatistics.m_AdmissionCount == 1 &&
				textureStatistics.m_CachedBytes == textureBytes &&
				textureStatistics.m_TotalLiveBytes == textureBytes,
				"Texture cache accounts a model texture allocation exactly once");

			ModelImportArtifactCache modelCache({ .m_BudgetBytes = 1024 * 1024 });
			artifact = modelCache.Admit(std::move(artifact));
			const uint64_t modelBytes = artifact ? artifact->GetAllocatedBytes() : 0;
			context.Check(modelBytes != 0 && modelCache.GetStatistics().m_CachedBytes == modelBytes,
				"Model artifact cache excludes referenced texture allocation bytes");

			ModelMeshUploadSource meshSource{
				.m_Owner = artifact,
				.m_MeshIndex = 0,
			};
			const Vertex* const sourceVertices = artifact->m_Meshes.front().m_Vertices.data();
			const uint32_t* const sourceIndices = artifact->m_Meshes.front().m_Indices.data();
			context.Check(meshSource.IsValid() &&
				meshSource.GetVertices().data() == sourceVertices &&
				meshSource.GetIndices().data() == sourceIndices,
				"Model mesh upload sources resolve immutable payloads without copying");
			context.Check(
				!ModelMeshUploadSource{
					.m_Owner = artifact,
					.m_MeshIndex = 1,
				}
				.IsValid(),
				"Model mesh upload sources reject out-of-range mesh indices");

			textureCache.Clear();
			const ArtifactCacheCoreStatistics retained = textureCache.GetStatistics();
			context.Check(retained.m_CachedBytes == 0 &&
				retained.m_ExternallyRetainedBytes == textureBytes &&
				retained.m_TotalLiveBytes == textureBytes,
				"Model artifacts keep evicted texture cache allocations externally retained");

			TextureArtifactHandle readmittedTexture =
				artifact ? textureCache.Admit(artifact->m_Textures.front().m_Artifact) : nullptr;
			const ArtifactCacheCoreStatistics readmittedTextureStatistics =
				textureCache.GetStatistics();
			context.Check(readmittedTexture &&
				readmittedTexture == artifact->m_Textures.front().m_Artifact &&
				readmittedTextureStatistics.m_AdmissionCount == 2 &&
				readmittedTextureStatistics.m_CachedBytes == textureBytes &&
				readmittedTextureStatistics.m_TotalLiveBytes == textureBytes &&
				readmittedTextureStatistics.m_ExternallyRetainedBytes == 0,
				"Texture cache re-admission reuses a model-retained allocation record");
			textureCache.Clear();
			readmittedTexture.reset();

			modelCache.Clear();
			artifact.reset();
			duplicate.reset();
			const ArtifactCacheCoreStatistics retainedModel = modelCache.GetStatistics();
			context.Check(meshSource.IsValid() && retainedModel.m_CachedBytes == 0 &&
				retainedModel.m_ExternallyRetainedBytes == modelBytes &&
				retainedModel.m_TotalLiveBytes == modelBytes,
				"Queued model mesh sources retain artifacts after model cache eviction");
			meshSource.Reset();
			context.Check(modelCache.GetStatistics().m_TotalLiveBytes == 0,
				"Model mesh upload sources release artifact ownership after staging");
			context.Check(textureCache.GetStatistics().m_TotalLiveBytes == 0,
				"Texture allocation accounting reaches zero after model handles are released");

			context.Check(!CreateModelImportArtifact(MakeModelImportFixture(), {}, textureCache),
				"Model artifact construction rejects an unresolved texture source");
		}

		void RunRHITextureValidationTests(SelfTestContext& context) noexcept
		{
			const RHIFormatInfo& r8Info = GetRHIFormatInfo(RHIFormat::R8Unorm);
			const RHIFormatInfo& r16Info = GetRHIFormatInfo(RHIFormat::R16Float);
			context.Check(r8Info.m_Family == RHIFormatFamily::R8 &&
				r8Info.m_BytesPerBlock == 1 && r8Info.m_BlockWidth == 1 &&
				r8Info.m_BlockHeight == 1 && r16Info.m_Family == RHIFormatFamily::R16 &&
				r16Info.m_BytesPerBlock == 2 && r16Info.m_BlockWidth == 1 &&
				r16Info.m_BlockHeight == 1 && ToDXGIFormat(RHIFormat::R8Unorm) == DXGI_FORMAT_R8_UNORM &&
				ToDXGIFormat(RHIFormat::R16Float) == DXGI_FORMAT_R16_FLOAT &&
				ToRHIFormat(DXGI_FORMAT_R8_UNORM) == RHIFormat::R8Unorm &&
				ToRHIFormat(DXGI_FORMAT_R16_FLOAT) == RHIFormat::R16Float,
				"Single-channel R8 and R16 formats preserve metadata and DXGI mappings");

			RHITextureDesc aoDesc{};
			aoDesc.m_Format = RHIFormat::R8Unorm;
			aoDesc.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::RenderTarget |
				RHITextureUsage::UnorderedAccess | RHITextureUsage::CopyDest;
			aoDesc.m_Extent = { 4, 2, 1 };
			RHITextureViewDesc aoSrv{
				.m_Type = RHITextureViewType::ShaderResource,
				.m_Dimension = RHITextureViewDimension::Texture2D,
				.m_Format = RHIFormat::R8Unorm,
			};
			RHITextureViewDesc aoUav = aoSrv;
			aoUav.m_Type = RHITextureViewType::UnorderedAccess;
			RHITextureViewDesc aoRtv = aoSrv;
			aoRtv.m_Type = RHITextureViewType::RenderTarget;
			const D3D12_RESOURCE_DESC nativeAoDesc = ToD3D12ResourceDesc(aoDesc);
			const D3D12_SHADER_RESOURCE_VIEW_DESC nativeAoSrv =
				BuildD3D12ShaderResourceViewDesc(aoSrv, nativeAoDesc);
			const D3D12_UNORDERED_ACCESS_VIEW_DESC nativeAoUav =
				BuildD3D12UnorderedAccessViewDesc(aoUav, nativeAoDesc);
			const D3D12_RENDER_TARGET_VIEW_DESC nativeAoRtv =
				BuildD3D12RenderTargetViewDesc(aoRtv, nativeAoDesc);
			context.Check(ValidateRHITextureDesc(aoDesc).IsValid() &&
				ValidateRHITextureViewDesc(aoDesc, aoSrv).IsValid() &&
				ValidateRHITextureViewDesc(aoDesc, aoUav).IsValid() &&
				ValidateRHITextureViewDesc(aoDesc, aoRtv).IsValid() &&
				nativeAoDesc.Format == DXGI_FORMAT_R8_UNORM &&
				nativeAoSrv.Format == DXGI_FORMAT_R8_UNORM &&
				nativeAoUav.Format == DXGI_FORMAT_R8_UNORM &&
				nativeAoRtv.Format == DXGI_FORMAT_R8_UNORM,
				"R8Unorm validates and translates consistently for SRV, UAV, and RTV usage");

			std::array<std::byte, 8> r8Pixels{};
			RHITextureUploadData r8Upload{
				.m_Subresources = {
					{.m_Data = r8Pixels.data(), .m_RowPitch = 4, .m_SlicePitch = 8},
				},
			};
			RHITextureDesc r16Desc = aoDesc;
			r16Desc.m_Format = RHIFormat::R16Float;
			std::array<std::byte, 16> r16Pixels{};
			RHITextureUploadData r16Upload{
				.m_Subresources = {
					{.m_Data = r16Pixels.data(), .m_RowPitch = 8, .m_SlicePitch = 16},
				},
			};
			RHITextureViewDesc r16Uav = aoUav;
			r16Uav.m_Format = RHIFormat::R16Float;
			RHITextureViewDesc r16Srv = aoSrv;
			r16Srv.m_Format = RHIFormat::R16Float;
			const D3D12_RESOURCE_DESC nativeR16Desc = ToD3D12ResourceDesc(r16Desc);
			const D3D12_SHADER_RESOURCE_VIEW_DESC nativeR16Srv =
				BuildD3D12ShaderResourceViewDesc(r16Srv, nativeR16Desc);
			const D3D12_UNORDERED_ACCESS_VIEW_DESC nativeR16Uav =
				BuildD3D12UnorderedAccessViewDesc(r16Uav, nativeR16Desc);
			context.Check(ValidateRHITextureUploadData(aoDesc, r8Upload).IsValid() &&
				ValidateRHITextureUploadData(r16Desc, r16Upload).IsValid() &&
				ValidateRHITextureViewDesc(r16Desc, r16Srv).IsValid() &&
				ValidateRHITextureViewDesc(r16Desc, r16Uav).IsValid() &&
				nativeR16Srv.Format == DXGI_FORMAT_R16_FLOAT &&
				nativeR16Uav.Format == DXGI_FORMAT_R16_FLOAT &&
				ValidateRHITextureViewDesc(aoDesc, r16Uav).m_Error ==
				RHITextureValidationError::IncompatibleViewFormat,
				"Single-channel upload pitches and R8/R16 view-family boundaries are exact");

			RHITextureDesc textureDesc{};
			textureDesc.m_Format = RHIFormat::R8G8B8A8Typeless;
			textureDesc.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::CopyDest;
			textureDesc.m_Extent = { 4, 2, 1 };
			textureDesc.m_MipLevels = 2;

			std::array<std::byte, 32> mip0{};
			std::array<std::byte, 8> mip1{};
			RHITextureUploadData uploadData{
				.m_Subresources =
					{
						{.m_Data = mip0.data(), .m_RowPitch = 16, .m_SlicePitch = 32},
						{.m_Data = mip1.data(), .m_RowPitch = 8, .m_SlicePitch = 8},
					},
			};
			context.Check(ValidateRHITextureUploadData(textureDesc, uploadData).IsValid(),
				"RHI texture upload validation accepts a complete mip chain");

			RHITextureUploadData missingMip = uploadData;
			missingMip.m_Subresources.pop_back();
			context.Check(ValidateRHITextureUploadData(textureDesc, missingMip).m_Error ==
				RHITextureValidationError::InvalidUploadSubresourceCount,
				"RHI texture upload validation rejects an incomplete mip chain");

			RHITextureUploadData shortRow = uploadData;
			shortRow.m_Subresources.front().m_RowPitch = 15;
			context.Check(ValidateRHITextureUploadData(textureDesc, shortRow).m_Error ==
				RHITextureValidationError::InvalidUploadRowPitch,
				"RHI texture upload validation rejects a short row pitch");

			RHITextureUploadData shortSlice = uploadData;
			shortSlice.m_Subresources.front().m_SlicePitch = 16;
			context.Check(ValidateRHITextureUploadData(textureDesc, shortSlice).m_Error ==
				RHITextureValidationError::InvalidUploadSlicePitch,
				"RHI texture upload validation rejects a short slice pitch");

			const RHITextureViewDesc typedSrv{
				.m_Type = RHITextureViewType::ShaderResource,
				.m_Dimension = RHITextureViewDimension::Texture2D,
				.m_Format = RHIFormat::R8G8B8A8UnormSrgb,
			};
			context.Check(ValidateRHITextureViewDesc(textureDesc, typedSrv).IsValid(),
				"RHI texture view validation accepts a typed view of a typeless resource");

			RHITextureViewDesc incompatibleView = typedSrv;
			incompatibleView.m_Format = RHIFormat::R16G16Float;
			context.Check(ValidateRHITextureViewDesc(textureDesc, incompatibleView).m_Error ==
				RHITextureValidationError::IncompatibleViewFormat,
				"RHI texture view validation rejects an incompatible format family");

			RHITextureDesc cubeDesc = textureDesc;
			cubeDesc.m_Extent = { 4, 4, 1 };
			cubeDesc.m_ArraySize = 6;
			cubeDesc.m_CreateFlags = RHITextureCreateFlags::CubeCompatible;
			RHITextureViewDesc cubeView = typedSrv;
			cubeView.m_Dimension = RHITextureViewDimension::TextureCube;
			cubeView.m_Subresources.m_ArraySliceCount = 5;
			context.Check(ValidateRHITextureViewDesc(cubeDesc, cubeView).m_Error ==
				RHITextureValidationError::InvalidSubresourceRange,
				"RHI texture view validation rejects an incomplete cube range");

			RHITextureDesc depthDesc{};
			depthDesc.m_Format = RHIFormat::R32Typeless;
			depthDesc.m_Usage = RHITextureUsage::DepthStencil | RHITextureUsage::Sampled;
			RHITextureViewDesc depthSrv{
				.m_Type = RHITextureViewType::ShaderResource,
				.m_Dimension = RHITextureViewDimension::Texture2D,
				.m_Format = RHIFormat::R32Float,
			};
			depthSrv.m_Subresources.m_Aspects = RHITextureAspect::Depth;
			context.Check(ValidateRHITextureViewDesc(depthDesc, depthSrv).IsValid(),
				"RHI texture view validation accepts a typed SRV of a typeless depth resource");

			RHITextureDesc texture1DDesc{};
			texture1DDesc.m_Dimension = RHITextureDimension::Texture1D;
			texture1DDesc.m_Format = RHIFormat::R8G8B8A8Unorm;
			texture1DDesc.m_Usage = RHITextureUsage::Sampled;
			texture1DDesc.m_Extent = { 8, 1, 1 };
			RHITextureViewDesc inferred1DSrv{};
			inferred1DSrv.m_Type = RHITextureViewType::ShaderResource;
			inferred1DSrv.m_Format = texture1DDesc.m_Format;
			const D3D12_SHADER_RESOURCE_VIEW_DESC native1DSrv =
				BuildD3D12ShaderResourceViewDesc(inferred1DSrv, ToD3D12ResourceDesc(texture1DDesc));
			context.Check(ValidateRHITextureViewDesc(texture1DDesc, inferred1DSrv).IsValid() &&
				native1DSrv.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE1D,
				"RHI and DX12 consistently infer an unknown 1D SRV dimension");

			RHITextureDesc texture1DArrayDesc = texture1DDesc;
			texture1DArrayDesc.m_Format = RHIFormat::D32Float;
			texture1DArrayDesc.m_Usage = RHITextureUsage::DepthStencil;
			texture1DArrayDesc.m_ArraySize = 2;
			RHITextureViewDesc inferred1DArrayDsv{};
			inferred1DArrayDsv.m_Type = RHITextureViewType::DepthStencil;
			inferred1DArrayDsv.m_Format = texture1DArrayDesc.m_Format;
			const D3D12_DEPTH_STENCIL_VIEW_DESC native1DArrayDsv = BuildD3D12DepthStencilViewDesc(
				inferred1DArrayDsv, ToD3D12ResourceDesc(texture1DArrayDesc));
			context.Check(
				ValidateRHITextureViewDesc(texture1DArrayDesc, inferred1DArrayDsv).IsValid() &&
				native1DArrayDsv.ViewDimension == D3D12_DSV_DIMENSION_TEXTURE1DARRAY &&
				native1DArrayDsv.Texture1DArray.ArraySize == 2,
				"RHI and DX12 consistently infer an unknown 1D-array DSV dimension");

			RHITextureDesc texture3DDesc{};
			texture3DDesc.m_Dimension = RHITextureDimension::Texture3D;
			texture3DDesc.m_Format = RHIFormat::R8G8B8A8Unorm;
			texture3DDesc.m_Usage = RHITextureUsage::UnorderedAccess;
			texture3DDesc.m_Extent = { 4, 4, 4 };
			RHITextureViewDesc inferred3DUav{};
			inferred3DUav.m_Type = RHITextureViewType::UnorderedAccess;
			inferred3DUav.m_Format = texture3DDesc.m_Format;
			const D3D12_UNORDERED_ACCESS_VIEW_DESC native3DUav = BuildD3D12UnorderedAccessViewDesc(
				inferred3DUav, ToD3D12ResourceDesc(texture3DDesc));
			context.Check(ValidateRHITextureViewDesc(texture3DDesc, inferred3DUav).IsValid() &&
				native3DUav.ViewDimension == D3D12_UAV_DIMENSION_TEXTURE3D,
				"RHI and DX12 consistently infer an unknown 3D UAV dimension");

			RHITextureDesc texture3DDepthDesc = texture3DDesc;
			texture3DDepthDesc.m_Format = RHIFormat::D32Float;
			texture3DDepthDesc.m_Usage = RHITextureUsage::DepthStencil;
			RHITextureViewDesc invalid3DDsv{};
			invalid3DDsv.m_Type = RHITextureViewType::DepthStencil;
			invalid3DDsv.m_Format = texture3DDepthDesc.m_Format;
			context.Check(ValidateRHITextureViewDesc(texture3DDepthDesc, invalid3DDsv).m_Error ==
				RHITextureValidationError::IncompatibleViewDimension,
				"RHI texture view validation rejects unsupported 3D depth-stencil views");

			RHITextureDesc multiPlaneDesc{};
			multiPlaneDesc.m_Format = RHIFormat::D24UnormS8Uint;
			multiPlaneDesc.m_Usage = RHITextureUsage::DepthStencil | RHITextureUsage::CopyDest;
			context.Check(ValidateRHITextureUploadData(multiPlaneDesc, {}).m_Error ==
				RHITextureValidationError::UnsupportedUploadFormat,
				"RHI texture upload validation explicitly rejects unsupported multi-plane data");
		}
	}

	void RunAssetDataSelfTests(SelfTestContext& context) noexcept
	{
		RunSha256Tests(context);
		RunDerivedDataKeyTests(context);
		RunTextureCodecTests(context);
		RunTextureStructureValidationTests(context);
		RunLocalDerivedDataStoreTests(context);
		RunLocalDerivedDataMaintenanceTests(context);
		RunModelImportArtifactTests(context);
		RunRHITextureValidationTests(context);
	}
}
