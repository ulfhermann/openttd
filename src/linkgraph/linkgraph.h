/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file linkgraph.h Declaration of link graph classes used for cargo distribution. */

#ifndef LINKGRAPH_H_
#define LINKGRAPH_H_

#include "../station_base.h"
#include "../cargo_type.h"
#include "../thread/thread.h"
#include "../settings_type.h"
#include "../date_func.h"
#include "../core/smallmap_type.hpp"
#include "linkgraph_type.h"
#include <list>
#include <vector>
#include <set>

struct SaveLoad;
class Path;

typedef std::set<Path *> PathSet;
typedef std::map<NodeID, Path *> PathViaMap;
typedef std::map<StationID, int> FlowViaMap;
typedef std::map<StationID, FlowViaMap> FlowMap;

class NodeIDPair : public SmallPair<NodeID, NodeID> {
public:
	NodeIDPair(NodeID first, NodeID second) : SmallPair<NodeID, NodeID>(first, second) {}

	inline bool operator==(const NodeIDPair &other) const
	{
		return this->first == other.first && this->second == other.second;
	}
};

/**
 * Node of the link graph. contains all relevant information from the associated
 * station. It's copied so that the link graph job can work on its own data set
 * in a separate thread.
 */
class Node {
public:
	uint supply;             ///< Supply at the station.
	uint undelivered_supply; ///< Amount of supply that hasn't been distributed yet.
	uint demand;             ///< Acceptance at the station.
	StationID station;       ///< Station ID.
	PathSet paths;           ///< Paths through this node.
	FlowMap flows;           ///< Planned flows to other nodes.
	union {
		NodeID import_node;      ///< Extra node for "unload all" orders.
		NodeID passby_via;       ///< ID of next node in passby chain.
	}
	union {
		NodeID export_node;      ///< Extra node for "transfer" orders.
		NodeID passby_flag;	 ///< Node::IS_PASSBY if it's a passby node.
	}

	/**
	 * Clear a node on destruction to delete paths that might remain.
	 */
	~Node() {this->Init();}

	void Init(StationID st = INVALID_STATION, uint sup = 0, uint dem = 0);
	void ExportFlows(CargoID cargo);

private:
	void ExportFlows(FlowMap::iterator &it, FlowStatMap &station_flows, CargoID cargo);
};

/**
 * An edge in the link graph. Corresponds to a link between two stations or at
 * least the distance between them. Edges from one node to itself contain the
 * ID of the opposite Node of the first active edge (i.e. not just distance) in
 * the column as next_edge.
 */
class Edge {
public:
	uint distance;           ///< Length of the link.
	uint capacity;           ///< Capacity of the link.
	uint demand;             ///< Transport demand between the nodes.
	uint unsatisfied_demand; ///< Demand over this edge that hasn't been satisfied yet.
	uint flow;               ///< Planned flow over this edge.
	NodeID next_edge;        ///< Destination of next valid edge starting at the same source node.

	void Init(uint distance = 0, uint capacity = 0);
};

/**
 * A connected component of a link graph. Contains a complete set of stations
 * connected by links as nodes and edges. Each component also holds a copy of
 * the link graph settings at the time of its creation. The global settings
 * might change between the creation and join time so we can't rely on them.
 */
class LinkGraphComponent {
private:
	typedef std::vector<Node> NodeVector;
	typedef std::vector<std::vector<Edge> > EdgeMatrix;

	bool InsertNode(StationID station, uint supply, uint demand);

public:
	LinkGraphComponent();

	void Init(LinkGraphComponentID id);

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
	 * Get a reference to a node with the specified id.
	 * @param num ID of the node.
	 * @return the Requested node.
	 */
	inline Node &GetNode(NodeID num)
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

	void SetSize();

	NodeID AddNode(Station *st);
	NodeID CloneNode(NodeID node);
	NodeID SplitImport(NodeID node);
	NodeID SplitExport(NodeID node);
	NodeID SplitPassby(NodeID node, StationID second, uint capacity);

	void AddEdge(NodeID from, NodeID to, uint capacity);

	/**
	 * Get the ID of this component.
	 * @return ID.
	 */
	inline LinkGraphComponentID GetIndex() const
	{
		return this->index;
	}

	/**
	 * Get the cargo ID this component's link graph refers to.
	 * @return Cargo ID.
	 */
	inline CargoID GetCargo() const
	{
		return this->cargo;
	}

	/**
	 * Get the link graph settings for this component.
	 * @return Settings.
	 */
	inline const LinkGraphSettings &GetSettings() const
	{
		return this->settings;
	}

