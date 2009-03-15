/*
 * graph.h
 *
 *  Created on: 28.02.2009
 *      Author: alve
 */

#ifndef GRAPH_H_
#define GRAPH_H_

#include "stdafx.h"
#include "station_base.h"
#include "cargo_type.h"
#include <list>

class InitEdge {
public:
	InitEdge(StationID st1, StationID st2, uint cap) :
		from(st1), to(st2), capacity(cap) {}
	StationID from;
	StationID to;
	uint capacity;
};

class InitNode {
public:
	InitNode() : supply(0), station(INVALID_STATION) {}
	InitNode(StationID st, uint sup) : supply(sup), station(st) {}
	uint supply;
	StationID station;
};

typedef std::list<InitNode> InitNodeList;
typedef std::list<InitEdge> InitEdgeList;

typedef ushort colour;

class Graph {
public:
	Graph();
	colour GetColour(StationID station) const {return station_colours[station];}
	CargoID GetCargo() const {return cargo;}
	bool NextComponent();
	void InitColours();
	const static uint COMPONENTS_TICK = 21;

private:
	colour c;
	StationID current_station;
	CargoID cargo;
	colour station_colours[Station_POOL_MAX_BLOCKS];
};

extern Graph _link_graphs[NUM_CARGO];





#endif /* GRAPH_H_ */
