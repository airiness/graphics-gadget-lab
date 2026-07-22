#include "Core/Precompiled.h"
#include "Graphics/Asset/Residency/AssetResidencyController.h"
#include "Graphics/Asset/TextureAssetSystem.h"
#include "Graphics/Asset/TextureAssetValidation.h"
#include "Graphics/Asset/AssetIdentityConversions.h"
#include "Graphics/Asset/BuiltinTextureFactory.h"
#include "Graphics/Asset/Dependency/AssetStateEventQueue.h"
#include "Graphics/Asset/Loading/AssetLoadCoordinator.h"
#include "Graphics/Asset/Streaming/AssetUploadScheduler.h"
#include "Graphics/TransferManager.h"
#include "Graphics/RHI/RHIDevice.h"
#include "Graphics/Utility/TextureUtils.h"
#include "Core/Utility/PathUtils.h"
#include "Core/Utility/TypeUtils.h"

namespace gglab
{
	namespace
	{
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

		[[nodiscard]] AssetStreamingWorkEstimate EstimateTextureUpload(
			const TextureAssetData& textureData) noexcept
		{
			const uint64_t payloadBytes = static_cast<uint64_t>(textureData.m_Pixels.size());
			return {
				.m_SourceBytes = payloadBytes,
				.m_StagingBytes = payloadBytes,
				.m_OperationCount = std::max<uint32_t>(
					1,
					static_cast<uint32_t>(textureData.m_Subresources.size())),
			};
		}

		[[nodiscard]] constexpr AssetStateEventOperationPhase ResidencyStateEventPhase(
			const AssetResidencyOperation& operation,
			bool completes = false) noexcept
		{
			return !operation.IsValid() ? AssetStateEventOperationPhase::None :
				completes ? AssetStateEventOperationPhase::Completes :
					AssetStateEventOperationPhase::InProgress;
		}

	}

	TextureAssetSystem::TextureAssetSystem(const CreateInfo& createInfo) noexcept :
		m_Device(createInfo.m_Device),
		m_LoadCoordinator(createInfo.m_LoadCoordinator),
		m_TransferManager(createInfo.m_TransferManager),
		m_AssetUploadScheduler(createInfo.m_AssetUploadScheduler),
		m_StateEvents(createInfo.m_StateEvents),
		m_ArtifactCache(createInfo.m_ArtifactCache)
	{
		GGLAB_ASSERT_MSG(m_Device != nullptr, "RHIDevice is null!");
		GGLAB_ASSERT_MSG(m_LoadCoordinator != nullptr, "AssetLoadCoordinator is null!");
		GGLAB_ASSERT_MSG(m_TransferManager != nullptr, "TransferManager is null!");
		GGLAB_ASSERT_MSG(m_AssetUploadScheduler != nullptr, "AssetUploadScheduler is null!");
		GGLAB_ASSERT_MSG(m_StateEvents != nullptr, "AssetStateEventQueue is null!");
		GGLAB_ASSERT_MSG(m_ArtifactCache != nullptr, "TextureArtifactCache is null!");
	}

	void TextureAssetSystem::SetTextureState(
		Texture& texture,
		AssetState state,
		AssetResidencyOperation residencyOperation,
		AssetStateEventOperationPhase operationPhase) noexcept
	{
		const bool hasOperation = residencyOperation.IsValid();
		GGLAB_ASSERT_MSG(
			hasOperation == (operationPhase != AssetStateEventOperationPhase::None),
			"Texture state event operation phase does not match its operation token.");
		const AssetState previousState = texture.m_State;
		SetAssetState(texture, state);
		// A same-state terminal transition still has to retire its operation.
		if ((previousState != state ||
			operationPhase == AssetStateEventOperationPhase::Completes))
		{
			m_StateEvents->Push({
				.m_ContentVersion = MakeAssetContentVersion(
					texture.m_Id,
					texture.m_ContentGeneration),
				.m_ContentState = texture.m_ContentState,
				.m_ResidencyState = texture.m_ResidencyState,
			},
				hasOperation ?
					std::optional{ residencyOperation.m_Token } : std::nullopt,
				operationPhase);
		}
	}

