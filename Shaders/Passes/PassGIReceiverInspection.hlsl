#include <Common/Common.hlsli>
#define GGLAB_GI_FIELD_QUERY_DIAGNOSTICS 1
#include <Lighting/GIFieldSampling.hlsli>

cbuffer Parameters : register(b2)
{
	GIFieldSamplingParameters Field;
	uint PositionIndex;
	uint VertexNormalIndex;
	uint ShadingNormalIndex;
	uint OutputIndex;
	uint2 Pixel;
	uint2 Padding;
	float3 CameraPosition;
	uint CameraPadding;
};

void Store(RWTexture2D<uint> output, uint record, uint4 value)
{
	output[uint2(record * 4, 0)] = value.x;
	output[uint2(record * 4 + 1, 0)] = value.y;
	output[uint2(record * 4 + 2, 0)] = value.z;
	output[uint2(record * 4 + 3, 0)] = value.w;
}

[numthreads(1, 1, 1)]
void CSMain()
{
	Texture2D<float4> positions = ResourceDescriptorHeap[NonUniformResourceIndex(PositionIndex)];
	Texture2D<float4> vertexNormals = ResourceDescriptorHeap[NonUniformResourceIndex(VertexNormalIndex)];
	Texture2D<float4> shadingNormals = ResourceDescriptorHeap[NonUniformResourceIndex(ShadingNormalIndex)];
	RWTexture2D<uint> output = ResourceDescriptorHeap[NonUniformResourceIndex(OutputIndex)];
	const float4 position = positions.Load(int3(Pixel, 0));
	float3 vertexNormal = 0.0.xxx;
	float3 shadingNormal = 0.0.xxx;
	float3 viewDirection = 0.0.xxx;
	GIFieldQuery query = (GIFieldQuery)(0);
	GIFieldQuery ordinary = (GIFieldQuery)(0);
	GIFieldQueryDiagnostics observation = (GIFieldQueryDiagnostics)(0);
	uint validity = 1;
	if (position.a == (float)(GIReceiverOpaque))
	{
		vertexNormal = vertexNormals.Load(int3(Pixel, 0)).xyz;
		shadingNormal = shadingNormals.Load(int3(Pixel, 0)).xyz;
		viewDirection = normalize(CameraPosition - position.xyz);
		query = SampleGIField(Field, position.xyz, shadingNormal, vertexNormal, viewDirection, true, observation);
		ordinary = SampleGIField(Field, position.xyz, shadingNormal, vertexNormal, viewDirection);
		validity = Field.IrradianceInteriorWidth == 0 ? 2 : query.Compatible == 0 ? 3 :
			query.Coverage == 0.0 ? 4 : query.Weight == 0.0 ? 5 : 0;
		if (!all(isfinite(query.Irradiance)) || !isfinite(query.Weight) || !all(isfinite(viewDirection)))
		{
			validity = 6;
		}
	}
	Store(output, 0, uint4(0x47524931u, validity, observation.CageEvaluated, query.Support));
	Store(output, 1, asuint(float4(position.xyz, query.Coverage)));
	Store(output, 2, asuint(float4(observation.BiasedPosition, query.Weight)));
	Store(output, 3, asuint(float4(shadingNormal, (float)(query.Compatible))));
	Store(output, 4, asuint(float4(vertexNormal, Field.ViewBias)));
	Store(output, 5, asuint(float4(viewDirection, Field.Spacing)));
	Store(output, 6, asuint(float4(query.Irradiance, 0)));
	Store(output, 7, asuint(float4(ordinary.Irradiance, ordinary.Weight)));
	Store(output, 8, uint4(Field.Counts, Field.IrradianceInteriorWidth));
	[unroll]
	for (uint corner = 0; corner < 8; ++corner)
	{
		const GIFieldCornerDiagnostics value = observation.Corners[corner];
		const uint first = 9 + corner * 8;
		Store(output, first, uint4(value.Coordinate, value.Classification));
		Store(output, first + 1, uint4(asuint(value.WorldPosition), value.Reason));
		Store(output, first + 2, asuint(float4(value.TrilinearFactors, value.TrilinearWeight)));
		Store(output, first + 3, asuint(float4(value.AtlasM, value.BackfaceFactor)));
		Store(output, first + 4, asuint(float4(value.MeanDistance, value.Variance, value.ReceiverDistance, value.Visibility)));
		Store(output, first + 5, asuint(float4(value.PreVisibilityWeight, value.PreCrushWeight, value.RawWeight, value.NormalizedWeight)));
		Store(output, first + 6, asuint(float4(value.ContributionE, value.NormalizationAvailable)));
		Store(output, first + 7, asuint(float4(value.UsedVariance, value.SamplingEvaluated, 0, 0)));
	}
}
