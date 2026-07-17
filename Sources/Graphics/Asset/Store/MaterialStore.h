#pragma once
#include "Core/CoreMacros.h"
#include "Graphics/GraphicsTypes.h"

#include <memory>
#include <unordered_map>

namespace gglab
{
	class MaterialStore final
	{
	public:
		using EntryMap = std::unordered_map<MaterialID, std::unique_ptr<Material>>;
		struct InsertResult
		{
			MaterialID m_Id{};
			bool m_Inserted = false;
		};

		MaterialStore() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(MaterialStore);

		[[nodiscard]] const Material* Find(MaterialID materialId) const noexcept;
		[[nodiscard]] InsertResult Insert(std::unique_ptr<Material>&& material) noexcept;
		[[nodiscard]] bool Remove(MaterialID materialId) noexcept;

		[[nodiscard]] const EntryMap& Entries() const noexcept { return m_Entries; }

	private:
		MaterialIDCounter m_IdCounter{ ReservedMaterialCount };
		EntryMap m_Entries;
	};
}
