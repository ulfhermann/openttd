/*
 * linkgraph.h
 *
 *  Created on: 28.02.2009
 *      Author: alve
 */

#ifndef LINKGRAPH_H_
#define LINKGRAPH_H_

#include "stdafx.h"
#include "station_base.h"
#include "cargo_type.h"
#include "thread.h"
#include "settings_type.h"
#include <list>
#include <vector>
#include <set>

struct SaveLoad;
class Path;

typedef uint16 colour;
typedef uint NodeID;
typedef std::set<Path *> PathSet;
typedef std::map<StationID, Number> FlowViaMap;
typedef std::map<StationID, FlowViaMap> FlowMap;

class Node {
public:
	static const NodeID INVALID = UINT_MAX;
	Node() : supply(0), demand(0), station(INVALID_STATION) {}
	~Node();
	Node(StationID st, uint sup, uint dem) : supply(sup), undelivered_supply(sup), demand(dem), station(st) {}
	void ExportFlows(FlowStatMap & station_flows);
	uint supply;
	uint undelivered_supply;
	uint demand;
	StationID station;
	PathSet paths;
	FlowMap flows;
private:
	void ExportNewFlows(FlowMap::iterator & source_flows_it, FlowStatSet & via_set);
};

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

typedef uint16 colour;

class LinkGraphComponent {
	typedef std::vector<Node> NodeVector;
	typedef std::vector<std::vector<Edge> > EdgeMatrix;

public:
	LinkGraphComponent(CargoID cargo, colour c = 0);
	Edge & GetEdge(NodeID from, NodeID to) {return edges[from][to];}
	Node & GetNode(NodeID num) {return nodes[num];}
	uint GetSize() const {return num_nodes;}
	void SetSize(uint size);
	uint AddNode(StationID st, uint supply, uint demand);
	void AddEdge(NodeID from, NodeID to, uint capacity);
	void CalculateDistances();
	colour GetColour() const {return component_colour;}
	CargoID GetCargo() const {return cargo;}
	const LinkGraphSettings & GetSettings() const {return settings;}
	NodeID GetFirstEdge(NodeID from) {return edges[from][from].next_edge;}
private:
	friend const SaveLoad * GetLinkGraphComponentDesc();
	LinkGraphSettings settings;
	CargoID cargo;
	uint num_nodes;
	colour component_colour;
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
	class Exception {};
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
	colour GetColour(StationID station) const {return station_colours[station];}
	CargoID GetCargo() const {return cargo;}
	void NextComponent();
	void InitColours();

	void Join();
	uint GetNumJobs() const {return jobs.size();}
	JobList & GetJobs() {return jobs;}
	void AddComponent(LinkGraphComponent * component, uint join);

	const static uint COMPONENTS_JOIN_TICK  = 21;
	const static uint COMPONENTS_SPAWN_TICK = 58;

private:
	friend const SaveLoad * GetLinkGraphDesc(uint);
	colour current_colour;
	StationID current_station;
	CargoID cargo;
	colour station_colours[Station_POOL_MAX_BLOCKS];
	JobList jobs;
};

class Path {
public:
	Path(NodeID n, bool source = false);
	NodeID GetNode() const {return node;}
	NodeID GetOrigin() const {return parent == NULL ? node : parent->GetOrigin();}
	Path * GetParent() {return parent;}
	uint GetCapacity() const {return capacity;}
	void Fork(Path * base, uint cap, uint dist);
	uint AddFlow(uint f, LinkGraphComponent * graph);
	uint GetFlow() {return flow;}
	uint GetNumChildren() {return num_children;}
	void UnFork();
protected:
	uint distance;
	uint capacity;
	uint flow;
	NodeID node;
	uint num_children;
	Path * parent;
};

extern LinkGraph _link_graphs[NUM_CARGO];

#endif /* LINKGRAPH_H_ */
