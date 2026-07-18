#include "Core/Precompiled.h"
#include "Graphics/Asset/AssetIdentityConversions.h"
#include "Graphics/Asset/Publication/AssetPublicationServices.h"
#include "Graphics/AssetManager.h"
#include "Graphics/SamplerRegistry.h"
#include "Graphics/TextureAssetSystem.h"

namespace gglab
{
	namespace
	{
		[[nodiscard]] constexpr bool IsTerminalPublicationState(
			AssetState state) noexcept
		{
			return state == AssetState::Failed || state == AssetState::Cancelled;
		}
	}

	class AssetManagerPublicationServices final : public AssetPublicationServicesBase
	{
	public:
		explicit AssetManagerPublicationServices(AssetManager* assetManager) noexcept :
			m_AssetManager(assetManager)
		{
			GGLAB_ASSERT_NOT_NULL(m_AssetManager);
		}

		[[nodiscard]] bool PrepareModelForPublication(
			const AssetContentVersion& modelVersion) noexcept override
		{
			Model* model = GetModel(modelVersion);
			if (!model || model->m_CancelRequested)
			{
				return false;
			}
			if (model->m_State == AssetState::CpuReady)
			{
				SetAssetState(*model, AssetState::Publishing);
				ProgressReporter(model->m_LoadProgress).Report(
					0.64f,
					"Publishing model resources incrementally");
			}
			return true;
		}

		[[nodiscard]] ModelPublicationTextureResult PublishTexture(
			ImportedTexture& importedTexture,
			TaskPriority priority) noexcept override
		{
			ModelPublicationTextureResult result{};
			const uint64_t sourceBytes = static_cast<uint64_t>(
				importedTexture.m_Data.m_Pixels.size());
			const auto fail = [&importedTexture, sourceBytes](
				ModelPublicationTextureResult& failedResult,
				std::string error) noexcept -> ModelPublicationTextureResult
				{
					importedTexture = {};
					failedResult.m_Usage.m_PayloadBytesDestroyed = sourceBytes;
					failedResult.m_Error = std::move(error);
					return std::move(failedResult);
				};
			if (importedTexture.m_ImportSettings.m_Semantic != importedTexture.m_Semantic)
			{
				return fail(
					result,
					"Imported texture semantic does not match its import settings");
			}

			TextureAssetSystem& textureAssets = *m_AssetManager->m_TextureAssets;
			TextureID textureId = textureAssets.FindTexture(
				importedTexture.m_CanonicalPath,
				importedTexture.m_ImportSettings);
			const Texture* texture = textureAssets.GetTexture(textureId);
			if (textureId.IsValid() && !texture)
			{
				GGLAB_UNUSED(textureAssets.RemoveTexture(textureId));
				textureId.Reset();
			}
			else if (texture && IsTerminalPublicationState(texture->m_State))
			{
				const AssetKey textureKey = MakeAssetKey(textureId);
				if (m_AssetManager->HasActiveInterest(textureKey) ||
					m_AssetManager->HasPublicationRetain(
						textureKey,
						texture->m_ContentGeneration))
				{
					return fail(
						result,
						std::format(
							"Terminal texture {} is still retained",
							textureId.Value()));
				}
				GGLAB_UNUSED(textureAssets.RemoveTexture(textureId));
				textureId.Reset();
				texture = nullptr;
			}

			bool created = false;
			if (!textureId.IsValid())
			{
				if (!importedTexture.m_Data.IsValid())
				{
					return fail(result, "Imported texture payload is invalid");
				}
				textureId = textureAssets.CreateTexture(
					importedTexture.m_CanonicalPath,
					importedTexture.m_ImportSettings);
				texture = textureAssets.GetTexture(textureId);
				if (!textureId.IsValid() || !texture)
				{
					return fail(
						result,
						"Failed to create texture entry during model publication");
				}
				created = true;
			}

			const AssetContentVersion contentVersion = MakeAssetContentVersion(
				textureId,
				texture->m_ContentGeneration);
			ModelPublicationRetainToken retain = StoreRetain(
				m_AssetManager->AcquirePublicationRetain(
					AssetKind::Texture,
					textureId.Value(),
					texture->m_ContentGeneration));
			if (!retain.IsValid())
			{
				if (created)
				{
					GGLAB_UNUSED(textureAssets.RemoveTexture(textureId));
				}
				return fail(result, "Failed to retain texture publication claim");
			}
			if (created && !textureAssets.BeginPublication(textureId))
			{
				ReleaseRetain(retain);
				GGLAB_UNUSED(textureAssets.RemoveTexture(textureId));
				return fail(result, "Failed to begin texture publication");
			}

			result.m_TextureId = textureId;
			result.m_Claim = {
				.m_ContentVersion = contentVersion,
				.m_Origin = created ?
					ModelPublicationClaimOrigin::Created :
					ModelPublicationClaimOrigin::Reused,
				.m_Retain = retain,
			};
			if (!IsReservedTextureId(textureId))
			{
				result.m_Dependency = contentVersion;
			}
			if (!created)
			{
				importedTexture = {};
				result.m_Usage.m_PayloadBytesDestroyed = sourceBytes;
				return result;
			}

			auto uploadData = textureAssets.MakeTextureUploadData(
				textureId,
				std::move(importedTexture.m_Data),
				importedTexture.m_ImportSettings);
			const bool queued = textureAssets.QueueTextureUpload(
				std::move(uploadData),
				priority);
			importedTexture = {};
			result.m_Usage.m_ResourceCreations = 1;
			if (!queued)
			{
				ReleaseRetain(retain);
				result.m_Claim = {};
				result.m_Dependency = {};
				result.m_TextureId.Reset();
				GGLAB_UNUSED(textureAssets.RemoveTexture(textureId));
				result.m_Usage.m_PayloadBytesDestroyed = sourceBytes;
				result.m_Error = std::format(
					"Failed to queue texture {} upload",
					textureId.Value());
				return result;
			}
			result.m_Usage.m_PayloadBytesMovedToUpload = sourceBytes;
			result.m_UploadQueued = true;
			return result;
		}

