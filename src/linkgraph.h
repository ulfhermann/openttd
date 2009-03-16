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

struct SaveLoad;

class Node {
public:
	Node() : supply(0), station(INVALID_STATION) {}
	Node(StationID st, uint sup) : supply(sup), station(st) {}
	uint supply;
	StationID station;
};

class Edge {
public:
	Edge() : distance(0), capacity(0), demand(0) {}
	uint distance;
	uint capacity;
	uint demand;
};




void SpawnComponentThread(void * handlers);

typedef ushort colour;

class Component {
	typedef std::vector<Node> NodeVector;

	typedef std::vector<std::vector<Edge> > EdgeMatrix;

public:
	Component(uint size, uint join, colour c);
	Component(colour c);
	Edge & GetEdge(uint from, uint to) {
		return edges[from][to];
	}

	Node & GetNode(uint num) {
		return nodes[num];
	}

	void Join() {thread->Join();}
	uint GetSize() const {return num_nodes;}
	void SetSize(uint size);
	uint AddNode(StationID st, uint supply);
	void AddEdge(uint from, uint to, uint capacity);
	void CalculateDistances();
	uint GetJoinTime() const {return join_time;}
	colour GetColour() const {return component_colour;}
	ThreadObject * & GetThread() {return thread;}
private:
	friend const SaveLoad * GetLinkGraphDesc(uint);
	ThreadObject * thread;
	uint num_nodes;
	uint join_time;
	colour component_colour;
	NodeVector nodes;
	EdgeMatrix edges;

};

typedef std::list<Component *> ComponentList;

class ComponentHandler {
public:
	virtual void Run(Component * component) = 0;
	virtual ~ComponentHandler() {}
};

class LinkGraphJob {
	typedef std::list<ComponentHandler *> HandlerList;
public:
	LinkGraphJob(Component * c) : component(c) {}
	void AddHandler(ComponentHandler * handler) {handlers.push_back(handler);}
	void Run();
	~LinkGraphJob();
private:
	Component * component;
	HandlerList handlers;
};


class LinkGraph {
public:
	LinkGraph();
	colour GetColour(StationID station) const {return station_colours[station];}
	CargoID GetCargo() const {return cargo;}
	bool NextComponent();
	void InitColours();
	bool Join();
	uint GetNumComponents() const {return components.size();}
	ComponentList & GetComponents() {return components;}
	void AddComponent(Component * component);
	const static uint COMPONENTS_TICK = 21;
private:
	friend const SaveLoad * GetLinkGraphDesc(uint);
	void SpawnComponentThread(Component * c);
	colour current_colour;
	StationID current_station;
	CargoID cargo;
	colour station_colours[Station_POOL_MAX_BLOCKS];
	ComponentList components;
};

extern LinkGraph _link_graphs[NUM_CARGO];


#endif /* LINKGRAPH_H_ */
