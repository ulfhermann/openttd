/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file town_sl.cpp Code handling saving and loading of towns and houses */

#include "../stdafx.h"
#include "../newgrf_house.h"
#include "../newgrf_commons.h"
#include "../town.h"
#include "../economy_func.h"

#include "saveload.h"

/**
 * Check and update town and house values.
 *
 * Checked are the HouseIDs. Updated are the
 * town population the number of houses per
 * town, the town radius and the max passengers
 * of the town.
 */
void UpdateHousesAndTowns()
{
	Town *town;
	InitializeBuildingCounts();

	/* Reset town population and num_houses */
	FOR_ALL_TOWNS(town) {
		town->population = 0;
		town->num_houses = 0;
		town->acceptance.Clear();
	}

	for (TileIndex t = 0; t < MapSize(); t++) {
		HouseID house_id;

		if (!IsTileType(t, MP_HOUSE)) continue;

		house_id = GetHouseType(t);
		if (!HouseSpec::Get(house_id)->enabled && house_id >= NEW_HOUSE_OFFSET) {
			/* The specs for this type of house are not available any more, so
			 * replace it with the substitute original house type. */
			house_id = _house_mngr.GetSubstituteID(house_id);
			SetHouseType(t, house_id);
		}

		town = Town::GetByTile(t);
		IncreaseBuildingCount(town, house_id);

		ModifyAcceptedCargo_Town(t, town->acceptance, ACCEPTANCE_ADD);
		ModifyAcceptedCargo_Town(t, _economy.global_acceptance, ACCEPTANCE_ADD);

		/* Increase the number of houses for every house, but only once. */
		if (GetHouseNorthPart(house_id) == 0) town->num_houses++;
	}

	/* Update the population and num_house dependant values */
	FOR_ALL_TOWNS(town) {
		UpdateTownRadius(town);
	}
}

