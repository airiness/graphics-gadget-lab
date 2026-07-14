#include "Core/Precompiled.h"
#include "Graphics/TextureRegistry.h"
#include "Graphics/AssetUploadScheduler.h"
#include "Graphics/TransferManager.h"
#include "Graphics/RHI/RHIDevice.h"
#include "Graphics/TextureLoader.h"
#include "Graphics/Utility/CubemapUtils.h"
#include "Graphics/Utility/TextureUtils.h"
#include "Core/Task/TaskSystem.h"
#include "Core/Utility/PathUtils.h"
#include "Core/Utility/TypeUtils.h"

#include <cmath>

namespace gglab
{
	namespace
	{
		struct TextureLoadJob
		{
			TextureAssetData m_TextureData;
		};

		[[nodiscard]] constexpr std::string_view TextureSemanticDebugText(
			TextureSemantic semantic) noexcept
		{
			switch (semantic)
			{
			case TextureSemantic::Unknown: return "Unknown";
			case TextureSemantic::BaseColor: return "BaseColor";
			case TextureSemantic::MetallicRoughness: return "MetallicRoughness";
			case TextureSemantic::Normal: return "Normal";
			case TextureSemantic::Occlusion: return "Occlusion";
			case TextureSemantic::Emissive: return "Emissive";
			case TextureSemantic::Environment: return "Environment";
			case TextureSemantic::UVTest: return "UVTest";
			case TextureSemantic::GenericColor: return "GenericColor";
			case TextureSemantic::GenericData: return "GenericData";
			}
			return "Unknown";
		}

		[[nodiscard]] std::string TextureDebugSourcePath(
			const std::filesystem::path& sourcePath)
		{
			if (sourcePath.empty())
			{
				return {};
			}

			const std::string normalized = sourcePath.generic_string();
			const size_t assetsOffset = normalized.find("Assets/");
			if (assetsOffset != std::string::npos)
			{
				return normalized.substr(assetsOffset);
			}

			return std::format(
				"External/{}#{:08X}",
				sourcePath.filename().generic_string(),
				static_cast<uint32_t>(Crc64(normalized)));
		}

		[[nodiscard]] Vector3 EvaluateProceduralEnvironment(const Vector3& direction) noexcept
		{
			const float t = std::clamp(direction.m_Y * 0.5f + 0.5f, 0.0f, 1.0f);
			const Vector3 ground(0.04f, 0.035f, 0.03f);
			const Vector3 skyHorizon(0.45f, 0.55f, 0.75f);
			const Vector3 skyZenith(0.08f, 0.18f, 0.45f);
			const Vector3 sky = Lerp(skyHorizon, skyZenith, std::pow(t, 1.5f));
			Vector3 color = Lerp(ground, sky, t);

			const Vector3 sunDirection = Vector3(0.2f, 0.8f, 0.3f).Normalized();
			const float sun = std::pow(std::max(direction.Dot(sunDirection), 0.0f), 512.0f);
			color += sun * Vector3(8.0f, 6.5f, 4.0f);
			return color;
		}

		[[nodiscard]] TextureAssetData MakeProceduralEnvironmentCubemap() noexcept
		{
			constexpr uint32_t faceSize = 16;
			std::vector<float> pixels;
			pixels.reserve(static_cast<size_t>(faceSize) * faceSize * CubemapFaceCount * 4);

			for (uint32_t faceIndex = 0; faceIndex < CubemapFaceCount; ++faceIndex)
			{
				const auto face = static_cast<CubemapFace>(faceIndex);
				for (uint32_t y = 0; y < faceSize; ++y)
				{
					for (uint32_t x = 0; x < faceSize; ++x)
					{
						const float u = (static_cast<float>(x) + 0.5f) / faceSize;
						const float v = (static_cast<float>(y) + 0.5f) / faceSize;
						const Vector3 direction = CubemapFaceUvToDirection(face, Vector2(u, v));
						const Vector3 color = EvaluateProceduralEnvironment(direction);
						pixels.insert(pixels.end(), { color.m_X, color.m_Y, color.m_Z, 1.0f });
					}
				}
			}

			return TextureLoader::MakeTextureCubeRgba16Float(faceSize, pixels);
		}
	}

