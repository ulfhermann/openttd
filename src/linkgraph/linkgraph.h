/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file linkgraph.h Declaration of link graph classes used for cargo distribution. */

#ifndef LINKGRAPH_H
#define LINKGRAPH_H

#include "../core/pool_type.hpp"
#include "../core/smallvec_type.hpp"
#include "../core/smallmatrix_type.hpp"
#include "../station_base.h"
#include "../cargo_type.h"
#include "../thread/thread.h"
#include "../settings_type.h"
#include "../date_func.h"
#include "linkgraph_type.h"
#include <list>
#include <set>

struct SaveLoad;
class LinkGraph;
class LinkGraphJob;
class Path;

typedef std::set<Path *> PathSet;
typedef std::map<NodeID, Path *> PathViaMap;

class GraphItem {
protected:
	uint Monthly(uint base, uint last_compression) const
	{
		return base * 30 / (_date - last_compression + 1);
	}
};

/**
 * Node of the link graph. contains all relevant information from the associated
 * station. It's copied so that the link graph job can work on its own data set
 * in a separate thread.
 */
class Node : public GraphItem{
public:
	uint supply;             ///< Supply at the station.
	uint demand;             ///< Acceptance at the station.
	StationID station;       ///< Station ID.
	Date last_update;        ///< When the supply was last updated.

	/**
	 * Clear a node on destruction to delete paths that might remain.
	 */
	~Node() { this->Init(); }

	void Init(StationID st = INVALID_STATION, uint demand = 0);

	uint MonthlySupply(Date last_compression) const
	{
		return this->Monthly(this->supply, last_compression);
	}
};

/**
 * An edge in the link graph. Corresponds to a link between two stations or at
 * least the distance between them. Edges from one node to itself contain the
 * ID of the opposite Node of the first active edge (i.e. not just distance) in
 * the column as next_edge.
 */
class Edge : public GraphItem {
public:
	static const uint MIN_DISTANCE = 48; ///< minimum effective distance for timeout calculation.

	uint distance;           ///< Length of the link.
	uint capacity;           ///< Capacity of the link.
	uint usage;              ///< Usage of the link.
	Date last_update;        ///< When the link was last updated.
	NodeID next_edge;        ///< Destination of next valid edge starting at the same source node.

	void Init(uint distance = 0);

	uint MonthlyCapacity(Date last_compression) const
	{
		return this->Monthly(this->capacity, last_compression);
	}

	uint MonthlyUsage(Date last_compression) const
	{
		return this->Monthly(this->usage, last_compression);
	}
};

/**
 * A handler doing "something" on a link graph component. It must not keep any
 * state as it is called concurrently from different threads.
 */
class ComponentHandler {
public:
	/**
	 * Destroy the handler. Must be given due to virtual Run.
	 */
	virtual ~ComponentHandler() {}

	/**
	 * Run the handler. A link graph handler must not read or write any data
	 * outside the given component as that would create a potential desync.
	 * @param job Link graph component to run the handler on.
	 */
	virtual void Run(LinkGraphJob *job) = 0;
};

/**
 * Type of the pool for link graph components. Each station can be in at up to
 * 32 link graphs. So we allow for plenty of them to be created.
 */
typedef Pool<LinkGraph, LinkGraphID, 32, 0xFFFFFF> LinkGraphPool;
/** The actual pool with link graphs. */
extern LinkGraphPool _link_graph_pool;

/**
 * A connected component of a link graph. Contains a complete set of stations
 * connected by links as nodes and edges. Each component also holds a copy of
 * the link graph settings at the time of its creation. The global settings
 * might change between the creation and join time so we can't rely on them.
 */
class LinkGraph : public LinkGraphPool::PoolItem<&_link_graph_pool> {
private:
	typedef SmallVector<Node, 16> NodeVector;
	typedef SmallMatrix<Edge> EdgeMatrix;

	friend const SaveLoad *GetLinkGraphDesc();
	friend const SaveLoad *GetLinkGraphJobDesc();

public:
	static const uint COMPRESSION_TICK = 58;

	/** Bare contructur, only for save/load. */
	LinkGraph() : cargo(INVALID_CARGO), num_nodes(0), last_compression(0) {}
	LinkGraph(CargoID cargo) : cargo(cargo), num_nodes(0), last_compression(_date) {}

	void Compress();
	void Merge(LinkGraph *other);

	/* Splitting link graphs is intentionally not implemented.
	 * The overhead in determining connectedness would probably outweigh the
	 * benefit of having to deal with smaller graphs. In real world examples
	 * networks generally grow. Only rarely a network is permanently split.
	 * Reacting to temporary splits here would obviously create performance
	 * problems and detecting the temporary or permanent nature of splits isn't
	 * trivial. */

	/**
	 * Get a reference to an edge.
	 * @param from Origin node.
	 * @param to Destination node.
	 * @return Edge between from and to.
	 */
	inline Edge &GetEdge(NodeID from, NodeID to)
	{
		return this->edges[from][to];
	}

