#pragma once
#include "Application/Lab/LabTypes.h"

#include <memory>
#include <vector>

namespace gglab
{
	class LabSessionBase;
	struct LabSessionCreateInfo;

	using LabSessionFactory = std::unique_ptr<LabSessionBase>(*)(
		const LabSessionCreateInfo& createInfo) noexcept;

	struct LabRegistration
	{
		LabDescriptor m_Descriptor;
		LabSessionFactory m_Factory = nullptr;
	};

	class LabCatalog
	{
	public:
		bool Register(LabDescriptor descriptor, LabSessionFactory factory) noexcept;

		uint32_t GetCount() const noexcept { return static_cast<uint32_t>(m_Entries.size()); }
		const LabDescriptor* GetDescriptor(uint32_t index) const noexcept;
		const LabDescriptor* Find(const LabId& id) const noexcept;
		std::unique_ptr<LabSessionBase> Create(
			const LabId& id, const LabSessionCreateInfo& createInfo) const noexcept;

	private:
		struct Entry
		{
			LabDescriptor m_Descriptor;
			LabSessionFactory m_Factory = nullptr;
		};

		const Entry* FindEntry(const LabId& id) const noexcept;

		std::vector<Entry> m_Entries;
	};
}
