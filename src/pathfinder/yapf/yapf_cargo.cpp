/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file yapf_cargo.cpp Implementation of YAPF for cargo routing. */

#include "../../stdafx.h"
#include "../../cargodest_base.h"
#include "../../station_base.h"
#include "../../town.h"
#include "yapf.hpp"


/** YAPF node key for cargo routing. */
struct CYapfRouteLinkNodeKeyT {
	RouteLink *m_link;

	/** Initialize this node key. */
	FORCEINLINE void Set(RouteLink *link)
	{
		this->m_link = link;
	}

	/** Calculate the hash of this cargo/route key. */
	FORCEINLINE int CalcHash() const
	{
		return (int)(size_t)this->m_link >> 4;
	}

	FORCEINLINE bool operator == (const CYapfRouteLinkNodeKeyT& other) const
	{
		return this->m_link == other.m_link;
	}

	void Dump(DumpTarget &dmp) const
	{
		dmp.WriteLine("m_link = %u", this->m_link->GetDestination());
	}
};

/** YAPF node class for cargo routing. */
struct CYapfRouteLinkNodeT : public CYapfNodeT<CYapfRouteLinkNodeKeyT, CYapfRouteLinkNodeT> {
	typedef CYapfNodeT<CYapfRouteLinkNodeKeyT, CYapfRouteLinkNodeT> Base;

	uint m_num_transfers; ///< Number of transfers to reach this node.

	/** Initialize this node. */
	FORCEINLINE void Set(CYapfRouteLinkNodeT *parent, RouteLink *link)
	{
		Base::Set(parent, false);
		this->m_key.Set(link);
		this->m_num_transfers = (parent != NULL) ? parent->m_num_transfers : 0;
	}

	/** Get the route link of this node. */
	FORCEINLINE RouteLink *GetRouteLink() const { return this->m_key.m_link; }

	/** Get the number of transfers needed to reach this node. */
	FORCEINLINE int GetNumberOfTransfers() const { return this->m_num_transfers; }
};

typedef CNodeList_HashTableT<CYapfRouteLinkNodeT, 8, 10, 2048> CRouteLinkNodeList;

/** Route link follower. */
struct CFollowRouteLinkT {
	CargoID m_cid;
	RouteLink *m_old_link;
	RouteLinkList *m_new_links;

	CFollowRouteLinkT(CargoID cid) : m_cid(cid) {}

	/** Fill in route links reachable by this route link. */
	inline bool Follow(RouteLink *from)
	{
		this->m_old_link = from;

		Station *st = Station::Get(from->GetDestination());
		m_new_links = &st->goods[this->m_cid].routes;
		return !this->m_new_links->empty();
	}
};

/** YAPF cost provider for route links. */
template <class Types>
class CYapfCostRouteLinkT {
	typedef typename Types::Tpf Tpf;                     ///< The pathfinder class (derived from THIS class).
	typedef typename Types::TrackFollower Follower;      ///< The route follower.
	typedef typename Types::NodeList::Titem Node;        ///< This will be our node type.

	static const int PENALTY_DIVISOR      = 16;          ///< Penalty factor divisor for fixed-point arithmetics.
	static const int LOCAL_PENALTY_FACTOR = 16;          ///< Penalty factor for source-local delivery.

	/** To access inherited path finder. */
	FORCEINLINE Tpf& Yapf() { return *static_cast<Tpf*>(this); }
	FORCEINLINE const Tpf& Yapf() const { return *static_cast<const Tpf*>(this); }

	/** Check if this is a valid connection. */
	FORCEINLINE bool ValidLink(Node &n, const RouteLink *link, const RouteLink *parent) const
	{
		/* If the parent link has an owner, and the owner is different to
		 * the new owner, discard the node. Otherwise cargo could switch
		 * companies at oil rigs, which would mess up payment. */
		if (parent->GetOwner() != INVALID_OWNER && link->GetOwner() != parent->GetOwner()) return false;

		/* Check for no loading/no unloading when transferring. */
		if (link->GetOriginOrderId() != parent->GetDestOrderId() || (Order::Get(link->GetOriginOrderId())->GetUnloadType() & OUFB_UNLOAD) != 0) {
			/* Can't transfer if the current order prohibits loading. */
			if ((Order::Get(link->GetOriginOrderId())->GetLoadType() & OLFB_NO_LOAD) != 0) return false;

			/* Can't transfer if the last order prohibits unloading. */
			if (parent->GetDestOrderId() != INVALID_ORDER && (Order::Get(parent->GetDestOrderId())->GetUnloadType() & OUFB_NO_UNLOAD) != 0) return false;

			/* Increase transfer counter and stop if max number of transfers is exceeded. */
			if (++n.m_num_transfers > Yapf().PfGetSettings().route_max_transfers) return false;
		}

		return true;
	}

