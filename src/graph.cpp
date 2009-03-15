/*
 * graph.cpp
 *
 *  Created on: 28.02.2009
 *      Author: alve
 */


#include "graph.h"
#include "settings_type.h"
#include "station_func.h"
#include "date_func.h"
#include "variables.h"
#include <queue>

Graph _link_graphs[NUM_CARGO];

bool Graph::NextComponent()
{
	InitNodeList nodes;
	InitEdgeList edges;
	uint numNodes = 0;
	std::queue<Station *> searchQueue;
	while (true) {
		// find first station of next component
		if (station_colours[current_station] > USHRT_MAX / 2 && IsValidStationID(current_station)) {
			Station * station = GetStation(current_station);
			LinkStatMap & links = station->goods[cargo].link_stats;
			if (!links.empty()) {
				if (++c == USHRT_MAX / 2) {
					c = 0;
				}
				searchQueue.push(station);
				station_colours[current_station] = c;
				break; // found a station
			}
		}
		if (++current_station == GetMaxStationIndex()) {
			current_station = 0;
			return false;
		}
	}
	// find all stations belonging to the current component
	while(!searchQueue.empty()) {
		Station * target = searchQueue.front();
		StationID targetID = target->index;
		searchQueue.pop();
		GoodsEntry & good = target->goods[cargo];
		nodes.push_back(InitNode(targetID, good.supply));
		numNodes++;
		LinkStatMap & links = good.link_stats;
		for(LinkStatMap::iterator i = links.begin(); i != links.end(); ++i) {
			StationID sourceID = i->first;
			Station * source = GetStation(i->first);
			LinkStat & linkStat = i->second;
			if (station_colours[sourceID] != c) {
				station_colours[sourceID] = c;
				searchQueue.push(source);
			}
			edges.push_back(InitEdge(sourceID, targetID, linkStat.capacity));
		}
	}
	// here the list of nodes and edges for this component is complete.
	return true;
}

void Graph::InitColours()
{
	for (int i = 0; i < Station_POOL_MAX_BLOCKS; ++i) {
		station_colours[i] = USHRT_MAX;
	}
}


void OnTick_LinkGraph()
{
	if ((_tick_counter + Graph::COMPONENTS_TICK) % DAY_TICKS == 0) {
		CargoID cargo = (_date) % NUM_CARGO;
		Graph & graph = _link_graphs[cargo];
		if (!graph.NextComponent()) {
			graph.InitColours();
		}
	}
}

Graph::Graph()  : c(0), current_station(0), cargo(CT_INVALID)
{
	for (CargoID i = 0; i < NUM_CARGO; ++i) {
		if (this == &(_link_graphs[i])) {
			cargo = i;
		}
	}
	InitColours();
}
