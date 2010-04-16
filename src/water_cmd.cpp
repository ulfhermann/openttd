/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file water_cmd.cpp Handling of water tiles. */

#include "stdafx.h"
#include "cmd_helper.h"
#include "landscape.h"
#include "viewport_func.h"
#include "command_func.h"
#include "town.h"
#include "news_func.h"
#include "depot_base.h"
#include "depot_func.h"
#include "water.h"
#include "industry_map.h"
#include "newgrf_canal.h"
#include "strings_func.h"
#include "functions.h"
#include "vehicle_func.h"
#include "sound_func.h"
#include "company_func.h"
#include "clear_map.h"
#include "tree_map.h"
#include "aircraft.h"
#include "effectvehicle_func.h"
#include "tunnelbridge_map.h"
#include "station_base.h"
#include "ai/ai.hpp"
#include "core/random_func.hpp"

#include "table/sprites.h"
#include "table/strings.h"

/**
 * Describes the behaviour of a tile during flooding.
 */
enum FloodingBehaviour {
	FLOOD_NONE,    ///< The tile does not flood neighboured tiles.
	FLOOD_ACTIVE,  ///< The tile floods neighboured tiles.
	FLOOD_PASSIVE, ///< The tile does not actively flood neighboured tiles, but it prevents them from drying up.
	FLOOD_DRYUP,   ///< The tile drys up if it is not constantly flooded from neighboured tiles.
};

/**
 * Describes from which directions a specific slope can be flooded (if the tile is floodable at all).
 */
static const uint8 _flood_from_dirs[] = {
	(1 << DIR_NW) | (1 << DIR_SW) | (1 << DIR_SE) | (1 << DIR_NE), // SLOPE_FLAT
	(1 << DIR_NE) | (1 << DIR_SE),                                 // SLOPE_W
	(1 << DIR_NW) | (1 << DIR_NE),                                 // SLOPE_S
	(1 << DIR_NE),                                                 // SLOPE_SW
	(1 << DIR_NW) | (1 << DIR_SW),                                 // SLOPE_E
	0,                                                             // SLOPE_EW
	(1 << DIR_NW),                                                 // SLOPE_SE
	(1 << DIR_N ) | (1 << DIR_NW) | (1 << DIR_NE),                 // SLOPE_WSE, SLOPE_STEEP_S
	(1 << DIR_SW) | (1 << DIR_SE),                                 // SLOPE_N
	(1 << DIR_SE),                                                 // SLOPE_NW
	0,                                                             // SLOPE_NS
	(1 << DIR_E ) | (1 << DIR_NE) | (1 << DIR_SE),                 // SLOPE_NWS, SLOPE_STEEP_W
	(1 << DIR_SW),                                                 // SLOPE_NE
	(1 << DIR_S ) | (1 << DIR_SW) | (1 << DIR_SE),                 // SLOPE_ENW, SLOPE_STEEP_N
	(1 << DIR_W ) | (1 << DIR_SW) | (1 << DIR_NW),                 // SLOPE_SEN, SLOPE_STEEP_E
};

/**
 * Marks tile dirty if it is a canal or river tile.
 * Called to avoid glitches when flooding tiles next to canal tile.
 *
 * @param tile tile to check
 */
static inline void MarkTileDirtyIfCanalOrRiver(TileIndex tile)
{
	if (IsTileType(tile, MP_WATER) && (IsCanal(tile) || IsRiver(tile))) MarkTileDirtyByTile(tile);
}

/**
 * Marks the tiles around a tile as dirty, if they are canals or rivers.
 *
 * @param tile The center of the tile where all other tiles are marked as dirty
 * @ingroup dirty
 */
static void MarkCanalsAndRiversAroundDirty(TileIndex tile)
{
	for (Direction dir = DIR_BEGIN; dir < DIR_END; dir++) {
		MarkTileDirtyIfCanalOrRiver(tile + TileOffsByDir(dir));
	}
}


/** Build a ship depot.
 * @param tile tile where ship depot is built
 * @param flags type of operation
 * @param p1 bit 0 depot orientation (Axis)
 * @param p2 unused
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdBuildShipDepot(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	Axis axis = Extract<Axis, 0, 1>(p1);

	TileIndex tile2 = tile + (axis == AXIS_X ? TileDiffXY(1, 0) : TileDiffXY(0, 1));

	if (!IsWaterTile(tile) || !IsWaterTile(tile2)) {
		return_cmd_error(STR_ERROR_MUST_BE_BUILT_ON_WATER);
	}

	if (IsBridgeAbove(tile) || IsBridgeAbove(tile2)) return_cmd_error(STR_ERROR_MUST_DEMOLISH_BRIDGE_FIRST);

	if (GetTileSlope(tile, NULL) != SLOPE_FLAT || GetTileSlope(tile2, NULL) != SLOPE_FLAT) {
		/* Prevent depots on rapids */
		return_cmd_error(STR_ERROR_SITE_UNSUITABLE);
	}

	WaterClass wc1 = GetWaterClass(tile);
	WaterClass wc2 = GetWaterClass(tile2);
	CommandCost ret = DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
	if (ret.Failed()) return ret;
	ret = DoCommand(tile2, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
	if (ret.Failed()) return ret;

	if (!Depot::CanAllocateItem()) return CMD_ERROR;

	if (flags & DC_EXEC) {
		Depot *depot = new Depot(tile);
		depot->town_index = ClosestTownFromTile(tile, UINT_MAX)->index;

		MakeShipDepot(tile,  _current_company, depot->index, DEPOT_NORTH, axis, wc1);
		MakeShipDepot(tile2, _current_company, depot->index, DEPOT_SOUTH, axis, wc2);
		MarkTileDirtyByTile(tile);
		MarkTileDirtyByTile(tile2);
	}

	return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_DEPOT_SHIP]);
}

