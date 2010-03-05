/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file terraform_cmd.cpp Commands related to terraforming. */

#include "stdafx.h"
#include "openttd.h"
#include "command_func.h"
#include "tunnel_map.h"
#include "bridge_map.h"
#include "variables.h"
#include "functions.h"
#include "economy_func.h"

#include "table/strings.h"

/*
 * In one terraforming command all four corners of a initial tile can be raised/lowered (though this is not available to the player).
 * The maximal amount of height modifications is archieved when raising a complete flat land from sea level to MAX_TILE_HEIGHT or vice versa.
 * This affects all corners with a manhatten distance smaller than MAX_TILE_HEIGHT to one of the initial 4 corners.
 * Their maximal amount is computed to 4 * \sum_{i=1}^{h_max} i  =  2 * h_max * (h_max + 1).
 */
static const int TERRAFORMER_MODHEIGHT_SIZE = 2 * MAX_TILE_HEIGHT * (MAX_TILE_HEIGHT + 1);

/*
 * The maximal amount of affected tiles (i.e. the tiles that incident with one of the corners above, is computed similiar to
 * 1 + 4 * \sum_{i=1}^{h_max} (i+1)  =  1 + 2 * h_max + (h_max + 3).
 */
static const int TERRAFORMER_TILE_TABLE_SIZE = 1 + 2 * MAX_TILE_HEIGHT * (MAX_TILE_HEIGHT + 3);

struct TerraformerHeightMod {
	TileIndex tile;   ///< Referenced tile.
	byte height;      ///< New TileHeight (height of north corner) of the tile.
};

struct TerraformerState {
	int modheight_count;  ///< amount of entries in "modheight".
	int tile_table_count; ///< amount of entries in "tile_table".

	/**
	 * Dirty tiles, i.e.\ at least one corner changed.
	 *
	 * This array contains the tiles which are or will be marked as dirty.
	 *
	 * @ingroup dirty
	 */
	TileIndex tile_table[TERRAFORMER_TILE_TABLE_SIZE];
	TerraformerHeightMod modheight[TERRAFORMER_MODHEIGHT_SIZE];  ///< Height modifications.
};

TileIndex _terraform_err_tile; ///< first tile we couldn't terraform

/**
 * Gets the TileHeight (height of north corner) of a tile as of current terraforming progress.
 *
 * @param ts TerraformerState.
 * @param tile Tile.
 * @return TileHeight.
 */
static int TerraformGetHeightOfTile(const TerraformerState *ts, TileIndex tile)
{
	const TerraformerHeightMod *mod = ts->modheight;

	for (int count = ts->modheight_count; count != 0; count--, mod++) {
		if (mod->tile == tile) return mod->height;
	}

	/* TileHeight unchanged so far, read value from map. */
	return TileHeight(tile);
}

/**
 * Stores the TileHeight (height of north corner) of a tile in a TerraformerState.
 *
 * @param ts TerraformerState.
 * @param tile Tile.
 * @param height New TileHeight.
 */
static void TerraformSetHeightOfTile(TerraformerState *ts, TileIndex tile, int height)
{
	/* Find tile in the "modheight" table.
	 * Note: In a normal user-terraform command the tile will not be found in the "modheight" table.
	 *       But during house- or industry-construction multiple corners can be terraformed at once. */
	TerraformerHeightMod *mod = ts->modheight;
	int count = ts->modheight_count;

	while ((count > 0) && (mod->tile != tile)) {
		mod++;
		count--;
	}

	/* New entry? */
	if (count == 0) {
		assert(ts->modheight_count < TERRAFORMER_MODHEIGHT_SIZE);
		ts->modheight_count++;
	}

	/* Finally store the new value */
	mod->tile = tile;
	mod->height = (byte)height;
}

/**
 * Adds a tile to the "tile_table" in a TerraformerState.
 *
 * @param ts TerraformerState.
 * @param tile Tile.
 * @ingroup dirty
 */