	/** Cost of a single route link. */
	FORCEINLINE int RouteLinkCost(const RouteLink *link, const RouteLink *parent) const
	{
		int cost = 0;

		/* Distance cost. */
		const Station *from = Station::Get(parent->GetDestination());
		const Station *to = Station::Get(link->GetDestination());
		cost = DistanceManhattan(from->xy, to->xy) * this->Yapf().PfGetSettings().route_distance_factor;

		/* Modulate the distance by a vehicle-type specific factor to
		 * simulate the different costs. */
		assert_compile(lengthof(_settings_game.pf.yapf.route_mode_cost_factor) == VEH_AIRCRAFT + 1);
		cost *= this->Yapf().PfGetSettings().route_mode_cost_factor[link->GetVehicleType()];

		/* Transfer penalty when switching vehicles or forced unloading. */
		if (link->GetOriginOrderId() != parent->GetDestOrderId() || (Order::Get(link->GetOriginOrderId())->GetUnloadType() & OUFB_UNLOAD) != 0) {
			cost += this->Yapf().PfGetSettings().route_transfer_cost;

			/* Penalty for time since the last vehicle arrived. */
			cost += link->GetWaitTime() * this->Yapf().PfGetSettings().route_station_last_veh_factor / PENALTY_DIVISOR;

			/* Penalty for cargo waiting on our link. */
			cost += (from->goods[this->Yapf().GetCargoID()].cargo.CountForNextHop(link->GetOriginOrderId()) * this->Yapf().PfGetSettings().route_station_waiting_factor) / PENALTY_DIVISOR;
		}

		/* Penalty for travel time. */
		cost += (link->GetTravelTime() * this->Yapf().PfGetSettings().route_travel_time_factor) / PENALTY_DIVISOR;

		return cost;
	}

public:
	/** Called by YAPF to calculate the cost from the origin to the given node. */
	inline bool PfCalcCost(Node& n, const Follower *follow)
	{
		int segment_cost = 0;

		if (this->Yapf().PfDetectDestination(n)) {
			Station *st = Station::Get(n.m_parent->GetRouteLink()->GetDestination());
			/* Discard node if the station doesn't accept the cargo type. */
			if (!HasBit(st->goods[follow->m_cid].acceptance_pickup, GoodsEntry::ACCEPTANCE)) return false;
			/* Destination node, get delivery cost. Parent has the station. */
			segment_cost += this->Yapf().DeliveryCost(st);
			/* If this link comes from an origin station, penalize it to encourage
			 * delivery using other stations. */
			if (n.m_parent->GetRouteLink()->GetDestOrderId() == INVALID_ORDER) segment_cost *= LOCAL_PENALTY_FACTOR;
		} else {
			RouteLink *link = n.GetRouteLink();
			RouteLink *parent = n.m_parent->GetRouteLink();

			/* Check if the link is a valid connection. */
			if (!this->ValidLink(n, link, parent)) return false;

			/* Cost of the single route link. */
			segment_cost += this->RouteLinkCost(link, parent);
		}

		/* Apply it. */
		n.m_cost = n.m_parent->m_cost + segment_cost;
		return n.m_cost <= this->Yapf().GetMaxCost();
	}
};

/** YAPF origin provider for route links. */
template <class Types>
class CYapfOriginRouteLinkT {
	typedef typename Types::Tpf Tpf;                     ///< The pathfinder class (derived from THIS class).
	typedef typename Types::NodeList::Titem Node;        ///< This will be our node type.

	CargoID   m_cid;
	TileIndex m_src;
	OrderID   m_order;
	SmallVector<RouteLink, 2> m_origin;

	/** To access inherited path finder. */
	FORCEINLINE Tpf& Yapf() { return *static_cast<Tpf*>(this); }

public:
	/** Get the current cargo type. */
	FORCEINLINE CargoID GetCargoID() const
	{
		return this->m_cid;
	}

