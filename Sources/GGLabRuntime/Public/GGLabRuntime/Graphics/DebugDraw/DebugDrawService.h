#pragma once
#include "GGLabRuntime/Graphics/DebugDraw/DebugDraw.h"

#include <memory>
#include <vector>

namespace gglab
{
	class RHIDevice;

	// Read-only channel observation for tooling. Channel identities stay stable
	// StringIDs; no live command storage crosses this boundary.
	class DebugDrawChannelViewBase
	{
	public:
		virtual ~DebugDrawChannelViewBase() = default;

		[[nodiscard]] virtual std::vector<DebugDrawChannelState> GetChannelStates()
			const noexcept = 0;
	};

	// Narrow channel mutation for tooling. Commands affect channel filtering and
	// persistence; they do not submit draw commands.
	class DebugDrawChannelControlBase
	{
	public:
		virtual ~DebugDrawChannelControlBase() = default;

		virtual void SetChannelEnabled(StringID channel, bool enabled) noexcept = 0;
		virtual void ClearChannel(StringID channel) noexcept = 0;
	};

	inline constexpr uint32_t DebugDrawDefaultMaxVertexCount = 131'072;

	struct DebugDrawServiceCreateInfo
	{
		RHIDevice* m_Device = nullptr;
		uint32_t m_FrameSlotCount = 0;
		uint32_t m_MaxVertexCountPerFrame = DebugDrawDefaultMaxVertexCount;
	};

	// Runtime-owned debug-draw service. Hosts compose it through this factory and
	// submit through the Public DebugDrawContext. Tooling receives the separate
	// channel view/control capabilities instead of the service itself.
	class DebugDrawService
	{
	public:
		virtual ~DebugDrawService() = default;

		[[nodiscard]] virtual DebugDrawContext& GetContext() noexcept = 0;
		[[nodiscard]] virtual const DebugDrawFrameView& SealFrame(uint32_t frameSlot,
			float deltaTime, const DebugDrawCullContext& cullContext) noexcept = 0;
		virtual void Clear() noexcept = 0;
		[[nodiscard]] virtual DebugDrawChannelViewBase* GetChannelView() noexcept = 0;
		[[nodiscard]] virtual DebugDrawChannelControlBase* GetChannelControl() noexcept = 0;
	};

	[[nodiscard]] std::unique_ptr<DebugDrawService> CreateDebugDrawService(
		const DebugDrawServiceCreateInfo& createInfo) noexcept;
}
