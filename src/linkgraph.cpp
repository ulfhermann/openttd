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
#include "flowmapper.h"
#include "core/bitmath_func.hpp"
#include "debug.h"
#include <limits>
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
				component = new Component(cargo, current_colour);
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

Component::Component(CargoID car, colour col) :
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

	Component * comp = job->GetComponent();

	for(NodeID node_id = 0; node_id < comp->GetSize(); ++node_id) {
		Node & node = comp->GetNode(node_id);
		if (IsValidStationID(node.station)) {
			FlowStatMap & station_flows = GetStation(node.station)->goods[cargo].flows;
			node.ExportFlows(station_flows);
		}
	}
	delete job;
	jobs.pop_front();
}

/**
 * exports all entries in the FlowViaMap pointed to by source_flows it and erases it afterwards
 */
void Node::ExportNewFlows(FlowMap::iterator & source_flows_it, FlowStatSet & via_set) {
	FlowViaMap & source_flows = source_flows_it->second;
	for (FlowViaMap::iterator update = source_flows.begin(); update != source_flows.end();) {
		if (update->second >= 1) {
			via_set.insert(FlowStat(update->first, update->second, 0));
		}
		source_flows.erase(update++);
	}
	flows.erase(source_flows_it++);
}

void Node::ExportFlows(FlowStatMap & station_flows) {
	FlowStatSet new_flows;
	/* loop over all existing flows in the station and update them */
	for(FlowStatMap::iterator flowmap_it = station_flows.begin(); flowmap_it != station_flows.end();) {
		FlowMap::iterator source_flows_it = flows.find(flowmap_it->first);
		if (source_flows_it == flows.end()) {
			/* there are no flows for this source node anymore */
			station_flows.erase(flowmap_it++);
		} else {
			FlowViaMap & source_flows = source_flows_it->second;
			FlowStatSet & via_set = flowmap_it->second;
			/* loop over the station's flow stats for this source node and update them */
			for (FlowStatSet::iterator flowset_it = via_set.begin(); flowset_it != via_set.end();) {
				FlowViaMap::iterator update = source_flows.find(flowset_it->via);
				if (update != source_flows.end()) {
					if (update->second >= 1) {
						new_flows.insert(FlowStat(flowset_it->via, update->second, flowset_it->sent));
					}
					source_flows.erase(update);
				}
				via_set.erase(flowset_it++);
			}
			/* swap takes constant time, so we swap instead of adding all entries */
			via_set.swap(new_flows);
			assert(new_flows.empty());
			/* insert remaining flows for this source node */
			ExportNewFlows(source_flows_it, via_set);
			/* source_flows is dangling here */
			++flowmap_it;
		}
	}
	/* loop over remaining flows (for other sources) in the node's map and insert them into the station */
	for (FlowMap::iterator source_flows_it = flows.begin(); source_flows_it != flows.end();) {
		FlowStatSet & via_set = station_flows[source_flows_it->first];
		ExportNewFlows(source_flows_it, via_set);
	}
}

void LinkGraph::AddComponent(Component * component, uint join) {
	 colour component_colour = component->GetColour();
	 for(NodeID i = 0; i < component->GetSize(); ++i) {
		 station_colours[component->GetNode(i).station] = component_colour;
	 }
	 LinkGraphJob * job = new LinkGraphJob(component, join);
	 job->SpawnThread(cargo);
	 jobs.push_back(job);
}

void LinkGraphJob::Run() {
	try {
		for (HandlerList::iterator i = handlers.begin(); i != handlers.end(); ++i) {
			ComponentHandler * handler = *i;
			handler->Run(component);
		}
	} catch(Exception e) {
		DEBUG(misc, 0, "link graph calculation aborted");
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

void Path::Fork(Path * base, Number cap, Number dist) {
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

void Path::AddFlow(Number f, Component * graph) {
	flow +=f;
	graph->GetNode(node).paths.insert(this);
	if (parent != NULL) {
		parent->AddFlow(f, graph);
	}
}

void Path::UnFork() {
	if (parent != NULL) {
		parent->num_children--;
		parent = NULL;
	}
}

Path::Path(NodeID n, bool source)  :
	distance(source ? 0 : std::numeric_limits<Number>::max()),
	capacity(source ? std::numeric_limits<Number>::max() : 0),
	flow(0), node(n), num_children(0), parent(NULL)
{}

void LinkGraphJob::SpawnThread(CargoID cargo) {
	join_date = _date + component->GetSettings().recalc_interval,
	AddHandler(new DemandCalculator);
	AddHandler(new MultiCommodityFlow);
	AddHandler(new FlowMapper);
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

LinkGraphJob::LinkGraphJob(Component * c) :
	thread(NULL),
	join_date(0),
	component(c)
{}

LinkGraphJob::LinkGraphJob(Component * c, Date join) :
	thread(NULL),
	join_date(join),
	component(c)
{}

Node::~Node() {
	for (PathSet::iterator i = paths.begin(); i != paths.end(); ++i) {
		 delete (*i);
	}
}

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
