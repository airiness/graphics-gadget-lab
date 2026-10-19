#pragma once
#include "GGLabRuntime/Graphics/Pipeline/TypedConstantBinding.h"

#include <array>
#include <cstdint>
#include <span>

namespace gglab
{
	// One recording capability for the small migrated constant set. Native commands
	// consume readiness directly; there is no optional commit/validation operation.
	template <typename Context>
	class ScopedConstantBindings final
	{
	public:
		template <ConstantPayload... Payloads>
		ScopedConstantBindings(Context& context, const ResolvedRenderLayout& layout,
			const ResolvedBindingRef<Payloads>&... references) noexcept :
			m_Context(context), m_Layout(layout)
		{
			static_assert(sizeof...(Payloads) > 0 && sizeof...(Payloads) <= 3);
			m_Count = sizeof...(Payloads);
			const std::array parameters{ references.GetParameterIndex(layout)... };
			for (uint32_t index = 0; index < m_Count; ++index)
			{
				if (!parameters[index])
				{
					context.RejectRecording();
					return;
				}
				m_Parameters[index] = *parameters[index];
				for (uint32_t previous = 0; previous < index; ++previous)
				{
					if (m_Parameters[previous] == m_Parameters[index])
					{
						context.RejectRecording();
						return;
					}
				}
			}
			m_Scope = context.BeginConstantBindingScope(std::span(m_Parameters).first(m_Count));
		}

		~ScopedConstantBindings() { m_Context.EndConstantBindingScope(m_Scope); }
		ScopedConstantBindings(const ScopedConstantBindings&) = delete;
		ScopedConstantBindings& operator=(const ScopedConstantBindings&) = delete;

		[[nodiscard]] bool SetPipeline(const ResolvedRenderPipeline& selection) noexcept
		{
			const auto pipeline = selection.GetHandle(m_Layout, std::derived_from<Context, RHIComputeCommandContext>);
			if (m_Context.HasRecordingError() ||
				!m_Context.IsConstantBindingScopeCurrent(m_Scope) || !pipeline.IsValid())
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
					"Scoped pipeline selection rejected: stale recording, logical contract or pipeline bind point.");
				m_Context.RejectRecording();
				return false;
			}
			m_Context.SetPipeline(pipeline);
			m_PipelineRevision = m_Context.GetPipelineBindingRevision();
			m_HasPipeline = m_Context.GetSelectedBindingPipeline() == pipeline;
			if (!m_HasPipeline)
			{
				m_Context.RejectRecording();
			}
			return m_HasPipeline && !m_Context.HasRecordingError();
		}

		template <ConstantPayload T>
		[[nodiscard]] bool Bind(const ResolvedBindingRef<T>& reference,
			const TypedConstantSnapshot<T>& snapshot) noexcept
		{
			const auto parameter = reference.GetParameterIndex(m_Layout);
			if (m_HasPipeline && parameter &&
				m_PipelineRevision == m_Context.GetPipelineBindingRevision() &&
				m_Context.IsConstantBindingScopeCurrent(m_Scope))
			{
				for (uint32_t index = 0; index < m_Count; ++index)
				{
					if (m_Parameters[index] == *parameter)
					{
						return BindConstantSnapshot(m_Context, m_Layout, reference, snapshot) &&
							m_Context.MarkConstantBindingReady(m_Scope, index);
					}
				}
			}
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
				"Scoped constant binding rejected for payload {}: stale scope, pipeline or logical reference.",
				ConstantPayloadTraits<T>::Tag.Value());
			m_Context.RejectRecording();
			return false;
		}

	private:
		Context& m_Context;
		ResolvedRenderLayout m_Layout;
		std::array<uint32_t, 3> m_Parameters{};
		uint32_t m_Count = 0;
		uint64_t m_Scope = 0;
		uint64_t m_PipelineRevision = 0;
		bool m_HasPipeline = false;
	};
}
