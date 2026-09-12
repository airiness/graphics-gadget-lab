#pragma once
#include "GGLabRuntime/Graphics/Asset/AssetManager.h"
#include "Graphics/Asset/Dependency/AssetDependencyGraph.h"
#include "Graphics/Asset/Dependency/AssetStateEventQueue.h"
#include "Graphics/Asset/Interest/AssetInterestTracker.h"
#include "Graphics/Asset/Loading/AssetLoadCoordinator.h"
#include "Graphics/Asset/ModelImportArtifactCache.h"
#include "Graphics/Asset/Residency/AssetResidencyController.h"
#include "Graphics/Asset/Store/MaterialStore.h"
#include "Graphics/Asset/Store/MeshStore.h"
#include "Graphics/Asset/Store/ModelStore.h"
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
		MeshStore m_MeshStore;
		MaterialStore m_MaterialStore;
		ModelStore m_ModelStore;
		AssetInterestTracker m_AssetInterestTracker;
		AssetResidencyController m_AssetResidencyController;
		AssetDependencyGraph m_AssetDependencyGraph;
	};
}
