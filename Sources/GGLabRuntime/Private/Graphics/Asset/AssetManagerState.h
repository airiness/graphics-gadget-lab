#pragma once
#include "GGLabRuntime/Graphics/Asset/AssetManager.h"
#include "Graphics/Asset/Dependency/AssetStateEventQueue.h"
#include "Graphics/Asset/Loading/AssetLoadCoordinator.h"
#include "Graphics/Asset/ModelAssetSystem.h"
#include "Graphics/Asset/ModelImportArtifactCache.h"
#include "Graphics/Asset/Publication/AssetPublicationCoordinator.h"
#include "Graphics/Asset/Residency/AssetResidencyCoordinator.h"
#include "Graphics/Asset/TextureArtifactCache.h"
#include "Graphics/Asset/TextureAssetSystem.h"

#include <memory>

namespace gglab
{
	// Owner-thread implementation state for the Public AssetManager facade. The
	// facade owns this state's lifetime; the store, coordinator, tracker and
	// cache owners stay Runtime-internal behind the Public contract.
	struct AssetManagerState
	{
		explicit AssetManagerState(const AssetManager::CreateInfo& createInfo) noexcept;

		TextureArtifactCache m_TextureArtifactCache;
		ModelImportArtifactCache m_ModelImportArtifactCache;
		AssetLoadCoordinator m_AssetLoadCoordinator;
		// State events outlive TextureAssetSystem so the injected sink remains valid
		// through texture-domain shutdown and destruction.
		AssetStateEventQueue m_AssetStateEventQueue;
		std::unique_ptr<TextureAssetSystem> m_TextureAssets;
		ModelAssetSystem m_ModelAssets;
		AssetResidencyCoordinator m_AssetResidencyCoordinator;
		AssetPublicationCoordinator m_AssetPublicationCoordinator;
	};
}
