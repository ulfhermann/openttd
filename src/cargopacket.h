/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file cargopacket.h Base class for cargo packets. */

#ifndef CARGOPACKET_H
#define CARGOPACKET_H

#include "core/pool_type.hpp"
#include "economy_type.h"
#include "tile_type.h"
#include "station_type.h"
#include "order_type.h"
#include "cargo_type.h"
#include "vehicle_type.h"
#include "core/multimap.hpp"
#include <set>

typedef uint32 CargoPacketID;
struct CargoPacket;
struct GoodsEntry;

/** We want to use a pool */
typedef Pool<CargoPacket, CargoPacketID, 1024, 1048576> CargoPacketPool;
extern CargoPacketPool _cargopacket_pool;

template <class LIST> class CargoList;
class StationCargoList;
class VehicleCargoList;
extern const struct SaveLoad *GetCargoPacketDesc();

/**
 * Container for cargo from the same location and time
 */
struct CargoPacket : CargoPacketPool::PoolItem<&_cargopacket_pool> {
private:
	/* These fields are all involved in the cargo list's cache or in the vehicle cargo list's set.
	 * They can only be modified by CargoList which knows about that.
	 */
	Money feeder_share;         ///< Value of feeder pickup to be paid for on delivery of cargo
	uint16 count;               ///< The amount of cargo in this packet
	byte days_in_transit;       ///< Amount of days this packet has been in transit
	SourceTypeByte source_type; ///< Type of #source_id
	SourceID source_id;         ///< Index of source, INVALID_SOURCE if unknown/invalid
	TileIndex source_xy;        ///< The origin of the cargo (first station in feeder chain)

public:
	/* CargoLists need to be able to modify their own packets */
	template<class LIST> friend class CargoList;
	friend class VehicleCargoList;
	friend class StationCargoList;

	/* we have to do some evil things when save/loading */
	friend const struct SaveLoad *GetCargoPacketDesc();
	friend bool AfterLoadGame();

	static const uint16 MAX_COUNT = 65535;

	/*
	 * source and loaded_at_xy don't mess with the cargo list's cache
	 * so it's OK to modify them.
	 */
	StationID source;           ///< The station where the cargo came from first
	TileIndex loaded_at_xy;     ///< Location where this cargo has been loaded into the vehicle

	/**
	 * Creates a new cargo packet
	 * @param source the source of the packet
	 * @param count  the number of cargo entities to put in this packet
	 * @param source_type the 'type' of source the packet comes from (for subsidies)
	 * @param source_id the actual source of the packet (for subsidies)
	 * @pre count != 0 || source == INVALID_STATION
	 */
	CargoPacket(StationID source = INVALID_STATION, uint16 count = 0, SourceType source_type = ST_INDUSTRY, SourceID source_id = INVALID_SOURCE);

	/*
	 * Creates a new cargo packet. Initializes the fields that cannot be changed later.
	 * Used when loading or splitting packets.
	 * @param cnt the number of cargo entities to put in this packet
	 * @param dit number of days the cargo has been in transit
	 * @param fs  feeder share the packet has already accumulated
	 */
	CargoPacket(SourceType st, SourceID sid, TileIndex sxy, uint16 cnt, byte dit, Money fs = 0) :
		feeder_share(fs), count(cnt), days_in_transit(dit), source_id(sid), source_xy(sxy) {source_type = st;}

	/** Destroy the packet */
	~CargoPacket() { }

	/**
	 * Checks whether the cargo packet is from (exactly) the same source
	 * in time and location.
	 * @param cp the cargo packet to compare to
	 * @return true if and only if days_in_transit and source_xy are equal
	 */
	FORCEINLINE bool SameSource(const CargoPacket *cp) const
	{
		return this->source_xy == cp->source_xy && this->days_in_transit == cp->days_in_transit &&
				this->source_type == cp->source_type && this->source_id == cp->source_id;
	}
	
	CargoPacket *Split(uint new_size);
	void Merge(CargoPacket *other);

	static void InvalidateAllFrom(SourceType src_type, SourceID src);

	/* read-only accessors for the private fields */
	FORCEINLINE uint16 GetCount() const {return count;}
	FORCEINLINE Money GetFeederShare() const {return feeder_share;}
	FORCEINLINE byte GetDaysInTransit() const {return days_in_transit;}
	FORCEINLINE SourceTypeByte GetSourceType() const {return source_type;}
	FORCEINLINE SourceID GetSourceID() const {return source_id;}
	FORCEINLINE TileIndex GetSourceXY() const {return source_xy;}
};

