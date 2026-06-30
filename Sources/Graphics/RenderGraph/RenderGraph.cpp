#include "Core/Precompiled.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/RGCompiler.h"
#include "Graphics/RenderGraph/RGExecutor.h"

namespace gglab
{
	RenderGraph::RenderGraph(const CreateInfo& createInfo) noexcept :
		m_Device(createInfo.m_Device),
		m_TransientResourcePool(createInfo.m_TransientResourcePool),
		m_ArenaAllocator(1u << 20),
		m_Blackboard(m_ArenaAllocator)
	{
		GGLAB_ASSERT_MSG(m_Device != nullptr, "RHIDevice can not be null.");
		GGLAB_ASSERT_MSG(m_TransientResourcePool != nullptr, "TransientResourcePool can not be null.");
	}

	RenderGraph::~RenderGraph() noexcept = default;

	bool RenderGraph::Compile() noexcept
	{
		m_ExecutionPlan.reset();
		m_CompileDiagnostics.clear();

		RGCompileResult result = RGCompiler::Compile(*this);
		m_CompileDiagnostics = std::move(result.m_Diagnostics);
		if (!result)
		{
			for (const auto& diagnostic : m_CompileDiagnostics)
			{
				if (diagnostic.m_Severity == RGCompileDiagnosticSeverity::Error)
				{
					GGLAB_LOG_GRAPHICS_ERROR("{}", diagnostic.m_Message);
				}
			}
			return false;
		}

		m_ExecutionPlan = std::move(result.m_Plan);
		return true;
	}

	void RenderGraph::Execute(RGExecuteContext& executeContext) noexcept
	{
		GGLAB_ASSERT_MSG(m_ExecutionPlan != nullptr,
			"RenderGraph::Execute requires a successful Compile().");
		if (!m_ExecutionPlan)
		{
			GGLAB_LOG_GRAPHICS_ERROR("RenderGraph execution skipped because compilation failed.");
			return;
		}

		RGExecutor::Execute(
			*m_ExecutionPlan,
			{
				.m_Device = m_Device,
				.m_TransientResourcePool = m_TransientResourcePool,
				.m_RetireTextures = &m_MarkedRetireTextures,
				.m_RetireBuffers = &m_MarkedRetireBuffers,
			},
			executeContext);
	}

	void RenderGraph::Retire(const RHIFencePoint& fencePoint) noexcept
	{
		for (auto& allocation : m_MarkedRetireTextures)
		{
			GGLAB_ASSERT_MSG(allocation.IsValid(), "Retiring an invalid texture allocation.");
			m_TransientResourcePool->RetireTexture(std::move(allocation), fencePoint);
		}
		m_MarkedRetireTextures.clear();

		for (auto& allocation : m_MarkedRetireBuffers)
		{
			GGLAB_ASSERT_MSG(allocation.IsValid(), "Retiring an invalid buffer allocation.");
			m_TransientResourcePool->RetireBuffer(std::move(allocation), fencePoint);
		}
		m_MarkedRetireBuffers.clear();
	}

	RGVirtualResourceBase* RenderGraph::GetVirtualResource(RGResourceHandle handle) const noexcept
	{
		if (!handle.IsValid())
		{
			return nullptr;
		}

		const auto& slot = m_ResourceSlots[handle.GetHandle().Value()];
		const auto handleVersion = handle.GetVersion();
		if (handleVersion == RGResourceHandle::UnintializedVersion ||
			handleVersion > slot.m_Version)
		{
			return nullptr;
		}

		return m_VirtualResources[slot.m_VirtualResourceIndex.Value()];
	}
}
