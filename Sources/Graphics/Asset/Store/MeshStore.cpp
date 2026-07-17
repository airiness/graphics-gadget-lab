#include "Core/Precompiled.h"
#include "Graphics/Asset/Store/MeshStore.h"

namespace gglab
{
	const Mesh* MeshStore::Find(MeshID meshId) const noexcept
	{
		const auto iterator = m_Entries.find(meshId);
		return iterator != m_Entries.end() ? iterator->second.get() : nullptr;
	}

	Mesh* MeshStore::Edit(MeshID meshId) noexcept
	{
		return const_cast<Mesh*>(std::as_const(*this).Find(meshId));
	}

	MeshID MeshStore::Create() noexcept
	{
		const MeshID meshId = m_IdCounter.Acquire();
		auto mesh = std::make_unique<Mesh>();
		mesh->m_Id = meshId;
		const auto [iterator, inserted] = m_Entries.emplace(meshId, std::move(mesh));
		GGLAB_ASSERT(inserted);
		return inserted ? iterator->first : MeshID{};
	}

	MeshStore::InsertResult MeshStore::Insert(std::unique_ptr<Mesh>&& mesh) noexcept
	{
		GGLAB_ASSERT_NOT_NULL(mesh);
		if (!mesh)
		{
			return {};
		}

		MeshID meshId = mesh->m_Id;
		if (!meshId.IsValid())
		{
			meshId = m_IdCounter.Acquire();
			mesh->m_Id = meshId;
		}
		if (m_Entries.contains(meshId))
		{
			return { .m_Id = meshId };
		}

		const auto [iterator, inserted] = m_Entries.emplace(meshId, std::move(mesh));
		GGLAB_ASSERT(inserted);
		return {
			.m_Id = inserted ? iterator->first : MeshID{},
			.m_Inserted = inserted,
		};
	}

	bool MeshStore::Remove(MeshID meshId) noexcept
	{
		return m_Entries.erase(meshId) > 0;
	}
}
