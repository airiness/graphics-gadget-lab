#pragma once
#include "LoadingProgress.h"
#include "Graphics/GraphicsTypes.h"

#include <string>
#include <string_view>
#include <vector>

namespace gglab
{
	class AssetManager;

	class AssetPreparationTracker final
	{
	public:
		void Reset() noexcept;
		void TrackModel(ModelID modelId, std::string_view label, float weight = 1.0f) noexcept;
		void TrackMesh(MeshID meshId, std::string_view label, float weight = 1.0f) noexcept;

		[[nodiscard]] LoadingProgress BuildProgress(
			const AssetManager& assetManager, std::string title = {}) const noexcept;

	private:
		template <typename AssetId> struct Dependency
		{
			AssetId m_Id{};
			std::string m_Label;
			float m_Weight = 1.0f;
		};

		std::vector<Dependency<ModelID>> m_Models;
		std::vector<Dependency<MeshID>> m_Meshes;
	};
}
