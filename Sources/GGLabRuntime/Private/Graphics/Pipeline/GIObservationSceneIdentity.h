#pragma once
#include <cstdint>
#include <memory>

namespace gglab
{
	class RayScene;

	// Observation identity must not prolong a Lab's asset interests into AssetManager
	// shutdown. Query submission leases own snapshots; this weak control-block key
	// distinguishes recycled addresses even after a scene has expired.
	struct GIObservationSceneIdentity
	{
		std::weak_ptr<RayScene> m_Owner;
		uintptr_t m_Address = 0;
		GIObservationSceneIdentity() noexcept = default;
		GIObservationSceneIdentity(const std::shared_ptr<RayScene>& owner) noexcept :
			m_Owner(owner), m_Address(reinterpret_cast<uintptr_t>(owner.get())) {}
		bool operator==(const GIObservationSceneIdentity& other) const noexcept
		{
			return m_Address == other.m_Address && !m_Owner.owner_before(other.m_Owner) &&
				!other.m_Owner.owner_before(m_Owner);
		}
	};
}