	/** Set origin. */
	void SetOrigin(CargoID cid, TileIndex src, const StationList *stations, bool cargo_creation, OrderID order)
	{
		this->m_cid = cid;
		this->m_src = src;
		this->m_order = order;
		/* Create fake links for the origin stations. */
		for (const Station * const *st = stations->Begin(); st != stations->End(); st++) {
			if (cargo_creation) {
				/* Exclusive rights in effect? Only serve those stations. */
				if ((*st)->town->exclusive_counter > 0 && (*st)->town->exclusivity != (*st)->owner) continue;
				/* Selectively servicing stations, and not this one. */
				if (_settings_game.order.selectgoods && (*st)->goods[cid].last_speed == 0) continue;
			}

			*this->m_origin.Append() = RouteLink((*st)->index, INVALID_ORDER, this->m_order);
		}
	}

	/** Called when YAPF needs to place origin nodes into the open list. */
	void PfSetStartupNodes()
	{
		for (RouteLink *link = this->m_origin.Begin(); link != this->m_origin.End(); link++) {
			Node &n = this->Yapf().CreateNewNode();
			n.Set(NULL, link);
			/* Prefer stations closer to the source tile. */
			n.m_cost = DistanceSquare(this->m_src, Station::Get(link->GetDestination())->xy) * this->Yapf().PfGetSettings().route_distance_factor;
			this->Yapf().AddStartupNode(n);
		}
	}
};

/** YAPF destination provider for route links. */
template <class Types>
class CYapfDestinationRouteLinkT {
	typedef typename Types::Tpf Tpf;                     ///< The pathfinder class (derived from THIS class).
	typedef typename Types::NodeList::Titem Node;        ///< This will be our node type.

	TileArea m_dest;
	int m_max_cost;            ///< Maximum node cost.

	/** To access inherited path finder. */
	FORCEINLINE Tpf& Yapf() { return *static_cast<Tpf*>(this); }

public:
	/** Get the maximum allowed node cost. */
	FORCEINLINE int GetMaxCost() const
	{
		return this->m_max_cost;
	}

	/** Set destination. */
	void SetDestination(const TileArea &dest, uint max_cost)
	{
		this->m_dest = dest;
		this->m_max_cost = max_cost;
	}

	/** Cost for delivering the cargo to the final destination tile. */
	FORCEINLINE int DeliveryCost(Station *st)
	{
		int x = TileX(this->m_dest.tile);
		int y = TileY(this->m_dest.tile);

		/* Inside the station area? Delivery costs "nothing". */
		if (st->rect.PtInExtendedRect(x, y)) return 0;

		int dist_x = x < st->rect.left ? x - st->rect.left : x - st->rect.right;
		int dist_y = y < st->rect.top  ? y - st->rect.top  : y - st->rect.bottom;

		return (dist_x * dist_x + dist_y * dist_y) * this->Yapf().PfGetSettings().route_distance_factor;
	}

	/** Called by YAPF to detect if the station reaches the destination. */
	FORCEINLINE bool PfDetectDestination(StationID st_id) const
	{
		const Station *st = Station::Get(st_id);
		return st->rect.AreaInExtendedRect(this->m_dest, st->GetCatchmentRadius());
	}

	/** Called by YAPF to detect if the node reaches the destination. */
	FORCEINLINE bool PfDetectDestination(const Node& n) const
	{
		return n.GetRouteLink() == NULL;
	}

	/** Called by YAPF to calculate the estimated cost to the destination. */
	FORCEINLINE bool PfCalcEstimate(Node& n)
	{
		if (this->PfDetectDestination(n)) {
			n.m_estimate = n.m_cost;
			return true;
		}

		/* Estimate based on Manhattan distance to destination. */
		Station *from = Station::Get(n.GetRouteLink()->GetDestination());
		int d = DistanceManhattan(from->xy, this->m_dest.tile) * this->Yapf().PfGetSettings().route_distance_factor;

		n.m_estimate = n.m_cost + d;
		assert(n.m_estimate >= n.m_parent->m_estimate);
		return true;
	}
};

/** Main route finding class. */
template <class Types>
class CYapfFollowRouteLinkT {
	typedef typename Types::Tpf Tpf;                     ///< The pathfinder class (derived from THIS class).
	typedef typename Types::TrackFollower Follower;      ///< The route follower.
	typedef typename Types::NodeList::Titem Node;        ///< This will be our node type.

