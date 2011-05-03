/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file cargodest_base.h Classes and types for entities having cargo destinations. */

#ifndef CARGODEST_BASE_H
#define CARGODEST_BASE_H

#include "cargodest_type.h"
#include "cargo_type.h"
#include "town_type.h"
#include "core/smallvec_type.hpp"
#include "core/pool_type.hpp"
#include "order_type.h"
#include "station_type.h"
#include "company_type.h"

struct CargoSourceSink;

/** Information about a demand link for cargo. */
struct CargoLink {
	CargoSourceSink      *dest;      ///< Destination of the link.
	TransportedCargoStat amount;     ///< Transported cargo statistics.
	uint                 weight;     ///< Weight of this link.
	byte                 weight_mod; ///< Weight modifier.

	CargoLink(CargoSourceSink *d, byte mod) : dest(d), weight(1), weight_mod(mod) {}

	/* Compare two cargo links for inequality. */
	bool operator !=(const CargoLink &other) const
	{
		return other.dest != dest;
	}
};

/** An entity producing or accepting cargo with a destination. */
struct CargoSourceSink {
	/** List of destinations for each cargo type. */
	SmallVector<CargoLink, 8> cargo_links[NUM_CARGO];
	/** Sum of the destination weights for each cargo type. */
	uint cargo_links_weight[NUM_CARGO];

	/** NOSAVE: Desired link count for each cargo. */
	uint16 num_links_expected[NUM_CARGO];

	/** NOSAVE: Incoming link count for each cargo. */
	uint num_incoming_links[NUM_CARGO];

	virtual ~CargoSourceSink();

	/** Get the type of this entity. */
	virtual SourceType GetType() const = 0;
	/** Get the source ID corresponding with this entity. */
	virtual SourceID GetID() const = 0;

	/**
	 * Test if a demand link to a destination exists.
	 * @param cid Cargo type for which a link should be searched.
	 * @param dest Destination to search for.
	 * @return True if a link to the destination is present.
	 */
	bool HasLinkTo(CargoID cid, const CargoSourceSink *dest) const
	{
		return this->cargo_links[cid].Contains(CargoLink(const_cast<CargoSourceSink *>(dest), 1));
	}

	/** Is this cargo accepted? */
	virtual bool AcceptsCargo(CargoID cid) const = 0;
	/** Is this cargo produced? */
	virtual bool SuppliesCargo(CargoID cid) const = 0;

	/** Get the link weight for this as a destination for a specific cargo. */
	virtual uint GetDestinationWeight(CargoID cid, byte weight_mod) const = 0;

	CargoLink *GetRandomLink(CargoID cid, bool allow_self);

	/** Create the special cargo links for a cargo if not already present. */
	virtual void CreateSpecialLinks(CargoID cid);

	void SaveCargoSourceSink();
	void LoadCargoSourceSink();
	void PtrsCargoSourceSink();
};


/** Pool of route links. */
typedef Pool<RouteLink, RouteLinkID, 512, 262144> RouteLinkPool;
extern RouteLinkPool _routelink_pool;

/** Holds information about a route service between two stations. */
struct RouteLink : public RouteLinkPool::PoolItem<&_routelink_pool> {
private:
	friend const struct SaveLoad *GetRouteLinkDescription(); ///< Saving and loading of route links.
	friend void ChangeOwnershipOfCompanyItems(Owner old_owner, Owner new_owner);
	friend void AgeRouteLinks(Station *st);

	StationID       dest;            ///< Destination station id.
	OrderID         prev_order;      ///< Id of the order the vehicle had when arriving at the origin.
	OrderID         next_order;      ///< Id of the order the vehicle will leave the station with.
	OwnerByte       owner;           ///< Owner of the vehicle of the link.
	uint32          travel_time;     ///< Average travel duration of this link.
	uint16          wait_time;       ///< Days since the last vehicle traveled this link.

public:
	/** Constructor */
	RouteLink(StationID dest = INVALID_STATION, OrderID prev_order = INVALID_ORDER, OrderID next_order = INVALID_ORDER, Owner owner = INVALID_OWNER, uint32 travel_time = 0)
		: dest(dest), prev_order(prev_order), next_order(next_order), travel_time(travel_time), wait_time(0)
	{
		this->owner = owner;
	}

	~RouteLink() {}

	/** Get the target station of this link. */
	inline StationID GetDestination() const { return this->dest; }

	/** Get the order id that lead to the origin station. */
	inline OrderID GetOriginOrderId() const { return this->prev_order; }

	/** Get the order id that lead to the destination station. */
	inline OrderID GetDestOrderId() const { return this->next_order; }

	/** Get the owner of this link. */
	inline Owner GetOwner() const { return this->owner; }

	/** Get the travel time of this link. */
	inline uint32 GetTravelTime() const { return this->travel_time; }

	/** Get the wait time at the origin station. */
	inline uint16 GetWaitTime() const { return this->wait_time; }

	/** Update the destination of the route link. */
	inline void SetDestination(StationID dest_id, OrderID dest_order_id)
	{
		this->dest = dest_id;
		this->next_order = dest_order_id;
	}

	/** Update the travel time with a new travel time.  */
	void UpdateTravelTime(uint32 new_time)
	{
		/* Weighted average so that a single late vehicle will not skew the time. */
		this->travel_time = (3 * this->travel_time + new_time) / 4;
	}

	/** A vehicle arrived at the origin of the link, reset waiting time. */
	void VehicleArrived()
	{
		this->wait_time = 0;
	}
};


/**
 * Iterate over all valid route links from a given start.
 * @param var   The variable to use as the "iterator".
 * @param start The #RouteLinkID to start the iteration from.
 */
#define FOR_ALL_ROUTELINKS_FROM(var, start) FOR_ALL_ITEMS_FROM(RouteLink, routelink_index, var, start)

/**
 * Iterate over all valid route links.
 * @param var   The variable to use as the "iterator".
 */
#define FOR_ALL_ROUTELINKS(var) FOR_ALL_ROUTELINKS_FROM(var, 0)

#endif /* CARGODEST_BASE_H */
