#include "Graphics/Capture/FrameCaptureService.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipelineSystem.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"
#include "ShaderArtifactRuntime/NonFiniteCounterABI.h"

#include <array>
#include <memory>
#include <iterator>
#include <utility>

namespace gglab
{
	bool FrameCaptureService::PrepareProductionCounters(uint64_t frameSerial, Tap& tap, RGProductionCounters& counters) noexcept
	{
		if (!m_ScanShaders || !m_ScanResolver || !m_ScanPipelineSystem)
		{
			return false;
		}
		if (!m_ScanPipelineSystem->IsAlive(m_ProductionClearLayout))
		{
			RHIBindingLayoutDesc desc;
			desc.m_Slots[0] = { .m_Type = RHIBindingType::ReadWriteStorageBuffer, .m_Visibility = RHIShaderStage::Compute, .m_Binding = 0 };
			desc.m_Slots[1] = { .m_Type = RHIBindingType::InlineConstants, .m_Visibility = RHIShaderStage::Compute, .m_Binding = 2, .m_SizeInBytes = 28 };
			desc.m_SlotCount = 2;
			desc.m_DebugName = "NonFiniteCounters.Clear";
			m_ProductionClearLayout = m_ScanPipelineSystem->CreateBindingLayout(desc);
		}
		if (!m_ProductionClearRecipe.m_CSId.IsValid())
		{
			m_ProductionClearRecipe.m_CSId = m_ScanShaders->LoadProgram(shader_programs::NonFiniteCountersClearCompute);
		}
		if (!m_ProductionClearRecipe.m_CSId.IsValid() || !m_ProductionClearLayout.IsValid())
		{
			return false;
		}
		m_ProductionClearRecipe.m_BindingLayout = m_ProductionClearLayout;
		const auto pipeline = m_ScanResolver->Resolve(m_ProductionClearSlot, m_ProductionClearRecipe, {
			.m_TypeName = "NonFiniteCounters",
			.m_DisplayName = "Non-finite production counters",
			.m_Category = RenderPassCategory::Debug,
			.m_Type = RenderPassType::Compute });
		if (!pipeline.IsValid())
		{
			return false;
		}
		tap.m_FrameSerial = frameSerial;
		tap.m_Source = FrameCaptureSource::NonFiniteScan;
		tap.m_AdapterCompatibilityIdentity = m_Device->GetAdapterCompatibilityIdentity();
		tap.m_ProductionCounters = true;
		tap.m_NonFiniteScanActive = true;
		if (!ProductionCaptureProducer::Allocate(*m_Device, tap.m_Readback))
		{
			return false;
		}
		// The clear pass writes this frame's injection request beside the header, so the
		// readback records exactly what the frame's injection points were asked to do.
		std::array<uint32_t, 7> constants{ static_cast<uint32_t>(frameSerial), static_cast<uint32_t>(frameSerial >> 32) };
		if (m_NonFiniteInjection)
		{
			constants[2] = m_NonFiniteInjection->m_PointIndex + 1;
			constants[3] = GetNonFiniteInjectionBits(m_NonFiniteInjection->m_Value);
			constants[4] = m_NonFiniteInjection->m_X;
			constants[5] = m_NonFiniteInjection->m_Y;
			constants[6] = m_NonFiniteInjection->m_Component;
		}
		counters = { .m_Readback = tap.m_Readback.GetBuffer(),
			.m_Bytes = nonfinite::BufferBytes,
			.m_Binding = nonfinite::CounterBinding,
			.m_Clear = [pipeline, constants](RGExecuteContext& context, RGBufferId record)
				{
					auto* commands = context.GetDirectComputeCommandContext();
					commands->SetPipeline(pipeline);
					commands->SetReadWriteBuffer(0, context.GetBufferHandle(record));
					commands->SetInlineConstants(1, constants);
					commands->Dispatch((nonfinite::SiteCount * nonfinite::ProgramCount + 63) / 64, 1, 1);
				} };
		return true;
	}

	bool FrameCaptureService::SetNonFiniteInjection(std::optional<NonFiniteInjection> injection) noexcept
	{
		if (!m_ProductionEnabled || (injection && !IsValidNonFiniteInjection(*injection)))
		{
			return false;
		}
		m_NonFiniteInjection = injection;
		return true;
	}

	void FrameCaptureService::PublishProductionFrame(std::shared_ptr<const NonFiniteProductionFrame> frame) noexcept
	{
		m_ProductionResults.push_back(frame);
		m_LatestProduction = std::move(frame);
	}

	void FrameCaptureService::ConsumeNonFiniteProductionFrames(std::vector<std::shared_ptr<const NonFiniteProductionFrame>>& frames) noexcept
	{
		CollectCompleted();
		frames.insert(frames.end(), std::make_move_iterator(m_ProductionResults.begin()), std::make_move_iterator(m_ProductionResults.end()));
		m_ProductionResults.clear();
	}

	void FrameCaptureService::PublishProductionTap(Tap& tap) noexcept
	{
		PublishProductionFrame(ProductionCaptureProducer::ReadProduct(
			*m_Device, tap.m_Readback, tap.m_PayloadLease, tap.m_FrameSerial));
	}
}
