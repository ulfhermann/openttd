/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file landscape.h Functions related to OTTD's landscape. */

#ifndef LANDSCAPE_H
#define LANDSCAPE_H

#include "core/geometry_type.hpp"
#include "tile_cmd.h"

static const uint SNOW_LINE_MONTHS = 12; ///< Number of months in the snow line table.
static const uint SNOW_LINE_DAYS   = 32; ///< Number of days in each month in the snow line table.

/** Structure describing the height of the snow line each day of the year
 * @ingroup SnowLineGroup */
struct SnowLine {
	byte table[SNOW_LINE_MONTHS][SNOW_LINE_DAYS]; ///< Height of the snow line each day of the year
	byte highest_value; ///< Highest snow line of the year
	byte lowest_value;  ///< Lowest snow line of the year
};

bool IsSnowLineSet();
void SetSnowLine(byte table[SNOW_LINE_MONTHS][SNOW_LINE_DAYS]);
byte GetSnowLine();
byte HighestSnowLine();
byte LowestSnowLine();
void ClearSnowLine();

uint GetPartialZ(int x, int y, Slope corners);
uint GetSlopeZ(int x, int y);
void GetSlopeZOnEdge(Slope tileh, DiagDirection edge, int *z1, int *z2);
int GetSlopeZInCorner(Slope tileh, Corner corner);
Slope GetFoundationSlope(TileIndex tile, uint *z);

/**
 * Map 3D world or tile coordinate to equivalent 2D coordinate as used in the viewports and smallmap.
 * @param x X world or tile coordinate (runs in SW direction in the 2D view).
 * @param y Y world or tile coordinate (runs in SE direction in the 2D view).
 * @param z Z world or tile coordinate (runs in N direction in the 2D view).
 * @return Equivalent coordinate in the 2D view.
 * @see RemapCoords2
 */
static inline Point RemapCoords(int x, int y, int z)
{
	Point pt;
	pt.x = (y - x) * 2;
	pt.y = y + x - z;
	return pt;
}

/**
 * Map 3D world or tile coordinate to equivalent 2D coordinate as used in the viewports and smallmap.
 * Same as #RemapCoords, except the Z coordinate is read from the map.
 * @param x X world or tile coordinate (runs in SW direction in the 2D view).
 * @param y Y world or tile coordinate (runs in SE direction in the 2D view).
 * @return Equivalent coordinate in the 2D view.
 * @see RemapCoords
 */
static inline Point RemapCoords2(int x, int y)
{
	return RemapCoords(x, y, GetSlopeZ(x, y));
}

/**
 * Map 2D viewport or smallmap coordinate to 3D world or tile coordinate.
 * Function assumes <tt>z == 0</tt>. For other values of \p z, add \p z to \a y before the call.
 * @param x X coordinate of the 2D coordinate.
 * @param y Y coordinate of the 2D coordinate.
 * @return X and Y components of equivalent world or tile coordinate.
 * @note Inverse of #RemapCoords function. Smaller values may get rounded.
 */
static inline Point InverseRemapCoords(int x, int y)
{
	Point pt = {(y * 2 - x) >> 2, (y * 2 + x) >> 2};
	return pt;
}

uint ApplyFoundationToSlope(Foundation f, Slope *s);
void DrawFoundation(TileInfo *ti, Foundation f);
bool HasFoundationNW(TileIndex tile, Slope slope_here, uint z_here);
bool HasFoundationNE(TileIndex tile, Slope slope_here, uint z_here);

void DoClearSquare(TileIndex tile);
void RunTileLoop();

void InitializeLandscape();
void GenerateLandscape(byte mode);

#endif /* LANDSCAPE_H */