		[[nodiscard]] ModelPublicationMaterialResult PublishMaterial(
			const ImportedMaterial* importedMaterial,
			std::span<const TextureID> textureIds) noexcept override
		{
			ModelPublicationMaterialResult result{};
			auto material = std::make_unique<Material>();
			if (importedMaterial)
			{
				static_cast<MaterialProperties&>(*material) =
					importedMaterial->m_Properties;
				material->m_Name = StringID(importedMaterial->m_Name);
				for (uint32_t slotIndex = 0;
					slotIndex < utils::ToIndex(MaterialTextureSlot::Count);
					++slotIndex)
				{
					const ImportedMaterialTextureBinding& importedBinding =
						importedMaterial->m_TextureBindings[slotIndex];
					if (importedBinding.m_TextureIndex ==
						ImportedMaterialTextureBinding::InvalidTextureIndex)
					{
						continue;
					}

					MaterialTextureBinding binding{};
					if (importedBinding.m_TextureIndex < textureIds.size())
					{
						binding.m_TextureId = textureIds[importedBinding.m_TextureIndex];
					}
					binding.m_SamplerId = m_AssetManager->m_SamplerRegistry->GetOrCreateSampler(
						importedBinding.m_SamplerKey);
					binding.m_TexCoordIndex = importedBinding.m_TexCoordIndex;
					AssetManager::SetMaterialTexture(
						*material,
						static_cast<MaterialTextureSlot>(slotIndex),
						binding);
				}
			}

			result.m_MaterialId = m_AssetManager->AddMaterial(std::move(material));
			if (!result.m_MaterialId.IsValid())
			{
				result.m_Error = "Failed to create material during model publication";
				return result;
			}
			result.m_Usage.m_ResourceCreations = 1;
			return result;
		}