	TextureRegistry::TextureRegistry(const CreateInfo& createInfo) noexcept :
		m_Device(createInfo.m_Device),
		m_TaskSystem(createInfo.m_TaskSystem),
		m_TransferManager(createInfo.m_TransferManager),
		m_AssetUploadScheduler(createInfo.m_AssetUploadScheduler)
	{
		GGLAB_ASSERT_MSG(m_Device != nullptr, "RHIDevice is null!");
		GGLAB_ASSERT_MSG(m_TaskSystem != nullptr, "TaskSystem is null!");
		GGLAB_ASSERT_MSG(m_TransferManager != nullptr, "TransferManager is null!");
		GGLAB_ASSERT_MSG(m_AssetUploadScheduler != nullptr, "AssetUploadScheduler is null!");
	}

	void TextureRegistry::InitializeReservedTextures() noexcept
	{
		const auto makeTextureData = [](uint32_t width, uint32_t height, TextureColorSpace colorSpace, auto&& pixelFunc) -> TextureAssetData
			{
				GGLAB_ASSERT(width > 0 && height > 0);
				constexpr size_t formatBytes = 4;
				std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * formatBytes);

				for (uint32_t y = 0; y < height; ++y)
				{
					for (uint32_t x = 0; x < width; ++x)
					{
						const auto color = pixelFunc(x, y);
						uint8_t* pixel = pixels.data() + (static_cast<size_t>(y) * width + x) * formatBytes;
						pixel[0] = color[0];
						pixel[1] = color[1];
						pixel[2] = color[2];
						pixel[3] = color[3];
					}
				}
				return TextureLoader::MakeTexture2DRgba8(width, height, pixels, colorSpace);
			};

		const auto makeUploadData = [&, this](ReservedTextureIDIndex idIndex,
			const char* texName,
			TextureSemantic semantic,
			TextureAssetData&& textureData)
			{
				const auto& id = ToTextureId(idIndex);
				CreateTextureEntry(id, texName);

				return MakeTextureUploadData(id, std::move(textureData), semantic);
			};

		const auto makeGeneratedUploadData = [&](ReservedTextureIDIndex idIndex,
			const char* texName,
			TextureSemantic semantic,
			uint32_t width,
			uint32_t height,
			auto&& pixelFunc)
			{
				return makeUploadData(
					idIndex,
					texName,
					semantic,
					makeTextureData(width, height, GetTextureColorSpaceFromSemantic(semantic), pixelFunc));
			};

		std::vector<TextureUploadData> uploads;
		uploads.reserve(utils::ToIndex(ReservedTextureIDIndex::Count));

		uploads.emplace_back(makeGeneratedUploadData(
			ReservedTextureIDIndex::BaseColorWhite,
			"BaseColorWhite",
			TextureSemantic::BaseColor,
			1,
			1,
			[](uint32_t, uint32_t) -> std::array<uint8_t, 4>
			{
				return { 255, 255, 255, 255 };
			}));

		uploads.emplace_back(makeGeneratedUploadData(
			ReservedTextureIDIndex::MissingTextureChecker,
			"MissingTextureChecker",
			TextureSemantic::BaseColor,
			64,
			64,
			[](uint32_t x, uint32_t y) -> std::array<uint8_t, 4>
			{
				const uint32_t tile = 8;
				const bool isPurple = ((x / tile) + (y / tile)) & 1;
				return isPurple ?
					std::array<uint8_t, 4>{ 255, 0, 255, 255 } :
					std::array<uint8_t, 4>{ 0, 0, 0, 255 };
			}));

		uploads.emplace_back(makeGeneratedUploadData(
			ReservedTextureIDIndex::NormalFlat,
			"NormalFlat",
			TextureSemantic::Normal,
			1,
			1,
			[](uint32_t, uint32_t) -> std::array<uint8_t, 4>
			{
				return { 128, 128, 255, 255 };
			}));

		uploads.emplace_back(makeGeneratedUploadData(
			ReservedTextureIDIndex::DefaultMetallicRoughness,
			"DefaultMetallicRoughness",
			TextureSemantic::MetallicRoughness,
			1,
			1,
			[](uint32_t, uint32_t) -> std::array<uint8_t, 4>
			{
				// The shader multiplies sampled values by material factors, so an
				// absent metallic-roughness texture must use the multiplicative identity.
				return { 0, 255, 255, 255 };
			}));