static void TerraformAddDirtyTile(TerraformerState *ts, TileIndex tile)
{
	int count = ts->tile_table_count;

	for (TileIndex *t = ts->tile_table; count != 0; count--, t++) {
		if (*t == tile) return;
	}

	assert(ts->tile_table_count < TERRAFORMER_TILE_TABLE_SIZE);

	ts->tile_table[ts->tile_table_count++] = tile;
}

/**
 * Adds all tiles that incident with the north corner of a specific tile to the "tile_table" in a TerraformerState.
 *
 * @param ts TerraformerState.
 * @param tile Tile.
 * @ingroup dirty
 */
static void TerraformAddDirtyTileAround(TerraformerState *ts, TileIndex tile)
{
	/* Make sure all tiles passed to TerraformAddDirtyTile are within [0, MapSize()] */
	if (TileY(tile) >= 1) TerraformAddDirtyTile(ts, tile + TileDiffXY( 0, -1));
	if (TileY(tile) >= 1 && TileX(tile) >= 1) TerraformAddDirtyTile(ts, tile + TileDiffXY(-1, -1));
	if (TileX(tile) >= 1) TerraformAddDirtyTile(ts, tile + TileDiffXY(-1,  0));
	TerraformAddDirtyTile(ts, tile);
}

/**
 * Terraform the north corner of a tile to a specific height.
 *
 * @param ts TerraformerState.
 * @param tile Tile.
 * @param height Aimed height.
 * @return Error code or cost.
 */
static CommandCost TerraformTileHeight(TerraformerState *ts, TileIndex tile, int height)
{
	assert(tile < MapSize());

	/* Check range of destination height */
	if (height < 0) return_cmd_error(STR_ERROR_ALREADY_AT_SEA_LEVEL);
	if (height > MAX_TILE_HEIGHT) return_cmd_error(STR_ERROR_TOO_HIGH);

	/*
	 * Check if the terraforming has any effect.
	 * This can only be true, if multiple corners of the start-tile are terraformed (i.e. the terraforming is done by towns/industries etc.).
	 * In this case the terraforming should fail. (Don't know why.)
	 */
	if (height == TerraformGetHeightOfTile(ts, tile)) return CMD_ERROR;

	/* Check "too close to edge of map". Only possible when freeform-edges is off. */
	uint x = TileX(tile);
	uint y = TileY(tile);
	if (!_settings_game.construction.freeform_edges && ((x <= 1) || (y <= 1) || (x >= MapMaxX() - 1) || (y >= MapMaxY() - 1))) {
		/*
		 * Determine a sensible error tile
		 */
		if (x == 1) x = 0;
		if (y == 1) y = 0;
		_terraform_err_tile = TileXY(x, y);
		return_cmd_error(STR_ERROR_TOO_CLOSE_TO_EDGE_OF_MAP);
	}

	/* Mark incident tiles, that are involved in the terraforming */
	TerraformAddDirtyTileAround(ts, tile);

	/* Store the height modification */
	TerraformSetHeightOfTile(ts, tile, height);

	CommandCost total_cost(EXPENSES_CONSTRUCTION);

	/* Increment cost */
	total_cost.AddCost(_price[PR_TERRAFORM]);

	/* Recurse to neighboured corners if height difference is larger than 1 */
	{
		const TileIndexDiffC *ttm;

		TileIndex orig_tile = tile;
		static const TileIndexDiffC _terraform_tilepos[] = {
			{ 1,  0}, // move to tile in SE
			{-2,  0}, // undo last move, and move to tile in NW
			{ 1,  1}, // undo last move, and move to tile in SW
			{ 0, -2}  // undo last move, and move to tile in NE
		};

		for (ttm = _terraform_tilepos; ttm != endof(_terraform_tilepos); ttm++) {
			tile += ToTileIndexDiff(*ttm);

			if (tile >= MapSize()) continue;
			/* Make sure we don't wrap around the map */
			if (Delta(TileX(orig_tile), TileX(tile)) == MapSizeX() - 1) continue;
			if (Delta(TileY(orig_tile), TileY(tile)) == MapSizeY() - 1) continue;

			/* Get TileHeight of neighboured tile as of current terraform progress */
			int r = TerraformGetHeightOfTile(ts, tile);
			int height_diff = height - r;

			/* Is the height difference to the neighboured corner greater than 1? */
			if (abs(height_diff) > 1) {
				/* Terraform the neighboured corner. The resulting height difference should be 1. */
				height_diff += (height_diff < 0 ? 1 : -1);
				CommandCost cost = TerraformTileHeight(ts, tile, r + height_diff);
				if (cost.Failed()) return cost;
				total_cost.AddCost(cost);
			}
		}
	}

	return total_cost;
}

