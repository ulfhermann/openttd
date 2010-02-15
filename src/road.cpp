/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file road.cpp Generic road related functions. */

#include "stdafx.h"
#include "openttd.h"
#include "rail_map.h"
#include "road_map.h"
#include "water_map.h"
#include "genworld.h"
#include "company_func.h"
#include "company_base.h"
#include "engine_base.h"
#include "date_func.h"
#include "landscape.h"

/**
 * Return if the tile is a valid tile for a crossing.
 *
 * @param tile the curent tile
 * @param ax the axis of the road over the rail
 * @return true if it is a valid tile
 */
static bool IsPossibleCrossing(const TileIndex tile, Axis ax)
{
	return (IsTileType(tile, MP_RAILWAY) &&
		GetRailTileType(tile) == RAIL_TILE_NORMAL &&
		GetTrackBits(tile) == (ax == AXIS_X ? TRACK_BIT_Y : TRACK_BIT_X) &&
		GetFoundationSlope(tile, NULL) == SLOPE_FLAT);
}

RoadBits CleanUpRoadBits(const TileIndex tile, RoadBits org_rb)
{
	if (!IsValidTile(tile)) return ROAD_NONE;
	for (DiagDirection dir = DIAGDIR_BEGIN; dir < DIAGDIR_END; dir++) {
		const TileIndex neighbor_tile = TileAddByDiagDir(tile, dir);

		/* Get the Roadbit pointing to the neighbor_tile */
		const RoadBits target_rb = DiagDirToRoadBits(dir);

		/* If the roadbit is in the current plan */
		if (org_rb & target_rb) {
			bool connective = false;
			const RoadBits mirrored_rb = MirrorRoadBits(target_rb);

			switch (GetTileType(neighbor_tile)) {
				/* Allways connective ones */
				case MP_CLEAR: case MP_TREES:
					connective = true;
					break;

				/* The conditionaly connective ones */
				case MP_TUNNELBRIDGE:
				case MP_STATION:
				case MP_ROAD: {
					const RoadBits neighbor_rb = GetAnyRoadBits(neighbor_tile, ROADTYPE_ROAD) | GetAnyRoadBits(neighbor_tile, ROADTYPE_TRAM);

					/* Accept only connective tiles */
					connective = (neighbor_rb & mirrored_rb) || // Neighbor has got the fitting RoadBit
							HasExactlyOneBit(neighbor_rb); // Neighbor has got only one Roadbit

				} break;

				case MP_RAILWAY:
					connective = IsPossibleCrossing(neighbor_tile, DiagDirToAxis(dir));
					break;

				case MP_WATER:
					/* Check for real water tile */
					connective = !IsWater(neighbor_tile);
					break;

				/* The defentetly not connective ones */
				default: break;
			}

			/* If the neighbor tile is inconnective remove the planed road connection to it */
			if (!connective) org_rb ^= target_rb;

		}
	}

	return org_rb;
}

bool HasRoadTypesAvail(const CompanyID company, const RoadTypes rts)
{
	RoadTypes avail_roadtypes;

	if (company == OWNER_TOWN || _game_mode == GM_EDITOR || IsGeneratingWorld()) {
		avail_roadtypes = ROADTYPES_ROAD;
	} else {
		Company *c = Company::GetIfValid(company);
		if (c == NULL) return false;
		avail_roadtypes = (RoadTypes)c->avail_roadtypes | ROADTYPES_ROAD; // road is available for always for everybody
	}
	return (rts & ~avail_roadtypes) == 0;
}

bool ValParamRoadType(const RoadType rt)
{
	return HasRoadTypesAvail(_current_company, RoadTypeToRoadTypes(rt));
}

RoadTypes GetCompanyRoadtypes(CompanyID company)
{
	RoadTypes rt = ROADTYPES_NONE;

	Engine *e;
	FOR_ALL_ENGINES_OF_TYPE(e, VEH_ROAD) {
		const EngineInfo *ei = &e->info;

		if (HasBit(ei->climates, _settings_game.game_creation.landscape) &&
				(HasBit(e->company_avail, company) || _date >= e->intro_date + DAYS_IN_YEAR)) {
			SetBit(rt, HasBit(ei->misc_flags, EF_ROAD_TRAM) ? ROADTYPE_TRAM : ROADTYPE_ROAD);
		}
	}

	return rt;
}
