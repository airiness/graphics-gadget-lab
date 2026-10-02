#include "AssetDataSelfTests.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/IBLCacheControlBase.h"
#include "GGLabFoundation/Hash/Sha256.h"
#include "GGLabFoundation/IO/PathUtils.h"
#include "GGLabRuntime/Graphics/Asset/AssetPaths.h"
#include "GGLabRuntime/Graphics/Asset/ModelImporter.h"
#include "GGLabRuntime/Graphics/Asset/DerivedDataKey.h"
#include "Graphics/Asset/DerivedData/IBLDerivedDataSystem.h"
#include "Graphics/Asset/DerivedData/LocalDerivedDataStore.h"
#include "Graphics/Asset/DerivedData/Platform/Win/Win32LocalDerivedDataPlatform.h"
#include "Graphics/Asset/DerivedData/TextureArtifactCodec.h"
#include "Graphics/Asset/ModelImportArtifactCache.h"
#include "Graphics/Asset/Interop/GltfMaterialIdentity.h"
#include "Graphics/MaterialGpuEncoder.h"
#include "Graphics/Asset/IBLStageArtifact.h"
#include "Graphics/Asset/Store/ModelStore.h"
#include "Graphics/Asset/Store/TextureStore.h"
#include "Graphics/Asset/TextureArtifactCache.h"
#include "Graphics/Asset/TextureSourceKey.h"
#include "GGLabRuntime/Graphics/Asset/TextureAssetValidation.h"
#include "Graphics/RHI/DX12/Utility/DX12ResourceDescUtils.h"
#include "Graphics/RHI/DX12/Utility/DX12ViewDescUtils.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureValidation.h"
#include "Graphics/Utility/DXGIFormatUtils.h"

#include <assimp/Importer.hpp>
#include <assimp/GltfMaterial.h>
#include <assimp/material.h>
#include <assimp/scene.h>

#include <Windows.h>

#include <atomic>
#include <array>
#include <chrono>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cwctype>
#include <deque>
#include <fstream>
#include <filesystem>
#include <format>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <numbers>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>

namespace gglab
{
	namespace
	{
		template <typename T>
		concept IBLCacheStatisticsQuery = requires(const T& value) {
			value.GetArtifactCacheStatistics();
		};
		template <typename T>
		concept IBLBakeTick = requires(T& value) { value.Tick({}); };
		static_assert(requires(IBLCacheControlBase& control) {
			{ control.ClearArtifactCache() } noexcept -> std::same_as<void>;
			{ control.ClearDerivedDataStore() } noexcept -> std::same_as<bool>;
		});
		static_assert(!IBLCacheStatisticsQuery<IBLCacheControlBase>);
		static_assert(!IBLBakeTick<IBLCacheControlBase>);

		struct FakeLocalDerivedDataPlatformState final
		{
			std::mutex m_MaintenanceMutex;
			std::mutex m_TokenMutex;
			std::deque<std::string> m_ForcedTokens;
			std::atomic_bool m_ReportAbandoned = false;
			std::atomic_uint64_t m_NextToken = 1;
		};

		class FakeLocalDerivedDataMaintenanceLockGuard final :
			public LocalDerivedDataMaintenanceLockGuardBase
		{
		public:
			FakeLocalDerivedDataMaintenanceLockGuard(
				std::mutex& mutex, bool abandoned) noexcept :
				m_Lock(mutex), m_Abandoned(abandoned)
			{
			}

			[[nodiscard]] bool IsAcquired() const noexcept override
			{
				return m_Lock.owns_lock();
			}
			[[nodiscard]] bool WasAbandoned() const noexcept override
			{
				return IsAcquired() && m_Abandoned;
			}

		private:
			std::unique_lock<std::mutex> m_Lock;
			bool m_Abandoned = false;
		};

		class FakeLocalDerivedDataMaintenanceLock final :
			public LocalDerivedDataMaintenanceLockBase
		{
		public:
			explicit FakeLocalDerivedDataMaintenanceLock(
				std::shared_ptr<FakeLocalDerivedDataPlatformState> state) noexcept :
				m_State(std::move(state))
			{
			}

			[[nodiscard]] bool IsValid() const noexcept override
			{
				return m_State != nullptr;
			}
			[[nodiscard]] std::unique_ptr<LocalDerivedDataMaintenanceLockGuardBase>
				Acquire() const noexcept override
			{
				if (!m_State)
				{
					return nullptr;
				}
				return std::make_unique<FakeLocalDerivedDataMaintenanceLockGuard>(
					m_State->m_MaintenanceMutex,
					m_State->m_ReportAbandoned.exchange(false, std::memory_order_acq_rel));
			}

		private:
			std::shared_ptr<FakeLocalDerivedDataPlatformState> m_State;
		};

		class FakeLocalDerivedDataPlatform final : public LocalDerivedDataPlatformBase
		{
		public:
			explicit FakeLocalDerivedDataPlatform(
				std::shared_ptr<FakeLocalDerivedDataPlatformState> state) noexcept :
				m_State(std::move(state))
			{
			}

			[[nodiscard]] LocalDerivedDataRootIdentity ResolveRootIdentity(
				const std::filesystem::path& rootDirectory) const noexcept override
			{
				if (rootDirectory.empty())
				{
					return {};
				}
				std::error_code errorCode;
				std::filesystem::path canonicalRoot =
					std::filesystem::absolute(rootDirectory, errorCode);
				if (errorCode || canonicalRoot.empty())
				{
					return {};
				}
				canonicalRoot = canonicalRoot.lexically_normal();
				return {
					.m_CanonicalRoot = canonicalRoot,
					.m_PlatformIdentity = canonicalRoot.generic_string(),
				};
			}

			[[nodiscard]] std::unique_ptr<LocalDerivedDataMaintenanceLockBase>
				CreateMaintenanceLock(
					const LocalDerivedDataRootIdentity& identity) const noexcept override
			{
				return identity.IsValid()
					? std::make_unique<FakeLocalDerivedDataMaintenanceLock>(m_State)
					: nullptr;
			}

			[[nodiscard]] std::string CreateUniquePathToken() noexcept override
			{
				std::scoped_lock lock(m_State->m_TokenMutex);
				if (!m_State->m_ForcedTokens.empty())
				{
					std::string token = std::move(m_State->m_ForcedTokens.front());
					m_State->m_ForcedTokens.pop_front();
					return token;
				}
				return std::format("fake.{}",
					m_State->m_NextToken.fetch_add(1, std::memory_order_relaxed));
			}

		private:
			std::shared_ptr<FakeLocalDerivedDataPlatformState> m_State;
		};

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
			const Sha256Digest emptyHash = ComputeSha256({});
			context.Check(MatchesHex(emptyHash.m_Value,
				"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"),
				"SHA-256 empty input matches the standard vector");

			constexpr std::string_view Input = "abc";
			const Sha256Digest abcHash = ComputeSha256(std::as_bytes(std::span{ Input }));
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

		void RunTextureSourceKeyTests(SelfTestContext& context) noexcept
		{
			const TextureSourceKey base{
				"Textures/Shared.png", MakeTextureImportSettings(TextureSemantic::BaseColor)
			};
			TextureSourceKey normal = base;
			normal.m_ImportSettings.m_Semantic = TextureSemantic::Normal;
			TextureSourceKey preserved = base;
			preserved.m_ImportSettings.m_MipPolicy = TextureMipPolicy::Preserve;
			TextureSourceKey other = base;
			other.m_CanonicalPath = "Textures/Other.png";

			struct ConstantHash
			{
				size_t operator()(const TextureSourceKey&) const noexcept { return 0; }
			};
			std::unordered_map<TextureSourceKey, uint32_t, ConstantHash> indices;
			indices.emplace(base, 0u);
			indices.emplace(normal, 1u);
			indices.emplace(preserved, 2u);
			indices.emplace(other, 3u);
			context.Check(indices.size() == 4u && indices.at(base) == 0u &&
				indices.at(normal) == 1u && indices.at(preserved) == 2u && indices.at(other) == 3u,
				"Texture source keys distinguish paths, semantics and mip policies under hash collisions");

			TextureStore store;
			const bool baseBound = store.BindCacheKey(base.m_CanonicalPath, base.m_ImportSettings, TextureID{ 101u });
			const bool normalBound = store.BindCacheKey(normal.m_CanonicalPath, normal.m_ImportSettings, TextureID{ 102u });
			const bool preservedBound = store.BindCacheKey(preserved.m_CanonicalPath, preserved.m_ImportSettings, TextureID{ 103u });
			const bool otherBound = store.BindCacheKey(other.m_CanonicalPath, other.m_ImportSettings, TextureID{ 104u });
			context.Check(baseBound && normalBound && preservedBound && otherBound &&
				store.FindCached(base.m_CanonicalPath, base.m_ImportSettings) == TextureID{ 101u } &&
				store.FindCached(normal.m_CanonicalPath, normal.m_ImportSettings) == TextureID{ 102u } &&
				store.FindCached(preserved.m_CanonicalPath, preserved.m_ImportSettings) == TextureID{ 103u } &&
				store.FindCached(other.m_CanonicalPath, other.m_ImportSettings) == TextureID{ 104u } &&
				!store.BindCacheKey(base.m_CanonicalPath, base.m_ImportSettings, TextureID{ 105u }),
				"Runtime texture lookup uses the shared source key and preserves existing registrations");
			const bool removed = store.Remove(TextureID{ 101u });
			context.Check(removed && !store.FindCached(base.m_CanonicalPath, base.m_ImportSettings).IsValid() &&
				store.FindCached(normal.m_CanonicalPath, normal.m_ImportSettings) == TextureID{ 102u } &&
				store.FindCached(preserved.m_CanonicalPath, preserved.m_ImportSettings) == TextureID{ 103u } &&
				store.FindCached(other.m_CanonicalPath, other.m_ImportSettings) == TextureID{ 104u },
				"Removing one texture registration preserves other source keys");
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
			const Sha256Digest payloadHash = ComputeSha256(payload);
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

				ArtifactContentDigest conflictingDigest = artifactDigest;
				conflictingDigest.m_Value.front() ^= std::byte{ 0xff };
				const bool conflictRejected = !store.Write(
					key, ArtifactType, SchemaVersion, conflictingDigest, Payload);
				const DerivedDataReadResult preserved =
					store.Read(key, ArtifactType, SchemaVersion);
				context.Check(conflictRejected &&
					preserved.m_Disposition == DerivedDataReadDisposition::Hit &&
					preserved.m_ArtifactContentDigest == artifactDigest &&
					std::ranges::equal(preserved.m_Payload, Payload),
					"Portable Local DDC publication rejects a conflicting immutable artifact");

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
				std::unique_ptr platform = CreateWin32LocalDerivedDataPlatform();
				const LocalDerivedDataRootIdentity canonical =
					platform->ResolveRootIdentity(identityRoot / ".");
				const LocalDerivedDataRootIdentity alternate =
					platform->ResolveRootIdentity(std::filesystem::path(alternateText));
				const std::wstring canonicalMutex =
					MakeWin32LocalDerivedDataMaintenanceMutexName(canonical);
				const std::wstring alternateMutex =
					MakeWin32LocalDerivedDataMaintenanceMutexName(alternate);
				context.Check(canonical.IsValid() && alternate.IsValid() &&
					canonical.m_PlatformIdentity == alternate.m_PlatformIdentity &&
					canonicalMutex == alternateMutex &&
					canonicalMutex.starts_with(L"Local\\gglab.ddc."),
					"Local DDC root identity normalizes absolute path spelling and case");
			}