void MakeWaterKeepingClass(TileIndex tile, Owner o)
{
	assert(IsTileType(tile, MP_WATER) || (IsTileType(tile, MP_STATION) && (IsBuoy(tile) || IsDock(tile) || IsOilRig(tile))) || IsTileType(tile, MP_INDUSTRY));

	WaterClass wc = GetWaterClass(tile);

	/* Autoslope might turn an originally canal or river tile into land */
	uint z;
	if (GetTileSlope(tile, &z) != SLOPE_FLAT) wc = WATER_CLASS_INVALID;

	if (wc == WATER_CLASS_SEA && z > 0) wc = WATER_CLASS_CANAL;

	switch (wc) {
		case WATER_CLASS_SEA:   MakeSea(tile);                break;
		case WATER_CLASS_CANAL: MakeCanal(tile, o, Random()); break;
		case WATER_CLASS_RIVER: MakeRiver(tile, Random());    break;
		default:                DoClearSquare(tile);          break;
	}
}

static CommandCost RemoveShipDepot(TileIndex tile, DoCommandFlag flags)
{
	if (!IsShipDepot(tile)) return CMD_ERROR;

	CommandCost ret = CheckTileOwnership(tile);
	if (ret.Failed()) return ret;

	TileIndex tile2 = GetOtherShipDepotTile(tile);

	/* do not check for ship on tile when company goes bankrupt */
	if (!(flags & DC_BANKRUPT)) {
		CommandCost ret = EnsureNoVehicleOnGround(tile);
		if (ret.Succeeded()) ret = EnsureNoVehicleOnGround(tile2);
		if (ret.Failed()) return ret;
	}

	if (flags & DC_EXEC) {
		/* Kill the depot, which is registered at the northernmost tile. Use that one */
		delete Depot::GetByTile(tile);

		MakeWaterKeepingClass(tile,  GetTileOwner(tile));
		MakeWaterKeepingClass(tile2, GetTileOwner(tile2));
		MarkTileDirtyByTile(tile);
		MarkTileDirtyByTile(tile2);
	}

	return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_CLEAR_DEPOT_SHIP]);
}

/** build a shiplift */
static CommandCost DoBuildShiplift(TileIndex tile, DiagDirection dir, DoCommandFlag flags)
{
	/* middle tile */
	CommandCost ret = DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
	if (ret.Failed()) return ret;

	int delta = TileOffsByDiagDir(dir);
	/* lower tile */
	WaterClass wc_lower = IsWaterTile(tile - delta) ? GetWaterClass(tile - delta) : WATER_CLASS_CANAL;

	ret = DoCommand(tile - delta, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
	if (ret.Failed()) return ret;
	if (GetTileSlope(tile - delta, NULL) != SLOPE_FLAT) {
		return_cmd_error(STR_ERROR_LAND_SLOPED_IN_WRONG_DIRECTION);
	}

	/* upper tile */
	WaterClass wc_upper = IsWaterTile(tile + delta) ? GetWaterClass(tile + delta) : WATER_CLASS_CANAL;

	ret = DoCommand(tile + delta, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
	if (ret.Failed()) return ret;
	if (GetTileSlope(tile + delta, NULL) != SLOPE_FLAT) {
		return_cmd_error(STR_ERROR_LAND_SLOPED_IN_WRONG_DIRECTION);
	}

	if ((MayHaveBridgeAbove(tile) && IsBridgeAbove(tile)) ||
	    (MayHaveBridgeAbove(tile - delta) && IsBridgeAbove(tile - delta)) ||
	    (MayHaveBridgeAbove(tile + delta) && IsBridgeAbove(tile + delta))) {
		return_cmd_error(STR_ERROR_MUST_DEMOLISH_BRIDGE_FIRST);
	}

	if (flags & DC_EXEC) {
		MakeLock(tile, _current_company, dir, wc_lower, wc_upper);
		MarkTileDirtyByTile(tile);
		MarkTileDirtyByTile(tile - delta);
		MarkTileDirtyByTile(tile + delta);
		MarkCanalsAndRiversAroundDirty(tile - delta);
		MarkCanalsAndRiversAroundDirty(tile + delta);
	}

	return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_CLEAR_WATER] * 22 >> 3);
}

static CommandCost RemoveShiplift(TileIndex tile, DoCommandFlag flags)
{
	if (GetTileOwner(tile) != OWNER_NONE) {
		CommandCost ret = CheckTileOwnership(tile);
		if (ret.Failed()) return ret;
	}

	TileIndexDiff delta = TileOffsByDiagDir(GetLockDirection(tile));

	/* make sure no vehicle is on the tile. */
	CommandCost ret = EnsureNoVehicleOnGround(tile);
	if (ret.Succeeded()) ret = EnsureNoVehicleOnGround(tile + delta);
	if (ret.Succeeded()) ret = EnsureNoVehicleOnGround(tile - delta);
	if (ret.Failed()) return ret;

	if (flags & DC_EXEC) {
		DoClearSquare(tile);
		MakeWaterKeepingClass(tile + delta, GetTileOwner(tile));
		MakeWaterKeepingClass(tile - delta, GetTileOwner(tile));
		MarkTileDirtyByTile(tile - delta);
		MarkTileDirtyByTile(tile + delta);
		MarkCanalsAndRiversAroundDirty(tile - delta);
		MarkCanalsAndRiversAroundDirty(tile + delta);
	}

	return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_CLEAR_WATER] * 2);
}

/** Builds a lock (ship-lift)
 * @param tile tile where to place the lock
 * @param flags type of operation
 * @param p1 unused
 * @param p2 unused
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdBuildLock(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	DiagDirection dir = GetInclinedSlopeDirection(GetTileSlope(tile, NULL));
	if (dir == INVALID_DIAGDIR) return_cmd_error(STR_ERROR_LAND_SLOPED_IN_WRONG_DIRECTION);

	/* Disallow building of locks on river rapids */
	if (IsWaterTile(tile)) return_cmd_error(STR_ERROR_SITE_UNSUITABLE);

	return DoBuildShiplift(tile, dir, flags);
}

