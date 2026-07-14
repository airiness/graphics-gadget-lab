#include "Core/Precompiled.h"
#include "Application/AssetPreparationTracker.h"
#include "Graphics/AssetLoadProgress.h"
#include "Graphics/AssetManager.h"

namespace gglab
{
	void AssetPreparationTracker::Reset() noexcept
	{
		m_Models.clear();
		m_Meshes.clear();
	}

	void AssetPreparationTracker::TrackModel(
		ModelID modelId,
		std::string_view label,
		float weight) noexcept
	{
		if (std::ranges::find(m_Models, modelId, &Dependency<ModelID>::m_Id) != m_Models.end())
		{
			return;
		}
		m_Models.push_back({ modelId, std::string(label), weight });
	}

	void AssetPreparationTracker::TrackMesh(
		MeshID meshId,
		std::string_view label,
		float weight) noexcept
	{
		if (std::ranges::find(m_Meshes, meshId, &Dependency<MeshID>::m_Id) != m_Meshes.end())
		{
			return;
		}
		m_Meshes.push_back({ meshId, std::string(label), weight });
	}

	LoadingProgress AssetPreparationTracker::BuildProgress(
		const AssetManager& assetManager,
		std::string title) const noexcept
	{
		LoadingProgressBuilder progress(std::move(title));
		for (const auto& dependency : m_Models)
		{
			const Model* model = assetManager.GetModel(dependency.m_Id);
			if (!model)
			{
				progress.AddStep(dependency.m_Weight, {
					.m_Status = LoadingStatus::Failed,
					.m_Fraction = 0.0f,
					.m_Stage = "Model request unavailable",
					.m_Detail = dependency.m_Label,
				});
				continue;
			}
			progress.AddAssetStep(
				dependency.m_Weight,
				GetAssetLoadProgress(model->m_State, AssetLoadKind::Model),
				dependency.m_Label);
		}

		for (const auto& dependency : m_Meshes)
		{
			const Mesh* mesh = assetManager.GetMesh(dependency.m_Id);
			if (!mesh)
			{
				progress.AddStep(dependency.m_Weight, {
					.m_Status = LoadingStatus::Failed,
					.m_Fraction = 0.0f,
					.m_Stage = "Mesh request unavailable",
					.m_Detail = dependency.m_Label,
				});
				continue;
			}
			progress.AddAssetStep(
				dependency.m_Weight,
				GetAssetLoadProgress(mesh->m_State, AssetLoadKind::Mesh),
				dependency.m_Label);
		}
		return progress.Build();
	}
}