/** Save and load of towns. */
static const SaveLoad _town_desc[] = {
	SLE_CONDVAR(Town, xy,                    SLE_FILE_U16 | SLE_VAR_U32, 0, 5),
	SLE_CONDVAR(Town, xy,                    SLE_UINT32,                 6, SL_MAX_VERSION),

	SLE_CONDNULL(2, 0, 2),                   ///< population, no longer in use
	SLE_CONDNULL(4, 3, 84),                  ///< population, no longer in use
	SLE_CONDNULL(2, 0, 91),                  ///< num_houses, no longer in use

	SLE_CONDVAR(Town, townnamegrfid,         SLE_UINT32, 66, SL_MAX_VERSION),
	    SLE_VAR(Town, townnametype,          SLE_UINT16),
	    SLE_VAR(Town, townnameparts,         SLE_UINT32),
	SLE_CONDSTR(Town, name,                  SLE_STR, 0, 84, SL_MAX_VERSION),

	    SLE_VAR(Town, flags,                 SLE_UINT8),
	SLE_CONDVAR(Town, statues,               SLE_FILE_U8  | SLE_VAR_U16, 0, 103),
	SLE_CONDVAR(Town, statues,               SLE_UINT16,               104, SL_MAX_VERSION),

	SLE_CONDNULL(1, 0, 1),                   ///< sort_index, no longer in use

	SLE_CONDVAR(Town, have_ratings,          SLE_FILE_U8  | SLE_VAR_U16, 0, 103),
	SLE_CONDVAR(Town, have_ratings,          SLE_UINT16,               104, SL_MAX_VERSION),
	SLE_CONDARR(Town, ratings,               SLE_INT16, 8,               0, 103),
	SLE_CONDARR(Town, ratings,               SLE_INT16, MAX_COMPANIES, 104, SL_MAX_VERSION),
	/* failed bribe attempts are stored since savegame format 4 */
	SLE_CONDARR(Town, unwanted,              SLE_INT8,  8,               4, 103),
	SLE_CONDARR(Town, unwanted,              SLE_INT8,  MAX_COMPANIES, 104, SL_MAX_VERSION),

	SLE_CONDVAR(Town, max_pass,              SLE_FILE_U16 | SLE_VAR_U32, 0, 8),
	SLE_CONDVAR(Town, max_mail,              SLE_FILE_U16 | SLE_VAR_U32, 0, 8),
	SLE_CONDVAR(Town, new_max_pass,          SLE_FILE_U16 | SLE_VAR_U32, 0, 8),
	SLE_CONDVAR(Town, new_max_mail,          SLE_FILE_U16 | SLE_VAR_U32, 0, 8),
	SLE_CONDVAR(Town, act_pass,              SLE_FILE_U16 | SLE_VAR_U32, 0, 8),
	SLE_CONDVAR(Town, act_mail,              SLE_FILE_U16 | SLE_VAR_U32, 0, 8),
	SLE_CONDVAR(Town, new_act_pass,          SLE_FILE_U16 | SLE_VAR_U32, 0, 8),
	SLE_CONDVAR(Town, new_act_mail,          SLE_FILE_U16 | SLE_VAR_U32, 0, 8),

	SLE_CONDVAR(Town, max_pass,              SLE_UINT32,                 9, SL_MAX_VERSION),
	SLE_CONDVAR(Town, max_mail,              SLE_UINT32,                 9, SL_MAX_VERSION),
	SLE_CONDVAR(Town, new_max_pass,          SLE_UINT32,                 9, SL_MAX_VERSION),
	SLE_CONDVAR(Town, new_max_mail,          SLE_UINT32,                 9, SL_MAX_VERSION),
	SLE_CONDVAR(Town, act_pass,              SLE_UINT32,                 9, SL_MAX_VERSION),
	SLE_CONDVAR(Town, act_mail,              SLE_UINT32,                 9, SL_MAX_VERSION),
	SLE_CONDVAR(Town, new_act_pass,          SLE_UINT32,                 9, SL_MAX_VERSION),
	SLE_CONDVAR(Town, new_act_mail,          SLE_UINT32,                 9, SL_MAX_VERSION),

	    SLE_VAR(Town, pct_pass_transported,  SLE_UINT8),
	    SLE_VAR(Town, pct_mail_transported,  SLE_UINT8),

	    SLE_VAR(Town, act_food,              SLE_UINT16),
	    SLE_VAR(Town, act_water,             SLE_UINT16),
	    SLE_VAR(Town, new_act_food,          SLE_UINT16),
	    SLE_VAR(Town, new_act_water,         SLE_UINT16),

	SLE_CONDVAR(Town, time_until_rebuild,    SLE_FILE_U8 | SLE_VAR_U16,  0, 53),
	SLE_CONDVAR(Town, grow_counter,          SLE_FILE_U8 | SLE_VAR_U16,  0, 53),
	SLE_CONDVAR(Town, growth_rate,           SLE_FILE_U8 | SLE_VAR_I16,  0, 53),

	SLE_CONDVAR(Town, time_until_rebuild,    SLE_UINT16,                54, SL_MAX_VERSION),
	SLE_CONDVAR(Town, grow_counter,          SLE_UINT16,                54, SL_MAX_VERSION),
	SLE_CONDVAR(Town, growth_rate,           SLE_INT16,                 54, SL_MAX_VERSION),

	    SLE_VAR(Town, fund_buildings_months, SLE_UINT8),
	    SLE_VAR(Town, road_build_months,     SLE_UINT8),

	SLE_CONDVAR(Town, exclusivity,           SLE_UINT8,                  2, SL_MAX_VERSION),
	SLE_CONDVAR(Town, exclusive_counter,     SLE_UINT8,                  2, SL_MAX_VERSION),

	SLE_CONDVAR(Town, larger_town,           SLE_BOOL,                  56, SL_MAX_VERSION),
	SLE_CONDVAR(Town, layout,                SLE_UINT8,                113, SL_MAX_VERSION),

	/* reserve extra space in savegame here. (currently 30 bytes) */
	SLE_CONDNULL(30, 2, SL_MAX_VERSION),

	SLE_END()
};

/* Save and load the mapping between the house id on the map, and the grf file
 * it came from. */
static const SaveLoad _house_id_mapping_desc[] = {
	SLE_VAR(EntityIDMapping, grfid,         SLE_UINT32),
	SLE_VAR(EntityIDMapping, entity_id,     SLE_UINT8),
	SLE_VAR(EntityIDMapping, substitute_id, SLE_UINT8),
	SLE_END()
};

static void Save_HOUSEIDS()
{
	uint j = _house_mngr.GetMaxMapping();

	for (uint i = 0; i < j; i++) {
		SlSetArrayIndex(i);
		SlObject(&_house_mngr.mapping_ID[i], _house_id_mapping_desc);
	}
}

static void Load_HOUSEIDS()
{
	int index;

	_house_mngr.ResetMapping();
	uint max_id = _house_mngr.GetMaxMapping();

	while ((index = SlIterateArray()) != -1) {
		if ((uint)index >= max_id) break;
		SlObject(&_house_mngr.mapping_ID[index], _house_id_mapping_desc);
	}
}

static void Save_TOWN()
{
	Town *t;

	FOR_ALL_TOWNS(t) {
		SlSetArrayIndex(t->index);
		SlObject(t, _town_desc);
	}
}

static void Load_TOWN()
{
	int index;

	while ((index = SlIterateArray()) != -1) {
		Town *t = new (index) Town();
		SlObject(t, _town_desc);
	}
}

extern const ChunkHandler _town_chunk_handlers[] = {
	{ 'HIDS', Save_HOUSEIDS, Load_HOUSEIDS, NULL, CH_ARRAY },
	{ 'CITY', Save_TOWN,     Load_TOWN,     NULL, CH_ARRAY | CH_LAST},
};
