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
#include <list>

/** Unique identifier for a single cargo packet. */
typedef uint32 CargoPacketID;
struct CargoPacket;
struct GoodsEntry;

/** Type of the pool for cargo packets. */
typedef Pool<CargoPacket, CargoPacketID, 1024, 1048576, true, false> CargoPacketPool;
/** The actual pool with cargo packets */
extern CargoPacketPool _cargopacket_pool;

template <class Tinst, class Tcont> class CargoList;
class StationCargoList;
class VehicleCargoList;
extern const struct SaveLoad *GetCargoPacketDesc();

/**
 * Container for cargo from the same location and time
 */
struct CargoPacket : CargoPacketPool::PoolItem<&_cargopacket_pool> {
private:
	Money feeder_share;         ///< Value of feeder pickup to be paid for on delivery of cargo
	uint16 count;               ///< The amount of cargo in this packet
	byte days_in_transit;       ///< Amount of days this packet has been in transit
	SourceTypeByte source_type; ///< Type of \c source_id
	SourceID source_id;         ///< Index of source, INVALID_SOURCE if unknown/invalid
	StationID source;           ///< The station where the cargo came from first
	TileIndex source_xy;        ///< The origin of the cargo (first station in feeder chain)
	TileIndex loaded_at_xy;     ///< Location where this cargo has been loaded into the vehicle

	/** The CargoList caches, thus needs to know about it. */
	template <class Tinst, class Tcont> friend class CargoList;
	friend class VehicleCargoList;
	friend class ReservationList;
	friend class StationCargoList;
	/** We want this to be saved, right? */
	friend const struct SaveLoad *GetCargoPacketDesc();
public:
	/** Maximum number of items in a single cargo packet. */
	static const uint16 MAX_COUNT = UINT16_MAX;

	/**
	 * Create a new packet for savegame loading.
	 */
	CargoPacket();

	/**
	 * Creates a new cargo packet
	 * @param source      the source station of the packet
	 * @param source_xy   the source location of the packet
	 * @param count       the number of cargo entities to put in this packet
	 * @param source_type the 'type' of source the packet comes from (for subsidies)
	 * @param source_id   the actual source of the packet (for subsidies)
	 * @pre count != 0
	 */
	CargoPacket(StationID source, TileIndex source_xy, uint16 count, SourceType source_type, SourceID source_id);

	/**
	 * Creates a new cargo packet. Initializes the fields that cannot be changed later.
	 * Used when loading or splitting packets.
	 * @param count           the number of cargo entities to put in this packet
	 * @param days_in_transit number of days the cargo has been in transit
	 * @param source          the station the cargo was initially loaded
	 * @param source_xy       the station location the cargo was initially loaded
	 * @param loaded_at_xy    the location the cargo was loaded last
	 * @param feeder_share    feeder share the packet has already accumulated
	 * @param source_type     the 'type' of source the packet comes from (for subsidies)
	 * @param source_id       the actual source of the packet (for subsidies)
	 */
	CargoPacket(uint16 count, byte days_in_transit, StationID source, TileIndex source_xy, TileIndex loaded_at_xy, Money feeder_share = 0, SourceType source_type = ST_INDUSTRY, SourceID source_id = INVALID_SOURCE);

	/** Destroy the packet */
	~CargoPacket() { }


	/**
	 * Gets the number of 'items' in this packet.
	 * @return the item count
	 */
	FORCEINLINE uint16 Count() const
	{
		return this->count;
	}

	/**
	 * Gets the amount of money already paid to earlier vehicles in
	 * the feeder chain.
	 * @return the feeder share
	 */
	FORCEINLINE Money FeederShare() const
	{
		return this->feeder_share;
	}

	/**
	 * Gets the number of days this cargo has been in transit.
	 * This number isn't really in days, but in 2.5 days (185 ticks) and
	 * it is capped at 255.
	 * @return the length this cargo has been in transit
	 */
	FORCEINLINE byte DaysInTransit() const
	{
		return this->days_in_transit;
	}

	/**
	 * Gets the type of the cargo's source. industry, town or head quarter
	 * @return the source type
	 */
	FORCEINLINE SourceType SourceSubsidyType() const
	{
		return this->source_type;
	}

	/**
	 * Gets the ID of the cargo's source. An IndustryID, TownID or CompanyID
	 * @return the source ID
	 */
	FORCEINLINE SourceID SourceSubsidyID() const
	{
		return this->source_id;
	}

	/**
	 * Gets the ID of the station where the cargo was loaded for the first time
	 * @return the StationID
	 */
	FORCEINLINE SourceID SourceStation() const
	{
		return this->source;
	}

	/**
	 * Gets the coordinates of the cargo's source station
	 * @return the source station's coordinates
	 */
	FORCEINLINE TileIndex SourceStationXY() const
	{
		return this->source_xy;
	}

	/**
	 * Gets the coordinates of the cargo's last loading station
	 * @return the last loading station's coordinates
	 */
	FORCEINLINE TileIndex LoadedAtXY() const
	{
		return this->loaded_at_xy;
	}