/** Terraform land
 * @param tile tile to terraform
 * @param flags for this command type
 * @param p1 corners to terraform (SLOPE_xxx)
 * @param p2 direction; eg up (non-zero) or down (zero)
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdTerraformLand(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	_terraform_err_tile = INVALID_TILE;

	CommandCost total_cost(EXPENSES_CONSTRUCTION);
	int direction = (p2 != 0 ? 1 : -1);
	TerraformerState ts;

	ts.modheight_count = ts.tile_table_count = 0;

	/* Compute the costs and the terraforming result in a model of the landscape */
	if ((p1 & SLOPE_W) != 0 && tile + TileDiffXY(1, 0) < MapSize()) {
		TileIndex t = tile + TileDiffXY(1, 0);
		CommandCost cost = TerraformTileHeight(&ts, t, TileHeight(t) + direction);
		if (cost.Failed()) return cost;
		total_cost.AddCost(cost);
	}

	if ((p1 & SLOPE_S) != 0 && tile + TileDiffXY(1, 1) < MapSize()) {
		TileIndex t = tile + TileDiffXY(1, 1);
		CommandCost cost = TerraformTileHeight(&ts, t, TileHeight(t) + direction);
		if (cost.Failed()) return cost;
		total_cost.AddCost(cost);
	}

	if ((p1 & SLOPE_E) != 0 && tile + TileDiffXY(0, 1) < MapSize()) {
		TileIndex t = tile + TileDiffXY(0, 1);
		CommandCost cost = TerraformTileHeight(&ts, t, TileHeight(t) + direction);
		if (cost.Failed()) return cost;
		total_cost.AddCost(cost);
	}

	if ((p1 & SLOPE_N) != 0) {
		TileIndex t = tile + TileDiffXY(0, 0);
		CommandCost cost = TerraformTileHeight(&ts, t, TileHeight(t) + direction);
		if (cost.Failed()) return cost;
		total_cost.AddCost(cost);
	}

	/* Check if the terraforming is valid wrt. tunnels, bridges and objects on the surface */
	{
		TileIndex *ti = ts.tile_table;

		for (int count = ts.tile_table_count; count != 0; count--, ti++) {
			TileIndex tile = *ti;

			assert(tile < MapSize());
			/* MP_VOID tiles can be terraformed but as tunnels and bridges
			 * cannot go under / over these tiles they don't need checking. */
			if (IsTileType(tile, MP_VOID)) continue;

			/* Find new heights of tile corners */
			uint z_N = TerraformGetHeightOfTile(&ts, tile + TileDiffXY(0, 0));
			uint z_W = TerraformGetHeightOfTile(&ts, tile + TileDiffXY(1, 0));
			uint z_S = TerraformGetHeightOfTile(&ts, tile + TileDiffXY(1, 1));
			uint z_E = TerraformGetHeightOfTile(&ts, tile + TileDiffXY(0, 1));

			/* Find min and max height of tile */
			uint z_min = min(min(z_N, z_W), min(z_S, z_E));
			uint z_max = max(max(z_N, z_W), max(z_S, z_E));

			/* Compute tile slope */
			Slope tileh = (z_max > z_min + 1 ? SLOPE_STEEP : SLOPE_FLAT);
			if (z_W > z_min) tileh |= SLOPE_W;
			if (z_S > z_min) tileh |= SLOPE_S;
			if (z_E > z_min) tileh |= SLOPE_E;
			if (z_N > z_min) tileh |= SLOPE_N;

			/* Check if bridge would take damage */
			if (direction == 1 && MayHaveBridgeAbove(tile) && IsBridgeAbove(tile) &&
					GetBridgeHeight(GetSouthernBridgeEnd(tile)) <= z_max * TILE_HEIGHT) {
				_terraform_err_tile = tile; // highlight the tile under the bridge
				return_cmd_error(STR_ERROR_MUST_DEMOLISH_BRIDGE_FIRST);
			}
			/* Check if tunnel would take damage */
			if (direction == -1 && IsTunnelInWay(tile, z_min * TILE_HEIGHT)) {
				_terraform_err_tile = tile; // highlight the tile above the tunnel
				return_cmd_error(STR_ERROR_EXCAVATION_WOULD_DAMAGE);
			}
			/* Check tiletype-specific things, and add extra-cost */
			const bool curr_gen = _generating_world;
			if (_game_mode == GM_EDITOR) _generating_world = true; // used to create green terraformed land
			CommandCost cost = _tile_type_procs[GetTileType(tile)]->terraform_tile_proc(tile, flags | DC_AUTO, z_min * TILE_HEIGHT, tileh);
			_generating_world = curr_gen;
			if (cost.Failed()) {
				_terraform_err_tile = tile;
				return cost;
			}
			total_cost.AddCost(cost);
		}
	}

	if (flags & DC_EXEC) {
		/* change the height */
		{
			int count;
			TerraformerHeightMod *mod;

			mod = ts.modheight;
			for (count = ts.modheight_count; count != 0; count--, mod++) {
				TileIndex til = mod->tile;

				SetTileHeight(til, mod->height);
			}
		}

		/* finally mark the dirty tiles dirty */
		{
			int count;
			TileIndex *ti = ts.tile_table;
			for (count = ts.tile_table_count; count != 0; count--, ti++) {
				MarkTileDirtyByTile(*ti);
			}
		}
	}
	return total_cost;
}


