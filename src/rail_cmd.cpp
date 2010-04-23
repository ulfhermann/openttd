/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file rail_cmd.cpp Handling of rail tiles. */

#include "stdafx.h"
#include "cmd_helper.h"
#include "landscape.h"
#include "viewport_func.h"
#include "command_func.h"
#include "engine_base.h"
#include "depot_base.h"
#include "pathfinder/yapf/yapf_cache.h"
#include "newgrf_engine.h"
#include "landscape_type.h"
#include "newgrf_railtype.h"
#include "newgrf_commons.h"
#include "train.h"
#include "variables.h"
#include "autoslope.h"
#include "water.h"
#include "tunnelbridge_map.h"
#include "window_func.h"
#include "vehicle_func.h"
#include "sound_func.h"
#include "tunnelbridge.h"
#include "functions.h"
#include "elrail_func.h"
#include "town.h"
#include "pbs.h"
#include "company_base.h"

#include "table/strings.h"
#include "table/sprites.h"
#include "table/railtypes.h"
#include "table/track_land.h"

RailtypeInfo _railtypes[RAILTYPE_END];

assert_compile(sizeof(_original_railtypes) <= sizeof(_railtypes));

/**
 * Initialize rail type information.
 */
void ResetRailTypes()
{
	memset(_railtypes, 0, sizeof(_railtypes));
	memcpy(_railtypes, _original_railtypes, sizeof(_original_railtypes));
}

void ResolveRailTypeGUISprites(RailtypeInfo *rti)
{
	SpriteID cursors_base = GetCustomRailSprite(rti, INVALID_TILE, RTSG_CURSORS);
	if (cursors_base != 0) {
		rti->gui_sprites.build_ns_rail = cursors_base +  0;
		rti->gui_sprites.build_x_rail  = cursors_base +  1;
		rti->gui_sprites.build_ew_rail = cursors_base +  2;
		rti->gui_sprites.build_y_rail  = cursors_base +  3;
		rti->gui_sprites.auto_rail     = cursors_base +  4;
		rti->gui_sprites.build_depot   = cursors_base +  5;
		rti->gui_sprites.build_tunnel  = cursors_base +  6;
		rti->gui_sprites.convert_rail  = cursors_base +  7;
		rti->cursor.rail_ns   = cursors_base +  8;
		rti->cursor.rail_swne = cursors_base +  9;
		rti->cursor.rail_ew   = cursors_base + 10;
		rti->cursor.rail_nwse = cursors_base + 11;
		rti->cursor.autorail  = cursors_base + 12;
		rti->cursor.depot     = cursors_base + 13;
		rti->cursor.tunnel    = cursors_base + 14;
		rti->cursor.convert   = cursors_base + 15;
	}
}

void InitRailTypes()
{
	for (RailType rt = RAILTYPE_BEGIN; rt != RAILTYPE_END; rt++) {
		RailtypeInfo *rti = &_railtypes[rt];
		ResolveRailTypeGUISprites(rti);
	}
}

RailType AllocateRailType(RailTypeLabel label)
{
	for (RailType rt = RAILTYPE_BEGIN; rt != RAILTYPE_END; rt++) {
		RailtypeInfo *rti = &_railtypes[rt];

		if (rti->label == 0) {
			/* Set up new rail type */
			memcpy(rti, &_railtypes[RAILTYPE_RAIL], sizeof(*rti));
			rti->label = label;

			/* Make us compatible with ourself. */
			rti->powered_railtypes    = (RailTypes)(1 << rt);
			rti->compatible_railtypes = (RailTypes)(1 << rt);
			return rt;
		}
	}

	return INVALID_RAILTYPE;
}

static const byte _track_sloped_sprites[14] = {
	14, 15, 22, 13,
	 0, 21, 17, 12,
	23,  0, 18, 20,
	19, 16
};


/*         4
 *     ---------
 *    |\       /|
 *    | \    1/ |
 *    |  \   /  |
 *    |   \ /   |
 *  16|    \    |32
 *    |   / \2  |
 *    |  /   \  |
 *    | /     \ |
 *    |/       \|
 *     ---------
 *         8
 */



/* MAP2 byte:    abcd???? => Signal On? Same coding as map3lo
 * MAP3LO byte:  abcd???? => Signal Exists?
 *               a and b are for diagonals, upper and left,
 *               one for each direction. (ie a == NE->SW, b ==
 *               SW->NE, or v.v., I don't know. b and c are
 *               similar for lower and right.
 * MAP2 byte:    ????abcd => Type of ground.
 * MAP3LO byte:  ????abcd => Type of rail.
 * MAP5:         00abcdef => rail
 *               01abcdef => rail w/ signals
 *               10uuuuuu => unused
 *               11uuuudd => rail depot
 */

/**
 * Tests if a vehicle interacts with the specified track.
 * All track bits interact except parallel #TRACK_BIT_HORZ or #TRACK_BIT_VERT.
 *
 * @param tile The tile.
 * @param track The track.
 * @return Succeeded command (no train found), or a failed command (a train was found).
 */
static CommandCost EnsureNoTrainOnTrack(TileIndex tile, Track track)
{
	TrackBits rail_bits = TrackToTrackBits(track);
	return EnsureNoTrainOnTrackBits(tile, rail_bits);
}

/** Check that the new track bits may be built.
 * @param tile %Tile to build on.
 * @param to_build New track bits.
 * @param flags    Flags of the operation.
 * @return Succeeded or failed command.
 */
static CommandCost CheckTrackCombination(TileIndex tile, TrackBits to_build, uint flags)
{
	if (!IsPlainRail(tile)) return_cmd_error(STR_ERROR_IMPOSSIBLE_TRACK_COMBINATION);

	/* So, we have a tile with tracks on it (and possibly signals). Let's see
	 * what tracks first */
	TrackBits current = GetTrackBits(tile); // The current track layout.
	TrackBits future = current | to_build;  // The track layout we want to build.

	/* Are we really building something new? */
	if (current == future) {
		/* Nothing new is being built */
		return_cmd_error(STR_ERROR_ALREADY_BUILT);
	}

	/* Let's see if we may build this */
	if ((flags & DC_NO_RAIL_OVERLAP) || HasSignals(tile)) {
		/* If we are not allowed to overlap (flag is on for ai companies or we have
		 * signals on the tile), check that */
		if (future != TRACK_BIT_HORZ && future != TRACK_BIT_VERT) {
			return_cmd_error((flags & DC_NO_RAIL_OVERLAP) ? STR_ERROR_IMPOSSIBLE_TRACK_COMBINATION : STR_ERROR_MUST_REMOVE_SIGNALS_FIRST);
		}
	}
	/* Normally, we may overlap and any combination is valid */
	return CommandCost();
}


/** Valid TrackBits on a specific (non-steep)-slope without foundation */
static const TrackBits _valid_tracks_without_foundation[15] = {
	TRACK_BIT_ALL,
	TRACK_BIT_RIGHT,
	TRACK_BIT_UPPER,
	TRACK_BIT_X,

	TRACK_BIT_LEFT,
	TRACK_BIT_NONE,
	TRACK_BIT_Y,
	TRACK_BIT_LOWER,

	TRACK_BIT_LOWER,
	TRACK_BIT_Y,
	TRACK_BIT_NONE,
	TRACK_BIT_LEFT,

	TRACK_BIT_X,
	TRACK_BIT_UPPER,
	TRACK_BIT_RIGHT,
};

/** Valid TrackBits on a specific (non-steep)-slope with leveled foundation */
static const TrackBits _valid_tracks_on_leveled_foundation[15] = {
	TRACK_BIT_NONE,
	TRACK_BIT_LEFT,
	TRACK_BIT_LOWER,
	TRACK_BIT_Y | TRACK_BIT_LOWER | TRACK_BIT_LEFT,

	TRACK_BIT_RIGHT,
	TRACK_BIT_ALL,
	TRACK_BIT_X | TRACK_BIT_LOWER | TRACK_BIT_RIGHT,
	TRACK_BIT_ALL,

	TRACK_BIT_UPPER,
	TRACK_BIT_X | TRACK_BIT_UPPER | TRACK_BIT_LEFT,
	TRACK_BIT_ALL,
	TRACK_BIT_ALL,

	TRACK_BIT_Y | TRACK_BIT_UPPER | TRACK_BIT_RIGHT,
	TRACK_BIT_ALL,
	TRACK_BIT_ALL
};

/**
 * Checks if a track combination is valid on a specific slope and returns the needed foundation.
 *
 * @param tileh Tile slope.
 * @param bits  Trackbits.
 * @return Needed foundation or FOUNDATION_INVALID if track/slope combination is not allowed.
 */
Foundation GetRailFoundation(Slope tileh, TrackBits bits)
{
	if (bits == TRACK_BIT_NONE) return FOUNDATION_NONE;

	if (IsSteepSlope(tileh)) {
		/* Test for inclined foundations */
		if (bits == TRACK_BIT_X) return FOUNDATION_INCLINED_X;
		if (bits == TRACK_BIT_Y) return FOUNDATION_INCLINED_Y;

		/* Get higher track */
		Corner highest_corner = GetHighestSlopeCorner(tileh);
		TrackBits higher_track = CornerToTrackBits(highest_corner);

		/* Only higher track? */
		if (bits == higher_track) return HalftileFoundation(highest_corner);

		/* Overlap with higher track? */
		if (TracksOverlap(bits | higher_track)) return FOUNDATION_INVALID;

		/* either lower track or both higher and lower track */
		return ((bits & higher_track) != 0 ? FOUNDATION_STEEP_BOTH : FOUNDATION_STEEP_LOWER);
	} else {
		if ((~_valid_tracks_without_foundation[tileh] & bits) == 0) return FOUNDATION_NONE;

		bool valid_on_leveled = ((~_valid_tracks_on_leveled_foundation[tileh] & bits) == 0);

		Corner track_corner;
		switch (bits) {
			case TRACK_BIT_LEFT:  track_corner = CORNER_W; break;
			case TRACK_BIT_LOWER: track_corner = CORNER_S; break;
			case TRACK_BIT_RIGHT: track_corner = CORNER_E; break;
			case TRACK_BIT_UPPER: track_corner = CORNER_N; break;

			case TRACK_BIT_HORZ:
				if (tileh == SLOPE_N) return HalftileFoundation(CORNER_N);
				if (tileh == SLOPE_S) return HalftileFoundation(CORNER_S);
				return (valid_on_leveled ? FOUNDATION_LEVELED : FOUNDATION_INVALID);

			case TRACK_BIT_VERT:
				if (tileh == SLOPE_W) return HalftileFoundation(CORNER_W);
				if (tileh == SLOPE_E) return HalftileFoundation(CORNER_E);
				return (valid_on_leveled ? FOUNDATION_LEVELED : FOUNDATION_INVALID);

			case TRACK_BIT_X:
				if (IsSlopeWithOneCornerRaised(tileh)) return FOUNDATION_INCLINED_X;
				return (valid_on_leveled ? FOUNDATION_LEVELED : FOUNDATION_INVALID);

			case TRACK_BIT_Y:
				if (IsSlopeWithOneCornerRaised(tileh)) return FOUNDATION_INCLINED_Y;
				return (valid_on_leveled ? FOUNDATION_LEVELED : FOUNDATION_INVALID);

			default:
				return (valid_on_leveled ? FOUNDATION_LEVELED : FOUNDATION_INVALID);
		}
		/* Single diagonal track */

		/* Track must be at least valid on leveled foundation */
		if (!valid_on_leveled) return FOUNDATION_INVALID;

		/* If slope has three raised corners, build leveled foundation */
		if (IsSlopeWithThreeCornersRaised(tileh)) return FOUNDATION_LEVELED;

		/* If neighboured corners of track_corner are lowered, build halftile foundation */
		if ((tileh & SlopeWithThreeCornersRaised(OppositeCorner(track_corner))) == SlopeWithOneCornerRaised(track_corner)) return HalftileFoundation(track_corner);

		/* else special anti-zig-zag foundation */
		return SpecialRailFoundation(track_corner);
	}
}


/**
 * Tests if a track can be build on a tile.
 *
 * @param tileh Tile slope.
 * @param rail_bits Tracks to build.
 * @param existing Tracks already built.
 * @param tile Tile (used for water test)
 * @return Error message or cost for foundation building.
 */
static CommandCost CheckRailSlope(Slope tileh, TrackBits rail_bits, TrackBits existing, TileIndex tile)
{
	/* don't allow building on the lower side of a coast */
	if (IsTileType(tile, MP_WATER) || (IsTileType(tile, MP_RAILWAY) && (GetRailGroundType(tile) == RAIL_GROUND_WATER))) {
		if (!IsSteepSlope(tileh) && ((~_valid_tracks_on_leveled_foundation[tileh] & (rail_bits | existing)) != 0)) return_cmd_error(STR_ERROR_CAN_T_BUILD_ON_WATER);
	}

	Foundation f_new = GetRailFoundation(tileh, rail_bits | existing);

	/* check track/slope combination */
	if ((f_new == FOUNDATION_INVALID) ||
			((f_new != FOUNDATION_NONE) && (!_settings_game.construction.build_on_slopes))) {
		return_cmd_error(STR_ERROR_LAND_SLOPED_IN_WRONG_DIRECTION);
	}

	Foundation f_old = GetRailFoundation(tileh, existing);
	return CommandCost(EXPENSES_CONSTRUCTION, f_new != f_old ? _price[PR_BUILD_FOUNDATION] : (Money)0);
}

/* Validate functions for rail building */
static inline bool ValParamTrackOrientation(Track track)
{
	return IsValidTrack(track);
}

