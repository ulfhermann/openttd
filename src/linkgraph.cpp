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
#include "mcf.h"
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
	LinkGraphComponent * component = NULL;
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
				component = new LinkGraphComponent(cargo, current_colour);
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
	LinkGraphJob * job = new LinkGraphJob(component);
	job->SpawnThread(cargo);
	jobs.push_back(job);
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
			if ((_date + cargo) % _settings_game.linkgraph.recalc_interval == 0) {
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

NodeID LinkGraphComponent::AddNode(StationID st, uint supply, uint demand) {
	nodes.push_back(Node(st, supply, demand));
	for(NodeID i = 0; i < num_nodes; ++i) {
		edges[i].push_back(Edge());
	}
	edges.push_back(std::vector<Edge>(++num_nodes));
	return num_nodes - 1;
}

void LinkGraphComponent::AddEdge(NodeID from, NodeID to, uint capacity) {
	Edge & edge = edges[from][to];
	Edge & first = edges[from][from];
	edge.capacity = capacity;
	edge.next_edge = first.next_edge;
	first.next_edge = to;
}

void LinkGraphComponent::CalculateDistances() {
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

void LinkGraphComponent::SetSize(uint size) {
	num_nodes = size;
	nodes.resize(num_nodes);
	edges.resize(num_nodes, std::vector<Edge>(num_nodes));
}

LinkGraphComponent::LinkGraphComponent(CargoID car, colour col) :
	settings(_settings_game.linkgraph),
	cargo(car),
	num_nodes(0),
	component_colour(col)
{
}

void LinkGraph::Join() {
	if (jobs.empty()) {
		return;
	}
	LinkGraphJob * job = jobs.front();

	if (job->GetJoinDate() > _date) {
		return;
	}
	job->Join();

	delete job;
	jobs.pop_front();
}

void LinkGraph::AddComponent(LinkGraphComponent * component, uint join) {
	 colour component_colour = component->GetColour();
	 for(NodeID i = 0; i < component->GetSize(); ++i) {
		 station_colours[component->GetNode(i).station] = component_colour;
	 }
	 LinkGraphJob * job = new LinkGraphJob(component, join);
	 job->SpawnThread(cargo);
	 jobs.push_back(job);
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
	delete component;
	delete thread;
}

void RunLinkGraphJob(void * j) {
	LinkGraphJob * job = (LinkGraphJob *)j;
	job->Run();
}

void Path::Fork(Path * base, uint cap, uint dist) {
	capacity = min(base->capacity, cap);
	distance = base->distance + dist;
	assert(distance > 0);
	if (parent != base) {
		if (parent != NULL) {
			parent->num_children--;
		}
		parent = base;
		parent->num_children++;
	}
}

uint Path::AddFlow(uint f, LinkGraphComponent * graph) {
	graph->GetNode(node).paths.insert(this);
	if (parent != NULL) {
		Edge & edge = graph->GetEdge(parent->node, node);
		f = min(f, edge.capacity - edge.flow);
		f = parent->AddFlow(f, graph);
		edge.flow += f;
	}
	flow +=f;
	return f;
}

void Path::UnFork() {
	if (parent != NULL) {
		parent->num_children--;
		parent = NULL;
	}
}

Path::Path(NodeID n, bool source)  :
	distance(source ? 0 : UINT_MAX),
	capacity(source ? UINT_MAX : 0),
	flow(0), node(n), num_children(0), parent(NULL)
{}

void LinkGraphJob::SpawnThread(CargoID cargo) {
	join_date = _date + component->GetSettings().recalc_interval;
	AddHandler(new DemandCalculator);
	AddHandler(new MultiCommodityFlow);
	if (!ThreadObject::New(&(RunLinkGraphJob), this, &thread)) {
		thread = NULL;
		// Of course this will hang a bit.
		// On the other hand, if you want to play games which make this hang noticably
		// on a platform without threads then you'll probably get other problems first.
		// OK:
		// If someone comes and tells me that this hangs for him/her, I'll implement a
		// smaller grained "Step" method for all handlers and add some more ticks where
		// "Step" is called. No problem in principle.
		RunLinkGraphJob(this);
	}
}

LinkGraphJob::LinkGraphJob(LinkGraphComponent * c) :
	thread(NULL),
	join_date(0),
	component(c)
{}

LinkGraphJob::LinkGraphJob(LinkGraphComponent * c, Date join) :
	thread(NULL),
	join_date(join),
	component(c)
{}

void LinkGraph::Clear() {
	for (JobList::iterator i = jobs.begin(); i != jobs.end(); ++i) {
		LinkGraphJob * job = *i;
		job->Join();
		delete job;
	}
	jobs.clear();
	InitColours();
	current_colour = 1;
	current_station = 0;
}

void InitializeLinkGraphs() {
	for (CargoID c = CT_BEGIN; c != CT_END; ++c) _link_graphs[c].Clear();
}
