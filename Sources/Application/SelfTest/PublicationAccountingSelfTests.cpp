#include "Core/Precompiled.h"
#include "Application/SelfTest/PublicationAccountingSelfTests.h"
#include "Graphics/Asset/Publication/AssetResourcePublication.h"

namespace gglab
{
	namespace
	{
		void RunStepAccountingTests(SelfTestContext& context) noexcept
		{
			AssetResourcePublicationPayloadState payload{
				.m_RemainingSourceBytes = 16,
			};
			const AssetResourcePublicationStepUsage copied{
				.m_SourceBytesCopiedToUpload = 16,
			};
			context.Check(payload.RetireStep(copied) == 0 && payload.m_RemainingSourceBytes == 16,
				"Copying upload payload does not release publication source bytes");

			const AssetResourcePublicationStepUsage released{
				.m_SourceBytesReleased = 6,
			};
			context.Check(payload.RetireStep(released) == 6 && payload.m_RemainingSourceBytes == 10,
				"Publication source bytes retire only when ownership is released");

			const AssetResourcePublicationStepUsage mixed{
				.m_SourceBytesReleased = 4,
				.m_SourceBytesCopiedToUpload = 8,
			};
			context.Check(payload.RetireStep(mixed) == 4 && payload.m_RemainingSourceBytes == 6,
				"Mixed publication usage retires released bytes without counting copies");
		}

		void RunTerminalAccountingTests(SelfTestContext& context) noexcept
		{
			AssetResourcePublicationPayloadState completed{
				.m_RemainingSourceBytes = 12,
			};
			context.Check(completed.RetireTerminal() == 12 && completed.m_RemainingSourceBytes == 0,
				"Terminal publication retires all remaining source bytes");
			context.Check(completed.RetireTerminal() == 0,
				"Repeated terminal retirement cannot underflow source accounting");

			AssetResourcePublicationPayloadState failed{
				.m_RemainingSourceBytes = 9,
			};
			const AssetResourcePublicationStepUsage copiedBeforeFailure{
				.m_SourceBytesCopiedToUpload = 9,
			};
			const uint64_t stepRetired = failed.RetireStep(copiedBeforeFailure);
			const uint64_t terminalRetired = failed.RetireTerminal();
			context.Check(
				stepRetired == 0 && terminalRetired == 9 && failed.m_RemainingSourceBytes == 0,
				"Failure cleanup retains copied source until terminal retirement");
		}
	}

	void RunPublicationAccountingSelfTests(SelfTestContext& context) noexcept
	{
		RunStepAccountingTests(context);
		RunTerminalAccountingTests(context);
	}
}
