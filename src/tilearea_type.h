/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tilearea_type.h Type for storing the 'area' of something uses on the map. */

#ifndef TILEAREA_TYPE_H
#define TILEAREA_TYPE_H

#include "tile_type.h"

/** Represents the covered area of e.g. a rail station */
struct TileArea {
	TileIndex tile; ///< The base tile of the area
	uint8 w;        ///< The width of the area
	uint8 h;        ///< The height of the area

	/** Just construct this tile area */
	TileArea() {}

	/**
	 * Construct this tile area with some set values
	 * @param tile the base tile
	 * @param w the width
	 * @param h the height
	 */
	TileArea(TileIndex tile, uint8 w, uint8 h) : tile(tile), w(w), h(h) {}

	TileArea(TileIndex start, TileIndex end);


	void Add(TileIndex to_add);

	/**
	 * Clears the 'tile area', i.e. make the tile invalid.
	 */
	void Clear()
	{
		this->tile = INVALID_TILE;
		this->w    = 0;
		this->h    = 0;
	}

	bool Intersects(const TileArea &ta) const;

	void ClampToMap();
};

#endif /* TILEAREA_TYPE_H */
