#pragma once
#include "Graphics/Asset/Publication/AssetPublicationServices.h"

#include <cstddef>
#include <span>
#include <vector>

namespace gglab
{
	class ModelPublicationJournal final
	{
	public:
		void Reserve(
			size_t claimCount,
			size_t materialCount,
			size_t dependencyCount);

		void RecordClaim(ModelPublicationClaim claim);
		void RecordCreatedMaterial(MaterialID materialId);
		void SetDependencyOwner(ModelPublicationOwnerToken owner) noexcept;
		void RecordDependencyLease(ModelPublicationLeaseToken lease);

		[[nodiscard]] ModelPublicationOwnerToken GetDependencyOwner() const noexcept
		{
			return m_DependencyOwner;
		}
		[[nodiscard]] std::span<const ModelPublicationLeaseToken>
			GetDependencyLeases() const noexcept
		{
			return m_DependencyLeases;
		}
		[[nodiscard]] bool IsCommitted() const noexcept { return m_Committed; }
		[[nodiscard]] bool IsAborted() const noexcept { return m_Aborted; }
		[[nodiscard]] size_t GetClaimCount() const noexcept { return m_Claims.size(); }

		void Commit() noexcept;
		[[nodiscard]] bool ReleaseNextRetain(
			AssetPublicationServicesBase& services) noexcept;
		void Abort(AssetPublicationServicesBase& services) noexcept;

	private:
		std::vector<ModelPublicationClaim> m_Claims;
		std::vector<MaterialID> m_CreatedMaterials;
		ModelPublicationOwnerToken m_DependencyOwner{};
		std::vector<ModelPublicationLeaseToken> m_DependencyLeases;
		size_t m_ReleaseRetainCursor = 0;
		bool m_Committed = false;
		bool m_Aborted = false;
	};
}
