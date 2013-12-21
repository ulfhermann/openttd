/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file cargodest_sl.cpp Code handling saving and loading of cargo destinations. */

#include "../stdafx.h"
#include "../cargodest_base.h"
#include "../town.h"
#include "../industry.h"
#include "saveload.h"

static uint32 _cargolink_uint;
static const SaveLoadGlobVarList _cargolink_uint_desc[] = {
	SLEG_VAR(_cargolink_uint, SLE_UINT32),
	SLEG_END()
};

static const SaveLoad _cargolink_desc[] = {
	SLE_VAR(CargoLink, weight,         SLE_UINT32),
	SLE_VAR(CargoLink, weight_mod,     SLE_UINT8),
	SLE_END()
};

void CargoSourceSink::SaveCargoSourceSink()
{
	if (IsSavegameVersionBefore(200)) return;

	static const SaveLoad _cargosourcesink_desc[] = {
		SLE_ARR(CargoSourceSink, cargo_links_weight, SLE_UINT32, NUM_CARGO),
		SLE_END()
	};
	SlObject(this, _cargosourcesink_desc);

	for (uint cid = 0; cid < lengthof(this->cargo_links); cid++) {
		_cargolink_uint = this->cargo_links[cid].Length();
		SlObject(NULL, _cargolink_uint_desc);
		for (CargoLink *l = this->cargo_links[cid].Begin(); l != this->cargo_links[cid].End(); l++) {
			SourceID dest = INVALID_SOURCE;
			SourceTypeByte type;
			type = ST_TOWN;

			if (l->dest != NULL) {
				type = l->dest->GetType();
				dest = l->dest->GetID();
			}

			/* Pack type and destination index into temp variable. */
			assert_compile(sizeof(SourceID) <= 3);
			_cargolink_uint = type | (dest << 8);

			SlGlobList(_cargolink_uint_desc);
			SlObject(l, _cargolink_desc);
		}
	}
}

void CargoSourceSink::LoadCargoSourceSink()
{
	if (IsSavegameVersionBefore(200)) return;

	static const SaveLoad _cargosourcesink_desc[] = {
		SLE_ARR(CargoSourceSink, cargo_links_weight, SLE_UINT32, NUM_CARGO),
		SLE_END()
	};
	SlObject(this, _cargosourcesink_desc);

	for (uint cid = 0; cid < lengthof(this->cargo_links); cid++) {
		/* Remove links created by constructors. */
		this->cargo_links[cid].Clear();
		/* Read vector length and allocate storage. */
		SlObject(NULL, _cargolink_uint_desc);
		this->cargo_links[cid].Append(_cargolink_uint);

		for (CargoLink *l = this->cargo_links[cid].Begin(); l != this->cargo_links[cid].End(); l++) {
			/* Read packed type and dest and store in dest pointer. */
			SlGlobList(_cargolink_uint_desc);
			*(size_t*)&l->dest = _cargolink_uint;

			SlObject(l, _cargolink_desc);
		}
	}
}

void CargoSourceSink::PtrsCargoSourceSink()
{
	if (IsSavegameVersionBefore(200)) return;

	for (uint cid = 0; cid < lengthof(this->cargo_links); cid++) {
		for (CargoLink *l = this->cargo_links[cid].Begin(); l != this->cargo_links[cid].End(); l++) {
			/* Extract type and destination index. */
			SourceType type = (SourceType)((size_t)l->dest & 0xFF);
			SourceID dest = (SourceID)((size_t)l->dest >> 8);

			/* Resolve index. */
			l->dest = NULL;
			if (dest != INVALID_SOURCE) {
				switch (type) {
					case ST_TOWN:
						if (!Town::IsValidID(dest)) SlErrorCorrupt("Invalid cargo link destination");
						l->dest = Town::Get(dest);
						break;

					case ST_INDUSTRY:
						if (!Industry::IsValidID(dest)) SlErrorCorrupt("Invalid cargo link destination");
						l->dest = Industry::Get(dest);
						break;

					default:
						SlErrorCorrupt("Invalid cargo link destination type");
				}
			}
		}
	}
}
