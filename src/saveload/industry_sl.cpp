/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file industry_sl.cpp Code handling saving and loading of industries */

#include "../stdafx.h"
#include "../industry.h"
#include "../newgrf_commons.h"
#include "../economy_func.h"
#include "../town.h"

#include "saveload.h"

static const SaveLoad _industry_desc[] = {
	SLE_CONDVAR(Industry, location.tile,              SLE_FILE_U16 | SLE_VAR_U32,  0, 5),
	SLE_CONDVAR(Industry, location.tile,              SLE_UINT32,                  6, SL_MAX_VERSION),
	    SLE_VAR(Industry, location.w,                 SLE_UINT8),
	    SLE_VAR(Industry, location.h,                 SLE_UINT8),
	    SLE_REF(Industry, town,                       REF_TOWN),
	SLE_CONDNULL( 2, 0, 60),       ///< used to be industry's produced_cargo
	SLE_CONDARR(Industry, produced_cargo,             SLE_UINT8,  2,              78, SL_MAX_VERSION),
	SLE_CONDARR(Industry, incoming_cargo_waiting,     SLE_UINT16, 3,              70, SL_MAX_VERSION),
	    SLE_ARR(Industry, produced_cargo_waiting,     SLE_UINT16, 2),
	    SLE_ARR(Industry, production_rate,            SLE_UINT8,  2),
	SLE_CONDNULL( 3, 0, 60),       ///< used to be industry's accepts_cargo
	SLE_CONDARR(Industry, accepts_cargo,              SLE_UINT8,  3,              78, SL_MAX_VERSION),
	    SLE_VAR(Industry, prod_level,                 SLE_UINT8),
	    SLE_ARR(Industry, this_month_production,      SLE_UINT16, 2),
	    SLE_ARR(Industry, this_month_transported,     SLE_UINT16, 2),
	    SLE_ARR(Industry, last_month_pct_transported, SLE_UINT8,  2),
	    SLE_ARR(Industry, last_month_production,      SLE_UINT16, 2),
	    SLE_ARR(Industry, last_month_transported,     SLE_UINT16, 2),

	    SLE_VAR(Industry, counter,                    SLE_UINT16),

	    SLE_VAR(Industry, type,                       SLE_UINT8),
	    SLE_VAR(Industry, owner,                      SLE_UINT8),
	    SLE_VAR(Industry, random_colour,              SLE_UINT8),
	SLE_CONDVAR(Industry, last_prod_year,             SLE_FILE_U8 | SLE_VAR_I32,  0, 30),
	SLE_CONDVAR(Industry, last_prod_year,             SLE_INT32,                 31, SL_MAX_VERSION),
	    SLE_VAR(Industry, was_cargo_delivered,        SLE_UINT8),

	SLE_CONDVAR(Industry, founder,                    SLE_UINT8,                 70, SL_MAX_VERSION),
	SLE_CONDVAR(Industry, construction_date,          SLE_INT32,                 70, SL_MAX_VERSION),
	SLE_CONDVAR(Industry, construction_type,          SLE_UINT8,                 70, SL_MAX_VERSION),
	SLE_CONDVAR(Industry, last_cargo_accepted_at,     SLE_INT32,                 70, SL_MAX_VERSION),
	SLE_CONDVAR(Industry, selected_layout,            SLE_UINT8,                 73, SL_MAX_VERSION),

	SLE_CONDARR(Industry, psa.storage,                SLE_UINT32, 16,            76, SL_MAX_VERSION),

	SLE_CONDVAR(Industry, random_triggers,            SLE_UINT8,                 82, SL_MAX_VERSION),
	SLE_CONDVAR(Industry, random,                     SLE_UINT16,                82, SL_MAX_VERSION),

	/* reserve extra space in savegame here. (currently 32 bytes) */
	SLE_CONDNULL(32, 2, SL_MAX_VERSION),

	SLE_END()
};

static void Save_INDY()
{
	Industry *ind;

	/* Write the industries */
	FOR_ALL_INDUSTRIES(ind) {
		SlSetArrayIndex(ind->index);
		SlObject(ind, _industry_desc);
	}
}

/* Save and load the mapping between the industry/tile id on the map, and the grf file
 * it came from. */
static const SaveLoad _industries_id_mapping_desc[] = {
	SLE_VAR(EntityIDMapping, grfid,         SLE_UINT32),
	SLE_VAR(EntityIDMapping, entity_id,     SLE_UINT8),
	SLE_VAR(EntityIDMapping, substitute_id, SLE_UINT8),
	SLE_END()
};

static void Save_IIDS()
{
	uint i;
	uint j = _industry_mngr.GetMaxMapping();

	for (i = 0; i < j; i++) {
		SlSetArrayIndex(i);
		SlObject(&_industry_mngr.mapping_ID[i], _industries_id_mapping_desc);
	}
}

static void Save_TIDS()
{
	uint i;
	uint j = _industile_mngr.GetMaxMapping();

	for (i = 0; i < j; i++) {
		SlSetArrayIndex(i);
		SlObject(&_industile_mngr.mapping_ID[i], _industries_id_mapping_desc);
	}
}

static void Load_INDY()
{
	int index;

	ResetIndustryCounts();

	while ((index = SlIterateArray()) != -1) {
		Industry *i = new (index) Industry();
		SlObject(i, _industry_desc);
		IncIndustryTypeCount(i->type);
	}
}

static void Load_IIDS()
{
	int index;
	uint max_id;

	/* clear the current mapping stored.
	 * This will create the manager if ever it is not yet done */
	_industry_mngr.ResetMapping();

	/* get boundary for the temporary map loader NUM_INDUSTRYTYPES? */
	max_id = _industry_mngr.GetMaxMapping();

	while ((index = SlIterateArray()) != -1) {
		if ((uint)index >= max_id) break;
		SlObject(&_industry_mngr.mapping_ID[index], _industries_id_mapping_desc);
	}
}

static void Load_TIDS()
{
	int index;
	uint max_id;

	/* clear the current mapping stored.
	 * This will create the manager if ever it is not yet done */
	_industile_mngr.ResetMapping();

	/* get boundary for the temporary map loader NUM_INDUSTILES? */
	max_id = _industile_mngr.GetMaxMapping();

	while ((index = SlIterateArray()) != -1) {
		if ((uint)index >= max_id) break;
		SlObject(&_industile_mngr.mapping_ID[index], _industries_id_mapping_desc);
	}
}

static void Ptrs_INDY()
{
	Industry *i;

	FOR_ALL_INDUSTRIES(i) {
		SlObject(i, _industry_desc);
	}
}

void UpdateGlobalIndustryStatistics() {
	const Industry *i;
	FOR_ALL_INDUSTRIES(i) {
		Town *t = i->town;
		TILE_AREA_LOOP(tile_cur, i->location) {
			if (IsTileType(tile_cur, MP_INDUSTRY)) {
				ModifyAcceptedCargo_Industry(tile_cur, t->acceptance, ACCEPTANCE_ADD);
				t->CountAcceptedCargos();
				ModifyAcceptedCargo_Industry(tile_cur, _economy.global_acceptance, ACCEPTANCE_ADD);
			}
		}
	}
}


extern const ChunkHandler _industry_chunk_handlers[] = {
	{ 'INDY', Save_INDY, Load_INDY, Ptrs_INDY, CH_ARRAY},
	{ 'IIDS', Save_IIDS, Load_IIDS,      NULL, CH_ARRAY},
	{ 'TIDS', Save_TIDS, Load_TIDS,      NULL, CH_ARRAY | CH_LAST},
};