			{
				const auto state = std::make_shared<FakeLocalDerivedDataPlatformState>();
				FakeLocalDerivedDataPlatform platform(state);
				const LocalDerivedDataRootIdentity lower =
					platform.ResolveRootIdentity(root / "case-sensitive");
				const LocalDerivedDataRootIdentity upper =
					platform.ResolveRootIdentity(root / "CASE-SENSITIVE");
				std::unique_ptr maintenance = platform.CreateMaintenanceLock(lower);
				std::unique_ptr guard = maintenance->Acquire();
				const bool acquired = guard && guard->IsAcquired() && !guard->WasAbandoned();
				guard.reset();
				state->m_ReportAbandoned.store(true, std::memory_order_release);
				guard = maintenance->Acquire();
				context.Check(lower.IsValid() && upper.IsValid() &&
					lower.m_PlatformIdentity != upper.m_PlatformIdentity && acquired &&
					guard && guard->IsAcquired() && guard->WasAbandoned(),
					"Fake Local DDC platform preserves target path semantics and abandoned-lock reporting");
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
				std::unique_ptr platform = CreateWin32LocalDerivedDataPlatform();
				const LocalDerivedDataRootIdentity identity =
					platform->ResolveRootIdentity(abandonedRoot);
				const std::wstring mutexName =
					MakeWin32LocalDerivedDataMaintenanceMutexName(identity);
				const std::filesystem::path orphanTemporary =
					abandonedRoot / "orphan.ddc.tmp.crashed";
				GGLAB_UNUSED(std::filesystem::create_directories(abandonedRoot, errorCode));
				{
					std::ofstream orphanStream(orphanTemporary, std::ios::binary | std::ios::trunc);
					orphanStream.put('x');
				}
				HANDLE rawMutex = ::CreateMutexW(nullptr, FALSE, mutexName.c_str());
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
				const std::filesystem::path fakeRoot = root / "fake-platform";
				const auto state = std::make_shared<FakeLocalDerivedDataPlatformState>();
				LocalDerivedDataStore store(
					fakeRoot, std::make_unique<FakeLocalDerivedDataPlatform>(state));
				const std::filesystem::path path = entryPath(fakeRoot);
				GGLAB_UNUSED(std::filesystem::create_directories(path.parent_path(), errorCode));
				const std::filesystem::path collidingTemporary =
					std::filesystem::path(path.string() + ".tmp.collision");
				{
					std::ofstream collisionStream(
						collidingTemporary, std::ios::binary | std::ios::trunc);
					collisionStream.put('x');
				}
				{
					std::scoped_lock tokenLock(state->m_TokenMutex);
					state->m_ForcedTokens.push_back("collision");
					state->m_ForcedTokens.push_back("replacement");
				}
				const bool wrote =
					store.Write(key, ArtifactType, SchemaVersion, artifactDigest, Payload);
				const bool collisionPreserved =
					std::filesystem::exists(collidingTemporary, errorCode);

				const std::filesystem::path orphanTemporary =
					fakeRoot / "orphan.ddc.tmp.fake-abandoned";
				{
					std::ofstream orphanStream(
						orphanTemporary, std::ios::binary | std::ios::trunc);
					orphanStream.put('x');
				}
				state->m_ReportAbandoned.store(true, std::memory_order_release);
				const bool recovered =
					store.Write(key, ArtifactType, SchemaVersion, artifactDigest, Payload);

				const std::filesystem::path collidingTrash = fakeRoot.parent_path() /
					(fakeRoot.filename().string() + ".trash.trash-collision");
				GGLAB_UNUSED(std::filesystem::create_directories(collidingTrash, errorCode));
				{
					std::ofstream collisionStream(
						collidingTrash / "entry.ddc", std::ios::binary | std::ios::trunc);
					collisionStream.put('x');
				}
				{
					std::scoped_lock tokenLock(state->m_TokenMutex);
					state->m_ForcedTokens.push_back("trash-collision");
					state->m_ForcedTokens.push_back("trash-replacement");
				}
				const bool cleared = store.Clear();
				for (uint32_t attempt = 0;
					attempt < 200 && std::filesystem::exists(collidingTrash); ++attempt)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
				}
				context.Check(wrote && collisionPreserved && recovered &&
					!std::filesystem::exists(orphanTemporary) && cleared &&
					std::filesystem::is_directory(fakeRoot) &&
					!std::filesystem::exists(collidingTrash),
					"Portable Local DDC workflow retries temporary/trash token collisions and recovers a fake abandoned owner");
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

		void RunGltfTangentImportTests(SelfTestContext& context) noexcept
		{
			std::error_code errorCode;
			const auto root = std::filesystem::temp_directory_path(errorCode) /
				std::format("gglab-tangent-import-{}-{}", GetCurrentProcessId(), GetTickCount64());
			const bool created = !errorCode && std::filesystem::create_directory(root, errorCode);
			context.Check(created && !errorCode, "glTF tangent regression creates an isolated fixture directory");
			if (!created || errorCode) return;
			for (const bool authored : { false, true })
			{
				for (const bool mirrored : { false, true })
				{
					const float sign = mirrored ? -1.0f : 1.0f;
					const float left = mirrored ? 1.0f : 0.0f;
					const float right = 1.0f - left;
					const std::array<float, 36> data = {
						0, 0, 0, 1, 0, 0, 0, 1, 0,
						0, 0, 1, 0, 0, 1, 0, 0, 1,
						sign, 0, 0, sign, sign, 0, 0, sign, sign, 0, 0, sign,
						left, 1, right, 1, left, 0,
					};
					{
						std::ofstream buffer(root / "probe.bin", std::ios::binary);
						buffer.write(reinterpret_cast<const char*>(data.data()), sizeof(data));
						std::ofstream gltf(root / "probe.gltf");
						gltf << R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],
"nodes":[{"mesh":0}],"buffers":[{"uri":"probe.bin","byteLength":144}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},
{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":48},
{"buffer":0,"byteOffset":120,"byteLength":24}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
{"bufferView":2,"componentType":5126,"count":3,"type":"VEC4"},
{"bufferView":3,"componentType":5126,"count":3,"type":"VEC2"}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":3)"
							<< (authored ? ",\"TANGENT\":2" : "") << "}}]}]}";
					}
					const auto result = ModelImporter::Import(root / "probe.gltf", {});
					bool valid = result.Succeeded();
					for (const auto& mesh : result.m_Model.m_Meshes)
					{
						for (const auto& vertex : mesh.m_Vertices)
						{
							const Vector3 tangent(vertex.m_Tangent.m_X, vertex.m_Tangent.m_Y, vertex.m_Tangent.m_Z);
							const Vector3 bitangent = vertex.m_Normal.Cross(tangent) * vertex.m_Tangent.m_W;
							valid &= (vertex.m_Normal + Vector3::UnitZ).Length() < 0.0001f &&
								(tangent - Vector3(sign, 0, 0)).Length() < 0.0001f &&
								(bitangent - Vector3::UnitY).Length() < 0.0001f;
						}
					}
					context.Check(valid, std::format("glTF {} tangents preserve normal-map +Y up with {} UVs: {}",
						authored ? "authored" : "generated", mirrored ? "mirrored" : "regular", result.m_Error));
				}
			}
			std::filesystem::remove(root / "probe.gltf", errorCode);
			std::filesystem::remove(root / "probe.bin", errorCode);
			std::filesystem::remove(root, errorCode);
		}

		void RunGltfMaterialIdentityTests(SelfTestContext& context) noexcept
		{
			using namespace asset::interop;
			aiScene scene;
			scene.mNumMaterials = 4;
			scene.mMaterials = new aiMaterial*[scene.mNumMaterials]{};
			for (uint32_t index = 0; index < scene.mNumMaterials; ++index)
			{
				scene.mMaterials[index] = new aiMaterial();
			}
			const auto setName = [&](uint32_t index, std::string_view name)
			{
				const aiString identity{ std::string(name) };
				GGLAB_UNUSED(scene.mMaterials[index]->AddProperty(&identity, AI_MATKEY_NAME));
			};
			setName(0, MakeGltfMaterialIdentity(2));
			setName(1, "Unused generated material");
			setName(2, MakeGltfMaterialIdentity(0));
			setName(3, MakeGltfMaterialIdentity(1));
			scene.mNumMeshes = 3;
			scene.mMeshes = new aiMesh*[scene.mNumMeshes]{};
			constexpr std::array<uint32_t, 3> meshMaterials{ 3, 0, 2 };
			for (uint32_t index = 0; index < scene.mNumMeshes; ++index)
			{
				scene.mMeshes[index] = new aiMesh();
				scene.mMeshes[index]->mMaterialIndex = meshMaterials[index];
			}
			std::vector<size_t> sources;
			std::string error;
			context.Check(ResolveGltfMaterialSources(scene, 3, sources, error) &&
				sources == std::vector<size_t>{ 2, NoGltfMaterialSource, 0, 1 },
				"Material identities resolve arbitrary Assimp order with an unused generated material in the middle");
			std::swap(scene.mMaterials[0], scene.mMaterials[2]);
			scene.mMeshes[1]->mMaterialIndex = 2;
			scene.mMeshes[2]->mMaterialIndex = 0;
			context.Check(ResolveGltfMaterialSources(scene, 3, sources, error) &&
				sources == std::vector<size_t>{ 0, NoGltfMaterialSource, 2, 1 },
				"Reordering Assimp materials preserves mesh-to-source identity");
			context.Check(ResolveGltfMaterialSources(scene, 4, sources, error),
				"Material identity mapping does not require a fixed source-to-Assimp material count");
			setName(0, MakeGltfMaterialIdentity(3));
			error.clear();
			context.Check(!ResolveGltfMaterialSources(scene, 3, sources, error) && !error.empty(),
				"Out-of-range source identity fails visibly");
			setName(0, MakeGltfMaterialIdentity(0) + "suffix");
			context.Check(!ResolveGltfMaterialSources(scene, 3, sources, error),
				"Source identity parsing consumes the complete tag");
			setName(0, "");
			context.Check(!ResolveGltfMaterialSources(scene, 3, sources, error),
				"A used material without preserved identity fails instead of using another source's extensions");
			setName(0, MakeGltfMaterialIdentity(0));
			scene.mMeshes[0]->mMaterialIndex = scene.mNumMaterials;
			context.Check(!ResolveGltfMaterialSources(scene, 3, sources, error),
				"Out-of-range Assimp mesh material index fails before publication");
		}

