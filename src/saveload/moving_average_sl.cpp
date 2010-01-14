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
		SLE_VAR(MovingAverage, length, SLE_UINT32),
		SLE_VAR(MovingAverage, value,  SLE_UINT32),
		SLE_END()
	};
	return _moving_average_desc;
}

static void Save_MOVA()
{
	MovingAverage *ma;

	FOR_ALL_MOVING_AVERAGES(ma) {
		SlSetArrayIndex(ma->index);
		SlObject(ma, GetMovingAverageDesc());
	}
}

static void Load_MOVA()
{
	int index;

	while ((index = SlIterateArray()) != -1) {
		MovingAverage *ma = new (index) MovingAverage();
		SlObject(ma, GetMovingAverageDesc());
	}
}

extern const ChunkHandler _moving_average_chunk_handlers[] = {
	{ 'MOVA', Save_MOVA, Load_MOVA, NULL, CH_ARRAY | CH_LAST},
};