		[[nodiscard]] ModelPublicationMeshResult PublishMesh(
			const AssetContentVersion& modelVersion,
			uint32_t sourceMeshIndex,
			ImportedMesh& importedMesh,
			TaskPriority priority) noexcept override
		{
			ModelPublicationMeshResult result{};
			const uint64_t vertexBytes = static_cast<uint64_t>(
				importedMesh.m_Vertices.size()) * sizeof(Vertex);
			const uint64_t indexBytes = static_cast<uint64_t>(
				importedMesh.m_Indices.size()) * sizeof(uint32_t);
			const uint64_t sourceBytes = vertexBytes + indexBytes;
			const MeshID meshId = m_AssetManager->CreateMesh();
			Mesh* mesh = m_AssetManager->EditMesh(meshId);
			if (!meshId.IsValid() || !mesh)
			{
				result.m_Usage.m_PayloadBytesDestroyed = sourceBytes;
				result.m_Error = "Failed to create mesh entry during model publication";
				return result;
			}

			result.m_MeshId = meshId;
			mesh->m_Id = meshId;
			mesh->m_Name = StringID(importedMesh.m_Name);
			mesh->m_Sphere = importedMesh.m_Sphere;
			mesh->m_Aabb = importedMesh.m_Aabb;
			mesh->m_HasBounds = importedMesh.m_HasBounds;
			mesh->m_SourceModelId = ToModelId(modelVersion);
			mesh->m_SourceMeshIndex = sourceMeshIndex;
			m_AssetManager->SetMeshState(*mesh, AssetState::Publishing);
			AssetPublicationRetain retain = m_AssetManager->AcquirePublicationRetain(
				AssetKind::Mesh,
				meshId.Value(),
				mesh->m_ContentGeneration);
			result.m_Claim = {
				.m_ContentVersion = MakeAssetContentVersion(
					AssetKind::Mesh,
					meshId.Value(),
					mesh->m_ContentGeneration),
				.m_Origin = ModelPublicationClaimOrigin::Created,
				.m_Retain = StoreRetain(std::move(retain)),
			};
			result.m_Dependency = result.m_Claim.m_ContentVersion;

			AssetManager::MeshUploadData uploadData{};
			uploadData.m_MeshId = meshId;
			uploadData.m_VerticesData = std::move(importedMesh.m_Vertices);
			uploadData.m_IndicesData = std::move(importedMesh.m_Indices);
			importedMesh.m_Name.clear();
			const bool queued = m_AssetManager->QueueMeshUpload(
				std::move(uploadData),
				priority);
			result.m_Usage.m_ResourceCreations = 1;
			if (!queued)
			{
				result.m_Usage.m_PayloadBytesDestroyed = sourceBytes;
				result.m_Error = std::format(
					"Failed to queue mesh {} upload",
					meshId.Value());
				return result;
			}
			result.m_Usage.m_PayloadBytesMovedToUpload = sourceBytes;
			result.m_UploadQueued = true;
			return result;
		}

		[[nodiscard]] ModelPublicationOwnerToken CreateDependencyOwner(
			const AssetContentVersion& modelVersion) noexcept override
		{
			if (modelVersion.m_Key.m_Kind != AssetKind::Model)
			{
				return {};
			}
			const AssetOwnerId owner = m_AssetManager->RegisterAssetOwner();
			return { .m_Value = owner.m_Value };
		}

		[[nodiscard]] ModelPublicationLeaseToken AcquireDependencyLease(
			ModelPublicationOwnerToken ownerToken,
			const AssetContentVersion& dependency,
			TaskPriority priority) noexcept override
		{
			const std::optional<AssetKey> interestKey = ToInterestKey(dependency.m_Key);
			if (!ownerToken.IsValid() || !dependency.IsValid() || !interestKey)
			{
				return {};
			}

			AssetLease lease = m_AssetManager->AcquireAssetLease(
				AssetOwnerId{ ownerToken.m_Value },
				interestKey->m_Kind,
				dependency.m_Key.m_StableId,
				dependency.m_ContentGeneration,
				priority);
			if (!lease.IsValid())
			{
				return {};
			}
			const ModelPublicationLeaseToken token{ lease.m_LeaseToken };
			lease.m_Manager = nullptr;
			lease.m_LeaseToken = 0;
			return token;
		}