/** Build a single piece of rail
 * @param tile tile  to build on
 * @param flags operation to perform
 * @param p1 railtype of being built piece (normal, mono, maglev)
 * @param p2 rail track to build
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdBuildSingleRail(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	RailType railtype = Extract<RailType, 0, 4>(p1);
	Track track = Extract<Track, 0, 3>(p2);
	CommandCost cost(EXPENSES_CONSTRUCTION);

	if (!ValParamRailtype(railtype) || !ValParamTrackOrientation(track)) return CMD_ERROR;

	Slope tileh = GetTileSlope(tile, NULL);
	TrackBits trackbit = TrackToTrackBits(track);

	switch (GetTileType(tile)) {
		case MP_RAILWAY: {
			CommandCost ret = CheckTileOwnership(tile);
			if (ret.Failed()) return ret;

			if (!IsPlainRail(tile)) return CMD_ERROR;

			if (!IsCompatibleRail(GetRailType(tile), railtype)) return_cmd_error(STR_ERROR_IMPOSSIBLE_TRACK_COMBINATION);

			ret = CheckTrackCombination(tile, trackbit, flags);
			if (ret.Succeeded()) ret = EnsureNoTrainOnTrack(tile, track);
			if (ret.Failed()) return ret;

			ret = CheckRailSlope(tileh, trackbit, GetTrackBits(tile), tile);
			if (ret.Failed()) return ret;
			cost.AddCost(ret);

			/* If the rail types don't match, try to convert only if engines of
			 * the new rail type are not powered on the present rail type and engines of
			 * the present rail type are powered on the new rail type. */
			if (GetRailType(tile) != railtype && !HasPowerOnRail(railtype, GetRailType(tile))) {
				if (HasPowerOnRail(GetRailType(tile), railtype)) {
					ret = DoCommand(tile, tile, railtype, flags, CMD_CONVERT_RAIL);
					if (ret.Failed()) return ret;
					cost.AddCost(ret);
				} else {
					return CMD_ERROR;
				}
			}

			if (flags & DC_EXEC) {
				SetRailGroundType(tile, RAIL_GROUND_BARREN);
				SetTrackBits(tile, GetTrackBits(tile) | trackbit);
			}
			break;
		}

		case MP_ROAD: {
#define M(x) (1 << (x))
			/* Level crossings may only be built on these slopes */
			if (!HasBit(M(SLOPE_SEN) | M(SLOPE_ENW) | M(SLOPE_NWS) | M(SLOPE_NS) | M(SLOPE_WSE) | M(SLOPE_EW) | M(SLOPE_FLAT), tileh)) {
				return_cmd_error(STR_ERROR_LAND_SLOPED_IN_WRONG_DIRECTION);
			}
#undef M

			CommandCost ret = EnsureNoVehicleOnGround(tile);
			if (ret.Failed()) return ret;

			if (IsNormalRoad(tile)) {
				if (HasRoadWorks(tile)) return_cmd_error(STR_ERROR_ROAD_WORKS_IN_PROGRESS);

				if (GetDisallowedRoadDirections(tile) != DRD_NONE) return_cmd_error(STR_ERROR_CROSSING_ON_ONEWAY_ROAD);

				RoadTypes roadtypes = GetRoadTypes(tile);
				RoadBits road = GetRoadBits(tile, ROADTYPE_ROAD);
				RoadBits tram = GetRoadBits(tile, ROADTYPE_TRAM);
				switch (roadtypes) {
					default: break;
					case ROADTYPES_TRAM:
						/* Tram crossings must always have road. */
						if (flags & DC_EXEC) SetRoadOwner(tile, ROADTYPE_ROAD, _current_company);
						roadtypes |= ROADTYPES_ROAD;
						break;

					case ROADTYPES_ALL:
						if (road != tram) return CMD_ERROR;
						break;
				}

				road |= tram;

				if ((track == TRACK_X && road == ROAD_Y) ||
						(track == TRACK_Y && road == ROAD_X)) {
					if (flags & DC_EXEC) {
						MakeRoadCrossing(tile, GetRoadOwner(tile, ROADTYPE_ROAD), GetRoadOwner(tile, ROADTYPE_TRAM), _current_company, (track == TRACK_X ? AXIS_Y : AXIS_X), railtype, roadtypes, GetTownIndex(tile));
						UpdateLevelCrossing(tile, false);
					}
					break;
				}
			}

			if (IsLevelCrossing(tile) && GetCrossingRailBits(tile) == trackbit) {
				return_cmd_error(STR_ERROR_ALREADY_BUILT);
			}
			/* FALLTHROUGH */
		}

		default: {
			/* Will there be flat water on the lower halftile? */
			bool water_ground = IsTileType(tile, MP_WATER) && IsSlopeWithOneCornerRaised(tileh);

			CommandCost ret = CheckRailSlope(tileh, trackbit, TRACK_BIT_NONE, tile);
			if (ret.Failed()) return ret;
			cost.AddCost(ret);

			ret = DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
			if (ret.Failed()) return ret;
			cost.AddCost(ret);

			if (water_ground) {
				cost.AddCost(-_price[PR_CLEAR_WATER]);
				cost.AddCost(_price[PR_CLEAR_ROUGH]);
			}

			if (flags & DC_EXEC) {
				MakeRailNormal(tile, _current_company, trackbit, railtype);
				if (water_ground) SetRailGroundType(tile, RAIL_GROUND_WATER);
			}
			break;
		}
	}

	if (flags & DC_EXEC) {
		MarkTileDirtyByTile(tile);
		AddTrackToSignalBuffer(tile, track, _current_company);
		YapfNotifyTrackLayoutChange(tile, track);
	}

	cost.AddCost(RailBuildCost(railtype));
	return cost;
}

/** Remove a single piece of track
 * @param tile tile to remove track from
 * @param flags operation to perform
 * @param p1 unused
 * @param p2 rail orientation
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdRemoveSingleRail(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	Track track = Extract<Track, 0, 3>(p2);
	CommandCost cost(EXPENSES_CONSTRUCTION);
	bool crossing = false;

	if (!ValParamTrackOrientation(track)) return CMD_ERROR;
	TrackBits trackbit = TrackToTrackBits(track);

	/* Need to read tile owner now because it may change when the rail is removed
	 * Also, in case of floods, _current_company != owner
	 * There may be invalid tiletype even in exec run (when removing long track),
	 * so do not call GetTileOwner(tile) in any case here */
	Owner owner = INVALID_OWNER;

	Train *v = NULL;

	switch (GetTileType(tile)) {
		case MP_ROAD: {
			if (!IsLevelCrossing(tile) || GetCrossingRailBits(tile) != trackbit) return CMD_ERROR;

			if (_current_company != OWNER_WATER) {
				CommandCost ret = CheckTileOwnership(tile);
				if (ret.Failed()) return ret;
			}

			if (!(flags & DC_BANKRUPT)) {
				CommandCost ret = EnsureNoVehicleOnGround(tile);
				if (ret.Failed()) return ret;
			}

			cost.AddCost(RailClearCost(GetRailType(tile)));

			if (flags & DC_EXEC) {
				if (HasReservedTracks(tile, trackbit)) {
					v = GetTrainForReservation(tile, track);
					if (v != NULL) FreeTrainTrackReservation(v);
				}
				owner = GetTileOwner(tile);
				MakeRoadNormal(tile, GetCrossingRoadBits(tile), GetRoadTypes(tile), GetTownIndex(tile), GetRoadOwner(tile, ROADTYPE_ROAD), GetRoadOwner(tile, ROADTYPE_TRAM));
			}
			break;
		}

		case MP_RAILWAY: {
			TrackBits present;

			if (!IsPlainRail(tile)) return CMD_ERROR;

			if (_current_company != OWNER_WATER) {
				CommandCost ret = CheckTileOwnership(tile);
				if (ret.Failed()) return ret;
			}

			CommandCost ret = EnsureNoTrainOnTrack(tile, track);
			if (ret.Failed()) return ret;

			present = GetTrackBits(tile);
			if ((present & trackbit) == 0) return CMD_ERROR;
			if (present == (TRACK_BIT_X | TRACK_BIT_Y)) crossing = true;

			cost.AddCost(RailClearCost(GetRailType(tile)));

			/* Charge extra to remove signals on the track, if they are there */
			if (HasSignalOnTrack(tile, track))
				cost.AddCost(DoCommand(tile, track, 0, flags, CMD_REMOVE_SIGNALS));

			if (flags & DC_EXEC) {
				if (HasReservedTracks(tile, trackbit)) {
					v = GetTrainForReservation(tile, track);
					if (v != NULL) FreeTrainTrackReservation(v);
				}
				owner = GetTileOwner(tile);
				present ^= trackbit;
				if (present == 0) {
					Slope tileh = GetTileSlope(tile, NULL);
					/* If there is flat water on the lower halftile, convert the tile to shore so the water remains */
					if (GetRailGroundType(tile) == RAIL_GROUND_WATER && IsSlopeWithOneCornerRaised(tileh)) {
						MakeShore(tile);
					} else {
						DoClearSquare(tile);
					}
				} else {
					SetTrackBits(tile, present);
					SetTrackReservation(tile, GetRailReservationTrackBits(tile) & present);
				}
			}
			break;
		}

		default: return CMD_ERROR;
	}

	if (flags & DC_EXEC) {
		/* if we got that far, 'owner' variable is set correctly */
		assert(Company::IsValidID(owner));

		MarkTileDirtyByTile(tile);
		if (crossing) {
			/* crossing is set when only TRACK_BIT_X and TRACK_BIT_Y are set. As we
			 * are removing one of these pieces, we'll need to update signals for
			 * both directions explicitly, as after the track is removed it won't
			 * 'connect' with the other piece. */
			AddTrackToSignalBuffer(tile, TRACK_X, owner);
			AddTrackToSignalBuffer(tile, TRACK_Y, owner);
			YapfNotifyTrackLayoutChange(tile, TRACK_X);
			YapfNotifyTrackLayoutChange(tile, TRACK_Y);
		} else {
			AddTrackToSignalBuffer(tile, track, owner);
			YapfNotifyTrackLayoutChange(tile, track);
		}

		if (v != NULL) TryPathReserve(v, true);
	}

	return cost;
}


/**
 * Called from water_cmd if a non-flat rail-tile gets flooded and should be converted to shore.
 * The function floods the lower halftile, if the tile has a halftile foundation.
 *
 * @param t The tile to flood.
 * @return true if something was flooded.
 */
bool FloodHalftile(TileIndex t)
{
	assert(IsPlainRailTile(t));

	bool flooded = false;
	if (GetRailGroundType(t) == RAIL_GROUND_WATER) return flooded;

	Slope tileh = GetTileSlope(t, NULL);
	TrackBits rail_bits = GetTrackBits(t);

	if (IsSlopeWithOneCornerRaised(tileh)) {
		TrackBits lower_track = CornerToTrackBits(OppositeCorner(GetHighestSlopeCorner(tileh)));

		TrackBits to_remove = lower_track & rail_bits;
		if (to_remove != 0) {
			_current_company = OWNER_WATER;
			if (DoCommand(t, 0, FIND_FIRST_BIT(to_remove), DC_EXEC, CMD_REMOVE_SINGLE_RAIL).Failed()) return flooded; // not yet floodable
			flooded = true;
			rail_bits = rail_bits & ~to_remove;
			if (rail_bits == 0) {
				MakeShore(t);
				MarkTileDirtyByTile(t);
				return flooded;
			}
		}

		if (IsNonContinuousFoundation(GetRailFoundation(tileh, rail_bits))) {
			flooded = true;
			SetRailGroundType(t, RAIL_GROUND_WATER);
			MarkTileDirtyByTile(t);
		}
	} else {
		/* Make shore on steep slopes and 'three-corners-raised'-slopes. */
		if (ApplyFoundationToSlope(GetRailFoundation(tileh, rail_bits), &tileh) == 0) {
			if (IsSteepSlope(tileh) || IsSlopeWithThreeCornersRaised(tileh)) {
				flooded = true;
				SetRailGroundType(t, RAIL_GROUND_WATER);
				MarkTileDirtyByTile(t);
			}
		}
	}
	return flooded;
}

static const TileIndexDiffC _trackdelta[] = {
	{ -1,  0 }, {  0,  1 }, { -1,  0 }, {  0,  1 }, {  1,  0 }, {  0,  1 },
	{  0,  0 },
	{  0,  0 },
	{  1,  0 }, {  0, -1 }, {  0, -1 }, {  1,  0 }, {  0, -1 }, { -1,  0 },
	{  0,  0 },
	{  0,  0 }
};


static CommandCost ValidateAutoDrag(Trackdir *trackdir, TileIndex start, TileIndex end)
{
	int x = TileX(start);
	int y = TileY(start);
	int ex = TileX(end);
	int ey = TileY(end);

	if (!ValParamTrackOrientation(TrackdirToTrack(*trackdir))) return CMD_ERROR;

	/* calculate delta x,y from start to end tile */
	int dx = ex - x;
	int dy = ey - y;

	/* calculate delta x,y for the first direction */
	int trdx = _trackdelta[*trackdir].x;
	int trdy = _trackdelta[*trackdir].y;

	if (!IsDiagonalTrackdir(*trackdir)) {
		trdx += _trackdelta[*trackdir ^ 1].x;
		trdy += _trackdelta[*trackdir ^ 1].y;
	}

	/* validate the direction */
	while (
		(trdx <= 0 && dx > 0) ||
		(trdx >= 0 && dx < 0) ||
		(trdy <= 0 && dy > 0) ||
		(trdy >= 0 && dy < 0)
	) {
		if (!HasBit(*trackdir, 3)) { // first direction is invalid, try the other
			SetBit(*trackdir, 3); // reverse the direction
			trdx = -trdx;
			trdy = -trdy;
		} else { // other direction is invalid too, invalid drag
			return CMD_ERROR;
		}
	}

	/* (for diagonal tracks, this is already made sure of by above test), but:
	 * for non-diagonal tracks, check if the start and end tile are on 1 line */
	if (!IsDiagonalTrackdir(*trackdir)) {
		trdx = _trackdelta[*trackdir].x;
		trdy = _trackdelta[*trackdir].y;
		if (abs(dx) != abs(dy) && abs(dx) + abs(trdy) != abs(dy) + abs(trdx)) return CMD_ERROR;
	}

	return CommandCost();
}

/** Build or remove a stretch of railroad tracks.
 * @param tile start tile of drag
 * @param flags operation to perform
 * @param p1 end tile of drag
 * @param p2 various bitstuffed elements
 * - p2 = (bit 0-3) - railroad type normal/maglev (0 = normal, 1 = mono, 2 = maglev)
 * - p2 = (bit 4-6) - track-orientation, valid values: 0-5 (Track enum)
 * - p2 = (bit 7)   - 0 = build, 1 = remove tracks
 * - p2 = (bit 8)   - 0 = build up to an obstacle, 1 = fail if an obstacle is found (used for AIs).
 * @param text unused
 * @return the cost of this operation or an error
 */
static CommandCost CmdRailTrackHelper(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	CommandCost total_cost(EXPENSES_CONSTRUCTION);
	Track track = Extract<Track, 4, 3>(p2);
	bool remove = HasBit(p2, 7);
	RailType railtype = Extract<RailType, 0, 4>(p2);

	if (!ValParamRailtype(railtype) || !ValParamTrackOrientation(track)) return CMD_ERROR;
	if (p1 >= MapSize()) return CMD_ERROR;
	TileIndex end_tile = p1;
	Trackdir trackdir = TrackToTrackdir(track);

	CommandCost ret = ValidateAutoDrag(&trackdir, tile, end_tile);
	if (ret.Failed()) return ret;

	if (flags & DC_EXEC) SndPlayTileFx(SND_20_SPLAT_2, tile);

	bool had_success = false;
	CommandCost last_error = CMD_ERROR;
	for (;;) {
		CommandCost ret = DoCommand(tile, railtype, TrackdirToTrack(trackdir), flags, remove ? CMD_REMOVE_SINGLE_RAIL : CMD_BUILD_SINGLE_RAIL);

		if (ret.Failed()) {
			last_error = ret;
			if (last_error.GetErrorMessage() != STR_ERROR_ALREADY_BUILT && !remove) {
				if (HasBit(p2, 8)) return last_error;
				break;
			}
		} else {
			had_success = true;
			total_cost.AddCost(ret);
		}

		if (tile == end_tile) break;

		tile += ToTileIndexDiff(_trackdelta[trackdir]);

		/* toggle railbit for the non-diagonal tracks */
		if (!IsDiagonalTrackdir(trackdir)) ToggleBit(trackdir, 0);
	}

	if (had_success) return total_cost;
	return last_error;
}

/** Build rail on a stretch of track.
 * Stub for the unified rail builder/remover
 * @param tile start tile of drag
 * @param flags operation to perform
 * @param p1 end tile of drag
 * @param p2 various bitstuffed elements
 * - p2 = (bit 0-3) - railroad type normal/maglev (0 = normal, 1 = mono, 2 = maglev)
 * - p2 = (bit 4-6) - track-orientation, valid values: 0-5 (Track enum)
 * - p2 = (bit 7)   - 0 = build, 1 = remove tracks
 * @param text unused
 * @return the cost of this operation or an error
 * @see CmdRailTrackHelper
 */
CommandCost CmdBuildRailroadTrack(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	return CmdRailTrackHelper(tile, flags, p1, ClrBit(p2, 7), text);
}

/** Build rail on a stretch of track.
 * Stub for the unified rail builder/remover
 * @param tile start tile of drag
 * @param flags operation to perform
 * @param p1 end tile of drag
 * @param p2 various bitstuffed elements
 * - p2 = (bit 0-3) - railroad type normal/maglev (0 = normal, 1 = mono, 2 = maglev)
 * - p2 = (bit 4-6) - track-orientation, valid values: 0-5 (Track enum)
 * - p2 = (bit 7)   - 0 = build, 1 = remove tracks
 * @param text unused
 * @return the cost of this operation or an error
 * @see CmdRailTrackHelper
 */
