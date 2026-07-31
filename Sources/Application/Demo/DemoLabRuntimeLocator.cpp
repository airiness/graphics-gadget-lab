#include "Core/Precompiled.h"
#include "Application/Demo/DemoLabRuntimeLocator.h"
#include "Application/Demo/DemoBase.h"
#include "Application/Demo/DemoLabHost.h"
#include "Application/Demo/DemoManager.h"

namespace gglab
{
	DemoLabRuntimeLocator::DemoLabRuntimeLocator(
		DemoManager* demoManager, uint32_t labHostIndex) noexcept :
		m_DemoManager(demoManager), m_LabHostIndex(labHostIndex)
	{
	}

	LabRuntime* DemoLabRuntimeLocator::GetLabRuntimeIfCreated() noexcept
	{
		if (DemoLabHost* labHost = GetLabHost())
		{
			return &labHost->GetLabRuntime();
		}
		return nullptr;
	}

	const LabRuntime* DemoLabRuntimeLocator::GetLabRuntimeIfCreated() const noexcept
	{
		if (const DemoLabHost* labHost = GetLabHost())
		{
			return &labHost->GetLabRuntime();
		}
		return nullptr;
	}

	DemoLabHost* DemoLabRuntimeLocator::GetLabHost() const noexcept
	{
		if (!m_DemoManager)
		{
			return nullptr;
		}
		const bool isActive = m_DemoManager->GetActiveIndex() == m_LabHostIndex;
		const bool isPending = m_DemoManager->HasPendingActiveDemo() &&
			m_DemoManager->GetPendingActiveIndex() == m_LabHostIndex;
		if (!isActive && !isPending)
		{
			return nullptr;
		}

		DemoBase* demo = m_DemoManager->GetDemo(m_LabHostIndex);
		return demo ? static_cast<DemoLabHost*>(demo) : nullptr;
	}
}