		void RunGltfMaterialImportTests(SelfTestContext& context) noexcept
		{
			std::error_code errorCode;
			const auto root = std::filesystem::temp_directory_path(errorCode) /
				std::format("gglab-material-import-{}-{}", GetCurrentProcessId(), GetTickCount64());
			const bool created = !errorCode && std::filesystem::create_directory(root, errorCode);
			context.Check(created && !errorCode, "Material import creates an isolated glTF fixture directory");
			if (!created || errorCode) return;

			const std::array<float, 30> vertices = {
				0, 0, 0, 1, 0, 0, 0, 1, 0,
				0, 0, 1, 0, 0, 1, 0, 0, 1,
				0, 0, 1, 0, 0, 1,
				0, 1, 1, 1, 0, 0,
			};
			{
				std::ofstream buffer(root / "probe.bin", std::ios::binary);
				buffer.write(reinterpret_cast<const char*>(vertices.data()), sizeof(vertices));
			}

			auto writeSource = [&](std::string_view extensions, std::string_view material,
				int firstMaterial = 0, int secondMaterial = -1,
				std::string_view textureDeclarations =
				R"("images":[{"uri":"map.png"}],"textures":[{"source":0}],)") noexcept
			{
				std::ofstream gltf(root / "probe.gltf");
				gltf << R"({"asset":{"version":"2.0"},)" << extensions << R"(
"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],
"buffers":[{"uri":"probe.bin","byteLength":120}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},
{"buffer":0,"byteOffset":36,"byteLength":36},
{"buffer":0,"byteOffset":72,"byteLength":24},
{"buffer":0,"byteOffset":96,"byteLength":24}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
{"bufferView":3,"componentType":5126,"count":3,"type":"VEC2"}],
)" << textureDeclarations << R"("materials":[)" << material << R"(],
"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2,"TEXCOORD_1":3})";
				if (firstMaterial >= 0) gltf << R"(,"material":)" << firstMaterial;
				gltf << '}';
				if (secondMaterial >= 0)
				{
					gltf << R"(,{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2,"TEXCOORD_1":3},"material":)"
						<< secondMaterial << '}';
				}
				gltf << R"(]}]})";
			};

			writeSource(R"("extensionsUsed":["KHR_texture_transform"],)",
				R"({"name":"MaterialProbe","pbrMetallicRoughness":{"baseColorTexture":{"index":0,"texCoord":0,"extensions":{"KHR_texture_transform":{"offset":[0.2,0.3],"rotation":0.5,"scale":[2,3],"texCoord":1}}},"metallicRoughnessTexture":{"index":0,"extensions":{"KHR_texture_transform":{"offset":[0.11,0]}}}},"normalTexture":{"index":0,"scale":0.35,"extensions":{"KHR_texture_transform":{"rotation":0.25}}},"occlusionTexture":{"index":0,"strength":0.4,"extensions":{"KHR_texture_transform":{"scale":[0.8,-1]}}},"emissiveTexture":{"index":0,"extensions":{"KHR_texture_transform":{"offset":[0,0.17]}}}})");
			const ModelImportResult imported = ModelImporter::Import(root / "probe.gltf", {});
			bool valid = imported.Succeeded() && imported.m_Model.m_Materials.size() == 2;
			std::string importDetail = imported.m_Error;
			if (valid)
			{
				const ImportedMaterial& material = imported.m_Model.m_Materials.front();
				const auto& base = material.m_TextureBindings[static_cast<size_t>(MaterialTextureSlot::BaseColor)];
				const auto& normal = material.m_TextureBindings[static_cast<size_t>(MaterialTextureSlot::Normal)];
				const auto& occlusion = material.m_TextureBindings[static_cast<size_t>(MaterialTextureSlot::Occlusion)];
				const auto& metallicRoughness = material.m_TextureBindings[static_cast<size_t>(MaterialTextureSlot::MetallicRoughness)];
				const auto& emissive = material.m_TextureBindings[static_cast<size_t>(MaterialTextureSlot::Emissive)];
				valid = base.m_TextureIndex != ImportedMaterialTextureBinding::InvalidTextureIndex &&
					base.m_TexCoordIndex == 1 && std::abs(base.m_UVOffset.m_X - 0.2f) < 0.00001f &&
					std::abs(base.m_UVOffset.m_Y - 0.3f) < 0.00001f &&
					std::abs(base.m_UVScale.m_X - 2.0f) < 0.00001f &&
					std::abs(base.m_UVScale.m_Y - 3.0f) < 0.00001f &&
					std::abs(base.m_UVRotation - 0.5f) < 0.00001f &&
					normal.m_TextureIndex != ImportedMaterialTextureBinding::InvalidTextureIndex &&
					occlusion.m_TextureIndex != ImportedMaterialTextureBinding::InvalidTextureIndex &&
					metallicRoughness.m_TextureIndex != ImportedMaterialTextureBinding::InvalidTextureIndex &&
					emissive.m_TextureIndex != ImportedMaterialTextureBinding::InvalidTextureIndex &&
					std::abs(metallicRoughness.m_UVOffset.m_X - 0.11f) < 0.00001f &&
					std::abs(normal.m_UVRotation - 0.25f) < 0.00001f &&
					std::abs(occlusion.m_UVScale.m_X - 0.8f) < 0.00001f &&
					std::abs(occlusion.m_UVScale.m_Y + 1.0f) < 0.00001f &&
					std::abs(emissive.m_UVOffset.m_Y - 0.17f) < 0.00001f &&
					std::abs(material.m_Properties.m_NormalScale - 0.35f) < 0.00001f &&
					std::abs(material.m_Properties.m_OcclusionStrength - 0.4f) < 0.00001f;
				importDetail = std::format("base UV{}, offset=({}, {}), scale=({}, {}), rotation={}, normal={}, occlusion={}",
					base.m_TexCoordIndex, base.m_UVOffset.m_X, base.m_UVOffset.m_Y,
					base.m_UVScale.m_X, base.m_UVScale.m_Y, base.m_UVRotation,
					material.m_Properties.m_NormalScale, material.m_Properties.m_OcclusionStrength);
			}
			context.Check(valid, std::format(
				"Core normal/occlusion inputs and per-texture transform survive import: {}", importDetail));

			writeSource(R"("extensionsUsed":["KHR_texture_transform"],"extensionsRequired":["KHR_texture_transform"],)",
				R"({"pbrMetallicRoughness":{"baseColorTexture":{"index":0,"extensions":{"KHR_texture_transform":false}}}})");
			const ModelImportResult invalidTransform = ModelImporter::Import(root / "probe.gltf", {});
			context.Check(!invalidTransform.Succeeded() &&
				invalidTransform.m_Error.find("KHR_texture_transform") != std::string::npos,
				"Malformed required texture transform is rejected visibly");
			writeSource(R"("extensionsUsed":["KHR_texture_transform"],"extensionsRequired":["KHR_texture_transform"],)",
				R"({"pbrMetallicRoughness":{"baseColorTexture":{"index":0,"extensions":{"KHR_texture_transform":{"texCoord":4294967296}}}}})");
			const ModelImportResult largeTexCoord = ModelImporter::Import(root / "probe.gltf", {});
			context.Check(!largeTexCoord.Succeeded() &&
				largeTexCoord.m_Error.find("TEXCOORD") != std::string::npos,
				"Out-of-range UV override cannot wrap into a supported TEXCOORD set");

			writeSource(R"("extensionsUsed":["KHR_materials_ior"],)",
				R"({"pbrMetallicRoughness":{"baseColorFactor":[0.2,0.3,0.4,1]},"extensions":{"KHR_materials_ior":{"ior":1.7}}})");
			const ModelImportResult optional = ModelImporter::Import(root / "probe.gltf", {});
			context.Check(optional.Succeeded() && optional.m_Model.m_Materials.size() == 2 &&
				std::abs(optional.m_Model.m_Materials.front().m_Properties.m_Ior - 1.7f) < 0.0001f &&
				optional.m_Model.m_Materials.back().m_Properties.m_Ior == DefaultDielectricIor,
				"Optional KHR_materials_ior survives Assimp import; absent IOR uses 1.5");

			writeSource(R"("extensionsUsed":["KHR_materials_ior"],"extensionsRequired":["KHR_materials_ior"],)",
				R"({"extensions":{"KHR_materials_ior":{"ior":1.7}}})");
			const ModelImportResult required = ModelImporter::Import(root / "probe.gltf", {});
			context.Check(required.Succeeded() &&
				std::abs(required.m_Model.m_Materials.front().m_Properties.m_Ior - 1.7f) < 0.0001f,
				"Required KHR_materials_ior survives Assimp import");

			writeSource(R"("extensionsUsed":["KHR_materials_ior"],"extensionsRequired":["KHR_materials_ior"],)",
				R"({"extensions":{"KHR_materials_ior":{}}})");
			const ModelImportResult emptyIor = ModelImporter::Import(root / "probe.gltf", {});
			context.Check(emptyIor.Succeeded() &&
				emptyIor.m_Model.m_Materials.front().m_Properties.m_Ior == DefaultDielectricIor,
				"Empty KHR_materials_ior uses the physical default");

			writeSource(R"("extensionsUsed":["KHR_materials_ior","KHR_materials_clearcoat","KHR_materials_anisotropy"],)",
				R"({"extensions":{"KHR_materials_ior":{},"KHR_materials_clearcoat":{},"KHR_materials_anisotropy":{}}})");
			const ModelImportResult emptyExtensions = ModelImporter::Import(root / "probe.gltf", {});
			bool extensionDefaults = emptyExtensions.Succeeded();
			if (extensionDefaults)
			{
				const auto& properties = emptyExtensions.m_Model.m_Materials.front().m_Properties;
				extensionDefaults = properties.m_Ior == DefaultDielectricIor &&
					properties.m_ClearcoatFactor == 0.0f && properties.m_ClearcoatRoughness == 0.0f &&
					properties.m_ClearcoatNormalScale == 1.0f &&
					properties.m_AnisotropyStrength == 0.0f && properties.m_AnisotropyRotation == 0.0f &&
					emptyExtensions.m_Model.m_TextureSources.empty();
			}
			context.Check(extensionDefaults, std::format(
				"Empty material extensions preserve defaults without texture dependencies: {}", emptyExtensions.m_Error));

			writeSource(R"("extensionsUsed":["KHR_materials_ior"],)",
				R"({"extensions":{"KHR_materials_ior":{"ior":2.4}}})");
			const ModelImportResult highIor = ModelImporter::Import(root / "probe.gltf", {});
			context.Check(highIor.Succeeded() &&
				std::abs(highIor.m_Model.m_Materials.front().m_Properties.m_Ior - 2.4f) < 0.0001f,
				"Valid IOR above common dielectric values is preserved");

