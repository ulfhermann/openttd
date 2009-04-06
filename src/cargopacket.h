/* $Id$ */

/** @file cargopacket.h Base class for cargo packets. */

#ifndef CARGOPACKET_H
#define CARGOPACKET_H

#include "oldpool.h"
#include "economy_type.h"
#include "tile_type.h"
#include "station_type.h"
#include <list>
#include <map>

typedef uint32 CargoPacketID;
struct CargoPacket;
struct GoodsEntry;

/** We want to use a pool */
DECLARE_OLD_POOL(CargoPacket, CargoPacket, 10, 1000)


/**
 * Container for cargo from the same location and time
 */
struct CargoPacket : PoolItem<CargoPacket, CargoPacketID, &_CargoPacket_pool> {
	Money feeder_share;     ///< Value of feeder pickup to be paid for on delivery of cargo
	TileIndex source_xy;    ///< The origin of the cargo (first station in feeder chain)
	TileIndex loaded_at_xy; ///< Location where this cargo has been loaded into the vehicle
	StationID source;       ///< The station where the cargo came from first
	StationID next;         ///< The next hop where this cargo is trying to go

	uint16 count;           ///< The amount of cargo in this packet
	byte days_in_transit;   ///< Amount of days this packet has been in transit
	bool paid_for;          ///< Have we been paid for this cargo packet?

	/**
	 * Creates a new cargo packet
	 * @param source the source of the packet
	 * @param count  the number of cargo entities to put in this packet
	 * @pre count != 0 || source == INVALID_STATION
	 */
	CargoPacket(StationID source = INVALID_STATION, StationID next = INVALID_STATION, uint16 count = 0);

	/** Destroy the packet */
	virtual ~CargoPacket();


	/**
	 * Is this a valid cargo packet ?
	 * @return true if and only it is valid
	 */
	inline bool IsValid() const { return this->count != 0; }

	/**
	 * Checks whether the cargo packet is from (exactly) the same source
	 * in time and location.
	 * @param cp the cargo packet to compare to
	 * @return true if and only if days_in_transit and source_xy are equal
	 */
	bool SameSource(const CargoPacket *cp) const;

	CargoPacket * Split(uint new_size);
};

/**
 * Iterate over all _valid_ cargo packets from the given start
 * @param cp    the variable used as "iterator"
 * @param start the cargo packet ID of the first packet to iterate over
 */
#define FOR_ALL_CARGOPACKETS_FROM(cp, start) for (cp = GetCargoPacket(start); cp != NULL; cp = (cp->index + 1U < GetCargoPacketPoolSize()) ? GetCargoPacket(cp->index + 1U) : NULL) if (cp->IsValid())

/**
 * Iterate over all _valid_ cargo packets from the begin of the pool
 * @param cp    the variable used as "iterator"
 */
#define FOR_ALL_CARGOPACKETS(cp) FOR_ALL_CARGOPACKETS_FROM(cp, 0)

extern void SaveLoad_STNS(Station *st);

struct UnloadDescription {
	UnloadDescription(GoodsEntry * d, StationID curr, StationID next, uint f);
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
	uint flags;
};

enum UnloadFlags {
	UL_KEEP     = 0,      ///< keep cargo on vehicle
	UL_DELIVER  = 1 << 0, ///< deliver cargo
	UL_TRANSFER = 1 << 1, ///< transfer cargo
	UL_PLANNED  = 1 << 2, ///< transaction takes place as planned
	UL_ACCEPTED = 1 << 3, ///< cargo is accepted
};

/**
 * Simple collection class for a list of cargo packets
 */
class CargoList {
public:
	/** List of cargo packets */
	typedef std::list<CargoPacket *> List;

private:
	List packets;         ///< The cargo packets in this list

	bool empty;           ///< Cache for whether this list is empty or not
	uint count;           ///< Cache for the number of cargo entities
	bool unpaid_cargo;    ///< Cache for the unpaid cargo
	Money feeder_share;   ///< Cache for the feeder share
	StationID source;     ///< Cache for the source of the packet
	uint days_in_transit; ///< Cache for the number of days in transit

