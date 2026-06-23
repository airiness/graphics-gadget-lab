#include "Core/Precompiled.h"
#include "Graphics/RHI/RHIResource.h"
#include "Graphics/RHI/RHIDevice.h"

#include <utility>

namespace gglab
{
	RHITextureOwner::RHITextureOwner(RHIDevice* device, RHITextureHandle handle) noexcept :
		m_Device(device),
		m_Handle(handle)
	{}

	RHITextureOwner::~RHITextureOwner() noexcept
	{
		Reset();
	}

	RHITextureOwner::RHITextureOwner(RHITextureOwner&& rhs) noexcept :
		m_Device(std::exchange(rhs.m_Device, nullptr)),
		m_Handle(std::exchange(rhs.m_Handle, {}))
	{}

	RHITextureOwner& RHITextureOwner::operator=(RHITextureOwner&& rhs) noexcept
	{
		if (this != &rhs)
		{
			Reset();
			m_Device = std::exchange(rhs.m_Device, nullptr);
			m_Handle = std::exchange(rhs.m_Handle, {});
		}
		return *this;
	}

	void RHITextureOwner::Reset() noexcept
	{
		if (m_Device && m_Handle.IsValid())
		{
			m_Device->DestroyTexture(m_Handle);
		}
		m_Device = nullptr;
		m_Handle.Reset();
	}

	RHIBufferOwner::RHIBufferOwner(RHIDevice* device, RHIBufferHandle handle) noexcept :
		m_Device(device),
		m_Handle(handle)
	{}

	RHIBufferOwner::~RHIBufferOwner() noexcept
	{
		Reset();
	}

	RHIBufferOwner::RHIBufferOwner(RHIBufferOwner&& rhs) noexcept :
		m_Device(std::exchange(rhs.m_Device, nullptr)),
		m_Handle(std::exchange(rhs.m_Handle, {}))
	{}

	RHIBufferOwner& RHIBufferOwner::operator=(RHIBufferOwner&& rhs) noexcept
	{
		if (this != &rhs)
		{
			Reset();
			m_Device = std::exchange(rhs.m_Device, nullptr);
			m_Handle = std::exchange(rhs.m_Handle, {});
		}
		return *this;
	}

	void RHIBufferOwner::Reset() noexcept
	{
		if (m_Device && m_Handle.IsValid())
		{
			m_Device->DestroyBuffer(m_Handle);
		}
		m_Device = nullptr;
		m_Handle.Reset();
	}
}
