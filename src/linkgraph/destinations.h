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
static const uint HQ_LINKS             = 3;
static const uint FILTER_LENGTH        = 8;


struct CargoSourceSink {
	CargoSourceSink(SourceType type, SourceID id) : type(type), id(id) {}
	SourceType type;
	SourceID id;
};

inline bool operator<(const CargoSourceSink &i1, const CargoSourceSink &i2)
{
	return i1.id < i2.id || (i1.id == i2.id && i1.type < i2.type);
}

inline bool operator!=(const CargoSourceSink &i1, const CargoSourceSink &i2)
{
	return i1.id != i2.id || i1.type != i2.type;
}

inline bool operator==(const CargoSourceSink &i1, const CargoSourceSink &i2)
{
	return i1.id == i2.id && i1.type == i2.type;
}

struct DestinationList : public SmallVector<CargoSourceSink, 2> {
	DestinationList() : num_links_expected(0) {}
	uint16 num_links_expected;
};

typedef SmallVector<CargoSourceSink, 2> OriginList;

class CargoDestinations {
public:

	static void Initialize();

	void RemoveSource(SourceType type, SourceID id);
	void RemoveSink(SourceType type, SourceID id);

	const DestinationList &GetDestinations(SourceType type, SourceID id) const;
	const OriginList &GetOrigins(SourceType type, SourceID id) const;

	void UpdateDestinations(const Town *town);
	void UpdateDestinations(const Industry *industry);
	void UpdateDestinations(const Company *company);

	void UpdateOrigins(const Town *town);
	void UpdateOrigins(const Industry *industry);
	void UpdateOrigins(const Company *company);

	bool IsSymmetric() { return _settings_game.linkgraph.GetDistributionType(cargo) == DT_DEST_SYMMETRIC; }
	CargoID GetCargo() { return this->cargo; }

	inline void AddOriginStation(StationID station, SourceID source)
	{
		this->AddStation(station, source, this->origin_stations);
	}

	inline void AddDestinationStation(StationID station, SourceID sink)
	{
		this->AddStation(station, sink, this->destination_stations);
	}

	inline bool IsOriginStation(StationID station, SourceID source) const
	{
		return this->IsAssociated(station, source, this->origin_stations);
	}

	inline bool IsDestinationStation(StationID station, SourceID source) const
	{
		return this->IsAssociated(station, source, this->destination_stations);
	}

protected:
	CargoID cargo;

	const static CargoSourceSink _invalid_source_sink;

	void AddMissingDestinations(DestinationList &own_destinations, const CargoSourceSink &self);
	void AddMissingOrigin(OriginList &own_origins, const CargoSourceSink &self);
	template<class Tmap, class Tlist>
	CargoSourceSink AddLink(Tlist &own, Tmap &other, const CargoSourceSink &self, const CargoSourceSink &last);
	void AddSymmetric(const CargoSourceSink &orig, const CargoSourceSink &dest);

	inline uint GetIndex(StationID station, SourceID source_sink) const
	{
		/* Switch station's bits around so that we get a good hash also for small IDs. */
		return (source_sink ^
				(station << (FILTER_LENGTH / 2)) ^
				((station >> (FILTER_LENGTH / 2)) & ((1 << (FILTER_LENGTH / 2)) - 1))) &
				((1 << FILTER_LENGTH) - 1);
	}

	inline bool IsAssociated(StationID station, SourceID source_sink, const int32 *filter) const
	{
		uint index = this->GetIndex(station, source_sink);
		return HasBit(filter[index >> 5], index & 0x1F);
	}

	inline void AddStation(StationID station, SourceID source_sink, int32 *filter)
	{
		uint index = this->GetIndex(station, source_sink);
		SetBit(filter[index >> 5], index & 0x1F);
	}

	std::map<CargoSourceSink, DestinationList> destinations;
    std::map<CargoSourceSink, OriginList> origins;

	int32 origin_stations[(1 << FILTER_LENGTH) / 32];
	int32 destination_stations[(1 << FILTER_LENGTH) / 32];
};


extern CargoDestinations _cargo_destinations[NUM_CARGO];

#endif // DESTINATIONS_H
