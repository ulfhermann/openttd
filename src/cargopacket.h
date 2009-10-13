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
#include "cargo_type.h"
#include "vehicle_type.h"
#include <list>
#include <set>

/** Unique identifier for a single cargo packet. */
typedef uint32 CargoPacketID;
struct CargoPacket;

/** Type of the pool for cargo packets. */
typedef Pool<CargoPacket, CargoPacketID, 1024, 1048576> CargoPacketPool;
/** The actual pool with cargo packets */
extern CargoPacketPool _cargopacket_pool;

template<class Tlist> class CargoList;
extern const struct SaveLoad *GetCargoPacketDesc();

/**
 * Container for cargo from the same location and time
 */
struct CargoPacket : CargoPacketPool::PoolItem<&_cargopacket_pool> {
private:
	/* Variables used by the CargoList cache. Only let them be modified via
	 * the proper accessor functions and/or CargoList itself. */
	Money feeder_share;     ///< Value of feeder pickup to be paid for on delivery of cargo
	uint16 count;           ///< The amount of cargo in this packet
	byte days_in_transit;   ///< Amount of days this packet has been in transit

	/* Variables used for sorting cargo lists. These need to be private, too. */
	SourceTypeByte source_type; ///< Type of #source_id
	SourceID source_id;         ///< Index of source, INVALID_SOURCE if unknown/invalid
	TileIndex source_xy;        ///< The origin of the cargo (first station in feeder chain)

	/** The CargoList caches, thus needs to know about it. */
	template<class Tlist> friend class CargoList;
	friend class VehicleCargoList;
	friend class StationCargoList;
	/** We want this to be saved, right? */
	friend const struct SaveLoad *GetCargoPacketDesc();
	/** AfterLoadGame can change the sorting attributes in station cargo. */
	friend bool AfterLoadGame();
public:
	/** Maximum number of items in a single cargo packet. */
	static const uint16 MAX_COUNT = UINT16_MAX;

	StationID source;           ///< The station where the cargo came from first
	TileIndex loaded_at_xy;     ///< Location where this cargo has been loaded into the vehicle

	/**
	 * Creates a new cargo packet at a defined source.
	 * @param source      the source of the packet
	 * @param count       the number of cargo entities to put in this packet
	 * @param source_type the 'type' of source the packet comes from (for subsidies)
	 * @param source_id   the actual source of the packet (for subsidies)
	 * @pre count != 0 || source == INVALID_STATION
	 */
	CargoPacket(StationID source, uint16 count, SourceType source_type, SourceID source_id);

	/**
	 * Creates a new cargo packet. Initializes the fields that cannot be changed later.
	 * Used when loading or splitting packets.
	 * @param count           the number of cargo entities to put in this packet
	 * @param days_in_transit number of days the cargo has been in transit
	 * @param feeder_share    feeder share the packet has already accumulated
	 * @param source_xy       the tile index of the source station
	 * @param source_type     the 'type' of source the packet comes from (for subsidies)
	 * @param source_id       the actual source of the packet (for subsidies)
	 */
	CargoPacket(uint16 count = 0, byte days_in_transit = 0, Money feeder_share = 0, TileIndex source_xy = INVALID_TILE, SourceType source_type = ST_INDUSTRY, SourceID source_id = INVALID_SOURCE);

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
	FORCEINLINE SourceTypeByte GetSourceType() const
	{
		return source_type;
	}

	/**
	 * Gets the ID of the cargo's source. An IndustryID, TownID or CompanyID
	 * @return the source ID
	 */
	FORCEINLINE SourceID GetSourceID() const
	{
		return source_id;
	}

	/**
	 * Gets the coordinates of the cargo's source station
	 * @return the source station's coordinates
	 */
	FORCEINLINE TileIndex GetSourceXY() const
	{
		return source_xy;
	}

	/**
	 * Checks whether the cargo packet is from (exactly) the same source
	 * in time and location.
	 * @param cp the cargo packet to compare to
	 * @return true if and only if days_in_transit and source_xy are equal
	 */
	FORCEINLINE bool SameSource(const CargoPacket *cp) const
	{
		return this->source_xy    == cp->source_xy &&
				this->days_in_transit == cp->days_in_transit &&
				this->source_type     == cp->source_type &&
				this->source_id       == cp->source_id;
	}

	void Merge(CargoPacket *other);