CommandCost CmdRemoveRailroadTrack(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	return CmdRailTrackHelper(tile, flags, p1, SetBit(p2, 7), text);
}

/** Build a train depot
 * @param tile position of the train depot
 * @param flags operation to perform
 * @param p1 rail type
 * @param p2 bit 0..1 entrance direction (DiagDirection)
 * @param text unused
 * @return the cost of this operation or an error
 *
 * @todo When checking for the tile slope,
 * distingush between "Flat land required" and "land sloped in wrong direction"
 */
CommandCost CmdBuildTrainDepot(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	/* check railtype and valid direction for depot (0 through 3), 4 in total */
	RailType railtype = Extract<RailType, 0, 4>(p1);
	if (!ValParamRailtype(railtype)) return CMD_ERROR;

	Slope tileh = GetTileSlope(tile, NULL);

	DiagDirection dir = Extract<DiagDirection, 0, 2>(p2);

	/* Prohibit construction if
	 * The tile is non-flat AND
	 * 1) build-on-slopes is disabled
	 * 2) the tile is steep i.e. spans two height levels
	 * 3) the exit points in the wrong direction
	 */

	if (tileh != SLOPE_FLAT && (
				!_settings_game.construction.build_on_slopes ||
				IsSteepSlope(tileh) ||
				!CanBuildDepotByTileh(dir, tileh)
			)) {
		return_cmd_error(STR_ERROR_FLAT_LAND_REQUIRED);
	}

	CommandCost cost = DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
	if (cost.Failed()) return cost;

	if (MayHaveBridgeAbove(tile) && IsBridgeAbove(tile)) return_cmd_error(STR_ERROR_MUST_DEMOLISH_BRIDGE_FIRST);

	if (!Depot::CanAllocateItem()) return CMD_ERROR;

	if (flags & DC_EXEC) {
		Depot *d = new Depot(tile);
		d->town_index = ClosestTownFromTile(tile, UINT_MAX)->index;

		MakeRailDepot(tile, _current_company, d->index, dir, railtype);
		MarkTileDirtyByTile(tile);

		AddSideToSignalBuffer(tile, INVALID_DIAGDIR, _current_company);
		YapfNotifyTrackLayoutChange(tile, DiagDirToDiagTrack(dir));
	}

	cost.AddCost(_price[PR_BUILD_DEPOT_TRAIN]);
	return cost;
}

/** Build signals, alternate between double/single, signal/semaphore,
 * pre/exit/combo-signals, and what-else not. If the rail piece does not
 * have any signals, bit 4 (cycle signal-type) is ignored
 * @param tile tile where to build the signals
 * @param flags operation to perform
 * @param p1 various bitstuffed elements
 * - p1 = (bit 0-2) - track-orientation, valid values: 0-5 (Track enum)
 * - p1 = (bit 3)   - 1 = override signal/semaphore, or pre/exit/combo signal or (for bit 7) toggle variant (CTRL-toggle)
 * - p1 = (bit 4)   - 0 = signals, 1 = semaphores
 * - p1 = (bit 5-7) - type of the signal, for valid values see enum SignalType in rail_map.h
 * - p1 = (bit 8)   - convert the present signal type and variant
 * - p1 = (bit 9-11)- start cycle from this signal type
 * - p1 = (bit 12-14)-wrap around after this signal type
 * - p1 = (bit 15-16)-cycle the signal direction this many times
 * - p1 = (bit 17)  - 1 = don't modify an existing signal but don't fail either, 0 = always set new signal type
 * @param p2 used for CmdBuildManySignals() to copy direction of first signal
 * @param text unused
 * @return the cost of this operation or an error
 * @todo p2 should be replaced by two bits for "along" and "against" the track.
 */
CommandCost CmdBuildSingleSignal(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	Track track = Extract<Track, 0, 3>(p1);
	bool ctrl_pressed = HasBit(p1, 3); // was the CTRL button pressed
	SignalVariant sigvar = (ctrl_pressed ^ HasBit(p1, 4)) ? SIG_SEMAPHORE : SIG_ELECTRIC; // the signal variant of the new signal
	SignalType sigtype = Extract<SignalType, 5, 3>(p1); // the signal type of the new signal
	bool convert_signal = HasBit(p1, 8); // convert button pressed
	SignalType cycle_start = Extract<SignalType, 9, 3>(p1);
	SignalType cycle_stop = Extract<SignalType, 12, 3>(p1);
	uint num_dir_cycle = GB(p1, 15, 2);

	if (sigtype > SIGTYPE_LAST) return CMD_ERROR;
	if (cycle_start > cycle_stop || cycle_stop > SIGTYPE_LAST) return CMD_ERROR;

	/* You can only build signals on plain rail tiles, and the selected track must exist */
	if (!ValParamTrackOrientation(track) || !IsPlainRailTile(tile) ||
			!HasTrack(tile, track)) {
		return CMD_ERROR;
	}
	CommandCost ret = EnsureNoTrainOnTrack(tile, track);
	if (ret.Failed()) return ret;

	/* Protect against invalid signal copying */
	if (p2 != 0 && (p2 & SignalOnTrack(track)) == 0) return CMD_ERROR;

	ret = CheckTileOwnership(tile);
	if (ret.Failed()) return ret;

	{
		/* See if this is a valid track combination for signals, (ie, no overlap) */
		TrackBits trackbits = GetTrackBits(tile);
		if (KillFirstBit(trackbits) != TRACK_BIT_NONE && // More than one track present
				trackbits != TRACK_BIT_HORZ &&
				trackbits != TRACK_BIT_VERT) {
			return_cmd_error(STR_ERROR_NO_SUITABLE_RAILROAD_TRACK);
		}
	}

	/* In case we don't want to change an existing signal, return without error. */
	if (HasBit(p1, 17) && HasSignalOnTrack(tile, track)) return CommandCost();

	/* you can not convert a signal if no signal is on track */
	if (convert_signal && !HasSignalOnTrack(tile, track)) return CMD_ERROR;

	CommandCost cost;
	if (!HasSignalOnTrack(tile, track)) {
		/* build new signals */
		cost = CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_SIGNALS]);
	} else {
		if (p2 != 0 && sigvar != GetSignalVariant(tile, track)) {
			/* convert signals <-> semaphores */
			cost = CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_SIGNALS] + _price[PR_CLEAR_SIGNALS]);

		} else if (convert_signal) {
			/* convert button pressed */
			if (ctrl_pressed || GetSignalVariant(tile, track) != sigvar) {
				/* convert electric <-> semaphore */
				cost = CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_SIGNALS] + _price[PR_CLEAR_SIGNALS]);
			} else {
				/* it is free to change signal type: normal-pre-exit-combo */
				cost = CommandCost();
			}

		} else {
			/* it is free to change orientation/pre-exit-combo signals */
			cost = CommandCost();
		}
	}

	if (flags & DC_EXEC) {
		Train *v = NULL;
		/* The new/changed signal could block our path. As this can lead to
		 * stale reservations, we clear the path reservation here and try
		 * to redo it later on. */
		if (HasReservedTracks(tile, TrackToTrackBits(track))) {
			v = GetTrainForReservation(tile, track);
			if (v != NULL) FreeTrainTrackReservation(v);
		}

		if (!HasSignals(tile)) {
			/* there are no signals at all on this tile yet */
			SetHasSignals(tile, true);
			SetSignalStates(tile, 0xF); // all signals are on
			SetPresentSignals(tile, 0); // no signals built by default
			SetSignalType(tile, track, sigtype);
			SetSignalVariant(tile, track, sigvar);
		}

		if (p2 == 0) {
			if (!HasSignalOnTrack(tile, track)) {
				/* build new signals */
				SetPresentSignals(tile, GetPresentSignals(tile) | (IsPbsSignal(sigtype) ? KillFirstBit(SignalOnTrack(track)) : SignalOnTrack(track)));
				SetSignalType(tile, track, sigtype);
				SetSignalVariant(tile, track, sigvar);
				while (num_dir_cycle-- > 0) CycleSignalSide(tile, track);
			} else {
				if (convert_signal) {
					/* convert signal button pressed */
					if (ctrl_pressed) {
						/* toggle the pressent signal variant: SIG_ELECTRIC <-> SIG_SEMAPHORE */
						SetSignalVariant(tile, track, (GetSignalVariant(tile, track) == SIG_ELECTRIC) ? SIG_SEMAPHORE : SIG_ELECTRIC);
						/* Query current signal type so the check for PBS signals below works. */
						sigtype = GetSignalType(tile, track);
					} else {
						/* convert the present signal to the chosen type and variant */
						SetSignalType(tile, track, sigtype);
						SetSignalVariant(tile, track, sigvar);
						if (IsPbsSignal(sigtype) && (GetPresentSignals(tile) & SignalOnTrack(track)) == SignalOnTrack(track)) {
							SetPresentSignals(tile, (GetPresentSignals(tile) & ~SignalOnTrack(track)) | KillFirstBit(SignalOnTrack(track)));
						}
					}

				} else if (ctrl_pressed) {
					/* cycle between cycle_start and cycle_end */
					sigtype = (SignalType)(GetSignalType(tile, track) + 1);

					if (sigtype < cycle_start || sigtype > cycle_stop) sigtype = cycle_start;

					SetSignalType(tile, track, sigtype);
					if (IsPbsSignal(sigtype) && (GetPresentSignals(tile) & SignalOnTrack(track)) == SignalOnTrack(track)) {
						SetPresentSignals(tile, (GetPresentSignals(tile) & ~SignalOnTrack(track)) | KillFirstBit(SignalOnTrack(track)));
					}
				} else {
					/* cycle the signal side: both -> left -> right -> both -> ... */
					CycleSignalSide(tile, track);
					/* Query current signal type so the check for PBS signals below works. */
					sigtype = GetSignalType(tile, track);
				}
			}
		} else {
			/* If CmdBuildManySignals is called with copying signals, just copy the
			 * direction of the first signal given as parameter by CmdBuildManySignals */
			SetPresentSignals(tile, (GetPresentSignals(tile) & ~SignalOnTrack(track)) | (p2 & SignalOnTrack(track)));
			SetSignalVariant(tile, track, sigvar);
			SetSignalType(tile, track, sigtype);
		}

		if (IsPbsSignal(sigtype)) {
			/* PBS signals should show red unless they are on a reservation. */
			uint mask = GetPresentSignals(tile) & SignalOnTrack(track);
			SetSignalStates(tile, (GetSignalStates(tile) & ~mask) | ((HasBit(GetRailReservationTrackBits(tile), track) ? UINT_MAX : 0) & mask));
		}
		MarkTileDirtyByTile(tile);
		AddTrackToSignalBuffer(tile, track, _current_company);
		YapfNotifyTrackLayoutChange(tile, track);
		if (v != NULL) {
			/* Extend the train's path if it's not stopped or loading, or not at a safe position. */
			if (!(((v->vehstatus & VS_STOPPED) && v->cur_speed == 0) || v->current_order.IsType(OT_LOADING)) ||
					!IsSafeWaitingPosition(v, v->tile, v->GetVehicleTrackdir(), true, _settings_game.pf.forbid_90_deg)) {
				TryPathReserve(v, true);
			}
		}
	}

	return cost;
}

static bool CheckSignalAutoFill(TileIndex &tile, Trackdir &trackdir, int &signal_ctr, bool remove)
{
	tile = AddTileIndexDiffCWrap(tile, _trackdelta[trackdir]);
	if (tile == INVALID_TILE) return false;

	/* Check for track bits on the new tile */
	TrackdirBits trackdirbits = TrackStatusToTrackdirBits(GetTileTrackStatus(tile, TRANSPORT_RAIL, 0));

	if (TracksOverlap(TrackdirBitsToTrackBits(trackdirbits))) return false;
	trackdirbits &= TrackdirReachesTrackdirs(trackdir);

	/* No track bits, must stop */
	if (trackdirbits == TRACKDIR_BIT_NONE) return false;

	/* Get the first track dir */
	trackdir = RemoveFirstTrackdir(&trackdirbits);

	/* Any left? It's a junction so we stop */
	if (trackdirbits != TRACKDIR_BIT_NONE) return false;

	switch (GetTileType(tile)) {
		case MP_RAILWAY:
			if (IsRailDepot(tile)) return false;
			if (!remove && HasSignalOnTrack(tile, TrackdirToTrack(trackdir))) return false;
			signal_ctr++;
			if (IsDiagonalTrackdir(trackdir)) {
				signal_ctr++;
				/* Ensure signal_ctr even so X and Y pieces get signals */
				ClrBit(signal_ctr, 0);
			}
			return true;

		case MP_ROAD:
			if (!IsLevelCrossing(tile)) return false;
			signal_ctr += 2;
			return true;

		case MP_TUNNELBRIDGE: {
			TileIndex orig_tile = tile; // backup old value

			if (GetTunnelBridgeTransportType(tile) != TRANSPORT_RAIL) return false;
			if (GetTunnelBridgeDirection(tile) != TrackdirToExitdir(trackdir)) return false;

			/* Skip to end of tunnel or bridge
			 * note that tile is a parameter by reference, so it must be updated */
			tile = GetOtherTunnelBridgeEnd(tile);

			signal_ctr += (GetTunnelBridgeLength(orig_tile, tile) + 2) * 2;
			return true;
		}

		default: return false;
	}
}

/** Build many signals by dragging; AutoSignals
 * @param tile start tile of drag
 * @param flags operation to perform
 * @param p1  end tile of drag
 * @param p2 various bitstuffed elements
 * - p2 = (bit  0- 2) - track-orientation, valid values: 0-5 (Track enum)
 * - p2 = (bit  3)    - 1 = override signal/semaphore, or pre/exit/combo signal (CTRL-toggle)
 * - p2 = (bit  4)    - 0 = signals, 1 = semaphores
 * - p2 = (bit  5)    - 0 = build, 1 = remove signals
 * - p2 = (bit  6)    - 0 = selected stretch, 1 = auto fill
 * - p2 = (bit  7- 9) - default signal type
 * - p2 = (bit 24-31) - user defined signals_density
 * @param text unused
 * @return the cost of this operation or an error
 */
