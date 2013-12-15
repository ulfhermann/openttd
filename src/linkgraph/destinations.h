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

struct CargoSourceSink {
    CargoSourceSink(SourceType type, SourceID id) : type(type), id(id) {}
    SourceType type;
    SourceID id;
};

bool operator<(const CargoSourceSink &i1, const CargoSourceSink &i2)
{
    return i1.type < i2.type || (i1.type == i2.type && i1.id < i2.id);
}

struct DestinationList : public SmallVector<CargoSourceSink> {
    DestinationList() : num_links_expected(0) {}
    uint16 num_links_expected;
};

typedef SmallVector<CargoSourceSink> OriginList;

class CargoDestinations {
public:
    CargoDestinations() : last_update(INVALID_DATE), last_removal(INVALID_DATE) {}

    void AddSource(SourceType type, SourceID id);
    void RemoveSource(SourceType type, SourceID id);

    void AddSink(SourceType type, SourceID id);
    void RemoveSink(SourceType type, SourceID id);

    void UpdateNumLinksExpected(CargoID cargo, Town *town);
    void UpdateNumLinksExpected(CargoID cargo, Industry *industry);

    const DestinationList &GetDestinations(SourceType type, SourceID id) const;
    const OriginList &GetOrigins(SourceType type, SourceID id) const;

    void UpdateDestinations();
    void Merge(const CargoDestinations &other);

protected:
    std::map<CargoSourceSink, DestinationList> destinations;
    std::map<CargoSourceSink, OriginList> origins;
    Date last_removal;
    Date last_update;
};


CargoDestinations _cargo_destinations[NUM_CARGO];

#endif // DESTINATIONS_H
