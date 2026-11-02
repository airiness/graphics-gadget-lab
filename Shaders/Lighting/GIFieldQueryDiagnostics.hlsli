#pragma once

// Eight float4/uint4 records; matches GIReceiverCorner in Runtime Public.
struct GIFieldCornerDiagnostics
{
	uint3 Coordinate;
	uint Classification;
	float3 WorldPosition;
	uint Reason;
	float3 TrilinearFactors;
	float TrilinearWeight;
	float3 AtlasM;
	float BackfaceFactor;
	float MeanDistance;
	float Variance;
	float ReceiverDistance;
	float Visibility;
	float PreVisibilityWeight;
	float PreCrushWeight;
	float RawWeight;
	float NormalizedWeight;
	float3 ContributionE;
	float NormalizationAvailable;
	float UsedVariance;
	float SamplingEvaluated;
	float2 Padding;
};

struct GIFieldQueryDiagnostics
{
	float3 BiasedPosition;
	uint CageEvaluated;
	GIFieldCornerDiagnostics Corners[8];
};
