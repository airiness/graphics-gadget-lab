#pragma once

namespace gglab
{
	class RGResourceHandle
	{
	public:
		using Handle = uint16_t;
		using Version = uint16_t;
		static constexpr Handle InvalidHandle = std::numeric_limits<Handle>::max();
		static constexpr Version UnintializedVersion = 0;

	public:
		bool IsValid() const noexcept
		{
			return m_Handle != InvalidHandle;
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