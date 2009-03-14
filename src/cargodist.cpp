/*
 * cargodist.cpp
 *
 *  Created on: 12.03.2009
 *      Author: alve
 */

#include "cargodist.h"
#include "station_base.h"
#include "map_func.h"

typedef std::map<StationID, uint> ReverseNodeIndex;

CargoDistGraph::CargoDistGraph(uint pNumNodes) :
	numNodes(pNumNodes), nodes(pNumNodes), edges(pNumNodes, std::vector<Edge>(pNumNodes)) {}

CargoDistGraph::CargoDistGraph(const InitNodeList & pNodes, const InitEdgeList & pEdges, uint pNumNodes) :
	numNodes(pNumNodes), nodes(pNumNodes), edges(pNumNodes, std::vector<Edge>(pNumNodes)) {
	ReverseNodeIndex indices;
	uint index = 0;
	for(InitNodeList::const_iterator i = pNodes.begin(); i != pNodes.end(); ++i) {
		indices[i->station] = index;
		nodes[index++] = *i;
	}


	for(InitEdgeList::const_iterator i = pEdges.begin(); i != pEdges.end(); ++i) {
		edges[indices[i->from]][indices[i->to]].capacity = i->capacity;
	}

	for(uint i = 0; i < numNodes; ++i) {
		for(uint j = 0; j < i; ++j) {
			Station * st1 = GetStation(nodes[i].station);
			Station * st2 = GetStation(nodes[j].station);
			uint distance = DistanceManhattan(st1->xy, st2->xy);
			edges[i][j].distance = distance;
			edges[j][i].distance = distance;
		}
	}
}

InitEdge::InitEdge(StationID pFrom, StationID pTo, uint pCap) :
	from(pFrom), to(pTo), capacity(pCap) {}

void CargoDistGraph::setSize(uint pSize) {
	numNodes = pSize;
	nodes.reserve(numNodes);
	edges.resize(numNodes, std::vector<Edge>(numNodes));
}
