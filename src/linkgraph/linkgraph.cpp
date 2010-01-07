/** @file linkgraph.cpp Definition of link graph classes used for cargo distribution. */

#include "linkgraph.h"
#include "demands.h"
#include "mcf.h"
#include "flowmapper.h"
#include "../date_func.h"
#include "../variables.h"
#include "../map_func.h"
#include "../core/bitmath_func.hpp"
#include "../debug.h"
#include "../window_func.h"
#include "../window_gui.h"
#include <queue>

LinkGraph _link_graphs[NUM_CARGO];

typedef std::map<StationID, NodeID> ReverseNodeIndex;

void LinkGraph::CreateComponent(Station * first) {
	ReverseNodeIndex index;
	NodeID node = 0;
	std::queue<Station *> search_queue;
	LinkGraphComponent * component = NULL;

	search_queue.push(first);

	first->goods[cargo].last_component = current_component_id;
	component = new LinkGraphComponent(cargo, current_component_id);
	GoodsEntry & good = first->goods[cargo];
	node = component->AddNode(current_station_id, good.supply, HasBit(good.acceptance_pickup, GoodsEntry::ACCEPTANCE));
	index[current_station_id++] = node;
	// find all stations belonging to the current component
	while(!search_queue.empty()) {
		Station * source = search_queue.front();
		StationID source_id = source->index;
		search_queue.pop();
		GoodsEntry & good = source->goods[cargo];
		LinkStatMap & links = good.link_stats;
		for(LinkStatMap::iterator i = links.begin(); i != links.end(); ++i) {
			StationID target_id = i->first;
			if (!Station::IsValidID(target_id)) {
				continue;
			}
			assert(target_id != source_id);
			LinkStat & link_stat = i->second;
			ReverseNodeIndex::iterator index_it = index.find(target_id);
			if (index_it == index.end()) {
				Station * target = Station::Get(target_id);
				GoodsEntry & good = target->goods[cargo];
				good.last_component = current_component_id;
				search_queue.push(target);
				node = component->AddNode(target_id, good.supply, HasBit(good.acceptance_pickup, GoodsEntry::ACCEPTANCE));
				index[target_id] = node;
			} else {
				node = index_it->second;
			}
			component->AddEdge(index[source_id], node, link_stat.capacity);
		}
	}
	// here the list of nodes and edges for this component is complete.
	component->CalculateDistances();
	LinkGraphJob * job = new LinkGraphJob(component);
	assert(job != NULL);
	job->SpawnThread(cargo);
	jobs.push_back(job);
}