	/**
	 * Get a const reference to an edge.
	 * @param from Origin node.
	 * @param to Destination node.
	 * @return Edge between from and to.
	 */
	inline const Edge &GetEdge(NodeID from, NodeID to) const
	{
		return this->edges[from][to];
	}

	/**
	 * Get a reference to a node with the specified id.
	 * @param num ID of the node.
	 * @return the Requested node.
	 */
	inline Node &GetNode(NodeID num)
	{
		return this->nodes[num];
	}

	/**
	 * Get a const reference to a node with the specified id.
	 * @param num ID of the node.
	 * @return the Requested node.
	 */
	inline const Node &GetNode(NodeID num) const
	{
		return this->nodes[num];
	}

	/**
	 * Get the current size of the component.
	 * @return Size.
	 */
	inline uint GetSize() const
	{
		return this->num_nodes;
	}

	/**
	 * Get date of last compression.
	 * @return Date of last compression.
	 */
	inline Date GetLastCompression() const
	{
		return this->last_compression;
	}

	/**
	 * Get the cargo ID this component's link graph refers to.
	 * @return Cargo ID.
	 */
	inline CargoID GetCargo() const
	{
		return this->cargo;
	}

	void SetSize();

	NodeID AddNode(const Station *st);
	void RemoveNode(NodeID id);

	void AddEdge(NodeID from, NodeID to, uint capacity);
	void UpdateEdge(NodeID from, NodeID to, uint capacity, uint usage);
	void RemoveEdge(NodeID from, NodeID to);

	/**
	 * Get the first valid edge starting at the specified node.
	 * @param from ID of the source node.
	 * @return ID of the destination node.
	 */
	inline NodeID GetFirstEdge(NodeID from) const { return edges[from][from].next_edge; }

	/**
	 * Get the next edge after the given one.
	 * @param from ID of source node.
	 * @param to ID of current destination node.
	 * @return ID of next destination node.
	 */
	inline NodeID GetNextEdge(NodeID from, NodeID to) const { return edges[from][to].next_edge; }

protected:
	void ResizeNodes();

	CargoID cargo;              ///< Cargo of this component's link graph.
	uint16 num_nodes;             ///< Number of nodes in the component.
	Date last_compression;      ///< Last time the capacities and supplies were compressed.
	NodeVector nodes;           ///< Nodes in the component.
	EdgeMatrix edges;           ///< Edges in the component.
};

#define FOR_ALL_LINK_GRAPHS(var) FOR_ALL_ITEMS_FROM(LinkGraph, link_graph_index, var, 0)

/** Type of the pool for link graph jobs. */
typedef Pool<LinkGraphJob, LinkGraphJobID, 32, 0xFFFFFF> LinkGraphJobPool;
/** The actual pool with link graph jobs. */
extern LinkGraphJobPool _link_graph_job_pool;

class EdgeAnnotation {
public:
	uint demand;             ///< Transport demand between the nodes.
	uint unsatisfied_demand; ///< Demand over this edge that hasn't been satisfied yet.
	uint flow;               ///< Planned flow over this edge.
};

class NodeAnnotation {
public:
	uint undelivered_supply; ///< Amount of supply that hasn't been distributed yet.
	PathSet paths;           ///< Paths through this node.
	FlowStatMap flows;       ///< Planned flows to other nodes.
};

class LinkGraphJob : public LinkGraphJobPool::PoolItem<&_link_graph_job_pool>{
private:
	typedef SmallVector<NodeAnnotation, 16> NodeAnnotationVector;
	typedef SmallMatrix<EdgeAnnotation> EdgeAnnotationMatrix;

	friend const SaveLoad *GetLinkGraphJobDesc();
	friend class LinkGraphSchedule;

protected:
	LinkGraph link_graph;
	LinkGraphSettings settings; ///< Copy of _settings_game.linkgraph at spawn time.
	ThreadObject *thread;       ///< Thread the job is running in or NULL if it's running in the main thread.
	Date join_date;             ///< Date when the job is to be joined.
	NodeAnnotationVector nodes; ///< Extra node data necessary for link graph calculation.
	EdgeAnnotationMatrix edges; ///< Extra edge data necessary for link graph calculation.

	void SpawnThread();
	void JoinThread();

public:
	/** Bare constructor, only for save/load */
	LinkGraphJob() : thread(NULL), join_date(INVALID_DATE) {}
	LinkGraphJob(const LinkGraph &orig);
	~LinkGraphJob();

	inline bool IsFinished() const
	{
		return this->join_date < _date;
	}

	inline Date JoinDate() const { return join_date; }

	/**
	 * Get the link graph settings for this component.
	 * @return Settings.
	 */
	inline const LinkGraphSettings &Settings() const
	{
		return this->settings;
	}

