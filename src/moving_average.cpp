/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file cargopacket.cpp Implementation of moving average functions */


#include "moving_average.h"
#include "variables.h"

template<class Titem>
/* static */ void RunAverages()
{
	uint interval = _settings_game.economy.moving_average_unit * DAY_TICKS;
	for(uint id = _tick_counter % interval; id < Titem::GetPoolSize(); ++id) {
		Titem *item = Titem::GetIfValid(id);
		if (item != NULL) {
			item->RunAverages();
		}
	}
}
