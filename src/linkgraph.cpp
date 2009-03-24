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
#include "demands.h"
#include "core/bitmath_func.hpp"
#include "mcf.h"
#include <queue>

LinkGraph _link_graphs[NUM_CARGO];

typedef std::map<StationID, NodeID> ReverseNodeIndex;

bool LinkGraph::NextComponent()
{
	ReverseNodeIndex index;
	NodeID node = 0;
	std::queue<Station *> search_queue;
	Component * component = NULL;
	while (true) {
		// find first station of next component
		if (station_colours[current_station] > USHRT_MAX / 2 && IsValidStationID(current_station)) {
			Station * station = GetStation(current_station);
			LinkStatMap & links = station->goods[cargo].link_stats;
			if (!links.empty()) {
				if (++current_colour == USHRT_MAX / 2) {
					current_colour = 0;
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
			return false;
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
	SpawnComponentThread(component);
	return true;
}

void LinkGraph::InitColours()
{
	for (uint i = 0; i < Station_POOL_MAX_BLOCKS; ++i) {
		station_colours[i] = USHRT_MAX;
	}
}


void OnTick_LinkGraph()
{
	if ((_tick_counter + LinkGraph::COMPONENTS_TICK) % DAY_TICKS == 0) {
		CargoID cargo = (_date) % NUM_CARGO;
		LinkGraph & graph = _link_graphs[cargo];
		if (!graph.NextComponent()) {
			graph.Join();
		}
	}
}

LinkGraph::LinkGraph()  : current_colour(0), current_station(0), cargo(CT_INVALID)
{
	for (CargoID i = 0; i < NUM_CARGO; ++i) {
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
	thread(NULL),
	num_nodes(0),
	join_time(_tick_counter + _settings_game.economy.linkgraph_recalc_interval * DAY_TICKS),
	component_colour(col)
{
}

Component::Component(uint size, uint join, colour c) :
	thread(NULL),
	num_nodes(size),
	join_time(join),
	component_colour(c),
	nodes(size),
	edges(size, std::vector<Edge>(size))
{
}

bool LinkGraph::Join() {
	if (components.empty()) {
		return false;
	}
	Component * comp = components.front();

	if (comp->GetJoinTime() > _tick_counter) {
		return false;
	}

	components.pop_front();

	for(NodeID i = 0; i < comp->GetSize(); ++i) {
		Node & node = comp->GetNode(i);
		StationID id = node.station;
		station_colours[id] += USHRT_MAX / 2;
		if (id < current_station) current_station = id;
	}
	return true;
}

void LinkGraph::AddComponent(Component * component) {
	 components.push_back(component);
	 colour component_colour = component->GetColour();
	 for(NodeID i = 0; i < component->GetSize(); ++i) {
		 station_colours[component->GetNode(i).station] = component_colour;
	 }
	 SpawnComponentThread(component);
}

void LinkGraphJob::Run() {
	for (HandlerList::iterator i = handlers.begin(); i != handlers.end(); ++i) {
		ComponentHandler * handler = *i;
		handler->Run(component);
	}
}

LinkGraphJob::~LinkGraphJob() {
	for (HandlerList::iterator i = handlers.begin(); i != handlers.end(); ++i) {
		ComponentHandler * handler = *i;
		delete handler;
	}
	handlers.clear();
}

void RunLinkGraphJob(void * j) {
	LinkGraphJob * job = (LinkGraphJob *)j;
	job->Run();
	delete job;
}

void LinkGraph::SpawnComponentThread(Component * c) {
	LinkGraphJob * job = new LinkGraphJob(c);
	job->AddHandler(new DemandCalculator(cargo));
	job->AddHandler(new MultiCommodityFlow);
	ThreadObject::New(&(RunLinkGraphJob), job, &c->GetThread());
}
