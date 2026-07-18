#include "Core/Precompiled.h"
#include "Graphics/Asset/Residency/AssetResidencyController.h"
#include "Graphics/TextureAssetSystem.h"
#include "Graphics/Asset/AssetIdentityConversions.h"
#include "Graphics/Asset/BuiltinTextureFactory.h"
#include "Graphics/AssetUploadScheduler.h"
#include "Graphics/TransferManager.h"
#include "Graphics/RHI/RHIDevice.h"
#include "Graphics/TextureLoader.h"
#include "Graphics/Utility/TextureUtils.h"
#include "Core/Task/TaskSystem.h"
#include "Core/Utility/PathUtils.h"
#include "Core/Utility/TypeUtils.h"

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
		m_TaskSystem(createInfo.m_TaskSystem),
		m_TransferManager(createInfo.m_TransferManager),
		m_AssetUploadScheduler(createInfo.m_AssetUploadScheduler)
	{
		GGLAB_ASSERT_MSG(m_Device != nullptr, "RHIDevice is null!");
		GGLAB_ASSERT_MSG(m_TaskSystem != nullptr, "TaskSystem is null!");
		GGLAB_ASSERT_MSG(m_TransferManager != nullptr, "TransferManager is null!");
		GGLAB_ASSERT_MSG(m_AssetUploadScheduler != nullptr, "AssetUploadScheduler is null!");
	}

	void TextureAssetSystem::SetStateChangeCallback(
		std::function<void(
			TextureID,
			uint64_t,
			AssetContentState,
			AssetResidencyState,
			std::optional<AssetOperationToken>,
			AssetStateEventOperationPhase)> callback) noexcept
	{
		m_StateChangeCallback = std::move(callback);
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
			operationPhase == AssetStateEventOperationPhase::Completes) &&
			m_StateChangeCallback)
		{
			m_StateChangeCallback(
				texture.m_Id,
				texture.m_ContentGeneration,
				texture.m_ContentState,
				texture.m_ResidencyState,
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
			CreateTextureEntry(id, builtinTexture.m_Name);
			uploads.emplace_back(MakeTextureUploadData(
				id,
				std::move(builtinTexture.m_Data),
				builtinTexture.m_Semantic));
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
				CreateTextureEntry(textureId, name, canonicalPath);
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

	void TextureAssetSystem::Finalize(const RHIFencePoint& fencePoint) noexcept
	{
		GGLAB_ASSERT_MSG(
			m_PublicationOrphanedTextures.empty(),
			"TextureAssetSystem finalized while publication rollback is pending GPU completion.");
		for (const auto& texture : m_Store.Entries() | std::views::values)
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
				const auto task = m_TextureLoadTasks.find(existing);
				const bool currentTask = task != m_TextureLoadTasks.end() &&
					task->second.m_Generation == texture->m_ContentGeneration &&
					(!task->second.m_ResidencyOperation.IsValid() ||
						(texture->m_IsReloading &&
							AssetResidencyController::IsCurrentOperation(
								*texture,
								task->second.m_ResidencyOperation)));
				return {
					.m_TextureId = existing,
					.m_Generation = texture->m_ContentGeneration,
					.m_Task = currentTask ? task->second.m_Task : TaskHandle{},
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
			.m_Generation = GetTexture(textureId)->m_ContentGeneration,
			.m_Task = task,
		};
	}

	TaskHandle TextureAssetSystem::QueueTextureLoad(
		TextureID textureId,
		const std::filesystem::path& canonicalPath,
		const TextureImportSettings& importSettings,
		TextureSemantic semantic,
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
		if (const auto existing = m_TextureLoadTasks.find(textureId);
			existing != m_TextureLoadTasks.end())
		{
			const bool matchingResidencyOperation = residencyReload ?
				existing->second.m_ResidencyOperation == residencyOperation :
				!existing->second.m_ResidencyOperation.IsValid();
			if (existing->second.m_Generation == texture->m_ContentGeneration &&
				matchingResidencyOperation)
			{
				return existing->second.m_Task;
			}
			GGLAB_UNUSED(m_TaskSystem->Cancel(existing->second.m_Task));
			m_TextureLoadTasks.erase(existing);
		}

		if (!residencyReload)
		{
			SetTextureState(*texture, AssetState::Queued);
		}
		const uint64_t generation = texture->m_ContentGeneration;
		texture->m_Semantic = semantic;
		ProgressReporter(texture->m_LoadProgress).Report(
			0.05f,
			"Queued for texture decoding",
			canonicalPath.filename().generic_string());
		const uint64_t loadSerial = m_NextTextureLoadSerial++;
		GGLAB_ASSERT_MSG(loadSerial != 0, "Texture load operation serial overflowed.");
		if (loadSerial == 0)
		{
			if (residencyReload)
			{
				texture->m_IsReloading = false;
				SetTextureState(
					*texture,
					AssetState::CpuReady,
					residencyOperation,
					AssetStateEventOperationPhase::Completes);
			}
			return {};
		}
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
			[this, textureId, generation, loadSerial, semantic, residencyReload,
				residencyOperation, job, progress = texture->m_LoadProgress](
				const TaskCompletionInfo& completion) noexcept
			{
				if (!IsCurrentTextureLoadOperation(
					textureId,
					generation,
					loadSerial))
				{
					return;
				}
				const Texture* currentTexture = EditTexture(textureId);
				if (!currentTexture || currentTexture->m_ContentGeneration != generation ||
					currentTexture->m_CancelRequested ||
					(residencyReload &&
						!AssetResidencyController::IsCurrentOperation(
							*currentTexture,
							residencyOperation)))
				{
					CompleteTextureLoadOperation(
						textureId,
						generation,
						loadSerial);
					return;
				}
				m_AssetUploadScheduler->EnqueueCpuPayload(
					{
						.m_Name = completion.m_Name,
						.m_Identity = {
							.m_Kind = AssetStreamingWorkKind::Texture,
							.m_StableId = textureId.Value(),
							.m_Generation = generation,
						},
						.m_Estimate = EstimateTextureUpload(job->m_TextureData),
						.m_Priority = completion.m_Priority,
						.m_Progress = progress,
					},
					[this, textureId, generation, loadSerial, semantic, completion,
						residencyReload, residencyOperation, job]() mutable noexcept
					{
						CompleteTextureLoad(
							textureId,
							generation,
							loadSerial,
							semantic,
							completion,
							std::move(job->m_TextureData),
							residencyReload,
							residencyOperation);
					});
			});
		if (!task.IsValid())
		{
			SetTextureState(
				*texture,
				residencyReload ? AssetState::CpuReady : AssetState::Failed,
				residencyOperation,
				ResidencyStateEventPhase(residencyOperation, true));
			texture->m_IsReloading = false;
			ProgressReporter(texture->m_LoadProgress).Report(
				0.05f,
				"Texture decode submission failed",
				canonicalPath.filename().generic_string());
			return {};
		}

		const auto [operation, inserted] = m_TextureLoadTasks.emplace(
			textureId,
			TextureLoadOperationRecord{
				.m_Task = task,
				.m_Generation = generation,
				.m_LoadSerial = loadSerial,
				.m_ResidencyOperation = residencyOperation,
			});
		GGLAB_ASSERT(inserted);
		GGLAB_UNUSED(operation);
		return task;
	}

	bool TextureAssetSystem::IsCurrentTextureLoadOperation(
		TextureID textureId,
		uint64_t generation,
		uint64_t loadSerial) const noexcept
	{
		const auto operation = m_TextureLoadTasks.find(textureId);
		return operation != m_TextureLoadTasks.end() &&
			operation->second.m_Generation == generation &&
			operation->second.m_LoadSerial == loadSerial;
	}

	void TextureAssetSystem::CompleteTextureLoadOperation(
		TextureID textureId,
		uint64_t generation,
		uint64_t loadSerial) noexcept
	{
		const auto operation = m_TextureLoadTasks.find(textureId);
		if (operation != m_TextureLoadTasks.end() &&
			operation->second.m_Generation == generation &&
			operation->second.m_LoadSerial == loadSerial)
		{
			m_TextureLoadTasks.erase(operation);
		}
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
		controller.RecordReloadRequest(texture->m_IsReloading);
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
		if (texture->m_IsReloading)
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

	const RHITextureDesc* TextureAssetSystem::GetTextureDesc(TextureID textureId) const noexcept
	{
		const auto* texture = GetTexture(textureId);
		return texture && texture->m_IsUploaded ? &texture->m_Desc : nullptr;
	}

	RHIDescriptorHandle TextureAssetSystem::GetSrvDescriptor(TextureID textureId) const noexcept
	{
		const auto* texture = GetTexture(textureId);
		if (!texture || !texture->m_IsUploaded || !texture->m_Srv.IsValid())
		{
			return {};
		}

		return m_Device->GetTextureViewDescriptor(texture->m_Srv);
	}

	uint32_t TextureAssetSystem::GetShaderVisibleSrvIndex(TextureID textureId) const noexcept
	{
		const RHIDescriptorHandle descriptor = GetSrvDescriptor(textureId);
		GGLAB_ASSERT_MSG(
			descriptor.IsValid() && descriptor.m_HeapType == RHIDescriptorHeapType::CbvSrvUav,
			"TextureAssetSystem::GetShaderVisibleSrvIndex: texture SRV descriptor is invalid.");
		return descriptor.m_Index;
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
			if (texture->m_IsUploaded && texture->m_Srv.IsValid() &&
				texture->m_ResidencyState == AssetResidencyState::Resident)
			{
				return resolveSrvIndex(texture->m_Srv);
			}
		}

		const auto fallbackId = ToTextureId(fallback);
		const auto* fallbackTexture = GetTexture(fallbackId);

		GGLAB_ASSERT(fallbackTexture != nullptr && fallbackTexture->m_Srv.IsValid());

		return resolveSrvIndex(fallbackTexture->m_Srv);
	}

	TextureContentRef TextureAssetSystem::GetTextureContentRef(TextureID textureId) const noexcept
	{
		const Texture* texture = GetTexture(textureId);
		return texture ? TextureContentRef{
			.m_Id = textureId,
			.m_Generation = texture->m_ContentGeneration,
		} : TextureContentRef{};
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
			!texture->m_IsUploaded || !texture->m_Texture.IsValid() ||
			!texture->m_Srv.IsValid())
		{
			return std::nullopt;
		}

		const RHIDescriptorHandle descriptor =
			m_Device->GetTextureViewDescriptor(texture->m_Srv);
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
			.m_Texture = texture->m_Texture,
			.m_Desc = texture->m_Desc,
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
				.m_SourcePath = texture->m_SourcePath,
				.m_Semantic = texture->m_Semantic,
				.m_Name = texture->m_Name,
				.m_Texture = texture->m_Texture,
				.m_DebugName = m_Device ?
					std::string(m_Device->GetTextureDebugName(texture->m_Texture)) :
					std::string{},
				.m_IsUploaded = texture->m_IsUploaded,
				.m_IsReserved = IsReservedTextureId(textureId),
			});
		}
		return infos;
	}

	uint32_t TextureAssetSystem::GetReloadingTextureCount() const noexcept
	{
		return static_cast<uint32_t>(std::ranges::count_if(
			m_Store.Entries() | std::views::values,
			[](const auto& texture) noexcept { return texture->m_IsReloading; }));
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
		insertedTexture->m_SourcePath = canonicalPath;
		insertedTexture->m_DebugLabel = canonicalPath.filename().generic_string();
		insertedTexture->m_LoadProgress = std::make_shared<ProgressChannel>();

		return textureId;
	}

	bool TextureAssetSystem::RemoveTexture(TextureID textureId) noexcept
	{
		if (!textureId.IsValid() || IsReservedTextureId(textureId))
		{
			return false;
		}

		m_TextureLoadTasks.erase(textureId);
		m_PublicationOrphanedTextures.erase(textureId);
		if (Texture* texture = m_Store.Edit(textureId))
		{
			if (texture->m_Texture.IsValid())
			{
				m_Device->DestroyTexture(texture->m_Texture);
			}
		}
		return m_Store.Remove(textureId);
	}

	TextureID TextureAssetSystem::FindTexture(const std::filesystem::path& canonicalPath,
		const TextureImportSettings& importSettings) const noexcept
	{
		return m_Store.FindCached(canonicalPath, importSettings);
	}

	TextureAssetSystem::TextureUploadData TextureAssetSystem::MakeTextureUploadData(TextureID textureId,
		TextureAssetData&& textureData, TextureSemantic semantic) noexcept
	{
		TextureUploadData uploadData{};
		uploadData.m_TextureId = textureId;
		uploadData.m_Semantic = semantic;
		uploadData.m_ColorSpace = GetTextureColorSpaceFromSemantic(semantic);
		uploadData.m_TextureData = std::move(textureData);
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
		const AssetStateEventOperationPhase operationPhase =
			ResidencyStateEventPhase(residencyOperation);
		SetTextureState(
			*texture,
			AssetState::UploadQueued,
			residencyOperation,
			operationPhase);

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
			SetTextureState(
				*texture,
				AssetState::Failed,
				residencyOperation,
				operationPhase);
			GGLAB_LOG_GRAPHICS_ERROR("TextureAssetSystem::UploadTexture received invalid texture asset data.");
			return false;
		}
		if (texture->m_Texture.IsValid() || texture->m_IsUploaded)
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
		GGLAB_ASSERT_MSG(texture->m_Texture.IsValid(), "TextureAssetSystem::UploadTexture: failed to create RHI texture.");
		if (!texture->m_Texture.IsValid())
		{
			SetTextureState(
				*texture,
				AssetState::Failed,
				residencyOperation,
				operationPhase);
			return false;
		}
		texture->m_Desc = textureDesc;
		texture->m_Desc.m_DebugName = nullptr;

		const RHITextureUploadData textureUploadData = textureData.MakeUploadData();
		if (!transferBatch.UploadTexture(texture->m_Texture, textureUploadData))
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
		GGLAB_ASSERT_MSG(texture->m_Srv.IsValid(), "TextureAssetSystem::UploadTexture: failed to create RHI texture SRV.");
		if (!texture->m_Srv.IsValid())
		{
			SetTextureState(
				*texture,
				AssetState::Failed,
				residencyOperation,
				operationPhase);
			return false;
		}
		texture->m_SrvDimension = textureData.m_SrvDimension;
		texture->m_Semantic = uploadData.m_Semantic;
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
		const bool cancelled = texture->m_CancelRequested || publicationOrphan;
		const bool publishSucceeded = succeeded && !cancelled;
		const bool residencyReload = texture->m_IsReloading;
		SetTextureState(
			*texture,
			cancelled ?
				(residencyReload ? AssetState::CpuReady : AssetState::Cancelled) :
				(publishSucceeded ? AssetState::Ready :
					(residencyReload ? AssetState::CpuReady : AssetState::Failed)),
			residencyOperation,
			ResidencyStateEventPhase(residencyOperation, residencyReload));
		ProgressReporter(texture->m_LoadProgress).Report(
			publishSucceeded ? 1.0f : 0.96f,
			publishSucceeded ? "Texture ready" :
				(cancelled ? "Texture upload cancelled" : "Texture GPU upload failed"),
			texture->m_DebugLabel);
		texture->m_IsUploaded = publishSucceeded;
		texture->m_IsReloading = false;
		if (residencyReload)
		{
			texture->m_CancelRequested = false;
		}
		if (!publishSucceeded)
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
		if (!texture || !uploadData.m_TextureData.IsValid())
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
			EstimateTextureUpload(uploadData.m_TextureData);
		if (texture->m_State != AssetState::Publishing)
		{
			SetTextureState(
				*texture,
				AssetState::CpuReady,
				residencyOperation,
				ResidencyStateEventPhase(residencyOperation));
		}
		ProgressReporter(texture->m_LoadProgress).Report(
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
				.m_Progress = texture->m_LoadProgress,
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
				if (currentTexture->m_CancelRequested)
				{
					const bool residencyReload = currentTexture->m_IsReloading;
					currentTexture->m_IsReloading = false;
					SetTextureState(
						*currentTexture,
						residencyReload ? AssetState::CpuReady : AssetState::Cancelled,
						residencyOperation,
						ResidencyStateEventPhase(
							residencyOperation,
							residencyReload));
					return;
				}
				if (!PublishImportedTexture(
					textureId,
					generation,
					payload->m_Semantic,
					priority,
					std::move(payload->m_TextureData),
					residencyOperation))
				{
					const bool residencyReload = currentTexture->m_IsReloading;
					SetTextureState(
						*currentTexture,
						residencyReload ? AssetState::CpuReady : AssetState::Failed,
						residencyOperation,
						ResidencyStateEventPhase(residencyOperation, residencyReload));
					currentTexture->m_IsReloading = false;
					ProgressReporter(currentTexture->m_LoadProgress).Report(
						0.68f,
						"Texture upload recording failed");
				}
			});
		return true;
	}

	bool TextureAssetSystem::PublishImportedTexture(
		TextureID textureId,
		uint64_t generation,
		TextureSemantic semantic,
		TaskPriority priority,
		TextureAssetData&& textureData,
		AssetResidencyOperation residencyOperation) noexcept
	{
		Texture* texture = EditTexture(textureId);
		if (!texture || texture->m_ContentGeneration != generation)
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

		const AssetStreamingWorkEstimate estimate = EstimateTextureUpload(textureData);
		auto uploadData = MakeTextureUploadData(
			textureId,
			std::move(textureData),
			semantic);
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
				.m_Progress = texture->m_LoadProgress,
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
				const bool cancelled = texture->m_CancelRequested;
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

	void TextureAssetSystem::CompleteTextureLoad(
		TextureID textureId,
		uint64_t generation,
		uint64_t loadSerial,
		TextureSemantic semantic,
		const TaskCompletionInfo& completion,
		TextureAssetData&& textureData,
		bool residencyReload,
		AssetResidencyOperation residencyOperation) noexcept
	{
		Texture* texture = EditTexture(textureId);
		if (!texture || texture->m_ContentGeneration != generation)
		{
			return;
		}
		if (!IsCurrentTextureLoadOperation(textureId, generation, loadSerial))
		{
			return;
		}
		CompleteTextureLoadOperation(textureId, generation, loadSerial);
		if (residencyReload &&
			!AssetResidencyController::IsCurrentOperation(*texture, residencyOperation))
		{
			return;
		}
		if (texture->m_CancelRequested)
		{
			SetTextureState(
				*texture,
				residencyReload ? AssetState::CpuReady : AssetState::Cancelled,
				residencyOperation,
				ResidencyStateEventPhase(residencyOperation, residencyReload));
			texture->m_IsReloading = false;
			ProgressReporter(texture->m_LoadProgress).Report(
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
			texture->m_IsReloading = false;
			ProgressReporter(texture->m_LoadProgress).Report(
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
			texture->m_IsReloading = false;
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

		SetTextureState(
			*texture,
			AssetState::CpuReady,
			residencyOperation,
			ResidencyStateEventPhase(residencyOperation));
		ProgressReporter(texture->m_LoadProgress).Report(
			0.62f,
			"Queued for texture upload publication",
			completion.m_Name);
		auto uploadData = MakeTextureUploadData(
			textureId,
			std::move(textureData),
			semantic);
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
			texture->m_IsReloading = false;
			ProgressReporter(texture->m_LoadProgress).Report(
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

		texture->m_CancelRequested = true;
		if (const auto task = m_TextureLoadTasks.find(textureId);
			task != m_TextureLoadTasks.end() &&
			task->second.m_Generation == generation)
		{
			GGLAB_UNUSED(m_TaskSystem->Cancel(task->second.m_Task));
		}
		GGLAB_UNUSED(m_AssetUploadScheduler->CancelReadyWork(
			MakeAssetContentVersion(textureId, generation)));
		if (texture->m_IsReloading && !texture->m_Texture.IsValid())
		{
			texture->m_IsReloading = false;
			AssetResidencyController::InvalidateResidencyOperation(*texture);
			SetTextureState(*texture, AssetState::CpuReady);
			ProgressReporter(texture->m_LoadProgress).Report(
				1.0f,
				"Texture residency reload cancelled",
				texture->m_DebugLabel);
			return;
		}
		SetTextureState(
			*texture,
			texture->m_Texture.IsValid() ?
				AssetState::GpuProcessing : AssetState::Cancelled);
		ProgressReporter(texture->m_LoadProgress).Report(
			0.96f,
			texture->m_Texture.IsValid() ?
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

		texture->m_CancelRequested = true;
		GGLAB_UNUSED(m_AssetUploadScheduler->CancelReadyWork(
			MakeAssetContentVersion(textureId, generation)));
		if (texture->m_Texture.IsValid() && texture->m_State != AssetState::Ready)
		{
			m_PublicationOrphanedTextures.insert(textureId);
			SetTextureState(*texture, AssetState::GpuProcessing);
			ProgressReporter(texture->m_LoadProgress).Report(
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
		if (const auto task = m_TextureLoadTasks.find(textureId);
			task != m_TextureLoadTasks.end() &&
			task->second.m_Generation == generation)
		{
			GGLAB_UNUSED(m_TaskSystem->UpdatePriority(task->second.m_Task, priority));
		}
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
			!texture->m_CancelRequested || texture->m_State != AssetState::GpuProcessing)
		{
			return;
		}
		texture->m_CancelRequested = false;
		ProgressReporter(texture->m_LoadProgress).Report(
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
		if (texture->m_State != AssetState::CpuReady || texture->m_ResidencyEpoch == 0 ||
			texture->m_SourcePath.empty())
		{
			return {};
		}
		if (texture->m_IsReloading)
		{
			if (const auto task = m_TextureLoadTasks.find(textureId);
				task != m_TextureLoadTasks.end() &&
				task->second.m_Generation == texture->m_ContentGeneration &&
				task->second.m_ResidencyOperation == operation)
			{
				return task->second.m_Task;
			}
			return {};
		}
		if (const auto staleTask = m_TextureLoadTasks.find(textureId);
			staleTask != m_TextureLoadTasks.end())
		{
			GGLAB_UNUSED(m_TaskSystem->Cancel(staleTask->second.m_Task));
			m_TextureLoadTasks.erase(staleTask);
		}

		texture->m_CancelRequested = false;
		texture->m_IsReloading = true;
		const TextureImportSettings importSettings =
			MakeTextureImportSettings(texture->m_Semantic);
		const TaskHandle task = QueueTextureLoad(
			textureId,
			texture->m_SourcePath,
			importSettings,
			texture->m_Semantic,
			priority,
			true,
			operation);
		if (!task.IsValid())
		{
			texture->m_IsReloading = false;
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

		texture->m_Srv.Reset();
		if (texture->m_Texture.IsValid())
		{
			m_Device->DestroyTexture(texture->m_Texture);
			texture->m_Texture.Reset();
		}
		texture->m_IsUploaded = false;
		texture->m_SrvDimension = RHITextureViewDimension::Unknown;
		SetTextureState(
			*texture,
			AssetState::CpuReady,
			operation,
			AssetStateEventOperationPhase::Completes);
		ProgressReporter(texture->m_LoadProgress).Report(
			1.0f,
			"Texture GPU residency released",
			texture->m_DebugLabel);
		return true;
	}

	void TextureAssetSystem::CreateTextureEntry(
		TextureID id,
		std::string_view textureName,
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
				insertedTexture->m_SourcePath = sourcePath;
				insertedTexture->m_DebugLabel = textureName;
				insertedTexture->m_LoadProgress = std::make_shared<ProgressChannel>();
				insertedTexture->m_IsUploaded = false;
				insertedTexture->m_Srv.Reset();
				insertedTexture->m_Texture.Reset();
				insertedTexture->m_Desc = {};
			}
		}
	}
}
