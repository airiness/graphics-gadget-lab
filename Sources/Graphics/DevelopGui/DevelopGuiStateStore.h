#pragma once
#include "Core/CoreMacros.h"

#include <memory>
#include <unordered_map>

namespace gglab
{
	// Store States for DevelopGui panel
	class DevelopGuiStateStore
	{
	private:
		static const void* TypeTagImpl(const void* ptr) noexcept { return ptr; }

		template<typename T>
		static const void* TypeTag() noexcept
		{
			static int s_Tag;
			return TypeTagImpl(&s_Tag);
		}

		struct IBox
		{
			explicit IBox(const void* tag) noexcept :m_TypeTag(tag) {}
			virtual ~IBox() = default;

			const void* m_TypeTag = nullptr;
		};

		template<typename T>
		struct Box final : IBox
		{
			template<typename... ARGS>
			explicit Box(ARGS&&... args) :
				IBox(TypeTag<T>()),
				m_Value(std::forward<ARGS>(args)...) {}

			T m_Value;
		};

	public:
		DevelopGuiStateStore() noexcept = default;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DevelopGuiStateStore);
		~DevelopGuiStateStore() = default;

		template<typename T, typename... ARGS>
		T& GetOrCreate(uint64_t key, ARGS&&... args) noexcept
		{
			auto iterator = m_StoreMap.find(key);
			if (iterator == m_StoreMap.end())
			{
				auto box = std::make_unique<Box<T>>(std::forward<ARGS>(args)...);
				T* ptr = &box->m_Value;

				m_StoreMap.emplace(key, std::move(box));
				return *ptr;
			}

			IBox* base = iterator->second.get();
			GGLAB_ASSERT(base->m_TypeTag == TypeTag<T>());
			return static_cast<Box<T>*>(base)->m_Value;
		}

		void Forget(uint64_t key) noexcept { m_StoreMap.erase(key); }
		void Clear() noexcept { m_StoreMap.clear(); }

	private:
		std::unordered_map<uint64_t, std::unique_ptr<IBox>> m_StoreMap;
	};
}