/** Build a piece of canal.
 * @param tile end tile of stretch-dragging
 * @param flags type of operation
 * @param p1 start tile of stretch-dragging
 * @param p2 specifies canal (0), water (1) or river (2); last two can only be built in scenario editor
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdBuildCanal(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	if (p1 >= MapSize()) return CMD_ERROR;

	/* Outside of the editor you can only build canals, not oceans */
	if (p2 != 0 && _game_mode != GM_EDITOR) return CMD_ERROR;

	TileArea ta(tile, p1);

	/* Outside the editor you can only drag canals, and not areas */
	if (_game_mode != GM_EDITOR && ta.w != 1 && ta.h != 1) return CMD_ERROR;

	CommandCost cost(EXPENSES_CONSTRUCTION);
	TILE_AREA_LOOP(tile, ta) {
		CommandCost ret;

		Slope slope = GetTileSlope(tile, NULL);
		if (slope != SLOPE_FLAT && (p2 != 2 || !IsInclinedSlope(slope))) {
			return_cmd_error(STR_ERROR_FLAT_LAND_REQUIRED);
		}

		/* can't make water of water! */
		if (IsTileType(tile, MP_WATER) && (!IsTileOwner(tile, OWNER_WATER) || p2 == 1)) continue;

		ret = DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
		if (ret.Failed()) return ret;
		cost.AddCost(ret);

		if (flags & DC_EXEC) {
			if (TileHeight(tile) == 0 && p2 == 1) {
				MakeSea(tile);
			} else if (p2 == 2) {
				MakeRiver(tile, Random());
			} else {
				MakeCanal(tile, _current_company, Random());
			}
			MarkTileDirtyByTile(tile);
			MarkCanalsAndRiversAroundDirty(tile);
		}

		cost.AddCost(_price[PR_CLEAR_WATER]);
	}

	if (cost.GetCost() == 0) {
		return_cmd_error(STR_ERROR_ALREADY_BUILT);
	} else {
		return cost;
	}
}

static CommandCost ClearTile_Water(TileIndex tile, DoCommandFlag flags)
{
	switch (GetWaterTileType(tile)) {
		case WATER_TILE_CLEAR: {
			if (flags & DC_NO_WATER) return_cmd_error(STR_ERROR_CAN_T_BUILD_ON_WATER);

			/* Make sure freeform edges are allowed or it's not an edge tile. */
			if (!_settings_game.construction.freeform_edges && (!IsInsideMM(TileX(tile), 1, MapMaxX() - 1) ||
					!IsInsideMM(TileY(tile), 1, MapMaxY() - 1))) {
				return_cmd_error(STR_ERROR_TOO_CLOSE_TO_EDGE_OF_MAP);
			}

			/* Make sure no vehicle is on the tile */
			CommandCost ret = EnsureNoVehicleOnGround(tile);
			if (ret.Failed()) return ret;

			if (GetTileOwner(tile) != OWNER_WATER && GetTileOwner(tile) != OWNER_NONE) {
				CommandCost ret = CheckTileOwnership(tile);
				if (ret.Failed()) return ret;
			}

			if (flags & DC_EXEC) {
				DoClearSquare(tile);
				MarkCanalsAndRiversAroundDirty(tile);
			}
			return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_CLEAR_WATER]);
		}

		case WATER_TILE_COAST: {
			Slope slope = GetTileSlope(tile, NULL);

			/* Make sure no vehicle is on the tile */
			CommandCost ret = EnsureNoVehicleOnGround(tile);
			if (ret.Failed()) return ret;

			if (flags & DC_EXEC) {
				DoClearSquare(tile);
				MarkCanalsAndRiversAroundDirty(tile);
			}
			if (IsSlopeWithOneCornerRaised(slope)) {
				return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_CLEAR_WATER]);
			} else {
				return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_CLEAR_ROUGH]);
			}
		}

		case WATER_TILE_LOCK: {
			static const TileIndexDiffC _shiplift_tomiddle_offs[] = {
				{ 0,  0}, {0,  0}, { 0, 0}, {0,  0}, // middle
				{-1,  0}, {0,  1}, { 1, 0}, {0, -1}, // lower
				{ 1,  0}, {0, -1}, {-1, 0}, {0,  1}, // upper
			};

			if (flags & DC_AUTO) return_cmd_error(STR_ERROR_BUILDING_MUST_BE_DEMOLISHED);
			if (_current_company == OWNER_WATER) return CMD_ERROR;
			/* move to the middle tile.. */
			return RemoveShiplift(tile + ToTileIndexDiff(_shiplift_tomiddle_offs[GetSection(tile)]), flags);
		}

		case WATER_TILE_DEPOT:
			if (flags & DC_AUTO) return_cmd_error(STR_ERROR_BUILDING_MUST_BE_DEMOLISHED);
			return RemoveShipDepot(tile, flags);

		default:
			NOT_REACHED();
	}
}

/**
 * return true if a tile is a water tile wrt. a certain direction.
 *
 * @param tile The tile of interest.
 * @param from The direction of interest.
 * @return true iff the tile is water in the view of 'from'.
 *
 */
