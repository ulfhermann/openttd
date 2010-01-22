/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file station_base.h Base classes/functions for stations. */

#ifndef STATION_BASE_H
#define STATION_BASE_H

#include "base_station_base.h"
#include "airport.h"
#include "cargopacket.h"
#include "industry_type.h"
#include "moving_average.h"
#include <map>

typedef Pool<BaseStation, StationID, 32, 64000> StationPool;
extern StationPool _station_pool;

static const byte INITIAL_STATION_RATING = 175;

class LinkStat : private MovingAverage<uint> {
private:
	/**
	 * capacity of the link.
	 * This is a moving average use MovingAverage::Monthly() to get a meaningful value 
	 */
	uint capacity;
	
	/**
	 * capacity of currently loading vehicles
	 */
	uint frozen;
	
	/**
	 * usage of the link.
	 * This is a moving average use MovingAverage::Monthly() to get a meaningful value 
	 */
	uint usage;

public:
	friend const SaveLoad *GetLinkStatDesc();

	FORCEINLINE LinkStat(uint distance = 0, uint capacity = 0, uint frozen = 0, uint usage = 0) : 
		MovingAverage<uint>(distance), capacity(capacity), frozen(frozen), usage(usage) {}

	FORCEINLINE void Clear()
	{
		this->capacity = 0;
		this->usage = 0;
		this->frozen = 0;
	}

	FORCEINLINE void Decrease()
	{
		this->usage = this->MovingAverage<uint>::Decrease(this->usage);
		this->capacity = max(this->MovingAverage<uint>::Decrease(this->capacity), this->frozen);
	}

	FORCEINLINE uint Capacity()
	{
		return this->MovingAverage<uint>::Monthly(this->capacity);
	}

	FORCEINLINE uint Usage()
	{
		return this->MovingAverage<uint>::Monthly(this->usage);
	}

	FORCEINLINE uint Frozen()
	{
		return this->frozen;
	}

	FORCEINLINE void Increase(uint capacity, uint usage)
	{
		this->capacity += capacity;
		this->usage += usage;
	}

	FORCEINLINE void Freeze(uint capacity)
	{
		this->frozen += capacity;
		this->capacity = max(this->frozen, this->capacity);
	}

	FORCEINLINE void Unfreeze(uint capacity)
	{
		this->frozen -= capacity;
	}

	FORCEINLINE void Unfreeze()
	{
		this->frozen = 0;
	}

	FORCEINLINE bool IsNull()
	{
		return this->capacity == 0;
	}
};

typedef std::map<StationID, LinkStat> LinkStatMap;

class SupplyMovingAverage {
private:
	uint supply;

public:
	friend const SaveLoad *GetGoodsDesc();

	FORCEINLINE SupplyMovingAverage() : supply(0) {}

	FORCEINLINE void Increase(uint value) {this->supply += value;}

	FORCEINLINE void Decrease() {this->supply = MovingAverage<uint>().Decrease(this->supply);}

	FORCEINLINE uint Value() const {return MovingAverage<uint>().Monthly(this->supply);}
};


struct GoodsEntry {
	enum AcceptancePickup {
		ACCEPTANCE,
		PICKUP
	};

	GoodsEntry() :
		acceptance_pickup(0),
		days_since_pickup(255),
		rating(INITIAL_STATION_RATING),
		last_speed(0),
		last_age(255)
	{}

	byte acceptance_pickup;
	byte days_since_pickup;
	byte rating;
	byte last_speed;
	byte last_age;
	StationCargoList cargo; ///< The cargo packets of cargo waiting in this station
	SupplyMovingAverage supply;
	LinkStatMap link_stats; ///< capacities and usage statistics for outgoing links
};


typedef SmallVector<Industry *, 2> IndustryVector;

/** Station data structure */
struct Station : SpecializedStation<Station, false> {
public:
	RoadStop *GetPrimaryRoadStop(RoadStopType type) const
	{
		return type == ROADSTOP_BUS ? bus_stops : truck_stops;
	}

	RoadStop *GetPrimaryRoadStop(const struct RoadVehicle *v) const;

	const AirportFTAClass *Airport() const
	{
		if (airport_tile == INVALID_TILE) return GetAirport(AT_DUMMY);
		return GetAirport(airport_type);
	}

	const AirportSpec *GetAirportSpec() const
	{
		if (airport_tile == INVALID_TILE) return &AirportSpec::dummy;
		return AirportSpec::Get(this->airport_type);
	}

	RoadStop *bus_stops;    ///< All the road stops
	TileArea bus_station;   ///< Tile area the bus 'station' part covers
	RoadStop *truck_stops;  ///< All the truck stops
	TileArea truck_station; ///< Tile area the truck 'station' part covers

	TileIndex airport_tile; ///< The location of the airport
	TileIndex dock_tile;    ///< The location of the dock

	IndustryType indtype;   ///< Industry type to get the name from

	StationHadVehicleOfTypeByte had_vehicle_of_type;

	byte time_since_load;
	byte time_since_unload;
	byte airport_type;

	uint64 airport_flags;   ///< stores which blocks on the airport are taken. was 16 bit earlier on, then 32

	byte last_vehicle_type;
	std::list<Vehicle *> loading_vehicles;
	GoodsEntry goods[NUM_CARGO];  ///< Goods at this station
	uint32 always_accepted;       ///< Bitmask of always accepted cargo types (by houses, HQs, industry tiles when industry doesn't accept cargo)

	IndustryVector industries_near; ///< Cached list of industries near the station that can accept cargo, @see DeliverGoodsToIndustry()

	Station(TileIndex tile = INVALID_TILE);
	~Station();

	void AddFacility(StationFacility new_facility_bit, TileIndex facil_xy);

	/**
	 * Marks the tiles of the station as dirty.
	 *
	 * @ingroup dirty
	 */
	void MarkTilesDirty(bool cargo_change) const;

	void UpdateVirtCoord();

	/* virtual */ uint GetPlatformLength(TileIndex tile, DiagDirection dir) const;
	/* virtual */ uint GetPlatformLength(TileIndex tile) const;
	void RecomputeIndustriesNear();
	static void RecomputeIndustriesNearForAll();

	uint GetCatchmentRadius() const;
	Rect GetCatchmentRect() const;

	/* virtual */ FORCEINLINE bool TileBelongsToRailStation(TileIndex tile) const
	{
		return IsRailStationTile(tile) && GetStationIndex(tile) == this->index;
	}

	FORCEINLINE bool TileBelongsToAirport(TileIndex tile) const
	{
		return IsAirportTile(tile) && GetStationIndex(tile) == this->index;
	}

	FORCEINLINE TileIndex GetHangarTile(uint hangar_num) const
	{
		assert(this->airport_tile != INVALID_TILE);
		assert(hangar_num < this->GetAirportSpec()->nof_depots);
		return this->airport_tile + ToTileIndexDiff(this->GetAirportSpec()->depot_table[hangar_num]);
	}

	/* virtual */ uint32 GetNewGRFVariable(const ResolverObject *object, byte variable, byte parameter, bool *available) const;

	/* virtual */ void GetTileArea(TileArea *ta, StationType type) const;

	void RunAverages();
};

#define FOR_ALL_STATIONS(var) FOR_ALL_BASE_STATIONS_OF_TYPE(Station, var)

#endif /* STATION_BASE_H */