	/**
	 * Get the first valid edge starting at the specified node.
	 * @param from ID of the source node
	 * @return ID of the destination node
	 */
	inline NodeID GetFirstEdge(NodeID from) {return edges[from][from].next_edge;}

	/**
	 * Set the number of nodes to 0 to mark this component as done.
	 */
	inline void Clear()
	{
		this->num_nodes = 0;
	}

protected:
	LinkGraphSettings settings; ///< Copy of _settings_game.linkgraph at creation time.
	CargoID cargo;              ///< Cargo of this component's link graph.
	uint num_nodes;             ///< Number of nodes in the component.
	LinkGraphComponentID index; ///< ID of the component.
	NodeVector nodes;           ///< Nodes in the component.
	EdgeMatrix edges;           ///< Edges in the component.
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
	 * @param component Link graph component to run the handler on.
	 */
	virtual void Run(LinkGraphComponent *component) = 0;
};

/**
 * A job to be executed on a link graph component. It inherits a component and
 * keeps a static list of handlers to be run on it. It may or may not run in a
 * thread and contains a thread object for this option.
 */
class LinkGraphJob : public LinkGraphComponent {
private:
	typedef std::list<ComponentHandler *> HandlerList;

public:

	LinkGraphJob() : thread(NULL) {}

	/**
	 * Destructor; Clean up the thread if it's there.
	 */
	~LinkGraphJob()
	{
		this->Join();
	}

	static void RunLinkGraphJob(void *j);

	/**
	 * Add a handler to the end of the list.
	 * @param handler Handler to be added.
	 */
	static void AddHandler(ComponentHandler *handler)
	{
		LinkGraphJob::_handlers.push_back(handler);
	}

	static void ClearHandlers();

	void SpawnThread();

	void Join();

private:
	static HandlerList _handlers;   ///< Handlers the job is executing.
	ThreadObject *thread;           ///< Thread the job is running in or NULL if it's running in the main thread.

	/**
	 * Private Copy-Constructor: there cannot be two identical LinkGraphJobs.
	 * @param other hypothetical other job to be copied.
	 * @note It's necessary to explicitly initialize the link graph component in order to silence some compile warnings.
	 */
	LinkGraphJob(const LinkGraphJob &other) : LinkGraphComponent(other) {NOT_REACHED();}
};

/**
 * A link graph, inheriting one job.
 */
class LinkGraph : public LinkGraphJob {
public:
	/* Those are ticks where not much else is happening, so a small lag might go unnoticed. */
	static const uint COMPONENTS_JOIN_TICK  = 21; ///< Tick when jobs are joined every day.
	static const uint COMPONENTS_SPAWN_TICK = 58; ///< Tick when jobs are spawned every day.

	/**
	 * Create a link graph.
	 */
	LinkGraph() : current_station_id(0) {}

	void Init(CargoID cargo);

	void NextComponent();

	void Join();

private:
	StationID current_station_id; ///< ID of the last station examined while creating components.

	friend const SaveLoad *GetLinkGraphDesc();

	void CreateComponent(Station *first);
};

/**
 * A leg of a path in the link graph. Paths can form trees by being "forked".
 */
class Path {
public:
	Path(NodeID n, bool source = false);

	/** Get the node this leg passes. */
	inline NodeID GetNode() const {return this->node;}

	/** Get the overall origin of the path. */
	inline NodeID GetOrigin() const {return this->origin;}

	/** Get the parent leg of this one. */
	inline Path *GetParent() {return this->parent;}

	/** Get the overall capacity of the path. */
	inline uint GetCapacity() const {return this->capacity;}

	/** Get the free capacity of the path. */
	inline int GetFreeCapacity() const {return this->free_capacity;}

	/**
	 * Get ratio of free * 16 (so that we get fewer 0) /
	 * overall capacity + 1 (so that we don't divide by 0).
	 */
	inline int GetCapacityRatio() const {return (this->free_capacity << 4) / (this->capacity + 1);}

	/** Get the overall distance of the path. */
	inline uint GetDistance() const {return this->distance;}

	/** Reduce the flow on this leg only by the specified amount. */
	inline void ReduceFlow(uint f) {this->flow -= f;}

	/** Increase the flow on this leg only by the specified amount. */
	inline void AddFlow(uint f) {this->flow += f;}

	/** Get the flow on this leg. */
	inline uint GetFlow() const {return this->flow;}

	/** Get the number of "forked off" child legs of this one. */
	inline uint GetNumChildren() const {return this->num_children;}

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

	uint AddFlow(uint f, LinkGraphComponent *graph, bool only_positive);
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

void InitializeLinkGraphs();
extern LinkGraph _link_graphs[NUM_CARGO];

#endif /* LINKGRAPH_H_ */
