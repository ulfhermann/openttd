/* $Id$ */

/** @file cargopacket.h Base class for cargo packets. */

#ifndef CARGOPACKET_H
#define CARGOPACKET_H

#include "core/pool_type.hpp"
#include "economy_type.h"
#include "tile_type.h"
#include "station_type.h"
#include "cargo_type.h"
#include <list>
#include <set>

typedef uint32 CargoPacketID;
struct CargoPacket;

/** We want to use a pool */
typedef Pool<CargoPacket, CargoPacketID, 1024, 1048576> CargoPacketPool;
extern CargoPacketPool _cargopacket_pool;

class CargoList;

/**
 * Container for cargo from the same location and time
 */
struct CargoPacket : CargoPacketPool::PoolItem<&_cargopacket_pool> {
private:
	/* These fields are all involved in the cargo list's cache or in the append positions set.
	 * They can only be modified by CargoList which knows about both.
	 */
	SourceTypeByte source_type; ///< Type of #source_id
	SourceID source_id;         ///< Index of source, INVALID_SOURCE if unknown/invalid
	TileIndex source_xy;        ///< The origin of the cargo (first station in feeder chain)

	Money feeder_share;         ///< Value of feeder pickup to be paid for on delivery of cargo
	uint16 count;               ///< The amount of cargo in this packet
	byte days_in_transit;       ///< Amount of days this packet has been in transit
public:
	friend class CargoList;
	friend const struct SaveLoad *GetCargoPacketDesc();

	static const uint16 MAX_COUNT = 65535;

	/*
	 * source and loaded_at_xy neither mess with the cargo list's cache nor with the append positions set
	 * so it's OK to modify them.
	 */
	StationID source;       ///< The station where the cargo came from first
	TileIndex loaded_at_xy; ///< Location where this cargo has been loaded into the vehicle
	/**
	 * Creates a new cargo packet
	 * @param source          the source station of the packet
	 * @param count           the number of cargo entities to put in this packet
	 * @param source_type     the type of the packet's source
	 * @param source_id       the number of the packet's source
	 * @param days_in_transit the time the packet has already travelled
	 * @param feeder_share    the packet's feeder share
	 * @param source_xy       the place where the packet was generated
	 * @param loaded_at_xy    the place where the packet was loaded
	 * @pre count != 0 || source == INVALID_STATION
	 */
	CargoPacket(StationID source = INVALID_STATION, uint16 count = 0, SourceType source_type = ST_INDUSTRY, SourceID source_id = INVALID_SOURCE,
			byte days_in_transit = 0, Money feeder_share = 0, TileIndex source_xy = 0, TileIndex loaded_at_xy = 0);

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

	static void InvalidateAllFrom(SourceType src_type, SourceID src);

	/* read-only accessors for the private fields */
	FORCEINLINE uint16 GetCount() const {return count;}
	FORCEINLINE TileIndex GetSourceXY() const {return source_xy;}
	FORCEINLINE byte GetDaysInTransit() const {return days_in_transit;}
	FORCEINLINE SourceTypeByte GetSourceType() const {return source_type;}
	FORCEINLINE SourceID GetSourceID() const {return source_id;}
	FORCEINLINE uint GetFeederShare() const {return feeder_share;}
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

class CargoSorter {
public:
	bool operator()(const CargoPacket *cp1, const CargoPacket *cp2) const;
};

/**
 * Simple collection class for a list of cargo packets
 */
class CargoList {
public:
	/** List of cargo packets */
	typedef std::list<CargoPacket *> List;

	/** Kind of actions that could be done with packets on move */
	enum MoveToAction {
		MTA_FINAL_DELIVERY, ///< "Deliver" the packet to the final destination, i.e. destroy the packet
		MTA_CARGO_LOAD,     ///< Load the packet onto a vehicle, i.e. set the last loaded station ID
		MTA_TRANSFER,       ///< The cargo is moved as part of a transfer
		MTA_UNLOAD,         ///< The cargo is moved as part of a forced unload
	};

private:
	typedef std::set<CargoPacket *, CargoSorter> AppendMap;

	List packets;               ///< The cargo packets in this list

	/* cache for the packets last appended to.
	 * Using this packets to be appended to can be found in O(log n) instead of O(n) time.
	 * Thus Append() can be used whenever there is cargo to insert, reducing the number of packets.
	 */
	AppendMap append_positions; ///< Cached positions of last Appends
	uint count;                 ///< Cache for the number of cargo entities
	Money feeder_share;         ///< Cache for the feeder share
	uint days_in_transit;       ///< Cache for the added number of days in transit of all packets

	/*
	 * Merge packet "insert" into packet "in_list" and updates the cached values.
	 * This doesn't invalidate the append_positions as those don't refer to the fields changed.
	 *
	 * @param in_list a packet from this cargolist
	 * @param a new packet to be inserted
	 */
	void Merge(CargoPacket *in_list, CargoPacket *insert);

	/*
	 * update the cached values to reflect the removal of this packet. Decreases cound, feeder share and days_in_transit
	 * and erases the packet from append_positions
	 * @param cp Packet to be removed from cache
	 */
	void RemoveFromCache(CargoPacket *cp);

public:
	friend const struct SaveLoad *GetGoodsDesc();

	void InvalidateAppend() {append_positions.clear();}

	/** Create the cargo list */
	FORCEINLINE CargoList() { this->InvalidateCache(); }
	/** And destroy it ("frees" all cargo packets) */
	~CargoList();

	/**
	 * Returns a pointer to the cargo packet list (so you can iterate over it etc).
	 * @return pointer to the packet list
	 */
	FORCEINLINE const CargoList::List *Packets() const { return &this->packets; }

	/**
	 * Ages the all cargo in this list
	 */
	void AgeCargo();

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
	FORCEINLINE StationID Source() const { return Empty() ? INVALID_STATION : this->packets.front()->source; }

	/**
	 * Returns average number of days in transit for a cargo entity
	 * @return the before mentioned number
	 */
	FORCEINLINE uint DaysInTransit() const { return this->days_in_transit / this->count; }


	/**
	 * Appends the given cargo packet
	 * @warning After appending this packet may not exist anymore!
	 * @note Do not use the cargo packet anymore after it has been appended to this CargoList!
	 * @param cp the cargo packet to add
	 * @pre cp != NULL
	 */
	void Append(CargoPacket *cp);

	/**
	 * Truncates the cargo in this list to the given amount. It leaves the
	 * first count cargo entities and removes the rest.
	 * @param count the maximum amount of entities to be in the list after the command
	 */
	void Truncate(uint count);

	/**
	 * Moves the given amount of cargo to another list.
	 * Depending on the value of mta the side effects of this function differ:
	 *  - MTA_FINAL_DELIVERY: destroys the packets that do not originate from a specific station
	 *  - MTA_CARGO_LOAD:     sets the loaded_at_xy value of the moved packets
	 *  - MTA_TRANSFER:       just move without side effects
	 *  - MTA_UNLOAD:         just move without side effects
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
	bool MoveTo(CargoList *dest, uint count, CargoList::MoveToAction mta, CargoPayment *payment, uint data = 0);

	/** Invalidates the cached data and rebuild it */
	void InvalidateCache();
};

#endif /* CARGOPACKET_H */