static CommandCost CmdSignalTrackHelper(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	CommandCost total_cost(EXPENSES_CONSTRUCTION);
	TileIndex start_tile = tile;

	Track track = Extract<Track, 0, 3>(p2);
	bool mode = HasBit(p2, 3);
	bool semaphores = HasBit(p2, 4);
	bool remove = HasBit(p2, 5);
	bool autofill = HasBit(p2, 6);
	byte signal_density = GB(p2, 24, 8);

	if (p1 >= MapSize() || !ValParamTrackOrientation(track)) return CMD_ERROR;
	TileIndex end_tile = p1;
	if (signal_density == 0 || signal_density > 20) return CMD_ERROR;

	if (!IsPlainRailTile(tile)) return CMD_ERROR;

	/* for vertical/horizontal tracks, double the given signals density
	 * since the original amount will be too dense (shorter tracks) */
	signal_density *= 2;

	Trackdir trackdir = TrackToTrackdir(track);
	CommandCost ret = ValidateAutoDrag(&trackdir, tile, end_tile);
	if (ret.Failed()) return ret;

	track = TrackdirToTrack(trackdir); // trackdir might have changed, keep track in sync
	Trackdir start_trackdir = trackdir;

	/* Must start on a valid track to be able to avoid loops */
	if (!HasTrack(tile, track)) return CMD_ERROR;

	SignalType sigtype = (SignalType)GB(p2, 7, 3);
	if (sigtype > SIGTYPE_LAST) return CMD_ERROR;

	byte signals;
	/* copy the signal-style of the first rail-piece if existing */
	if (HasSignalOnTrack(tile, track)) {
		signals = GetPresentSignals(tile) & SignalOnTrack(track);
		assert(signals != 0);

		/* copy signal/semaphores style (independent of CTRL) */
		semaphores = GetSignalVariant(tile, track) != SIG_ELECTRIC;

		sigtype = GetSignalType(tile, track);
		/* Don't but copy pre-signal type */
		if (sigtype < SIGTYPE_PBS) sigtype = SIGTYPE_NORMAL;
	} else { // no signals exist, drag a two-way signal stretch
		signals = IsPbsSignal(sigtype) ? SignalAlongTrackdir(trackdir) : SignalOnTrack(track);
	}

	byte signal_dir = 0;
	if (signals & SignalAlongTrackdir(trackdir))   SetBit(signal_dir, 0);
	if (signals & SignalAgainstTrackdir(trackdir)) SetBit(signal_dir, 1);

	/* signal_ctr         - amount of tiles already processed
	 * signals_density    - setting to put signal on every Nth tile (double space on |, -- tracks)
	 **********
	 * trackdir   - trackdir to build with autorail
	 * semaphores - semaphores or signals
	 * signals    - is there a signal/semaphore on the first tile, copy its style (two-way/single-way)
	 *              and convert all others to semaphore/signal
	 * remove     - 1 remove signals, 0 build signals */
	int signal_ctr = 0;
	CommandCost last_error = CMD_ERROR;
	bool had_success = false;
	for (;;) {
		/* only build/remove signals with the specified density */
		if ((remove && autofill) || signal_ctr % signal_density == 0) {
			uint32 p1 = GB(TrackdirToTrack(trackdir), 0, 3);
			SB(p1, 3, 1, mode);
			SB(p1, 4, 1, semaphores);
			SB(p1, 5, 3, sigtype);
			if (!remove && signal_ctr == 0) SetBit(p1, 17);

			/* Pick the correct orientation for the track direction */
			signals = 0;
			if (HasBit(signal_dir, 0)) signals |= SignalAlongTrackdir(trackdir);
			if (HasBit(signal_dir, 1)) signals |= SignalAgainstTrackdir(trackdir);

			CommandCost ret = DoCommand(tile, p1, signals, flags, remove ? CMD_REMOVE_SIGNALS : CMD_BUILD_SIGNALS);

			/* Be user-friendly and try placing signals as much as possible */
			if (ret.Succeeded()) {
				had_success = true;
				total_cost.AddCost(ret);
			} else {
				last_error = ret;
			}
		}

		if (autofill) {
			if (!CheckSignalAutoFill(tile, trackdir, signal_ctr, remove)) break;

			/* Prevent possible loops */
			if (tile == start_tile && trackdir == start_trackdir) break;
		} else {
			if (tile == end_tile) break;

			tile += ToTileIndexDiff(_trackdelta[trackdir]);
			signal_ctr++;

			/* toggle railbit for the non-diagonal tracks (|, -- tracks) */
			if (IsDiagonalTrackdir(trackdir)) {
				signal_ctr++;
			} else {
				ToggleBit(trackdir, 0);
			}
		}
	}

	return had_success ? total_cost : last_error;
}

/** Build signals on a stretch of track.
 * Stub for the unified signal builder/remover
 * @param tile start tile of drag
 * @param flags operation to perform
 * @param p1  end tile of drag
 * @param p2 various bitstuffed elements
 * - p2 = (bit  0- 2) - track-orientation, valid values: 0-5 (Track enum)
 * - p2 = (bit  3)    - 1 = override signal/semaphore, or pre/exit/combo signal (CTRL-toggle)
 * - p2 = (bit  4)    - 0 = signals, 1 = semaphores
 * - p2 = (bit  5)    - 0 = build, 1 = remove signals
 * - p2 = (bit  6)    - 0 = selected stretch, 1 = auto fill
 * - p2 = (bit  7- 9) - default signal type
 * - p2 = (bit 24-31) - user defined signals_density
 * @param text unused
 * @return the cost of this operation or an error
 * @see CmdSignalTrackHelper
 */
CommandCost CmdBuildSignalTrack(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	return CmdSignalTrackHelper(tile, flags, p1, p2, text);
}

/** Remove signals
 * @param tile coordinates where signal is being deleted from
 * @param flags operation to perform
 * @param p1 various bitstuffed elements, only track information is used
 *           - (bit  0- 2) - track-orientation, valid values: 0-5 (Track enum)
 *           - (bit  3)    - override signal/semaphore, or pre/exit/combo signal (CTRL-toggle)
 *           - (bit  4)    - 0 = signals, 1 = semaphores
 * @param p2 unused
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdRemoveSingleSignal(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	Track track = Extract<Track, 0, 3>(p1);

	if (!ValParamTrackOrientation(track) ||
			!IsPlainRailTile(tile) ||
			!HasTrack(tile, track) ||
			!HasSignalOnTrack(tile, track)) {
		return CMD_ERROR;
	}
	CommandCost ret = EnsureNoTrainOnTrack(tile, track);
	if (ret.Failed()) return ret;

	/* Only water can remove signals from anyone */
	if (_current_company != OWNER_WATER) {
		CommandCost ret = CheckTileOwnership(tile);
		if (ret.Failed()) return ret;
	}

	/* Do it? */
	if (flags & DC_EXEC) {
		Train *v = NULL;
		if (HasReservedTracks(tile, TrackToTrackBits(track))) {
			v = GetTrainForReservation(tile, track);
		} else if (IsPbsSignal(GetSignalType(tile, track))) {
			/* PBS signal, might be the end of a path reservation. */
			Trackdir td = TrackToTrackdir(track);
			for (int i = 0; v == NULL && i < 2; i++, td = ReverseTrackdir(td)) {
				/* Only test the active signal side. */
				if (!HasSignalOnTrackdir(tile, ReverseTrackdir(td))) continue;
				TileIndex next = TileAddByDiagDir(tile, TrackdirToExitdir(td));
				TrackBits tracks = TrackdirBitsToTrackBits(TrackdirReachesTrackdirs(td));
				if (HasReservedTracks(next, tracks)) {
					v = GetTrainForReservation(next, TrackBitsToTrack(GetReservedTrackbits(next) & tracks));
				}
			}
		}
		SetPresentSignals(tile, GetPresentSignals(tile) & ~SignalOnTrack(track));

		/* removed last signal from tile? */
		if (GetPresentSignals(tile) == 0) {
			SetSignalStates(tile, 0);
			SetHasSignals(tile, false);
			SetSignalVariant(tile, INVALID_TRACK, SIG_ELECTRIC); // remove any possible semaphores
		}

		AddTrackToSignalBuffer(tile, track, GetTileOwner(tile));
		YapfNotifyTrackLayoutChange(tile, track);
		if (v != NULL) TryPathReserve(v, false);

		MarkTileDirtyByTile(tile);
	}

	return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_CLEAR_SIGNALS]);
}

/** Remove signals on a stretch of track.
 * Stub for the unified signal builder/remover
 * @param tile start tile of drag
 * @param flags operation to perform
 * @param p1  end tile of drag
 * @param p2 various bitstuffed elements
 * - p2 = (bit  0- 2) - track-orientation, valid values: 0-5 (Track enum)
 * - p2 = (bit  3)    - 1 = override signal/semaphore, or pre/exit/combo signal (CTRL-toggle)
 * - p2 = (bit  4)    - 0 = signals, 1 = semaphores
 * - p2 = (bit  5)    - 0 = build, 1 = remove signals
 * - p2 = (bit  6)    - 0 = selected stretch, 1 = auto fill
 * - p2 = (bit  7- 9) - default signal type
 * - p2 = (bit 24-31) - user defined signals_density
 * @param text unused
 * @return the cost of this operation or an error
 * @see CmdSignalTrackHelper
 */
CommandCost CmdRemoveSignalTrack(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	return CmdSignalTrackHelper(tile, flags, p1, SetBit(p2, 5), text); // bit 5 is remove bit
}

/** Update power of train under which is the railtype being converted */
static Vehicle *UpdateTrainPowerProc(Vehicle *v, void *data)
{
	if (v->type != VEH_TRAIN) return NULL;

	/* Similar checks as in Train::PowerChanged() */

	Train *t = Train::From(v);
	if (t->IsArticulatedPart()) return NULL;

	const RailVehicleInfo *rvi = RailVehInfo(t->engine_type);
	if (GetVehicleProperty(t, PROP_TRAIN_POWER, rvi->power) != 0) t->First()->PowerChanged();

	return NULL;
}

/** Convert one rail type to the other. You can convert normal rail to
 * monorail/maglev easily or vice-versa.
 * @param tile end tile of rail conversion drag
 * @param flags operation to perform
 * @param p1 start tile of drag
 * @param p2 new railtype to convert to
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdConvertRail(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	RailType totype = Extract<RailType, 0, 4>(p2);

	if (!ValParamRailtype(totype)) return CMD_ERROR;
	if (p1 >= MapSize()) return CMD_ERROR;

	uint ex = TileX(tile);
	uint ey = TileY(tile);
	uint sx = TileX(p1);
	uint sy = TileY(p1);

	/* make sure sx,sy are smaller than ex,ey */
	if (ex < sx) Swap(ex, sx);
	if (ey < sy) Swap(ey, sy);

	CommandCost cost(EXPENSES_CONSTRUCTION);
	CommandCost error = CommandCost(STR_ERROR_NO_SUITABLE_RAILROAD_TRACK); // by default, there is no track to convert.
	for (uint x = sx; x <= ex; ++x) {
		for (uint y = sy; y <= ey; ++y) {
			TileIndex tile = TileXY(x, y);
			TileType tt = GetTileType(tile);

			/* Check if there is any track on tile */
			switch (tt) {
				case MP_RAILWAY:
					break;
				case MP_STATION:
					if (!HasStationRail(tile)) continue;
					break;
				case MP_ROAD:
					if (!IsLevelCrossing(tile)) continue;
					break;
				case MP_TUNNELBRIDGE:
					if (GetTunnelBridgeTransportType(tile) != TRANSPORT_RAIL) continue;
					break;
				default: continue;
			}

			/* Original railtype we are converting from */
			RailType type = GetRailType(tile);

			/* Converting to the same type or converting 'hidden' elrail -> rail */
			if (type == totype || (_settings_game.vehicle.disable_elrails && totype == RAILTYPE_RAIL && type == RAILTYPE_ELECTRIC)) continue;

			/* Trying to convert other's rail */
			CommandCost ret = CheckTileOwnership(tile);
			if (ret.Failed()) {
				error = ret;
				continue;
			}

			SmallVector<Train *, 2> vehicles_affected;

			/* Vehicle on the tile when not converting Rail <-> ElRail
			 * Tunnels and bridges have special check later */
			if (tt != MP_TUNNELBRIDGE) {
				if (!IsCompatibleRail(type, totype)) {
					CommandCost ret = EnsureNoVehicleOnGround(tile);
					if (ret.Failed()) {
						error = ret;
						continue;
					}
				}
				if (flags & DC_EXEC) { // we can safely convert, too
					TrackBits reserved = GetReservedTrackbits(tile);
					Track     track;
					while ((track = RemoveFirstTrack(&reserved)) != INVALID_TRACK) {
						Train *v = GetTrainForReservation(tile, track);
						if (v != NULL && !HasPowerOnRail(v->railtype, totype)) {
							/* No power on new rail type, reroute. */
							FreeTrainTrackReservation(v);
							*vehicles_affected.Append() = v;
						}
					}

					SetRailType(tile, totype);
					MarkTileDirtyByTile(tile);
					/* update power of train engines on this tile */
					FindVehicleOnPos(tile, NULL, &UpdateTrainPowerProc);
				}
			}

			switch (tt) {
				case MP_RAILWAY:
					switch (GetRailTileType(tile)) {
						case RAIL_TILE_DEPOT:
							if (flags & DC_EXEC) {
								/* notify YAPF about the track layout change */
								YapfNotifyTrackLayoutChange(tile, GetRailDepotTrack(tile));

								/* Update build vehicle window related to this depot */
								InvalidateWindowData(WC_VEHICLE_DEPOT, tile);
								InvalidateWindowData(WC_BUILD_VEHICLE, tile);
							}
							cost.AddCost(RailConvertCost(type, totype));
							break;

						default: // RAIL_TILE_NORMAL, RAIL_TILE_SIGNALS
							if (flags & DC_EXEC) {
								/* notify YAPF about the track layout change */
								TrackBits tracks = GetTrackBits(tile);
								while (tracks != TRACK_BIT_NONE) {
									YapfNotifyTrackLayoutChange(tile, RemoveFirstTrack(&tracks));
								}
							}
							cost.AddCost(RailConvertCost(type, totype) * CountBits(GetTrackBits(tile)));
							break;
					}
					break;

				case MP_TUNNELBRIDGE: {
					TileIndex endtile = GetOtherTunnelBridgeEnd(tile);

					/* If both ends of tunnel/bridge are in the range, do not try to convert twice -
					 * it would cause assert because of different test and exec runs */
					if (endtile < tile && TileX(endtile) >= sx && TileX(endtile) <= ex &&
							TileY(endtile) >= sy && TileY(endtile) <= ey) continue;

					/* When not coverting rail <-> el. rail, any vehicle cannot be in tunnel/bridge */
					if (!IsCompatibleRail(GetRailType(tile), totype)) {
						CommandCost ret = TunnelBridgeIsFree(tile, endtile);
						if (ret.Failed()) {
							error = ret;
							continue;
						}
					}

					if (flags & DC_EXEC) {
						Track track = DiagDirToDiagTrack(GetTunnelBridgeDirection(tile));
						if (HasTunnelBridgeReservation(tile)) {
							Train *v = GetTrainForReservation(tile, track);
							if (v != NULL && !HasPowerOnRail(v->railtype, totype)) {
								/* No power on new rail type, reroute. */
								FreeTrainTrackReservation(v);
								*vehicles_affected.Append() = v;
							}
						}
						SetRailType(tile, totype);
						SetRailType(endtile, totype);

						FindVehicleOnPos(tile, NULL, &UpdateTrainPowerProc);
						FindVehicleOnPos(endtile, NULL, &UpdateTrainPowerProc);

						YapfNotifyTrackLayoutChange(tile, track);
						YapfNotifyTrackLayoutChange(endtile, track);

						MarkTileDirtyByTile(tile);
						MarkTileDirtyByTile(endtile);

						if (IsBridge(tile)) {
							TileIndexDiff delta = TileOffsByDiagDir(GetTunnelBridgeDirection(tile));
							TileIndex t = tile + delta;
							for (; t != endtile; t += delta) MarkTileDirtyByTile(t); // TODO encapsulate this into a function
						}
					}

					cost.AddCost((GetTunnelBridgeLength(tile, endtile) + 2) * RailConvertCost(type, totype));
				} break;

				default: // MP_STATION, MP_ROAD
					if (flags & DC_EXEC) {
						Track track = ((tt == MP_STATION) ? GetRailStationTrack(tile) : GetCrossingRailTrack(tile));
						YapfNotifyTrackLayoutChange(tile, track);
					}

					cost.AddCost(RailConvertCost(type, totype));
					break;
			}

			for (uint i = 0; i < vehicles_affected.Length(); ++i) {
				TryPathReserve(vehicles_affected[i], true);
			}
		}
	}

	return (cost.GetCost() == 0) ? error : cost;
}