static bool IsWateredTile(TileIndex tile, Direction from)
{
	switch (GetTileType(tile)) {
		case MP_WATER:
			switch (GetWaterTileType(tile)) {
				default: NOT_REACHED();
				case WATER_TILE_DEPOT: case WATER_TILE_CLEAR: return true;
				case WATER_TILE_LOCK: return DiagDirToAxis(GetLockDirection(tile)) == DiagDirToAxis(DirToDiagDir(from));

				case WATER_TILE_COAST:
					switch (GetTileSlope(tile, NULL)) {
						case SLOPE_W: return (from == DIR_SE) || (from == DIR_E) || (from == DIR_NE);
						case SLOPE_S: return (from == DIR_NE) || (from == DIR_N) || (from == DIR_NW);
						case SLOPE_E: return (from == DIR_NW) || (from == DIR_W) || (from == DIR_SW);
						case SLOPE_N: return (from == DIR_SW) || (from == DIR_S) || (from == DIR_SE);
						default: return false;
					}
			}

		case MP_RAILWAY:
			if (GetRailGroundType(tile) == RAIL_GROUND_WATER) {
				assert(IsPlainRail(tile));
				switch (GetTileSlope(tile, NULL)) {
					case SLOPE_W: return (from == DIR_SE) || (from == DIR_E) || (from == DIR_NE);
					case SLOPE_S: return (from == DIR_NE) || (from == DIR_N) || (from == DIR_NW);
					case SLOPE_E: return (from == DIR_NW) || (from == DIR_W) || (from == DIR_SW);
					case SLOPE_N: return (from == DIR_SW) || (from == DIR_S) || (from == DIR_SE);
					default: return false;
				}
			}
			return false;

		case MP_STATION:
			if (IsOilRig(tile)) {
				/* Do not draw waterborders inside of industries.
				 * Note: There is no easy way to detect the industry of an oilrig tile. */
				TileIndex src_tile = tile + TileOffsByDir(from);
				if ((IsTileType(src_tile, MP_STATION) && IsOilRig(src_tile)) ||
				    (IsTileType(src_tile, MP_INDUSTRY))) return true;

				return GetWaterClass(tile) != WATER_CLASS_INVALID;
			}
			return (IsDock(tile) && GetTileSlope(tile, NULL) == SLOPE_FLAT) || IsBuoy(tile);

		case MP_INDUSTRY: {
			/* Do not draw waterborders inside of industries.
			 * Note: There is no easy way to detect the industry of an oilrig tile. */
			TileIndex src_tile = tile + TileOffsByDir(from);
			if ((IsTileType(src_tile, MP_STATION) && IsOilRig(src_tile)) ||
			    (IsTileType(src_tile, MP_INDUSTRY) && GetIndustryIndex(src_tile) == GetIndustryIndex(tile))) return true;

			return IsIndustryTileOnWater(tile);
		}

		case MP_TUNNELBRIDGE: return GetTunnelBridgeTransportType(tile) == TRANSPORT_WATER && ReverseDiagDir(GetTunnelBridgeDirection(tile)) == DirToDiagDir(from);

		default:          return false;
	}
}

static void DrawWaterEdges(SpriteID base, TileIndex tile)
{
	uint wa;

	/* determine the edges around with water. */
	wa  = IsWateredTile(TILE_ADDXY(tile, -1,  0), DIR_SW) << 0;
	wa += IsWateredTile(TILE_ADDXY(tile,  0,  1), DIR_NW) << 1;
	wa += IsWateredTile(TILE_ADDXY(tile,  1,  0), DIR_NE) << 2;
	wa += IsWateredTile(TILE_ADDXY(tile,  0, -1), DIR_SE) << 3;

	if (!(wa & 1)) DrawGroundSprite(base,     PAL_NONE);
	if (!(wa & 2)) DrawGroundSprite(base + 1, PAL_NONE);
	if (!(wa & 4)) DrawGroundSprite(base + 2, PAL_NONE);
	if (!(wa & 8)) DrawGroundSprite(base + 3, PAL_NONE);

	/* right corner */
	switch (wa & 0x03) {
		case 0: DrawGroundSprite(base + 4, PAL_NONE); break;
		case 3: if (!IsWateredTile(TILE_ADDXY(tile, -1, 1), DIR_W)) DrawGroundSprite(base + 8, PAL_NONE); break;
	}

	/* bottom corner */
	switch (wa & 0x06) {
		case 0: DrawGroundSprite(base + 5, PAL_NONE); break;
		case 6: if (!IsWateredTile(TILE_ADDXY(tile, 1, 1), DIR_N)) DrawGroundSprite(base + 9, PAL_NONE); break;
	}

	/* left corner */
	switch (wa & 0x0C) {
		case  0: DrawGroundSprite(base + 6, PAL_NONE); break;
		case 12: if (!IsWateredTile(TILE_ADDXY(tile, 1, -1), DIR_E)) DrawGroundSprite(base + 10, PAL_NONE); break;
	}

	/* upper corner */
	switch (wa & 0x09) {
		case 0: DrawGroundSprite(base + 7, PAL_NONE); break;
		case 9: if (!IsWateredTile(TILE_ADDXY(tile, -1, -1), DIR_S)) DrawGroundSprite(base + 11, PAL_NONE); break;
	}
}

/** Draw a plain sea water tile with no edges */
static void DrawSeaWater(TileIndex tile)
{
	DrawGroundSprite(SPR_FLAT_WATER_TILE, PAL_NONE);
}

/** draw a canal styled water tile with dikes around */
static void DrawCanalWater(TileIndex tile)
{
	DrawGroundSprite(SPR_FLAT_WATER_TILE, PAL_NONE);

	/* Test for custom graphics, else use the default */
	SpriteID dikes_base = GetCanalSprite(CF_DIKES, tile);
	if (dikes_base == 0) dikes_base = SPR_CANAL_DIKES_BASE;

	DrawWaterEdges(dikes_base, tile);
}

struct LocksDrawTileStruct {
	int8 delta_x, delta_y, delta_z;
	byte width, height, depth;
	SpriteID image;
};

#include "table/water_land.h"

