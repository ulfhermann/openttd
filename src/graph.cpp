/*
 * graph.cpp
 *
 *  Created on: 28.02.2009
 *      Author: alve
 */


#include "graph.h"
#include "demands.h"
#include "settings_type.h"
#include "station_func.h"
#include "date_func.h"
#include "variables.h"

ComponentHandler CargoDist::handlers[NUM_CARGO];


bool ComponentHandler::nextComponent() {
	InitNodeList nodes;
	InitEdgeList edges;
	uint numNodes = 0;
	std::queue<Station *> searchQueue;
	while (true) {
		// find first station of next component
		if (stationColours[currentStation] > USHRT_MAX / 2 && IsValidStationID(currentStation)) {
			Station * station = GetStation(currentStation);
			LinkStatMap & links = station->goods[cargo].link_stats;
			if (!links.empty()) {
				if (++c == USHRT_MAX / 2) {
					c = 0;
				}
				searchQueue.push(station);
				stationColours[currentStation] = c;
				break; // found a station
			}
		}
		if (++currentStation == GetMaxStationIndex()) {
			currentStation = 0;
			return false;
		}
	}
	// find all stations belonging to the current component
	while(!searchQueue.empty()) {
		Station * target = searchQueue.front();
		StationID targetID = target->index;
		searchQueue.pop();
		GoodsEntry & good = target->goods[cargo];
		nodes.push_back(Node(targetID, good.supply));
		numNodes++;
		LinkStatMap & links = good.link_stats;
		for(LinkStatMap::iterator i = links.begin(); i != links.end(); ++i) {
			StationID sourceID = i->first;
			Station * source = GetStation(i->first);
			LinkStat & linkStat = i->second;
			if (stationColours[sourceID] != c) {
				stationColours[sourceID] = c;
				searchQueue.push(source);
			}
			edges.push_back(InitEdge(sourceID, targetID, linkStat.capacity));
		}
	}
	CargoDist * cd = new CargoDist(nodes, edges, numNodes, cargo, c);
	components.push_back(cd);
	cd->start();
	return true;
}


bool ComponentHandler::join() {
	if (components.empty()) {
		return false;
	}
	CargoDist * cd = components.front();

	if (cd->getJoinTime() > _tick_counter) {
		return false;
	}

	components.pop_front();

	cd->join();
	CargoDistGraph & graph = cd->getGraph();

	for(uint i = 0; i < graph.size(); ++i) {
		Node & node = graph.node(i);
		StationID id = node.station;
		stationColours[id] += USHRT_MAX / 2;
		if (id < currentStation) currentStation = id;
		//TODO: handle edges
	}
	return true;
}

CargoDist::CargoDist(CargoID cargo) :
	graph(0), demands(cargo), joinTime(0), componentColour(0) {}

CargoDist::CargoDist(const InitNodeList & nodes, const InitEdgeList & edges, uint numNodes, CargoID cargo, colour pCol) :
	graph(nodes, edges, numNodes), demands(cargo), joinTime(_tick_counter + _settings_game.economy.cdist_recalc_interval * DAY_TICKS), componentColour(pCol)
{}

void runCargoDist(void * cd) {
	CargoDist * cargoDist = (CargoDist *)cd;
	cargoDist->calculateDemands();
	//cargoDist->calculateUsage();
}

void OnTick_CargoDist() {
	if ((_tick_counter + ComponentHandler::CARGO_DIST_TICK) % DAY_TICKS == 0) {
		CargoID cargo = (_date) % NUM_CARGO;
		ComponentHandler & handler = CargoDist::handlers[cargo];
		if (!handler.nextComponent()) {
			handler.join();
		}
	}
}

ComponentHandler::ComponentHandler()  : c(0), currentStation(0), cargo(CT_INVALID) {
	for (int i = 0; i < Station_POOL_MAX_BLOCKS; ++i) {
		stationColours[i] = USHRT_MAX;
	}
	//memset(stationColors, CHAR_MAX, sizeof(stationColors));
	for (CargoID i = 0; i < NUM_CARGO; ++i) {
		if (this == &(CargoDist::handlers[i])) {
			cargo = i;
		}
	}
}

uint ComponentHandler::getNumComponents() {
	return components.size();
}

void ComponentHandler::addComponent(CargoDist * component) {
	 components.push_back(component);
	 CargoDistGraph & graph = component->getGraph();
	 colour componentColour = component->getColour();
	 for(uint i = 0; i < graph.size(); ++i) {
		 stationColours[graph.node(i).station] = componentColour;
	 }
	 component->start();
}
