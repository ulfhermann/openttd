/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file spritecache.h Functions to cache sprites in memory. */

#ifndef SPRITECACHE_H
#define SPRITECACHE_H

#include "gfx_type.h"

/** Data structure describing a sprite. */
struct Sprite {
	byte height;  ///< Height of the sprite.
	uint16 width; ///< Width of the sprite.
	int16 x_offs; ///< Number of pixels to shift the sprite to the right.
	int16 y_offs; ///< Number of pixels to shift the sprite downwards.
	byte data[];  ///< Sprite data.
};

extern uint _sprite_cache_size;

void *GetRawSprite(SpriteID sprite, SpriteType type);
bool SpriteExists(SpriteID sprite);

SpriteType GetSpriteType(SpriteID sprite);
uint GetOriginFileSlot(SpriteID sprite);
uint GetMaxSpriteID();


static inline const Sprite *GetSprite(SpriteID sprite, SpriteType type)
{
	assert(type != ST_RECOLOUR);
	return (Sprite*)GetRawSprite(sprite, type);
}

static inline const byte *GetNonSprite(SpriteID sprite, SpriteType type)
{
	assert(type == ST_RECOLOUR);
	return (byte*)GetRawSprite(sprite, type);
}

void GfxInitSpriteMem();
void IncreaseSpriteLRU();

bool LoadNextSprite(int load_index, byte file_index, uint file_sprite_id);
bool SkipSpriteData(byte type, uint16 num);
void DupSprite(SpriteID old_spr, SpriteID new_spr);

#endif /* SPRITECACHE_H */
