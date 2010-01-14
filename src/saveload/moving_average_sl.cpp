/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file cargopacket_sl.cpp Code handling saving and loading of cargo packets */

#include "../stdafx.h"
#include "../moving_average.h"

#include "saveload.h"

/**
 * Wrapper function to get the MovingAverage's internal structure while
 * some of the variables itself are private.
 * @return the saveload description for MovingAverages.
 */
const SaveLoad *GetMovingAverageDesc()
{
	static const SaveLoad _moving_average_desc[] = {
		SLE_VAR(MovingAverage, length, SLE_UINT),
		SLE_VAR(MovingAverage, value,  SLE_UINT),
		SLE_END()
	};
	return _moving_average_desc;
}

static MovingAverageList _ma_entry;
static size_t _ma_size;

const SaveLoad _moving_average_registry_size_desc[] = {
	SLEG_VAR(_ma_size, SLE_UINT),
	SLE_END()
};

const SaveLoad _moving_average_registry_entry_desc[] = {
	SLEG_LST(_ma_entry, SLE_UINT),
	SLE_END()
};

static void Save_MOVA()
{
	MovingAverage *ma;

	FOR_ALL_MOVING_AVERAGES(ma) {
		SlSetArrayIndex(ma->index);
		SlObject(ma, GetMovingAverageDesc());
	}
	
	_ma_size = _moving_averages.size();
	SlObject(NULL, _moving_average_registry_size_desc);

	for (MovingAverageRegistry::iterator i = _moving_averages.begin(); i != _moving_averages.end(); ++i) {
		_ma_entry = *i;
		SlObject(NULL, _moving_average_registry_entry_desc);
	}
}

static void Load_MOVA()
{
	int index;

	while ((index = SlIterateArray()) != -1) {
		MovingAverage *ma = new (index) MovingAverage();
		SlObject(ma, GetMovingAverageDesc());
	}

	SlObject(NULL, _moving_average_registry_size_desc);
	
	while(_ma_size-- != 0) {
		SlObject(NULL, _moving_average_registry_entry_desc);
		_moving_averages.push_back(_ma_entry);
	}
}

extern const ChunkHandler _moving_average_chunk_handlers[] = {
	{ 'MOVA', Save_MOVA, Load_MOVA, NULL, CH_ARRAY | CH_LAST},
};
