/** @file linkgraph.h Declaration of link graph classes used for cargo distribution. */

#ifndef LINKGRAPH_H_
#define LINKGRAPH_H_

#include "../stdafx.h"
#include "../station_base.h"
#include "../cargo_type.h"
#include "../thread/thread.h"
#include "../settings_type.h"
#include "linkgraph_types.h"
#include <list>
#include <vector>
#include <set>

struct SaveLoad;
class Path;

typedef std::set<Path *> PathSet;
typedef std::map<NodeID, Path *> PathViaMap;
typedef std::map<StationID, int> FlowViaMap;
typedef std::map<StationID, FlowViaMap> FlowMap;

class Node {
public:
	static const NodeID INVALID = UINT_MAX;
	Node() : supply(0), undelivered_supply(0), demand(0), station(INVALID_STATION) {}
	Node(StationID st, uint sup, uint dem) : supply(sup), undelivered_supply(sup), demand(dem), station(st) {}
	~Node();
	void ExportFlows(FlowStatMap & station_flows, CargoID cargo);
	uint supply;
	uint undelivered_supply;
	uint demand;
	StationID station;
	PathSet paths;
	FlowMap flows;
private:
	void ExportNewFlows(FlowMap::iterator & source_flows_it, FlowStatSet & via_set, CargoID cargo);
};

typedef std::set<NodeID> ViaSet;

class Edge {
public:
	Edge() : distance(0), capacity(0), demand(0), unsatisfied_demand(0), flow(0), next_edge(Node::INVALID) {}
	uint distance;
	uint capacity;
	uint demand;
	uint unsatisfied_demand;
	uint flow;
	NodeID next_edge;
};

class LinkGraphComponent {
	typedef std::vector<Node> NodeVector;
	typedef std::vector<std::vector<Edge> > EdgeMatrix;

public:
	LinkGraphComponent(CargoID cargo, LinkGraphComponentID c = 0);
	Edge & GetEdge(NodeID from, NodeID to) {return edges[from][to];}
	Node & GetNode(NodeID num) {return nodes[num];}
	uint GetSize() const {return num_nodes;}
	void SetSize(uint size);
	NodeID AddNode(StationID st, uint supply, uint demand);
	void AddEdge(NodeID from, NodeID to, uint capacity);
	void CalculateDistances();
	LinkGraphComponentID GetIndex() const {return index;}
	CargoID GetCargo() const {return cargo;}
	const LinkGraphSettings & GetSettings() const {return settings;}
	NodeID GetFirstEdge(NodeID from) {return edges[from][from].next_edge;}
private:
	friend const SaveLoad * GetLinkGraphComponentDesc();
	LinkGraphSettings settings;
	CargoID cargo;
	uint num_nodes;
	LinkGraphComponentID index;
	NodeVector nodes;
	EdgeMatrix edges;
};

class ComponentHandler {
public:
	virtual void Run(LinkGraphComponent * component) = 0;
	virtual ~ComponentHandler() {}
};

class LinkGraphJob {
	typedef std::list<ComponentHandler *> HandlerList;
public:
	LinkGraphJob(LinkGraphComponent * c);
	LinkGraphJob(LinkGraphComponent * c, Date join);

	void AddHandler(ComponentHandler * handler) {handlers.push_back(handler);}
	void Run();
	void SpawnThread(CargoID cargo);
	void Join() {if (thread != NULL) thread->Join();}
	Date GetJoinDate() {return join_date;}
	LinkGraphComponent * GetComponent() {return component;}
	~LinkGraphJob();
private:
	/**
	 * there cannot be two identical LinkGraphJobs,
	 */
	LinkGraphJob(const LinkGraphJob & other) {NOT_REACHED();}
	ThreadObject * thread;
	Date join_date;
	LinkGraphComponent * component;
	HandlerList handlers;
};

typedef std::list<LinkGraphJob *> JobList;

class LinkGraph {
public:
	LinkGraph();
	void Clear();
	CargoID GetCargo() const {return cargo;}
	/**
	 * Starts calcluation of the next component of the link graph.
	 * Uses a breadth first search on the graph spanned by the
	 * stations' link stats.
	 *
	 * TODO: This method could be changed to only search a defined number
	 * of stations in each run, thus decreasing the delay. The state of
	 * the search queue would have to be saved and loaded then.
	 */
	void NextComponent();

	/**
	 * Merges the results of the link graph calculation into the main
	 * game state.
	 *
	 * TODO: This method could be changed to only merge a fixed number of
	 * nodes in each run. In order to do so, the ID of last node merged
	 * would have to be saved and loaded. Merging only a fixed  number
	 * of nodes is faster than merging all nodes of the component.
	 */
	void Join();
	uint GetNumJobs() const {return jobs.size();}
	JobList & GetJobs() {return jobs;}
	void AddComponent(LinkGraphComponent * component, uint join);

	const static uint COMPONENTS_JOIN_TICK  = 21;
	const static uint COMPONENTS_SPAWN_TICK = 58;

private:
	friend const SaveLoad * GetLinkGraphDesc(uint);
	void CreateComponent(Station * first);
	LinkGraphComponentID current_component_id;
	StationID current_station_id;
	CargoID cargo;
	JobList jobs;
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
