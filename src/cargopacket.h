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
#include "station_type.h"
#include "order_type.h"
#include "cargo_type.h"
#include "cargotype.h"
#include "vehicle_type.h"
#include "core/multimap.hpp"
#include <list>

/** Unique identifier for a single cargo packet. */
typedef uint32 CargoPacketID;
struct CargoPacket;

/** Type of the pool for cargo packets for a little over 16 million packets. */
typedef Pool<CargoPacket, CargoPacketID, 1024, 0xFFF000, true, false> CargoPacketPool;
/** The actual pool with cargo packets. */
extern CargoPacketPool _cargopacket_pool;

template <class Tinst, class Tcont> class CargoList;
class StationCargoList; // forward-declare, so we can use it in VehicleCargoList::Unreserve
class VehicleCargoList; // forward-declare, so we can use it in CargoList::MovePacket
extern const struct SaveLoad *GetCargoPacketDesc();

/**
 * Container for cargo from the same location and time.
 */
struct CargoPacket : CargoPacketPool::PoolItem<&_cargopacket_pool> {
private:
	Money feeder_share;         ///< Value of feeder pickup to be paid for on delivery of cargo.
	uint16 count;               ///< The amount of cargo in this packet.
	byte days_in_transit;       ///< Amount of days this packet has been in transit.
	SourceTypeByte source_type; ///< Type of \c source_id.
	SourceID source_id;         ///< Index of source, INVALID_SOURCE if unknown/invalid.
	StationID source;           ///< The station where the cargo came from first.
	TileIndex source_xy;        ///< The origin of the cargo (first station in feeder chain).
	TileIndex loaded_at_xy;     ///< Location where this cargo has been loaded into the vehicle.

	/** The CargoList caches, thus needs to know about it. */
	template <class Tinst, class Tcont> friend class CargoList;
	friend class VehicleCargoList;
	friend class StationCargoList;
	/** We want this to be saved, right? */
	friend const struct SaveLoad *GetCargoPacketDesc();
public:
	/** Maximum number of items in a single cargo packet. */
	static const uint16 MAX_COUNT = UINT16_MAX;

	CargoPacket();
	CargoPacket(StationID source, TileIndex source_xy, uint16 count, SourceType source_type, SourceID source_id);
	CargoPacket(uint16 count, byte days_in_transit, StationID source, TileIndex source_xy, TileIndex loaded_at_xy, Money feeder_share = 0, SourceType source_type = ST_INDUSTRY, SourceID source_id = INVALID_SOURCE);

	/** Destroy the packet. */
	~CargoPacket() { }

	CargoPacket *Split(uint new_size);
	void Merge(CargoPacket *cp);

	/**
	 * Gets the number of 'items' in this packet.
	 * @return Item count.
	 */
	FORCEINLINE uint16 Count() const
	{
		return this->count;
	}

	/**
	 * Gets the amount of money already paid to earlier vehicles in
	 * the feeder chain.
	 * @return Feeder share.
	 */
	FORCEINLINE Money FeederShare() const
	{
		return this->feeder_share;
	}

	/**
	 * Gets the number of days this cargo has been in transit.
	 * This number isn't really in days, but in 2.5 days (185 ticks) and
	 * it is capped at 255.
	 * @return Length this cargo has been in transit.
	 */
	FORCEINLINE byte DaysInTransit() const
	{
		return this->days_in_transit;
	}

	/**
	 * Gets the type of the cargo's source. industry, town or head quarter.
	 * @return Source type.
	 */
	FORCEINLINE SourceType SourceSubsidyType() const
	{
		return this->source_type;
	}

	/**
	 * Gets the ID of the cargo's source. An IndustryID, TownID or CompanyID.
	 * @return Source ID.
	 */
	FORCEINLINE SourceID SourceSubsidyID() const
	{
		return this->source_id;
	}

	/**
	 * Gets the ID of the station where the cargo was loaded for the first time.
	 * @return StationID.
	 */
	FORCEINLINE SourceID SourceStation() const
	{
		return this->source;
	}

	/**
	 * Gets the coordinates of the cargo's source station.
	 * @return Source station's coordinates.
	 */
	FORCEINLINE TileIndex SourceStationXY() const
	{
		return this->source_xy;
	}

	/**
	 * Gets the coordinates of the cargo's last loading station.
	 * @return Last loading station's coordinates.
	 */
	FORCEINLINE TileIndex LoadedAtXY() const
	{
		return this->loaded_at_xy;
	}