static CommandCost RemoveTrainDepot(TileIndex tile, DoCommandFlag flags)
{
	if (_current_company != OWNER_WATER) {
		CommandCost ret = CheckTileOwnership(tile);
		if (ret.Failed()) return ret;
	}

	CommandCost ret = EnsureNoVehicleOnGround(tile);
	if (ret.Failed()) return ret;

	if (flags & DC_EXEC) {
		/* read variables before the depot is removed */
		DiagDirection dir = GetRailDepotDirection(tile);
		Owner owner = GetTileOwner(tile);
		Train *v = NULL;

		if (HasDepotReservation(tile)) {
			v = GetTrainForReservation(tile, DiagDirToDiagTrack(dir));
			if (v != NULL) FreeTrainTrackReservation(v);
		}

		delete Depot::GetByTile(tile);
		DoClearSquare(tile);
		AddSideToSignalBuffer(tile, dir, owner);
		YapfNotifyTrackLayoutChange(tile, DiagDirToDiagTrack(dir));
		if (v != NULL) TryPathReserve(v, true);
	}

	return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_CLEAR_DEPOT_TRAIN]);
}

static CommandCost ClearTile_Track(TileIndex tile, DoCommandFlag flags)
{
	CommandCost cost(EXPENSES_CONSTRUCTION);

	if (flags & DC_AUTO) {
		if (!IsTileOwner(tile, _current_company)) {
			return_cmd_error(STR_ERROR_AREA_IS_OWNED_BY_ANOTHER);
		}

		if (IsPlainRail(tile)) {
			return_cmd_error(STR_ERROR_MUST_REMOVE_RAILROAD_TRACK);
		} else {
			return_cmd_error(STR_ERROR_BUILDING_MUST_BE_DEMOLISHED);
		}
	}

	switch (GetRailTileType(tile)) {
		case RAIL_TILE_SIGNALS:
		case RAIL_TILE_NORMAL: {
			Slope tileh = GetTileSlope(tile, NULL);
			/* Is there flat water on the lower halftile, that gets cleared expensively? */
			bool water_ground = (GetRailGroundType(tile) == RAIL_GROUND_WATER && IsSlopeWithOneCornerRaised(tileh));

			TrackBits tracks = GetTrackBits(tile);
			while (tracks != TRACK_BIT_NONE) {
				Track track = RemoveFirstTrack(&tracks);
				CommandCost ret = DoCommand(tile, 0, track, flags, CMD_REMOVE_SINGLE_RAIL);
				if (ret.Failed()) return ret;
				cost.AddCost(ret);
			}

			/* when bankrupting, don't make water dirty, there could be a ship on lower halftile */
			if (water_ground && !(flags & DC_BANKRUPT)) {
				CommandCost ret = EnsureNoVehicleOnGround(tile);
				if (ret.Failed()) return ret;

				/* The track was removed, and left a coast tile. Now also clear the water. */
				if (flags & DC_EXEC) DoClearSquare(tile);
				cost.AddCost(_price[PR_CLEAR_WATER]);
			}

			return cost;
		}

		case RAIL_TILE_DEPOT:
			return RemoveTrainDepot(tile, flags);

		default:
			return CMD_ERROR;
	}
}

/**
 * Get surface height in point (x,y)
 * On tiles with halftile foundations move (x,y) to a safe point wrt. track
 */
static uint GetSaveSlopeZ(uint x, uint y, Track track)
{
	switch (track) {
		case TRACK_UPPER: x &= ~0xF; y &= ~0xF; break;
		case TRACK_LOWER: x |=  0xF; y |=  0xF; break;
		case TRACK_LEFT:  x |=  0xF; y &= ~0xF; break;
		case TRACK_RIGHT: x &= ~0xF; y |=  0xF; break;
		default: break;
	}
	return GetSlopeZ(x, y);
}

static void DrawSingleSignal(TileIndex tile, Track track, byte condition, uint image, uint pos)
{
	bool side = (_settings_game.vehicle.road_side != 0) && _settings_game.construction.signal_side;
	static const Point SignalPositions[2][12] = {
		{ // Signals on the left side
		/*  LEFT      LEFT      RIGHT     RIGHT     UPPER     UPPER */
			{ 8,  5}, {14,  1}, { 1, 14}, { 9, 11}, { 1,  0}, { 3, 10},
		/*  LOWER     LOWER     X         X         Y         Y     */
			{11,  4}, {14, 14}, {11,  3}, { 4, 13}, { 3,  4}, {11, 13}
		}, { // Signals on the right side
		/*  LEFT      LEFT      RIGHT     RIGHT     UPPER     UPPER */
			{14,  1}, {12, 10}, { 4,  6}, { 1, 14}, {10,  4}, { 0,  1},
		/*  LOWER     LOWER     X         X         Y         Y     */
			{14, 14}, { 5, 12}, {11, 13}, { 4,  3}, {13,  4}, { 3, 11}
		}
	};

	uint x = TileX(tile) * TILE_SIZE + SignalPositions[side][pos].x;
	uint y = TileY(tile) * TILE_SIZE + SignalPositions[side][pos].y;

	SpriteID sprite;

	SignalType type       = GetSignalType(tile, track);
	SignalVariant variant = GetSignalVariant(tile, track);

	if (type == SIGTYPE_NORMAL && variant == SIG_ELECTRIC) {
		/* Normal electric signals are picked from original sprites. */
		sprite = SPR_ORIGINAL_SIGNALS_BASE + image + condition;
	} else {
		/* All other signals are picked from add on sprites. */
		sprite = SPR_SIGNALS_BASE + (type - 1) * 16 + variant * 64 + image + condition + (type > SIGTYPE_LAST_NOPBS ? 64 : 0);
	}

	AddSortableSpriteToDraw(sprite, PAL_NONE, x, y, 1, 1, BB_HEIGHT_UNDER_BRIDGE, GetSaveSlopeZ(x, y, track));
}

static uint32 _drawtile_track_palette;


static void DrawTrackFence_NW(const TileInfo *ti, SpriteID base_image)
{
	RailFenceOffset rfo = RFO_FLAT_X;
	if (ti->tileh != SLOPE_FLAT) rfo = (ti->tileh & SLOPE_S) ? RFO_SLOPE_SW : RFO_SLOPE_NE;
	AddSortableSpriteToDraw(base_image + rfo, _drawtile_track_palette,
		ti->x, ti->y + 1, 16, 1, 4, ti->z);
}

static void DrawTrackFence_SE(const TileInfo *ti, SpriteID base_image)
{
	RailFenceOffset rfo = RFO_FLAT_X;
	if (ti->tileh != SLOPE_FLAT) rfo = (ti->tileh & SLOPE_S) ? RFO_SLOPE_SW : RFO_SLOPE_NE;
	AddSortableSpriteToDraw(base_image + rfo, _drawtile_track_palette,
		ti->x, ti->y + TILE_SIZE - 1, 16, 1, 4, ti->z);
}

static void DrawTrackFence_NW_SE(const TileInfo *ti, SpriteID base_image)
{
	DrawTrackFence_NW(ti, base_image);
	DrawTrackFence_SE(ti, base_image);
}

static void DrawTrackFence_NE(const TileInfo *ti, SpriteID base_image)
{
	RailFenceOffset rfo = RFO_FLAT_Y;
	if (ti->tileh != SLOPE_FLAT) rfo = (ti->tileh & SLOPE_S) ? RFO_SLOPE_SE : RFO_SLOPE_NW;
	AddSortableSpriteToDraw(base_image + rfo, _drawtile_track_palette,
		ti->x + 1, ti->y, 1, 16, 4, ti->z);
}

static void DrawTrackFence_SW(const TileInfo *ti, SpriteID base_image)
{
	RailFenceOffset rfo = RFO_FLAT_Y;
	if (ti->tileh != SLOPE_FLAT) rfo = (ti->tileh & SLOPE_S) ? RFO_SLOPE_SE : RFO_SLOPE_NW;
	AddSortableSpriteToDraw(base_image + rfo, _drawtile_track_palette,
		ti->x + TILE_SIZE - 1, ti->y, 1, 16, 4, ti->z);
}

static void DrawTrackFence_NE_SW(const TileInfo *ti, SpriteID base_image)
{
	DrawTrackFence_NE(ti, base_image);
	DrawTrackFence_SW(ti, base_image);
}

/**
 * Draw fence at eastern side of track.
 */
static void DrawTrackFence_NS_1(const TileInfo *ti, SpriteID base_image)
{
	uint z = ti->z + GetSlopeZInCorner(RemoveHalftileSlope(ti->tileh), CORNER_W);
	AddSortableSpriteToDraw(base_image + RFO_FLAT_VERT, _drawtile_track_palette,
		ti->x + TILE_SIZE / 2, ti->y + TILE_SIZE / 2, 1, 1, 4, z);
}

/**
 * Draw fence at western side of track.
 */
static void DrawTrackFence_NS_2(const TileInfo *ti, SpriteID base_image)
{
	uint z = ti->z + GetSlopeZInCorner(RemoveHalftileSlope(ti->tileh), CORNER_E);
	AddSortableSpriteToDraw(base_image + RFO_FLAT_VERT, _drawtile_track_palette,
		ti->x + TILE_SIZE / 2, ti->y + TILE_SIZE / 2, 1, 1, 4, z);
}

/**
 * Draw fence at southern side of track.
 */
static void DrawTrackFence_WE_1(const TileInfo *ti, SpriteID base_image)
{
	uint z = ti->z + GetSlopeZInCorner(RemoveHalftileSlope(ti->tileh), CORNER_N);
	AddSortableSpriteToDraw(base_image + RFO_FLAT_HORZ, _drawtile_track_palette,
		ti->x + TILE_SIZE / 2, ti->y + TILE_SIZE / 2, 1, 1, 4, z);
}

/**
 * Draw fence at northern side of track.
 */
static void DrawTrackFence_WE_2(const TileInfo *ti, SpriteID base_image)
{
	uint z = ti->z + GetSlopeZInCorner(RemoveHalftileSlope(ti->tileh), CORNER_S);
	AddSortableSpriteToDraw(base_image + RFO_FLAT_HORZ, _drawtile_track_palette,
		ti->x + TILE_SIZE / 2, ti->y + TILE_SIZE / 2, 1, 1, 4, z);
}


static void DrawTrackDetails(const TileInfo *ti, const RailtypeInfo *rti)
{
	/* Base sprite for track fences. */
	SpriteID base_image = GetCustomRailSprite(rti, ti->tile, RTSG_FENCES);
	if (base_image == 0) base_image = SPR_TRACK_FENCE_FLAT_X;

	switch (GetRailGroundType(ti->tile)) {
		case RAIL_GROUND_FENCE_NW:     DrawTrackFence_NW(ti, base_image);    break;
		case RAIL_GROUND_FENCE_SE:     DrawTrackFence_SE(ti, base_image);    break;
		case RAIL_GROUND_FENCE_SENW:   DrawTrackFence_NW_SE(ti, base_image); break;
		case RAIL_GROUND_FENCE_NE:     DrawTrackFence_NE(ti, base_image);    break;
		case RAIL_GROUND_FENCE_SW:     DrawTrackFence_SW(ti, base_image);    break;
		case RAIL_GROUND_FENCE_NESW:   DrawTrackFence_NE_SW(ti, base_image); break;
		case RAIL_GROUND_FENCE_VERT1:  DrawTrackFence_NS_1(ti, base_image);  break;
		case RAIL_GROUND_FENCE_VERT2:  DrawTrackFence_NS_2(ti, base_image);  break;
		case RAIL_GROUND_FENCE_HORIZ1: DrawTrackFence_WE_1(ti, base_image);  break;
		case RAIL_GROUND_FENCE_HORIZ2: DrawTrackFence_WE_2(ti, base_image);  break;
		case RAIL_GROUND_WATER: {
			Corner track_corner;
			if (IsHalftileSlope(ti->tileh)) {
				/* Steep slope or one-corner-raised slope with halftile foundation */
				track_corner = GetHalftileSlopeCorner(ti->tileh);
			} else {
				/* Three-corner-raised slope */
				track_corner = OppositeCorner(GetHighestSlopeCorner(ComplementSlope(ti->tileh)));
			}
			switch (track_corner) {
				case CORNER_W: DrawTrackFence_NS_1(ti, base_image); break;
				case CORNER_S: DrawTrackFence_WE_2(ti, base_image); break;
				case CORNER_E: DrawTrackFence_NS_2(ti, base_image); break;
				case CORNER_N: DrawTrackFence_WE_1(ti, base_image); break;
				default: NOT_REACHED();
			}
			break;
		}
		default: break;
	}
}

/* SubSprite for drawing the track halftile of 'three-corners-raised'-sloped rail sprites. */
static const int INF = 1000; // big number compared to tilesprite size
static const SubSprite _halftile_sub_sprite[4] = {
	{ -INF    , -INF  , 32 - 33, INF     }, // CORNER_W, clip 33 pixels from right
	{ -INF    ,  0 + 7, INF    , INF     }, // CORNER_S, clip 7 pixels from top
	{ -31 + 33, -INF  , INF    , INF     }, // CORNER_E, clip 33 pixels from left
	{ -INF    , -INF  , INF    , 30 - 23 }  // CORNER_N, clip 23 pixels from bottom
};

static inline void DrawTrackSprite(SpriteID sprite, PaletteID pal, const TileInfo *ti, Slope s)
{
	DrawGroundSprite(sprite, pal, NULL, 0, (ti->tileh & s) ? -8 : 0);
}

