#pragma once
#include "Core/CoreMacros.h"
#include "Graphics/GraphicsTypes.h"

#include <memory>
#include <unordered_map>

namespace gglab
{
	class MeshStore final
	{
	public:
		using EntryMap = std::unordered_map<MeshID, std::unique_ptr<Mesh>>;
		struct InsertResult
		{
			MeshID m_Id{};
			bool m_Inserted = false;
		};

		MeshStore() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(MeshStore);

		[[nodiscard]] const Mesh* Find(MeshID meshId) const noexcept;
		[[nodiscard]] Mesh* Edit(MeshID meshId) noexcept;
		[[nodiscard]] MeshID Create() noexcept;
		[[nodiscard]] InsertResult Insert(std::unique_ptr<Mesh>&& mesh) noexcept;
		[[nodiscard]] bool Remove(MeshID meshId) noexcept;

		[[nodiscard]] const EntryMap& Entries() const noexcept { return m_Entries; }

	private:
		MeshIDCounter m_IdCounter{ ReservedMeshCount };
		EntryMap m_Entries;
	};
}