		[[nodiscard]] std::string CommitModel(
			ModelPublicationCommit&& commit) noexcept override
		{
			Model* model = GetModel(commit.m_Model);
			if (!model || model->m_CancelRequested)
			{
				return "Model content version changed before publication commit";
			}
			if (commit.m_MeshInstances.empty())
			{
				return "Model publication produced no renderable mesh instances";
			}
			if (!commit.m_DependencyOwner.IsValid())
			{
				return "Model publication has no dependency owner";
			}

			const ModelID modelId = ToModelId(commit.m_Model);
			if (m_AssetManager->m_ModelDependencyOwners.contains(modelId) ||
				m_AssetManager->m_ModelDependencyLeaseTokens.contains(modelId))
			{
				return "Model already owns dependency interests before publication commit";
			}

			std::vector<uint64_t> dependencyLeaseTokens;
			dependencyLeaseTokens.reserve(commit.m_DependencyLeases.size());
			for (ModelPublicationLeaseToken lease : commit.m_DependencyLeases)
			{
				if (!lease.IsValid())
				{
					return "Model publication contains an invalid dependency lease";
				}
				dependencyLeaseTokens.push_back(lease.m_Value);
			}

			model->m_Name = StringID(commit.m_Name);
			model->m_Type = commit.m_Type;
			model->m_MeshInstance = std::move(commit.m_MeshInstances);
			m_AssetManager->m_ModelDependencyOwners.emplace(
				modelId,
				AssetOwnerId{ commit.m_DependencyOwner.m_Value });
			m_AssetManager->m_ModelDependencyLeaseTokens.emplace(
				modelId,
				std::move(dependencyLeaseTokens));
			SetAssetState(*model, AssetState::UploadQueued);
			m_AssetManager->RegisterModelDependencies(
				modelId,
				commit.m_Model.m_ContentGeneration);
			m_AssetManager->m_PendingModels.insert(modelId);
			ProgressReporter(model->m_LoadProgress).Report(
				0.66f,
				"Waiting for model dependency uploads",
				std::format(
					"{} texture uploads, {} mesh uploads",
					commit.m_QueuedTextureUploads,
					commit.m_QueuedMeshUploads));
			if (m_AssetManager->RefreshModelState(modelId))
			{
				m_AssetManager->m_PendingModels.erase(modelId);
			}
			return {};
		}

		void ReleaseRetain(ModelPublicationRetainToken retain) noexcept override
		{
			if (!retain.IsValid())
			{
				return;
			}
			const auto retained = m_Retains.find(retain.m_Value);
			if (retained == m_Retains.end())
			{
				GGLAB_ASSERT_MSG(false, "Released an unknown model publication retain.");
				return;
			}
			m_Retains.erase(retained);
		}

		void CancelClaimIfUnreferenced(
			const ModelPublicationClaim& claim) noexcept override
		{
			const std::optional<AssetKey> key = ToInterestKey(
				claim.m_ContentVersion.m_Key);
			if (!key ||
				m_AssetManager->HasPublicationRetain(
					*key,
					claim.m_ContentVersion.m_ContentGeneration) ||
				m_AssetManager->HasActiveInterest(*key))
			{
				return;
			}
			m_AssetManager->CancelAssetIfUnreferenced(
				*key,
				claim.m_ContentVersion.m_ContentGeneration);
		}