static void DrawTrackBitsOverlay(TileInfo *ti, TrackBits track, const RailtypeInfo *rti)
{
	RailGroundType rgt = GetRailGroundType(ti->tile);
	Foundation f = GetRailFoundation(ti->tileh, track);
	Corner halftile_corner = CORNER_INVALID;

	if (IsNonContinuousFoundation(f)) {
		/* Save halftile corner */
		halftile_corner = (f == FOUNDATION_STEEP_BOTH ? GetHighestSlopeCorner(ti->tileh) : GetHalftileFoundationCorner(f));
		/* Draw lower part first */
		track &= ~CornerToTrackBits(halftile_corner);
		f = (f == FOUNDATION_STEEP_BOTH ? FOUNDATION_STEEP_LOWER : FOUNDATION_NONE);
	}

	DrawFoundation(ti, f);
	/* DrawFoundation modifies ti */

	/* Draw ground */
	if (track == TRACK_BIT_NONE && rgt == RAIL_GROUND_WATER) {
		if (IsSteepSlope(ti->tileh)) {
			DrawShoreTile(ti->tileh);
		} else {
			DrawGroundSprite(SPR_FLAT_WATER_TILE, PAL_NONE);
		}
	} else {
		SpriteID image;

		switch (rgt) {
			case RAIL_GROUND_BARREN:     image = SPR_FLAT_BARE_LAND;  break;
			case RAIL_GROUND_ICE_DESERT: image = SPR_FLAT_SNOW_DESERT_TILE; break;
			default:                     image = SPR_FLAT_GRASS_TILE; break;
		}

		image += _tileh_to_sprite[ti->tileh];

		DrawGroundSprite(image, PAL_NONE);
	}

	SpriteID overlay = GetCustomRailSprite(rti, ti->tile, RTSG_OVERLAY);
	SpriteID ground = GetCustomRailSprite(rti, ti->tile, RTSG_GROUND);
	TrackBits pbs = _settings_client.gui.show_track_reservation ? GetRailReservationTrackBits(ti->tile) : TRACK_BIT_NONE;

	if (track == TRACK_BIT_NONE) {
		/* Half-tile foundation, no track here? */
	} else if (ti->tileh == SLOPE_NW && track == TRACK_BIT_Y) {
		DrawGroundSprite(ground + RTO_SLOPE_NW, PAL_NONE);
		if (pbs != TRACK_BIT_NONE) DrawGroundSprite(overlay + 9, PALETTE_CRASH);
	} else if (ti->tileh == SLOPE_NE && track == TRACK_BIT_X) {
		DrawGroundSprite(ground + RTO_SLOPE_NE, PAL_NONE);
		if (pbs != TRACK_BIT_NONE) DrawGroundSprite(overlay + 6, PALETTE_CRASH);
	} else if (ti->tileh == SLOPE_SE && track == TRACK_BIT_Y) {
		DrawGroundSprite(ground + RTO_SLOPE_SE, PAL_NONE);
		if (pbs != TRACK_BIT_NONE) DrawGroundSprite(overlay + 7, PALETTE_CRASH);
	} else if (ti->tileh == SLOPE_SW && track == TRACK_BIT_X) {
		DrawGroundSprite(ground + RTO_SLOPE_SW, PAL_NONE);
		if (pbs != TRACK_BIT_NONE) DrawGroundSprite(overlay + 8, PALETTE_CRASH);
	} else {
		switch (track) {
			/* Draw single ground sprite when not overlapping. No track overlay
			 * is necessary for these sprites. */
			case TRACK_BIT_X:     DrawGroundSprite(ground + RTO_X, PAL_NONE); break;
			case TRACK_BIT_Y:     DrawGroundSprite(ground + RTO_Y, PAL_NONE); break;
			case TRACK_BIT_UPPER: DrawTrackSprite(ground + RTO_N, PAL_NONE, ti, SLOPE_N); break;
			case TRACK_BIT_LOWER: DrawTrackSprite(ground + RTO_S, PAL_NONE, ti, SLOPE_S); break;
			case TRACK_BIT_RIGHT: DrawTrackSprite(ground + RTO_E, PAL_NONE, ti, SLOPE_E); break;
			case TRACK_BIT_LEFT:  DrawTrackSprite(ground + RTO_W, PAL_NONE, ti, SLOPE_W); break;
			case TRACK_BIT_CROSS: DrawGroundSprite(ground + RTO_CROSSING_XY, PAL_NONE); break;
			case TRACK_BIT_HORZ:  DrawTrackSprite(ground + RTO_N, PAL_NONE, ti, SLOPE_N);
			                      DrawTrackSprite(ground + RTO_S, PAL_NONE, ti, SLOPE_S); break;
			case TRACK_BIT_VERT:  DrawTrackSprite(ground + RTO_E, PAL_NONE, ti, SLOPE_E);
			                      DrawTrackSprite(ground + RTO_W, PAL_NONE, ti, SLOPE_W); break;

			default:
				/* We're drawing a junction tile */
				if ((track & TRACK_BIT_3WAY_NE) == 0) {
					DrawGroundSprite(ground + RTO_JUNCTION_SW, PAL_NONE);
				} else if ((track & TRACK_BIT_3WAY_SW) == 0) {
					DrawGroundSprite(ground + RTO_JUNCTION_NE, PAL_NONE);
				} else if ((track & TRACK_BIT_3WAY_NW) == 0) {
					DrawGroundSprite(ground + RTO_JUNCTION_SE, PAL_NONE);
				} else if ((track & TRACK_BIT_3WAY_SE) == 0) {
					DrawGroundSprite(ground + RTO_JUNCTION_NW, PAL_NONE);
				} else {
					DrawGroundSprite(ground + RTO_JUNCTION_NSEW, PAL_NONE);
				}

				/* Mask out PBS bits as we shall draw them afterwards anyway. */
				track &= ~pbs;

				/* Draw regular track bits */
				if (track & TRACK_BIT_X)     DrawGroundSprite(overlay + RTO_X, PAL_NONE);
				if (track & TRACK_BIT_Y)     DrawGroundSprite(overlay + RTO_Y, PAL_NONE);
				if (track & TRACK_BIT_UPPER) DrawGroundSprite(overlay + RTO_N, PAL_NONE);
				if (track & TRACK_BIT_LOWER) DrawGroundSprite(overlay + RTO_S, PAL_NONE);
				if (track & TRACK_BIT_RIGHT) DrawGroundSprite(overlay + RTO_E, PAL_NONE);
				if (track & TRACK_BIT_LEFT)  DrawGroundSprite(overlay + RTO_W, PAL_NONE);
		}

		/* Draw reserved track bits */
		if (pbs & TRACK_BIT_X)     DrawGroundSprite(overlay + RTO_X, PALETTE_CRASH);
		if (pbs & TRACK_BIT_Y)     DrawGroundSprite(overlay + RTO_Y, PALETTE_CRASH);
		if (pbs & TRACK_BIT_UPPER) DrawTrackSprite(overlay + RTO_N, PALETTE_CRASH, ti, SLOPE_N);
		if (pbs & TRACK_BIT_LOWER) DrawTrackSprite(overlay + RTO_S, PALETTE_CRASH, ti, SLOPE_S);
		if (pbs & TRACK_BIT_RIGHT) DrawTrackSprite(overlay + RTO_E, PALETTE_CRASH, ti, SLOPE_E);
		if (pbs & TRACK_BIT_LEFT)  DrawTrackSprite(overlay + RTO_W, PALETTE_CRASH, ti, SLOPE_W);
	}

	if (IsValidCorner(halftile_corner)) {
		DrawFoundation(ti, HalftileFoundation(halftile_corner));

		/* Draw higher halftile-overlay: Use the sloped sprites with three corners raised. They probably best fit the lightning. */
		Slope fake_slope = SlopeWithThreeCornersRaised(OppositeCorner(halftile_corner));

		SpriteID image;
		switch (rgt) {
			case RAIL_GROUND_BARREN:     image = SPR_FLAT_BARE_LAND;  break;
			case RAIL_GROUND_ICE_DESERT:
			case RAIL_GROUND_HALF_SNOW:  image = SPR_FLAT_SNOW_DESERT_TILE; break;
			default:                     image = SPR_FLAT_GRASS_TILE; break;
		}

		image += _tileh_to_sprite[fake_slope];

		DrawGroundSprite(image, PAL_NONE, &(_halftile_sub_sprite[halftile_corner]));

		track = CornerToTrackBits(halftile_corner);

		int offset;
		switch (track) {
			default: NOT_REACHED();
			case TRACK_BIT_UPPER: offset = RTO_N; break;
			case TRACK_BIT_LOWER: offset = RTO_S; break;
			case TRACK_BIT_RIGHT: offset = RTO_E; break;
			case TRACK_BIT_LEFT:  offset = RTO_W; break;
		}

		DrawTrackSprite(ground + offset, PAL_NONE, ti, fake_slope);
		if (HasReservedTracks(ti->tile, track)) {
			DrawTrackSprite(overlay + offset, PALETTE_CRASH, ti, fake_slope);
		}
	}
}

/**
 * Draw ground sprite and track bits
 * @param ti TileInfo
 * @param track TrackBits to draw
 */
static void DrawTrackBits(TileInfo *ti, TrackBits track)
{
	const RailtypeInfo *rti = GetRailTypeInfo(GetRailType(ti->tile));

	if (rti->UsesOverlay()) {
		DrawTrackBitsOverlay(ti, track, rti);
		return;
	}

	RailGroundType rgt = GetRailGroundType(ti->tile);
	Foundation f = GetRailFoundation(ti->tileh, track);
	Corner halftile_corner = CORNER_INVALID;

	if (IsNonContinuousFoundation(f)) {
		/* Save halftile corner */
		halftile_corner = (f == FOUNDATION_STEEP_BOTH ? GetHighestSlopeCorner(ti->tileh) : GetHalftileFoundationCorner(f));
		/* Draw lower part first */
		track &= ~CornerToTrackBits(halftile_corner);
		f = (f == FOUNDATION_STEEP_BOTH ? FOUNDATION_STEEP_LOWER : FOUNDATION_NONE);
	}

	DrawFoundation(ti, f);
	/* DrawFoundation modifies ti */

	SpriteID image;
	PaletteID pal = PAL_NONE;
	const SubSprite *sub = NULL;
	bool junction = false;

	/* Select the sprite to use. */
	if (track == 0) {
		/* Clear ground (only track on halftile foundation) */
		if (rgt == RAIL_GROUND_WATER) {
			if (IsSteepSlope(ti->tileh)) {
				DrawShoreTile(ti->tileh);
				image = 0;
			} else {
				image = SPR_FLAT_WATER_TILE;
			}
		} else {
			switch (rgt) {
				case RAIL_GROUND_BARREN:     image = SPR_FLAT_BARE_LAND;  break;
				case RAIL_GROUND_ICE_DESERT: image = SPR_FLAT_SNOW_DESERT_TILE; break;
				default:                     image = SPR_FLAT_GRASS_TILE; break;
			}
			image += _tileh_to_sprite[ti->tileh];
		}
	} else {
		if (ti->tileh != SLOPE_FLAT) {
			/* track on non-flat ground */
			image = _track_sloped_sprites[ti->tileh - 1] + rti->base_sprites.track_y;
		} else {
			/* track on flat ground */
			(image = rti->base_sprites.track_y, track == TRACK_BIT_Y) ||
			(image++,                           track == TRACK_BIT_X) ||
			(image++,                           track == TRACK_BIT_UPPER) ||
			(image++,                           track == TRACK_BIT_LOWER) ||
			(image++,                           track == TRACK_BIT_RIGHT) ||
			(image++,                           track == TRACK_BIT_LEFT) ||
			(image++,                           track == TRACK_BIT_CROSS) ||

			(image = rti->base_sprites.track_ns, track == TRACK_BIT_HORZ) ||
			(image++,                            track == TRACK_BIT_VERT) ||

			(junction = true, false) ||
			(image = rti->base_sprites.ground, (track & TRACK_BIT_3WAY_NE) == 0) ||
			(image++,                          (track & TRACK_BIT_3WAY_SW) == 0) ||
			(image++,                          (track & TRACK_BIT_3WAY_NW) == 0) ||
			(image++,                          (track & TRACK_BIT_3WAY_SE) == 0) ||
			(image++, true);
		}

		switch (rgt) {
			case RAIL_GROUND_BARREN:     pal = PALETTE_TO_BARE_LAND; break;
			case RAIL_GROUND_ICE_DESERT: image += rti->snow_offset;  break;
			case RAIL_GROUND_WATER: {
				/* three-corner-raised slope */
				DrawShoreTile(ti->tileh);
				Corner track_corner = OppositeCorner(GetHighestSlopeCorner(ComplementSlope(ti->tileh)));
				sub = &(_halftile_sub_sprite[track_corner]);
				break;
			}
			default: break;
		}
	}

	if (image != 0) DrawGroundSprite(image, pal, sub);

	/* Draw track pieces individually for junction tiles */
	if (junction) {
		if (track & TRACK_BIT_X)     DrawGroundSprite(rti->base_sprites.single_x, PAL_NONE);
		if (track & TRACK_BIT_Y)     DrawGroundSprite(rti->base_sprites.single_y, PAL_NONE);
		if (track & TRACK_BIT_UPPER) DrawGroundSprite(rti->base_sprites.single_n, PAL_NONE);
		if (track & TRACK_BIT_LOWER) DrawGroundSprite(rti->base_sprites.single_s, PAL_NONE);
		if (track & TRACK_BIT_LEFT)  DrawGroundSprite(rti->base_sprites.single_w, PAL_NONE);
		if (track & TRACK_BIT_RIGHT) DrawGroundSprite(rti->base_sprites.single_e, PAL_NONE);
	}

	/* PBS debugging, draw reserved tracks darker */
	if (_game_mode != GM_MENU && _settings_client.gui.show_track_reservation) {
		/* Get reservation, but mask track on halftile slope */
		TrackBits pbs = GetRailReservationTrackBits(ti->tile) & track;
		if (pbs & TRACK_BIT_X) {
			if (ti->tileh == SLOPE_FLAT || ti->tileh == SLOPE_ELEVATED) {
				DrawGroundSprite(rti->base_sprites.single_x, PALETTE_CRASH);
			} else {
				DrawGroundSprite(_track_sloped_sprites[ti->tileh - 1] + rti->base_sprites.single_sloped - 20, PALETTE_CRASH);
			}
		}
		if (pbs & TRACK_BIT_Y) {
			if (ti->tileh == SLOPE_FLAT || ti->tileh == SLOPE_ELEVATED) {
				DrawGroundSprite(rti->base_sprites.single_y, PALETTE_CRASH);
			} else {
				DrawGroundSprite(_track_sloped_sprites[ti->tileh - 1] + rti->base_sprites.single_sloped - 20, PALETTE_CRASH);
			}
		}
		if (pbs & TRACK_BIT_UPPER) DrawGroundSprite(rti->base_sprites.single_n, PALETTE_CRASH, NULL, 0, ti->tileh & SLOPE_N ? -TILE_HEIGHT : 0);
		if (pbs & TRACK_BIT_LOWER) DrawGroundSprite(rti->base_sprites.single_s, PALETTE_CRASH, NULL, 0, ti->tileh & SLOPE_S ? -TILE_HEIGHT : 0);
		if (pbs & TRACK_BIT_LEFT)  DrawGroundSprite(rti->base_sprites.single_w, PALETTE_CRASH, NULL, 0, ti->tileh & SLOPE_W ? -TILE_HEIGHT : 0);
		if (pbs & TRACK_BIT_RIGHT) DrawGroundSprite(rti->base_sprites.single_e, PALETTE_CRASH, NULL, 0, ti->tileh & SLOPE_E ? -TILE_HEIGHT : 0);
	}

	if (IsValidCorner(halftile_corner)) {
		DrawFoundation(ti, HalftileFoundation(halftile_corner));

		/* Draw higher halftile-overlay: Use the sloped sprites with three corners raised. They probably best fit the lightning. */
		Slope fake_slope = SlopeWithThreeCornersRaised(OppositeCorner(halftile_corner));
		image = _track_sloped_sprites[fake_slope - 1] + rti->base_sprites.track_y;
		pal = PAL_NONE;
		switch (rgt) {
			case RAIL_GROUND_BARREN:     pal = PALETTE_TO_BARE_LAND; break;
			case RAIL_GROUND_ICE_DESERT:
			case RAIL_GROUND_HALF_SNOW:  image += rti->snow_offset;  break; // higher part has snow in this case too
			default: break;
		}
		DrawGroundSprite(image, pal, &(_halftile_sub_sprite[halftile_corner]));

		if (_game_mode != GM_MENU && _settings_client.gui.show_track_reservation && HasReservedTracks(ti->tile, CornerToTrackBits(halftile_corner))) {
			static const byte _corner_to_track_sprite[] = {3, 1, 2, 0};
			DrawGroundSprite(_corner_to_track_sprite[halftile_corner] + rti->base_sprites.single_n, PALETTE_CRASH, NULL, 0, -TILE_HEIGHT);
		}
	}
}

/** Enums holding the offsets from base signal sprite,
 * according to the side it is representing.
 * The addtion of 2 per enum is necessary in order to "jump" over the
 * green state sprite, all signal sprites being in pair,
 * starting with the off-red state */
enum {
	SIGNAL_TO_SOUTHWEST =  0,
	SIGNAL_TO_NORTHEAST =  2,
	SIGNAL_TO_SOUTHEAST =  4,
	SIGNAL_TO_NORTHWEST =  6,
	SIGNAL_TO_EAST      =  8,
	SIGNAL_TO_WEST      = 10,
	SIGNAL_TO_SOUTH     = 12,
	SIGNAL_TO_NORTH     = 14,
};