void LinkGraph::NextComponent()
{
	while (!Station::IsValidID(current_station_id) && current_station_id > 0) {
		--current_station_id;
	}
	StationID last_station_id = current_station_id;

	do {
		// find first station of next component
		if (Station::IsValidID(current_station_id)) {
			Station * station = Station::Get(current_station_id);
			GoodsEntry & ge = station->goods[cargo];
			if ((ge.last_component + current_component_id) % 2 != 0) {
				// has not been seen in this run through the graph

				LinkStatMap & links = station->goods[cargo].link_stats;
				if (!links.empty()) {
					current_component_id += 2;
					CreateComponent(station);
					return;
				}
			}
		}

		if (++current_station_id == Station::GetPoolSize()) {
			current_station_id = 0;
			if (current_component_id % 2 == 0) {
				current_component_id = 1;
			} else {
				current_component_id = 0;
			}
		}
	} while (current_station_id != last_station_id);
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

LinkGraph::LinkGraph()  : current_component_id(1), current_station_id(0), cargo(CT_INVALID)
{
	for (CargoID i = CT_BEGIN; i != CT_END; ++i) {
		if (this == &(_link_graphs[i])) {
			cargo = i;
		}
	}
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
	assert(capacity > 0);
	assert(from != to);
	Edge & edge = edges[from][to];
	Edge & first = edges[from][from];
	edge.capacity = capacity;
	edge.next_edge = first.next_edge;
	first.next_edge = to;
	edges[from][to].capacity = capacity;
}

void LinkGraphComponent::CalculateDistances() {
	for(NodeID i = 0; i < num_nodes; ++i) {
		for(NodeID j = 0; j < i; ++j) {
			Station * st1 = Station::Get(nodes[i].station);
			Station * st2 = Station::Get(nodes[j].station);
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

LinkGraphComponent::LinkGraphComponent(CargoID car, LinkGraphComponentID col) :
	settings(_settings_game.linkgraph),
	cargo(car),
	num_nodes(0),
	index(col)
{
}

void LinkGraph::Join() {
	while (!jobs.empty()) {
		LinkGraphJob * job = jobs.front();
		assert(job != NULL);

		/* also join if join date is far in the future. This prevents excessive memory use when resetting time */
		if (job->GetJoinDate() > _date && job->GetJoinDate() <= _date + _settings_game.linkgraph.recalc_interval) {
			return;
		}
		job->Join();

		LinkGraphComponent * comp = job->GetComponent();

		for(NodeID node_id = 0; node_id < comp->GetSize(); ++node_id) {
			Node & node = comp->GetNode(node_id);
			if (Station::IsValidID(node.station)) {
				FlowStatMap & station_flows = Station::Get(node.station)->goods[cargo].flows;
				node.ExportFlows(station_flows, cargo);
				Window *station_gui = FindWindowById(WC_STATION_VIEW, node.station);
				if (station_gui != NULL) {
					station_gui->OnInvalidateData(comp->GetCargo());
				}
			}
		}
		delete job;
		jobs.pop_front();
	}
}

/**
 * exports all entries in the FlowViaMap pointed to by source_flows it and erases it afterwards
 */
void Node::ExportNewFlows(FlowMap::iterator & source_flows_it, FlowStatSet & via_set, CargoID cargo) {
	StationID source = source_flows_it->first;
	FlowViaMap & source_flows = source_flows_it->second;
	if (!Station::IsValidID(source)) {
		source_flows.clear();
	} else {
		for (FlowViaMap::iterator update = source_flows.begin(); update != source_flows.end();) {
			assert(update->second >= 0);
			StationID next = update->first;
			if (update->second > 0 && Station::IsValidID(next)) {
				if (next != station) {
					LinkStatMap & ls = Station::Get(station)->goods[cargo].link_stats;
					if (ls.find(next) != ls.end()) {
						via_set.insert(FlowStat(update->first, update->second, 0));
					}
				} else {
					via_set.insert(FlowStat(update->first, update->second, 0));
				}
			}
			source_flows.erase(update++);
		}
	}
	assert(source_flows.empty());

	flows.erase(source_flows_it++);
}

void Node::ExportFlows(FlowStatMap & station_flows, CargoID cargo) {
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
					assert(update->second >= 0);
					if (update->second > 0) {
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
			ExportNewFlows(source_flows_it, via_set, cargo);
			/* source_flows is dangling here */
			++flowmap_it;
		}
	}
	/* loop over remaining flows (for other sources) in the node's map and insert them into the station */
	for (FlowMap::iterator source_flows_it = flows.begin(); source_flows_it != flows.end();) {
		FlowStatSet & via_set = station_flows[source_flows_it->first];
		ExportNewFlows(source_flows_it, via_set, cargo);
	}
	assert(flows.empty());
}

void LinkGraph::AddComponent(LinkGraphComponent * component, uint join) {
	LinkGraphComponentID index = component->GetIndex();
	for(NodeID i = 0; i < component->GetSize(); ++i) {
		StationID station_id = component->GetNode(i).station;
		if (Station::IsValidID(station_id)) {
			Station::Get(station_id)->goods[cargo].last_component = index;
		}
	}
	LinkGraphJob * job = new LinkGraphJob(component, join);
	assert(job != NULL);
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
	DEBUG(misc, 2, "removing job for cargo %d with index %d and join date %d at %d", component->GetCargo(), component->GetIndex(), join_date, _date);
	delete component;
	delete thread;
}

void RunLinkGraphJob(void * j) {
	LinkGraphJob * job = (LinkGraphJob *)j;
	job->Run();
}

void Path::Fork(Path * base, int cap, uint dist) {
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
	origin = base->origin;
}

uint Path::AddFlow(uint f, LinkGraphComponent * graph, bool only_positive) {
	if (parent != NULL) {
		Edge & edge = graph->GetEdge(parent->node, node);
		if (only_positive) {
			uint usable_cap = edge.capacity * graph->GetSettings().short_path_saturation / 100;
			if(usable_cap > edge.flow) {
				f = min(f, usable_cap - edge.flow);
			} else {
				return 0;
			}
		}
		f = parent->AddFlow(f, graph, only_positive);
		if (f > 0) {
			graph->GetNode(parent->node).paths.insert(this);
		}
		edge.flow += f;
	}
	flow += f;
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
	capacity(source ? INT_MAX : INT_MIN),
	flow(0), node(n), origin(source ? n : Node::INVALID),
	num_children(0), parent(NULL)
{}

void LinkGraphJob::SpawnThread(CargoID cargo) {
	AddHandler(new DemandCalculator);
	AddHandler(new MCF1stPass);
	AddHandler(new FlowMapper);
	AddHandler(new MCF2ndPass);
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

LinkGraphJob::LinkGraphJob(LinkGraphComponent * c) :
	thread(NULL),
	join_date(_date + c->GetSettings().recalc_interval),
	component(c)
{
	DEBUG(misc, 2, "new job for cargo %d with index %d and join date %d at %d", c->GetCargo(), c->GetIndex(), join_date, _date);
}

LinkGraphJob::LinkGraphJob(LinkGraphComponent * c, Date join) :
	thread(NULL),
	join_date(join),
	component(c)
{
	DEBUG(misc, 2, "new job for cargo %d with index %d and join date %d at %d", c->GetCargo(), c->GetIndex(), join_date, _date);
}

Node::~Node() {
	for (PathSet::iterator i = paths.begin(); i != paths.end(); ++i) {
		delete (*i);
	}
}

void LinkGraph::Clear() {
	for (JobList::iterator i = jobs.begin(); i != jobs.end(); ++i) {
		LinkGraphJob * job = *i;
		assert(job != NULL);
		job->Join();
		delete job;
	}
	jobs.clear();
	current_component_id = 1;
	current_station_id = 0;
}

void InitializeLinkGraphs() {
	for (CargoID c = CT_BEGIN; c != CT_END; ++c) _link_graphs[c].Clear();
}