			writeSource(R"("extensionsUsed":["KHR_materials_ior"],)",
				R"({"name":"Repeated","extensions":{"KHR_materials_ior":{"ior":1.2}}},{"name":"Repeated","extensions":{"KHR_materials_ior":{"ior":2.2}}})", 1, 0);
			const ModelImportResult duplicateNames = ModelImporter::Import(root / "probe.gltf", {});
			context.Check(duplicateNames.Succeeded() &&
				duplicateNames.m_Model.m_Materials.size() == 3u &&
				duplicateNames.m_Model.m_Materials[0].m_Name == "Repeated" &&
				duplicateNames.m_Model.m_Materials[1].m_Name == "Repeated" &&
				std::abs(duplicateNames.m_Model.m_Materials[0].m_Properties.m_Ior - 2.2f) < 0.0001f &&
				std::abs(duplicateNames.m_Model.m_Materials[1].m_Properties.m_Ior - 1.2f) < 0.0001f,
				std::format("Duplicate material names retain their glTF index identities (error='{}', count={})",
					duplicateNames.m_Error, duplicateNames.m_Model.m_Materials.size()));

			writeSource(R"("extensionsUsed":["KHR_materials_ior"],)",
				R"({"extensions":{"KHR_materials_ior":{"ior":1.25}}},{"extensions":{"KHR_materials_ior":{"ior":2.25}}})", 0, 1);
			const ModelImportResult unnamedMaterials = ModelImporter::Import(root / "probe.gltf", {});
			context.Check(unnamedMaterials.Succeeded() &&
				unnamedMaterials.m_Model.m_Materials.size() == 3u &&
				unnamedMaterials.m_Model.m_Materials[0].m_Name.empty() &&
				unnamedMaterials.m_Model.m_Materials[1].m_Name.empty() &&
				std::abs(unnamedMaterials.m_Model.m_Materials[0].m_Properties.m_Ior - 1.25f) < 0.0001f &&
				std::abs(unnamedMaterials.m_Model.m_Materials[1].m_Properties.m_Ior - 2.25f) < 0.0001f,
				std::format("Unnamed materials retain their glTF index identities (error='{}', count={})",
					unnamedMaterials.m_Error, unnamedMaterials.m_Model.m_Materials.size()));

			writeSource(R"("extensionsUsed":["KHR_materials_ior"],)",
				R"({"extensions":{"KHR_materials_ior":{"ior":1.3}}},{"extensions":{"KHR_materials_ior":{"ior":2.3}}})");
			const ModelImportResult unreferencedMaterial = ModelImporter::Import(root / "probe.gltf", {});
			context.Check(unreferencedMaterial.Succeeded() &&
				unreferencedMaterial.m_Model.m_Materials.size() == 2u &&
				std::abs(unreferencedMaterial.m_Model.m_Materials[0].m_Properties.m_Ior - 1.3f) < 0.0001f,
				"Unreferenced glTF materials do not shift Assimp's dense material indices");
			writeSource(R"("extensionsUsed":["KHR_materials_ior"],)",
				R"({"extensions":{"KHR_materials_ior":{"ior":1.3}}},{"extensions":{"KHR_materials_ior":{"ior":2.3}}})", 1);
			const ModelImportResult laterMaterial = ModelImporter::Import(root / "probe.gltf", {});
			context.Check(laterMaterial.Succeeded() &&
				laterMaterial.m_Model.m_Materials.size() == 2u &&
				std::abs(laterMaterial.m_Model.m_Materials[0].m_Properties.m_Ior - 2.3f) < 0.0001f,
				"A later glTF material can occupy the first dense Assimp index");

			writeSource(R"("extensionsUsed":["KHR_materials_ior"],)",
				R"({"name":"gglab.material.1","extensions":{"KHR_materials_ior":{"ior":1.3}}},{"name":"gglab.material.0","pbrMetallicRoughness":{"metallicFactor":0.7,"roughnessFactor":0.3},"extensions":{"KHR_materials_ior":{"ior":2.3}}})", 1);
			const auto readSourceText = [&]()
			{
				std::ifstream source(root / "probe.gltf", std::ios::binary);
				return std::string(std::istreambuf_iterator<char>(source), std::istreambuf_iterator<char>());
			};
			const std::string authoredSource = readSourceText();
			const ModelImportResult identityNames = ModelImporter::Import(root / "probe.gltf", {});
			context.Check(identityNames.Succeeded() && identityNames.m_Model.m_Materials[0].m_Name == "gglab.material.0" &&
				std::abs(identityNames.m_Model.m_Materials[0].m_Properties.m_Ior - 2.3f) < 0.0001f &&
				std::abs(identityNames.m_Model.m_Materials[0].m_Properties.m_MetallicFactor - 0.7f) < 0.0001f &&
				std::abs(identityNames.m_Model.m_Materials[0].m_Properties.m_RoughnessFactor - 0.3f) < 0.0001f,
				"Authored names resembling identity tags are restored and keep core and extension factors together");
			context.Check(readSourceText() == authoredSource,
				"Material identity overlay leaves the original glTF bytes unchanged");

			writeSource(R"("extensionsUsed":["KHR_materials_ior"],)",
				R"({"name":"Unused","extensions":{"KHR_materials_ior":{"ior":1.3}}},{"name":"Explicit","extensions":{"KHR_materials_ior":{"ior":2.3}}})", -1, 1);
			const ModelImportResult mixedDefault = ModelImporter::Import(root / "probe.gltf", {});
			bool mappedDefault = false;
			bool mappedExplicit = false;
			for (const ImportedMesh& mesh : mixedDefault.m_Model.m_Meshes)
			{
				const ImportedMaterial& material = mixedDefault.m_Model.m_Materials[mesh.m_MaterialIndex];
				mappedDefault |= material.m_Name.empty() && std::abs(material.m_Properties.m_Ior - 1.5f) < 0.0001f;
				mappedExplicit |= material.m_Name == "Explicit" && std::abs(material.m_Properties.m_Ior - 2.3f) < 0.0001f;
			}
			context.Check(mixedDefault.Succeeded() && mappedDefault && mappedExplicit,
				std::format("Missing primitive material retains default PBR alongside an explicit source material: {}", mixedDefault.m_Error));

			writeSource("", "", -1);
			std::string noMaterialSource = readSourceText();
			constexpr std::string_view EmptyMaterials = "\"materials\":[],";
			noMaterialSource.erase(noMaterialSource.find(EmptyMaterials), EmptyMaterials.size());
			{
				std::ofstream source(root / "probe.gltf");
				source << noMaterialSource;
			}
			const ModelImportResult noMaterials = ModelImporter::Import(root / "probe.gltf", {});
			context.Check(noMaterials.Succeeded() && noMaterials.m_Model.m_Materials.front().m_Name.empty() &&
				std::abs(noMaterials.m_Model.m_Materials.front().m_Properties.m_Ior - 1.5f) < 0.0001f,
				std::format("A glTF without a materials array imports its default PBR material: {}", noMaterials.m_Error));

			writeSource("", R"({"name":"Explicit"})", -1, 1);
			const ModelImportResult invalidDefaultIndex = ModelImporter::Import(root / "probe.gltf", {});
			context.Check(!invalidDefaultIndex.Succeeded() && !invalidDefaultIndex.m_Error.empty(),
				"An invalid explicit material index cannot alias the injected default material");

			writeSource(R"("extensionsUsed":["KHR_materials_ior"],)",
				R"({"extensions":{"KHR_materials_ior":{"ior":0}}})");
			const ModelImportResult infiniteIor = ModelImporter::Import(root / "probe.gltf", {});
			context.Check(infiniteIor.Succeeded() &&
				infiniteIor.m_Model.m_Materials.front().m_Properties.m_Ior == 0.0f,
				"Zero IOR preserves the glTF infinite-Fresnel mode");

			writeSource(R"("extensionsUsed":["KHR_materials_ior"],)",
				R"({"extensions":{"KHR_materials_ior":{"ior":0.9}}})");
			const ModelImportResult invalidIor = ModelImporter::Import(root / "probe.gltf", {});
			context.Check(!invalidIor.Succeeded() &&
				invalidIor.m_Error.find("KHR_materials_ior") != std::string::npos,
				"IOR below the glTF physical range is rejected visibly");

			writeSource(R"("extensionsUsed":["KHR_materials_ior"],)",
				R"({"extensions":{"KHR_materials_ior":{"ior":"glass"}}})");
			const ModelImportResult malformedIor = ModelImporter::Import(root / "probe.gltf", {});
			context.Check(!malformedIor.Succeeded() &&
				malformedIor.m_Error.find("KHR_materials_ior") != std::string::npos,
				"Non-numeric IOR is rejected visibly");

			writeSource(R"("extensionsUsed":["KHR_materials_clearcoat"],"extensionsRequired":["KHR_materials_clearcoat"],)",
				R"({"extensions":{"KHR_materials_clearcoat":{"clearcoatFactor":0.6,"clearcoatRoughnessFactor":0.25,"clearcoatTexture":{"index":0,"texCoord":1,"extensions":{"KHR_texture_transform":{"offset":[0.1,0.2]}}},"clearcoatRoughnessTexture":{"index":0},"clearcoatNormalTexture":{"index":0,"scale":0.45}}}})");
			const ModelImportResult requiredCoat = ModelImporter::Import(root / "probe.gltf", {});
			bool coatValid = requiredCoat.Succeeded();
			if (coatValid)
			{
				const auto& material = requiredCoat.m_Model.m_Materials.front();
				const auto& factor = material.m_TextureBindings[static_cast<uint32_t>(MaterialTextureSlot::Clearcoat)];
				const auto& roughness = material.m_TextureBindings[static_cast<uint32_t>(MaterialTextureSlot::ClearcoatRoughness)];
				const auto& normal = material.m_TextureBindings[static_cast<uint32_t>(MaterialTextureSlot::ClearcoatNormal)];
				coatValid = std::abs(material.m_Properties.m_ClearcoatFactor - 0.6f) < 0.0001f &&
					std::abs(material.m_Properties.m_ClearcoatRoughness - 0.25f) < 0.0001f &&
					std::abs(material.m_Properties.m_ClearcoatNormalScale - 0.45f) < 0.0001f &&
					factor.m_TextureIndex != ImportedMaterialTextureBinding::InvalidTextureIndex &&
					roughness.m_TextureIndex != ImportedMaterialTextureBinding::InvalidTextureIndex &&
					normal.m_TextureIndex != ImportedMaterialTextureBinding::InvalidTextureIndex &&
					factor.m_TexCoordIndex == 1 && std::abs(factor.m_UVOffset.m_X - 0.1f) < 0.0001f &&
					requiredCoat.m_Model.m_TextureSources.size() == 2;
			}
			context.Check(coatValid,
				std::format("Required clearcoat factors and three texture roles survive import: {}", requiredCoat.m_Error));

			writeSource(R"("extensionsUsed":["KHR_materials_clearcoat"],)",
				R"({"extensions":{"KHR_materials_clearcoat":{"clearcoatFactor":0,"clearcoatRoughnessFactor":0.4}}})");
			const ModelImportResult disabledCoat = ModelImporter::Import(root / "probe.gltf", {});
			context.Check(disabledCoat.Succeeded() &&
				disabledCoat.m_Model.m_Materials.front().m_Properties.m_ClearcoatFactor == 0.0f &&
				std::abs(disabledCoat.m_Model.m_Materials.front().m_Properties.m_ClearcoatRoughness - 0.4f) < 0.0001f,
				"Zero clearcoat factor retains the authored roughness without enabling the layer");