	void DeliverPacket(List::iterator & c, uint & remaining_unload);
	CargoPacket * TransferPacket(List::iterator & c, uint & remaining_unload, GoodsEntry * dest);
	uint WillUnloadOld(const UnloadDescription & ul, const CargoPacket * p) const;
	uint WillUnloadCargoDist(const UnloadDescription & ul, const CargoPacket * p) const;
	uint LoadPackets(List * dest, uint cap, StationID next_station, List * rejected = NULL, TileIndex load_place = INVALID_TILE);

public:
	friend void SaveLoad_STNS(Station *st);

	/** Create the cargo list */
	CargoList() { this->InvalidateCache(); }
	/** And destroy it ("frees" all cargo packets) */
	~CargoList();

	/**
	 * Returns a pointer to the cargo packet list (so you can iterate over it etc).
	 * @return pointer to the packet list
	 */
	const CargoList::List *Packets() const;

	/**
	 * Ages the all cargo in this list
	 */
	void AgeCargo();

	/**
	 * Checks whether this list is empty
	 * @return true if and only if the list is empty
	 */
	bool Empty() const;

	/**
	 * Returns the number of cargo entities in this list
	 * @return the before mentioned number
	 */
	uint Count() const;

	/**
	 * Is there some cargo that has not been paid for?
	 * @return true if and only if there is such a cargo
	 */
	bool UnpaidCargo() const;

	/**
	 * Returns total sum of the feeder share for all packets
	 * @return the before mentioned number
	 */
	Money FeederShare() const;

	/**
	 * Returns source of the first cargo packet in this list
	 * @return the before mentioned source
	 */
	StationID Source() const;

	/**
	 * Returns average number of days in transit for a cargo entity
	 * @return the before mentioned number
	 */
	uint DaysInTransit() const;


	/**
	 * Appends the given cargo packet
	 * @warning After appending this packet may not exist anymore!
	 * @note Do not use the cargo packet anymore after it has been appended to this CargoList!
	 * @param cp the cargo packet to add
	 * @pre cp != NULL
	 */
	void Append(CargoPacket *cp);

	/**
	 * imports a complete CargoList by splicing its elements into this one
	 * runs in constant time.
	 */
	void Import(List & list);

	/**
	 * Truncates the cargo in this list to the given amount. It leaves the
	 * first count cargo entities and removes the rest.
	 * @param count the maximum amount of entities to be in the list after the command
	 */
	void Truncate(uint count);

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
	uint MoveToStation(GoodsEntry * dest, uint max_unload, uint flags, StationID curr_station, StationID next_station);

	uint WillUnload(const UnloadDescription & ul, const CargoPacket * p) const;

	/**
	 * Moves the given amount of cargo to a vehicle.
	 * @param dest         the destination to move the cargo to
	 * @param max_load     the maximum amount of cargo entities to move
	 * @param force_load   if set, move cargo unconditionally,
	 *                     else only move if CargoPacket::next==next_station or CargoPacket::next==INVALID_STATION
	 * @param load_place   The place where the loading takes/took place;
	 *                     if load_place != INVALID_TILE CargoPacket::loaded_at_xy will be set accordingly
	 */
	uint MoveToVehicle(CargoList *dest, uint max_load, StationID next_station = INVALID_STATION, List * rejected = NULL, TileIndex load_place = INVALID_TILE);

	void ReleaseStalePackets(StationID to);

	void ReservePacketsForLoading(List * reserved, uint cap, StationID next_station, List * rejected)
		{LoadPackets(reserved, cap, next_station, rejected);}

	/** Invalidates the cached data and rebuild it */
	void InvalidateCache();
};

typedef std::map<CargoID, CargoList::List> CargoReservation;

#endif /* CARGOPACKET_H */