	CargoPacket *Split(uint new_size);
	void Merge(CargoPacket *other);

	static void InvalidateAllFrom(SourceType src_type, SourceID src);
	static void InvalidateAllFrom(StationID sid);
	static void AfterLoad();
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

enum UnloadType {
	UL_KEEP     = 0,      ///< keep cargo on vehicle
	UL_DELIVER  = 1 << 0, ///< deliver cargo
	UL_TRANSFER = 1 << 1, ///< transfer cargo
	UL_ACCEPTED = 1 << 2, ///< cargo is accepted
};

class StationCargoList;
class VehicleCargoList;

/**
 * Simple collection class for a list of cargo packets
 * @tparam Tinst The actual instantation of this cargo list
 */
template <class Tinst, class Tcont>
class CargoList {
public:
	/** The iterator for our container */
	typedef typename Tcont::iterator Iterator;
	/** The const iterator for our container */
	typedef typename Tcont::const_iterator ConstIterator;
	/** The reverse iterator for our container */
	typedef typename Tcont::reverse_iterator ReverseIterator;
	/** The const reverse iterator for our container */
	typedef typename Tcont::const_reverse_iterator ConstReverseIterator;

protected:
	uint count;                 ///< Cache for the number of cargo entities
	uint cargo_days_in_transit; ///< Cache for the sum of number of days in transit of each entity; comparable to man-hours

	Tcont packets;               ///< The cargo packets in this list

	/**
	 * Update the cache to reflect adding of this packet.
	 * Increases count and days_in_transit
	 * @param cp a new packet to be inserted
	 */
	void AddToCache(const CargoPacket *cp);

	/**
	 * Update the cached values to reflect the removal of this packet.
	 * Decreases count and days_in_transit
	 * @param cp Packet to be removed from cache
	 */
	void RemoveFromCache(const CargoPacket *cp);

	CargoPacket *MovePacket(Iterator &it, uint cap, TileIndex load_place = INVALID_TILE);

	uint MovePacket(StationCargoList *dest, StationID next, Iterator &it, uint cap);

	uint MovePacket(VehicleCargoList *dest, Iterator &it, uint cap, TileIndex load_place = INVALID_TILE, bool reserved = false);

public:
	/** Create the cargo list */
	CargoList() {}
	/** And destroy it ("frees" all cargo packets) */
	~CargoList();

	/**
	 * Returns a pointer to the cargo packet list (so you can iterate over it etc).
	 * @return pointer to the packet list
	 */
	FORCEINLINE const Tcont *Packets() const
	{
		return &this->packets;
	}

	/**
	 * Checks whether this list is empty
	 * @return true if and only if the list is empty
	 */
	FORCEINLINE bool Empty() const
	{
		return this->count == 0;
	}

	/**
	 * Returns the number of cargo entities in this list
	 * @return the before mentioned number
	 */
	FORCEINLINE uint Count() const
	{
		return this->count;
	}

	/**
	 * Returns source of the first cargo packet in this list
	 * @return the before mentioned source
	 */
	FORCEINLINE StationID Source() const
	{
		return this->Empty() ? INVALID_STATION : (*(ConstIterator(packets.begin())))->source;
	}

	/**
	 * Returns average number of days in transit for a cargo entity
	 * @return the before mentioned number
	 */
	FORCEINLINE uint DaysInTransit() const
	{
		return this->count == 0 ? 0 : this->cargo_days_in_transit / this->count;
	}

	/**
	 * Truncates the cargo in this list to the given amount. It leaves the
	 * first count cargo entities and removes the rest.
	 * @param max_remaining the maximum amount of entities to be in the list after the command
	 */
	void Truncate(uint max_remaining);

	/** Invalidates the cached data and rebuild it */
	void InvalidateCache();
};

typedef std::list<CargoPacket *> CargoPacketList;

/**
 * CargoList that is used for vehicles.
 */
class VehicleCargoList : public CargoList<VehicleCargoList, CargoPacketList> {
protected:
	static UnloadType WillUnloadOld(byte flags, StationID curr_station, StationID source);
	static UnloadType WillUnloadCargoDist(byte flags, StationID curr_station, StationID next_station, StationID via, StationID source);

	uint TransferPacket(Iterator &c, uint remaining_unload, GoodsEntry *dest, CargoPayment *payment, StationID next);
	uint DeliverPacket(Iterator &c, uint remaining_unload, CargoPayment *payment);
	uint KeepPacket(Iterator &c);

	/** The (direct) parent of this class */
	typedef CargoList<VehicleCargoList, CargoPacketList> Parent;

	CargoPacketList reserved; ///< The packets reserved for unloading in this list
	Money feeder_share;       ///< Cache for the feeder share
	uint reserved_count;      ///< count(reserved)

	/**
	 * Update the cache to reflect adding of this packet.
	 * Increases count, feeder share and days_in_transit
	 * @param cp a new packet to be inserted
	 */
	void AddToCache(const CargoPacket *cp);