	static void InvalidateAllFrom(SourceType src_type, SourceID src);
	static void InvalidateAllFrom(StationID sid);
	static void AfterLoad();
};

/**
 * Iterate over all _valid_ cargo packets from the given start.
 * @param var   Variable used as "iterator".
 * @param start Cargo packet ID of the first packet to iterate over.
 */
#define FOR_ALL_CARGOPACKETS_FROM(var, start) FOR_ALL_ITEMS_FROM(CargoPacket, cargopacket_index, var, start)

/**
 * Iterate over all _valid_ cargo packets from the begin of the pool.
 * @param var   Variable used as "iterator".
 */
#define FOR_ALL_CARGOPACKETS(var) FOR_ALL_CARGOPACKETS_FROM(var, 0)

/** Kind of actions that could be done with packets on unloading */
enum UnloadType {
	UL_KEEP     = 0,      ///< keep cargo on vehicle
	UL_DELIVER  = 1 << 0, ///< deliver cargo
	UL_TRANSFER = 1 << 1, ///< transfer cargo
	UL_ACCEPTED = 1 << 2, ///< cargo is accepted
};

/**
 * Simple collection class for a list of cargo packets.
 * @tparam Tinst Actual instantation of this cargo list.
 */
template <class Tinst, class Tcont>
class CargoList {
public:
	/** Iterator for our container. */
	typedef typename Tcont::iterator Iterator;
	/** Const iterator for our container. */
	typedef typename Tcont::const_iterator ConstIterator;
	/** Reverse iterator for our container. */
	typedef typename Tcont::reverse_iterator ReverseIterator;
	/** Const reverse iterator for our container. */
	typedef typename Tcont::const_reverse_iterator ConstReverseIterator;

protected:
	uint count;                 ///< Cache for the number of cargo entities.
	uint cargo_days_in_transit; ///< Cache for the sum of number of days in transit of each entity; comparable to man-hours.

	Tcont packets;              ///< The cargo packets in this list.

	void AddToCache(const CargoPacket *cp);

	void RemoveFromCache(const CargoPacket *cp);

	CargoPacket *RemovePacket(Iterator &it, uint cap, TileIndex load_place = INVALID_TILE);

	uint MovePacket(StationCargoList *dest, StationID next, Iterator &it, uint cap);

	uint MovePacket(VehicleCargoList *dest, Iterator &it, uint cap, TileIndex load_place = INVALID_TILE, bool reserved = false);

public:
	/** Create the cargo list. */
	CargoList() {}

	~CargoList();

	/**
	 * Returns a pointer to the cargo packet list (so you can iterate over it etc).
	 * @return Pointer to the packet list.
	 */
	FORCEINLINE const Tcont *Packets() const
	{
		return &this->packets;
	}

	/**
	 * Checks whether this list is empty.
	 * @return True if and only if the list is empty.
	 */
	FORCEINLINE bool Empty() const
	{
		return this->count == 0;
	}

	/**
	 * Returns the number of cargo entities in this list.
	 * @return The before mentioned number.
	 */
	FORCEINLINE uint Count() const
	{
		return this->count;
	}

	/**
	 * Returns average number of days in transit for a cargo entity.
	 * @return The before mentioned number.
	 */
	FORCEINLINE uint DaysInTransit() const
	{
		return this->count == 0 ? 0 : this->cargo_days_in_transit / this->count;
	}

	void Append(CargoPacket *cp, bool update_cache = true);
	void Truncate(uint max_remaining);

	void InvalidateCache();
};

typedef std::list<CargoPacket *> CargoPacketList;

/**
 * CargoList that is used for vehicles.
 */
class VehicleCargoList : public CargoList<VehicleCargoList, CargoPacketList> {
protected:
	uint TransferPacket(Iterator &c, uint remaining_unload, StationCargoList *dest, CargoPayment *payment, StationID next);
	uint DeliverPacket(Iterator &c, uint remaining_unload, CargoPayment *payment);
	uint KeepPacket(Iterator &c);

	/** The (direct) parent of this class. */
	typedef CargoList<VehicleCargoList, CargoPacketList> Parent;

	CargoPacketList reserved; ///< Packets reserved for unloading in this list.
	Money feeder_share;       ///< Cache for the feeder share.
	uint reserved_count;      ///< Cache for the number of reserved cargo entities.

