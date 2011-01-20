/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file cargodest_func.h Functions related to cargo destinations. */

#ifndef CARGODEST_FUNC_H
#define CARGODEST_FUNC_H

#include "cargo_type.h"
#include "vehicle_type.h"
#include "station_type.h"

bool CargoHasDestinations(CargoID cid);

void UpdateVehicleRouteLinks(const Vehicle *v, StationID arrived_at);
void InvalidateStationRouteLinks(Station *station);

void RebuildCargoLinkCounts();
void UpdateCargoLinks();

#endif /* CARGODEST_FUNC_H */
