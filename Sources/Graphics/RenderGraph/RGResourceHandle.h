#pragma once
#include "Core/TypedIndex.h"

namespace gglab
{
	GGLAB_DEFINE_TYPED_INDEX(RGPassNodeIndex, uint32_t);
	GGLAB_DEFINE_TYPED_INDEX(RGResourceNodeIndex, uint32_t);
	GGLAB_DEFINE_TYPED_INDEX(RGVirtualResourceIndex, uint32_t);
	GGLAB_DEFINE_TYPED_INDEX(RGTextureViewId, uint32_t);

	class RGResourceHandle
	{
	public:
		GGLAB_DEFINE_NESTED_TYPED_INDEX(Handle, uint16_t);

		using Version = uint16_t;
		static constexpr Version UnintializedVersion = 0;

	public:
		bool IsValid() const noexcept
		{
			return m_Handle.IsValid();
		}

		void Clear() noexcept
		{
			m_Handle = InvalidHandle;
		}

		Handle GetHandle() const noexcept
		{
			return m_Handle;
		}

		Version GetVersion() const noexcept
		{
			return m_Version;
		}

		auto operator<=>(const RGResourceHandle&) const = default;

	protected:
		RGResourceHandle() noexcept = default;
		explicit RGResourceHandle(Handle handle, Version version) noexcept :
			m_Handle(handle),
			m_Version(version)
		{
		}

	protected:
		Handle m_Handle = InvalidHandle;
		Version m_Version = UnintializedVersion;

		friend class RenderGraph;
		friend class RGBuilder;
	};

	template<typename RESOURCE>
	class RGResourceId : public RGResourceHandle
	{
	public:
		using RGResourceHandle::RGResourceHandle;
		RGResourceId() noexcept = default;
		explicit RGResourceId(const RGResourceHandle& handle) :
			RGResourceHandle(handle)
		{
		}
	};
}