static void DrawSignals(TileIndex tile, TrackBits rails)
{
#define MAYBE_DRAW_SIGNAL(x, y, z, t) if (IsSignalPresent(tile, x)) DrawSingleSignal(tile, t, GetSingleSignalState(tile, x), y, z)

	if (!(rails & TRACK_BIT_Y)) {
		if (!(rails & TRACK_BIT_X)) {
			if (rails & TRACK_BIT_LEFT) {
				MAYBE_DRAW_SIGNAL(2, SIGNAL_TO_NORTH, 0, TRACK_LEFT);
				MAYBE_DRAW_SIGNAL(3, SIGNAL_TO_SOUTH, 1, TRACK_LEFT);
			}
			if (rails & TRACK_BIT_RIGHT) {
				MAYBE_DRAW_SIGNAL(0, SIGNAL_TO_NORTH, 2, TRACK_RIGHT);
				MAYBE_DRAW_SIGNAL(1, SIGNAL_TO_SOUTH, 3, TRACK_RIGHT);
			}
			if (rails & TRACK_BIT_UPPER) {
				MAYBE_DRAW_SIGNAL(3, SIGNAL_TO_WEST, 4, TRACK_UPPER);
				MAYBE_DRAW_SIGNAL(2, SIGNAL_TO_EAST, 5, TRACK_UPPER);
			}
			if (rails & TRACK_BIT_LOWER) {
				MAYBE_DRAW_SIGNAL(1, SIGNAL_TO_WEST, 6, TRACK_LOWER);
				MAYBE_DRAW_SIGNAL(0, SIGNAL_TO_EAST, 7, TRACK_LOWER);
			}
		} else {
			MAYBE_DRAW_SIGNAL(3, SIGNAL_TO_SOUTHWEST, 8, TRACK_X);
			MAYBE_DRAW_SIGNAL(2, SIGNAL_TO_NORTHEAST, 9, TRACK_X);
		}
	} else {
		MAYBE_DRAW_SIGNAL(3, SIGNAL_TO_SOUTHEAST, 10, TRACK_Y);
		MAYBE_DRAW_SIGNAL(2, SIGNAL_TO_NORTHWEST, 11, TRACK_Y);
	}
}

static void DrawTile_Track(TileInfo *ti)
{
	const RailtypeInfo *rti = GetRailTypeInfo(GetRailType(ti->tile));

	_drawtile_track_palette = COMPANY_SPRITE_COLOUR(GetTileOwner(ti->tile));

	if (IsPlainRail(ti->tile)) {
		TrackBits rails = GetTrackBits(ti->tile);

		DrawTrackBits(ti, rails);

		if (HasBit(_display_opt, DO_FULL_DETAIL)) DrawTrackDetails(ti, rti);

		if (HasCatenaryDrawn(GetRailType(ti->tile))) DrawCatenary(ti);

		if (HasSignals(ti->tile)) DrawSignals(ti->tile, rails);
	} else {
		/* draw depot */
		const DrawTileSprites *dts;
		PaletteID pal = PAL_NONE;
		SpriteID relocation;

		if (ti->tileh != SLOPE_FLAT) DrawFoundation(ti, FOUNDATION_LEVELED);

		if (IsInvisibilitySet(TO_BUILDINGS)) {
			/* Draw rail instead of depot */
			dts = &_depot_invisible_gfx_table[GetRailDepotDirection(ti->tile)];
		} else {
			dts = &_depot_gfx_table[GetRailDepotDirection(ti->tile)];
		}

		SpriteID image;
		if (rti->UsesOverlay()) {
			image = SPR_FLAT_GRASS_TILE;
		} else {
			image = dts->ground.sprite;
			if (image != SPR_FLAT_GRASS_TILE) image += rti->total_offset;
		}

		/* adjust ground tile for desert
		 * don't adjust for snow, because snow in depots looks weird */
		if (IsSnowRailGround(ti->tile) && _settings_game.game_creation.landscape == LT_TROPIC) {
			if (image != SPR_FLAT_GRASS_TILE) {
				image += rti->snow_offset; // tile with tracks
			} else {
				image = SPR_FLAT_SNOW_DESERT_TILE; // flat ground
			}
		}

		DrawGroundSprite(image, GroundSpritePaletteTransform(image, pal, _drawtile_track_palette));

		if (rti->UsesOverlay()) {
			SpriteID ground = GetCustomRailSprite(rti, ti->tile, RTSG_GROUND);

			switch (GetRailDepotDirection(ti->tile)) {
				case DIAGDIR_NE: if (!IsInvisibilitySet(TO_BUILDINGS)) break; // else FALL THROUGH
				case DIAGDIR_SW: DrawGroundSprite(ground + RTO_X, PAL_NONE); break;
				case DIAGDIR_NW: if (!IsInvisibilitySet(TO_BUILDINGS)) break; // else FALL THROUGH
				case DIAGDIR_SE: DrawGroundSprite(ground + RTO_Y, PAL_NONE); break;
				default: break;
			}

			if (_settings_client.gui.show_track_reservation && HasDepotReservation(ti->tile)) {
				SpriteID overlay = GetCustomRailSprite(rti, ti->tile, RTSG_OVERLAY);

				switch (GetRailDepotDirection(ti->tile)) {
					case DIAGDIR_NE: if (!IsInvisibilitySet(TO_BUILDINGS)) break; // else FALL THROUGH
					case DIAGDIR_SW: DrawGroundSprite(overlay + RTO_X, PALETTE_CRASH); break;
					case DIAGDIR_NW: if (!IsInvisibilitySet(TO_BUILDINGS)) break; // else FALL THROUGH
					case DIAGDIR_SE: DrawGroundSprite(overlay + RTO_Y, PALETTE_CRASH); break;
					default: break;
				}
			}

			relocation  = GetCustomRailSprite(rti, ti->tile, RTSG_DEPOT);
			relocation -= SPR_RAIL_DEPOT_SE_1;
		} else {
			/* PBS debugging, draw reserved tracks darker */
			if (_game_mode != GM_MENU && _settings_client.gui.show_track_reservation && HasDepotReservation(ti->tile)) {
				switch (GetRailDepotDirection(ti->tile)) {
					case DIAGDIR_NE: if (!IsInvisibilitySet(TO_BUILDINGS)) break; // else FALL THROUGH
					case DIAGDIR_SW: DrawGroundSprite(rti->base_sprites.single_x, PALETTE_CRASH); break;
					case DIAGDIR_NW: if (!IsInvisibilitySet(TO_BUILDINGS)) break; // else FALL THROUGH
					case DIAGDIR_SE: DrawGroundSprite(rti->base_sprites.single_y, PALETTE_CRASH); break;
					default: break;
				}
			}

			relocation = rti->total_offset;
		}

		if (HasCatenaryDrawn(GetRailType(ti->tile))) DrawCatenary(ti);

		DrawRailTileSeq(ti, dts, TO_BUILDINGS, relocation, 0, _drawtile_track_palette);
	}
	DrawBridgeMiddle(ti);
}

void DrawTrainDepotSprite(int x, int y, int dir, RailType railtype)
{
	const DrawTileSprites *dts = &_depot_gfx_table[dir];
	const RailtypeInfo *rti = GetRailTypeInfo(railtype);
	SpriteID image = rti->UsesOverlay() ? SPR_FLAT_GRASS_TILE : dts->ground.sprite;
	uint32 offset = rti->total_offset;

	x += 33;
	y += 17;

	if (image != SPR_FLAT_GRASS_TILE) image += offset;
	PaletteID palette = COMPANY_SPRITE_COLOUR(_local_company);

	DrawSprite(image, PAL_NONE, x, y);

	if (rti->UsesOverlay()) {
		SpriteID ground = GetCustomRailSprite(rti, INVALID_TILE, RTSG_GROUND);

		switch (dir) {
			case DIAGDIR_SW: DrawSprite(ground + RTO_X, PAL_NONE, x, y); break;
			case DIAGDIR_SE: DrawSprite(ground + RTO_Y, PAL_NONE, x, y); break;
			default: break;
		}

		offset  = GetCustomRailSprite(rti, INVALID_TILE, RTSG_DEPOT);
		offset -= SPR_RAIL_DEPOT_SE_1;
	}

	DrawRailTileSeqInGUI(x, y, dts, offset, 0, palette);
}

static uint GetSlopeZ_Track(TileIndex tile, uint x, uint y)
{
	uint z;
	Slope tileh = GetTileSlope(tile, &z);

	if (tileh == SLOPE_FLAT) return z;
	if (IsPlainRail(tile)) {
		z += ApplyFoundationToSlope(GetRailFoundation(tileh, GetTrackBits(tile)), &tileh);
		return z + GetPartialZ(x & 0xF, y & 0xF, tileh);
	} else {
		return z + TILE_HEIGHT;
	}
}

static Foundation GetFoundation_Track(TileIndex tile, Slope tileh)
{
	return IsPlainRail(tile) ? GetRailFoundation(tileh, GetTrackBits(tile)) : FlatteningFoundation(tileh);
}

static void TileLoop_Track(TileIndex tile)
{
	RailGroundType old_ground = GetRailGroundType(tile);
	RailGroundType new_ground;

	if (old_ground == RAIL_GROUND_WATER) {
		TileLoop_Water(tile);
		return;
	}

	switch (_settings_game.game_creation.landscape) {
		case LT_ARCTIC: {
			uint z;
			Slope slope = GetTileSlope(tile, &z);
			bool half = false;

			/* for non-flat track, use lower part of track
			 * in other cases, use the highest part with track */
			if (IsPlainRail(tile)) {
				TrackBits track = GetTrackBits(tile);
				Foundation f = GetRailFoundation(slope, track);

				switch (f) {
					case FOUNDATION_NONE:
						/* no foundation - is the track on the upper side of three corners raised tile? */
						if (IsSlopeWithThreeCornersRaised(slope)) z += TILE_HEIGHT;
						break;

					case FOUNDATION_INCLINED_X:
					case FOUNDATION_INCLINED_Y:
						/* sloped track - is it on a steep slope? */
						if (IsSteepSlope(slope)) z += TILE_HEIGHT;
						break;

					case FOUNDATION_STEEP_LOWER:
						/* only lower part of steep slope */
						z += TILE_HEIGHT;
						break;

					default:
						/* if it is a steep slope, then there is a track on higher part */
						if (IsSteepSlope(slope)) z += TILE_HEIGHT;
						z += TILE_HEIGHT;
						break;
				}

				half = IsInsideMM(f, FOUNDATION_STEEP_BOTH, FOUNDATION_HALFTILE_N + 1);
			} else {
				/* is the depot on a non-flat tile? */
				if (slope != SLOPE_FLAT) z += TILE_HEIGHT;
			}

			/* 'z' is now the lowest part of the highest track bit -
			 * for sloped track, it is 'z' of lower part
			 * for two track bits, it is 'z' of higher track bit
			 * For non-continuous foundations (and STEEP_BOTH), 'half' is set */
			if (z > GetSnowLine()) {
				if (half && z - GetSnowLine() == TILE_HEIGHT) {
					/* track on non-continuous foundation, lower part is not under snow */
					new_ground = RAIL_GROUND_HALF_SNOW;
				} else {
					new_ground = RAIL_GROUND_ICE_DESERT;
				}
				goto set_ground;
			}
			break;
			}

		case LT_TROPIC:
			if (GetTropicZone(tile) == TROPICZONE_DESERT) {
				new_ground = RAIL_GROUND_ICE_DESERT;
				goto set_ground;
			}
			break;
	}

	if (!IsPlainRail(tile)) return;

	new_ground = RAIL_GROUND_GRASS;

	if (old_ground != RAIL_GROUND_BARREN) { // wait until bottom is green
		/* determine direction of fence */
		TrackBits rail = GetTrackBits(tile);

		switch (rail) {
			case TRACK_BIT_UPPER: new_ground = RAIL_GROUND_FENCE_HORIZ1; break;
			case TRACK_BIT_LOWER: new_ground = RAIL_GROUND_FENCE_HORIZ2; break;
			case TRACK_BIT_LEFT:  new_ground = RAIL_GROUND_FENCE_VERT1;  break;
			case TRACK_BIT_RIGHT: new_ground = RAIL_GROUND_FENCE_VERT2;  break;

			default: {
				Owner owner = GetTileOwner(tile);

				if (rail == (TRACK_BIT_LOWER | TRACK_BIT_RIGHT) || (
							(rail & TRACK_BIT_3WAY_NW) == 0 &&
							(rail & TRACK_BIT_X)
						)) {
					TileIndex n = tile + TileDiffXY(0, -1);
					TrackBits nrail = (IsPlainRailTile(n) ? GetTrackBits(n) : TRACK_BIT_NONE);

					if (!IsTileType(n, MP_RAILWAY) ||
							!IsTileOwner(n, owner) ||
							nrail == TRACK_BIT_UPPER ||
							nrail == TRACK_BIT_LEFT) {
						new_ground = RAIL_GROUND_FENCE_NW;
					}
				}

				if (rail == (TRACK_BIT_UPPER | TRACK_BIT_LEFT) || (
							(rail & TRACK_BIT_3WAY_SE) == 0 &&
							(rail & TRACK_BIT_X)
						)) {
					TileIndex n = tile + TileDiffXY(0, 1);
					TrackBits nrail = (IsPlainRailTile(n) ? GetTrackBits(n) : TRACK_BIT_NONE);

					if (!IsTileType(n, MP_RAILWAY) ||
							!IsTileOwner(n, owner) ||
							nrail == TRACK_BIT_LOWER ||
							nrail == TRACK_BIT_RIGHT) {
						new_ground = (new_ground == RAIL_GROUND_FENCE_NW) ?
							RAIL_GROUND_FENCE_SENW : RAIL_GROUND_FENCE_SE;
					}
				}

				if (rail == (TRACK_BIT_LOWER | TRACK_BIT_LEFT) || (
							(rail & TRACK_BIT_3WAY_NE) == 0 &&
							(rail & TRACK_BIT_Y)
						)) {
					TileIndex n = tile + TileDiffXY(-1, 0);
					TrackBits nrail = (IsPlainRailTile(n) ? GetTrackBits(n) : TRACK_BIT_NONE);

					if (!IsTileType(n, MP_RAILWAY) ||
							!IsTileOwner(n, owner) ||
							nrail == TRACK_BIT_UPPER ||
							nrail == TRACK_BIT_RIGHT) {
						new_ground = RAIL_GROUND_FENCE_NE;
					}
				}

				if (rail == (TRACK_BIT_UPPER | TRACK_BIT_RIGHT) || (
							(rail & TRACK_BIT_3WAY_SW) == 0 &&
							(rail & TRACK_BIT_Y)
						)) {
					TileIndex n = tile + TileDiffXY(1, 0);
					TrackBits nrail = (IsPlainRailTile(n) ? GetTrackBits(n) : TRACK_BIT_NONE);

					if (!IsTileType(n, MP_RAILWAY) ||
							!IsTileOwner(n, owner) ||
							nrail == TRACK_BIT_LOWER ||
							nrail == TRACK_BIT_LEFT) {
						new_ground = (new_ground == RAIL_GROUND_FENCE_NE) ?
							RAIL_GROUND_FENCE_NESW : RAIL_GROUND_FENCE_SW;
					}
				}
				break;
			}
		}
	}

set_ground:
	if (old_ground != new_ground) {
		SetRailGroundType(tile, new_ground);
		MarkTileDirtyByTile(tile);
	}
}


