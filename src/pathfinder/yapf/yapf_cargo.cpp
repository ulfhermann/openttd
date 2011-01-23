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

	/** Initialize this node. */
	FORCEINLINE void Set(CYapfRouteLinkNodeT *parent, RouteLink *link)
	{
		Base::Set(parent, false);
		this->m_key.Set(link);
	}

	/** Get the route link of this node. */
	FORCEINLINE RouteLink *GetRouteLink() const { return this->m_key.m_link; }
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

	/** To access inherited path finder. */
	FORCEINLINE Tpf& Yapf() { return *static_cast<Tpf*>(this); }

public:
	/** Called by YAPF to calculate the cost from the origin to the given node. */
	inline bool PfCalcCost(Node& n, const Follower *follow)
	{
		/* Apply it. */
		n.m_cost = n.m_parent->m_cost;
		return true;
	}
};

/** YAPF origin provider for route links. */
template <class Types>
class CYapfOriginRouteLinkT {
	typedef typename Types::Tpf Tpf;                     ///< The pathfinder class (derived from THIS class).
	typedef typename Types::NodeList::Titem Node;        ///< This will be our node type.

	CargoID   m_cid;
	TileIndex m_src;

	/** To access inherited path finder. */
	FORCEINLINE Tpf& Yapf() { return *static_cast<Tpf*>(this); }

public:
	/** Get the current cargo type. */
	FORCEINLINE CargoID GetCargoID() const
	{
		return this->m_cid;
	}

	/** Set origin. */
	void SetOrigin(CargoID cid, TileIndex src, const StationList *stations)
	{
		this->m_cid = cid;
		this->m_src = src;
	}

	/** Called when YAPF needs to place origin nodes into the open list. */
	void PfSetStartupNodes()
	{
	}
};

/** YAPF destination provider for route links. */
template <class Types>
class CYapfDestinationRouteLinkT {
	typedef typename Types::Tpf Tpf;                     ///< The pathfinder class (derived from THIS class).
	typedef typename Types::NodeList::Titem Node;        ///< This will be our node type.

	TileArea m_dest;

	/** To access inherited path finder. */
	FORCEINLINE Tpf& Yapf() { return *static_cast<Tpf*>(this); }

public:
	/** Set destination. */
	void SetDestination(const TileArea &dest)
	{
		this->m_dest = dest;
	}

	/** Called by YAPF to detect if the node reaches the destination. */
	FORCEINLINE bool PfDetectDestination(const Node& n) const
	{
		return false;
	}

	/** Called by YAPF to calculate the estimated cost to the destination. */
	FORCEINLINE bool PfCalcEstimate(Node& n)
	{
		n.m_estimate = n.m_cost;
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
	}

	/** Return debug report character to identify the transportation type. */
	FORCEINLINE char TransportTypeChar() const
	{
		return 'c';
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