	void AddToCache(const CargoPacket *cp);
	void RemoveFromCache(const CargoPacket *cp);

public:
	/** The station cargo list needs to control the unloading. */
	friend class StationCargoList;
	/** The super class ought to know what it's doing. */
	friend class CargoList<VehicleCargoList, CargoPacketList>;
	/** The vehicles have a cargo list (and we want that saved). */
	friend const struct SaveLoad *GetVehicleDescription(VehicleType vt);

	~VehicleCargoList();

	/**
	 * Returns total sum of the feeder share for all packets.
	 * @return The before mentioned number.
	 */
	FORCEINLINE Money FeederShare() const
	{
		return this->feeder_share;
	}

	void Append(CargoPacket *cp, bool update_cache = true);

	/**
	 * Returns sum of cargo on board the vehicle (ie not only
	 * reserved).
	 * @return Cargo on board the vehicle.
	 */
	FORCEINLINE uint OnboardCount() const
	{
		return this->count - this->reserved_count;
	}

	/**
	 * Returns sum of cargo reserved for the vehicle.
	 * @return Cargo reserved for the vehicle.
	 */
	FORCEINLINE uint ReservedCount() const
	{
		return this->reserved_count;
	}

	/**
	 * Returns a pointer to the reserved cargo list.
	 * @return Pointer to the reserved list.
	 */
	FORCEINLINE const CargoPacketList *Reserved() const
	{
		return &this->reserved;
	}

	/**
	 * Returns source of the first cargo packet in this list.
	 * If the regular packets list is empty but there are packets
	 * in the reservation list it returns the source of the first
	 * reserved packet.
	 * @return The before mentioned source.
	 */
	FORCEINLINE StationID Source() const
	{
		if (this->Empty()) {
			return INVALID_STATION;
		} else if (this->packets.empty()) {
			return this->reserved.front()->source;
		} else {
			return this->packets.front()->source;
		}
	}

	void Reserve(CargoPacket *cp);

	void Unreserve(StationID next, StationCargoList *dest);

	uint LoadReserved(uint count);

	void SwapReserved();

	void AgeCargo();

	void InvalidateCache();

	uint MoveTo(VehicleCargoList *dest, uint cap);

	/**
	 * Are two the two CargoPackets mergeable in the context of
	 * a list of CargoPackets for a Vehicle?
	 * @param cp1 First CargoPacket.
	 * @param cp2 Second CargoPacket.
	 * @return True if they are mergeable.
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
	/** The super class ought to know what it's doing. */
	friend class CargoList<StationCargoList, StationCargoPacketMap>;
	/** The stations, via GoodsEntry, have a CargoList. */
	friend const struct SaveLoad *GetGoodsDesc();

	StationCargoList() : station(NULL), cargo(INVALID_CARGO) {}

	/**
	 * Are two the two CargoPackets mergeable in the context of
	 * a list of CargoPackets for a Vehicle?
	 * @param cp1 First CargoPacket.
	 * @param cp2 Second CargoPacket.
	 * @return True if they are mergeable.
	 */
	static bool AreMergable(const CargoPacket *cp1, const CargoPacket *cp2)
	{
		return cp1->source_xy    == cp2->source_xy &&
				cp1->days_in_transit == cp2->days_in_transit &&
				cp1->source_type     == cp2->source_type &&
				cp1->source_id       == cp2->source_id;
	}

	uint TakeFrom(VehicleCargoList *source, uint max_unload, OrderUnloadFlags flags, StationID next_station, bool has_stopped, CargoPayment *payment);

	uint MoveTo(VehicleCargoList *dest, uint cap, StationID next_station, bool reserve = false);

	void Append(StationID next, CargoPacket *cp);

	void RerouteStalePackets(StationID to);

	void RandomTruncate(uint max_remaining);

	/**
	 * Returns source of the first cargo packet in this list.
	 * @return The before mentioned source.
	 */
	FORCEINLINE StationID Source() const
	{
		return this->Empty() ? INVALID_STATION : this->packets.begin()->second.front()->source;
	}

	void AssignTo(Station *station, CargoID cargo);

	static void InvalidateAllFrom(SourceType src_type, SourceID src);

protected:
	Station *station; ///< Station this cargo list belongs to.
	CargoID cargo;    ///< Cargo type this list holds.

	byte GetUnloadFlags(OrderUnloadFlags order_flags);

	UnloadType WillUnloadOld(byte flags, StationID source);
	UnloadType WillUnloadCargoDist(byte flags, StationID next_station, StationID via, StationID source);

	uint MovePackets(VehicleCargoList *dest, uint cap, Iterator begin, Iterator end, bool reserve);
};

#endif /* CARGOPACKET_H */