/** Levels a selected (rectangle) area of land
 * @param tile end tile of area-drag
 * @param flags for this command type
 * @param p1 start tile of area drag
 * @param p2 height difference; eg raise (+1), lower (-1) or level (0)
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdLevelLand(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	if (p1 >= MapSize()) return CMD_ERROR;

	_terraform_err_tile = INVALID_TILE;

	/* remember level height */
	uint oldh = TileHeight(p1);

	/* compute new height */
	uint h = oldh + p2;

	/* Check range of destination height */
	if (h > MAX_TILE_HEIGHT) return_cmd_error((oldh == 0) ? STR_ERROR_ALREADY_AT_SEA_LEVEL : STR_ERROR_TOO_HIGH);

	Money money = GetAvailableMoneyForCommand();
	CommandCost cost(EXPENSES_CONSTRUCTION);

	TileArea ta(tile, p1);
	TILE_AREA_LOOP(tile, ta) {
		uint curh = TileHeight(tile);
		while (curh != h) {
			CommandCost ret = DoCommand(tile, SLOPE_N, (curh > h) ? 0 : 1, flags & ~DC_EXEC, CMD_TERRAFORM_LAND);
			if (ret.Failed()) return (cost.GetCost() == 0) ? ret : cost;

			if (flags & DC_EXEC) {
				money -= ret.GetCost();
				if (money < 0) {
					_additional_cash_required = ret.GetCost();
					return cost;
				}
				DoCommand(tile, SLOPE_N, (curh > h) ? 0 : 1, flags, CMD_TERRAFORM_LAND);
			}

			cost.AddCost(ret);
			curh += (curh > h) ? -1 : 1;
		}
	}

	if (cost.GetCost() == 0) {
		if (p2 != 0) return CMD_ERROR;
		cost.MakeError(STR_ERROR_ALREADY_LEVELLED);
		cost.SetGlobalErrorMessage();
	}
	return cost;
}
