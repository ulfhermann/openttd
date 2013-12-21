/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file destinations.h Declaration of classes for cargo destinations. */

#ifndef DESTINATIONS_H
#define DESTINATIONS_H

#include "../core/smallvec_type.hpp"
#include "../cargotype.h"
#include "../industry.h"
#include "../town.h"
#include <map>

static const uint BIG_TOWN_POP_PAX     = 2000;
static const uint BIG_TOWN_POP_OTHER   = 500;
static const uint BASE_TOWN_LINKS_SYMM = 1;
static const uint BASE_TOWN_LINKS      = 0;
static const uint CITY_TOWN_LINKS      = 8;
static const uint SCALE_TOWN           = 100;
static const uint SCALE_TOWN_BIG       = 180;
static const uint SCALE_TOWN_PAX       = 200;
static const uint SCALE_TOWN_BIG_PAX   = 1000;
static const uint BASE_IND_LINKS       = 2;
static const uint BASE_IND_LINKS_TOWN  = 4;
static const uint BASE_IND_LINKS_SYMM  = 1;
static const uint CARGO_SCALE_IND      = 250;
static const uint CARGO_SCALE_IND_TOWN = 200;


struct CargoSourceSink {
	CargoSourceSink(SourceType type, SourceID id) : type(type), id(id) {}
	SourceType type;
	SourceID id;
};

bool operator<(const CargoSourceSink &i1, const CargoSourceSink &i2)
{
    return i1.id < i2.id || (i1.id == i2.id && i1.type < i2.type);
}

bool operator!=(const CargoSourceSink &i1, const CargoSourceSink &i2)
{
    return i1.id != i2.id || i1.type != i2.type;
}


struct DestinationList : public SmallVector<CargoSourceSink, 2> {
	DestinationList() : num_links_expected(0) {}
	uint16 num_links_expected;
};

typedef SmallVector<CargoSourceSink, 2> OriginList;

class CargoDestinations {
public:

	void AddSource(SourceType type, SourceID id);
	void RemoveSource(SourceType type, SourceID id);

	void AddSink(SourceType type, SourceID id);
	void RemoveSink(SourceType type, SourceID id);

	const DestinationList &GetDestinations(SourceType type, SourceID id) const;
	const OriginList &GetOrigins(SourceType type, SourceID id) const;

	void UpdateDestinations(CargoID cargo, Town *town);
	void UpdateDestinations(CargoID cargo, Industry *industry);

protected:
    void AddMissingDestinations(DestinationList &own_destinations, const CargoSourceSink &self);
    void AddAnywhere(DestinationList &own_destinations);
	std::map<CargoSourceSink, DestinationList> destinations;
    std::map<CargoSourceSink, OriginList> origins;
};


CargoDestinations _cargo_destinations[NUM_CARGO];

#endif // DESTINATIONS_H
