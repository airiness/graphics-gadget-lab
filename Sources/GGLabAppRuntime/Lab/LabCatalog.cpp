#include "Lab/LabCatalog.h"
#include "AppRuntimeLog.h"
#include "Lab/LabSessionBase.h"

namespace gglab
{
	bool LabCatalog::Register(LabDescriptor descriptor, LabSessionFactory factory) noexcept
	{
		if (!descriptor.m_Id.IsValid() || descriptor.m_DisplayName.empty() || !factory)
		{
			GGLAB_LOG_ERROR("Cannot register an invalid lab descriptor.");
			return false;
		}

		if (FindEntry(descriptor.m_Id))
		{
			GGLAB_LOG_ERROR("Lab '{}' is already registered.", descriptor.m_Id.GetName());
			return false;
		}

		for (const Entry& entry : m_Entries)
		{
			if (entry.m_Descriptor.m_Id.GetHash() == descriptor.m_Id.GetHash())
			{
				GGLAB_LOG_ERROR("Lab ID hash collision between '{}' and '{}'.",
					entry.m_Descriptor.m_Id.GetName(), descriptor.m_Id.GetName());
				return false;
			}
		}

		m_Entries.push_back({
			.m_Descriptor = std::move(descriptor),
			.m_Factory = factory,
			});
		return true;
	}

	const LabDescriptor* LabCatalog::GetDescriptor(uint32_t index) const noexcept
	{
		return index < m_Entries.size() ? &m_Entries[index].m_Descriptor : nullptr;
	}

	const LabDescriptor* LabCatalog::Find(const LabId& id) const noexcept
	{
		const Entry* entry = FindEntry(id);
		return entry ? &entry->m_Descriptor : nullptr;
	}

	std::unique_ptr<LabSessionBase> LabCatalog::Create(
		const LabId& id, const LabSessionCreateInfo& createInfo) const noexcept
	{
		const Entry* entry = FindEntry(id);
		return entry ? entry->m_Factory(createInfo) : nullptr;
	}

	const LabCatalog::Entry* LabCatalog::FindEntry(const LabId& id) const noexcept
	{
		const auto iter = std::ranges::find_if(
			m_Entries, [&id](const Entry& entry) { return entry.m_Descriptor.m_Id == id; });
		return iter != m_Entries.end() ? &*iter : nullptr;
	}
}
