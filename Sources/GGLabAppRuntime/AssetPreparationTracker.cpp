#include "AssetPreparationTracker.h"
#include "GGLabRuntime/Graphics/Asset/AssetLoadProgress.h"
#include "GGLabRuntime/Graphics/Asset/AssetManager.h"

#include <algorithm>
#include <utility>

namespace gglab
{
	void AssetPreparationTracker::Reset() noexcept
	{
		m_Models.clear();
	}

	void AssetPreparationTracker::TrackModel(
		ModelID modelId, std::string_view label, float weight) noexcept
	{
		if (std::ranges::find(m_Models, modelId, &Dependency<ModelID>::m_Id) != m_Models.end())
		{
			return;
		}
		m_Models.push_back({ modelId, std::string(label), weight });
	}

	LoadingProgress AssetPreparationTracker::BuildProgress(
		const AssetManager& assetManager, std::string title) const noexcept
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
			progress.AddAssetStep(dependency.m_Weight,
				GetAssetLoadProgress(model->m_State, AssetLoadKind::Model, model->m_LoadProgress),
				dependency.m_Label);
		}
		return progress.Build();
	}
}