	/** To access inherited path finder. */
	FORCEINLINE Tpf& Yapf() { return *static_cast<Tpf*>(this); }

public:
	/** Called by YAPF to move from the given node to the next nodes. */
	inline void PfFollowNode(Node& old_node)
	{
		Follower f(this->Yapf().GetCargoID());

		if (this->Yapf().PfDetectDestination(old_node.GetRouteLink()->GetDestination()) && (old_node.GetRouteLink()->GetDestOrderId() == INVALID_ORDER || (Order::Get(old_node.GetRouteLink()->GetDestOrderId())->GetUnloadType() & OUFB_NO_UNLOAD) == 0)) {
			/* Possible destination? Add sentinel node for final delivery. */
			Node &n = this->Yapf().CreateNewNode();
			n.Set(&old_node, NULL);
			this->Yapf().AddNewNode(n, f);
		}

		if (f.Follow(old_node.GetRouteLink())) {
			for (RouteLinkList::iterator link = f.m_new_links->begin(); link != f.m_new_links->end(); ++link) {
				/* Add new node. */
				Node &n = this->Yapf().CreateNewNode();
				n.Set(&old_node, *link);
				this->Yapf().AddNewNode(n, f);
			}
		}
	}

	/** Return debug report character to identify the transportation type. */
	FORCEINLINE char TransportTypeChar() const
	{
		return 'c';
	}

	/** Find the best cargo routing from a station to a destination. */
	static RouteLink *ChooseRouteLink(CargoID cid, const StationList *stations, TileIndex src, const TileArea &dest, StationID *start_station, StationID *next_unload, bool *found, OrderID order, int max_cost)
	{
		/* Initialize pathfinder instance. */
		Tpf pf;
		pf.SetOrigin(cid, src, stations, start_station != NULL, order);
		pf.SetDestination(dest, max_cost);

		*next_unload = INVALID_STATION;

		/* Do it. Exit if we didn't find a path. */
		bool res = pf.FindPath(NULL);
		if (found != NULL) *found = res;
		if (!res) return NULL;

		/* Walk back to find the start node. */
		Node *node = pf.GetBestNode();
		while (node->m_parent->m_parent != NULL) {
			/* Transfer? Then save transfer station as next unload station. */
			if (node->GetRouteLink() == NULL || (node->GetRouteLink()->GetOriginOrderId() != node->m_parent->GetRouteLink()->GetDestOrderId())) {
				*next_unload = node->m_parent->GetRouteLink()->GetDestination();
			}

			node = node->m_parent;
		}

		/* Save result. */
		if (start_station != NULL) {
			*start_station = node->m_parent->GetRouteLink()->GetDestination();
			/* Path starts and ends at the same station, do local delivery. */
			if (*start_station == pf.GetBestNode()->m_parent->GetRouteLink()->GetDestination()) return NULL;
		}
		return node->GetRouteLink();
	}
};

/** Config struct for route link finding. */
template <class Tpf_>
struct CYapfRouteLink_TypesT {
	typedef CYapfRouteLink_TypesT<Tpf_> Types;

	typedef Tpf_               Tpf;           ///< Pathfinder type
	typedef CFollowRouteLinkT  TrackFollower; ///< Node follower
	typedef CRouteLinkNodeList NodeList;      ///< Node list type
	typedef Vehicle            VehicleType;   ///< Dummy type

	typedef CYapfBaseT<Types>                 PfBase;        ///< Base pathfinder class
	typedef CYapfFollowRouteLinkT<Types>      PfFollow;      ///< Node follower
	typedef CYapfOriginRouteLinkT<Types>      PfOrigin;      ///< Origin provider
	typedef CYapfDestinationRouteLinkT<Types> PfDestination; ///< Destination/distance provider
	typedef CYapfSegmentCostCacheNoneT<Types> PfCache;       ///< Cost cache provider
	typedef CYapfCostRouteLinkT<Types>        PfCost;        ///< Cost provider
};

struct CYapfRouteLink : CYapfT<CYapfRouteLink_TypesT<CYapfRouteLink> > {};


/**
 * Find the best cargo routing from a station to a destination.
 * @param cid      Cargo type to route.
 * @param stations Set of possible originating stations.
 * @param dest     Destination tile area.
 * @param[out] start_station Station the best route link originates from.
 * @param[out] next_unload Next station the cargo should be unloaded from the vehicle.
 * @param[out] found True if a link was found.
 * @param order    Order the vehicle arrived at the origin station.
 * @param max_cost Maxmimum allowed node cost.
 * @return The best RouteLink to the target or NULL if either no link found or one of the origin stations is the best destination.
 */
RouteLink *YapfChooseRouteLink(CargoID cid, const StationList *stations, TileIndex src, const TileArea &dest, StationID *start_station, StationID *next_unload, bool *found, OrderID order, int max_cost)
{
	return CYapfRouteLink::ChooseRouteLink(cid, stations, src, dest, start_station, next_unload, found, order, max_cost);
}
