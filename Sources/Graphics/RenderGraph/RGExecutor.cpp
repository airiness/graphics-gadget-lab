#include "Core/Precompiled.h"
#include "Graphics/RenderGraph/RGExecutor.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RHI/RHICommandContext.h"
#include "Graphics/RHI/RHIDevice.h"

namespace gglab
{
	namespace
	{
		RHITextureHandle ResolveTextureHandle(const RGCompiledResource& resource) noexcept
		{
			GGLAB_ASSERT_MSG(resource.m_ResourceType == RGResourceType::RGTexture,
				"RenderGraph texture resolution requires a texture resource.");
			const auto* texture = static_cast<const RGVirtualResource<RGTextureResource>*>(resource.m_Resource);
			return texture->m_Imported ? texture->m_ImportedHandle : texture->m_PhysicalAllocation.m_Texture;
		}

		RHIBufferHandle ResolveBufferHandle(const RGCompiledResource& resource) noexcept
		{
			GGLAB_ASSERT_MSG(resource.m_ResourceType == RGResourceType::RGBuffer,
				"RenderGraph buffer resolution requires a buffer resource.");
			const auto* buffer = static_cast<const RGVirtualResource<RGBufferResource>*>(resource.m_Resource);
			return buffer->m_Imported ? buffer->m_ImportedHandle : buffer->m_PhysicalAllocation.m_Buffer;
		}

		void TrackPassResourceUses(
			RHICommandContext* commandContext,
			const RGExecutionPlan& plan,
			const RGCompiledPass& pass) noexcept
		{
			GGLAB_ASSERT_NOT_NULL(commandContext);
			if (!commandContext)
			{
				return;
			}

			for (const auto& access : pass.m_Accesses)
			{
				const auto& resource = plan.GetResources()[access.m_Resource.Value()];
				if (resource.m_ResourceType == RGResourceType::RGTexture)
				{
					const RHITextureHandle handle = ResolveTextureHandle(resource);
					GGLAB_ASSERT_MSG(handle.IsValid(), "RenderGraph texture access requires a live RHI handle.");
					if (handle.IsValid())
					{
						commandContext->TrackTextureUse(handle);
					}
				}
				else
				{
					const RHIBufferHandle handle = ResolveBufferHandle(resource);
					GGLAB_ASSERT_MSG(handle.IsValid(), "RenderGraph buffer access requires a live RHI handle.");
					if (handle.IsValid())
					{
						commandContext->TrackBufferUse(handle);
					}
				}
			}
		}

		void EmitBarriers(
			RHICommandContext* commandContext,
			const RGExecutionPlan& plan,
			const std::vector<RGBarrierIntent>& barriers) noexcept
		{
			if (barriers.empty())
			{
				return;
			}
			GGLAB_ASSERT_NOT_NULL(commandContext);
			if (!commandContext)
			{
				return;
			}

			std::vector<RHITextureBarrier> textureBarriers;
			std::vector<RHIBufferBarrier> bufferBarriers;
			textureBarriers.reserve(barriers.size());
			bufferBarriers.reserve(barriers.size());
			for (const auto& intent : barriers)
			{
				GGLAB_ASSERT_MSG(intent.m_Resource.IsValid() &&
					intent.m_Resource.Value() < plan.GetResources().size(),
					"RenderGraph barrier references an invalid resource.");
				const auto& resource = plan.GetResources()[intent.m_Resource.Value()];
				if (resource.m_ResourceType == RGResourceType::RGTexture)
				{
					const RHITextureHandle handle = ResolveTextureHandle(resource);
					GGLAB_ASSERT_MSG(handle.IsValid(), "RenderGraph texture barrier requires a live RHI handle.");
					if (handle.IsValid())
					{
						textureBarriers.push_back(
							{
								.m_Texture = handle,
								.m_Before = intent.m_Before,
								.m_After = intent.m_After,
								.m_Subresources = intent.m_Subresources,
							});
					}
				}
				else
				{
					const RHIBufferHandle handle = ResolveBufferHandle(resource);
					GGLAB_ASSERT_MSG(handle.IsValid(), "RenderGraph buffer barrier requires a live RHI handle.");
					if (handle.IsValid())
					{
						bufferBarriers.push_back({ handle, intent.m_Before, intent.m_After });
					}
				}
			}

			commandContext->TextureBarrier(textureBarriers);
			commandContext->BufferBarrier(bufferBarriers);
		}
	}

