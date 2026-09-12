#pragma once

#include <cstdint>

namespace gglab
{
	enum RGResourceType : uint8_t
	{
		RGTexture,
		RGBuffer,
	};

	// Describes dependency semantics in the render graph.
	// Read      : the pass depends on previous contents.
	// Write     : the pass produces new contents and does not depend on previous contents.
	// ReadWrite : the pass reads previous contents and writes updated contents.
	enum class RGDependencyAccess : uint8_t
	{
		Read,
		Write,
		ReadWrite,
	};

	enum class RGOrderingRequirement : uint8_t
	{
		Ordered,
		Unordered,
	};

	enum class RGDependencyReason : uint8_t
	{
		// Liveness dependency: the consumer needs contents produced by the writer.
		WriterToReader,

		// Execution hazards: order passes only when both sides remain live.
		PreviousWriterToWriter,
		PreviousReaderToWriter,

		// Exporting preserves the final writer's contents. Prior readers are only
		// ordered before the export transition when they remain live independently.
		ExportWriterToExport,
		ExportReaderToExport,
	};

	enum class RGBarrierKind : uint8_t
	{
		Transition,
		Uav,
	};

	enum class RGBarrierReason : uint8_t
	{
		AccessTransition,
		OrderedStorageHazard,
		FinalStateTransition,
	};

	enum class RGPassEncoderType : uint8_t
	{
		Graphics,
		Compute,
		Copy,
	};
}