static void DrawWaterStuff(const TileInfo *ti, const WaterDrawTileStruct *wdts,
	PaletteID palette, uint base, bool draw_ground)
{
	SpriteID water_base = GetCanalSprite(CF_WATERSLOPE, ti->tile);
	SpriteID locks_base = GetCanalSprite(CF_LOCKS, ti->tile);

	/* If no custom graphics, use defaults */
	if (water_base == 0) water_base = SPR_CANALS_BASE;
	if (locks_base == 0) {
		locks_base = SPR_SHIPLIFT_BASE;
	} else {
		/* If using custom graphics, ignore the variation on height */
		base = 0;
	}

	SpriteID image = wdts++->image;
	if (image < 4) image += water_base;
	if (draw_ground) DrawGroundSprite(image, PAL_NONE);

	/* End now if buildings are invisible */
	if (IsInvisibilitySet(TO_BUILDINGS)) return;

	for (; wdts->delta_x != 0x80; wdts++) {
		AddSortableSpriteToDraw(wdts->image + base + ((wdts->image < 24) ? locks_base : 0), palette,
			ti->x + wdts->delta_x, ti->y + wdts->delta_y,
			wdts->size_x, wdts->size_y,
			wdts->size_z, ti->z + wdts->delta_z,
			IsTransparencySet(TO_BUILDINGS));
	}
}

static void DrawRiverWater(const TileInfo *ti)
{
	SpriteID image = SPR_FLAT_WATER_TILE;
	SpriteID edges_base = GetCanalSprite(CF_RIVER_EDGE, ti->tile);

	if (ti->tileh != SLOPE_FLAT) {
		image = GetCanalSprite(CF_RIVER_SLOPE, ti->tile);
		if (image == 0) {
			switch (ti->tileh) {
				case SLOPE_NW: image = SPR_WATER_SLOPE_Y_DOWN; break;
				case SLOPE_SW: image = SPR_WATER_SLOPE_X_UP;   break;
				case SLOPE_SE: image = SPR_WATER_SLOPE_Y_UP;   break;
				case SLOPE_NE: image = SPR_WATER_SLOPE_X_DOWN; break;
				default:       image = SPR_FLAT_WATER_TILE;    break;
			}
		} else {
			switch (ti->tileh) {
				default: NOT_REACHED();
				case SLOPE_SE:             edges_base += 12; break;
				case SLOPE_NE: image += 1; edges_base += 24; break;
				case SLOPE_SW: image += 2; edges_base += 36; break;
				case SLOPE_NW: image += 3; edges_base += 48; break;
			}
		}
	}

	DrawGroundSprite(image, PAL_NONE);

	/* Draw river edges if available. */
	if (edges_base > 48) DrawWaterEdges(edges_base, ti->tile);
}

void DrawShoreTile(Slope tileh)
{
	/* Converts the enum Slope into an offset based on SPR_SHORE_BASE.
	 * This allows to calculate the proper sprite to display for this Slope */
	static const byte tileh_to_shoresprite[32] = {
		0, 1, 2, 3, 4, 16, 6, 7, 8, 9, 17, 11, 12, 13, 14, 0,
		0, 0, 0, 0, 0,  0, 0, 0, 0, 0,  0,  5,  0, 10, 15, 0,
	};

	assert(!IsHalftileSlope(tileh)); // Halftile slopes need to get handled earlier.
	assert(tileh != SLOPE_FLAT);     // Shore is never flat

	assert((tileh != SLOPE_EW) && (tileh != SLOPE_NS)); // No suitable sprites for current flooding behaviour

	DrawGroundSprite(SPR_SHORE_BASE + tileh_to_shoresprite[tileh], PAL_NONE);
}

void DrawWaterClassGround(const TileInfo *ti)
{
	switch (GetWaterClass(ti->tile)) {
		case WATER_CLASS_SEA:   DrawSeaWater(ti->tile); break;
		case WATER_CLASS_CANAL: DrawCanalWater(ti->tile); break;
		case WATER_CLASS_RIVER: DrawRiverWater(ti); break;
		default: NOT_REACHED();
	}
}

static void DrawTile_Water(TileInfo *ti)
{
	switch (GetWaterTileType(ti->tile)) {
		case WATER_TILE_CLEAR:
			DrawWaterClassGround(ti);
			DrawBridgeMiddle(ti);
			break;

		case WATER_TILE_COAST: {
			DrawShoreTile(ti->tileh);
			DrawBridgeMiddle(ti);
		} break;

		case WATER_TILE_LOCK: {
			const WaterDrawTileStruct *t = _shiplift_display_seq[GetSection(ti->tile)];
			DrawWaterStuff(ti, t, 0, ti->z > t[3].delta_y ? 24 : 0, true);
		} break;

		case WATER_TILE_DEPOT:
			DrawWaterClassGround(ti);
			DrawWaterStuff(ti, _shipdepot_display_seq[GetSection(ti->tile)], COMPANY_SPRITE_COLOUR(GetTileOwner(ti->tile)), 0, false);
			break;
	}
}

void DrawShipDepotSprite(int x, int y, int image)
{
	const WaterDrawTileStruct *wdts = _shipdepot_display_seq[image];

	DrawSprite(wdts++->image, PAL_NONE, x, y);

	for (; wdts->delta_x != 0x80; wdts++) {
		Point pt = RemapCoords(wdts->delta_x, wdts->delta_y, wdts->delta_z);
		DrawSprite(wdts->image, COMPANY_SPRITE_COLOUR(_local_company), x + pt.x, y + pt.y);
	}
}


static uint GetSlopeZ_Water(TileIndex tile, uint x, uint y)
{
	uint z;
	Slope tileh = GetTileSlope(tile, &z);

	return z + GetPartialZ(x & 0xF, y & 0xF, tileh);
}

static Foundation GetFoundation_Water(TileIndex tile, Slope tileh)
{
	return FOUNDATION_NONE;
}