			constexpr std::string_view coatTextureDeclarations = R"(
"images":[{"uri":"unused.png"},{"uri":"coat.png"}],
"samplers":[{}, {"wrapS":33071,"wrapT":33648,"magFilter":9728,"minFilter":9728}],
"textures":[{"source":0},{"source":1,"sampler":1}],)";
			auto writeCoatWithTextures = [&](std::string_view factor) noexcept
			{
				const std::string material =
					std::string(R"({"normalTexture":{"index":1,"scale":0.35},"extensions":{"KHR_materials_clearcoat":{)") +
					std::string(factor) + R"("clearcoatRoughnessFactor":0.4,
"clearcoatTexture":{"index":1,"texCoord":0,"extensions":{"KHR_texture_transform":{"texCoord":1,"offset":[0.1,0.2],"scale":[-2,0.5],"rotation":0.25}}},
"clearcoatRoughnessTexture":{"index":1},"clearcoatNormalTexture":{"index":1,"scale":2}}}})";
				writeSource(R"("extensionsUsed":["KHR_materials_clearcoat","KHR_texture_transform"],"extensionsRequired":["KHR_materials_clearcoat"],)",
					material, 0, -1, coatTextureDeclarations);
			};
			writeCoatWithTextures(R"("clearcoatFactor":1,)");
			const ModelImportResult coatBindingReference = ModelImporter::Import(root / "probe.gltf", {});
			context.Check(coatBindingReference.Succeeded() && coatBindingReference.m_Model.m_TextureSources.size() == 3u,
				std::format("Enabled clearcoat establishes the authored texture binding reference: {}",
					coatBindingReference.m_Error));
			for (const std::string_view factor : { std::string_view(R"("clearcoatFactor":0,)"), std::string_view{} })
			{
				writeCoatWithTextures(factor);
				const ModelImportResult disabledTexturedCoat = ModelImporter::Import(root / "probe.gltf", {});
				bool bindingsMatch = disabledTexturedCoat.Succeeded() && coatBindingReference.Succeeded();
				if (bindingsMatch)
				{
					const ImportedMaterial& material = disabledTexturedCoat.m_Model.m_Materials.front();
					const ImportedMaterial& reference = coatBindingReference.m_Model.m_Materials.front();
					bindingsMatch = material.m_Properties.m_ClearcoatFactor == 0.0f &&
						material.m_Properties.m_ClearcoatRoughness == 0.4f &&
						material.m_Properties.m_ClearcoatNormalScale == 2.0f &&
						material.m_Properties.m_NormalScale == 0.35f &&
						disabledTexturedCoat.m_Model.m_TextureSources.size() == 3u;
					for (const MaterialTextureSlot slot : { MaterialTextureSlot::Clearcoat,
						MaterialTextureSlot::ClearcoatRoughness, MaterialTextureSlot::ClearcoatNormal })
					{
						const auto& binding = material.m_TextureBindings[static_cast<size_t>(slot)];
						const auto& expected = reference.m_TextureBindings[static_cast<size_t>(slot)];
						bindingsMatch &= binding.m_TextureIndex != ImportedMaterialTextureBinding::InvalidTextureIndex &&
							binding.m_TextureIndex < disabledTexturedCoat.m_Model.m_TextureSources.size() &&
							binding.m_TextureIndex == expected.m_TextureIndex &&
							binding.m_SamplerKey == expected.m_SamplerKey &&
							binding.m_TexCoordIndex == expected.m_TexCoordIndex &&
							binding.m_UVOffset.m_X == expected.m_UVOffset.m_X &&
							binding.m_UVOffset.m_Y == expected.m_UVOffset.m_Y &&
							binding.m_UVScale.m_X == expected.m_UVScale.m_X &&
							binding.m_UVScale.m_Y == expected.m_UVScale.m_Y &&
							binding.m_UVRotation == expected.m_UVRotation;
						if (binding.m_TextureIndex < disabledTexturedCoat.m_Model.m_TextureSources.size())
						{
							bindingsMatch &= disabledTexturedCoat.m_Model.m_TextureSources[binding.m_TextureIndex].m_CanonicalPath ==
								utils::Canonical(root / "coat.png");
						}
					}
				}
				context.Check(bindingsMatch, std::format(
					"{} zero clearcoat retains all texture bindings, samplers, UV transforms and independent normal scales: {}",
					factor.empty() ? "Default" : "Explicit", disabledTexturedCoat.m_Error));
			}

			writeSource(R"("extensionsUsed":["KHR_materials_clearcoat"],)",
				R"({"extensions":{"KHR_materials_clearcoat":{"clearcoatTexture":{"index":0}}}})");
			const ModelImportResult defaultCoatSampler = ModelImporter::Import(root / "probe.gltf", {});
			context.Check(defaultCoatSampler.Succeeded() && requiredCoat.Succeeded() &&
				defaultCoatSampler.m_Model.m_Materials.front().m_TextureBindings[static_cast<size_t>(MaterialTextureSlot::Clearcoat)].m_SamplerKey ==
				requiredCoat.m_Model.m_Materials.front().m_TextureBindings[static_cast<size_t>(MaterialTextureSlot::Clearcoat)].m_SamplerKey,
				"Disabled and enabled clearcoat bindings use the same default sampler");
			writeSource(R"("extensionsUsed":["KHR_materials_clearcoat"],)",
				R"({"extensions":{"KHR_materials_clearcoat":{"clearcoatTexture":{"index":0}}}})", 0, -1,
				R"("images":[{"uri":"map.png"}],"samplers":[{"wrapS":999}],"textures":[{"source":0,"sampler":0}],)");
			const ModelImportResult invalidCoatSampler = ModelImporter::Import(root / "probe.gltf", {});
			context.Check(!invalidCoatSampler.Succeeded() &&
				invalidCoatSampler.m_Error.find("sampler") != std::string::npos,
				"Disabled clearcoat still rejects invalid sampler values");

			writeSource(R"("extensionsUsed":["KHR_materials_clearcoat"],)",
				R"({"extensions":{"KHR_materials_clearcoat":{"clearcoatFactor":0,"clearcoatTexture":{"index":999}}}})");
			const ModelImportResult invalidDisabledCoatTexture = ModelImporter::Import(root / "probe.gltf", {});
			context.Check(!invalidDisabledCoatTexture.Succeeded() && !invalidDisabledCoatTexture.m_Error.empty(),
				"Disabled clearcoat still rejects invalid texture indices");
			writeSource(R"("extensionsUsed":["KHR_materials_clearcoat","KHR_texture_transform"],)",
				R"({"extensions":{"KHR_materials_clearcoat":{"clearcoatFactor":0,"clearcoatTexture":{"index":0,"extensions":{"KHR_texture_transform":{"texCoord":2}}}}}})");
			const ModelImportResult invalidDisabledCoatUV = ModelImporter::Import(root / "probe.gltf", {});
			context.Check(!invalidDisabledCoatUV.Succeeded() &&
				invalidDisabledCoatUV.m_Error.find("TEXCOORD") != std::string::npos,
				"Disabled clearcoat still rejects unsupported transformed UV sets");

			writeSource(R"("extensionsUsed":["KHR_materials_clearcoat"],)",
				R"({"extensions":{"KHR_materials_clearcoat":{"clearcoatFactor":1.2}}})");
			const ModelImportResult invalidCoat = ModelImporter::Import(root / "probe.gltf", {});
			context.Check(!invalidCoat.Succeeded() &&
				invalidCoat.m_Error.find("KHR_materials_clearcoat") != std::string::npos,
				"Out-of-range clearcoat factor is rejected visibly");

			writeSource(R"("extensionsUsed":["KHR_materials_anisotropy"],"extensionsRequired":["KHR_materials_anisotropy"],)",
				R"({"extensions":{"KHR_materials_anisotropy":{"anisotropyStrength":0.7,"anisotropyRotation":0.785398,"anisotropyTexture":{"index":0,"texCoord":1,"extensions":{"KHR_texture_transform":{"rotation":0.25}}}}}})");
			const ModelImportResult requiredAnisotropy = ModelImporter::Import(root / "probe.gltf", {});
			bool anisotropyValid = requiredAnisotropy.Succeeded();
			if (anisotropyValid)
			{
				const auto& material = requiredAnisotropy.m_Model.m_Materials.front();
				const auto& binding = material.m_TextureBindings[static_cast<uint32_t>(MaterialTextureSlot::Anisotropy)];
				anisotropyValid =
					std::abs(material.m_Properties.m_AnisotropyStrength - 0.7f) < 0.0001f &&
					std::abs(material.m_Properties.m_AnisotropyRotation - 0.785398f) < 0.0001f &&
					binding.m_TextureIndex != ImportedMaterialTextureBinding::InvalidTextureIndex &&
					binding.m_TextureIndex < requiredAnisotropy.m_Model.m_TextureSources.size() &&
					binding.m_TexCoordIndex == 1 &&
					std::abs(binding.m_UVRotation - 0.25f) < 0.0001f &&
					requiredAnisotropy.m_Model.m_TextureSources[binding.m_TextureIndex].m_Semantic ==
						TextureSemantic::Anisotropy;
			}
			context.Check(anisotropyValid, std::format(
				"Required anisotropy factors, linear texture and UV transform survive import: {}",
				requiredAnisotropy.m_Error));

			writeSource(R"("extensionsUsed":["KHR_materials_anisotropy"],)",
				R"({"extensions":{"KHR_materials_anisotropy":{"anisotropyStrength":0,"anisotropyRotation":1.2}}})");
			const ModelImportResult disabledAnisotropy = ModelImporter::Import(root / "probe.gltf", {});
			context.Check(disabledAnisotropy.Succeeded() &&
				disabledAnisotropy.m_Model.m_Materials.front().m_Properties.m_AnisotropyStrength == 0.0f &&
				std::abs(disabledAnisotropy.m_Model.m_Materials.front().m_Properties.m_AnisotropyRotation - 1.2f) < 0.0001f,
				"Zero anisotropy strength preserves rotation without enabling the lobe");

			// The binding is independent of the lobe factor. Check authored values directly,
			// including a negative rotation, rather than using Assimp's output as a reference.
			for (const std::string_view strength : { std::string_view(R"("anisotropyStrength":0,)"), std::string_view{} })
			{
				const std::string material = std::string(R"({"extensions":{"KHR_materials_anisotropy":{)") +
					std::string(strength) + R"("anisotropyRotation":-2.5,"anisotropyTexture":{"index":1,"texCoord":0,
"extensions":{"KHR_texture_transform":{"texCoord":1,"offset":[0.1,0.2],"scale":[-2,0.5],"rotation":0.25}}}}}})";
				writeSource(R"("extensionsUsed":["KHR_materials_anisotropy","KHR_texture_transform"],"extensionsRequired":["KHR_materials_anisotropy"],)",
					material, 0, -1, coatTextureDeclarations);
				const ModelImportResult texturedAnisotropy = ModelImporter::Import(root / "probe.gltf", {});
				bool bindingValid = texturedAnisotropy.Succeeded() && texturedAnisotropy.m_Model.m_TextureSources.size() == 1u;
				if (bindingValid)
				{
					const auto& properties = texturedAnisotropy.m_Model.m_Materials.front().m_Properties;
					const auto& binding = texturedAnisotropy.m_Model.m_Materials.front().m_TextureBindings[
						static_cast<size_t>(MaterialTextureSlot::Anisotropy)];
					bindingValid = properties.m_AnisotropyStrength == 0.0f && properties.m_AnisotropyRotation == -2.5f &&
						binding.m_TextureIndex == 0u && binding.m_TexCoordIndex == 1u &&
						binding.m_SamplerKey.m_AddressU == RHITextureAddressMode::Clamp &&
						binding.m_SamplerKey.m_AddressV == RHITextureAddressMode::Mirror &&
						binding.m_SamplerKey.m_Filter == RHISamplerFilter::MinMagMipPoint &&
						binding.m_SamplerKey.m_MaxLOD == 0.0f &&
						binding.m_UVOffset.m_X == 0.1f && binding.m_UVOffset.m_Y == 0.2f &&
						binding.m_UVScale.m_X == -2.0f && binding.m_UVScale.m_Y == 0.5f && binding.m_UVRotation == 0.25f &&
						texturedAnisotropy.m_Model.m_TextureSources.front().m_CanonicalPath == utils::Canonical(root / "coat.png") &&
						texturedAnisotropy.m_Model.m_TextureSources.front().m_Semantic == TextureSemantic::Anisotropy;
				}
				context.Check(bindingValid, std::format(
					"{} zero anisotropy retains its authored texture, sampler, UV transform and negative rotation: {}",
					strength.empty() ? "Default" : "Explicit", texturedAnisotropy.m_Error));
			}

			writeSource(R"("extensionsUsed":["KHR_materials_anisotropy"],)",
				R"({"extensions":{"KHR_materials_anisotropy":{"anisotropyStrength":1.2}}})");
			const ModelImportResult invalidAnisotropy = ModelImporter::Import(root / "probe.gltf", {});
			context.Check(!invalidAnisotropy.Succeeded() &&
				invalidAnisotropy.m_Error.find("KHR_materials_anisotropy") != std::string::npos,
				"Out-of-range anisotropy strength is rejected visibly");

			struct InvalidExtensionInput
			{
				std::string_view m_Material;
				std::string_view m_Reason;
			};
			constexpr InvalidExtensionInput invalidExtensionInputs[] = {
				{ R"({"extensions":[]})", "Non-object extension container" },
				{ R"({"extensions":{"KHR_materials_ior":false}})", "Non-object IOR extension" },
				{ R"({"extensions":{"KHR_materials_clearcoat":false}})", "Non-object clearcoat extension" },
				{ R"({"extensions":{"KHR_materials_anisotropy":false}})", "Non-object anisotropy extension" },
				{ R"({"extensions":{"KHR_materials_ior":{"ior":1e100}}})", "IOR exceeding float range" },
				{ R"({"extensions":{"KHR_materials_clearcoat":{"clearcoatRoughnessFactor":-0.1}}})", "Negative clearcoat roughness" },
				{ R"({"extensions":{"KHR_materials_clearcoat":{"clearcoatFactor":1e100}}})", "Clearcoat factor exceeding float range" },
				{ R"({"extensions":{"KHR_materials_anisotropy":{"anisotropyRotation":1e100}}})", "Anisotropy rotation exceeding float range" },
				{ R"({"extensions":{"KHR_materials_anisotropy":{"anisotropyRotation":"invalid"}}})", "Non-numeric anisotropy rotation" },
				{ R"({"extensions":{"KHR_materials_clearcoat":{"clearcoatTexture":false}}})", "Non-object disabled clearcoat texture" },
				{ R"({"extensions":{"KHR_materials_clearcoat":{"clearcoatNormalTexture":{}}}})", "Missing disabled coat normal texture index" },
				{ R"({"extensions":{"KHR_materials_anisotropy":{"anisotropyTexture":false}}})", "Non-object disabled anisotropy texture" },
				{ R"({"extensions":{"KHR_materials_anisotropy":{"anisotropyTexture":{"index":999}}}})", "Invalid disabled anisotropy texture index" },
				{ R"({"extensions":{"KHR_materials_anisotropy":{"anisotropyTexture":{"index":0,"texCoord":2}}}})", "Unsupported disabled anisotropy UV set" },
			};
			for (const auto& input : invalidExtensionInputs)
			{
				writeSource(R"("extensionsUsed":["KHR_materials_ior","KHR_materials_clearcoat","KHR_materials_anisotropy"],)", input.m_Material);
				const ModelImportResult invalid = ModelImporter::Import(root / "probe.gltf", {});
				context.Check(!invalid.Succeeded() && !invalid.m_Error.empty(),
					std::format("{} is rejected visibly: {}", input.m_Reason, invalid.m_Error));
			}

			writeSource(R"("extensionsUsed":["KHR_materials_sheen"],"extensionsRequired":["KHR_materials_sheen"],)",
				R"({"extensions":{"KHR_materials_sheen":{"sheenColorFactor":[0.2,0.3,0.4]}}})");
			const ModelImportResult requiredDeferred = ModelImporter::Import(root / "probe.gltf", {});
			context.Check(!requiredDeferred.Succeeded() &&
				requiredDeferred.m_Error.find("Required material extension 'KHR_materials_sheen'") != std::string::npos,
				"A required deferred material extension is rejected visibly");

			writeSource(R"("extensionsUsed":["KHR_materials_sheen"],)",
				R"({"pbrMetallicRoughness":{"baseColorFactor":[0.4,0.5,0.6,1],"roughnessFactor":0.3},"extensions":{"KHR_materials_sheen":{"sheenColorFactor":[0.2,0.3,0.4],"sheenColorTexture":{"index":0},"sheenRoughnessTexture":{"index":0}}}})");
			const ModelImportResult optionalDeferred = ModelImporter::Import(root / "probe.gltf", {});
			context.Check(optionalDeferred.Succeeded() &&
				std::abs(optionalDeferred.m_Model.m_Materials.front().m_Properties.m_BaseColor.m_R - 0.4f) < 0.0001f &&
				std::abs(optionalDeferred.m_Model.m_Materials.front().m_Properties.m_RoughnessFactor - 0.3f) < 0.0001f &&
				optionalDeferred.m_Model.m_TextureSources.empty(),
				"An optional deferred extension preserves core material factors and adds no texture dependencies");

			writeSource(R"("extensionsUsed":["KHR_materials_ior","KHR_materials_clearcoat","KHR_materials_anisotropy"],)",
				R"({"extensions":{"KHR_materials_ior":{"ior":1.7},"KHR_materials_clearcoat":{"clearcoatFactor":0.6,"clearcoatRoughnessFactor":0.25,"clearcoatTexture":{"index":0},"clearcoatRoughnessTexture":{"index":0},"clearcoatNormalTexture":{"index":0,"scale":0.8}},"KHR_materials_anisotropy":{"anisotropyStrength":0.75,"anisotropyRotation":0.4,"anisotropyTexture":{"index":0}}}})");
			Assimp::Importer assimp;
			// Qualify the pinned dependency separately from source-authoritative imports.
			const aiScene* extensionScene = assimp.ReadFile((root / "probe.gltf").string(), 0);
			bool extensionFactors = extensionScene && extensionScene->mNumMaterials > 0;
			bool extensionTextures = extensionFactors;
			if (extensionFactors)
			{
				const aiMaterial* source = extensionScene->mMaterials[0];
				float ior = 0.0f;
				float coat = 0.0f;
				float coatRoughness = 0.0f;
				float anisotropy = 0.0f;
				float rotation = 0.0f;
				float coatNormalScale = 0.0f;
				extensionFactors = source->Get(AI_MATKEY_REFRACTI, ior) == aiReturn_SUCCESS &&
					source->Get(AI_MATKEY_CLEARCOAT_FACTOR, coat) == aiReturn_SUCCESS &&
					source->Get(AI_MATKEY_CLEARCOAT_ROUGHNESS_FACTOR, coatRoughness) == aiReturn_SUCCESS &&
					source->Get(AI_MATKEY_ANISOTROPY_FACTOR, anisotropy) == aiReturn_SUCCESS &&
					source->Get(AI_MATKEY_ANISOTROPY_ROTATION, rotation) == aiReturn_SUCCESS &&
					std::abs(ior - 1.7f) < 0.0001f && std::abs(coat - 0.6f) < 0.0001f &&
					std::abs(coatRoughness - 0.25f) < 0.0001f &&
					std::abs(anisotropy - 0.75f) < 0.0001f &&
					std::abs(rotation - 0.4f) < 0.0001f;
				extensionTextures = source->GetTextureCount(aiTextureType_CLEARCOAT) == 3 &&
					source->GetTextureCount(aiTextureType_ANISOTROPY) == 1 &&
					source->Get(AI_MATKEY_GLTF_TEXTURE_SCALE(aiTextureType_CLEARCOAT, 2),
						coatNormalScale) == aiReturn_SUCCESS &&
					std::abs(coatNormalScale - 0.8f) < 0.0001f;
			}
			context.Check(extensionFactors,
				"Pinned Assimp exposes IOR, clearcoat and anisotropy factors");
			context.Check(extensionTextures,
				"Pinned Assimp exposes targeted lobe texture slots and coat normal scale");
			const ModelImportResult combined = ModelImporter::Import(root / "probe.gltf", {});
			bool combinedValid = combined.Succeeded();
			if (combinedValid)
			{
				const auto& properties = combined.m_Model.m_Materials.front().m_Properties;
				combinedValid = std::abs(properties.m_ClearcoatFactor - 0.6f) < 0.0001f &&
					std::abs(properties.m_AnisotropyStrength - 0.75f) < 0.0001f &&
					combined.m_Model.m_TextureSources.size() == 3u;
			}
			context.Check(combinedValid, std::format(
				"Clearcoat and anisotropy share one imported material: {}",
				combined.m_Error));

			std::filesystem::remove(root / "probe.gltf", errorCode);
			std::filesystem::remove(root / "probe.bin", errorCode);
			std::filesystem::remove(root, errorCode);
		}

		void RunGltfTextureSourceTests(SelfTestContext& context) noexcept
		{
			std::error_code errorCode;
			const auto root = std::filesystem::temp_directory_path(errorCode) /
				std::format("gglab-texture-source-import-{}-{}", GetCurrentProcessId(), GetTickCount64());
			const bool created = !errorCode && std::filesystem::create_directory(root, errorCode);
			context.Check(created && !errorCode, "Texture source import creates an isolated glTF fixture directory");
			if (!created || errorCode) return;

			const std::array<float, 24> vertices = {
				0, 0, 0, 1, 0, 0, 0, 1, 0,
				0, 0, 1, 0, 0, 1, 0, 0, 1,
				0, 0, 1, 0, 0, 1,
			};
			{
				std::ofstream buffer(root / "probe.bin", std::ios::binary);
				buffer.write(reinterpret_cast<const char*>(vertices.data()), sizeof(vertices));
			}
			constexpr uint32_t UniqueTextureCount = 32;
			constexpr uint32_t MaterialCount = UniqueTextureCount * 2;
			const auto writeSource = [&](std::string_view prefix) noexcept
			{
				std::ofstream gltf(root / "probe.gltf");
				gltf << R"({"asset":{"version":"2.0"},"extensionsUsed":["KHR_materials_clearcoat","KHR_texture_transform"],
"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],
"buffers":[{"uri":"probe.bin","byteLength":96}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":24}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"}],
"samplers":[{"wrapS":10497,"wrapT":10497},{"wrapS":33071,"wrapT":33071}],"images":[)";
				for (uint32_t index = 0; index < UniqueTextureCount; ++index)
				{
					if (index != 0) gltf << ',';
					gltf << "{\"uri\":\"" << prefix << '-' << index << ".png\"}";
				}
				gltf << ",{\"uri\":\"./" << prefix << R"(-0.png"}],"textures":[)";
				for (uint32_t index = 0; index < UniqueTextureCount; ++index)
				{
					if (index != 0) gltf << ',';
					gltf << "{\"source\":" << index << ",\"sampler\":0}";
				}
				gltf << ",{\"source\":" << UniqueTextureCount << R"(,"sampler":1}],"materials":[)";
				for (uint32_t index = 0; index < MaterialCount; ++index)
				{
					if (index != 0) gltf << ',';
					const uint32_t textureIndex = index == UniqueTextureCount ? UniqueTextureCount : index % UniqueTextureCount;
					gltf << "{\"name\":\"Material-" << index << R"(","pbrMetallicRoughness":{"baseColorTexture":{"index":)" << textureIndex;
					if (index == UniqueTextureCount)
						gltf << R"(,"extensions":{"KHR_texture_transform":{"offset":[0.25,0.5]}})";
					gltf << R"(}},"normalTexture":{"index":0},"extensions":{"KHR_materials_clearcoat":{"clearcoatFactor":0.5,"clearcoatTexture":{"index":0},"clearcoatRoughnessTexture":{"index":0}}}})";
				}
				gltf << R"(],"meshes":[{"primitives":[)";
				for (uint32_t index = 0; index < MaterialCount; ++index)
				{
					if (index != 0) gltf << ',';
					gltf << R"({"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"material":)" << index << '}';
				}
				gltf << R"(]}]})";
			};
			const auto hasStableBindings = [](const ImportedModel& model) noexcept
			{
				if (model.m_Materials.size() < MaterialCount || model.m_TextureSources.size() != UniqueTextureCount + 2u)
					return false;
				// The first material adds base color, normal and coat in slot order.
				// Later materials add only their previously unseen base-color sources.
				for (uint32_t index = 0; index < MaterialCount; ++index)
				{
					const auto& bindings = model.m_Materials[index].m_TextureBindings;
					const uint32_t textureIndex = index % UniqueTextureCount;
					const uint32_t expectedBase = textureIndex == 0 ? 0 : textureIndex + 2u;
					if (bindings[static_cast<size_t>(MaterialTextureSlot::BaseColor)].m_TextureIndex != expectedBase ||
						bindings[static_cast<size_t>(MaterialTextureSlot::Normal)].m_TextureIndex != 1u ||
						bindings[static_cast<size_t>(MaterialTextureSlot::Clearcoat)].m_TextureIndex != 2u ||
						bindings[static_cast<size_t>(MaterialTextureSlot::ClearcoatRoughness)].m_TextureIndex != 2u)
						return false;
				}
				return true;
			};

			writeSource("map");
			const ModelImportResult imported = ModelImporter::Import(root / "probe.gltf", {});
			context.Check(imported.Succeeded() && hasStableBindings(imported.m_Model), std::format(
				"Repeated core and extension textures retain first-use indices across source-table growth: {}", imported.m_Error));
			if (imported.Succeeded() && hasStableBindings(imported.m_Model))
			{
				const auto& model = imported.m_Model;
				bool sourceOrder = model.m_TextureSources[0].m_Semantic == TextureSemantic::BaseColor &&
					model.m_TextureSources[1].m_Semantic == TextureSemantic::Normal &&
					model.m_TextureSources[2].m_Semantic == TextureSemantic::Clearcoat;
				for (uint32_t index = 0; index < UniqueTextureCount; ++index)
				{
					const uint32_t sourceIndex = index == 0 ? 0 : index + 2u;
					sourceOrder &= model.m_TextureSources[sourceIndex].m_CanonicalPath ==
						utils::Canonical(root / std::format("map-{}.png", index));
				}
				context.Check(sourceOrder, "Texture source vector order follows first use rather than hash-table order");
				const auto& original = model.m_Materials[0].m_TextureBindings[static_cast<size_t>(MaterialTextureSlot::BaseColor)];
				const auto& alias = model.m_Materials[UniqueTextureCount].m_TextureBindings[static_cast<size_t>(MaterialTextureSlot::BaseColor)];
				context.Check(original.m_TextureIndex == alias.m_TextureIndex && original.m_SamplerKey != alias.m_SamplerKey &&
					alias.m_UVOffset.m_X == 0.25f && alias.m_UVOffset.m_Y == 0.5f,
					"Canonical URI aliases share a texture source while sampler and UV transform remain per binding");
			}
			writeSource("replacement");
			const ModelImportResult replacement = ModelImporter::Import(root / "probe.gltf", {});
			context.Check(replacement.Succeeded() && hasStableBindings(replacement.m_Model) &&
				replacement.m_Model.m_TextureSources[0].m_CanonicalPath == utils::Canonical(root / "replacement-0.png"),
				"A subsequent model import owns a fresh source index with unchanged first-use ordering");
			writeSource("map");
			const ModelImportResult repeated = ModelImporter::Import(root / "probe.gltf", {});
			context.Check(repeated.Succeeded() && hasStableBindings(repeated.m_Model) &&
				repeated.m_Model.m_TextureSources[0].m_CanonicalPath == utils::Canonical(root / "map-0.png"),
				"Reimporting earlier texture paths rebuilds the model-local source vector");

			std::filesystem::remove(root / "probe.gltf", errorCode);
			std::filesystem::remove(root / "probe.bin", errorCode);
			std::filesystem::remove(root, errorCode);
		}

		void RunMaterialUVTransformTests(SelfTestContext& context) noexcept
		{
			context.Check(SanitizeMaterialIor(DefaultDielectricIor) == 1.5f &&
				SanitizeMaterialIor(0.0f) == 0.0f &&
				SanitizeMaterialIor(1.0f) == 1.0f &&
				SanitizeMaterialIor(2.4f) == 2.4f &&
				SanitizeMaterialIor(0.9f) == DefaultDielectricIor &&
				SanitizeMaterialIor(std::numeric_limits<float>::infinity()) ==
					DefaultDielectricIor &&
				SanitizeMaterialIor(std::numeric_limits<float>::quiet_NaN()) ==
					DefaultDielectricIor,
				"Runtime material IOR preserves valid values and sanitizes invalid inputs");

			const MaterialTextureBinding identity{};
			const MaterialUVTransformRows identityRows = EncodeMaterialUVTransform(identity);
			context.Check(identityRows.m_U.m_X == 1.0f && identityRows.m_U.m_Y == 0.0f &&
				identityRows.m_U.m_Z == 0.0f && identityRows.m_V.m_X == 0.0f &&
				identityRows.m_V.m_Y == 1.0f && identityRows.m_V.m_Z == 0.0f,
				"Default material UV transform preserves authored texture coordinates");

			MaterialTextureBinding binding{};
			binding.m_UVOffset = Vector2(0.1f, 0.2f);
			binding.m_UVScale = Vector2(2.0f, 3.0f);
			binding.m_UVRotation = std::numbers::pi_v<float> * 0.5f;
			const MaterialUVTransformRows rows = EncodeMaterialUVTransform(binding);
			const Vector2 uv(0.25f, 0.75f);
			const Vector2 transformed(rows.m_U.m_X * uv.m_X + rows.m_U.m_Y * uv.m_Y +
				rows.m_U.m_Z, rows.m_V.m_X * uv.m_X + rows.m_V.m_Y * uv.m_Y + rows.m_V.m_Z);
			context.Check(std::abs(transformed.m_X + 2.15f) < 0.00001f &&
				std::abs(transformed.m_Y - 0.7f) < 0.00001f,
				"GPU UV rows implement glTF offset + rotation * scale * selected UV");
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

			auto materialFixture = []() noexcept
			{
				ImportedModel model = MakeModelImportFixture();
				model.m_Materials.emplace_back();
				model.m_Materials.front().m_TextureBindings[0].m_TextureIndex = 0;
				return model;
			};
			ModelImportArtifactHandle materialBaseline = CreateModelImportArtifact(
				materialFixture(), MakeResolvedModelTextureFixture(), textureCache);
			bool materialDigestValid = materialBaseline && materialBaseline->IsValid();
			for (uint32_t variant = 0; variant < 14; ++variant)
			{
				ImportedModel changed = materialFixture();
				auto& binding = changed.m_Materials.front().m_TextureBindings[0];
				switch (variant)
				{
				case 0: binding.m_UVOffset.m_X = 0.25f; break;
				case 1: binding.m_UVScale.m_Y = 2.0f; break;
				case 2: binding.m_UVRotation = 0.5f; break;
				case 3: binding.m_TexCoordIndex = 1; break;
				case 4: changed.m_Materials.front().m_Properties.m_NormalScale = 0.5f; break;
				case 5: changed.m_Materials.front().m_Properties.m_OcclusionStrength = 0.5f; break;
				case 6: changed.m_Materials.front().m_Properties.m_Ior = 1.7f; break;
				case 7: changed.m_Materials.front().m_Properties.m_ClearcoatFactor = 0.8f; break;
				case 8: changed.m_Materials.front().m_Properties.m_ClearcoatRoughness = 0.2f; break;
				case 9: changed.m_Materials.front().m_Properties.m_ClearcoatNormalScale = 0.5f; break;
				case 10: changed.m_Materials.front().m_Properties.m_ClearcoatNormalBinding.m_UVOffset.m_X = 0.1f; break;
				case 11: changed.m_Materials.front().m_Properties.m_AnisotropyStrength = 0.7f; break;
				case 12: changed.m_Materials.front().m_Properties.m_AnisotropyRotation = 0.4f; break;
				case 13: changed.m_Materials.front().m_Properties.m_AnisotropyBinding.m_UVScale.m_Y = 0.5f; break;
				default: break;
				}
				const ModelImportArtifactHandle changedArtifact = CreateModelImportArtifact(
				std::move(changed), MakeResolvedModelTextureFixture(), textureCache);
				materialDigestValid &= changedArtifact && materialBaseline &&
					changedArtifact->m_ContentDigest != materialBaseline->m_ContentDigest;
			}
			context.Check(materialDigestValid,
				"Material artifact digest tracks UV transforms, lobe factors and IOR");
			materialBaseline.reset();

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

		void RunIBLCacheControlTests(SelfTestContext& context) noexcept
		{
			IBLDerivedDataSystem disabled({});
			IBLCacheControlBase& disabledControl = disabled;
			disabledControl.ClearArtifactCache();
			context.Check(disabledControl.ClearDerivedDataStore() &&
				disabled.GetArtifactCacheStatistics().m_CachedEntryCount == 0,
				"IBL cache controls accept an empty CPU cache and a disabled DDC");

			std::error_code errorCode;
			const auto temporaryRoot = std::filesystem::temp_directory_path(errorCode);
			if (errorCode)
			{
				context.Check(false, "IBL cache control tests resolve a temporary directory");
				return;
			}
			const auto root = temporaryRoot / std::format("gglab.ibl-cache-control.{}.{}",
				GetCurrentProcessId(), std::chrono::steady_clock::now().time_since_epoch().count());
			{
				IBLDerivedDataSystem system({ .m_CacheDirectory = root / "cache" });
				IBLCacheControlBase& control = system;
				const auto artifact = CreateIBLStageArtifact(
					IBLArtifactStage::BrdfLut, MakeTextureFixture());
				DerivedDataKey key{};
				key.m_Value[0] = std::byte{ 1 };
				const auto retained = system.Admit(key, artifact);
				const bool stored = system.Store(key, artifact);
				context.Check(retained && stored &&
					system.GetArtifactCacheStatistics().m_CachedEntryCount == 1 &&
					system.GetStoreStatistics().m_StoredEntryCount == 1,
					"IBL cache control fixture populates the real CPU cache and DDC");

				control.ClearArtifactCache();
				context.Check(system.GetArtifactCacheStatistics().m_CachedEntryCount == 0 &&
					system.GetArtifactCacheStatistics().m_CachedBytes == 0 &&
					system.GetStoreStatistics().m_StoredEntryCount == 1 &&
					retained && retained->IsValid() && retained->m_Texture.m_Pixels[0] == std::byte{ 0x10 },
					"IBL CPU clear preserves retained artifacts and the persistent DDC");

				const auto readmitted = system.Admit(key, retained);
				const bool cleared = control.ClearDerivedDataStore();
				LocalDerivedDataStore observer(root / "cache");
				const auto read = observer.Read(key, GetIBLStageArtifactType(IBLArtifactStage::BrdfLut),
					IBLStageArtifactSchemaVersion);
				context.Check(cleared && readmitted &&
					system.GetArtifactCacheStatistics().m_CachedEntryCount == 1 &&
					system.GetStoreStatistics().m_StoredEntryCount == 0 &&
					read.m_Disposition == DerivedDataReadDisposition::Miss && retained->IsValid(),
					"IBL DDC clear removes stored entries without clearing CPU or retained artifacts");
				context.Check(system.Store(key, retained) &&
					system.GetStoreStatistics().m_StoredEntryCount == 1,
					"Later IBL bake work can repopulate the DDC after maintenance");
				control.ClearArtifactCache();
				control.ClearArtifactCache();
				const bool firstClear = control.ClearDerivedDataStore();
				const bool secondClear = control.ClearDerivedDataStore();
				context.Check(firstClear && secondClear &&
					system.GetArtifactCacheStatistics().m_CachedEntryCount == 0 &&
					system.GetStoreStatistics().m_StoredEntryCount == 0,
					"Repeated IBL cache maintenance remains idempotent");
			}
			{
				const auto blockedParent = root / "blocked";
				std::ofstream blocker(blockedParent, std::ios::binary);
				blocker << "not a directory";
				const bool created = blocker.good();
				blocker.close();
				IBLDerivedDataSystem blocked({ .m_CacheDirectory = blockedParent / "cache" });
				IBLCacheControlBase& control = blocked;
				context.Check(created && !control.ClearDerivedDataStore(),
					"IBL cache control preserves the DDC maintenance failure result");
			}
			std::filesystem::remove_all(root, errorCode);
		}

		void RunIBLDerivedDataShaderIdentityTests(SelfTestContext& context) noexcept
		{
			const std::filesystem::path cacheDirectory =
				std::filesystem::temp_directory_path() / "gglab-ibl-key-self-test";
			IBLDerivedDataSystem system({
				.m_CacheDirectory = cacheDirectory,
				.m_Compatibility = IBLArtifactCompatibility::Portable,
			});

			IBLShaderArtifactIdentities identities{};
			const std::array<uint32_t, static_cast<size_t>(IBLArtifactStage::Count)>
				artifactCounts{ 4, 2, 4, 2 };
			uint8_t marker = 1;
			for (size_t stageIndex = 0; stageIndex < identities.size(); ++stageIndex)
			{
				auto& identity = identities[stageIndex];
				identity.m_Count = artifactCounts[stageIndex];
				for (size_t artifactIndex = 0; artifactIndex < identity.m_Count; ++artifactIndex)
				{
					identity.m_ArtifactRefs[artifactIndex]
						.m_ArtifactId.m_DurableDigest.m_Value[0] = static_cast<std::byte>(marker++);
				}
			}

			const AssetContentFingerprint source{ 1, 2, 3 };
			const IBLBakeConfig config{};
			const IBLDerivedDataLookupResult baseline = system.Lookup(source,
				EnvironmentTextureSourceType::Equirectangular, config, identities, true);

			IBLShaderArtifactIdentities irradianceChanged = identities;
			irradianceChanged[static_cast<size_t>(IBLArtifactStage::Irradiance)]
				.m_ArtifactRefs[0].m_ArtifactId.m_DurableDigest.m_Value[1] = std::byte{ 1 };
			const IBLDerivedDataLookupResult changedIrradiance = system.Lookup(source,
				EnvironmentTextureSourceType::Equirectangular, config, irradianceChanged, true);
			context.Check(
				baseline.Get(IBLArtifactStage::Environment).m_Key ==
					changedIrradiance.Get(IBLArtifactStage::Environment).m_Key &&
				baseline.Get(IBLArtifactStage::Irradiance).m_Key !=
					changedIrradiance.Get(IBLArtifactStage::Irradiance).m_Key &&
				baseline.Get(IBLArtifactStage::PrefilteredSpecular).m_Key ==
					changedIrradiance.Get(IBLArtifactStage::PrefilteredSpecular).m_Key &&
				baseline.Get(IBLArtifactStage::BrdfLut).m_Key ==
					changedIrradiance.Get(IBLArtifactStage::BrdfLut).m_Key,
				"An IBL stage shader change invalidates only that stage and its data dependencies");

			IBLShaderArtifactIdentities importanceChanged = identities;
			importanceChanged[static_cast<size_t>(IBLArtifactStage::PrefilteredSpecular)]
				.m_ArtifactRefs[3].m_ArtifactId.m_DurableDigest.m_Value[1] = std::byte{ 1 };
			const auto changedImportance = system.Lookup(source,
				EnvironmentTextureSourceType::Equirectangular, config, importanceChanged, true);
			context.Check(
				baseline.Get(IBLArtifactStage::PrefilteredSpecular).m_Key != changedImportance.Get(IBLArtifactStage::PrefilteredSpecular).m_Key &&
				baseline.Get(IBLArtifactStage::Environment).m_Key == changedImportance.Get(IBLArtifactStage::Environment).m_Key &&
				baseline.Get(IBLArtifactStage::Irradiance).m_Key == changedImportance.Get(IBLArtifactStage::Irradiance).m_Key &&
				baseline.Get(IBLArtifactStage::BrdfLut).m_Key == changedImportance.Get(IBLArtifactStage::BrdfLut).m_Key,
				"Environment importance producer changes invalidate the specular stage without invalidating unrelated producers");

			IBLShaderArtifactIdentities environmentChanged = identities;
			environmentChanged[static_cast<size_t>(IBLArtifactStage::Environment)]
				.m_ArtifactRefs[0].m_ArtifactId.m_DurableDigest.m_Value[1] = std::byte{ 1 };
			const IBLDerivedDataLookupResult changedEnvironment = system.Lookup(source,
				EnvironmentTextureSourceType::Equirectangular, config, environmentChanged, true);
			context.Check(
				baseline.Get(IBLArtifactStage::Environment).m_Key !=
					changedEnvironment.Get(IBLArtifactStage::Environment).m_Key &&
				baseline.Get(IBLArtifactStage::Irradiance).m_Key !=
					changedEnvironment.Get(IBLArtifactStage::Irradiance).m_Key &&
				baseline.Get(IBLArtifactStage::PrefilteredSpecular).m_Key !=
					changedEnvironment.Get(IBLArtifactStage::PrefilteredSpecular).m_Key &&
				baseline.Get(IBLArtifactStage::BrdfLut).m_Key ==
					changedEnvironment.Get(IBLArtifactStage::BrdfLut).m_Key,
				"Environment shader identity propagates only through dependent IBL stage keys");
		}

		void RunAssetPathTests(SelfTestContext& context) noexcept
		{
			const std::filesystem::path assetRoot =
				std::filesystem::temp_directory_path() / "gglab-asset-path-self-test" / "Assets";
			const std::filesystem::path expected =
				utils::Canonical(assetRoot / "Textures" / "UVTest1K.png");
			context.Check(ResolveAssetPath(
				assetRoot, "Assets/Textures/UVTest1K.png") == expected,
				"Injected asset root preserves the legacy Assets logical prefix");
			context.Check(ResolveAssetPath(assetRoot, "Textures/UVTest1K.png") == expected,
				"Injected asset root resolves root-relative content identities");
			const std::filesystem::path external =
				utils::Canonical(std::filesystem::temp_directory_path() / "external-texture.png");
			context.Check(ResolveAssetPath(assetRoot, external) == external,
				"Explicit absolute asset paths remain externally addressable");
			context.Check(ResolveAssetPath(assetRoot, "../outside.png").empty(),
				"Relative asset paths cannot escape the injected root");
		}
	}

	void RunAssetDataSelfTests(SelfTestContext& context) noexcept
	{
		RunSha256Tests(context);
		RunDerivedDataKeyTests(context);
		RunTextureSourceKeyTests(context);
		RunTextureCodecTests(context);
		RunTextureStructureValidationTests(context);
		RunLocalDerivedDataStoreTests(context);
		RunLocalDerivedDataMaintenanceTests(context);
		RunModelImportArtifactTests(context);
		RunGltfTangentImportTests(context);
		RunGltfMaterialIdentityTests(context);
		RunGltfMaterialImportTests(context);
		RunGltfTextureSourceTests(context);
		RunMaterialUVTransformTests(context);
		RunRHITextureValidationTests(context);
		RunIBLDerivedDataShaderIdentityTests(context);
		RunIBLCacheControlTests(context);
		RunAssetPathTests(context);
	}
}
