#pragma once

#include "Diagnostics/SnapshotCommon.h"

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
		};

		template<typename T>
		class Holder final : public HolderBase
		{
		public:
			T m_Value{};
		};

	public:
		template<typename T>
		T& GetOrCreate()
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

		template<typename T>
		[[nodiscard]] const T* Get() const noexcept
		{
			const auto iterator = m_Snapshots.find(SnapshotIdOf<T>);
			return iterator == m_Snapshots.end() ? nullptr :
				&static_cast<const Holder<T>&>(*iterator->second).m_Value;
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
