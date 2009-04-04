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
#include <list>
#include <vector>
#include <set>

struct SaveLoad;

typedef uint NodeID;

class Path;

typedef std::set<Path *> PathSet;

typedef std::map<StationID, uint> FlowViaMap;
typedef std::map<StationID, FlowViaMap> FlowMap;


class Node {
public:
	Node() : supply(0), demand(0), station(INVALID_STATION) {}
	~Node();
	Node(StationID st, uint sup, uint dem) : supply(sup), undelivered_supply(sup), demand(dem), station(st) {}
	uint supply;
	uint undelivered_supply;
	uint demand;
	StationID station;
	PathSet paths;
	FlowMap flows;
};

class Edge {
public:
	Edge() : distance(0), capacity(0), demand(0) {}
	uint distance;
	uint capacity;
	uint demand;
};

typedef ushort colour;

class Component {
	typedef std::vector<Node> NodeVector;

	typedef std::vector<std::vector<Edge> > EdgeMatrix;

public:
	Component(uint size, colour c);
	Component(colour c = USHRT_MAX);
	Edge & GetEdge(NodeID from, NodeID to) {return edges[from][to];}
	Node & GetNode(NodeID num) {return nodes[num];}
	uint GetSize() const {return num_nodes;}
	void SetSize(uint size);
	uint AddNode(StationID st, uint supply, uint demand);
	void AddEdge(NodeID from, NodeID to, uint capacity);
	void CalculateDistances();
	colour GetColour() const {return component_colour;}
private:
	friend const SaveLoad * GetLinkGraphDesc(uint);
	uint num_nodes;
	colour component_colour;
	NodeVector nodes;
	EdgeMatrix edges;

};

class ComponentHandler {
public:
	virtual void Run(Component * component) = 0;
	virtual ~ComponentHandler() {}
};

class LinkGraphJob {
	typedef std::list<ComponentHandler *> HandlerList;
public:
	LinkGraphJob(Component * c);
	LinkGraphJob(Component * c, uint join);
	void AddHandler(ComponentHandler * handler) {handlers.push_back(handler);}
	void Run();
	void SpawnThread(CargoID cargo);
	uint GetJoinTime() const {return join_time;}
	void Join() {if (thread != NULL) thread->Join();}
	Component * GetComponent() {return component;}
	~LinkGraphJob();
private:
	ThreadObject * thread;
	uint join_time;
	Component * component;
	HandlerList handlers;
};

typedef std::list<LinkGraphJob> JobList;

class LinkGraph {
public:
	LinkGraph();
	void Clear();
	colour GetColour(StationID station) const {return station_colours[station];}
	CargoID GetCargo() const {return cargo;}
	bool NextComponent();
	void InitColours();
	bool Join();
	uint GetNumJobs() const {return jobs.size();}
	JobList & GetJobs() {return jobs;}
	void AddComponent(Component * component, uint join);
	const static uint COMPONENTS_TICK = 21;
private:
	friend const SaveLoad * GetLinkGraphDesc(uint);
	void StartJob(Component * component);
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
	float GetCapacity() const {return capacity;}
	void Fork(Path * base, float cap, float dist);
	void AddFlow(float f, Component * graph);
	float GetFlow() {return flow;}
	uint GetNumChildren() {return num_children;}
	void UnFork();
protected:
	float distance;
	float capacity;
	float flow;
	NodeID node;
	uint num_children;
	Path * parent;
};

extern LinkGraph _link_graphs[NUM_CARGO];

void InitializeLinkGraphs();

#endif /* LINKGRAPH_H_ */
