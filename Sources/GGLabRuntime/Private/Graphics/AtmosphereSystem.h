#pragma once
#include "GGLabRuntime/Graphics/Atmosphere.h"
#include "Graphics/Resource/PersistentTexturePool.h"
#include <vector>
namespace gglab
{
	class AtmosphereSystem final : public RenderAtmosphereAccess
	{
	public:
		AtmosphereSystem(RHIDevice* device, PersistentTexturePool* pool) noexcept : m_Device(device), m_Pool(pool) {}
		~AtmosphereSystem() override;
		bool Begin(const AtmosphereGPU& parameters, const std::array<uint64_t, 3>& shaderGenerations) noexcept override;
		void Disable() noexcept override;
		RHITextureHandle GetTexture(uint32_t i) const noexcept override { return m_Textures[i].GetTexture(); }
		RHITextureDesc GetTextureDesc(uint32_t i) const noexcept override;
		bool IsInitialized(uint32_t i) const noexcept override { return m_Initialized[i]; }
		RHIBufferHandle GetConstants() const noexcept override { return m_Constants; }
		void NotifyExecuted(uint32_t i) noexcept override { m_ExecutedMask |= 1u << i; }
		AtmosphereDiagnostics GetDiagnostics() const noexcept override { return m_Diagnostics; }
		void EndFrame(bool completed, const RHIFencePoint& fence) noexcept;
		void Tick() noexcept;
		void Shutdown() noexcept;
	private:
		RHIDevice* m_Device;
		PersistentTexturePool* m_Pool;
		std::array<PersistentTextureAllocation, 3> m_Textures;
		std::array<bool, 3> m_Initialized{};
		std::array<uint64_t, 3> m_ShaderGenerations{};
		std::array<uint64_t, 3> m_PendingShaderGenerations{};
		AtmosphereGPU m_Committed{};
		AtmosphereDiagnostics m_Diagnostics{};
		RHIFencePoint m_LastFence{};
		RHIBufferHandle m_Constants{};
		struct PendingBuffer { RHIBufferHandle m_Buffer; RHIFencePoint m_Fence; };
		std::vector<PendingBuffer> m_PendingBuffers;
		uint32_t m_ExecutedMask = 0;
		bool m_InFrame = false;
		bool m_Shutdown = false;
	};
}
