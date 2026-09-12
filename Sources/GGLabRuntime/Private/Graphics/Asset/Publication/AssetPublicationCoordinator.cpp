#include "Graphics/Asset/Publication/AssetPublicationCoordinator.h"
#include "Graphics/Asset/Residency/AssetResidencyCoordinator.h"

namespace gglab
{
	bool AssetPublicationCoordinator::AcquireRetain(AssetContentVersion contentVersion) noexcept
	{
		return m_Residency.AcquirePublicationRetain(contentVersion);
	}

	void AssetPublicationCoordinator::ReleaseRetain(AssetContentVersion contentVersion) noexcept
	{
		m_Residency.ReleasePublicationRetain(contentVersion);
	}

	bool AssetPublicationCoordinator::HasRetain(AssetContentVersion contentVersion) const noexcept
	{
		return m_Residency.HasPublicationRetain(contentVersion);
	}

	bool AssetPublicationCoordinator::HasRetains() const noexcept
	{
		return m_Residency.HasPublicationRetains();
	}

	void AssetPublicationCoordinator::BeginMeshRollback(MeshID meshId) noexcept
	{
		m_OrphanedRollbackMeshes.insert(meshId);
	}

	void AssetPublicationCoordinator::CompleteMeshRollback(MeshID meshId) noexcept
	{
		m_OrphanedRollbackMeshes.erase(meshId);
	}

	bool AssetPublicationCoordinator::IsMeshRollbackPending(MeshID meshId) const noexcept
	{
		return m_OrphanedRollbackMeshes.contains(meshId);
	}

	bool AssetPublicationCoordinator::HasPendingMeshRollbacks() const noexcept
	{
		return !m_OrphanedRollbackMeshes.empty();
	}

	void AssetPublicationCoordinator::RecordProtectedCancellation() noexcept
	{
		++m_ProtectedCancellationCount;
	}

	uint64_t AssetPublicationCoordinator::GetProtectedCancellationCount() const noexcept
	{
		return m_ProtectedCancellationCount;
	}
}
