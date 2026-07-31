#pragma once
#include "Graphics/Asset/Publication/ModelPublicationJournal.h"
#include "Graphics/Asset/ModelImportArtifact.h"

#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

namespace gglab
{
	class ModelPublicationJob final : public IResourcePublicationJob
	{
	public:
		ModelPublicationJob(std::unique_ptr<AssetPublicationServicesBase>&& services,
			AssetContentVersion model, double importQueueMilliseconds,
			double importExecutionMilliseconds, ModelImportArtifactHandle artifact) noexcept;

		[[nodiscard]] AssetResourcePublicationStepResult Step(
			AssetResourcePublicationContext& context) noexcept override;
		void Abort(AssetResourcePublicationContext& context,
			AssetResourcePublicationAbortReason reason) noexcept override;
		[[nodiscard]] uint64_t GetProgressToken() const noexcept override
		{
			return m_ProgressToken;
		}
		[[nodiscard]] AssetResourcePublicationStage GetCurrentStage() const noexcept override;

	private:
		enum class Stage : uint8_t
		{
			Textures,
			Materials,
			Meshes,
			MeshInstances,
			FallbackMeshInstances,
			Dependencies,
			Commit,
			ReleaseRetains,
			Finished,
		};

		struct ProgressState
		{
			Stage m_Stage = Stage::Finished;
			size_t m_TextureCursor = 0;
			size_t m_MaterialCursor = 0;
			size_t m_MeshCursor = 0;
			size_t m_InstanceCursor = 0;
			size_t m_FallbackInstanceCursor = 0;
			size_t m_DependencyCursor = 0;
			size_t m_ReleasedRetainCount = 0;
			bool m_DefaultMaterialCreated = false;
			bool m_Committed = false;

			bool operator==(const ProgressState&) const = default;
		};

		using OptionalStepResult = std::optional<AssetResourcePublicationStepResult>;

		[[nodiscard]] OptionalStepResult StepTextures(TaskPriority priority) noexcept;
		[[nodiscard]] OptionalStepResult StepMaterials() noexcept;
		[[nodiscard]] OptionalStepResult StepMeshes(TaskPriority priority) noexcept;
		[[nodiscard]] OptionalStepResult StepMeshInstances() noexcept;
		[[nodiscard]] OptionalStepResult StepFallbackMeshInstances() noexcept;
		[[nodiscard]] OptionalStepResult StepDependencies(TaskPriority priority) noexcept;
		[[nodiscard]] OptionalStepResult Commit() noexcept;
		[[nodiscard]] OptionalStepResult ReleaseRetains() noexcept;

		void AddDependency(const AssetContentVersion& dependency);
		[[nodiscard]] ProgressState CaptureProgressState() const noexcept;
		[[nodiscard]] AssetResourcePublicationStepResult FinalizeStep(
			AssetResourcePublicationStepResult result,
			const ProgressState& progressBefore) noexcept;
		[[nodiscard]] static AssetResourcePublicationStage PublicationStage(Stage stage) noexcept;
		[[nodiscard]] static AssetResourcePublicationStepResult Failed(
			std::string error, AssetResourcePublicationStepUsage usage = {}) noexcept;
		[[nodiscard]] static AssetResourcePublicationStepResult Continued(
			AssetResourcePublicationStepUsage usage = {}) noexcept;

		std::unique_ptr<AssetPublicationServicesBase> m_Services;
		AssetContentVersion m_Model{};
		double m_ImportQueueMilliseconds = 0.0;
		double m_ImportExecutionMilliseconds = 0.0;
		ModelImportArtifactHandle m_Artifact;
		ModelPublicationJournal m_Journal;
		Stage m_Stage = Stage::Textures;
		Stage m_LastStepStage = Stage::Finished;
		uint64_t m_ProgressToken = 0;
		size_t m_TextureCursor = 0;
		size_t m_MaterialCursor = 0;
		size_t m_MeshCursor = 0;
		size_t m_InstanceCursor = 0;
		size_t m_FallbackInstanceCursor = 0;
		size_t m_DependencyCursor = 0;
		size_t m_ReleasedRetainCount = 0;
		std::vector<TextureID> m_TextureIds;
		std::vector<MaterialID> m_MaterialIds;
		std::vector<MeshID> m_MeshIds;
		std::vector<ModelMesh> m_PendingInstances;
		std::vector<AssetContentVersion> m_Dependencies;
		std::unordered_set<AssetKey, AssetKeyHash> m_DependencyKeys;
		uint32_t m_QueuedTextureUploads = 0;
		uint32_t m_QueuedMeshUploads = 0;
		uint32_t m_CommittedInstanceCount = 0;
		bool m_DefaultMaterialCreated = false;
	};
}
