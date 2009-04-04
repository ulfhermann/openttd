/*
 * graph.cpp
 *
 *  Created on: 28.02.2009
 *      Author: alve
 */


#include "linkgraph.h"
#include "settings_type.h"
#include "station_func.h"
#include "date_func.h"
#include "variables.h"
#include "map_func.h"
#include "core/bitmath_func.hpp"
#include <queue>

LinkGraph _link_graphs[NUM_CARGO];

typedef std::map<StationID, NodeID> ReverseNodeIndex;

void LinkGraph::NextComponent()
{
	StationID last_station = current_station;
	ReverseNodeIndex index;
	NodeID node = 0;
	std::queue<Station *> search_queue;
	Component * component = NULL;
	while (true) {
		// find first station of next component
		if (station_colours[current_station] == 0 && IsValidStationID(current_station)) {
			Station * station = GetStation(current_station);
			LinkStatMap & links = station->goods[cargo].link_stats;
			if (!links.empty()) {
				if (++current_colour == UINT16_MAX) {
					current_colour = 1;
				}
				search_queue.push(station);
				station_colours[current_station] = current_colour;
				component = new Component(current_colour);
				GoodsEntry & good = station->goods[cargo];
				node = component->AddNode(current_station, good.supply, HasBit(good.acceptance_pickup, GoodsEntry::ACCEPTANCE));
				index[current_station++] = node;
				break; // found a station
			}
		}
		if (++current_station == GetMaxStationIndex()) {
			current_station = 0;
			InitColours();
		}
		if (current_station == last_station) {
			return;
		}
	}
	// find all stations belonging to the current component
	while(!search_queue.empty()) {
		Station * target = search_queue.front();
		StationID target_id = target->index;
		search_queue.pop();
		GoodsEntry & good = target->goods[cargo];
		LinkStatMap & links = good.link_stats;
		for(LinkStatMap::iterator i = links.begin(); i != links.end(); ++i) {
			StationID source_id = i->first;
			Station * source = GetStation(i->first);
			LinkStat & link_stat = i->second;
			if (station_colours[source_id] != current_colour) {
				station_colours[source_id] = current_colour;
				search_queue.push(source);
				GoodsEntry & good = source->goods[cargo];
				node = component->AddNode(source_id, good.supply, HasBit(good.acceptance_pickup, GoodsEntry::ACCEPTANCE));
				index[source_id] = node;
			} else {
				node = index[source_id];
			}
			component->AddEdge(node, index[target_id], link_stat.capacity);
		}
	}
	// here the list of nodes and edges for this component is complete.
	component->CalculateDistances();
	components.push_back(component);
}

void LinkGraph::InitColours()
{
	memset(station_colours, 0, Station_POOL_MAX_BLOCKS * sizeof(uint16));
}


void OnTick_LinkGraph()
{
	bool spawn = (_tick_counter + LinkGraph::COMPONENTS_SPAWN_TICK) % DAY_TICKS == 0;
	bool join =  (_tick_counter + LinkGraph::COMPONENTS_JOIN_TICK)  % DAY_TICKS == 0;
	if (spawn || join) {
		for(CargoID cargo = CT_BEGIN; cargo != CT_END; ++cargo) {
			if ((_date + cargo) % _settings_game.economy.linkgraph_recalc_interval == 0) {
				LinkGraph & graph = _link_graphs[cargo];
				if (spawn) {
					graph.NextComponent();
				} else {
					graph.Join();
				}
			}
		}
	}
}

LinkGraph::LinkGraph()  : current_colour(1), current_station(0), cargo(CT_INVALID)
{
	for (CargoID i = CT_BEGIN; i != CT_END; ++i) {
		if (this == &(_link_graphs[i])) {
			cargo = i;
		}
	}
	InitColours();
}

uint Component::AddNode(StationID st, uint supply, uint demand) {
	nodes.push_back(Node(st, supply, demand));
	for(NodeID i = 0; i < num_nodes; ++i) {
		edges[i].push_back(Edge());
	}
	edges.push_back(std::vector<Edge>(++num_nodes));
	return num_nodes - 1;
}

void Component::AddEdge(NodeID from, NodeID to, uint capacity) {
	edges[from][to].capacity = capacity;
}

void Component::CalculateDistances() {
	for(NodeID i = 0; i < num_nodes; ++i) {
		for(NodeID j = 0; j < i; ++j) {
			Station * st1 = GetStation(nodes[i].station);
			Station * st2 = GetStation(nodes[j].station);
			uint distance = DistanceManhattan(st1->xy, st2->xy);
			edges[i][j].distance = distance;
			edges[j][i].distance = distance;
		}
	}
}

void Component::SetSize(uint size) {
	num_nodes = size;
	nodes.resize(num_nodes);
	edges.resize(num_nodes, std::vector<Edge>(num_nodes));
}

Component::Component(colour col) :
	num_nodes(0),
	component_colour(col)
{
}

Component::Component(uint size, colour c) :
	num_nodes(size),
	component_colour(c),
	nodes(size),
	edges(size, std::vector<Edge>(size))
{
}

void LinkGraph::Join() {
	if (components.empty()) {
		return;
	}
	Component * comp = components.front();

	components.pop_front();

	delete comp;
}

void LinkGraph::AddComponent(Component * component) {
	 components.push_back(component);
	 colour component_colour = component->GetColour();
	 for(NodeID i = 0; i < component->GetSize(); ++i) {
		 station_colours[component->GetNode(i).station] = component_colour;
	 }
}

void LinkGraph::Clear() {
	for(ComponentList::iterator i = components.begin(); i != components.end(); ++i) {
		delete *i;
	}
	components.clear();
	InitColours();
	current_colour = 1;
	current_station = 0;
}

void InitializeLinkGraphs() {
	for (CargoID c = CT_BEGIN; c != CT_END; ++c) _link_graphs[c].Clear();
}