	/**
	 * Retrieve the link graph object we're working with.
	 */
	inline LinkGraph &Graph()
	{
		return this->link_graph;
	}

	inline EdgeAnnotationMatrix &Edges()
	{
		return this->edges;
	}

	inline NodeAnnotationVector &Nodes()
	{
		return this->nodes;
	}

	/**
	 * Get a reference to an edge annotation.
	 * @param from Origin node.
	 * @param to Destination node.
	 * @return Edge annotation between from and to.
	 */
	inline EdgeAnnotation &GetEdge(NodeID from, NodeID to)
	{
		return this->edges[from][to];
	}

	/**
	 * Get a reference to a node annotation with the specified id.
	 * @param num ID of the node.
	 * @return the Requested node annotation.
	 */
	inline NodeAnnotation &GetNode(NodeID num)
	{
		return this->nodes[num];
	}
};

#define FOR_ALL_LINK_GRAPH_JOBS(var) FOR_ALL_ITEMS_FROM(LinkGraphJob, link_graph_job_index, var, 0)

class LinkGraphSchedule {
private:
	LinkGraphSchedule();
	~LinkGraphSchedule();
	typedef std::list<LinkGraph *> GraphList;
	typedef std::list<LinkGraphJob *> JobList;
	friend const SaveLoad *GetLinkGraphScheduleDesc();

protected:
	ComponentHandler *handlers[6]; ///< Handlers to be run for each job.
	GraphList schedule;            ///< Queue for new jobs.
	JobList running;               ///< Currently running jobs.

public:
	/* This is a tick where not much else is happening, so a small lag might go unnoticed. */
	static const uint SPAWN_JOIN_TICK = 21; ///< Tick when jobs are spawned or joined every day.

	static LinkGraphSchedule *Instance();
	static void Run(void *j);
	static void Clear();
	void SpawnNext();
	void JoinNext();
	void Queue(LinkGraph *lg)
	{
		assert(LinkGraph::Get(lg->index) == lg);
		this->schedule.push_back(lg);
	}
	void Unqueue(LinkGraph *lg) { this->schedule.remove(lg); }
	void SpawnAll();
};

/**
 * A leg of a path in the link graph. Paths can form trees by being "forked".
 */
class Path {
public:
	Path(NodeID n, bool source = false);

	/** Get the node this leg passes. */
	inline NodeID GetNode() const { return this->node; }

	/** Get the overall origin of the path. */
	inline NodeID GetOrigin() const { return this->origin; }

	/** Get the parent leg of this one. */
	inline Path *GetParent() { return this->parent; }

	/** Get the overall capacity of the path. */
	inline uint GetCapacity() const { return this->capacity; }

	/** Get the free capacity of the path. */
	inline int GetFreeCapacity() const { return this->free_capacity; }

	/**
	 * Get ratio of free * 16 (so that we get fewer 0) /
	 * total capacity + 1 (so that we don't divide by 0).
	 * @param free Free capacity.
	 * @param total Total capacity.
	 * @return free * 16 / (total + 1).
	 */
	inline static int GetCapacityRatio(int free, int total)
	{
		return (free << 4) / (total + 1);
	}

	/**
	 * Get capacity ratio of this path.
	 * @return free capacity * 16 / (total capacity + 1).
	 */
	inline int GetCapacityRatio() const
	{
		return Path::GetCapacityRatio(this->free_capacity, this->capacity);
	}

	/** Get the overall distance of the path. */
	inline uint GetDistance() const { return this->distance; }

	/** Reduce the flow on this leg only by the specified amount. */
	inline void ReduceFlow(uint f) { this->flow -= f; }

	/** Increase the flow on this leg only by the specified amount. */
	inline void AddFlow(uint f) { this->flow += f; }

	/** Get the flow on this leg. */
	inline uint GetFlow() const { return this->flow; }

	/** Get the number of "forked off" child legs of this one. */
	inline uint GetNumChildren() const { return this->num_children; }

	/**
	 * Detach this path from its parent.
	 */
	inline void Detach()
	{
		if (this->parent != NULL) {
			this->parent->num_children--;
			this->parent = NULL;
		}
	}

	uint AddFlow(uint f, LinkGraphJob *job, uint max_saturation);
	void Fork(Path *base, uint cap, int free_cap, uint dist);

protected:
	uint distance;     ///< Sum(distance of all legs up to this one).
	uint capacity;     ///< This capacity is min(capacity) fom all edges.
	int free_capacity; ///< This capacity is min(edge.capacity - edge.flow) for the current run of Dijkstra.
	uint flow;         ///< Flow the current run of the mcf solver assigns.
	NodeID node;       ///< Link graph node this leg passes.
	NodeID origin;     ///< Link graph node this path originates from.
	uint num_children; ///< Number of child legs that have been forked from this path.
	Path *parent;      ///< Parent leg of this one.
};

#endif /* LINKGRAPH_H */