	void TextureAssetSystem::InitializeReservedTextures() noexcept
	{
		std::vector<BuiltinTextureAsset> builtinTextures =
			BuiltinTextureFactory::BuildBootstrapTextures();
		std::vector<TextureUploadData> uploads;
		uploads.reserve(builtinTextures.size());

		for (BuiltinTextureAsset& builtinTexture : builtinTextures)
		{
			const TextureID id = ToTextureId(builtinTexture.m_Id);
			const TextureImportSettings importSettings =
				MakeTextureImportSettings(builtinTexture.m_Semantic);
			CreateTextureEntry(id, builtinTexture.m_Name, importSettings);
			uploads.emplace_back(MakeTextureUploadData(
				id,
				std::move(builtinTexture.m_Data),
				importSettings));
		}

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
			GGLAB_ASSERT_MSG(uploadsSucceeded, "TextureAssetSystem failed to initialize reserved textures.");
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
				CreateTextureEntry(textureId, name, importSettings, canonicalPath);
				const bool inserted = m_Store.BindCacheKey(
					canonicalPath,
					importSettings,
					textureId);
				GGLAB_ASSERT_MSG(inserted,
					"TextureAssetSystem failed to register an optional reserved texture.");
				GGLAB_UNUSED(QueueTextureLoad(
					textureId,
					canonicalPath,
					importSettings,
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

	void TextureAssetSystem::Finalize(const RHIFencePoint& fencePoint) noexcept
	{
		GGLAB_ASSERT_MSG(
			m_PublicationOrphanedTextures.empty(),
			"TextureAssetSystem finalized while publication rollback is pending GPU completion.");
		for (const auto& texture : m_Store.Entries() | std::views::values)
		{
			if (texture->m_Gpu.m_Texture.IsValid())
			{
				m_Device->RecordTextureUse(texture->m_Gpu.m_Texture, fencePoint);
				m_Device->DestroyTexture(texture->m_Gpu.m_Texture);
				texture->m_Gpu.m_Texture.Reset();
			}
			texture->m_Gpu.m_Srv.Reset();
		}
	}

	TextureAssetSystem::TextureLoadRequest TextureAssetSystem::LoadTextureAsync(
		const std::filesystem::path& path,
		TextureSemantic semantic,
		TaskPriority priority) noexcept
	{
		if (path.empty())
		{
			GGLAB_LOG_GRAPHICS_WARN("TextureAssetSystem::LoadTextureAsync received an empty path.");
			return {};
		}

		const auto canonicalPath = utils::Canonical(path);
		std::error_code errorCode;
		if (!std::filesystem::exists(canonicalPath, errorCode) ||
			!std::filesystem::is_regular_file(canonicalPath, errorCode))
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"TextureAssetSystem::LoadTextureAsync received a missing texture file: '{}'.",
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
				const TaskHandle task = texture->m_State == AssetState::CpuReady &&
					!texture->m_Load.m_IsReloading ? TaskHandle{} :
					m_LoadCoordinator->GetTextureDecodeTask(
						MakeAssetContentVersion(existing, texture->m_ContentGeneration));
				return {
					.m_TextureId = existing,
					.m_Generation = texture->m_ContentGeneration,
					.m_Task = task,
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
			priority);
		return {
			.m_TextureId = textureId,
			.m_Generation = GetTexture(textureId)->m_ContentGeneration,
			.m_Task = task,
		};
	}

	TaskHandle TextureAssetSystem::QueueTextureLoad(
		TextureID textureId,
		const std::filesystem::path& canonicalPath,
		const TextureImportSettings& importSettings,
		TaskPriority priority,
		bool residencyReload,
		AssetResidencyOperation residencyOperation) noexcept
	{
		Texture* texture = EditTexture(textureId);
		GGLAB_ASSERT_NOT_NULL(texture);
		if (!texture)
		{
			return {};
		}
		if (residencyReload &&
			(residencyOperation.m_Kind != AssetResidencyOperationKind::Reload ||
				!AssetResidencyController::IsCurrentOperation(
					*texture,
					residencyOperation)))
		{
			return {};
		}
		if (!residencyReload)
		{
			SetTextureState(*texture, AssetState::Queued);
		}
		GGLAB_ASSERT_MSG(
			texture->m_Source.m_CanonicalPath == canonicalPath &&
				texture->m_Source.m_ImportSettings == importSettings,
			"Texture decode request does not match the texture source identity.");
		ProgressReporter(texture->m_Load.m_Progress).Report(
			0.05f,
			"Queued for texture decoding",
			canonicalPath.filename().generic_string());
		const AssetLoadSubmission submission = m_LoadCoordinator->SubmitTextureDecode({
			.m_ContentVersion = MakeAssetContentVersion(
				textureId,
				texture->m_ContentGeneration),
			.m_SourcePath = canonicalPath,
			.m_ImportSettings = importSettings,
			.m_Semantic = importSettings.m_Semantic,
			.m_Priority = priority,
			.m_Progress = texture->m_Load.m_Progress,
			.m_ExpectedSourceDigest = residencyReload ?
				texture->m_Source.m_SourceDigest : SourceDigest{},
			.m_ExpectedDerivedDataKey = residencyReload ?
				texture->m_Source.m_DerivedDataKey : DerivedDataKey{},
			.m_ExpectedArtifactContentDigest = residencyReload ?
				texture->m_Source.m_ArtifactContentDigest : ArtifactContentDigest{},
			.m_ResidencyReload = residencyReload,
			.m_ResidencyOperation = residencyOperation,
		});
		if (!submission.IsValid())
		{
			SetTextureState(
				*texture,
				residencyReload ? AssetState::CpuReady : AssetState::Failed,
				residencyOperation,
				ResidencyStateEventPhase(residencyOperation, true));
			texture->m_Load.m_IsReloading = false;
			ProgressReporter(texture->m_Load.m_Progress).Report(
				0.05f,
				"Texture decode submission failed",
				canonicalPath.filename().generic_string());
			return {};
		}
		return submission.m_Task;
	}

	Texture* TextureAssetSystem::EditTexture(TextureID textureId) noexcept
	{
		return const_cast<Texture*>(std::as_const(*this).GetTexture(textureId));
	}

	const Texture* TextureAssetSystem::GetTexture(TextureID textureId) const noexcept
	{
		return m_Store.Find(textureId);
	}

	bool TextureAssetSystem::IsCurrentResidencyOperation(
		const AssetResidencyOperation& operation) const noexcept
	{
		const AssetContentVersion contentVersion = operation.m_Token.m_ContentVersion;
		const Texture* texture = GetTexture(TextureID{
			static_cast<uint32_t>(contentVersion.m_Key.m_StableId) });
		return contentVersion.m_Key.m_Kind == AssetKind::Texture && texture &&
			texture->m_ContentGeneration == contentVersion.m_ContentGeneration &&
			AssetResidencyController::IsCurrentOperation(*texture, operation);
	}

	bool TextureAssetSystem::IsCurrentResidencyOperation(
		const AssetOperationToken& operation) const noexcept
	{
		const AssetContentVersion contentVersion = operation.m_ContentVersion;
		const Texture* texture = GetTexture(TextureID{
			static_cast<uint32_t>(contentVersion.m_Key.m_StableId) });
		return contentVersion.m_Key.m_Kind == AssetKind::Texture && texture &&
			texture->m_ContentGeneration == contentVersion.m_ContentGeneration &&
			AssetResidencyController::IsCurrentOperation(*texture, operation);
	}

	void TextureAssetSystem::CompleteResidencyOperation(
		const AssetResidencyOperation& operation) noexcept
	{
		const AssetContentVersion contentVersion = operation.m_Token.m_ContentVersion;
		Texture* texture = EditTexture(TextureID{
			static_cast<uint32_t>(contentVersion.m_Key.m_StableId) });
		if (texture && texture->m_ContentGeneration == contentVersion.m_ContentGeneration)
		{
			AssetResidencyController::CompleteResidencyOperation(*texture, operation);
		}
	}

	void TextureAssetSystem::CompleteResidencyOperation(
		const AssetOperationToken& operation) noexcept
	{
		const AssetContentVersion contentVersion = operation.m_ContentVersion;
		Texture* texture = EditTexture(TextureID{
			static_cast<uint32_t>(contentVersion.m_Key.m_StableId) });
		if (texture && texture->m_ContentGeneration == contentVersion.m_ContentGeneration)
		{
			AssetResidencyController::CompleteResidencyOperation(*texture, operation);
		}
	}

	bool TextureAssetSystem::RestoreEvictionForShutdown(
		const AssetResidencyOperation& operation) noexcept
	{
		const AssetContentVersion contentVersion = operation.m_Token.m_ContentVersion;
		Texture* texture = EditTexture(TextureID{
			static_cast<uint32_t>(contentVersion.m_Key.m_StableId) });
		if (!texture || texture->m_ContentGeneration != contentVersion.m_ContentGeneration)
		{
			return false;
		}
		SetTextureState(
			*texture,
			texture->m_State == AssetState::Evicting ? AssetState::Ready : texture->m_State,
			operation,
			AssetStateEventOperationPhase::Completes);
		return true;
	}

	TextureAssetSystem::EvictionFinalizationResult TextureAssetSystem::FinalizeEviction(
		const AssetResidencyOperation& operation,
		bool protectedByInterest) noexcept
	{
		const AssetContentVersion contentVersion = operation.m_Token.m_ContentVersion;
		Texture* texture = EditTexture(TextureID{
			static_cast<uint32_t>(contentVersion.m_Key.m_StableId) });
		if (!texture || texture->m_ContentGeneration != contentVersion.m_ContentGeneration)
		{
			return { .m_Finalized = true };
		}
		if (texture->m_State != AssetState::Evicting)
		{
			const bool cancelled = texture->m_State == AssetState::Ready;
			AssetResidencyController::CompleteResidencyOperation(*texture, operation);
			return { .m_Finalized = true, .m_Cancelled = cancelled };
		}
		if (protectedByInterest || texture->m_ResidencyPolicy == AssetResidencyPolicy::Pinned)
		{
			SetTextureState(
				*texture,
				AssetState::Ready,
				operation,
				AssetStateEventOperationPhase::Completes);
			return { .m_Finalized = true, .m_Cancelled = true };
		}
		return {
			.m_Finalized = FinalizeTextureEviction(texture->m_Id, operation),
		};
	}

	AssetResidencyOperation TextureAssetSystem::BeginResidencyOperation(
		const AssetContentVersion& contentVersion,
		AssetResidencyOperationKind kind,
		AssetResidencyController& controller) noexcept
	{
		Texture* texture = EditTexture(TextureID{
			static_cast<uint32_t>(contentVersion.m_Key.m_StableId) });
		if (!texture || texture->m_ContentGeneration != contentVersion.m_ContentGeneration)
		{
			return {};
		}
		const AssetResidencyOperation operation = controller.BeginResidencyOperation(
			*texture,
			contentVersion,
			kind);
		if (operation.IsValid() && kind == AssetResidencyOperationKind::Evict)
		{
			SetTextureState(
				*texture,
				AssetState::Evicting,
				operation,
				AssetStateEventOperationPhase::InProgress);
		}
		return operation;
	}

	TaskHandle TextureAssetSystem::RequestResidency(
		TextureID textureId,
		uint64_t generation,
		TaskPriority priority,
		AssetResidencyController& controller) noexcept
	{
		Texture* texture = EditTexture(textureId);
		if (!texture || texture->m_ContentGeneration != generation ||
			(texture->m_State != AssetState::Evicting &&
				(texture->m_State != AssetState::CpuReady || texture->m_ResidencyEpoch == 0)))
		{
			return {};
		}
		controller.RecordReloadRequest(texture->m_Load.m_IsReloading);
		const AssetContentVersion contentVersion = MakeAssetContentVersion(textureId, generation);
		if (texture->m_State == AssetState::Evicting)
		{
			const AssetResidencyOperation operation = controller.BeginResidencyOperation(
				*texture,
				contentVersion,
				AssetResidencyOperationKind::Reload);
			SetTextureState(
				*texture,
				AssetState::Ready,
				operation,
				AssetStateEventOperationPhase::Completes);
			return {};
		}
		AssetResidencyOperation operation;
		if (texture->m_Load.m_IsReloading)
		{
			operation = {
				.m_Token = MakeAssetOperationToken(contentVersion, texture->m_ResidencyOperationSerial),
				.m_Kind = AssetResidencyOperationKind::Reload,
			};
		}
		else
		{
			operation = controller.BeginResidencyOperation(
				*texture,
				contentVersion,
				AssetResidencyOperationKind::Reload);
			if (!operation.IsValid())
			{
				return {};
			}
		}
		return RequestTextureResidency(textureId, operation, priority);
	}

	void TextureAssetSystem::MarkUsed(TextureID textureId, uint64_t frame) noexcept
	{
		if (Texture* texture = EditTexture(textureId))
		{
			AssetResidencyController::MarkAssetUsed(*texture, frame);
		}
	}

	bool TextureAssetSystem::SetResidencyPolicy(
		TextureID textureId,
		AssetResidencyPolicy policy) noexcept
	{
		Texture* texture = EditTexture(textureId);
		return texture && AssetResidencyController::SetResidencyPolicy(
			*texture,
			policy,
			IsReservedTextureId(textureId));
	}

	bool TextureAssetSystem::BeginPublication(TextureID textureId) noexcept
	{
		Texture* texture = EditTexture(textureId);
		if (!texture)
		{
			return false;
		}
		SetTextureState(*texture, AssetState::Publishing);
		return true;
	}

	uint32_t TextureAssetSystem::ResolveSrvIndex(TextureID textureId, ReservedTextureIDIndex fallback) const noexcept
	{
		const auto resolveSrvIndex = [this](RHITextureViewHandle srv) noexcept -> uint32_t
			{
				const RHIDescriptorHandle descriptor = m_Device->GetTextureViewDescriptor(srv);
				GGLAB_ASSERT_MSG(
					descriptor.IsValid() && descriptor.m_HeapType == RHIDescriptorHeapType::CbvSrvUav,
					"TextureAssetSystem::ResolveSrvIndex: texture SRV descriptor is invalid.");
				return descriptor.m_Index;
			};

		if (const auto* texture = GetTexture(textureId); texture != nullptr)
		{
			if (texture->m_Gpu.m_IsUploaded && texture->m_Gpu.m_Srv.IsValid() &&
				texture->m_ResidencyState == AssetResidencyState::Resident)
			{
				return resolveSrvIndex(texture->m_Gpu.m_Srv);
			}
		}

		const auto fallbackId = ToTextureId(fallback);
		const auto* fallbackTexture = GetTexture(fallbackId);

		GGLAB_ASSERT(fallbackTexture != nullptr && fallbackTexture->m_Gpu.m_Srv.IsValid());

		return resolveSrvIndex(fallbackTexture->m_Gpu.m_Srv);
	}

	TextureContentRef TextureAssetSystem::GetTextureContentRef(TextureID textureId) const noexcept
	{
		const Texture* texture = GetTexture(textureId);
		return texture ? TextureContentRef{
			.m_Id = textureId,
			.m_Generation = texture->m_ContentGeneration,
		} : TextureContentRef{};
	}

	std::optional<AssetContentFingerprint> TextureAssetSystem::GetTextureContentFingerprint(
		TextureContentRef content) const noexcept
	{
		const Texture* texture = GetTexture(content.m_Id);
		if (!content.IsValid() || !texture ||
			texture->m_ContentGeneration != content.m_Generation ||
			!texture->m_Source.m_ContentFingerprint.IsValid())
		{
			return std::nullopt;
		}
		return texture->m_Source.m_ContentFingerprint;
	}

	std::optional<AssetState> TextureAssetSystem::GetTextureState(
		TextureContentRef content) const noexcept
	{
		const Texture* texture = GetTexture(content.m_Id);
		if (!content.IsValid() || !texture ||
			texture->m_ContentGeneration != content.m_Generation)
		{
			return std::nullopt;
		}
		return texture->m_State;
	}

	std::optional<ResidentTextureResource> TextureAssetSystem::GetResidentTextureResource(
		TextureContentRef content) const noexcept
	{
		const Texture* texture = GetTexture(content.m_Id);
		if (!content.IsValid() || !texture ||
			texture->m_ContentGeneration != content.m_Generation ||
			texture->m_State != AssetState::Ready ||
			texture->m_ResidencyState != AssetResidencyState::Resident ||
			!texture->m_Gpu.m_IsUploaded || !texture->m_Gpu.m_Texture.IsValid() ||
			!texture->m_Gpu.m_Srv.IsValid())
		{
			return std::nullopt;
		}

		const RHIDescriptorHandle descriptor =
			m_Device->GetTextureViewDescriptor(texture->m_Gpu.m_Srv);
		GGLAB_ASSERT_MSG(
			descriptor.IsValid() &&
				descriptor.m_HeapType == RHIDescriptorHeapType::CbvSrvUav,
			"TextureAssetSystem::GetResidentTextureResource: texture SRV descriptor is invalid.");
		if (!descriptor.IsValid() ||
			descriptor.m_HeapType != RHIDescriptorHeapType::CbvSrvUav)
		{
			return std::nullopt;
		}

		return ResidentTextureResource{
			.m_Content = content,
			.m_Texture = texture->m_Gpu.m_Texture,
			.m_Desc = texture->m_Gpu.m_Desc,
			.m_SrvIndex = descriptor.m_Index,
		};
	}

	std::vector<TextureAssetReadInfo> TextureAssetSystem::GetTextureAssetReadInfos() const
	{
		std::vector<TextureAssetReadInfo> infos;
		infos.reserve(m_Store.Size());
		for (const auto& [textureId, texture] : m_Store.Entries())
		{
			infos.push_back({
				.m_Content = {
					.m_Id = textureId,
					.m_Generation = texture->m_ContentGeneration,
				},
				.m_Lifecycle = static_cast<const AssetLifecycle&>(*texture),
				.m_SourcePath = texture->m_Source.m_CanonicalPath,
				.m_ImportSettings = texture->m_Source.m_ImportSettings,
				.m_Semantic = texture->m_Source.m_ImportSettings.m_Semantic,
				.m_Name = texture->m_Name,
				.m_Texture = texture->m_Gpu.m_Texture,
				.m_DebugName = m_Device ?
					std::string(m_Device->GetTextureDebugName(texture->m_Gpu.m_Texture)) :
					std::string{},
				.m_ArtifactContentDigest = texture->m_Source.m_ArtifactContentDigest,
				.m_SourceDigest = texture->m_Source.m_SourceDigest,
				.m_DerivedDataKey = texture->m_Source.m_DerivedDataKey,
				.m_IsUploaded = texture->m_Gpu.m_IsUploaded,
				.m_HasSrv = texture->m_Gpu.m_Srv.IsValid(),
				.m_IsReserved = IsReservedTextureId(textureId),
				.m_IsCpuArtifactCached = m_ArtifactCache->Contains(
					texture->m_Source.m_ArtifactContentDigest),
				.m_IsDerivedDataCached = m_LoadCoordinator->IsTextureDerivedDataCached(
					texture->m_Source.m_DerivedDataKey),
			});
		}
		return infos;
	}

	uint32_t TextureAssetSystem::GetReloadingTextureCount() const noexcept
	{
		return static_cast<uint32_t>(std::ranges::count_if(
			m_Store.Entries() | std::views::values,
			[](const auto& texture) noexcept { return texture->m_Load.m_IsReloading; }));
	}

	std::vector<TextureID> TextureAssetSystem::GetTextureIds() const
	{
		std::vector<TextureID> ids;
		ids.reserve(m_Store.Size());
		for (TextureID textureId : m_Store.Entries() | std::views::keys)
		{
			ids.push_back(textureId);
		}
		return ids;
	}

	TextureID TextureAssetSystem::CreateTexture(const std::filesystem::path& canonicalPath,
		const TextureImportSettings& importSettings) noexcept
	{
		const auto textureId = m_TextureIdCounter.Acquire();
		if (!textureId.IsValid())
		{
			return InvalidTextureID;
		}
		const bool cacheKeyBound = m_Store.BindCacheKey(
			canonicalPath,
			importSettings,
			textureId);
		GGLAB_ASSERT_MSG(cacheKeyBound, "TextureAssetSystem: emplace path and TextureID pair failed.");
		if (!cacheKeyBound)
		{
			return InvalidTextureID;
		}

		auto texture = std::make_unique<Texture>();
		Texture* insertedTexture = texture.get();
		const bool inserted = m_Store.Insert(textureId, std::move(texture));
		GGLAB_ASSERT_MSG(inserted, "TextureAssetSystem: emplace TextureID and Texture pair failed.");
		if (!inserted)
		{
			GGLAB_UNUSED(m_Store.Remove(textureId));
			return InvalidTextureID;
		}

		insertedTexture->m_Id = textureId;
		BeginAssetContentGeneration(
			*insertedTexture,
			1,
			AssetState::CpuReady);
		insertedTexture->m_Name = StringID(canonicalPath.generic_string());
		insertedTexture->m_Source = {
			.m_CanonicalPath = canonicalPath,
			.m_ImportSettings = importSettings,
		};
		insertedTexture->m_DebugLabel = canonicalPath.filename().generic_string();
		insertedTexture->m_Load.m_Progress = std::make_shared<ProgressChannel>();

		return textureId;
	}

	bool TextureAssetSystem::RemoveTexture(TextureID textureId) noexcept
	{
		if (!textureId.IsValid() || IsReservedTextureId(textureId))
		{
			return false;
		}

		m_LoadCoordinator->DiscardTextureDecode(MakeAssetKey(textureId));
		m_PublicationOrphanedTextures.erase(textureId);
		if (Texture* texture = m_Store.Edit(textureId))
		{
			if (texture->m_Gpu.m_Texture.IsValid())
			{
				m_Device->DestroyTexture(texture->m_Gpu.m_Texture);
			}
		}
		return m_Store.Remove(textureId);
	}

	TextureID TextureAssetSystem::FindTexture(const std::filesystem::path& canonicalPath,
		const TextureImportSettings& importSettings) const noexcept
	{
		return m_Store.FindCached(canonicalPath, importSettings);
	}

	TextureAssetSystem::TextureUploadData TextureAssetSystem::MakeTextureUploadData(
		TextureID textureId,
		TextureAssetData&& textureData,
		const TextureImportSettings& importSettings,
		AssetContentFingerprint contentFingerprint) noexcept
	{
		if (!contentFingerprint.IsValid())
		{
			contentFingerprint = ComputeTextureContentFingerprint(
				textureData,
				importSettings);
		}
		TextureArtifactHandle artifactHandle = m_ArtifactCache->CreateAndAdmit(
			std::move(textureData));
		if (!artifactHandle)
		{
			return {};
		}
		return MakeTextureUploadData(
			textureId,
			std::move(artifactHandle),
			importSettings,
			contentFingerprint);
	}

	TextureAssetSystem::TextureUploadData TextureAssetSystem::MakeTextureUploadData(
		TextureID textureId,
		TextureArtifactHandle artifact,
		const TextureImportSettings& importSettings,
		AssetContentFingerprint contentFingerprint,
		SourceDigest sourceDigest,
		DerivedDataKey derivedDataKey) noexcept
	{
		artifact = m_ArtifactCache->Admit(std::move(artifact));
		if (!artifact)
		{
			return {};
		}
		if (!contentFingerprint.IsValid())
		{
			contentFingerprint = ComputeTextureContentFingerprint(
				artifact->m_Data,
				importSettings);
		}
		if (!contentFingerprint.IsValid())
		{
			return {};
		}

		TextureUploadData uploadData{};
		uploadData.m_TextureId = textureId;
		uploadData.m_ImportSettings = importSettings;
		uploadData.m_ColorSpace = GetTextureColorSpaceFromSemantic(
			importSettings.m_Semantic);
		uploadData.m_Artifact = std::move(artifact);
		uploadData.m_ContentFingerprint = contentFingerprint;
		uploadData.m_SourceDigest = sourceDigest;
		uploadData.m_DerivedDataKey = derivedDataKey;
		return uploadData;
	}

	bool TextureAssetSystem::UploadTexture(
		const TextureUploadData& uploadData,
		TransferBatch& transferBatch,
		AssetResidencyOperation residencyOperation) noexcept
	{
		auto* texture = EditTexture(uploadData.m_TextureId);
		GGLAB_ASSERT_MSG(texture != nullptr, "TextureAssetSystem::UploadTexture: invalid TextureID.");
		if (!texture)
		{
			return false;
		}
		GGLAB_ASSERT_MSG(
			texture->m_Source.m_ImportSettings == uploadData.m_ImportSettings,
			"Texture upload settings do not match the texture source identity.");
		if (texture->m_Source.m_ImportSettings != uploadData.m_ImportSettings)
		{
			return false;
		}
		const AssetStateEventOperationPhase operationPhase =
			ResidencyStateEventPhase(residencyOperation);
		SetTextureState(
			*texture,
			AssetState::UploadQueued,
			residencyOperation,
			operationPhase);

		if (!uploadData.m_Artifact || !uploadData.m_Artifact->IsValid())
		{
			SetTextureState(
				*texture,
				AssetState::Failed,
				residencyOperation,
				operationPhase);
			GGLAB_LOG_GRAPHICS_ERROR(
				"TextureAssetSystem::UploadTexture received an invalid texture artifact.");
			return false;
		}
		if (residencyOperation.IsValid() &&
			texture->m_Source.m_ArtifactContentDigest.IsValid() &&
			uploadData.m_Artifact->m_ContentDigest !=
				texture->m_Source.m_ArtifactContentDigest)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"TextureAssetSystem::UploadTexture rejected immutable generation content (expected artifact {}, upload artifact {}).",
				ArtifactContentDigestText(texture->m_Source.m_ArtifactContentDigest),
				ArtifactContentDigestText(uploadData.m_Artifact->m_ContentDigest));
			return false;
		}
		const TextureAssetData& textureData = uploadData.m_Artifact->m_Data;
		ProgressReporter(texture->m_Load.m_Progress).Report(
			0.68f,
			"Recording texture upload",
			std::format(
				"{}x{}, {} mip levels, {} subresources",
				textureData.m_Extent.m_Width,
				textureData.m_Extent.m_Height,
				textureData.m_MipLevels,
				textureData.m_Subresources.size()));
		const TextureUploadValidationResult textureValidation =
			ValidateTextureUploadForDevice(textureData, *m_Device);
		if (!textureValidation.IsValid())
		{
			SetTextureState(
				*texture,
				AssetState::Failed,
				residencyOperation,
				operationPhase);
			GGLAB_LOG_GRAPHICS_ERROR(
				"TextureAssetSystem::UploadTexture rejected {} texture data (structure={}, rhi={}).",
				TextureUploadValidationDispositionText(textureValidation.m_Disposition),
				TextureStructureValidationErrorText(textureValidation.m_StructureError),
				RHITextureValidationErrorText(textureValidation.m_RHIError));
			return false;
		}
		if (!uploadData.m_ContentFingerprint.IsValid())
		{
			SetTextureState(
				*texture,
				AssetState::Failed,
				residencyOperation,
				operationPhase);
			GGLAB_LOG_GRAPHICS_ERROR(
				"TextureAssetSystem::UploadTexture received an invalid content fingerprint.");
			return false;
		}
		if (texture->m_Gpu.m_Texture.IsValid() || texture->m_Gpu.m_IsUploaded)
		{
			SetTextureState(
				*texture,
				AssetState::Failed,
				residencyOperation,
				operationPhase);
			GGLAB_LOG_GRAPHICS_ERROR(
				"TextureAssetSystem::UploadTexture only supports initial upload of a texture entry.");
			return false;
		}

		const RHITextureDesc textureDesc = BuildTextureRHITextureDesc(textureData);
		const std::string category = std::format(
			"Texture.{}",
			TextureSemanticDebugText(uploadData.m_ImportSettings.m_Semantic));
		const std::string source = TextureDebugSourcePath(
			texture->m_Source.m_CanonicalPath);
		const RHIResourceDebugIdentityDesc debugIdentity
		{
			.m_Domain = RHIResourceDebugDomain::Asset,
			.m_Category = category,
			.m_Label = texture->m_DebugLabel,
			.m_Source = source,
			.m_StableId = uploadData.m_TextureId.Value(),
		};

		texture->m_Gpu.m_Texture = m_Device->CreateTexture(textureDesc, debugIdentity);
		GGLAB_ASSERT_MSG(texture->m_Gpu.m_Texture.IsValid(), "TextureAssetSystem::UploadTexture: failed to create RHI texture.");
		if (!texture->m_Gpu.m_Texture.IsValid())
		{
			SetTextureState(
				*texture,
				AssetState::Failed,
				residencyOperation,
				operationPhase);
			return false;
		}
		texture->m_Gpu.m_Desc = textureDesc;
		texture->m_Gpu.m_Desc.m_DebugName = nullptr;
		texture->m_Source.m_ContentFingerprint = uploadData.m_ContentFingerprint;
		texture->m_Source.m_ArtifactContentDigest =
			uploadData.m_Artifact->m_ContentDigest;
		if (uploadData.m_SourceDigest.IsValid())
		{
			texture->m_Source.m_SourceDigest = uploadData.m_SourceDigest;
		}
		if (uploadData.m_DerivedDataKey.IsValid())
		{
			texture->m_Source.m_DerivedDataKey = uploadData.m_DerivedDataKey;
		}

		const RHITextureUploadData textureUploadData = textureData.MakeUploadData();
		if (!transferBatch.UploadTexture(texture->m_Gpu.m_Texture, textureUploadData))
		{
			SetTextureState(
				*texture,
				AssetState::Failed,
				residencyOperation,
				operationPhase);
			GGLAB_LOG_GRAPHICS_ERROR("TextureAssetSystem::UploadTexture failed to record the texture upload.");
			return false;
		}

		const RHITextureBarrier barrier
		{
			.m_Texture = texture->m_Gpu.m_Texture,
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

		const RHITextureViewDesc srvDesc = BuildTextureRHISRVDesc(textureData);

		texture->m_Gpu.m_Srv = m_Device->CreateTextureView(texture->m_Gpu.m_Texture, srvDesc);
		GGLAB_ASSERT_MSG(texture->m_Gpu.m_Srv.IsValid(), "TextureAssetSystem::UploadTexture: failed to create RHI texture SRV.");
		if (!texture->m_Gpu.m_Srv.IsValid())
		{
			SetTextureState(
				*texture,
				AssetState::Failed,
				residencyOperation,
				operationPhase);
			return false;
		}
		texture->m_Gpu.m_SrvDimension = textureData.m_SrvDimension;
		GGLAB_ASSERT_MSG(
			texture->m_Source.m_ImportSettings == uploadData.m_ImportSettings,
			"Uploaded texture settings do not match the texture source identity.");
		SetTextureState(
			*texture,
			AssetState::GpuProcessing,
			residencyOperation,
			operationPhase);
		return true;
	}

	void TextureAssetSystem::CompleteTextureUpload(
		TextureID textureId,
		bool succeeded,
		AssetResidencyOperation residencyOperation) noexcept
	{
		auto* texture = EditTexture(textureId);
		if (!texture)
		{
			return;
		}
		if (residencyOperation.IsValid() &&
			!AssetResidencyController::IsCurrentOperation(*texture, residencyOperation))
		{
			return;
		}
		const bool publicationOrphan = m_PublicationOrphanedTextures.contains(textureId);
		const bool cancelled = texture->m_Load.m_CancelRequested || publicationOrphan;
		const bool publishSucceeded = succeeded && !cancelled;
		const bool residencyReload = texture->m_Load.m_IsReloading;
		SetTextureState(
			*texture,
			cancelled ?
				(residencyReload ? AssetState::CpuReady : AssetState::Cancelled) :
				(publishSucceeded ? AssetState::Ready :
					(residencyReload ? AssetState::CpuReady : AssetState::Failed)),
			residencyOperation,
			ResidencyStateEventPhase(residencyOperation, residencyReload));
		ProgressReporter(texture->m_Load.m_Progress).Report(
			publishSucceeded ? 1.0f : 0.96f,
			publishSucceeded ? "Texture ready" :
				(cancelled ? "Texture upload cancelled" : "Texture GPU upload failed"),
			texture->m_DebugLabel);
		texture->m_Gpu.m_IsUploaded = publishSucceeded;
		texture->m_Load.m_IsReloading = false;
		if (residencyReload)
		{
			texture->m_Load.m_CancelRequested = false;
		}
		if (!publishSucceeded)
		{
			texture->m_Gpu.m_Srv.Reset();
			if (texture->m_Gpu.m_Texture.IsValid())
			{
				m_Device->DestroyTexture(texture->m_Gpu.m_Texture);
				texture->m_Gpu.m_Texture.Reset();
			}
			texture->m_Gpu.m_Desc = {};
			texture->m_Gpu.m_SrvDimension = RHITextureViewDimension::Unknown;
		}
		if (publicationOrphan)
		{
			GGLAB_UNUSED(RemoveTexture(textureId));
		}
	}

	bool TextureAssetSystem::QueueTextureUpload(
		TextureUploadData&& uploadData,
		TaskPriority priority,
		AssetResidencyOperation residencyOperation) noexcept
	{
		Texture* texture = EditTexture(uploadData.m_TextureId);
		if (!texture || !uploadData.m_Artifact || !uploadData.m_Artifact->IsValid())
		{
			return false;
		}
		if (residencyOperation.IsValid() &&
			!AssetResidencyController::IsCurrentOperation(*texture, residencyOperation))
		{
			return false;
		}

		const TextureID textureId = uploadData.m_TextureId;
		const uint64_t generation = texture->m_ContentGeneration;
		const AssetStreamingWorkEstimate estimate =
			EstimateTextureUpload(uploadData.m_Artifact->m_Data);
		if (texture->m_State != AssetState::Publishing)
		{
			SetTextureState(
				*texture,
				AssetState::CpuReady,
				residencyOperation,
				ResidencyStateEventPhase(residencyOperation));
		}
		ProgressReporter(texture->m_Load.m_Progress).Report(
			0.62f,
			"Waiting for texture upload admission",
			std::format("{} bytes", estimate.m_StagingBytes));
		auto payload = std::make_shared<TextureUploadData>(std::move(uploadData));
		m_AssetUploadScheduler->EnqueueUploadRecording(
			{
				.m_Name = std::format("Texture {}", textureId.Value()),
				.m_Identity = {
					.m_Kind = AssetStreamingWorkKind::Texture,
					.m_StableId = textureId.Value(),
					.m_Generation = generation,
				},
				.m_Estimate = estimate,
				.m_Priority = priority,
				.m_Progress = texture->m_Load.m_Progress,
			},
			[this, textureId, generation, priority, residencyOperation,
				payload]() mutable noexcept
			{
				Texture* currentTexture = EditTexture(textureId);
				if (!currentTexture || currentTexture->m_ContentGeneration != generation)
				{
					return;
				}
				if (residencyOperation.IsValid() &&
					!AssetResidencyController::IsCurrentOperation(
						*currentTexture,
						residencyOperation))
				{
					return;
				}
				if (currentTexture->m_Load.m_CancelRequested)
				{
					const bool residencyReload = currentTexture->m_Load.m_IsReloading;
					currentTexture->m_Load.m_IsReloading = false;
					SetTextureState(
						*currentTexture,
						residencyReload ? AssetState::CpuReady : AssetState::Cancelled,
						residencyOperation,
						ResidencyStateEventPhase(
							residencyOperation,
							residencyReload));
					return;
				}
				if (!RecordTextureUpload(
					textureId,
					generation,
					priority,
					std::move(*payload),
					residencyOperation))
				{
					const bool residencyReload = currentTexture->m_Load.m_IsReloading;
					SetTextureState(
						*currentTexture,
						residencyReload ? AssetState::CpuReady : AssetState::Failed,
						residencyOperation,
						ResidencyStateEventPhase(residencyOperation, residencyReload));
					currentTexture->m_Load.m_IsReloading = false;
					ProgressReporter(currentTexture->m_Load.m_Progress).Report(
						0.68f,
						"Texture upload recording failed");
				}
			});
		return true;
	}

	bool TextureAssetSystem::RecordTextureUpload(
		TextureID textureId,
		uint64_t generation,
		TaskPriority priority,
		TextureUploadData uploadData,
		AssetResidencyOperation residencyOperation) noexcept
	{
		Texture* texture = EditTexture(textureId);
		if (!texture || texture->m_ContentGeneration != generation)
		{
			return false;
		}
		GGLAB_ASSERT_MSG(
			texture->m_Source.m_ImportSettings == uploadData.m_ImportSettings,
			"Recorded texture upload settings do not match the source identity.");
		if (texture->m_Source.m_ImportSettings != uploadData.m_ImportSettings ||
			!uploadData.m_Artifact || !uploadData.m_Artifact->IsValid())
		{
			return false;
		}
		if (residencyOperation.IsValid() &&
			!AssetResidencyController::IsCurrentOperation(*texture, residencyOperation))
		{
			return false;
		}
		SetTextureState(
			*texture,
			AssetState::CpuReady,
			residencyOperation,
			ResidencyStateEventPhase(residencyOperation));

		const AssetStreamingWorkEstimate estimate = EstimateTextureUpload(
			uploadData.m_Artifact->m_Data);
		const AssetUploadHandle uploadHandle = m_AssetUploadScheduler->RecordUpload(
			{
				.m_Name = std::format("Texture {}", textureId.Value()),
				.m_Identity = {
					.m_Kind = AssetStreamingWorkKind::Texture,
					.m_StableId = textureId.Value(),
					.m_Generation = generation,
				},
				.m_Estimate = estimate,
				.m_Priority = priority,
				.m_Progress = texture->m_Load.m_Progress,
			},
			[this, &uploadData, residencyOperation](TransferBatch& batch) noexcept
			{
				return UploadTexture(uploadData, batch, residencyOperation);
			},
			[this, textureId, generation, residencyOperation](
				const AssetUploadCompletionInfo& completion) noexcept
			{
				const Texture* texture = GetTexture(textureId);
				if (!texture || texture->m_ContentGeneration != generation ||
					(residencyOperation.IsValid() &&
						!AssetResidencyController::IsCurrentOperation(
							*texture,
							residencyOperation)))
				{
					return;
				}
				const bool cancelled = texture->m_Load.m_CancelRequested;
				const bool succeeded = completion.m_Status == AssetUploadStatus::Succeeded;
				CompleteTextureUpload(textureId, succeeded, residencyOperation);
				if (cancelled)
				{
					GGLAB_LOG_GRAPHICS_INFO(
						"Texture {} GPU upload completed after cancellation; resources released in {:.2f} ms.",
						textureId.Value(),
						completion.m_ElapsedMilliseconds);
				}
				else if (succeeded)
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

	void TextureAssetSystem::RouteTextureDecodeCompletion(
		TextureDecodeSucceeded&& result) noexcept
	{
		if (!m_LoadCoordinator->IsCurrentTextureDecode(result.m_Operation) ||
			result.m_Operation.m_ContentVersion.m_Key.m_Kind != AssetKind::Texture)
		{
			return;
		}
		const TextureID textureId{ static_cast<uint32_t>(
			result.m_Operation.m_ContentVersion.m_Key.m_StableId) };
		const uint64_t generation =
			result.m_Operation.m_ContentVersion.m_ContentGeneration;
		const Texture* texture = GetTexture(textureId);
		if (!texture || texture->m_ContentGeneration != generation ||
			texture->m_Load.m_CancelRequested ||
			(result.m_ResidencyReload &&
				!AssetResidencyController::IsCurrentOperation(
					*texture,
					result.m_ResidencyOperation)))
		{
			m_LoadCoordinator->CompleteTextureDecode(result.m_Operation);
			return;
		}

		TextureArtifactHandle payload = std::move(result.m_Artifact);
		const AssetStreamingWorkEstimate estimate = EstimateTextureUpload(payload->m_Data);
		const AssetOperationToken operation = result.m_Operation;
		const TaskCompletionInfo completion = result.m_Completion;
		const TextureSemantic semantic = result.m_Semantic;
		const AssetContentFingerprint contentFingerprint = result.m_ContentFingerprint;
		const SourceDigest sourceDigest = result.m_SourceDigest;
		const DerivedDataKey derivedDataKey = result.m_DerivedDataKey;
		GGLAB_LOG_GRAPHICS_INFO(
			"Texture {} derived data resolved (cache={}, key={}).",
			textureId.Value(),
			result.m_DerivedDataCacheHit ? "hit" : "miss",
			DerivedDataKeyText(derivedDataKey));
		const bool residencyReload = result.m_ResidencyReload;
		const AssetResidencyOperation residencyOperation = result.m_ResidencyOperation;
		m_AssetUploadScheduler->EnqueueCpuPayload(
			{
				.m_Name = completion.m_Name,
				.m_Identity = {
					.m_Kind = AssetStreamingWorkKind::Texture,
					.m_StableId = textureId.Value(),
					.m_Generation = generation,
				},
				.m_Estimate = estimate,
				.m_Priority = completion.m_Priority,
				.m_Progress = texture->m_Load.m_Progress,
			},
			[this, operation, semantic, completion, payload, contentFingerprint,
				sourceDigest, derivedDataKey,
				residencyReload, residencyOperation]() mutable noexcept
			{
				CompleteTextureLoad(
					operation,
					semantic,
					completion,
					std::move(payload),
					contentFingerprint,
					sourceDigest,
					derivedDataKey,
					residencyReload,
					residencyOperation);
			});
	}

	void TextureAssetSystem::RouteTextureDecodeCompletion(
		TextureDecodeFailed&& result) noexcept
	{
		CompleteTextureLoad(
			result.m_Operation,
			result.m_Semantic,
			result.m_Completion,
			{},
			{},
			{},
			{},
			result.m_ResidencyReload,
			result.m_ResidencyOperation);
	}

	void TextureAssetSystem::CompleteTextureLoad(
		AssetOperationToken operation,
		TextureSemantic semantic,
		const TaskCompletionInfo& completion,
		TextureArtifactHandle artifact,
		AssetContentFingerprint contentFingerprint,
		SourceDigest sourceDigest,
		DerivedDataKey derivedDataKey,
		bool residencyReload,
		AssetResidencyOperation residencyOperation) noexcept
	{
		if (!m_LoadCoordinator->IsCurrentTextureDecode(operation) ||
			operation.m_ContentVersion.m_Key.m_Kind != AssetKind::Texture)
		{
			return;
		}
		m_LoadCoordinator->CompleteTextureDecode(operation);
		const TextureID textureId{ static_cast<uint32_t>(
			operation.m_ContentVersion.m_Key.m_StableId) };
		const uint64_t generation = operation.m_ContentVersion.m_ContentGeneration;
		Texture* texture = EditTexture(textureId);
		if (!texture || texture->m_ContentGeneration != generation)
		{
			return;
		}
		GGLAB_ASSERT_MSG(
			texture->m_Source.m_ImportSettings.m_Semantic == semantic,
			"Decoded texture semantic does not match the texture source identity.");
		if (texture->m_Source.m_ImportSettings.m_Semantic != semantic)
		{
			SetTextureState(
				*texture,
				residencyReload ? AssetState::CpuReady : AssetState::Failed,
				residencyOperation,
				ResidencyStateEventPhase(residencyOperation, residencyReload));
			texture->m_Load.m_IsReloading = false;
			return;
		}
		if (residencyReload &&
			!AssetResidencyController::IsCurrentOperation(*texture, residencyOperation))
		{
			return;
		}
		if (texture->m_Load.m_CancelRequested)
		{
			SetTextureState(
				*texture,
				residencyReload ? AssetState::CpuReady : AssetState::Cancelled,
				residencyOperation,
				ResidencyStateEventPhase(residencyOperation, residencyReload));
			texture->m_Load.m_IsReloading = false;
			ProgressReporter(texture->m_Load.m_Progress).Report(
				0.05f,
				"Texture loading cancelled",
				completion.m_Name);
			return;
		}

		if (completion.m_Status == TaskStatus::Cancelled)
		{
			SetTextureState(
				*texture,
				residencyReload ? AssetState::CpuReady : AssetState::Cancelled,
				residencyOperation,
				ResidencyStateEventPhase(residencyOperation, residencyReload));
			texture->m_Load.m_IsReloading = false;
			ProgressReporter(texture->m_Load.m_Progress).Report(
				0.05f,
				"Texture loading cancelled",
				completion.m_Name);
			return;
		}
		if (completion.m_Status != TaskStatus::Succeeded)
		{
			SetTextureState(
				*texture,
				residencyReload ? AssetState::CpuReady : AssetState::Failed,
				residencyOperation,
				ResidencyStateEventPhase(residencyOperation, residencyReload));
			texture->m_Load.m_IsReloading = false;
			ProgressReporter(texture->m_Load.m_Progress).Report(
				0.05f,
				"Texture decoding failed",
				completion.m_Error);
			GGLAB_LOG_GRAPHICS_ERROR(
				"Async texture decode '{}' failed: {}",
				completion.m_Name,
				completion.m_Error);
			return;
		}
		if (residencyReload &&
			texture->m_Source.m_ArtifactContentDigest.IsValid() &&
			(!artifact || artifact->m_ContentDigest !=
				texture->m_Source.m_ArtifactContentDigest))
		{
			const ArtifactContentDigest actualDigest = artifact ?
				artifact->m_ContentDigest : ArtifactContentDigest{};
			SetTextureState(
				*texture,
				AssetState::CpuReady,
				residencyOperation,
				AssetStateEventOperationPhase::Completes);
			texture->m_Load.m_IsReloading = false;
			GGLAB_LOG_GRAPHICS_ERROR(
				"Rejected texture residency reload for immutable generation {} (expected artifact {}, resolved artifact {}).",
				generation,
				ArtifactContentDigestText(texture->m_Source.m_ArtifactContentDigest),
				ArtifactContentDigestText(actualDigest));
			return;
		}

		SetTextureState(
			*texture,
			AssetState::CpuReady,
			residencyOperation,
			ResidencyStateEventPhase(residencyOperation));
		ProgressReporter(texture->m_Load.m_Progress).Report(
			0.62f,
			"Queued for texture upload publication",
			completion.m_Name);
		auto uploadData = MakeTextureUploadData(
			textureId,
			std::move(artifact),
			texture->m_Source.m_ImportSettings,
			contentFingerprint,
			sourceDigest,
			derivedDataKey);
		if (!QueueTextureUpload(
			std::move(uploadData),
			completion.m_Priority,
			residencyOperation))
		{
			SetTextureState(
				*texture,
				residencyReload ? AssetState::CpuReady : AssetState::Failed,
				residencyOperation,
				ResidencyStateEventPhase(residencyOperation, residencyReload));
			texture->m_Load.m_IsReloading = false;
			ProgressReporter(texture->m_Load.m_Progress).Report(
				0.62f,
				"Texture upload queueing failed");
			return;
		}
		GGLAB_LOG_GRAPHICS_INFO(
			"Async texture {} queued for upload admission (queueMs={:.2f}, cpuMs={:.2f}).",
			textureId.Value(),
			completion.m_QueueMilliseconds,
			completion.m_ExecutionMilliseconds);
	}

	void TextureAssetSystem::CancelTextureIfUnreferenced(
		TextureID textureId,
		uint64_t generation) noexcept
	{
		Texture* texture = EditTexture(textureId);
		if (!texture || texture->m_ContentGeneration != generation ||
			texture->m_State == AssetState::Ready ||
			texture->m_State == AssetState::Failed ||
			texture->m_State == AssetState::Cancelled)
		{
			return;
		}

		texture->m_Load.m_CancelRequested = true;
		const AssetContentVersion contentVersion =
			MakeAssetContentVersion(textureId, generation);
		GGLAB_UNUSED(m_LoadCoordinator->CancelTextureDecode(contentVersion));
		// A successful decode remains registered until its CPU payload runs.
		// Cancelling that payload bypasses CompleteTextureLoad, so retire the
		// coordinator operation explicitly before removing ready work.
		m_LoadCoordinator->DiscardTextureDecode(contentVersion.m_Key);
		GGLAB_UNUSED(m_AssetUploadScheduler->CancelReadyWork(contentVersion));
		if (texture->m_Load.m_IsReloading && !texture->m_Gpu.m_Texture.IsValid())
		{
			texture->m_Load.m_IsReloading = false;
			AssetResidencyController::InvalidateResidencyOperation(*texture);
			SetTextureState(*texture, AssetState::CpuReady);
			ProgressReporter(texture->m_Load.m_Progress).Report(
				1.0f,
				"Texture residency reload cancelled",
				texture->m_DebugLabel);
			return;
		}
		SetTextureState(
			*texture,
			texture->m_Gpu.m_Texture.IsValid() ?
				AssetState::GpuProcessing : AssetState::Cancelled);
		ProgressReporter(texture->m_Load.m_Progress).Report(
			0.96f,
			texture->m_Gpu.m_Texture.IsValid() ?
				"Texture cancellation pending GPU completion" : "Texture loading cancelled",
			texture->m_DebugLabel);
	}

	void TextureAssetSystem::RollbackPublicationTexture(
		TextureID textureId,
		uint64_t generation) noexcept
	{
		Texture* texture = EditTexture(textureId);
		if (!texture || texture->m_ContentGeneration != generation ||
			IsReservedTextureId(textureId))
		{
			return;
		}

		texture->m_Load.m_CancelRequested = true;
		GGLAB_UNUSED(m_AssetUploadScheduler->CancelReadyWork(
			MakeAssetContentVersion(textureId, generation)));
		if (texture->m_Gpu.m_Texture.IsValid() && texture->m_State != AssetState::Ready)
		{
			m_PublicationOrphanedTextures.insert(textureId);
			SetTextureState(*texture, AssetState::GpuProcessing);
			ProgressReporter(texture->m_Load.m_Progress).Report(
				0.96f,
				"Texture publication rollback pending GPU completion",
				texture->m_DebugLabel);
			return;
		}

		GGLAB_UNUSED(RemoveTexture(textureId));
	}

	void TextureAssetSystem::UpdateTextureLoadPriority(
		TextureID textureId,
		uint64_t generation,
		TaskPriority priority) noexcept
	{
		const Texture* texture = GetTexture(textureId);
		if (!texture || texture->m_ContentGeneration != generation)
		{
			return;
		}
		GGLAB_UNUSED(m_LoadCoordinator->UpdateTextureDecodePriority(
			MakeAssetContentVersion(textureId, generation),
			priority));
		GGLAB_UNUSED(m_AssetUploadScheduler->UpdateWorkPriority(
			MakeAssetContentVersion(textureId, generation),
			priority));
	}

	void TextureAssetSystem::ReviveTextureInterest(
		TextureID textureId,
		uint64_t generation) noexcept
	{
		Texture* texture = EditTexture(textureId);
		if (!texture || texture->m_ContentGeneration != generation ||
			!texture->m_Load.m_CancelRequested || texture->m_State != AssetState::GpuProcessing)
		{
			return;
		}
		texture->m_Load.m_CancelRequested = false;
		ProgressReporter(texture->m_Load.m_Progress).Report(
			0.82f,
			"Texture cancellation rescinded",
			"A new owner acquired the in-flight texture");
	}

	TaskHandle TextureAssetSystem::RequestTextureResidency(
		TextureID textureId,
		AssetResidencyOperation operation,
		TaskPriority priority) noexcept
	{
		Texture* texture = EditTexture(textureId);
		if (!texture || operation.m_Kind != AssetResidencyOperationKind::Reload ||
			operation.m_Token.m_ContentVersion.m_Key != MakeAssetKey(textureId) ||
			!AssetResidencyController::IsCurrentOperation(*texture, operation) ||
			IsReservedTextureId(textureId))
		{
			return {};
		}
		if (texture->m_State != AssetState::CpuReady || texture->m_ResidencyEpoch == 0)
		{
			return {};
		}
		if (texture->m_Load.m_IsReloading)
		{
			return m_LoadCoordinator->GetTextureDecodeTask(
				MakeAssetContentVersion(textureId, texture->m_ContentGeneration));
		}
		const AssetContentVersion contentVersion = MakeAssetContentVersion(
			textureId,
			texture->m_ContentGeneration);
		GGLAB_UNUSED(m_LoadCoordinator->CancelTextureDecode(contentVersion));
		m_LoadCoordinator->DiscardTextureDecode(contentVersion.m_Key);

		texture->m_Load.m_CancelRequested = false;
		texture->m_Load.m_IsReloading = true;
		if (TextureArtifactHandle artifact = m_ArtifactCache->Find(
			texture->m_Source.m_ArtifactContentDigest))
		{
			ProgressReporter(texture->m_Load.m_Progress).Report(
				0.62f,
				"Texture CPU artifact cache hit",
				ArtifactContentDigestText(artifact->m_ContentDigest));
			TextureUploadData uploadData = MakeTextureUploadData(
				textureId,
				std::move(artifact),
				texture->m_Source.m_ImportSettings,
				texture->m_Source.m_ContentFingerprint,
				texture->m_Source.m_SourceDigest,
				texture->m_Source.m_DerivedDataKey);
			if (QueueTextureUpload(
				std::move(uploadData),
				priority,
				operation))
			{
				return {};
			}
		}

		if (texture->m_Source.m_CanonicalPath.empty())
		{
			texture->m_Load.m_IsReloading = false;
			SetTextureState(
				*texture,
				AssetState::CpuReady,
				operation,
				AssetStateEventOperationPhase::Completes);
			ProgressReporter(texture->m_Load.m_Progress).Report(
				0.05f,
				"Texture residency reload unavailable",
				"CPU artifact cache miss and no source path");
			return {};
		}
		const TaskHandle task = QueueTextureLoad(
			textureId,
			texture->m_Source.m_CanonicalPath,
			texture->m_Source.m_ImportSettings,
			priority,
			true,
			operation);
		if (!task.IsValid())
		{
			texture->m_Load.m_IsReloading = false;
		}
		return task;
	}

	bool TextureAssetSystem::FinalizeTextureEviction(
		TextureID textureId,
		AssetResidencyOperation operation) noexcept
	{
		Texture* texture = EditTexture(textureId);
		if (!texture || operation.m_Kind != AssetResidencyOperationKind::Evict ||
			operation.m_Token.m_ContentVersion.m_Key != MakeAssetKey(textureId) ||
			!AssetResidencyController::IsCurrentOperation(*texture, operation) ||
			texture->m_State != AssetState::Evicting)
		{
			return false;
		}

		texture->m_Gpu.m_Srv.Reset();
		if (texture->m_Gpu.m_Texture.IsValid())
		{
			m_Device->DestroyTexture(texture->m_Gpu.m_Texture);
			texture->m_Gpu.m_Texture.Reset();
		}
		texture->m_Gpu.m_IsUploaded = false;
		texture->m_Gpu.m_SrvDimension = RHITextureViewDimension::Unknown;
		SetTextureState(
			*texture,
			AssetState::CpuReady,
			operation,
			AssetStateEventOperationPhase::Completes);
		ProgressReporter(texture->m_Load.m_Progress).Report(
			1.0f,
			"Texture GPU residency released",
			texture->m_DebugLabel);
		return true;
	}

	void TextureAssetSystem::CreateTextureEntry(
		TextureID id,
		std::string_view textureName,
		TextureImportSettings importSettings,
		const std::filesystem::path& sourcePath) noexcept
	{
		if (GetTexture(id) == nullptr)
		{
			auto texture = std::make_unique<Texture>();
			Texture* insertedTexture = texture.get();
			if (m_Store.Insert(id, std::move(texture)))
			{
				insertedTexture->m_Id = id;
				BeginAssetContentGeneration(
					*insertedTexture,
					1,
					AssetState::CpuReady,
					IsReservedTextureId(id) ?
						AssetResidencyPolicy::Pinned : AssetResidencyPolicy::Cacheable);
				insertedTexture->m_Name = StringID(textureName);
				insertedTexture->m_Source = {
					.m_CanonicalPath = sourcePath,
					.m_ImportSettings = importSettings,
				};
				insertedTexture->m_DebugLabel = textureName;
				insertedTexture->m_Load.m_Progress = std::make_shared<ProgressChannel>();
				insertedTexture->m_Gpu.m_IsUploaded = false;
				insertedTexture->m_Gpu.m_Srv.Reset();
				insertedTexture->m_Gpu.m_Texture.Reset();
				insertedTexture->m_Gpu.m_Desc = {};
			}
		}
	}
}