static void GetTileDesc_Water(TileIndex tile, TileDesc *td)
{
	switch (GetWaterTileType(tile)) {
		case WATER_TILE_CLEAR:
			switch (GetWaterClass(tile)) {
				case WATER_CLASS_SEA:   td->str = STR_LAI_WATER_DESCRIPTION_WATER; break;
				case WATER_CLASS_CANAL: td->str = STR_LAI_WATER_DESCRIPTION_CANAL; break;
				case WATER_CLASS_RIVER: td->str = STR_LAI_WATER_DESCRIPTION_RIVER; break;
				default: NOT_REACHED(); break;
			}
			break;
		case WATER_TILE_COAST: td->str = STR_LAI_WATER_DESCRIPTION_COAST_OR_RIVERBANK; break;
		case WATER_TILE_LOCK : td->str = STR_LAI_WATER_DESCRIPTION_LOCK;               break;
		case WATER_TILE_DEPOT: td->str = STR_LAI_WATER_DESCRIPTION_SHIP_DEPOT;         break;
		default: NOT_REACHED(); break;
	}

	td->owner[0] = GetTileOwner(tile);
}

static void FloodVehicle(Vehicle *v);

/**
 * Flood a vehicle if we are allowed to flood it, i.e. when it is on the ground.
 * @param v    The vehicle to test for flooding.
 * @param data The z of level to flood.
 * @return NULL as we always want to remove everything.
 */
static Vehicle *FloodVehicleProc(Vehicle *v, void *data)
{
	byte z = *(byte*)data;

	if (v->type == VEH_DISASTER || (v->type == VEH_AIRCRAFT && v->subtype == AIR_SHADOW)) return NULL;
	if (v->z_pos > z || (v->vehstatus & VS_CRASHED) != 0) return NULL;

	FloodVehicle(v);
	return NULL;
}

/**
 * Finds a vehicle to flood.
 * It does not find vehicles that are already crashed on bridges, i.e. flooded.
 * @param tile the tile where to find a vehicle to flood
 */
static void FloodVehicles(TileIndex tile)
{
	byte z = 0;

	if (IsAirportTile(tile)) {
		const Station *st = Station::GetByTile(tile);
		z = 1 + st->airport.GetFTA()->delta_z;
		TILE_AREA_LOOP(tile, st->airport) {
			if (st->TileBelongsToAirport(tile)) FindVehicleOnPos(tile, &z, &FloodVehicleProc);
		}

		/* No vehicle could be flooded on this airport anymore */
		return;
	}

	/* if non-uniform stations are disabled, flood some train in this train station (if there is any) */
	if (!_settings_game.station.nonuniform_stations && IsTileType(tile, MP_STATION) && GetStationType(tile) == STATION_RAIL) {
		const Station *st = Station::GetByTile(tile);

		TILE_AREA_LOOP(t, st->train_station) {
			if (st->TileBelongsToRailStation(t)) {
				FindVehicleOnPos(tile, &z, &FloodVehicleProc);
			}
		}

		return;
	}

	if (!IsBridgeTile(tile)) {
		FindVehicleOnPos(tile, &z, &FloodVehicleProc);
		return;
	}

	TileIndex end = GetOtherBridgeEnd(tile);
	z = GetBridgeHeight(tile);

	FindVehicleOnPos(tile, &z, &FloodVehicleProc);
	FindVehicleOnPos(end, &z, &FloodVehicleProc);
}

static void FloodVehicle(Vehicle *v)
{
	if ((v->vehstatus & VS_CRASHED) != 0) return;
	if (v->type != VEH_TRAIN && v->type != VEH_ROAD && v->type != VEH_AIRCRAFT) return;

	if (v->type == VEH_AIRCRAFT) {
		/* Crashing aircraft are always at z_pos == 1, never on z_pos == 0,
		 * because that's always the shadow. Except for the heliport, because
		 * that station has a big z_offset for the aircraft. */
		if (!IsAirportTile(v->tile) || GetTileMaxZ(v->tile) != 0) return;
		const Station *st = Station::GetByTile(v->tile);
		const AirportFTAClass *airport = st->airport.GetFTA();

		if (v->z_pos != airport->delta_z + 1) return;
	} else {
		v = v->First();
	}

	uint pass = v->Crash(true);

	AI::NewEvent(v->owner, new AIEventVehicleCrashed(v->index, v->tile, AIEventVehicleCrashed::CRASH_FLOODED));
	SetDParam(0, pass);
	AddVehicleNewsItem(STR_NEWS_DISASTER_FLOOD_VEHICLE,
		NS_ACCIDENT,
		v->index);
	CreateEffectVehicleRel(v, 4, 4, 8, EV_EXPLOSION_LARGE);
	SndPlayVehicleFx(SND_12_EXPLOSION, v);
}

/**
 * Returns the behaviour of a tile during flooding.
 *
 * @return Behaviour of the tile
 */
