#include "Graphics/Asset/Publication/ModelPublicationJob.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Core/Log/LogMacros.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace gglab
{
	ModelPublicationJob::ModelPublicationJob(
		std::unique_ptr<AssetPublicationServicesBase>&& services, AssetContentVersion model,
		double importQueueMilliseconds, double importExecutionMilliseconds,
		ModelImportArtifactHandle artifact) noexcept :
		m_Services(std::move(services)), m_Model(model),
		m_ImportQueueMilliseconds(importQueueMilliseconds),
		m_ImportExecutionMilliseconds(importExecutionMilliseconds),
		m_Artifact(std::move(artifact)),
		m_TextureIds(m_Artifact ? m_Artifact->m_Textures.size() : 0),
		m_MeshIds(m_Artifact ? m_Artifact->m_Meshes.size() : 0)
	{
		GGLAB_ASSERT(m_Model.IsValid());
		GGLAB_ASSERT(m_Model.m_Key.m_Kind == AssetKind::Model);
		GGLAB_ASSERT(m_Artifact && m_Artifact->IsValid());
		const ModelImportArtifact& source = *m_Artifact;
		m_MaterialIds.reserve(std::max<size_t>(source.m_Materials.size(), 1));
		m_PendingInstances.reserve(std::max(source.m_MeshInstances.size(), source.m_Meshes.size()));
		m_Dependencies.reserve(source.m_Textures.size() + source.m_Meshes.size());
		m_Journal.Reserve(source.m_Textures.size() + source.m_Meshes.size(),
			std::max<size_t>(source.m_Materials.size(), 1),
			source.m_Textures.size() + source.m_Meshes.size());
	}

	AssetResourcePublicationStepResult ModelPublicationJob::Step(
		AssetResourcePublicationContext& context) noexcept
	{
		const ProgressState progressBefore = CaptureProgressState();
		if (!m_Services)
		{
			return FinalizeStep(
				Failed("Model publication job has no publication services"), progressBefore);
		}
		if (m_Journal.IsAborted())
		{
			return FinalizeStep(
				{ .m_Status = AssetResourcePublicationStepStatus::Cancelled }, progressBefore);
		}
		if (!m_Services->PrepareModelForPublication(m_Model))
		{
			return FinalizeStep(
				{ .m_Status = AssetResourcePublicationStepStatus::Cancelled }, progressBefore);
		}
		if (!m_Artifact || m_Artifact->m_Meshes.empty())
		{
			return FinalizeStep(Failed("Imported model contains no meshes"), progressBefore);
		}

		for (;;)
		{
			m_LastStepStage = m_Stage;
			OptionalStepResult result;
			switch (m_Stage)
			{
			case Stage::Textures:
				result = StepTextures(context.m_Priority);
				break;
			case Stage::Materials:
				result = StepMaterials();
				break;
			case Stage::Meshes:
				result = StepMeshes(context.m_Priority);
				break;
			case Stage::MeshInstances:
				result = StepMeshInstances();
				break;
			case Stage::FallbackMeshInstances:
				result = StepFallbackMeshInstances();
				break;
			case Stage::Dependencies:
				result = StepDependencies(context.m_Priority);
				break;
			case Stage::Commit:
				result = Commit();
				break;
			case Stage::ReleaseRetains:
				result = ReleaseRetains();
				break;
			case Stage::Finished:
				result = AssetResourcePublicationStepResult{
					.m_Status = AssetResourcePublicationStepStatus::Completed,
				};
				break;
			}

			if (result)
			{
				return FinalizeStep(std::move(*result), progressBefore);
			}
		}
	}

	void ModelPublicationJob::Abort(AssetResourcePublicationContext& context,
		AssetResourcePublicationAbortReason reason) noexcept
	{
		GGLAB_UNUSED(context);
		if (!m_Services || m_Journal.IsAborted())
		{
			return;
		}

		const bool committed = m_Journal.IsCommitted();
		m_Journal.Abort(*m_Services);
		if (!committed)
		{
			m_Services->AbortModel(m_Model, reason);
		}
	}

	AssetResourcePublicationStage ModelPublicationJob::GetCurrentStage() const noexcept
	{
		if (!m_Artifact)
		{
			return AssetResourcePublicationStage::Unknown;
		}
		const ModelImportArtifact& source = *m_Artifact;
		Stage stage = m_Stage;
		for (;;)
		{
			switch (stage)
			{
			case Stage::Textures:
				if (m_TextureCursor < source.m_Textures.size())
				{
					return AssetResourcePublicationStage::Textures;
				}
				stage = Stage::Materials;
				break;
			case Stage::Materials:
				if (m_MaterialCursor < source.m_Materials.size() ||
					(m_MaterialIds.empty() && !m_DefaultMaterialCreated))
				{
					return AssetResourcePublicationStage::Materials;
				}
				stage = Stage::Meshes;
				break;
			case Stage::Meshes:
				if (m_MeshCursor < source.m_Meshes.size())
				{
					return AssetResourcePublicationStage::Meshes;
				}
				stage = Stage::MeshInstances;
				break;
			case Stage::MeshInstances:
				if (m_InstanceCursor < source.m_MeshInstances.size())
				{
					return AssetResourcePublicationStage::MeshInstances;
				}
				stage =
					m_PendingInstances.empty() ? Stage::FallbackMeshInstances : Stage::Dependencies;
				break;
			case Stage::FallbackMeshInstances:
				if (m_FallbackInstanceCursor < m_MeshIds.size())
				{
					return AssetResourcePublicationStage::MeshInstances;
				}
				stage = Stage::Dependencies;
				break;
			case Stage::Dependencies:
				if (m_DependencyCursor < m_Dependencies.size())
				{
					return AssetResourcePublicationStage::Dependencies;
				}
				return AssetResourcePublicationStage::Commit;
			case Stage::Commit:
				return AssetResourcePublicationStage::Commit;
			case Stage::ReleaseRetains:
				return AssetResourcePublicationStage::ReleaseRetains;
			case Stage::Finished:
				return AssetResourcePublicationStage::Unknown;
			}
		}
	}

	ModelPublicationJob::OptionalStepResult ModelPublicationJob::StepTextures(
		TaskPriority priority) noexcept
	{
		const ModelImportArtifact& source = *m_Artifact;
		if (m_TextureCursor >= source.m_Textures.size())
		{
			m_Stage = Stage::Materials;
			return std::nullopt;
		}

		const size_t textureIndex = m_TextureCursor++;
		ModelPublicationTextureResult result =
			m_Services->PublishTexture(source.m_Textures[textureIndex], priority);
		m_TextureIds[textureIndex] = result.m_TextureId;
		if (result.m_Claim.m_ContentVersion.IsValid())
		{
			m_Journal.RecordClaim(std::move(result.m_Claim));
		}
		if (result.m_Dependency.IsValid())
		{
			AddDependency(result.m_Dependency);
		}
		if (!result.Succeeded())
		{
			return Failed(std::move(result.m_Error), result.m_Usage);
		}
		if (result.m_UploadQueued)
		{
			++m_QueuedTextureUploads;
		}
		return Continued(result.m_Usage);
	}

	ModelPublicationJob::OptionalStepResult ModelPublicationJob::StepMaterials() noexcept
	{
		const ModelImportArtifact& source = *m_Artifact;
		const ImportedMaterial* importedMaterial = nullptr;
		if (m_MaterialCursor < source.m_Materials.size())
		{
			importedMaterial = &source.m_Materials[m_MaterialCursor++];
		}
		else if (m_MaterialIds.empty() && !m_DefaultMaterialCreated)
		{
			m_DefaultMaterialCreated = true;
		}
		else
		{
			m_Stage = Stage::Meshes;
			return std::nullopt;
		}

		ModelPublicationMaterialResult result =
			m_Services->PublishMaterial(importedMaterial, m_TextureIds);
		if (!result.Succeeded())
		{
			return Failed(std::move(result.m_Error), result.m_Usage);
		}
		m_MaterialIds.push_back(result.m_MaterialId);
		m_Journal.RecordCreatedMaterial(result.m_MaterialId);
		return Continued(result.m_Usage);
	}

	ModelPublicationJob::OptionalStepResult ModelPublicationJob::StepMeshes(
		TaskPriority priority) noexcept
	{
		const ModelImportArtifact& source = *m_Artifact;
		if (m_MeshCursor >= source.m_Meshes.size())
		{
			m_Stage = Stage::MeshInstances;
			return std::nullopt;
		}

		const size_t meshIndex = m_MeshCursor++;
		ModelPublicationMeshResult result = m_Services->PublishMesh(m_Model,
			{
				.m_Owner = m_Artifact,
				.m_MeshIndex = static_cast<uint32_t>(meshIndex),
			},
			priority);
		m_MeshIds[meshIndex] = result.m_MeshId;
		if (result.m_Claim.m_ContentVersion.IsValid())
		{
			m_Journal.RecordClaim(std::move(result.m_Claim));
		}
		if (result.m_Dependency.IsValid())
		{
			AddDependency(result.m_Dependency);
		}
		if (!result.Succeeded())
		{
			return Failed(std::move(result.m_Error), result.m_Usage);
		}
		if (result.m_UploadQueued)
		{
			++m_QueuedMeshUploads;
		}
		return Continued(result.m_Usage);
	}

	ModelPublicationJob::OptionalStepResult ModelPublicationJob::StepMeshInstances() noexcept
	{
		const ModelImportArtifact& source = *m_Artifact;
		if (m_InstanceCursor >= source.m_MeshInstances.size())
		{
			m_Stage =
				m_PendingInstances.empty() ? Stage::FallbackMeshInstances : Stage::Dependencies;
			return std::nullopt;
		}

		const ImportedModelMesh& importedInstance = source.m_MeshInstances[m_InstanceCursor++];
		if (importedInstance.m_MeshIndex >= m_MeshIds.size())
		{
			return Continued();
		}
		const uint32_t materialIndex = importedInstance.m_MaterialIndex < m_MaterialIds.size()
			? importedInstance.m_MaterialIndex
			: 0;
		m_PendingInstances.push_back({
			.m_MeshId = m_MeshIds[importedInstance.m_MeshIndex],
			.m_MaterialId = m_MaterialIds[materialIndex],
			.m_LocalTransform = importedInstance.m_LocalTransform,
			});
		return Continued({ .m_ResourceCreations = 1 });
	}

	ModelPublicationJob::OptionalStepResult ModelPublicationJob::
		StepFallbackMeshInstances() noexcept
	{
		if (m_FallbackInstanceCursor >= m_MeshIds.size())
		{
			m_Stage = Stage::Dependencies;
			return std::nullopt;
		}

		const size_t meshIndex = m_FallbackInstanceCursor++;
		const uint32_t sourceMaterialIndex = m_Artifact->m_Meshes[meshIndex].m_MaterialIndex;
		const uint32_t materialIndex =
			sourceMaterialIndex < m_MaterialIds.size() ? sourceMaterialIndex : 0;
		m_PendingInstances.push_back({
			.m_MeshId = m_MeshIds[meshIndex],
			.m_MaterialId = m_MaterialIds[materialIndex],
			});
		return Continued({ .m_ResourceCreations = 1 });
	}

	ModelPublicationJob::OptionalStepResult ModelPublicationJob::StepDependencies(
		TaskPriority priority) noexcept
	{
		if (m_DependencyCursor >= m_Dependencies.size())
		{
			m_Stage = Stage::Commit;
			return std::nullopt;
		}
		if (!m_Journal.GetDependencyOwner().IsValid())
		{
			const ModelPublicationOwnerToken owner = m_Services->CreateDependencyOwner(m_Model);
			if (!owner.IsValid())
			{
				return Failed("Failed to create model dependency owner");
			}
			m_Journal.SetDependencyOwner(owner);
		}

		const AssetContentVersion dependency = m_Dependencies[m_DependencyCursor++];
		const ModelPublicationLeaseToken lease = m_Services->AcquireDependencyLease(
			m_Journal.GetDependencyOwner(), dependency, priority);
		if (!lease.IsValid())
		{
			return Failed("Failed to acquire model dependency lease");
		}
		m_Journal.RecordDependencyLease(lease);
		return Continued();
	}

	ModelPublicationJob::OptionalStepResult ModelPublicationJob::Commit() noexcept
	{
		if (m_PendingInstances.empty())
		{
			return Failed("Model publication produced no renderable mesh instances");
		}
		if (!m_Journal.GetDependencyOwner().IsValid())
		{
			const ModelPublicationOwnerToken owner = m_Services->CreateDependencyOwner(m_Model);
			if (!owner.IsValid())
			{
				return Failed("Failed to create model dependency owner");
			}
			m_Journal.SetDependencyOwner(owner);
		}

		m_CommittedInstanceCount = static_cast<uint32_t>(m_PendingInstances.size());
		std::string error = m_Services->CommitModel({
			.m_Model = m_Model,
			.m_Name = m_Artifact->m_Name,
			.m_Type = m_Artifact->m_Type,
			.m_MeshInstances = std::move(m_PendingInstances),
			.m_DependencyOwner = m_Journal.GetDependencyOwner(),
			.m_DependencyLeases = m_Journal.GetDependencyLeases(),
			.m_QueuedTextureUploads = m_QueuedTextureUploads,
			.m_QueuedMeshUploads = m_QueuedMeshUploads,
			});
		if (!error.empty())
		{
			return Failed(std::move(error));
		}

		m_Journal.Commit();
		m_Stage = Stage::ReleaseRetains;
		return Continued();
	}

	ModelPublicationJob::OptionalStepResult ModelPublicationJob::ReleaseRetains() noexcept
	{
		if (m_ReleasedRetainCount < m_Journal.GetClaimCount())
		{
			const bool hasMore = m_Journal.ReleaseNextRetain(*m_Services);
			++m_ReleasedRetainCount;
			if (hasMore)
			{
				return Continued();
			}
		}

		m_Stage = Stage::Finished;
		GGLAB_LOG_GRAPHICS_INFO(
			"Async model {} published incrementally (instances={}, textureUploads={}, meshUploads={}, queueMs={:.2f}, cpuMs={:.2f}).",
			m_Model.m_Key.m_StableId, m_CommittedInstanceCount, m_QueuedTextureUploads,
			m_QueuedMeshUploads, m_ImportQueueMilliseconds, m_ImportExecutionMilliseconds);
		return AssetResourcePublicationStepResult{
			.m_Status = AssetResourcePublicationStepStatus::Completed,
		};
	}

	void ModelPublicationJob::AddDependency(const AssetContentVersion& dependency)
	{
		GGLAB_ASSERT(dependency.IsValid());
		if (m_DependencyKeys.insert(dependency.m_Key).second)
		{
			m_Dependencies.push_back(dependency);
		}
	}

	ModelPublicationJob::ProgressState ModelPublicationJob::CaptureProgressState() const noexcept
	{
		return {
			.m_Stage = m_Stage,
			.m_TextureCursor = m_TextureCursor,
			.m_MaterialCursor = m_MaterialCursor,
			.m_MeshCursor = m_MeshCursor,
			.m_InstanceCursor = m_InstanceCursor,
			.m_FallbackInstanceCursor = m_FallbackInstanceCursor,
			.m_DependencyCursor = m_DependencyCursor,
			.m_ReleasedRetainCount = m_ReleasedRetainCount,
			.m_DefaultMaterialCreated = m_DefaultMaterialCreated,
			.m_Committed = m_Journal.IsCommitted(),
		};
	}

	AssetResourcePublicationStepResult ModelPublicationJob::FinalizeStep(
		AssetResourcePublicationStepResult result, const ProgressState& progressBefore) noexcept
	{
		result.m_Usage.m_Stage = PublicationStage(m_LastStepStage);
		if (CaptureProgressState() != progressBefore)
		{
			++m_ProgressToken;
		}
		return result;
	}

	AssetResourcePublicationStage ModelPublicationJob::PublicationStage(Stage stage) noexcept
	{
		switch (stage)
		{
		case Stage::Textures:
			return AssetResourcePublicationStage::Textures;
		case Stage::Materials:
			return AssetResourcePublicationStage::Materials;
		case Stage::Meshes:
			return AssetResourcePublicationStage::Meshes;
		case Stage::MeshInstances:
		case Stage::FallbackMeshInstances:
			return AssetResourcePublicationStage::MeshInstances;
		case Stage::Dependencies:
			return AssetResourcePublicationStage::Dependencies;
		case Stage::Commit:
			return AssetResourcePublicationStage::Commit;
		case Stage::ReleaseRetains:
			return AssetResourcePublicationStage::ReleaseRetains;
		case Stage::Finished:
			return AssetResourcePublicationStage::Unknown;
		}
		return AssetResourcePublicationStage::Unknown;
	}

	AssetResourcePublicationStepResult ModelPublicationJob::Failed(
		std::string error, AssetResourcePublicationStepUsage usage) noexcept
	{
		return {
			.m_Status = AssetResourcePublicationStepStatus::Failed,
			.m_Usage = usage,
			.m_Error = std::move(error),
		};
	}

	AssetResourcePublicationStepResult ModelPublicationJob::Continued(
		AssetResourcePublicationStepUsage usage) noexcept
	{
		return {
			.m_Status = AssetResourcePublicationStepStatus::Continue,
			.m_Usage = usage,
		};
	}
}
