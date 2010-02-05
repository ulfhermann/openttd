/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file water.h Functions related to water (management) */

#ifndef WATER_H
#define WATER_H

#include "tile_type.h"
#include "company_type.h"
#include "slope_type.h"

void TileLoop_Water(TileIndex tile);
bool FloodHalftile(TileIndex t);
void DoFloodTile(TileIndex target);

void ConvertGroundTilesIntoWaterTiles();

void DrawShipDepotSprite(int x, int y, int image);
void DrawWaterClassGround(const struct TileInfo *ti);
void DrawShoreTile(Slope tileh);

void MakeWaterKeepingClass(TileIndex tile, Owner o);

#endif /* WATER_H */