		void RollbackClaimIfUnreferenced(
			const ModelPublicationClaim& claim) noexcept override
		{
			const std::optional<AssetKey> key = ToInterestKey(
				claim.m_ContentVersion.m_Key);
			if (!key ||
				m_AssetManager->HasPublicationRetain(
					*key,
					claim.m_ContentVersion.m_ContentGeneration) ||
				m_AssetManager->HasActiveInterest(*key))
			{
				return;
			}

			if (claim.m_Origin == ModelPublicationClaimOrigin::Created)
			{
				if (claim.m_ContentVersion.m_Key.m_Kind == AssetKind::Texture)
				{
					m_AssetManager->m_TextureAssets->RollbackPublicationTexture(
						TextureID{ static_cast<uint32_t>(
							claim.m_ContentVersion.m_Key.m_StableId) },
						claim.m_ContentVersion.m_ContentGeneration);
				}
				else if (claim.m_ContentVersion.m_Key.m_Kind == AssetKind::Mesh)
				{
					m_AssetManager->RollbackPublicationMesh(
						MeshID{ static_cast<uint32_t>(
							claim.m_ContentVersion.m_Key.m_StableId) },
						claim.m_ContentVersion.m_ContentGeneration);
				}
				return;
			}

			m_AssetManager->CancelAssetIfUnreferenced(
				*key,
				claim.m_ContentVersion.m_ContentGeneration);
		}

		void RemoveMaterial(MaterialID materialId) noexcept override
		{
			GGLAB_UNUSED(m_AssetManager->RemoveMaterial(materialId));
		}

		void ReleaseDependencyLease(
			ModelPublicationLeaseToken lease) noexcept override
		{
			if (lease.IsValid())
			{
				m_AssetManager->ReleaseAssetLease(lease.m_Value);
			}
		}

		void DestroyDependencyOwner(
			ModelPublicationOwnerToken owner) noexcept override
		{
			if (owner.IsValid())
			{
				m_AssetManager->UnregisterAssetOwner(AssetOwnerId{ owner.m_Value });
			}
		}

		void AbortModel(
			const AssetContentVersion& modelVersion,
			AssetResourcePublicationAbortReason reason) noexcept override
		{
			Model* model = GetModel(modelVersion);
			if (!model)
			{
				return;
			}
			const ModelID modelId = ToModelId(modelVersion);
			m_AssetManager->UnregisterModelDependencies(
				modelId,
				modelVersion.m_ContentGeneration);
			model->m_MeshInstance.clear();
			SetAssetState(
				*model,
				reason == AssetResourcePublicationAbortReason::Failed ?
					AssetState::Failed : AssetState::Cancelled);
			m_AssetManager->m_PendingModels.erase(modelId);
			ProgressReporter(model->m_LoadProgress).Report(
				0.62f,
				reason == AssetResourcePublicationAbortReason::Failed ?
					"Model publication failed" : "Model publication cancelled");
		}

	private:
		[[nodiscard]] ModelPublicationRetainToken StoreRetain(
			AssetPublicationRetain&& retain)
		{
			if (!retain.IsValid())
			{
				return {};
			}
			const ModelPublicationRetainToken token{ m_NextRetainToken++ };
			m_Retains.emplace(token.m_Value, std::move(retain));
			return token;
		}

		[[nodiscard]] static std::optional<AssetKey> ToInterestKey(
			const AssetKey& key) noexcept
		{
			if (!key.IsValid() ||
				(key.m_Kind != AssetKind::Model &&
					key.m_Kind != AssetKind::Texture &&
					key.m_Kind != AssetKind::Mesh))
			{
				return std::nullopt;
			}
			return key;
		}

		[[nodiscard]] static ModelID ToModelId(
			const AssetContentVersion& modelVersion) noexcept
		{
			if (modelVersion.m_Key.m_Kind != AssetKind::Model)
			{
				return {};
			}
			return ModelID{
				static_cast<uint32_t>(modelVersion.m_Key.m_StableId),
			};
		}

		[[nodiscard]] Model* GetModel(
			const AssetContentVersion& modelVersion) const noexcept
		{
			const ModelID modelId = ToModelId(modelVersion);
			Model* model = m_AssetManager->EditModel(modelId);
			return model &&
				model->m_ContentGeneration == modelVersion.m_ContentGeneration ?
				model : nullptr;
		}

		AssetManager* m_AssetManager = nullptr;
		uint64_t m_NextRetainToken = 1;
		std::unordered_map<uint64_t, AssetPublicationRetain> m_Retains;
	};

	std::unique_ptr<AssetPublicationServicesBase>
		AssetManager::CreateModelPublicationServices() noexcept
	{
		return std::make_unique<AssetManagerPublicationServices>(this);
	}
}
