#pragma once

static const uint FORWARD_PLUS_TILE_SIZE = 16u;
static const uint FORWARD_PLUS_TILE_LIGHT_CAPACITY = 64u;
static const uint FORWARD_PLUS_TILE_COUNT_MASK = 0x0000ffffu;

uint GetForwardPlusTileIndex(uint2 pixel, uint2 tileCount)
{
	const uint2 tile =
		min(pixel / FORWARD_PLUS_TILE_SIZE, max(tileCount, uint2(1u, 1u)) - uint2(1u, 1u));
	return tile.y * tileCount.x + tile.x;
}

uint GetForwardPlusTileLightCount(uint countAndFlags)
{
	return countAndFlags & FORWARD_PLUS_TILE_COUNT_MASK;
}