static FloodingBehaviour GetFloodingBehaviour(TileIndex tile)
{
	/* FLOOD_ACTIVE:  'single-corner-raised'-coast, sea, sea-shipdepots, sea-buoys, sea-docks (water part), rail with flooded halftile, sea-water-industries, sea-oilrigs
	 * FLOOD_DRYUP:   coast with more than one corner raised, coast with rail-track, coast with trees
	 * FLOOD_PASSIVE: (not used)
	 * FLOOD_NONE:    canals, rivers, everything else
	 */
	switch (GetTileType(tile)) {
		case MP_WATER:
			if (IsCoast(tile)) {
				Slope tileh = GetTileSlope(tile, NULL);
				return (IsSlopeWithOneCornerRaised(tileh) ? FLOOD_ACTIVE : FLOOD_DRYUP);
			} else {
				return (GetWaterClass(tile) == WATER_CLASS_SEA) ? FLOOD_ACTIVE : FLOOD_NONE;
			}

		case MP_RAILWAY:
			if (GetRailGroundType(tile) == RAIL_GROUND_WATER) {
				return (IsSlopeWithOneCornerRaised(GetTileSlope(tile, NULL)) ? FLOOD_ACTIVE : FLOOD_DRYUP);
			}
			return FLOOD_NONE;

		case MP_TREES:
			return (GetTreeGround(tile) == TREE_GROUND_SHORE ? FLOOD_DRYUP : FLOOD_NONE);

		case MP_STATION:
			if (IsBuoy(tile) || (IsDock(tile) && GetTileSlope(tile, NULL) == SLOPE_FLAT) || IsOilRig(tile)) {
				return (GetWaterClass(tile) == WATER_CLASS_SEA ? FLOOD_ACTIVE : FLOOD_NONE);
			}
			return FLOOD_NONE;

		case MP_INDUSTRY:
			return ((IsIndustryTileOnWater(tile) && GetWaterClass(tile) == WATER_CLASS_SEA) ? FLOOD_ACTIVE : FLOOD_NONE);

		default:
			return FLOOD_NONE;
	}
}

/**
 * Floods a tile.
 */
void DoFloodTile(TileIndex target)
{
	assert(!IsTileType(target, MP_WATER));

	bool flooded = false; // Will be set to true if something is changed.

	_current_company = OWNER_WATER;

	Slope tileh = GetTileSlope(target, NULL);
	if (tileh != SLOPE_FLAT) {
		/* make coast.. */
		switch (GetTileType(target)) {
			case MP_RAILWAY: {
				if (!IsPlainRail(target)) break;
				FloodVehicles(target);
				flooded = FloodHalftile(target);
				break;
			}

			case MP_TREES:
				if (!IsSlopeWithOneCornerRaised(tileh)) {
					SetTreeGroundDensity(target, TREE_GROUND_SHORE, 3);
					MarkTileDirtyByTile(target);
					flooded = true;
					break;
				}
			/* FALL THROUGH */
			case MP_CLEAR:
				if (DoCommand(target, 0, 0, DC_EXEC, CMD_LANDSCAPE_CLEAR).Succeeded()) {
					MakeShore(target);
					MarkTileDirtyByTile(target);
					flooded = true;
				}
				break;

			default:
				break;
		}
	} else {
		/* Flood vehicles */
		FloodVehicles(target);

		/* flood flat tile */
		if (DoCommand(target, 0, 0, DC_EXEC, CMD_LANDSCAPE_CLEAR).Succeeded()) {
			MakeSea(target);
			MarkTileDirtyByTile(target);
			flooded = true;
		}
	}

	if (flooded) {
		/* Mark surrounding canal tiles dirty too to avoid glitches */
		MarkCanalsAndRiversAroundDirty(target);

		/* update signals if needed */
		UpdateSignalsInBuffer();
	}

	_current_company = OWNER_NONE;
}

/**
 * Drys a tile up.
 */
static void DoDryUp(TileIndex tile)
{
	_current_company = OWNER_WATER;

	switch (GetTileType(tile)) {
		case MP_RAILWAY:
			assert(IsPlainRail(tile));
			assert(GetRailGroundType(tile) == RAIL_GROUND_WATER);

			RailGroundType new_ground;
			switch (GetTrackBits(tile)) {
				case TRACK_BIT_UPPER: new_ground = RAIL_GROUND_FENCE_HORIZ1; break;
				case TRACK_BIT_LOWER: new_ground = RAIL_GROUND_FENCE_HORIZ2; break;
				case TRACK_BIT_LEFT:  new_ground = RAIL_GROUND_FENCE_VERT1;  break;
				case TRACK_BIT_RIGHT: new_ground = RAIL_GROUND_FENCE_VERT2;  break;
				default: NOT_REACHED();
			}
			SetRailGroundType(tile, new_ground);
			MarkTileDirtyByTile(tile);
			break;

		case MP_TREES:
			SetTreeGroundDensity(tile, TREE_GROUND_GRASS, 3);
			MarkTileDirtyByTile(tile);
			break;

		case MP_WATER:
			assert(IsCoast(tile));

			if (DoCommand(tile, 0, 0, DC_EXEC, CMD_LANDSCAPE_CLEAR).Succeeded()) {
				MakeClear(tile, CLEAR_GRASS, 3);
				MarkTileDirtyByTile(tile);
			}
			break;

		default: NOT_REACHED();
	}

	_current_company = OWNER_NONE;
}

/**
 * Let a water tile floods its diagonal adjoining tiles
 * called from tunnelbridge_cmd, and by TileLoop_Industry() and TileLoop_Track()
 *
 * @param tile the water/shore tile that floods
 */
void TileLoop_Water(TileIndex tile)
{
	switch (GetFloodingBehaviour(tile)) {
		case FLOOD_ACTIVE:
			for (Direction dir = DIR_BEGIN; dir < DIR_END; dir++) {
				TileIndex dest = AddTileIndexDiffCWrap(tile, TileIndexDiffCByDir(dir));
				if (dest == INVALID_TILE) continue;
				/* do not try to flood water tiles - increases performance a lot */
				if (IsTileType(dest, MP_WATER)) continue;

				uint z_dest;
				Slope slope_dest = GetFoundationSlope(dest, &z_dest) & ~SLOPE_HALFTILE_MASK & ~SLOPE_STEEP;
				if (z_dest > 0) continue;

				if (!HasBit(_flood_from_dirs[slope_dest], ReverseDir(dir))) continue;

				DoFloodTile(dest);
			}
			break;

		case FLOOD_DRYUP: {
			Slope slope_here = GetFoundationSlope(tile, NULL) & ~SLOPE_HALFTILE_MASK & ~SLOPE_STEEP;
			uint check_dirs = _flood_from_dirs[slope_here];
			uint dir;
			FOR_EACH_SET_BIT(dir, check_dirs) {
				TileIndex dest = AddTileIndexDiffCWrap(tile, TileIndexDiffCByDir((Direction)dir));
				if (dest == INVALID_TILE) continue;

				FloodingBehaviour dest_behaviour = GetFloodingBehaviour(dest);
				if ((dest_behaviour == FLOOD_ACTIVE) || (dest_behaviour == FLOOD_PASSIVE)) return;
			}
			DoDryUp(tile);
			break;
		}

		default: return;
	}
}

