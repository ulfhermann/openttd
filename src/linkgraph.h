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
#include <list>
#include <vector>

struct SaveLoad;

typedef uint NodeID;

class Node {
public:
	Node() : supply(0), demand(0), station(INVALID_STATION) {}
	Node(StationID st, uint sup, uint dem) : supply(sup), demand(dem), station(st) {}
	uint supply;
	uint demand;
	StationID station;
};

class Edge {
public:
	Edge() : distance(0), capacity(0) {}
	uint distance;
	uint capacity;
};

typedef uint16 colour;

class Component {
	typedef std::vector<Node> NodeVector;

	typedef std::vector<std::vector<Edge> > EdgeMatrix;

public:
	Component(uint size, colour c);
	Component(colour c = 0);
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

typedef std::list<Component *> ComponentList;

class LinkGraph {
public:
	LinkGraph();
	void Clear();
	colour GetColour(StationID station) const {return station_colours[station];}
	CargoID GetCargo() const {return cargo;}
	void NextComponent();
	void InitColours();
	void Join();
	uint GetNumComponents() const {return components.size();}
	ComponentList & GetComponents() {return components;}
	void AddComponent(Component * component);
	const static uint COMPONENTS_SPAWN_TICK = 21;
	const static uint COMPONENTS_JOIN_TICK  = 58;
private:
	friend const SaveLoad * GetLinkGraphDesc(uint);
	colour current_colour;
	StationID current_station;
	CargoID cargo;
	colour station_colours[Station_POOL_MAX_BLOCKS];
	ComponentList components;
};

extern LinkGraph _link_graphs[NUM_CARGO];

#endif /* LINKGRAPH_H_ */
