/** @file linkgraph.h Declaration of link graph classes used for cargo distribution. */

#ifndef LINKGRAPH_H_
#define LINKGRAPH_H_

#include "../stdafx.h"
#include "../station_base.h"
#include "../cargo_type.h"
#include "../thread/thread.h"
#include "../settings_type.h"
#include "../date_func.h"
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

/**
 * Node of the link graph. contains all relevant information from the associated
 * station. It's copied so that the link graph job can work on its own data set
 * in a separate thread.
 */
class Node {
public:
	void Init(StationID st = INVALID_STATION, uint sup = 0, uint dem = 0);

	/**
	 * Clear a node on destruction to delete paths that might remain.
	 */
	~Node() {this->Init();}

	void ExportFlows(FlowStatMap &station_flows, CargoID cargo);

	uint supply;             ///< supply at the station
	uint undelivered_supply; ///< amount of supply that hasn't been distributed yet
	uint demand;             ///< acceptance at the station
	StationID station;       ///< the station's ID
	PathSet paths;           ///< paths through this node
	FlowMap flows;           ///< planned flows to other nodes

private:
	void ExportNewFlows(FlowMap::iterator &source_flows_it, FlowStatSet &via_set, CargoID cargo);
};

/**
 * An edge in the link graph. Corresponds to a link between two stations or at
 * least the distance between them. Edges from one node to itself contain the
 * ID of the opposite Node of the first active edge (i.e. not just distance) in
 * the column as next_edge.
 */
class Edge {
public:
	/**
	 * Create an edge.
	 * @param distance length of the link as manhattan distance
	 * @param capacity capacity of the link
	 */
	FORCEINLINE void Init(uint distance = 0, uint capacity = 0)
	{
		this->distance = distance;
		this->capacity = capacity;
		this->demand = 0;
		this->unsatisfied_demand = 0;
		this->flow = 0;
		this->next_edge = INVALID_NODE;
	}

	uint distance;           ///< length of the link
	uint capacity;           ///< capacity of the link
	uint demand;             ///< transport demand between the nodes
	uint unsatisfied_demand; ///< demand over this edge that hasn't been satisfied yet
	uint flow;               ///< planned flow over this edge
	NodeID next_edge;        ///< destination of next valid edge starting at the same source node
};

/**
 * A connected component of a link graph. Contains a complete set of stations
 * connected by links as nodes and edges. Each component also holds a copy of
 * the link graph settings at the time of its creation. The global settings
 * might change between the creation and join time so we can't rely on them.
 */
class LinkGraphComponent {
	typedef std::vector<Node> NodeVector;
	typedef std::vector<std::vector<Edge> > EdgeMatrix;

public:
	LinkGraphComponent();

	void Init(LinkGraphComponentID id);

	/**
	 * Get a reference to an edge.
	 * @param from the origin node
	 * @param the destination node
	 * @return the edge between from and to
	 */
	FORCEINLINE Edge &GetEdge(NodeID from, NodeID to) {return this->edges[from][to];}

	/**
	 * Get a reference to a node with the specified id.
	 * @param num ID of the node
	 * @return the requested node
	 */
	FORCEINLINE Node &GetNode(NodeID num) {return this->nodes[num];}

	/**
	 * Get the current size of the component.
	 * @return the size
	 */
	FORCEINLINE uint GetSize() const {return this->num_nodes;}

	void SetSize();

	NodeID AddNode(Station *st);

	FORCEINLINE void AddEdge(NodeID from, NodeID to, uint capacity);

	/**
	 * Get the ID of this component.
	 * @return the ID
	 */
	FORCEINLINE LinkGraphComponentID GetIndex() const {return this->index;}

	/**
	 * Get the cargo ID this component's link graph refers to.
	 * @return the cargo ID
	 */
	FORCEINLINE CargoID GetCargo() const {return this->cargo;}

	/**
	 * Get the link graph settings for this component.
	 * @return the settings
	 */
	FORCEINLINE const LinkGraphSettings &GetSettings() const {return this->settings;}

	/**
	 * Get the first valid edge starting at the specified node.
	 * @param from ID of the source node
	 * @return ID of the destination node
	 */
	FORCEINLINE NodeID GetFirstEdge(NodeID from) {return edges[from][from].next_edge;}

