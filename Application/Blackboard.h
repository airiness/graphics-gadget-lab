#pragma once
#include "FNV1a.h"
#include "StringId.h"

namespace gglab
{
	class RGArenaAllocator;

	struct RGTypeId
	{
		const void* m_Ptr = nullptr;
		uintptr_t Value() const noexcept { return reinterpret_cast<uintptr_t>(m_Ptr); }
		constexpr bool operator==(const RGTypeId&) const noexcept = default;
	};

	template<typename T>
	inline RGTypeId MakeRGTypeId() noexcept
	{
		static int32_t s_Unique;
		return RGTypeId{ &s_Unique };
	}

	struct BlackboardKey
	{
		RGTypeId m_TypeId;
		StringId m_Name;

		auto AsTuple() const noexcept
		{
			return std::make_tuple(
				m_Name.Value(),
				m_TypeId.Value());
		}
		constexpr bool operator==(const BlackboardKey&) const noexcept = default;
	};
	using BlackboardKeyHash = KeyHash<BlackboardKey>;


	class Blackboard
	{
	public:
		explicit Blackboard(RGArenaAllocator& arenaAllocator) noexcept;

		void Reset() noexcept;

		template<typename T>
		bool Contains() const noexcept { return Contains<T>(StringId{}); }

		template<typename T>
		bool Contains(const char* name) const noexcept { return Contains<T>(StringId(name)); }

		template<typename T>
		T* TryGet() noexcept { return TryGet<T>(StringId{}); }

		template<typename T>
		T* TryGet(const char* name) noexcept { return TryGet<T>(StringId(name)); }

		template<typename T>
		const T* TryGet() const noexcept { return TryGet<T>(StringId{}); }

		template<typename T>
		const T* TryGet(const char* name) const noexcept { return TryGet<T>(StringId(name)); }

		template<typename T>
		T& Get() noexcept { return Get<T>(StringId{}); }

		template<typename T>
		T& Get(const char* name) noexcept { return Get<T>(StringId(name)); }

		template<typename T, typename... ARGS>
		T& Create(ARGS&&... args) noexcept
		{
			return Create<T>(StringId{}, std::forward<ARGS>(args)...);
		}

		template<typename T, typename... ARGS>
		T& Create(const char* name, ARGS&&... args) noexcept
		{
			return Create<T>(StringId(name), std::forward<ARGS>(args)...);
		}

		template<typename T, typename... ARGS>
		T& GetOrCreate(ARGS&&... args) noexcept
		{
			return GetOrCreate<T>(StringId{}, std::forward<ARGS>(args)...);
		}

		template<typename T, typename... ARGS>
		T& GetOrCreate(const char* name, ARGS&&... args) noexcept
		{
			return GetOrCreate<T>(StringId(name), std::forward<ARGS>(args)...);
		}

		template<typename T>
		void Set(const T& value) noexcept { Set<T>(StringId{}, value); }

		template<typename T>
		void Set(const char* name, const T& value) noexcept { Set<T>(StringId(name), value); }

	private:
		template<typename T>
		bool Contains(StringId nameId) const noexcept
		{
			BlackboardKey key{};
			key.m_TypeId = MakeRGTypeId<T>();
			key.m_Name = nameId;

			return m_Container.find(key) != m_Container.end();
		}

		template<typename T>
		T* TryGet(StringId nameId) noexcept
		{
			BlackboardKey key{};
			key.m_TypeId = MakeRGTypeId<T>();
			key.m_Name = nameId;

			auto iter = m_Container.find(key);
			if (iter == m_Container.end())
			{
				return nullptr;
			}

			return static_cast<T*>(iter->second);
		}

		template<typename T>
		T& Get(StringId nameId) noexcept
		{
			T* ptr = TryGet<T>(nameId);
			GGLAB_ASSERT_MSG(ptr != nullptr,
				"Blackboard::Get() failed. Entry not found.");
			return *ptr;
		}

		template<typename T, typename... ARGS>
		T& Create(StringId nameId, ARGS&&... args) noexcept
		{
			BlackboardKey key{};
			key.m_TypeId = MakeRGTypeId<T>();
			key.m_Name = nameId;

			GGLAB_ASSERT_MSG(m_Container.find(key) == m_Container.end(),
				"RGBlackboard::Create() duplicated entry.");

			T* object = m_ArenaAllocator.MakeTracked<T>(std::forward<ARGS>(args)...);
			m_Container.emplace(key, object);

			return *object;
		}

		template<typename T, typename... ARGS>
		T& GetOrCreate(StringId nameId, ARGS&&... args) noexcept
		{
			if (T* ptr = TryGet<T>(nameId))
			{
				return *ptr;
			}
			return Create<T>(nameId, std::forward<ARGS>(args)...);
		}

		template<typename T>
		void Set(StringId nameId, const T& value) noexcept
		{
			T& object = GetOrCreate<T>(nameId);
			object = value;
		}

	private:
		RGArenaAllocator& m_ArenaAllocator;
		std::unordered_map<BlackboardKey, void*, BlackboardKeyHash> m_Container;
	};
}