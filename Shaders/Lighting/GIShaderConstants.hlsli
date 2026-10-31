#pragma once

// Shared CPU/HLSL execution and diagnostic contracts. Values appear in resource
// exports and verification inputs; naming them must not renumber those records.
#if defined(__cplusplus)
#include <cstdint>
namespace gglab::gi_shader
{
	using uint = std::uint32_t;
#endif

static const uint GIQueryGroupWidth = 8;
static const uint GIQueryGroupHeight = 8;

static const uint RaySceneOpaqueMask = 1u;
static const uint RaySceneVertexStride = 56;
static const uint RayScenePositionOffset = 0;
static const uint RaySceneNormalOffset = 12;
static const uint RaySceneUV0Offset = 24;
static const uint RaySceneUV1Offset = 32;
static const uint RaySceneTangentOffset = 40;
static const uint RaySceneTriangleIndexBytes = 3 * 4;

// Receiver alpha stores coverage class; zero is the cleared background.
static const uint GIReceiverOpaque = 1;
static const uint GIReceiverMasked = 2;

static const uint GIReceiverMatchCenter = 1;
static const uint GIReceiverMatchEnvelope = 2;
static const uint GIReceiverMatchInvalid = 3;
static const uint GIReceiverMatchDifferentSurface = 4;
static const uint GIReceiverMatchMiss = 5;
static const uint GIReceiverMatchRaster = 6;

static const uint GIVisibilityBaseSum = 0;
static const uint GIVisibilityCoatSum = 1;
static const uint GIVisibilityBaseCompensation = 2;
static const uint GIVisibilityCoatCompensation = 3;
static const uint GIVisibilitySurfaceCount = 4;
static const uint GIVisibilityBaseActive = 1u;
static const uint GIVisibilityCoatActive = 2u;

static const uint GIProbeBorderWidth = 1;
static const uint GIProbeIrradianceInteriorWidth = 6;
static const uint GIProbeDistanceInteriorWidth = 14;
static const uint GIProbeIrradianceTileWidth = GIProbeIrradianceInteriorWidth + 2 * GIProbeBorderWidth;
static const uint GIProbeDistanceTileWidth = GIProbeDistanceInteriorWidth + 2 * GIProbeBorderWidth;
static const uint GIProbeIrradianceInteriorTexels = GIProbeIrradianceInteriorWidth * GIProbeIrradianceInteriorWidth;
static const uint GIProbeDistanceInteriorTexels = GIProbeDistanceInteriorWidth * GIProbeDistanceInteriorWidth;
static const uint GIProbeInteriorTexels = GIProbeIrradianceInteriorTexels + GIProbeDistanceInteriorTexels;
static const uint GIProbeIrradianceTileTexels = GIProbeIrradianceTileWidth * GIProbeIrradianceTileWidth;
static const uint GIProbeTileTexels = GIProbeIrradianceTileTexels + GIProbeDistanceTileWidth * GIProbeDistanceTileWidth;
static const uint GIProbeGroupThreads = 64;
static const uint GIProbeMaximumRays = 256;
static const uint GIProbeRayStride = 4 * 4; // float4: scene radiance and signed hit distance.

static const uint GIPreviewIrradiance = 29;
static const uint GIPreviewLambertRadiance = 30;
static const uint GIPreviewSunIrradiance = 31;
static const uint GIPreviewEnvironmentIrradiance = 32;
static const uint GIPreviewBaseVisibility = 33;
static const uint GIPreviewCoatVisibility = 34;
static const uint GIPreviewCombinedVisibility = 35;

#if defined(__cplusplus)
}
#endif