	/**
	 * Set the number of nodes to 0 to mark this component as done.
	 */
	FORCEINLINE void Clear() {this->num_nodes = 0;}

protected:
	LinkGraphSettings settings; ///< Copy of _settings_game.linkgraph at creation time
	CargoID cargo;              ///< Cargo of this component's link graph
	uint num_nodes;             ///< Number of nodes in the component
	LinkGraphComponentID index; ///< ID of the component
	NodeVector nodes;           ///< Nodes in the component
	EdgeMatrix edges;           ///< Edges in the component
};

/**
 * A handler doing "something" on a link graph component. It must not keep any
 * state as it is called concurrently from different threads.
 */
class ComponentHandler {
public:
	/**
	 * Run the handler. A link graph handler must not read or write any data
	 * outside the given component as that would create a potential desync.
	 */
	virtual void Run(LinkGraphComponent *component) = 0;

	/**
	 * Destroy the handler. Must be given due to virtual Run.
	 */
	virtual ~ComponentHandler() {}
};

/**
 * A job to be executed on a link graph component. It inherits a component and
 * keeps a static list of handlers to be run on it. It may or may not run in a
 * thread and contains a thread object for this option.
 */
class LinkGraphJob : public LinkGraphComponent {
	typedef std::list<ComponentHandler *> HandlerList;
public:

	LinkGraphJob() : thread(NULL) {}

	/**
	 * Add a handler to the end of the list.
	 * @param handler the handler to be added
	 */
	static void AddHandler(ComponentHandler *handler)
		{LinkGraphJob::_handlers.push_back(handler);}

	static void ClearHandlers();

	void SpawnThread();

	/**
	 * Join the calling thread with this job's thread if threading is enabled.
	 */
	FORCEINLINE void Join() {
		if (this->thread != NULL) {
			this->thread->Join();
			delete this->thread;
			this->thread = NULL;
		}
	}

	/**
	 * Destructor; Clean up the thread if it's there.
	 */
	~LinkGraphJob() {this->Join();}

	static void RunLinkGraphJob(void *j);
private:

	/**
	 * Private Copy-Constructor: there cannot be two identical LinkGraphJobs.
	 * @param other hypothetical other job to be copied.
	 */
	LinkGraphJob(const LinkGraphJob &other) {NOT_REACHED();}

	ThreadObject *thread;           ///< Thread the job is running in or NULL if it's running in the main thread
	static HandlerList _handlers;   ///< Handlers the job is executing
};

/**
 * A link graph, inheriting one job.
 */
class LinkGraph : public LinkGraphJob {
public:
	/**
	 * Create a link graph.
	 */
	LinkGraph() : current_station_id(0) {}

	void Init(CargoID cargo);

	void NextComponent();

	void Join();

	const static uint COMPONENTS_JOIN_TICK  = 21; ///< tick when jobs are joined every day
	const static uint COMPONENTS_SPAWN_TICK = 58; ///< tick when jobs are spawned every day

private:
	friend const SaveLoad *GetLinkGraphDesc();

	void CreateComponent(Station *first);

	StationID current_station_id; ///< ID of the last station examined while creating components
};

class Path {
public:
	Path(NodeID n, bool source = false);
	NodeID GetNode() const {return node;}
	NodeID GetOrigin() const {return origin;}
	Path * GetParent() {return parent;}
	int GetCapacity() const {return capacity;}
	uint GetDistance() const {return distance;}
	void Fork(Path * base, int cap, uint dist);
	void ReduceFlow(uint f) {flow -= f;}
	void AddFlow(uint f) {flow += f;}
	uint AddFlow(uint f, LinkGraphComponent * graph, bool only_positive);
	uint GetFlow() const {return flow;}
	uint GetNumChildren() const {return num_children;}
	void UnFork();
protected:
	uint distance;
	int capacity;      ///< this capacity is edge.capacity - edge.flow for the current run of dijkstra
	uint flow;         ///< this is the flow the current run of the mcf solver assigns
	NodeID node;
	NodeID origin;
	uint num_children;
	Path * parent;
};

extern LinkGraph _link_graphs[NUM_CARGO];

#endif /* LINKGRAPH_H_ */
