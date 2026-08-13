#include "Graphics/Asset/Publication/ModelPublicationJournal.h"
#include "Core/CoreMacros.h"

#include <cstddef>
#include <utility>

namespace gglab
{
	void ModelPublicationJournal::Reserve(
		size_t claimCount, size_t materialCount, size_t dependencyCount)
	{
		m_Claims.reserve(claimCount);
		m_CreatedMaterials.reserve(materialCount);
		m_DependencyLeases.reserve(dependencyCount);
	}

	void ModelPublicationJournal::RecordClaim(ModelPublicationClaim claim)
	{
		GGLAB_ASSERT(!m_Committed && !m_Aborted);
		GGLAB_ASSERT(claim.m_ContentVersion.IsValid());
		GGLAB_ASSERT(claim.m_Retain.IsValid());
		m_Claims.push_back(std::move(claim));
	}

	void ModelPublicationJournal::RecordCreatedMaterial(MaterialID materialId)
	{
		GGLAB_ASSERT(!m_Committed && !m_Aborted);
		GGLAB_ASSERT(materialId.IsValid());
		m_CreatedMaterials.push_back(materialId);
	}

	void ModelPublicationJournal::SetDependencyOwner(ModelPublicationOwnerToken owner) noexcept
	{
		GGLAB_ASSERT(!m_Committed && !m_Aborted);
		GGLAB_ASSERT(owner.IsValid());
		GGLAB_ASSERT(!m_DependencyOwner.IsValid());
		m_DependencyOwner = owner;
	}

	void ModelPublicationJournal::RecordDependencyLease(ModelPublicationLeaseToken lease)
	{
		GGLAB_ASSERT(!m_Committed && !m_Aborted);
		GGLAB_ASSERT(lease.IsValid());
		m_DependencyLeases.push_back(lease);
	}

	void ModelPublicationJournal::Commit() noexcept
	{
		GGLAB_ASSERT(!m_Committed && !m_Aborted);
		m_Committed = true;
		m_DependencyOwner = {};
		m_DependencyLeases.clear();
	}

	bool ModelPublicationJournal::ReleaseNextRetain(AssetPublicationServicesBase& services) noexcept
	{
		GGLAB_ASSERT(m_Committed && !m_Aborted);
		if (m_ReleaseRetainCursor >= m_Claims.size())
		{
			return false;
		}

		ModelPublicationClaim& claim = m_Claims[m_ReleaseRetainCursor++];
		services.ReleaseRetain(claim.m_Retain);
		claim.m_Retain = {};
		return m_ReleaseRetainCursor < m_Claims.size();
	}

	void ModelPublicationJournal::Abort(AssetPublicationServicesBase& services) noexcept
	{
		if (m_Aborted)
		{
			return;
		}
		m_Aborted = true;

		if (m_Committed)
		{
			for (ModelPublicationClaim& claim : m_Claims)
			{
				services.ReleaseRetain(claim.m_Retain);
				claim.m_Retain = {};
				services.CancelClaimIfUnreferenced(claim);
			}
			return;
		}

		for (ModelPublicationLeaseToken lease : m_DependencyLeases)
		{
			services.ReleaseDependencyLease(lease);
		}
		m_DependencyLeases.clear();
		if (m_DependencyOwner.IsValid())
		{
			services.DestroyDependencyOwner(m_DependencyOwner);
			m_DependencyOwner = {};
		}

		for (auto claim = m_Claims.rbegin(); claim != m_Claims.rend(); ++claim)
		{
			services.ReleaseRetain(claim->m_Retain);
			claim->m_Retain = {};
			services.RollbackClaimIfUnreferenced(*claim);
		}

		for (MaterialID materialId : m_CreatedMaterials)
		{
			services.RemoveMaterial(materialId);
		}
	}
}