		uploads.emplace_back(makeGeneratedUploadData(
			ReservedTextureIDIndex::OcclusionWhite,
			"OcclusionWhite",
			TextureSemantic::Occlusion,
			1,
			1,
			[](uint32_t, uint32_t) -> std::array<uint8_t, 4>
			{
				return { 255, 255, 255, 255 };
			}));

		uploads.emplace_back(makeGeneratedUploadData(
			ReservedTextureIDIndex::EmissiveBlack,
			"EmissiveBlack",
			TextureSemantic::Emissive,
			1,
			1,
			[](uint32_t, uint32_t) -> std::array<uint8_t, 4>
			{
				return { 0, 0, 0, 255 };
			}));

		uploads.emplace_back(makeGeneratedUploadData(
			ReservedTextureIDIndex::ErrorRed,
			"ErrorRed",
			TextureSemantic::BaseColor,
			1,
			1,
			[](uint32_t, uint32_t) -> std::array<uint8_t, 4>
			{
				return { 255, 0, 0, 255 };
			}));

		uploads.emplace_back(makeGeneratedUploadData(
			ReservedTextureIDIndex::UVTest,
			"UVTest",
			TextureSemantic::UVTest,
			256,
			256,
			[](uint32_t x, uint32_t y) -> std::array<uint8_t, 4>
			{
				uint8_t r = static_cast<uint8_t>(x);
				uint8_t g = static_cast<uint8_t>(y);
				uint8_t b = 0;

				const uint32_t grid = 32;
				if ((grid != 0) && ((x % grid) == 0 || (y % grid) == 0))
				{
					r = g = b = 0;
				}

				return { r, g, b, 255 };
			}));

		uploads.emplace_back(makeUploadData(
			ReservedTextureIDIndex::FallbackEnvironmentCubemap,
			"FallbackEnvironmentCubemap",
			TextureSemantic::Environment,
			MakeProceduralEnvironmentCubemap()));

		if (!uploads.empty())
		{
			// Fallback descriptors must exist before the first frame can resolve any
			// material binding, so this bootstrap upload intentionally remains synchronous.
			auto batch = m_TransferManager->BeginBatch();
			bool uploadsSucceeded = true;
			std::vector<TextureID> uploadedTextureIds;
			uploadedTextureIds.reserve(uploads.size());
			for (const auto& data : uploads)
			{
				const bool uploaded = UploadTexture(data, batch);
				uploadsSucceeded &= uploaded;
				if (uploaded)
				{
					uploadedTextureIds.push_back(data.m_TextureId);
				}
			}
			const RHIFencePoint uploadFence = batch.Submit(true);
			uploadsSucceeded &= uploadFence.IsValid();
			for (const TextureID textureId : uploadedTextureIds)
			{
				CompleteTextureUpload(textureId, uploadFence.IsValid());
			}
			GGLAB_ASSERT_MSG(uploadsSucceeded, "TextureRegistry failed to initialize reserved textures.");
		}

		const auto queueOptionalTexture = [this](
			ReservedTextureIDIndex idIndex,
			std::string_view name,
			const std::filesystem::path& path) noexcept
			{
				const auto canonicalPath = utils::Canonical(path);
				const TextureID textureId = ToTextureId(idIndex);
				const TextureImportSettings importSettings =
					MakeTextureImportSettings(TextureSemantic::UVTest);
				CreateTextureEntry(textureId, name, canonicalPath);
				const auto [iterator, inserted] =
					m_TextureContainer.m_CacheKeyIDMap.emplace(
						TextureCacheKey{ canonicalPath, importSettings },
						textureId);
				GGLAB_UNUSED(iterator);
				GGLAB_ASSERT_MSG(inserted,
					"TextureRegistry failed to register an optional reserved texture.");
				GGLAB_UNUSED(QueueTextureLoad(
					textureId,
					canonicalPath,
					importSettings,
					TextureSemantic::UVTest,
					TaskPriority::Background));
			};

