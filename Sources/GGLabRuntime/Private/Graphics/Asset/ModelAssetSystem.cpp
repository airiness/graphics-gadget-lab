#include "Graphics/Asset/ModelAssetSystem.h"

#include <algorithm>

namespace gglab
{
	const Mesh* ModelAssetSystem::FindMesh(MeshID meshId) const noexcept
	{
		return m_MeshStore.Find(meshId);
	}

	Mesh* ModelAssetSystem::EditMesh(MeshID meshId) noexcept
	{
		return m_MeshStore.Edit(meshId);
	}

	MeshID ModelAssetSystem::CreateMesh() noexcept
	{
		return m_MeshStore.Create();
	}

	MeshStore::InsertResult ModelAssetSystem::InsertMesh(std::unique_ptr<Mesh>&& mesh) noexcept
	{
		return m_MeshStore.Insert(std::move(mesh));
	}

	bool ModelAssetSystem::RemoveMesh(MeshID meshId) noexcept
	{
		return m_MeshStore.Remove(meshId);
	}

	const MeshStore::EntryMap& ModelAssetSystem::MeshEntries() const noexcept
	{
		return m_MeshStore.Entries();
	}

	const Material* ModelAssetSystem::FindMaterial(MaterialID materialId) const noexcept
	{
		return m_MaterialStore.Find(materialId);
	}

	MaterialID ModelAssetSystem::InsertMaterial(std::unique_ptr<Material>&& material) noexcept
	{
		return m_MaterialStore.Insert(std::move(material)).m_Id;
	}

	bool ModelAssetSystem::RemoveMaterial(MaterialID materialId) noexcept
	{
		return m_MaterialStore.Remove(materialId);
	}

	const MaterialStore::EntryMap& ModelAssetSystem::MaterialEntries() const noexcept
	{
		return m_MaterialStore.Entries();
	}

	const Model* ModelAssetSystem::FindModel(ModelID modelId) const noexcept
	{
		return m_ModelStore.Find(modelId);
	}

	Model* ModelAssetSystem::EditModel(ModelID modelId) noexcept
	{
		return m_ModelStore.Edit(modelId);
	}

	ModelID ModelAssetSystem::CreateModel(const std::filesystem::path& canonicalPath) noexcept
	{
		return m_ModelStore.Create(canonicalPath);
	}

	ModelStore::InsertResult ModelAssetSystem::InsertModel(std::unique_ptr<Model>&& model) noexcept
	{
		return m_ModelStore.Insert(std::move(model));
	}

	ModelID ModelAssetSystem::FindModelByPath(
		const std::filesystem::path& canonicalPath) const noexcept
	{
		return m_ModelStore.FindByPath(canonicalPath);
	}

	bool ModelAssetSystem::RemoveModel(ModelID modelId) noexcept
	{
		return m_ModelStore.Remove(modelId);
	}

	bool ModelAssetSystem::DetachModelPath(
		const std::filesystem::path& canonicalPath, ModelID modelId) noexcept
	{
		return m_ModelStore.DetachPath(canonicalPath, modelId);
	}

	const ModelStore::EntryMap& ModelAssetSystem::ModelEntries() const noexcept
	{
		return m_ModelStore.Entries();
	}

	void ModelAssetSystem::AddPendingModel(ModelID modelId) noexcept
	{
		m_PendingModels.insert(modelId);
	}

	void ModelAssetSystem::RemovePendingModel(ModelID modelId) noexcept
	{
		m_PendingModels.erase(modelId);
	}

	bool ModelAssetSystem::IsModelPending(ModelID modelId) const noexcept
	{
		return m_PendingModels.contains(modelId);
	}

	bool ModelAssetSystem::HasPendingModels() const noexcept
	{
		return !m_PendingModels.empty();
	}
}
