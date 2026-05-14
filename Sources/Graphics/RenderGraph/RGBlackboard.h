#pragma once
#include "Core/Hash/FNV1a.h"
#include "Core/StringId.h"
#include "Graphics/RenderGraph/RGArenaAllocator.h"

namespace gglab
{
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
		StringID m_Name;

		auto AsTuple() const noexcept
		{
			return std::make_tuple(
				m_Name.Value(),
				m_TypeId.Value());
		}
		constexpr bool operator==(const BlackboardKey&) const noexcept = default;
	};
	using BlackboardKeyHash = KeyHash<BlackboardKey>;


	class RGBlackboard
	{
	public:
		explicit RGBlackboard(RGArenaAllocator& arenaAllocator) noexcept;

		void Reset() noexcept;

		template<typename T>
		bool Contains() const noexcept { return Contains<T>(StringID{}); }

		template<typename T>
		bool Contains(const char* name) const noexcept { return Contains<T>(StringID(name)); }

		template<typename T>
		T* TryGet() noexcept { return TryGet<T>(StringID{}); }

		template<typename T>
		T* TryGet(const char* name) noexcept { return TryGet<T>(StringID(name)); }

		template<typename T>
		const T* TryGet() const noexcept { return TryGet<T>(StringID{}); }

		template<typename T>
		const T* TryGet(const char* name) const noexcept { return TryGet<T>(StringID(name)); }

		template<typename T>
		T& Get() noexcept { return Get<T>(StringID{}); }

		template<typename T>
		T& Get(const char* name) noexcept { return Get<T>(StringID(name)); }

		template<typename T, typename... ARGS>
		T& Create(ARGS&&... args) noexcept
		{
			return Create<T>(StringID{}, std::forward<ARGS>(args)...);
		}

		template<typename T, typename... ARGS>
		T& Create(const char* name, ARGS&&... args) noexcept
		{
			return Create<T>(StringID(name), std::forward<ARGS>(args)...);
		}

		template<typename T, typename... ARGS>
		T& GetOrCreate(ARGS&&... args) noexcept
		{
			return GetOrCreate<T>(StringID{}, std::forward<ARGS>(args)...);
		}

		template<typename T, typename... ARGS>
		T& GetOrCreate(const char* name, ARGS&&... args) noexcept
		{
			return GetOrCreate<T>(StringID(name), std::forward<ARGS>(args)...);
		}

		template<typename T>
		void Set(const T& value) noexcept { Set<T>(StringID{}, value); }

		template<typename T>
		void Set(const char* name, const T& value) noexcept { Set<T>(StringID(name), value); }

	private:
		template<typename T>
		bool Contains(StringID nameId) const noexcept
		{
			BlackboardKey key{};
			key.m_TypeId = MakeRGTypeId<T>();
			key.m_Name = nameId;

			return m_Container.contains(key);
		}

		template<typename T>
		T* TryGet(StringID nameId) noexcept
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
		T& Get(StringID nameId) noexcept
		{
			T* ptr = TryGet<T>(nameId);
			GGLAB_ASSERT_MSG(ptr != nullptr,
				"RGBlackboard::Get() failed. Entry not found.");
			return *ptr;
		}

		template<typename T, typename... ARGS>
		T& Create(StringID nameId, ARGS&&... args) noexcept
		{
			BlackboardKey key{};
			key.m_TypeId = MakeRGTypeId<T>();
			key.m_Name = nameId;

			GGLAB_ASSERT_MSG(!m_Container.contains(key),
				"RGBlackboard::Create() duplicated entry.");

			T* object = m_ArenaAllocator.MakeTracked<T>(std::forward<ARGS>(args)...);
			m_Container.emplace(key, object);

			return *object;
		}

		template<typename T, typename... ARGS>
		T& GetOrCreate(StringID nameId, ARGS&&... args) noexcept
		{
			if (T* ptr = TryGet<T>(nameId))
			{
				return *ptr;
			}
			return Create<T>(nameId, std::forward<ARGS>(args)...);
		}

		template<typename T>
		void Set(StringID nameId, const T& value) noexcept
		{
			T& object = GetOrCreate<T>(nameId);
			object = value;
		}

	private:
		RGArenaAllocator& m_ArenaAllocator;
		std::unordered_map<BlackboardKey, void*, BlackboardKeyHash> m_Container;
	};
}