	RHITextureViewHandle RGExecuteContext::GetViewHandle(RGTextureViewId viewId) const noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_ExecutionPlan);
		GGLAB_ASSERT_NOT_NULL(m_Device);
		if (!m_ExecutionPlan || !m_Device || !viewId.IsValid() ||
			viewId.Value() >= m_ExecutionPlan->GetTextureViews().size())
		{
			return {};
		}

		const auto& view = m_ExecutionPlan->GetTextureViews()[viewId.Value()];
		const auto& resource = m_ExecutionPlan->GetResources()[view.m_Resource.Value()];
		const RHITextureHandle texture = ResolveTextureHandle(resource);
		return texture.IsValid() ? m_Device->CreateTextureView(texture, view.m_Desc) : RHITextureViewHandle{};
	}

	RHIDescriptorHandle RGExecuteContext::GetViewDescriptor(RGTextureViewId viewId) const noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_Device);
		const RHITextureViewHandle view = GetViewHandle(viewId);
		return m_Device && view.IsValid() ? m_Device->GetTextureViewDescriptor(view) : RHIDescriptorHandle{};
	}

	void RGExecutor::Execute(
		const RGExecutionPlan& plan,
		const RGExecutorRuntime& runtime,
		RGExecuteContext& executeContext) noexcept
	{
		GGLAB_ASSERT_NOT_NULL(runtime.m_Device);
		GGLAB_ASSERT_NOT_NULL(runtime.m_TransientResourcePool);
		GGLAB_ASSERT_NOT_NULL(runtime.m_RetireTextures);
		GGLAB_ASSERT_NOT_NULL(runtime.m_RetireBuffers);
		if (!runtime.m_Device || !runtime.m_TransientResourcePool ||
			!runtime.m_RetireTextures || !runtime.m_RetireBuffers)
		{
			return;
		}

		const RGExecutionPlan* previousPlan = executeContext.m_ExecutionPlan;
		RHIDevice* previousDevice = executeContext.m_Device;
		executeContext.m_ExecutionPlan = &plan;
		executeContext.m_Device = runtime.m_Device;

		for (const auto passIndex : plan.GetExecutionOrder())
		{
			const auto& pass = plan.GetPasses()[passIndex.Value()];
			RHICommandContext* profileContext = executeContext.GetGraphicsCommandContext();
			const std::string_view passName = pass.m_NameId.Name();
			if (profileContext)
			{
				profileContext->BeginGpuProfileScope(passName);
			}
			for (const auto resourceIndex : pass.m_AcquireResources)
			{
				const auto& resource = plan.GetResources()[resourceIndex.Value()];
				resource.m_Resource->Devirtualize(runtime.m_TransientResourcePool, resource.m_UsageBits);
			}

			TrackPassResourceUses(executeContext.GetGraphicsCommandContext(), plan, pass);
			EmitBarriers(executeContext.GetGraphicsCommandContext(), plan, pass.m_PreBarriers);
			if (pass.m_Executor)
			{
				pass.m_Executor->Execute(executeContext);
			}
			EmitBarriers(executeContext.GetGraphicsCommandContext(), plan, pass.m_PostBarriers);
			if (profileContext)
			{
				profileContext->EndGpuProfileScope();
			}

			for (const auto resourceIndex : pass.m_ReleaseResources)
			{
				plan.GetResources()[resourceIndex.Value()].m_Resource->Release(
					*runtime.m_RetireTextures,
					*runtime.m_RetireBuffers);
			}
		}

		executeContext.m_ExecutionPlan = previousPlan;
		executeContext.m_Device = previousDevice;
	}
}
