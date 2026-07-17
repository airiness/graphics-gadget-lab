#include "Core/Precompiled.h"
#include "Graphics/Asset/Store/MaterialStore.h"

namespace gglab
{
	const Material* MaterialStore::Find(MaterialID materialId) const noexcept
	{
		const auto iterator = m_Entries.find(materialId);
		return iterator != m_Entries.end() ? iterator->second.get() : nullptr;
	}

	MaterialStore::InsertResult MaterialStore::Insert(
		std::unique_ptr<Material>&& material) noexcept
	{
		GGLAB_ASSERT_NOT_NULL(material);
		if (!material)
		{
			return {};
		}

		MaterialID materialId = material->m_Id;
		if (!materialId.IsValid())
		{
			materialId = m_IdCounter.Acquire();
			material->m_Id = materialId;
		}
		if (m_Entries.contains(materialId))
		{
			return { .m_Id = materialId };
		}

		const auto [iterator, inserted] = m_Entries.emplace(materialId, std::move(material));
		GGLAB_ASSERT(inserted);
		return {
			.m_Id = inserted ? iterator->first : MaterialID{},
			.m_Inserted = inserted,
		};
	}

	bool MaterialStore::Remove(MaterialID materialId) noexcept
	{
		return m_Entries.erase(materialId) > 0;
	}
}
