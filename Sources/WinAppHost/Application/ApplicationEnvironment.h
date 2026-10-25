#pragma once
#include "Capture/CaptureEnvironment.h"

namespace gglab
{
	// The build's stamped source declaration, the operating system version and how the
	// application presents frames, for the capture environment record.
	[[nodiscard]] CaptureHostEnvironment MakeApplicationHostEnvironment(bool hidden) noexcept;
}