	static void InvalidateAllFrom(SourceType src_type, SourceID src);
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
extern const SaveLoad *GetVehicleDescription(VehicleType vt);

/**
 * Simple collection class for a list of cargo packets
 * @tparam Tlist the actual container class to hold the cargo packets.
 */
template <class Tlist> class CargoList
{
public:
	/** iterator of the packet container */
	typedef typename Tlist::iterator Iterator;
	/** const iterator of the packet container */
	typedef typename Tlist::const_iterator ConstIterator;

	/** Kind of actions that could be done with packets on move */
	enum MoveToAction {
		MTA_FINAL_DELIVERY, ///< "Deliver" the packet to the final destination, i.e. destroy the packet
		MTA_CARGO_LOAD,     ///< Load the packet onto a vehicle, i.e. set the last loaded station ID
		MTA_TRANSFER,       ///< The cargo is moved as part of a transfer
		MTA_UNLOAD,         ///< The cargo is moved as part of a forced unload
	};

protected:
	Money feeder_share;         ///< Cache for the feeder share
	uint count;                 ///< Cache for the number of cargo entities
	uint cargo_days_in_transit; ///< Cache for the sum of number of days in transit of each entity; comparable to man-hours

	Tlist packets;               ///< The cargo packets in this list

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

public:
	/** The stations, via GoodsEntry, have a CargoList. */
	friend const struct SaveLoad *GetGoodsDesc();
	/** The vehicles have a cargo list too. */
	friend const SaveLoad *GetVehicleDescription(VehicleType vt);

	/** Create the cargo list */
	FORCEINLINE CargoList() { this->InvalidateCache(); }
	/** And destroy it ("frees" all cargo packets) */
	virtual ~CargoList();

	/**
	 * Returns a pointer to the cargo packet list (so you can iterate over it etc).
	 * @return pointer to the packet list
	 */
	FORCEINLINE const Tlist *Packets() const
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
	 * Returns total sum of the feeder share for all packets
	 * @return the before mentioned number
	 */
	FORCEINLINE Money FeederShare() const
	{
		return this->feeder_share;
	}

	/**
	 * Returns source of the first cargo packet in this list
	 * @return the before mentioned source
	 */
	FORCEINLINE StationID Source() const
	{
		return this->Empty() ? INVALID_STATION : (*(this->packets.begin()))->source;
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

	/**
	 * Moves the given amount of cargo to another list.
	 * Depending on the value of mta the side effects of this function differ:
	 *  - MTA_FINAL_DELIVERY: destroys the packets that do not originate from a specific station
	 *  - MTA_CARGO_LOAD:     sets the loaded_at_xy value of the moved packets
	 *  - MTA_TRANSFER:       just move without side effects
	 *  - MTA_UNLOAD:         just move without side effects
	 * @tparam Tother_list type of the destination list. Tested with StationCargoList and VehicleCargoList.
	 * @param dest  the destination to move the cargo to
	 * @param count the amount of cargo entities to move
	 * @param mta   how to handle the moving (side effects)
	 * @param data  Depending on mta the data of this variable differs:
	 *              - MTA_FINAL_DELIVERY - station ID of packet's origin not to remove
	 *              - MTA_CARGO_LOAD     - station's tile index of load
	 *              - MTA_TRANSFER       - unused
	 *              - MTA_UNLOAD         - unused
	 * @param payment The payment helper
	 *
	 * @pre mta == MTA_FINAL_DELIVERY || dest != NULL
	 * @pre mta == MTA_UNLOAD || mta == MTA_CARGO_LOAD || payment != NULL
	 * @return true if there are still packets that might be moved from this cargo list
	 */
	template <class Tother_list>
	bool MoveTo(Tother_list *dest, uint count, MoveToAction mta, CargoPayment *payment, uint data = 0);

	/** Invalidates the cached data and rebuild it */
	void InvalidateCache();
};

class PacketCompare {
public:
	bool operator()(const CargoPacket *a, const CargoPacket *b) const;
};

typedef std::set<CargoPacket *, PacketCompare> CargoPacketSet;

/**
 * CargoList sorted by the same principles as SameSource
 */
class VehicleCargoList : public CargoList<CargoPacketSet> {
public:
	friend const struct SaveLoad *GetVehicleDescription(VehicleType vt);

	/**
	 * Inserts the given cargo packet into the set (not necessarily at the end).
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

typedef std::list<CargoPacket *> CargoPacketList;

/**
 * unsorted CargoList
 */
class StationCargoList : public CargoList<CargoPacketList> {
public:

	/**
	 * Appends the given cargo packet to the end of the list.
	 * @warning After appending this packet may not exist anymore!
	 * @note Do not use the cargo packet anymore after it has been appended to this CargoList!
	 * @param cp the cargo packet to add
	 * @pre cp != NULL
	 */
	virtual void Append(CargoPacket *cp);

	static void InvalidateAllFrom(SourceType src_type, SourceID src);
};

#endif /* CARGOPACKET_H */
