#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Asset/AssetIdentity.h"
#include "GGLabRuntime/Graphics/GraphicsHandles.h"

#include <cstdint>
#include <unordered_set>

namespace gglab
{
	class AssetResidencyCoordinator;

	// Owner-thread cancellation, protected-publication and runtime-retirement
	// accounting reported through AssetOwnershipStatistics. AssetManager updates
	// the counters; the publication coordinator owns the storage.
	struct AssetPublicationAccounting
	{
		uint64_t m_CpuCancellationCount = 0;
		uint64_t m_ReadyCancellationCount = 0;
		uint64_t m_GpuDeferredCancellationCount = 0;
		uint64_t m_ProtectedCancellationCount = 0;
		uint64_t m_RuntimeRetirementRequestCount = 0;
		uint64_t m_RuntimeRetirementCancellationCount = 0;
		uint64_t m_RuntimeRetirementCount = 0;
	};

	// Owner-thread publication-protection state: publication retains, meshes
	// whose publication was rolled back while GPU work may still complete, the
	// protected-cancellation accounting and the shared cancellation/retirement
	// counters. The upload scheduler keeps the upload queue and AssetManager
	// keeps the publication state transitions.
	class AssetPublicationCoordinator final
	{
	public:
		explicit AssetPublicationCoordinator(AssetResidencyCoordinator& residency) noexcept :
			m_Residency(residency)
		{
		}
		GGLAB_DELETE_COPYABLE_MOVABLE(AssetPublicationCoordinator);

		[[nodiscard]] bool AcquireRetain(AssetContentVersion contentVersion) noexcept;
		void ReleaseRetain(AssetContentVersion contentVersion) noexcept;
		[[nodiscard]] bool HasRetain(AssetContentVersion contentVersion) const noexcept;

		void BeginMeshRollback(MeshID meshId) noexcept;
		void CompleteMeshRollback(MeshID meshId) noexcept;
		[[nodiscard]] bool IsMeshRollbackPending(MeshID meshId) const noexcept;
		[[nodiscard]] bool HasPendingMeshRollbacks() const noexcept;

		void RecordProtectedCancellation() noexcept;
		[[nodiscard]] uint64_t GetProtectedCancellationCount() const noexcept;

		AssetPublicationAccounting m_Accounting{};

	private:
		AssetResidencyCoordinator& m_Residency;
		std::unordered_set<MeshID> m_OrphanedRollbackMeshes;
	};
}
