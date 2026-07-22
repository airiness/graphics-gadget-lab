#include "Core/Precompiled.h"
#include "Application/SelfTest/AssetDataSelfTests.h"
#include "Core/Hash/Sha256.h"
#include "Graphics/Asset/DerivedData/DerivedDataKey.h"
#include "Graphics/Asset/DerivedData/LocalDerivedDataStore.h"
#include "Graphics/Asset/DerivedData/TextureArtifactCodec.h"
#include "Graphics/Asset/ModelImportArtifactCache.h"
#include "Graphics/Asset/TextureArtifactCache.h"
#include "Graphics/Asset/TextureAssetValidation.h"
#include "Graphics/RHI/RHITextureValidation.h"

#include <fstream>
#include <thread>

namespace gglab
{
	namespace
	{
		[[nodiscard]] bool MatchesHex(
			std::span<const std::byte> bytes,
			std::string_view expected) noexcept
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
			std::span<std::byte> bytes,
			size_t offset,
			uint64_t value) noexcept
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
				std::byte{ 0x10 }, std::byte{ 0x11 }, std::byte{ 0x12 }, std::byte{ 0x13 },
				std::byte{ 0x14 }, std::byte{ 0x15 }, std::byte{ 0x16 }, std::byte{ 0x17 },
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
			model.m_Textures.push_back({
				.m_CanonicalPath = "Assets/Textures/SelfTest.png",
				.m_ImportSettings = MakeTextureImportSettings(TextureSemantic::BaseColor),
				.m_Semantic = TextureSemantic::BaseColor,
				.m_Data = MakeTextureFixture(),
			});
			model.m_Meshes.push_back({
				.m_Name = "SelfTestMesh",
				.m_Vertices = { Vertex{} },
				.m_Indices = { 0 },
			});
			return model;
		}

		void RunSha256Tests(SelfTestContext& context) noexcept
		{
			const Sha256Hash emptyHash = ComputeSha256({});
			context.Check(
				MatchesHex(emptyHash.m_Value,
					"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"),
				"SHA-256 empty input matches the standard vector");

			constexpr std::string_view Input = "abc";
			const Sha256Hash abcHash = ComputeSha256(std::as_bytes(std::span{ Input }));
			context.Check(
				MatchesHex(abcHash.m_Value,
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
			const DerivedDataKey key = BuildTextureDerivedDataKey(
				sourceDigest,
				"Textures/Fixture.PNG",
				settings);
			context.Check(
				MatchesHex(key.m_Value,
					"5889e6c301b099379dab9713ef36830489dfba6fee4c20ed6e07e7e4203c7e88"),
				"Texture derived-data key matches the stable golden vector");
		}

		void RunTextureCodecTests(SelfTestContext& context) noexcept
		{
			TextureArtifactBuildResult built = CreateTextureArtifact(MakeTextureFixture());
			const bool buildSucceeded = built.Succeeded();
			TextureArtifact artifact = std::move(built.m_Artifact);
			context.Check(
				buildSucceeded && MatchesHex(artifact.m_ContentDigest.m_Value,
					"9f01361721504e531dce2e8437dd2698515b96e47718b64cd372390e242720f1"),
				"Texture artifact factory validates data and matches the stable digest vector");

			TextureAssetData invalidFixture = MakeTextureFixture();
			invalidFixture.m_Subresources.front().m_DataOffset = 1;
			const TextureArtifactBuildResult invalid = CreateTextureArtifact(
				std::move(invalidFixture));
			context.Check(
				!invalid.Succeeded() &&
					invalid.m_Error == TextureArtifactBuildError::InvalidStructure &&
					invalid.m_StructureError == TextureStructureValidationError::OutOfBounds,
				"Texture artifact factory rejects structurally invalid input before hashing");

			std::vector<std::byte> payload = TextureArtifactCodec::Serialize(artifact);
			const Sha256Hash payloadHash = ComputeSha256(payload);
			context.Check(
				payload.size() == 116 && MatchesHex(payloadHash.m_Value,
					"d2a89c689bc4db351973d96ff9ff6745ac129ca3b2f8f56f210c316cb3bf6c8f"),
				"Texture artifact codec matches the stable payload layout");

			const TextureArtifactDecodeResult decoded = TextureArtifactCodec::Deserialize(
				payload,
				artifact.m_ContentDigest);
			context.Check(
				decoded.Succeeded() && decoded.m_Artifact.m_ContentDigest == artifact.m_ContentDigest &&
					TextureDataMatchesFixture(decoded.m_Artifact.m_Data),
				"Texture artifact codec round-trips the fixture");

			std::vector<std::byte> oversizedDeclaration = payload;
			constexpr size_t SubresourceCountOffset = 10 * sizeof(uint32_t);
			WriteU64LittleEndian(
				oversizedDeclaration,
				SubresourceCountOffset,
				std::numeric_limits<uint64_t>::max());
			const TextureArtifactDecodeResult oversized = TextureArtifactCodec::Deserialize(
				oversizedDeclaration,
				artifact.m_ContentDigest);
			context.Check(
				!oversized.Succeeded() && oversized.m_StructureError ==
					TextureStructureValidationError::ExceedsConfiguredLimit,
				"Texture artifact codec rejects oversized declarations before allocation");

			payload.back() ^= std::byte{ 0xff };
			const TextureArtifactDecodeResult corrupted = TextureArtifactCodec::Deserialize(
				payload,
				artifact.m_ContentDigest);
			context.Check(
				!corrupted.Succeeded() && !corrupted.m_Error.empty(),
				"Texture artifact codec rejects corrupted pixel data");

			payload.pop_back();
			const TextureArtifactDecodeResult truncated = TextureArtifactCodec::Deserialize(
				payload,
				artifact.m_ContentDigest);
			context.Check(
				!truncated.Succeeded() && !truncated.m_Error.empty(),
				"Texture artifact codec rejects truncated payloads");
		}

		void RunTextureStructureValidationTests(SelfTestContext& context) noexcept
		{
			const TextureAssetData valid = MakeTextureFixture();
			context.Check(
				ValidateTextureAssetStructure(valid).IsValid(),
				"Texture structure validation accepts the canonical fixture");

			TextureAssetData outOfBounds = valid;
			outOfBounds.m_Subresources.front().m_DataOffset = 1;
			context.Check(
				ValidateTextureAssetStructure(outOfBounds).m_Error ==
					TextureStructureValidationError::OutOfBounds,
				"Texture structure validation rejects out-of-bounds pixel data");

			TextureAssetData shortRow = valid;
			shortRow.m_Subresources.front().m_RowPitch = 7;
			context.Check(
				ValidateTextureAssetStructure(shortRow).m_Error ==
					TextureStructureValidationError::InvalidRowPitch,
				"Texture structure validation rejects a short row pitch");

			TextureAssetData wrongExtent = valid;
			wrongExtent.m_Subresources.front().m_Width = 1;
			context.Check(
				ValidateTextureAssetStructure(wrongExtent).m_Error ==
					TextureStructureValidationError::InvalidSubresourceExtent,
				"Texture structure validation rejects inconsistent mip extents");

			TextureAssetData duplicate = valid;
			duplicate.m_SrvDimension = RHITextureViewDimension::Texture2DArray;
			duplicate.m_ArraySize = 2;
			duplicate.m_Pixels.resize(16);
			TextureAssetSubresource duplicateSubresource = duplicate.m_Subresources.front();
			duplicateSubresource.m_DataOffset = 8;
			duplicate.m_Subresources.push_back(duplicateSubresource);
			context.Check(
				ValidateTextureAssetStructure(duplicate).m_Error ==
					TextureStructureValidationError::DuplicateSubresource,
				"Texture structure validation rejects duplicate subresource coordinates");

			TextureAssetValidationLimits limits{};
			limits.m_MaxPixelBytes = 7;
			context.Check(
				ValidateTextureAssetStructure(valid, limits).m_Error ==
					TextureStructureValidationError::ExceedsConfiguredLimit,
				"Texture structure validation applies configurable allocation limits");
		}

		void RunLocalDerivedDataStoreTests(SelfTestContext& context) noexcept
		{
			std::error_code errorCode;
			const std::filesystem::path root = std::filesystem::temp_directory_path(errorCode) /
				std::format(
					"gglab.asset-data-self-test.{}.{}",
					GetCurrentProcessId(),
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
				std::byte{ 0x10 },
				std::byte{ 0x20 },
				std::byte{ 0x30 },
				std::byte{ 0x40 },
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
				context.Check(
					observer.Probe(key) == DerivedDataPresence::Missing,
					"Local DDC probe reports a missing filesystem entry");
				const bool externallyWritten = externalWriter.Write(
					key,
					ArtifactType,
					SchemaVersion,
					artifactDigest,
					Payload);
				context.Check(
					externallyWritten && observer.Probe(key) == DerivedDataPresence::Present &&
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
				context.Check(
					externallyRemoved && !errorCode &&
						observer.Probe(key) == DerivedDataPresence::Missing &&
						observer.GetStatistics().m_StoredEntryCount == 1,
					"Local DDC probe observes external deletion before catalog reconciliation");
				const bool reconciledDeletion = observer.ReconcileCatalog();
				context.Check(
					reconciledDeletion && observer.GetStatistics().m_StoredEntryCount == 0,
					"Local DDC reconciliation removes externally deleted entries from diagnostics");

				GGLAB_UNUSED(externalWriter.Write(
					key,
					ArtifactType,
					SchemaVersion,
					artifactDigest,
					Payload));
				{
					std::ofstream corruptStream(entryPath, std::ios::binary | std::ios::trunc);
					constexpr std::array CorruptBytes{ 'b', 'a', 'd' };
					corruptStream.write(CorruptBytes.data(), CorruptBytes.size());
				}
				const uint64_t corruptionCount = observer.GetStatistics().m_CorruptionCount;
				context.Check(
					observer.Probe(key) == DerivedDataPresence::Present &&
						std::filesystem::exists(entryPath) &&
						observer.GetStatistics().m_CorruptionCount == corruptionCount,
					"Local DDC probe does not validate or delete a corrupt entry");
				const DerivedDataReadResult corrupt = observer.Read(
					key,
					ArtifactType,
					SchemaVersion);
				context.Check(
					corrupt.m_Disposition == DerivedDataReadDisposition::Corrupt &&
						!std::filesystem::exists(entryPath) &&
						observer.GetStatistics().m_CorruptionCount == corruptionCount + 1,
					"Local DDC read retains responsibility for corrupt entry disposal");
			}

			{
				LocalDerivedDataStore store(root / "read");
				const bool wrote = store.Write(
					key,
					ArtifactType,
					SchemaVersion,
					artifactDigest,
					Payload);
				const DerivedDataReadResult hit = store.Read(
					key,
					ArtifactType,
					SchemaVersion,
					{
						.m_MaxContainerBytes = ComputeLocalDerivedDataContainerByteLimit(
							ArtifactType,
							Payload.size()),
					});
				context.Check(
					wrote && hit.m_Disposition == DerivedDataReadDisposition::Hit &&
						hit.m_ArtifactContentDigest == artifactDigest &&
						std::ranges::equal(hit.m_Payload, Payload),
					"Local DDC bounded read accepts a container within its payload limit");

				const DerivedDataReadResult oversized = store.Read(
					key,
					ArtifactType,
					SchemaVersion,
					{ .m_MaxContainerBytes = 1 });
				context.Check(
					oversized.m_Disposition == DerivedDataReadDisposition::Corrupt &&
						oversized.m_Payload.empty() && !store.Contains(key) &&
						store.GetStatistics().m_CorruptionCount == 1,
					"Local DDC rejects and discards an oversized container before payload allocation");
			}

			errorCode.clear();
			std::filesystem::remove_all(root, errorCode);
		}

		void RunModelImportArtifactTests(SelfTestContext& context) noexcept
		{
			{
				TextureArtifactCache concurrentCache({ .m_BudgetBytes = 1024 * 1024 });
				std::array<TextureArtifactHandle, 4> handles;
				std::array<std::jthread, 4> workers;
				for (size_t index = 0; index < workers.size(); ++index)
				{
					workers[index] = std::jthread([&concurrentCache, &handles, index]() noexcept
					{
						handles[index] = concurrentCache.CreateAndAdmit(MakeTextureFixture());
					});
				}
				for (std::jthread& worker : workers)
				{
					worker.join();
				}
				context.Check(
					std::ranges::all_of(handles,
						[&handles](const TextureArtifactHandle& handle) noexcept
						{
							return handle && handle == handles.front();
						}) &&
						concurrentCache.GetStatistics().m_AdmissionCount == 1,
					"Texture artifact cache canonicalizes concurrent worker admissions");
			}

			TextureArtifactCache textureCache({ .m_BudgetBytes = 1024 * 1024 });
			ImportedModel source = MakeModelImportFixture();
			const std::byte* const sourcePixels =
				source.m_Textures.front().m_Data.m_Pixels.data();
			ModelImportArtifactHandle artifact = CreateModelImportArtifact(
				std::move(source),
				textureCache);
			context.Check(
				artifact && artifact->IsValid() && artifact->m_Textures.size() == 1 &&
					artifact->m_Textures.front().m_Artifact->m_Data.m_Pixels.data() ==
						sourcePixels,
				"Model import artifacts move texture pixels into immutable texture artifacts");

			ModelImportArtifactHandle duplicate = CreateModelImportArtifact(
				MakeModelImportFixture(),
				textureCache);
			const bool sharesCanonicalTexture = artifact && duplicate &&
				artifact->m_Textures.front().m_Artifact ==
					duplicate->m_Textures.front().m_Artifact;
			context.Check(
				sharesCanonicalTexture &&
					artifact->m_ContentDigest == duplicate->m_ContentDigest,
				"Model import artifacts reference the canonical texture allocation and digest");

			const uint64_t textureBytes = artifact ?
				artifact->m_Textures.front().m_Artifact->GetAllocatedBytes() : 0;
			const ArtifactCacheCoreStatistics textureStatistics = textureCache.GetStatistics();
			context.Check(
				textureBytes != 0 && textureStatistics.m_AdmissionCount == 1 &&
					textureStatistics.m_CachedBytes == textureBytes &&
					textureStatistics.m_TotalLiveBytes == textureBytes,
				"Texture cache accounts a model texture allocation exactly once");

			ModelImportArtifactCache modelCache({ .m_BudgetBytes = 1024 * 1024 });
			artifact = modelCache.Admit(std::move(artifact));
			const uint64_t modelBytes = artifact ? artifact->GetAllocatedBytes() : 0;
			context.Check(
				modelBytes != 0 &&
					modelCache.GetStatistics().m_CachedBytes == modelBytes,
				"Model artifact cache excludes referenced texture allocation bytes");

			ModelMeshUploadSource meshSource{
				.m_Owner = artifact,
				.m_MeshIndex = 0,
			};
			const Vertex* const sourceVertices =
				artifact->m_Meshes.front().m_Vertices.data();
			const uint32_t* const sourceIndices =
				artifact->m_Meshes.front().m_Indices.data();
			context.Check(
				meshSource.IsValid() &&
					meshSource.GetVertices().data() == sourceVertices &&
					meshSource.GetIndices().data() == sourceIndices,
				"Model mesh upload sources resolve immutable payloads without copying");
			context.Check(
				!ModelMeshUploadSource{
					.m_Owner = artifact,
					.m_MeshIndex = 1,
				}.IsValid(),
				"Model mesh upload sources reject out-of-range mesh indices");

			textureCache.Clear();
			const ArtifactCacheCoreStatistics retained = textureCache.GetStatistics();
			context.Check(
				retained.m_CachedBytes == 0 &&
					retained.m_ExternallyRetainedBytes == textureBytes &&
					retained.m_TotalLiveBytes == textureBytes,
				"Model artifacts keep evicted texture cache allocations externally retained");

			modelCache.Clear();
			artifact.reset();
			duplicate.reset();
			const ArtifactCacheCoreStatistics retainedModel = modelCache.GetStatistics();
			context.Check(
				meshSource.IsValid() && retainedModel.m_CachedBytes == 0 &&
					retainedModel.m_ExternallyRetainedBytes == modelBytes &&
					retainedModel.m_TotalLiveBytes == modelBytes,
				"Queued model mesh sources retain artifacts after model cache eviction");
			meshSource.Reset();
			context.Check(
				modelCache.GetStatistics().m_TotalLiveBytes == 0,
				"Model mesh upload sources release artifact ownership after staging");
			context.Check(
				textureCache.GetStatistics().m_TotalLiveBytes == 0,
				"Texture allocation accounting reaches zero after model handles are released");

			ImportedModel invalid = MakeModelImportFixture();
			invalid.m_Textures.front().m_Data.m_Subresources.front().m_DataOffset = 1;
			context.Check(
				!CreateModelImportArtifact(std::move(invalid), textureCache),
				"Model artifact construction rejects an invalid embedded texture");
		}

		void RunRHITextureValidationTests(SelfTestContext& context) noexcept
		{
			RHITextureDesc textureDesc{};
			textureDesc.m_Format = RHIFormat::R8G8B8A8Typeless;
			textureDesc.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::CopyDest;
			textureDesc.m_Extent = { 4, 2, 1 };
			textureDesc.m_MipLevels = 2;

			std::array<std::byte, 32> mip0{};
			std::array<std::byte, 8> mip1{};
			RHITextureUploadData uploadData{
				.m_Subresources = {
					{ .m_Data = mip0.data(), .m_RowPitch = 16, .m_SlicePitch = 32 },
					{ .m_Data = mip1.data(), .m_RowPitch = 8, .m_SlicePitch = 8 },
				},
			};
			context.Check(
				ValidateRHITextureUploadData(textureDesc, uploadData).IsValid(),
				"RHI texture upload validation accepts a complete mip chain");

			RHITextureUploadData missingMip = uploadData;
			missingMip.m_Subresources.pop_back();
			context.Check(
				ValidateRHITextureUploadData(textureDesc, missingMip).m_Error ==
					RHITextureValidationError::InvalidUploadSubresourceCount,
				"RHI texture upload validation rejects an incomplete mip chain");

			RHITextureUploadData shortRow = uploadData;
			shortRow.m_Subresources.front().m_RowPitch = 15;
			context.Check(
				ValidateRHITextureUploadData(textureDesc, shortRow).m_Error ==
					RHITextureValidationError::InvalidUploadRowPitch,
				"RHI texture upload validation rejects a short row pitch");

			RHITextureUploadData shortSlice = uploadData;
			shortSlice.m_Subresources.front().m_SlicePitch = 16;
			context.Check(
				ValidateRHITextureUploadData(textureDesc, shortSlice).m_Error ==
					RHITextureValidationError::InvalidUploadSlicePitch,
				"RHI texture upload validation rejects a short slice pitch");

			const RHITextureViewDesc typedSrv{
				.m_Type = RHITextureViewType::ShaderResource,
				.m_Dimension = RHITextureViewDimension::Texture2D,
				.m_Format = RHIFormat::R8G8B8A8UnormSrgb,
			};
			context.Check(
				ValidateRHITextureViewDesc(textureDesc, typedSrv).IsValid(),
				"RHI texture view validation accepts a typed view of a typeless resource");

			RHITextureViewDesc incompatibleView = typedSrv;
			incompatibleView.m_Format = RHIFormat::R16G16Float;
			context.Check(
				ValidateRHITextureViewDesc(textureDesc, incompatibleView).m_Error ==
					RHITextureValidationError::IncompatibleViewFormat,
				"RHI texture view validation rejects an incompatible format family");

			RHITextureDesc cubeDesc = textureDesc;
			cubeDesc.m_Extent = { 4, 4, 1 };
			cubeDesc.m_ArraySize = 6;
			RHITextureViewDesc cubeView = typedSrv;
			cubeView.m_Dimension = RHITextureViewDimension::TextureCube;
			cubeView.m_Subresources.m_ArraySliceCount = 5;
			context.Check(
				ValidateRHITextureViewDesc(cubeDesc, cubeView).m_Error ==
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
			context.Check(
				ValidateRHITextureViewDesc(depthDesc, depthSrv).IsValid(),
				"RHI texture view validation accepts a typed SRV of a typeless depth resource");

			RHITextureDesc multiPlaneDesc{};
			multiPlaneDesc.m_Format = RHIFormat::D24UnormS8Uint;
			multiPlaneDesc.m_Usage = RHITextureUsage::DepthStencil | RHITextureUsage::CopyDest;
			context.Check(
				ValidateRHITextureUploadData(multiPlaneDesc, {}).m_Error ==
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
		RunModelImportArtifactTests(context);
		RunRHITextureValidationTests(context);
	}
}
