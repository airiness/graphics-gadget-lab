#include "Graphics/AtmosphereSystem.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureViewDescUtils.h"
#include <algorithm>
#include <cstring>
#include <utility>
namespace gglab
{
	AtmosphereSystem::~AtmosphereSystem()
	{
		GGLAB_ASSERT_MSG(m_Shutdown, "AtmosphereSystem must shut down after the device becomes idle.");
	}
	RHITextureDesc AtmosphereSystem::GetTextureDesc(uint32_t i) const noexcept
	{
		return { .m_Format = RHIFormat::R32G32B32A32Float,
			.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::UnorderedAccess,
			.m_Extent = { AtmosphereLutWidths[i], AtmosphereLutHeights[i], 1 },
			.m_DebugName = AtmosphereLutNames[i] };
	}
	bool AtmosphereSystem::Begin(const AtmosphereGPU& parameters, const std::array<uint64_t, 3>& shaders) noexcept
	{
		GGLAB_ASSERT(!m_InFrame && !m_Shutdown);
		m_Diagnostics.m_Available = false;
		uint32_t dirty = AtmosphereDirtyMask(m_Committed, parameters);
		for (uint32_t i = 0; i < 3; ++i)
		{
			if (!m_Textures[i].IsValid())
			{
				const auto desc = GetTextureDesc(i);
				auto view = MakeRHITexture2DViewDesc(desc.m_Format);
				view.m_Type = RHITextureViewType::ShaderResource;
				const bool sampled = m_Device->QueryTextureViewSupport(desc, view).IsSupported();
				view.m_Type = RHITextureViewType::UnorderedAccess;
				if (!sampled || !m_Device->QueryTextureViewSupport(desc, view).IsSupported()) { Disable(); return false; }
				m_Textures[i] = m_Pool->AcquireTexture({ .m_Desc = desc }, AtmosphereLutNames[i]);
				if (!m_Textures[i].IsValid()) { Disable(); return false; }
			}
			if (!m_Initialized[i] || shaders[i] != m_ShaderGenerations[i]) dirty |= 7u & (~0u << i);
		}
		m_Diagnostics.m_Parameters = parameters;
		m_Diagnostics.m_DirtyMask = dirty;
		m_PendingShaderGenerations = shaders;
		if (dirty)
		{
			m_Constants = m_Device->CreateBuffer({ .m_SizeInBytes = 256, .m_Usage = RHIBufferUsage::Constant,
				.m_MemoryUsage = RHIMemoryUsage::CpuToGpu, .m_DebugName = "Atmosphere.Constants" });
			if (!m_Constants.IsValid()) { Disable(); return false; }
			void* mapped = m_Device->MapBuffer(m_Constants, {});
			if (!mapped) { m_Device->DestroyBuffer(m_Constants); m_Constants = {}; Disable(); return false; }
			std::memcpy(mapped, &parameters, sizeof(parameters));
			m_Device->UnmapBuffer(m_Constants, { 0, sizeof(parameters) });
		}
		m_ExecutedMask = 0;
		m_InFrame = true;
		m_Diagnostics.m_Available = true;
		return true;
	}
	void AtmosphereSystem::EndFrame(bool completed, const RHIFencePoint& fence) noexcept
	{
		if (!m_InFrame) return;
		if (fence.IsValid()) m_LastFence = fence;
		if (m_Constants.IsValid())
		{
			if (fence.IsValid()) m_PendingBuffers.push_back({ m_Constants, fence });
			else m_Device->DestroyBuffer(m_Constants);
			m_Constants = {};
		}
		m_InFrame = false;
		if (!completed || !fence.IsValid() || m_ExecutedMask != m_Diagnostics.m_DirtyMask)
		{
			// A cancelled recording cannot certify contents or exported resource states.
			Disable();
			return;
		}
		for (uint32_t i = 0; i < 3; ++i)
		{
			if (m_ExecutedMask & (1u << i)) ++m_Diagnostics.m_Generations[i];
			m_Initialized[i] = true;
		}
		m_Committed = m_Diagnostics.m_Parameters;
		m_ShaderGenerations = m_PendingShaderGenerations;
	}
	void AtmosphereSystem::Disable() noexcept
	{
		GGLAB_ASSERT(!m_InFrame);
		for (uint32_t i = 0; i < 3; ++i)
		{
			if (m_Textures[i].IsValid())
			{
				const bool released = m_LastFence.IsValid()
					? m_Pool->ReleaseTexture(std::move(m_Textures[i]), m_LastFence)
					: m_Pool->ReleaseTextureWithoutSubmission(std::move(m_Textures[i]));
				GGLAB_ASSERT(released);
			}
			m_Initialized[i] = false;
		}
		m_Diagnostics.m_Available = false;
		m_Diagnostics.m_DirtyMask = 7;
	}
	void AtmosphereSystem::Tick() noexcept
	{
		std::erase_if(m_PendingBuffers, [this](const PendingBuffer& buffer)
		{
			if (!m_Device->IsFencePointCompleted(buffer.m_Fence)) return false;
			m_Device->DestroyBuffer(buffer.m_Buffer);
			return true;
		});
	}
	void AtmosphereSystem::Shutdown() noexcept
	{
		GGLAB_ASSERT(!m_InFrame);
		Disable();
		Tick();
		GGLAB_ASSERT(m_PendingBuffers.empty());
		m_Shutdown = true;
	}
}
