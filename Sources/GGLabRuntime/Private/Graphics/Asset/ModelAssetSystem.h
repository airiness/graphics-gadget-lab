#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/GraphicsTypes.h"
#include "Graphics/Asset/Store/MaterialStore.h"
#include "Graphics/Asset/Store/MeshStore.h"
#include "Graphics/Asset/Store/ModelStore.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <unordered_set>
#include <utility>

namespace gglab
{
	// Owner-thread model, mesh and material record stores plus the pending-model
	// set. AssetManager keeps the import, publication and residency orchestration
	// that consumes and mutates these records.
	class ModelAssetSystem final
	{
	public:
		[[nodiscard]] const Mesh* FindMesh(MeshID meshId) const noexcept;
		[[nodiscard]] Mesh* EditMesh(MeshID meshId) noexcept;
		[[nodiscard]] MeshID CreateMesh() noexcept;
		[[nodiscard]] MeshStore::InsertResult InsertMesh(std::unique_ptr<Mesh>&& mesh) noexcept;
		[[nodiscard]] bool RemoveMesh(MeshID meshId) noexcept;
		[[nodiscard]] const MeshStore::EntryMap& MeshEntries() const noexcept;

		[[nodiscard]] const Material* FindMaterial(MaterialID materialId) const noexcept;
		[[nodiscard]] MaterialID InsertMaterial(std::unique_ptr<Material>&& material) noexcept;
		[[nodiscard]] bool RemoveMaterial(MaterialID materialId) noexcept;
		[[nodiscard]] const MaterialStore::EntryMap& MaterialEntries() const noexcept;

		[[nodiscard]] const Model* FindModel(ModelID modelId) const noexcept;
		[[nodiscard]] Model* EditModel(ModelID modelId) noexcept;
		[[nodiscard]] ModelID CreateModel(const std::filesystem::path& canonicalPath) noexcept;
		[[nodiscard]] ModelStore::InsertResult InsertModel(std::unique_ptr<Model>&& model) noexcept;
		[[nodiscard]] ModelID FindModelByPath(
			const std::filesystem::path& canonicalPath) const noexcept;
		[[nodiscard]] bool RemoveModel(ModelID modelId) noexcept;
		[[nodiscard]] bool DetachModelPath(
			const std::filesystem::path& canonicalPath, ModelID modelId) noexcept;
		[[nodiscard]] const ModelStore::EntryMap& ModelEntries() const noexcept;

		void AddPendingModel(ModelID modelId) noexcept;
		void RemovePendingModel(ModelID modelId) noexcept;
		[[nodiscard]] bool IsModelPending(ModelID modelId) const noexcept;
		[[nodiscard]] bool HasPendingModels() const noexcept;
		template <typename Predicate> void RemovePendingModelsIf(Predicate&& predicate)
		{
			std::erase_if(m_PendingModels, std::forward<Predicate>(predicate));
		}

	private:
		MeshStore m_MeshStore;
		MaterialStore m_MaterialStore;
		ModelStore m_ModelStore;
		std::unordered_set<ModelID> m_PendingModels;
	};
}