	/**
	 * Update the cached values to reflect the removal of this packet.
	 * Decreases count, feeder share and days_in_transit
	 * @param cp Packet to be removed from cache
	 */
	void RemoveFromCache(const CargoPacket *cp);

	static byte GetUnloadFlags(GoodsEntry *dest, OrderUnloadFlags order_flags);

public:
	/** The super class ought to know what it's doing */
	friend class CargoList<VehicleCargoList, CargoPacketList>;
	/** The vehicles have a cargo list (and we want that saved). */
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

	~VehicleCargoList();

	/**
	 * Returns total sum of the feeder share for all packets
	 * @return the before mentioned number
	 */
	FORCEINLINE Money FeederShare() const
	{
		return this->feeder_share;
	}

	/**
	 * tries to merge the packet with another one in the packets list.
	 * if no fitting packet is found, appends it.
	 * @param cp the packet to be inserted
	 */
	void MergeOrPush(CargoPacket *cp);

	/**
	 * Appends the given cargo packet
	 * @warning After appending this packet may not exist anymore!
	 * @note Do not use the cargo packet anymore after it has been appended to this CargoList!
	 * @param cp the cargo packet to add
	 * @param check_merge if true, check existing packets in the list for mergability
	 * @pre cp != NULL
	 */
	void Append(CargoPacket *cp);

	/**
	 * Returns sum of cargo on board the vehicle (ie not only
	 * reserved)
	 * @return cargo on board the vehicle
	 */
	FORCEINLINE uint OnboardCount() const
	{
		return this->count - this->reserved_count;
	}

	/**
	 * Returns sum of cargo reserved for the vehicle
	 * @return cargo reserved for the vehicle
	 */
	FORCEINLINE uint ReservedCount() const
	{
		return this->reserved_count;
	}

	/**
	 * Returns a pointer to the reserved cargo list (so you can iterate over it etc).
	 * @return pointer to the reserved list
	 */
	FORCEINLINE const CargoPacketList *Reserved() const
	{
		return &this->reserved;
	}

	/**
	 * Reserves a packet for later loading
	 */
	void Reserve(CargoPacket *cp);

	/**
	 * Returns all reserved cargo to the station
	 */
	void Unreserve(StationID next, StationCargoList *dest);

	/**
	 * load packets from the reserved list
	 * @params count the number of cargo to load
	 * @return true if there are still packets that might be loaded from the reservation list
	 */
	uint LoadReserved(uint count);

	/**
	 * swap the reserved and packets lists when starting to load cargo.
	 * @pre this->packets.empty()
	 */
	void SwapReserved();

	/**
	 * Ages the all cargo in this list
	 */
	void AgeCargo();

	/** Invalidates the cached data and rebuild it */
	void InvalidateCache();

	/**
	 * Moves the given amount of cargo to another vehicle (during autoreplace).
	 * @param dest         the destination to move the cargo to
	 * @param max_load     the maximum amount of cargo entities to move
	 */
	uint MoveTo(VehicleCargoList *dest, uint cap);

	/**
	 * Are two the two CargoPackets mergeable in the context of
	 * a list of CargoPackets for a Vehicle?
	 * @param cp1 the first CargoPacket
	 * @param cp2 the second CargoPacket
	 * @return true if they are mergeable
	 */
	static bool AreMergable(const CargoPacket *cp1, const CargoPacket *cp2)
	{
		return cp1->source_xy    == cp2->source_xy &&
				cp1->days_in_transit == cp2->days_in_transit &&
				cp1->source_type     == cp2->source_type &&
				cp1->source_id       == cp2->source_id &&
				cp1->loaded_at_xy    == cp2->loaded_at_xy;
	}
};

typedef MultiMap<StationID, CargoPacket *> StationCargoPacketMap;

/**
 * CargoList that is used for stations.
 */
class StationCargoList : public CargoList<StationCargoList, StationCargoPacketMap> {
public:
	/** The super class ought to know what it's doing */
	friend class CargoList<StationCargoList, StationCargoPacketMap>;
	/** The stations, via GoodsEntry, have a CargoList. */
	friend const struct SaveLoad *GetGoodsDesc();

	/**
	 * Are two the two CargoPackets mergeable in the context of
	 * a list of CargoPackets for a Vehicle?
	 * @param cp1 the first CargoPacket
	 * @param cp2 the second CargoPacket
	 * @return true if they are mergeable
	 */
	static bool AreMergable(const CargoPacket *cp1, const CargoPacket *cp2)
	{
		return cp1->source_xy    == cp2->source_xy &&
				cp1->days_in_transit == cp2->days_in_transit &&
				cp1->source_type     == cp2->source_type &&
				cp1->source_id       == cp2->source_id;
	}

	uint MoveTo(VehicleCargoList *dest, uint cap, StationID next_station, TileIndex load_place = INVALID_TILE, bool reserve = false);

	/**
	 * Appends the given cargo packet to the range of packets with the same next station
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
	uint MovePackets(VehicleCargoList *dest, uint cap, Iterator begin, Iterator end, TileIndex load_place, bool reserve);
};

#endif /* CARGOPACKET_H */