/**
 * Iterate over all _valid_ cargo packets from the given start
 * @param var   the variable used as "iterator"
 * @param start the cargo packet ID of the first packet to iterate over
 */
#define FOR_ALL_CARGOPACKETS_FROM(var, start) FOR_ALL_ITEMS_FROM(CargoPacket, cargopacket_index, var, start)

/**
 * Iterate over all _valid_ cargo packets from the begin of the pool
 * @param var   the variable used as "iterator"
 */
#define FOR_ALL_CARGOPACKETS(var) FOR_ALL_CARGOPACKETS_FROM(var, 0)

extern const struct SaveLoad *GetGoodsDesc();
extern const struct SaveLoad *GetVehicleDescription(VehicleType vt);

enum UnloadType {
	UL_KEEP     = 0,      ///< keep cargo on vehicle
	UL_DELIVER  = 1 << 0, ///< deliver cargo
	UL_TRANSFER = 1 << 1, ///< transfer cargo
	UL_ACCEPTED = 1 << 2, ///< cargo is accepted
};

struct UnloadDescription {
	UnloadDescription(GoodsEntry * d, StationID curr, StationID next, OrderUnloadFlags f);
	GoodsEntry * dest;
	/**
	 * station we are trying to unload at now
	 */
	StationID curr_station;
	/**
	 * station the vehicle will unload at next
	 */
	StationID next_station;
	/**
	 * delivery flags
	 */
	byte flags;
};

class PacketCompare {
public:
	bool operator()(const CargoPacket *a, const CargoPacket *b) const;
};

typedef std::list<CargoPacket *> CargoPacketList;
typedef std::set<CargoPacket *, PacketCompare> CargoPacketSet;
typedef MultiMap<StationID, CargoPacket *> StationCargoPacketMap;

/**
 * Simple collection class for a list of cargo packets
 */
template<class LIST>
class CargoList {
public:
	typedef typename LIST::iterator Iterator;
	typedef typename LIST::const_iterator ConstIterator;
	friend const struct SaveLoad *GetGoodsDesc();

	/** Create the cargo list */
	FORCEINLINE CargoList() : count(0), feeder_share(0), days_in_transit(0) {}
	/** And destroy it ("frees" all cargo packets) */
	virtual ~CargoList();

	/**
	 * Returns a pointer to the cargo packet list (so you can iterate over it etc).
	 * @return pointer to the packet list
	 */
	FORCEINLINE const LIST *Packets() const { return &this->packets; }

	/**
	 * Checks whether this list is empty
	 * @return true if and only if the list is empty
	 */
	FORCEINLINE bool Empty() const { return this->packets.empty(); }

	/**
	 * Returns the number of cargo entities in this list
	 * @return the before mentioned number
	 */
	FORCEINLINE uint Count() const { return this->count; }

	/**
	 * Returns total sum of the feeder share for all packets
	 * @return the before mentioned number
	 */
	FORCEINLINE Money FeederShare() const { return this->feeder_share; }

	/**
	 * Returns source of the first cargo packet in this list
	 * @return the before mentioned source
	 */
	FORCEINLINE StationID Source() const { return Empty() ? INVALID_STATION : (*(ConstIterator(packets.begin())))->source; }

	/**
	 * Returns average number of days in transit for a cargo entity
	 * @return the before mentioned number
	 */
	FORCEINLINE uint DaysInTransit() const { return this->days_in_transit / this->count; }

	/**
	 * Truncates the cargo in this list to the given amount. It leaves the
	 * first count cargo entities and removes the rest.
	 * @param count the maximum amount of entities to be in the list after the command
	 */
	void Truncate(uint count);

	/**
	 * send all packets to the specified station and update the flow stats at the GoodsEntry accordingly
	 */
	void UpdateFlows(StationID next, GoodsEntry * ge);

	/** Make sure the cache is consistent with the actual packets */
	void ValidateCache();
	/** Invalidates the cached data and rebuild it */
	void InvalidateCache();

protected:

	LIST packets;         ///< The cargo packets in this list

	uint count;           ///< Cache for the number of cargo entities
	Money feeder_share;   ///< Cache for the feeder share
	uint days_in_transit; ///< Cache for the added number of days in transit of all packets

	uint MovePacket(StationCargoList *dest, StationID next, Iterator &it, uint cap, TileIndex load_place = INVALID_TILE);
	uint MovePacket(VehicleCargoList *dest, Iterator &it, uint cap, TileIndex load_place = INVALID_TILE);

	/*
	 * Update the cache to reflect adding of this packet.
	 * Increases count, feeder share and days_in_transit
	 * @param cp a new packet to be inserted
	 */
	void AddToCache(CargoPacket *cp);

