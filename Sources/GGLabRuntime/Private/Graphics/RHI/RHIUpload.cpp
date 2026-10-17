#include "GGLabRuntime/Graphics/RHI/RHIUpload.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"
#include "Graphics/RHI/RHIUploadPool.h"

namespace gglab
{
	RHIUploadWriter::~RHIUploadWriter() noexcept
	{
		GGLAB_ASSERT_MSG(!m_Batch, "An upload writer must be detached before it is destroyed.");
	}

	RHIConstantBufferBinding RHIUploadWriter::UploadConstants(
		std::span<const std::byte> bytes) noexcept
	{
		if (!m_Batch)
		{
			GGLAB_LOG_GRAPHICS_ERROR("Uploads require a writer attached to a frame batch.");
			return {};
		}
		return m_Batch->Upload(bytes);
	}

	bool RHIUploadWriter::Owns(const RHIConstantBufferBinding& binding) const noexcept
	{
		return m_Batch && m_Batch->Owns(binding);
	}

	bool RHIUploadWriter::IsRecording() const noexcept
	{
		return m_Batch && m_Batch->IsRecording();
	}

	bool RHIUploadWriter::HasFailed() const noexcept
	{
		return m_Batch && m_Batch->HasFailed();
	}

	void RHIUploadWriter::Fail() noexcept
	{
		if (m_Batch)
		{
			m_Batch->Fail();
		}
	}

	RHIUploadBatchId RHIUploadWriter::GetBatchId() const noexcept
	{
		return m_Batch ? m_Batch->GetId() : 0;
	}

	RHIFrameUploads RHIUploadWriter::GetFrameUploads() noexcept
	{
		return RHIFrameUploads(*this, GetBatchId());
	}

	RHIConstantBufferBinding RHIFrameUploads::UploadConstants(
		std::span<const std::byte> bytes) const noexcept
	{
		if (!IsCurrent())
		{
			// A capability kept past its frame must not feed whichever batch is attached now.
			GGLAB_LOG_GRAPHICS_ERROR(
				"Frame uploads were used outside the frame of batch {}.", m_BatchId);
			if (m_Writer)
			{
				m_Writer->Fail();
			}
			return {};
		}
		return m_Writer->UploadConstants(bytes);
	}

	bool RHIFrameUploads::IsCurrent() const noexcept
	{
		return m_Writer && m_BatchId != 0 && m_Writer->GetBatchId() == m_BatchId;
	}
}
