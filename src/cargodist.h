/*
 * cargodist.h
 *
 *  Created on: 07.03.2009
 *      Author: alve
 */

#ifndef CARGODIST_H_
#define CARGODIST_H_

#include "stdafx.h"
#include "station_type.h"
#include <vector>
#include <map>
#include <list>

class InitEdge {
public:
	InitEdge(StationID pFrom, StationID pTo, uint pCap);
	StationID from;
	StationID to;
	uint capacity;
};


class Node {
public:
	Node() : supply(0), undeliveredSupply(0), station(INVALID_STATION) {}
	Node(StationID pStation, uint pSupply) : supply(pSupply), undeliveredSupply(pSupply), station(pStation) {}
	uint supply;
	uint undeliveredSupply;
	StationID station;
};


typedef std::list<Node> InitNodeList;
typedef std::list<InitEdge> InitEdgeList;


class Edge {
public:
	Edge() : capacity(0), usage(0), demand(0) {}
	uint distance;
	uint capacity;
	uint usage;
	uint demand;
};



class CargoDistGraph {
	typedef std::vector<Node> NodeVector;

	typedef std::vector<std::vector<Edge> > EdgeMatrix;

public:
	CargoDistGraph(uint numNodes);
	CargoDistGraph(const InitNodeList & pNodes, const InitEdgeList & pEdges, uint numNodes);
	Edge & edge(uint from, uint to) {
		return edges[from][to];
	}

	Node & node(uint num) {
		return nodes[num];
	}

	uint size() const {return numNodes;}
	void setSize(uint size);
private:
	uint numNodes;
	NodeVector nodes;
	EdgeMatrix edges;

};

#endif /* CARGODIST_H_ */