	/*
	 * Update the cached values to reflect the removal of this packet.
	 * Decreases count, feeder share and days_in_transit
	 * @param cp Packet to be removed from cache
	 */
	void RemoveFromCache(CargoPacket *cp);

private:
	CargoPacket *MovePacket(Iterator &it, uint cap, TileIndex load_place = INVALID_TILE);
};

/**
 * unsorted CargoList
 */
class VehicleCargoList : public CargoList<CargoPacketSet> {
protected:
	UnloadType WillUnloadOld(const UnloadDescription & ul, const CargoPacket * p) const;
	UnloadType WillUnloadCargoDist(const UnloadDescription & ul, const CargoPacket * p) const;

	uint TransferPacket(Iterator &c, uint remaining_unload, GoodsEntry *dest, CargoPayment *payment, StationID curr_station);
	uint DeliverPacket(Iterator &c, uint remaining_unload, GoodsEntry *dest, CargoPayment *payment, StationID curr_station);

public:
	friend const struct SaveLoad *GetVehicleDescription(VehicleType vt);
	/**
	 * Moves the given amount of cargo from a vehicle to a station.
	 * Depending on the value of flags and dest the side effects of this function differ:
	 *  - dest->acceptance_pickup & GoodsEntry::ACCEPTANCE:
	 *                        => MoveToStation sets OUF_UNLOAD_IF_POSSIBLE in the flags
	 *                        packets are accepted here and may be unloaded and/or delivered (=destroyed);
	 *                        if not using cargodist: all packets are unloaded and delivered
	 *                        if using cargodist: only packets which have this station as final destination are unloaded and delivered
	 *                        if using cargodist: other packets may or may not be unloaded, depending on next_station
	 *                        if not set and using cargodist: packets may still be unloaded, but not delivered.
	 *  - OUFB_UNLOAD: unload all packets unconditionally;
	 *                        if OUF_UNLOAD_IF_POSSIBLE set and OUFB_TRANSFER not set: also deliver packets (no matter if using cargodist)
	 *  - OUFB_TRANSFER: don't deliver any packets;
	 *                        overrides delivering aspect of OUF_UNLOAD_IF_POSSIBLE
	 * @param dest         the destination to move the cargo to
	 * @param max_unload   the maximum amount of cargo entities to move
	 * @param flags        how to handle the moving (side effects)
	 * @param curr_station the station where the cargo currently resides
	 * @param next_station the next unloading station in the vehicle's order list
	 * @return the number of cargo entities actually moved
	 */
	uint MoveToStation(GoodsEntry * dest, uint max_unload, OrderUnloadFlags flags, StationID curr_station, StationID next_station, CargoPayment *payment);

	UnloadType WillUnload(const UnloadDescription & ul, const CargoPacket * p) const;

	/**
	 * Moves the given amount of cargo to a vehicle.
	 * @param dest         the destination to move the cargo to
	 * @param max_load     the maximum amount of cargo entities to move
	 */
	uint MoveToVehicle(VehicleCargoList *dest, uint cap, TileIndex load_place = INVALID_TILE);

	/**
	 * Appends the given cargo packet
	 * @warning After appending this packet may not exist anymore!
	 * @note Do not use the cargo packet anymore after it has been appended to this CargoList!
	 * @param cp the cargo packet to add
	 * @pre cp != NULL
	 */
	void Append(CargoPacket *cp);

	/**
	 * Ages the all cargo in this list
	 */
	void AgeCargo();

	static void InvalidateAllFrom(SourceType src_type, SourceID src);

	void SortAndCache();
};

/**
 * CargoList sorted by next hop
 */
class StationCargoList : public CargoList<StationCargoPacketMap> {
public:
	uint MoveToVehicle(VehicleCargoList *dest, uint cap, StationID next_station, TileIndex load_place);

	/**
	 * Appends the given cargo packet
	 * @warning After appending this packet may not exist anymore!
	 * @note Do not use the cargo packet anymore after it has been appended to this CargoList!
	 * @param next the next hop
	 * @param cp the cargo packet to add
	 * @pre cp != NULL
	 */
	void Append(StationID next, CargoPacket *cp);

	/**
	 * route all packets with station "to" as next hop to a different place, except "curr"
	 */
	void RerouteStalePackets(StationID curr, StationID to, GoodsEntry * ge);

	static void InvalidateAllFrom(SourceType src_type, SourceID src);

protected:
	uint MovePackets(VehicleCargoList *dest, uint cap, Iterator begin, Iterator end, TileIndex load_place);
};


#endif /* CARGOPACKET_H */
