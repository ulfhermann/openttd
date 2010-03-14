/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file road_internal.h Functions used internally by the roads. */

#ifndef ROAD_INTERNAL_H
#define ROAD_INTERNAL_H

#include "tile_cmd.h"
#include "road_type.h"

/**
 * Clean up unneccesary RoadBits of a planed tile.
 * @param tile current tile
 * @param org_rb planed RoadBits
 * @return optimised RoadBits
 */
RoadBits CleanUpRoadBits(const TileIndex tile, RoadBits org_rb);

CommandCost CheckAllowRemoveRoad(TileIndex tile, RoadBits remove, Owner owner, RoadType rt, DoCommandFlag flags, bool town_check = true);

/**
 * Draw the catenary for tram road bits
 * @param ti   information about the tile (position, slope)
 * @param tram the roadbits to draw the catenary for
 */
void DrawTramCatenary(const TileInfo *ti, RoadBits tram);

#endif /* ROAD_INTERNAL_H */