static TrackStatus GetTileTrackStatus_Track(TileIndex tile, TransportType mode, uint sub_mode, DiagDirection side)
{
	/* Case of half tile slope with water. */
	if (mode == TRANSPORT_WATER && IsPlainRail(tile) && GetRailGroundType(tile) == RAIL_GROUND_WATER) {
		TrackBits tb = GetTrackBits(tile);
		switch (tb) {
			default: NOT_REACHED();
			case TRACK_BIT_UPPER: tb = TRACK_BIT_LOWER; break;
			case TRACK_BIT_LOWER: tb = TRACK_BIT_UPPER; break;
			case TRACK_BIT_LEFT:  tb = TRACK_BIT_RIGHT; break;
			case TRACK_BIT_RIGHT: tb = TRACK_BIT_LEFT;  break;
		}
		return CombineTrackStatus(TrackBitsToTrackdirBits(tb), TRACKDIR_BIT_NONE);
	}

	if (mode != TRANSPORT_RAIL) return 0;

	TrackBits trackbits = TRACK_BIT_NONE;
	TrackdirBits red_signals = TRACKDIR_BIT_NONE;

	switch (GetRailTileType(tile)) {
		default: NOT_REACHED();
		case RAIL_TILE_NORMAL:
			trackbits = GetTrackBits(tile);
			break;

		case RAIL_TILE_SIGNALS: {
			trackbits = GetTrackBits(tile);
			byte a = GetPresentSignals(tile);
			uint b = GetSignalStates(tile);

			b &= a;

			/* When signals are not present (in neither direction),
			 * we pretend them to be green. Otherwise, it depends on
			 * the signal type. For signals that are only active from
			 * one side, we set the missing signals explicitely to
			 * `green'. Otherwise, they implicitely become `red'. */
			if (!IsOnewaySignal(tile, TRACK_UPPER) || (a & SignalOnTrack(TRACK_UPPER)) == 0) b |= ~a & SignalOnTrack(TRACK_UPPER);
			if (!IsOnewaySignal(tile, TRACK_LOWER) || (a & SignalOnTrack(TRACK_LOWER)) == 0) b |= ~a & SignalOnTrack(TRACK_LOWER);

			if ((b & 0x8) == 0) red_signals |= (TRACKDIR_BIT_LEFT_N | TRACKDIR_BIT_X_NE | TRACKDIR_BIT_Y_SE | TRACKDIR_BIT_UPPER_E);
			if ((b & 0x4) == 0) red_signals |= (TRACKDIR_BIT_LEFT_S | TRACKDIR_BIT_X_SW | TRACKDIR_BIT_Y_NW | TRACKDIR_BIT_UPPER_W);
			if ((b & 0x2) == 0) red_signals |= (TRACKDIR_BIT_RIGHT_N | TRACKDIR_BIT_LOWER_E);
			if ((b & 0x1) == 0) red_signals |= (TRACKDIR_BIT_RIGHT_S | TRACKDIR_BIT_LOWER_W);

			break;
		}

		case RAIL_TILE_DEPOT: {
			DiagDirection dir = GetRailDepotDirection(tile);

			if (side != INVALID_DIAGDIR && side != dir) break;

			trackbits = DiagDirToDiagTrackBits(dir);
			break;
		}
	}

	return CombineTrackStatus(TrackBitsToTrackdirBits(trackbits), red_signals);
}

static bool ClickTile_Track(TileIndex tile)
{
	if (!IsRailDepot(tile)) return false;

	ShowDepotWindow(tile, VEH_TRAIN);
	return true;
}

static void GetTileDesc_Track(TileIndex tile, TileDesc *td)
{
	const RailtypeInfo *rti = GetRailTypeInfo(GetRailType(tile));
	td->rail_speed = rti->max_speed;
	td->owner[0] = GetTileOwner(tile);
	switch (GetRailTileType(tile)) {
		case RAIL_TILE_NORMAL:
			td->str = STR_LAI_RAIL_DESCRIPTION_TRACK;
			break;

		case RAIL_TILE_SIGNALS: {
			static const StringID signal_type[6][6] = {
				{
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_NORMAL_SIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_NORMAL_PRESIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_NORMAL_EXITSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_NORMAL_COMBOSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_NORMAL_PBSSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_NORMAL_NOENTRYSIGNALS
				},
				{
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_NORMAL_PRESIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_PRESIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_PRE_EXITSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_PRE_COMBOSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_PRE_PBSSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_PRE_NOENTRYSIGNALS
				},
				{
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_NORMAL_EXITSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_PRE_EXITSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_EXITSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_EXIT_COMBOSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_EXIT_PBSSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_EXIT_NOENTRYSIGNALS
				},
				{
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_NORMAL_COMBOSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_PRE_COMBOSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_EXIT_COMBOSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_COMBOSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_COMBO_PBSSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_COMBO_NOENTRYSIGNALS
				},
				{
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_NORMAL_PBSSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_PRE_PBSSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_EXIT_PBSSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_COMBO_PBSSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_PBSSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_PBS_NOENTRYSIGNALS
				},
				{
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_NORMAL_NOENTRYSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_PRE_NOENTRYSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_EXIT_NOENTRYSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_COMBO_NOENTRYSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_PBS_NOENTRYSIGNALS,
					STR_LAI_RAIL_DESCRIPTION_TRACK_WITH_NOENTRYSIGNALS
				}
			};

			SignalType primary_signal;
			SignalType secondary_signal;
			if (HasSignalOnTrack(tile, TRACK_UPPER)) {
				primary_signal = GetSignalType(tile, TRACK_UPPER);
				secondary_signal = HasSignalOnTrack(tile, TRACK_LOWER) ? GetSignalType(tile, TRACK_LOWER) : primary_signal;
			} else {
				secondary_signal = primary_signal = GetSignalType(tile, TRACK_LOWER);
			}

			td->str = signal_type[secondary_signal][primary_signal];
			break;
		}

		case RAIL_TILE_DEPOT:
			td->str = STR_LAI_RAIL_DESCRIPTION_TRAIN_DEPOT;
			if (_settings_game.vehicle.train_acceleration_model != AM_ORIGINAL) {
				if (td->rail_speed > 0) {
					td->rail_speed = min(td->rail_speed, 61);
				} else {
					td->rail_speed = 61;
				}
			}
			break;

		default:
			NOT_REACHED();
	}
}

static void ChangeTileOwner_Track(TileIndex tile, Owner old_owner, Owner new_owner)
{
	if (!IsTileOwner(tile, old_owner)) return;

	if (new_owner != INVALID_OWNER) {
		SetTileOwner(tile, new_owner);
	} else {
		DoCommand(tile, 0, 0, DC_EXEC | DC_BANKRUPT, CMD_LANDSCAPE_CLEAR);
	}
}

static const byte _fractcoords_behind[4] = { 0x8F, 0x8, 0x80, 0xF8 };
static const byte _fractcoords_enter[4] = { 0x8A, 0x48, 0x84, 0xA8 };
static const int8 _deltacoord_leaveoffset[8] = {
	-1,  0,  1,  0, /* x */
	 0,  1,  0, -1  /* y */
};


/** Compute number of ticks when next wagon will leave a depot.
 * Negative means next wagon should have left depot n ticks before.
 * @param v vehicle outside (leaving) the depot
 * @return number of ticks when the next wagon will leave
 */
int TicksToLeaveDepot(const Train *v)
{
	DiagDirection dir = GetRailDepotDirection(v->tile);
	int length = v->tcache.cached_veh_length;

	switch (dir) {
		case DIAGDIR_NE: return  ((int)(v->x_pos & 0x0F) - ((_fractcoords_enter[dir] & 0x0F) - (length + 1)));
		case DIAGDIR_SE: return -((int)(v->y_pos & 0x0F) - ((_fractcoords_enter[dir] >> 4)   + (length + 1)));
		case DIAGDIR_SW: return -((int)(v->x_pos & 0x0F) - ((_fractcoords_enter[dir] & 0x0F) + (length + 1)));
		default:
		case DIAGDIR_NW: return  ((int)(v->y_pos & 0x0F) - ((_fractcoords_enter[dir] >> 4)   - (length + 1)));
	}

	return 0; // make compilers happy
}

/** Tile callback routine when vehicle enters tile
 * @see vehicle_enter_tile_proc */
static VehicleEnterTileStatus VehicleEnter_Track(Vehicle *u, TileIndex tile, int x, int y)
{
	/* this routine applies only to trains in depot tiles */
	if (u->type != VEH_TRAIN || !IsRailDepotTile(tile)) return VETSB_CONTINUE;

	Train *v = Train::From(u);

	/* depot direction */
	DiagDirection dir = GetRailDepotDirection(tile);

	/* calculate the point where the following wagon should be activated
	 * this depends on the length of the current vehicle */
	int length = v->tcache.cached_veh_length;

	byte fract_coord_leave =
		((_fractcoords_enter[dir] & 0x0F) + // x
			(length + 1) * _deltacoord_leaveoffset[dir]) +
		(((_fractcoords_enter[dir] >> 4) +  // y
			((length + 1) * _deltacoord_leaveoffset[dir + 4])) << 4);

	byte fract_coord = (x & 0xF) + ((y & 0xF) << 4);

	if (_fractcoords_behind[dir] == fract_coord) {
		/* make sure a train is not entering the tile from behind */
		return VETSB_CANNOT_ENTER;
	} else if (_fractcoords_enter[dir] == fract_coord) {
		if (DiagDirToDir(ReverseDiagDir(dir)) == v->direction) {
			/* enter the depot */
			v->track = TRACK_BIT_DEPOT,
			v->vehstatus |= VS_HIDDEN; // hide it
			v->direction = ReverseDir(v->direction);
			if (v->Next() == NULL) VehicleEnterDepot(v->First());
			v->tile = tile;

			InvalidateWindowData(WC_VEHICLE_DEPOT, v->tile);
			return VETSB_ENTERED_WORMHOLE;
		}
	} else if (fract_coord_leave == fract_coord) {
		if (DiagDirToDir(dir) == v->direction) {
			/* leave the depot? */
			if ((v = v->Next()) != NULL) {
				v->vehstatus &= ~VS_HIDDEN;
				v->track = (DiagDirToAxis(dir) == AXIS_X ? TRACK_BIT_X : TRACK_BIT_Y);
			}
		}
	}

	return VETSB_CONTINUE;
}

/**
 * Tests if autoslope is allowed.
 *
 * @param tile The tile.
 * @param flags Terraform command flags.
 * @param z_old Old TileZ.
 * @param tileh_old Old TileSlope.
 * @param z_new New TileZ.
 * @param tileh_new New TileSlope.
 * @param rail_bits Trackbits.
 */
static CommandCost TestAutoslopeOnRailTile(TileIndex tile, uint flags, uint z_old, Slope tileh_old, uint z_new, Slope tileh_new, TrackBits rail_bits)
{
	if (!_settings_game.construction.build_on_slopes || !AutoslopeEnabled()) return_cmd_error(STR_ERROR_MUST_REMOVE_RAILROAD_TRACK);

	/* Is the slope-rail_bits combination valid in general? I.e. is it safe to call GetRailFoundation() ? */
	if (CheckRailSlope(tileh_new, rail_bits, TRACK_BIT_NONE, tile).Failed()) return_cmd_error(STR_ERROR_MUST_REMOVE_RAILROAD_TRACK);

	/* Get the slopes on top of the foundations */
	z_old += ApplyFoundationToSlope(GetRailFoundation(tileh_old, rail_bits), &tileh_old);
	z_new += ApplyFoundationToSlope(GetRailFoundation(tileh_new, rail_bits), &tileh_new);

	Corner track_corner;
	switch (rail_bits) {
		case TRACK_BIT_LEFT:  track_corner = CORNER_W; break;
		case TRACK_BIT_LOWER: track_corner = CORNER_S; break;
		case TRACK_BIT_RIGHT: track_corner = CORNER_E; break;
		case TRACK_BIT_UPPER: track_corner = CORNER_N; break;

		/* Surface slope must not be changed */
		default:
			if (z_old != z_new || tileh_old != tileh_new) return_cmd_error(STR_ERROR_MUST_REMOVE_RAILROAD_TRACK);
			return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_FOUNDATION]);
	}

	/* The height of the track_corner must not be changed. The rest ensures GetRailFoundation() already. */
	z_old += GetSlopeZInCorner(RemoveHalftileSlope(tileh_old), track_corner);
	z_new += GetSlopeZInCorner(RemoveHalftileSlope(tileh_new), track_corner);
	if (z_old != z_new) return_cmd_error(STR_ERROR_MUST_REMOVE_RAILROAD_TRACK);

	CommandCost cost = CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_FOUNDATION]);
	/* Make the ground dirty, if surface slope has changed */
	if (tileh_old != tileh_new) {
		/* If there is flat water on the lower halftile add the cost for clearing it */
		if (GetRailGroundType(tile) == RAIL_GROUND_WATER && IsSlopeWithOneCornerRaised(tileh_old)) cost.AddCost(_price[PR_CLEAR_WATER]);
		if ((flags & DC_EXEC) != 0) SetRailGroundType(tile, RAIL_GROUND_BARREN);
	}
	return  cost;
}

static CommandCost TerraformTile_Track(TileIndex tile, DoCommandFlag flags, uint z_new, Slope tileh_new)
{
	uint z_old;
	Slope tileh_old = GetTileSlope(tile, &z_old);
	if (IsPlainRail(tile)) {
		TrackBits rail_bits = GetTrackBits(tile);
		/* Is there flat water on the lower halftile, that must be cleared expensively? */
		bool was_water = (GetRailGroundType(tile) == RAIL_GROUND_WATER && IsSlopeWithOneCornerRaised(tileh_old));

		/* First test autoslope. However if it succeeds we still have to test the rest, because non-autoslope terraforming is cheaper. */
		CommandCost autoslope_result = TestAutoslopeOnRailTile(tile, flags, z_old, tileh_old, z_new, tileh_new, rail_bits);

		/* When there is only a single horizontal/vertical track, one corner can be terraformed. */
		Corner allowed_corner;
		switch (rail_bits) {
			case TRACK_BIT_RIGHT: allowed_corner = CORNER_W; break;
			case TRACK_BIT_UPPER: allowed_corner = CORNER_S; break;
			case TRACK_BIT_LEFT:  allowed_corner = CORNER_E; break;
			case TRACK_BIT_LOWER: allowed_corner = CORNER_N; break;
			default: return autoslope_result;
		}

		Foundation f_old = GetRailFoundation(tileh_old, rail_bits);

		/* Do not allow terraforming if allowed_corner is part of anti-zig-zag foundations */
		if (tileh_old != SLOPE_NS && tileh_old != SLOPE_EW && IsSpecialRailFoundation(f_old)) return autoslope_result;

		/* Everything is valid, which only changes allowed_corner */
		for (Corner corner = (Corner)0; corner < CORNER_END; corner = (Corner)(corner + 1)) {
			if (allowed_corner == corner) continue;
			if (z_old + GetSlopeZInCorner(tileh_old, corner) != z_new + GetSlopeZInCorner(tileh_new, corner)) return autoslope_result;
		}

		/* Make the ground dirty */
		if ((flags & DC_EXEC) != 0) SetRailGroundType(tile, RAIL_GROUND_BARREN);

		/* allow terraforming */
		return CommandCost(EXPENSES_CONSTRUCTION, was_water ? _price[PR_CLEAR_WATER] : (Money)0);
	} else if (_settings_game.construction.build_on_slopes && AutoslopeEnabled() &&
			AutoslopeCheckForEntranceEdge(tile, z_new, tileh_new, GetRailDepotDirection(tile))) {
		return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_FOUNDATION]);
	}
	return DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
}


extern const TileTypeProcs _tile_type_rail_procs = {
	DrawTile_Track,           // draw_tile_proc
	GetSlopeZ_Track,          // get_slope_z_proc
	ClearTile_Track,          // clear_tile_proc
	NULL,                     // add_accepted_cargo_proc
	GetTileDesc_Track,        // get_tile_desc_proc
	GetTileTrackStatus_Track, // get_tile_track_status_proc
	ClickTile_Track,          // click_tile_proc
	NULL,                     // animate_tile_proc
	TileLoop_Track,           // tile_loop_clear
	ChangeTileOwner_Track,    // change_tile_owner_clear
	NULL,                     // add_produced_cargo_proc
	VehicleEnter_Track,       // vehicle_enter_tile_proc
	GetFoundation_Track,      // get_foundation_proc
	TerraformTile_Track,      // terraform_tile_proc
};