void ConvertGroundTilesIntoWaterTiles()
{
	uint z;

	for (TileIndex tile = 0; tile < MapSize(); ++tile) {
		Slope slope = GetTileSlope(tile, &z);
		if (IsTileType(tile, MP_CLEAR) && z == 0) {
			/* Make both water for tiles at level 0
			 * and make shore, as that looks much better
			 * during the generation. */
			switch (slope) {
				case SLOPE_FLAT:
					MakeSea(tile);
					break;

				case SLOPE_N:
				case SLOPE_E:
				case SLOPE_S:
				case SLOPE_W:
					MakeShore(tile);
					break;

				default:
					uint check_dirs = _flood_from_dirs[slope & ~SLOPE_STEEP];
					uint dir;
					FOR_EACH_SET_BIT(dir, check_dirs) {
						TileIndex dest = TILE_ADD(tile, TileOffsByDir((Direction)dir));
						Slope slope_dest = GetTileSlope(dest, NULL) & ~SLOPE_STEEP;
						if (slope_dest == SLOPE_FLAT || IsSlopeWithOneCornerRaised(slope_dest)) {
							MakeShore(tile);
							break;
						}
					}
					break;
			}
		}
	}
}

static TrackStatus GetTileTrackStatus_Water(TileIndex tile, TransportType mode, uint sub_mode, DiagDirection side)
{
	static const byte coast_tracks[] = {0, 32, 4, 0, 16, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0};

	TrackBits ts;

	if (mode != TRANSPORT_WATER) return 0;

	switch (GetWaterTileType(tile)) {
		case WATER_TILE_CLEAR: ts = (GetTileSlope(tile, NULL) == SLOPE_FLAT) ? TRACK_BIT_ALL : TRACK_BIT_NONE; break;
		case WATER_TILE_COAST: ts = (TrackBits)coast_tracks[GetTileSlope(tile, NULL) & 0xF]; break;
		case WATER_TILE_LOCK:  ts = DiagDirToDiagTrackBits(GetLockDirection(tile)); break;
		case WATER_TILE_DEPOT: ts = AxisToTrackBits(GetShipDepotAxis(tile)); break;
		default: return 0;
	}
	if (TileX(tile) == 0) {
		/* NE border: remove tracks that connects NE tile edge */
		ts &= ~(TRACK_BIT_X | TRACK_BIT_UPPER | TRACK_BIT_RIGHT);
	}
	if (TileY(tile) == 0) {
		/* NW border: remove tracks that connects NW tile edge */
		ts &= ~(TRACK_BIT_Y | TRACK_BIT_LEFT | TRACK_BIT_UPPER);
	}
	return CombineTrackStatus(TrackBitsToTrackdirBits(ts), TRACKDIR_BIT_NONE);
}

static bool ClickTile_Water(TileIndex tile)
{
	if (GetWaterTileType(tile) == WATER_TILE_DEPOT) {
		TileIndex tile2 = GetOtherShipDepotTile(tile);

		ShowDepotWindow(tile < tile2 ? tile : tile2, VEH_SHIP);
		return true;
	}
	return false;
}

static void ChangeTileOwner_Water(TileIndex tile, Owner old_owner, Owner new_owner)
{
	if (!IsTileOwner(tile, old_owner)) return;

	if (new_owner != INVALID_OWNER) {
		SetTileOwner(tile, new_owner);
		return;
	}

	/* Remove depot */
	if (IsShipDepot(tile)) DoCommand(tile, 0, 0, DC_EXEC | DC_BANKRUPT, CMD_LANDSCAPE_CLEAR);

	/* Set owner of canals and locks ... and also canal under dock there was before.
	 * Check if the new owner after removing depot isn't OWNER_WATER. */
	if (IsTileOwner(tile, old_owner)) SetTileOwner(tile, OWNER_NONE);
}

static VehicleEnterTileStatus VehicleEnter_Water(Vehicle *v, TileIndex tile, int x, int y)
{
	return VETSB_CONTINUE;
}

static CommandCost TerraformTile_Water(TileIndex tile, DoCommandFlag flags, uint z_new, Slope tileh_new)
{
	/* Canals can't be terraformed */
	if (IsWaterTile(tile) && IsCanal(tile)) return_cmd_error(STR_ERROR_MUST_DEMOLISH_CANAL_FIRST);

	return DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
}


extern const TileTypeProcs _tile_type_water_procs = {
	DrawTile_Water,           // draw_tile_proc
	GetSlopeZ_Water,          // get_slope_z_proc
	ClearTile_Water,          // clear_tile_proc
	NULL,                     // add_accepted_cargo_proc
	GetTileDesc_Water,        // get_tile_desc_proc
	GetTileTrackStatus_Water, // get_tile_track_status_proc
	ClickTile_Water,          // click_tile_proc
	NULL,                     // animate_tile_proc
	TileLoop_Water,           // tile_loop_clear
	ChangeTileOwner_Water,    // change_tile_owner_clear
	NULL,                     // add_produced_cargo_proc
	VehicleEnter_Water,       // vehicle_enter_tile_proc
	GetFoundation_Water,      // get_foundation_proc
	TerraformTile_Water,      // terraform_tile_proc
};