		queueOptionalTexture(
			ReservedTextureIDIndex::UVTestTexture1K,
			"UVTestTexture1K",
			"Assets/Textures/UVTest1K.png");
		queueOptionalTexture(
			ReservedTextureIDIndex::UVTestTexture4K,
			"UVTestTexture4K",
			"Assets/Textures/UVTest4K.png");
	}

	void TextureRegistry::Finalize(const RHIFencePoint& fencePoint) noexcept
	{
		for (const auto& texture : m_TextureContainer.m_TextureIDMap | std::views::values)
		{
			if (texture->m_Texture.IsValid())
			{
				m_Device->RecordTextureUse(texture->m_Texture, fencePoint);
				m_Device->DestroyTexture(texture->m_Texture);
				texture->m_Texture.Reset();
			}
			texture->m_Srv.Reset();
		}
	}

	TextureRegistry::TextureLoadRequest TextureRegistry::LoadTextureAsync(
		const std::filesystem::path& path,
		TextureSemantic semantic,
		TaskPriority priority) noexcept
	{
		if (path.empty())
		{
			GGLAB_LOG_GRAPHICS_WARN("TextureRegistry::LoadTextureAsync received an empty path.");
			return {};
		}

		const auto canonicalPath = utils::Canonical(path);
		std::error_code errorCode;
		if (!std::filesystem::exists(canonicalPath, errorCode) ||
			!std::filesystem::is_regular_file(canonicalPath, errorCode))
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"TextureRegistry::LoadTextureAsync received a missing texture file: '{}'.",
				canonicalPath.string());
			return {};
		}

		const TextureImportSettings importSettings = MakeTextureImportSettings(semantic);
		if (const TextureID existing = FindTexture(canonicalPath, importSettings); existing.IsValid())
		{
			const Texture* texture = GetTexture(existing);
			if (texture && texture->m_State != AssetState::Failed &&
				texture->m_State != AssetState::Cancelled)
			{
				const auto task = m_TextureLoadTasks.find(existing);
				return {
					.m_TextureId = existing,
					.m_Task = task != m_TextureLoadTasks.end() ? task->second : TaskHandle{},
				};
			}

			if (IsReservedTextureId(existing) || !RemoveTexture(existing))
			{
				return {};
			}
			GGLAB_LOG_GRAPHICS_INFO(
				"Removed terminal texture {} from cache path '{}' so a later request can retry.",
				existing.Value(),
				canonicalPath.string());
		}

		const TextureID textureId = CreateTexture(canonicalPath, importSettings);
		if (!textureId.IsValid())
		{
			return {};
		}
		const TaskHandle task = QueueTextureLoad(
			textureId,
			canonicalPath,
			importSettings,
			semantic,
			priority);
		return {
			.m_TextureId = textureId,
			.m_Task = task,
		};
	}

	TaskHandle TextureRegistry::QueueTextureLoad(
		TextureID textureId,
		const std::filesystem::path& canonicalPath,
		const TextureImportSettings& importSettings,
		TextureSemantic semantic,
		TaskPriority priority) noexcept
	{
		Texture* texture = GetTexture(textureId);
		GGLAB_ASSERT_NOT_NULL(texture);
		if (!texture)
		{
			return {};
		}
		if (const auto iterator = m_TextureLoadTasks.find(textureId);
			iterator != m_TextureLoadTasks.end())
		{
			return iterator->second;
		}

		texture->m_State = AssetState::Queued;
		texture->m_Semantic = semantic;
		ProgressReporter(texture->m_LoadProgress).Report(
			0.05f,
			"Queued for texture decoding",
			canonicalPath.filename().generic_string());
		auto job = std::make_shared<TextureLoadJob>();
		const TaskHandle task = m_TaskSystem->Submit(
			{
				.m_Name = std::format(
					"Asset.TextureDecode: {}",
					canonicalPath.filename().generic_string()),
				.m_Priority = priority,
				.m_Progress = texture->m_LoadProgress,
			},
			[canonicalPath, importSettings, job, progress = texture->m_LoadProgress](
				std::stop_token stopToken) noexcept
			{
				if (stopToken.stop_requested())
				{
					return TaskResult::Success();
				}
				job->m_TextureData = TextureLoader::LoadTextureData(
					canonicalPath,
					importSettings,
					ProgressReporter(progress, 0.05f, 0.62f));
				if (!job->m_TextureData.IsValid())
				{
					return TaskResult::Failure(std::format(
						"Failed to decode texture '{}'.",
						canonicalPath.string()));
				}
				return TaskResult::Success();
			},
			[this, textureId, semantic, job](const TaskCompletionInfo& completion) noexcept
			{
				CompleteTextureLoad(
					textureId,
					semantic,
					completion,
					std::move(job->m_TextureData));
			});
		if (!task.IsValid())
		{
			texture->m_State = AssetState::Failed;
			ProgressReporter(texture->m_LoadProgress).Report(
				0.05f,
				"Texture decode submission failed",
				canonicalPath.filename().generic_string());
			return {};
		}

		m_TextureLoadTasks.emplace(textureId, task);
		return task;
	}

	Texture* TextureRegistry::GetTexture(TextureID textureId) noexcept
	{
		return const_cast<Texture*>(std::as_const(*this).GetTexture(textureId));
	}

	const Texture* TextureRegistry::GetTexture(TextureID textureId) const noexcept
	{
		auto iterator = m_TextureContainer.m_TextureIDMap.find(textureId);
		if (iterator != m_TextureContainer.m_TextureIDMap.end())
		{
			return iterator->second.get();
		}
		return nullptr;
	}

	const RHITextureDesc* TextureRegistry::GetTextureDesc(TextureID textureId) const noexcept
	{
		const auto* texture = GetTexture(textureId);
		return texture && texture->m_IsUploaded ? &texture->m_Desc : nullptr;
	}

	RHIDescriptorHandle TextureRegistry::GetSrvDescriptor(TextureID textureId) const noexcept
	{
		const auto* texture = GetTexture(textureId);
		if (!texture || !texture->m_IsUploaded || !texture->m_Srv.IsValid())
		{
			return {};
		}

		return m_Device->GetTextureViewDescriptor(texture->m_Srv);
	}

	uint32_t TextureRegistry::GetShaderVisibleSrvIndex(TextureID textureId) const noexcept
	{
		const RHIDescriptorHandle descriptor = GetSrvDescriptor(textureId);
		GGLAB_ASSERT_MSG(
			descriptor.IsValid() && descriptor.m_HeapType == RHIDescriptorHeapType::CbvSrvUav,
			"TextureRegistry::GetShaderVisibleSrvIndex: texture SRV descriptor is invalid.");
		return descriptor.m_Index;
	}

	uint32_t TextureRegistry::ResolveSrvIndex(TextureID textureId, ReservedTextureIDIndex fallback) const noexcept
	{
		const auto resolveSrvIndex = [this](RHITextureViewHandle srv) noexcept -> uint32_t
			{
				const RHIDescriptorHandle descriptor = m_Device->GetTextureViewDescriptor(srv);
				GGLAB_ASSERT_MSG(
					descriptor.IsValid() && descriptor.m_HeapType == RHIDescriptorHeapType::CbvSrvUav,
					"TextureRegistry::ResolveSrvIndex: texture SRV descriptor is invalid.");
				return descriptor.m_Index;
			};

		if (const auto* texture = GetTexture(textureId); texture != nullptr)
		{
			if (texture->m_IsUploaded && texture->m_Srv.IsValid())
			{
				return resolveSrvIndex(texture->m_Srv);
			}
		}

		const auto fallbackId = ToTextureId(fallback);
		const auto* fallbackTexture = GetTexture(fallbackId);

		GGLAB_ASSERT(fallbackTexture != nullptr && fallbackTexture->m_Srv.IsValid());

		return resolveSrvIndex(fallbackTexture->m_Srv);
	}

	TextureID TextureRegistry::CreateTexture(const std::filesystem::path& canonicalPath,
		const TextureImportSettings& importSettings) noexcept
	{
		const auto textureId = m_TextureIdCounter.Acquire();
		if (!textureId.IsValid())
		{
			return InvalidTextureID;
		}
		auto pathIdPair = m_TextureContainer.m_CacheKeyIDMap.emplace(
			TextureCacheKey{ canonicalPath, importSettings }, textureId);
		GGLAB_ASSERT_MSG(pathIdPair.second, "TextureRegistry: emplace path and TextureID pair failed.");
		if (!pathIdPair.second)
		{
			return InvalidTextureID;
		}

		auto idTexPair = m_TextureContainer.m_TextureIDMap.emplace(textureId, std::make_unique<Texture>());
		GGLAB_ASSERT_MSG(idTexPair.second, "TextureRegistry: emplace TextureID and Texture pair failed.");
		if (!idTexPair.second)
		{
			m_TextureContainer.m_CacheKeyIDMap.erase(pathIdPair.first);
			return InvalidTextureID;
		}

		idTexPair.first->second->m_Id = textureId;
		idTexPair.first->second->m_State = AssetState::CpuReady;
		idTexPair.first->second->m_Name = StringID(canonicalPath.generic_string());
		idTexPair.first->second->m_SourcePath = canonicalPath;
		idTexPair.first->second->m_DebugLabel = canonicalPath.filename().generic_string();
		idTexPair.first->second->m_LoadProgress = std::make_shared<ProgressChannel>();

		return textureId;
	}

	bool TextureRegistry::RemoveTexture(TextureID textureId) noexcept
	{
		if (!textureId.IsValid() || IsReservedTextureId(textureId))
		{
			return false;
		}

		m_TextureLoadTasks.erase(textureId);
		auto textureIter = m_TextureContainer.m_TextureIDMap.find(textureId);
		bool removed = false;
		if (textureIter != m_TextureContainer.m_TextureIDMap.end())
		{
			if (textureIter->second && textureIter->second->m_Texture.IsValid())
			{
				m_Device->DestroyTexture(textureIter->second->m_Texture);
			}
			m_TextureContainer.m_TextureIDMap.erase(textureIter);
			removed = true;
		}
		const size_t removedPaths = std::erase_if(m_TextureContainer.m_CacheKeyIDMap,
			[textureId](const auto& entry) noexcept
			{
				return entry.second == textureId;
			});
		return removed || removedPaths > 0;
	}

	TextureID TextureRegistry::FindTexture(const std::filesystem::path& canonicalPath,
		const TextureImportSettings& importSettings) const noexcept
	{
		const auto& cacheKeyIds = m_TextureContainer.m_CacheKeyIDMap;
		auto iterator = cacheKeyIds.find(TextureCacheKey{ canonicalPath, importSettings });
		if (iterator != cacheKeyIds.end())
		{
			return iterator->second;
		}
		return InvalidTextureID;
	}

	TextureRegistry::TextureUploadData TextureRegistry::MakeTextureUploadData(TextureID textureId,
		TextureAssetData&& textureData, TextureSemantic semantic) noexcept
	{
		TextureUploadData uploadData{};
		uploadData.m_TextureId = textureId;
		uploadData.m_Semantic = semantic;
		uploadData.m_ColorSpace = GetTextureColorSpaceFromSemantic(semantic);
		uploadData.m_TextureData = std::move(textureData);
		return uploadData;
	}

	bool TextureRegistry::UploadTexture(
		const TextureUploadData& uploadData,
		TransferBatch& transferBatch) noexcept
	{
		auto* texture = GetTexture(uploadData.m_TextureId);
		GGLAB_ASSERT_MSG(texture != nullptr, "TextureRegistry::UploadTexture: invalid TextureID.");
		if (!texture)
		{
			return false;
		}
		texture->m_State = AssetState::UploadQueued;

		const TextureAssetData& textureData = uploadData.m_TextureData;
		ProgressReporter(texture->m_LoadProgress).Report(
			0.68f,
			"Recording texture upload",
			std::format(
				"{}x{}, {} mip levels, {} subresources",
				textureData.m_Extent.m_Width,
				textureData.m_Extent.m_Height,
				textureData.m_MipLevels,
				textureData.m_Subresources.size()));
		if (!textureData.IsValid())
		{
			texture->m_State = AssetState::Failed;
			GGLAB_LOG_GRAPHICS_ERROR("TextureRegistry::UploadTexture received invalid texture asset data.");
			return false;
		}
		if (texture->m_Texture.IsValid() || texture->m_IsUploaded)
		{
			texture->m_State = AssetState::Failed;
			GGLAB_LOG_GRAPHICS_ERROR(
				"TextureRegistry::UploadTexture only supports initial upload of a texture entry.");
			return false;
		}

		RHITextureDesc textureDesc{};
		textureDesc.m_Dimension = RHITextureDimension::Texture2D;
		textureDesc.m_Format = textureData.m_ResourceFormat;
		textureDesc.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::CopyDest;
		textureDesc.m_Extent = textureData.m_Extent;
		textureDesc.m_ArraySize = textureData.m_ArraySize;
		textureDesc.m_MipLevels = textureData.m_MipLevels;
		textureDesc.m_SampleCount = 1;
		const std::string category = std::format(
			"Texture.{}", TextureSemanticDebugText(uploadData.m_Semantic));
		const std::string source = TextureDebugSourcePath(texture->m_SourcePath);
		const RHIResourceDebugIdentityDesc debugIdentity
		{
			.m_Domain = RHIResourceDebugDomain::Asset,
			.m_Category = category,
			.m_Label = texture->m_DebugLabel,
			.m_Source = source,
			.m_StableId = uploadData.m_TextureId.Value(),
		};

		texture->m_Texture = m_Device->CreateTexture(textureDesc, debugIdentity);
		GGLAB_ASSERT_MSG(texture->m_Texture.IsValid(), "TextureRegistry::UploadTexture: failed to create RHI texture.");
		if (!texture->m_Texture.IsValid())
		{
			texture->m_State = AssetState::Failed;
			return false;
		}
		texture->m_Desc = textureDesc;
		texture->m_Desc.m_DebugName = nullptr;

		const RHITextureUploadData textureUploadData = textureData.MakeUploadData();
		if (!transferBatch.UploadTexture(texture->m_Texture, textureUploadData))
		{
			texture->m_State = AssetState::Failed;
			GGLAB_LOG_GRAPHICS_ERROR("TextureRegistry::UploadTexture failed to record the texture upload.");
			return false;
		}

		const RHITextureBarrier barrier
		{
			.m_Texture = texture->m_Texture,
			.m_Before =
			{
				.m_Stages = RHIStage::Copy,
				.m_Access = RHIAccess::CopyDest,
				.m_Layout = RHILayout::CopyDest,
			},
			.m_After =
			{
				.m_Stages = RHIStage::PixelShader | RHIStage::ComputeShader,
				.m_Access = RHIAccess::ShaderResource,
				.m_Layout = RHILayout::ShaderResource,
			},
		};
		transferBatch.TextureBarrier(std::span{ &barrier, 1 });

		RHITextureViewDesc srvDesc{};
		srvDesc.m_Type = RHITextureViewType::ShaderResource;
		srvDesc.m_Dimension = textureData.m_SrvDimension;
		srvDesc.m_Format = textureData.m_ViewFormat;
		srvDesc.m_Subresources.m_BaseMip = 0;
		srvDesc.m_Subresources.m_MipCount = textureData.m_MipLevels;
		srvDesc.m_Subresources.m_BaseArraySlice = 0;
		srvDesc.m_Subresources.m_ArraySliceCount = textureData.m_ArraySize;

		texture->m_Srv = m_Device->CreateTextureView(texture->m_Texture, srvDesc);
		GGLAB_ASSERT_MSG(texture->m_Srv.IsValid(), "TextureRegistry::UploadTexture: failed to create RHI texture SRV.");
		if (!texture->m_Srv.IsValid())
		{
			texture->m_State = AssetState::Failed;
			return false;
		}
		texture->m_SrvDimension = textureData.m_SrvDimension;
		texture->m_Semantic = uploadData.m_Semantic;
		texture->m_State = AssetState::GpuProcessing;
		return true;
	}

	void TextureRegistry::CompleteTextureUpload(TextureID textureId, bool succeeded) noexcept
	{
		auto* texture = GetTexture(textureId);
		if (!texture)
		{
			return;
		}
		texture->m_State = succeeded ? AssetState::Ready : AssetState::Failed;
		ProgressReporter(texture->m_LoadProgress).Report(
			succeeded ? 1.0f : 0.96f,
			succeeded ? "Texture ready" : "Texture GPU upload failed",
			texture->m_DebugLabel);
		texture->m_IsUploaded = succeeded;
		if (!succeeded)
		{
			texture->m_Srv.Reset();
			if (texture->m_Texture.IsValid())
			{
				m_Device->DestroyTexture(texture->m_Texture);
				texture->m_Texture.Reset();
			}
			texture->m_Desc = {};
			texture->m_SrvDimension = RHITextureViewDimension::Unknown;
		}
	}

	bool TextureRegistry::PublishImportedTexture(
		TextureID textureId,
		TextureSemantic semantic,
		TextureAssetData&& textureData) noexcept
	{
		Texture* texture = GetTexture(textureId);
		if (!texture)
		{
			return false;
		}
		texture->m_State = AssetState::CpuReady;

		auto uploadData = MakeTextureUploadData(
			textureId,
			std::move(textureData),
			semantic);
		auto batch = m_TransferManager->BeginBatch();
		const bool recorded = UploadTexture(uploadData, batch);
		const AssetUploadHandle uploadHandle = m_AssetUploadScheduler->Submit(
			{
				.m_Name = std::format("Texture {}", textureId.Value()),
				.m_Progress = texture->m_LoadProgress,
			},
			std::move(batch),
			recorded,
			[this, textureId](const AssetUploadCompletionInfo& completion) noexcept
			{
				const bool succeeded = completion.m_Status == AssetUploadStatus::Succeeded;
				CompleteTextureUpload(textureId, succeeded);
				if (succeeded)
				{
					GGLAB_LOG_GRAPHICS_INFO(
						"Texture {} GPU upload completed in {:.2f} ms.",
						textureId.Value(),
						completion.m_ElapsedMilliseconds);
				}
				else
				{
					GGLAB_LOG_GRAPHICS_ERROR("Texture {} GPU upload failed.", textureId.Value());
				}
			});
		return uploadHandle.IsValid();
	}

	void TextureRegistry::CompleteTextureLoad(
		TextureID textureId,
		TextureSemantic semantic,
		const TaskCompletionInfo& completion,
		TextureAssetData&& textureData) noexcept
	{
		m_TextureLoadTasks.erase(textureId);
		Texture* texture = GetTexture(textureId);
		if (!texture)
		{
			return;
		}

		if (completion.m_Status == TaskStatus::Cancelled)
		{
			texture->m_State = AssetState::Cancelled;
			ProgressReporter(texture->m_LoadProgress).Report(
				0.05f,
				"Texture loading cancelled",
				completion.m_Name);
			return;
		}
		if (completion.m_Status != TaskStatus::Succeeded)
		{
			texture->m_State = AssetState::Failed;
			ProgressReporter(texture->m_LoadProgress).Report(
				0.05f,
				"Texture decoding failed",
				completion.m_Error);
			GGLAB_LOG_GRAPHICS_ERROR(
				"Async texture decode '{}' failed: {}",
				completion.m_Name,
				completion.m_Error);
			return;
		}

		if (!PublishImportedTexture(textureId, semantic, std::move(textureData)))
		{
			texture->m_State = AssetState::Failed;
			ProgressReporter(texture->m_LoadProgress).Report(
				0.62f,
				"Texture publication failed");
			return;
		}
		GGLAB_LOG_GRAPHICS_INFO(
			"Async texture {} queued for GPU upload (queueMs={:.2f}, cpuMs={:.2f}).",
			textureId.Value(),
			completion.m_QueueMilliseconds,
			completion.m_ExecutionMilliseconds);
	}

	void TextureRegistry::CreateTextureEntry(
		TextureID id,
		std::string_view textureName,
		const std::filesystem::path& sourcePath) noexcept
	{
		if (GetTexture(id) == nullptr)
		{
			auto [iterator, result] = m_TextureContainer.m_TextureIDMap.emplace(id, std::make_unique<Texture>());
			if (result)
			{
				auto& texture = iterator->second;
				texture->m_Id = id;
				texture->m_State = AssetState::CpuReady;
				texture->m_Name = StringID(textureName);
				texture->m_SourcePath = sourcePath;
				texture->m_DebugLabel = textureName;
				texture->m_LoadProgress = std::make_shared<ProgressChannel>();
				texture->m_IsUploaded = false;
				texture->m_Srv.Reset();
				texture->m_Texture.Reset();
				texture->m_Desc = {};
			}
		}
	}
}
