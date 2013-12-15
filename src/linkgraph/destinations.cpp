/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file destinations.cpp Definition of cargo destinations. */

#include "../stdafx.h"
#include "destinations.h"

void CargoDestinations::AddSource(SourceType type, SourceID id)
{
    this->destinations[CargoSourceSink(type, id)] = DestinationList();
    this->last_update = _date;
}

void CargoDestinations::RemoveSource(SourceType type, SourceID id)
{
    this->destinations.erase(CargoSourceSink(type, id));
    this->last_update = this->last_removal = _date;
}

void CargoDestinations::AddSink(SourceType type, SourceID id)
{
    this->origins[CargoSourceSink(type, id)] = OriginList();
    this->last_update = _date;
}

void CargoDestinations::RemoveSink(SourceType type, SourceID id)
{
    this->origins.erase(CargoSourceSink(type, id));
    this->last_update = this->last_removal = _date;
}

void CargoDestinations::UpdateNumLinksExpected(CargoID cargo, Town *town)
{
}

const DestinationList &CargoDestinations::GetDestinations(SourceType type, SourceID id) const
{
}

const OriginList &CargoDestinations::GetOrigins(SourceType type, SourceID id) const
{
}

void CargoDestinations::UpdateDestinations()
{
}

void CargoDestinations::Merge(const CargoDestinations &other)
{
}
