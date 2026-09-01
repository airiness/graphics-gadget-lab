#pragma once

#include "GGLabRuntime/Diagnostics/SnapshotCommon.h"

#include <memory>
#include <unordered_map>

namespace gglab
{
	class SnapshotStore
	{
	private:
		class HolderBase
		{
		public:
			virtual ~HolderBase() = default;
			[[nodiscard]] virtual const void* GetValue() const noexcept = 0;
		};

		template <typename T> class Holder final : public HolderBase
		{
		public:
			T m_Value{};

			[[nodiscard]] const void* GetValue() const noexcept override
			{
				return &m_Value;
			}
		};

	public:
		template <typename T> T& GetOrCreate()
		{
			const SnapshotId id = SnapshotIdOf<T>;
			auto iterator = m_Snapshots.find(id);
			if (iterator == m_Snapshots.end())
			{
				auto holder = std::make_unique<Holder<T>>();
				auto* value = &holder->m_Value;
				m_Snapshots.emplace(id, std::move(holder));
				return *value;
			}
			return static_cast<Holder<T>&>(*iterator->second).m_Value;
		}

		[[nodiscard]] const void* Get(SnapshotId id) const noexcept
		{
			const auto iterator = m_Snapshots.find(id);
			return iterator == m_Snapshots.end()
				? nullptr
				: iterator->second->GetValue();
		}

		[[nodiscard]] bool Contains(SnapshotId id) const noexcept
		{
			return m_Snapshots.contains(id);
		}

		void Clear() noexcept { m_Snapshots.clear(); }

	private:
		std::unordered_map<SnapshotId, std::unique_ptr<HolderBase>> m_Snapshots;
	};
}
