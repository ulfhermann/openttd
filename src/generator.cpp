#include "stdafx.h"
#include "map_func.h"
#include "command_func.h"
#include "tile_map.h"
#include "road_type.h"

TileIndex GetOtherAqueductEnd(TileIndex tile_from, TileIndex *tile_to = NULL);
void GenerateCrazyStuff() {
	for (uint x = 15; x < MapSizeX(); x += 192) {
		for (uint y = 15; y < MapSizeY(); y += 192) {
			TileIndex ti = TileXY(x, y);
			while(TileHeight(ti) < 15) {
				DoCommand(ti, TileXY(x + 3, y + 3), LM_RAISE << 1, DC_EXEC | DC_AUTO | DC_FORCE_CLEAR_TILE, CMD_LEVEL_LAND);
			}
			ti = TileXY(x - 1, y + 1);
			DoCommand(ti, GetOtherAqueductEnd(ti), TRANSPORT_WATER << 15, DC_EXEC, CMD_BUILD_BRIDGE);
			ti = TileXY(x + 1, y - 1);
			DoCommand(ti, GetOtherAqueductEnd(ti), TRANSPORT_WATER << 15, DC_EXEC, CMD_BUILD_BRIDGE);
		}
	}
	for (uint x = 111; x < MapSizeX(); x += 192) {
		for (uint y = 111; y < MapSizeY(); y += 192) {
			for (int xi = 0; xi < 200 && x + xi < MapSizeX(); ++xi) {
				if (TileHeight(TileXY(x + xi, y + 1)) == 0) {
					DoCommand(TileXY(x + xi, y + 1),
							TileXY(x + xi + 1, y + 2),
							LM_RAISE << 1, DC_EXEC | DC_AUTO | DC_FORCE_CLEAR_TILE, CMD_LEVEL_LAND);
				}
				if (TileHeight(TileXY(x + xi, y + 2)) == 0) {
					DoCommand(TileXY(x + xi, y + 2),
							TileXY(x + xi + 1, y + 1),
							LM_RAISE << 1, DC_EXEC | DC_AUTO | DC_FORCE_CLEAR_TILE, CMD_LEVEL_LAND);
				}
			}
			for (int yi = 0; yi < 200 && y + yi < MapSizeY(); ++yi) {
				if (TileHeight(TileXY(x + 1, y + yi)) == 0) {
					DoCommand(TileXY(x + 1, y + yi),
							TileXY(x + 2, y + yi + 1),
							LM_RAISE << 1, DC_EXEC | DC_AUTO | DC_FORCE_CLEAR_TILE, CMD_LEVEL_LAND);
				}
				if (TileHeight(TileXY(x + 2, y + yi)) == 0) {
					DoCommand(TileXY(x + 2, y + yi),
							TileXY(x + 1, y + yi + 1),
							LM_RAISE << 1, DC_EXEC | DC_AUTO | DC_FORCE_CLEAR_TILE, CMD_LEVEL_LAND);
				}
			}
			TileIndex ti = TileXY(x, y);
			DoCommand(ti, TileXY(x + 3, y + 3), LM_RAISE << 1, DC_EXEC | DC_AUTO | DC_FORCE_CLEAR_TILE, CMD_LEVEL_LAND);
			DoCommand(ti, TileXY(x + 3, y + 3), LM_RAISE << 1, DC_EXEC | DC_AUTO | DC_FORCE_CLEAR_TILE, CMD_LEVEL_LAND);
			while(TileHeight(ti) > 0) {
				DoCommand(ti, TileXY(x + 3, y + 3), LM_LOWER << 1, DC_EXEC | DC_AUTO | DC_FORCE_CLEAR_TILE, CMD_LEVEL_LAND);
			}
			ti = TileXY(x - 1, y + 1);
			DoCommand(ti, (RoadTypes)(1 << ROADTYPE_ROAD) | (TRANSPORT_ROAD << 8), 0, DC_EXEC, CMD_BUILD_TUNNEL);
			ti = TileXY(x + 1, y - 1);
			DoCommand(ti, (RoadTypes)(1 << ROADTYPE_ROAD) | (TRANSPORT_ROAD << 8), 0, DC_EXEC, CMD_BUILD_TUNNEL);
		}
	}
}
