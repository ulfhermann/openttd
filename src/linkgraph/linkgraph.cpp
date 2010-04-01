/** @file linkgraph.cpp Definition of link graph classes used for cargo distribution. */

#include "linkgraph.h"
#include "demands.h"
#include "mcf.h"
#include "flowmapper.h"
#include "../variables.h"
#include "../map_func.h"
#include "../core/bitmath_func.hpp"
#include "../debug.h"
#include "../moving_average.h"
#include <queue>

/**
 * Global array of link graphs, one for each cargo.
 */
LinkGraph _link_graphs[NUM_CARGO];

/**
 * Handlers to be run for each job.
 */
LinkGraphJob::HandlerList LinkGraphJob::_handlers;

/**
 * 1. Build the link graph component containing the given station by using BFS on the link stats.
 * 2. Set every included station's last_component to the new component's ID (this->current_component_id).
 * 3. Start a link graph job with the new component.
 * @param first Station to start the search at
 */
void LinkGraph::CreateComponent(Station *first)
{
	std::map<Station *, NodeID> index;
	index[first] = this->AddNode(first);

	std::queue<Station *> search_queue;
	search_queue.push(first);

	/* find all stations belonging to the current component */
	while(!search_queue.empty()) {
		Station *source = search_queue.front();
		search_queue.pop();

		LinkStatMap &links = source->goods[this->cargo].link_stats;
		for(LinkStatMap::iterator i = links.begin(); i != links.end(); ++i) {
			Station *target = Station::GetIfValid(i->first);
			if (target == NULL) continue;

			std::map<Station *, NodeID>::iterator index_it = index.find(target);
			if (index_it == index.end()) {
				search_queue.push(target);
				NodeID node = this->AddNode(target);
				index[target] = node;

				this->AddEdge(index[source], node,	i->second.Capacity());
			} else {
				this->AddEdge(index[source], index_it->second,	i->second.Capacity());
			}
		}
	}

	/* here the list of nodes and edges for this component is complete. */
	this->SpawnThread();
}

/**
 * Looks for a suitable station to create the next link graph component from.
 * Linearly searches all stations starting from current_station_id for one that
 * hasn't been visited in this run over the link graph. The current run and the
 * last run are differentiated by giving the components IDs divisible by 2
 * every second run and ones not divisible by 2 on the other runs.
 *
 * If such a station is found current_station_id is set to that station's ID
 * and CreateComponent is called with it.
 *
 * The search wraps around and changes current_component_id to 0 or 1
 * accordingly. If the starting point is reached again it stops.
 */
void LinkGraph::NextComponent()
{
	StationID last_station_id = this->current_station_id;
	LinkGraphComponentID current_component_id = this->LinkGraphComponent::index;

	do {
		if (++this->current_station_id >= Station::GetPoolSize()) {
			/* Wrap around and recycle the component IDs. Use different
			 * divisibility by 2 than in the last run so that we can find out
			 * which stations haven't been seen in this run.
			 */
			this->current_station_id = 0;
			if (current_component_id % 2 == 0) {
				current_component_id = 1;
			} else {
				current_component_id = 0;
			}
		}

		/* find first station of next component */
		Station *station = Station::GetIfValid(this->current_station_id);
		if (station != NULL) {
			GoodsEntry &ge = station->goods[this->cargo];
			if (ge.last_component == INVALID_LINKGRAPH_COMPONENT ||
					(ge.last_component + current_component_id) % 2 != 0) {
				/* Different divisibility by 2: This station has not been seen
				 * in the current run over the link graph.
				 */

				if (!ge.link_stats.empty()) {
					this->LinkGraphComponent::Init(current_component_id + 2);
					CreateComponent(station);
					return;
				}
			}
		}

	} while (this->current_station_id != last_station_id);
}

/**
 * Spawn or join a link graph component if any link graph is due to do so.
 * Spawning is done on COMPONENTS_SPAWN_TICK every day, joining on
 * COMPONENT_JOIN_TICK. Each link graph is due every recalc_interval days.
 */
void OnTick_LinkGraph()
{
	if (_date_fract == LinkGraph::COMPONENTS_SPAWN_TICK ||
			_date_fract == LinkGraph::COMPONENTS_JOIN_TICK) {

		LinkGraphSettings &settings = _settings_game.linkgraph;

		/* This creates a fair distribution of all link graphs' turns over
		 * the available dates.
		 */
		uint interval = settings.recalc_interval;
		for (uint cargo = _date % interval; cargo < CT_END; cargo += interval) {

			/* don't calculate a link graph if the distribution is manual */
			if (settings.GetDistributionType(cargo) == DT_MANUAL) continue;

			if (_date_fract == LinkGraph::COMPONENTS_SPAWN_TICK) {
				_link_graphs[cargo].NextComponent();
			} else /* LinkGraph::COMPONENTS_JOIN_TICK */ {
				_link_graphs[cargo].Join();
			}
		}
	}
}

/**
 * Add a node to the component and create empty edges associated with it. Set
 * the station's last_component to this component. Calculate the distances to all
 * other nodes. The distances to _all_ nodes are important as the demand
 * calculator relies on their availability.
 * @param st the new node's station
 * @return the new node's ID
 */
NodeID LinkGraphComponent::AddNode(Station *st)
{
	GoodsEntry &good = st->goods[this->cargo];
	good.last_component = this->index;

	bool do_resize = (this->nodes.size() == this->num_nodes);

	if (do_resize) {
		this->nodes.push_back(Node());
		this->edges.push_back(std::vector<Edge>(this->num_nodes + 1));
	}

	this->nodes[this->num_nodes].Init(st->index, good.supply,
			HasBit(good.acceptance_pickup, GoodsEntry::ACCEPTANCE));

	for(NodeID i = 0; i < this->num_nodes; ++i) {
		uint distance = DistanceManhattan(st->xy, Station::Get(this->nodes[i].station)->xy);
		if (do_resize) this->edges[i].push_back(Edge());
		this->edges[this->num_nodes][i].Init(distance);
	}
	return this->num_nodes++;
}

/**
 * Fill an edge with values from a link.
 * @param from source node of the link
 * @param to destination node of the link
 * @param capacity capacity of the link
 */
FORCEINLINE void LinkGraphComponent::AddEdge(NodeID from, NodeID to, uint capacity)
{
	assert(from != to);
	Edge &edge = this->edges[from][to];
	Edge &first = this->edges[from][from];
	edge.capacity = capacity;
	edge.next_edge = first.next_edge;
	first.next_edge = to;
}

/**
 * Resize the component and fill it with empty nodes and edges. Used when
 * loading from save games.
 *
 * WARNING: The nodes and edges are expected to be empty while num_nodes is
 * expected to contain the desired size. Normally this is an invalid state,
 * but just after loading the component's structure it is valid. This method
 * should only be called from Load_LGRP; otherwise it is a NOP.
 */
void LinkGraphComponent::SetSize()
{
	if (this->nodes.size() < this->num_nodes) {
		this->nodes.resize(this->num_nodes);
		this->edges.resize(this->num_nodes, std::vector<Edge>(this->num_nodes));
	}
}

/**
 * Create an empty component.
 */
LinkGraphComponent::LinkGraphComponent() :
	settings(_settings_game.linkgraph),
	cargo(INVALID_CARGO),
	num_nodes(0),
	index(INVALID_LINKGRAPH_COMPONENT)
{}

/**
 * (re-)initialize this component with a new ID and a new copy of the settings.
 */
void LinkGraphComponent::Init(LinkGraphComponentID id)
{
	this->index = id;
	this->settings = _settings_game.linkgraph;
}


/**
 * exports all entries in the FlowViaMap pointed to by source_flows it and erases it afterwards
 */
void Node::ExportNewFlows(FlowMap::iterator &source_flows_it, FlowStatSet &via_set, CargoID cargo)
{
	StationID source = source_flows_it->first;
	FlowViaMap &source_flows = source_flows_it->second;
	if (!Station::IsValidID(source)) {
		source_flows.clear();
	} else {
		Station *curr_station = Station::Get(this->station);
		for (FlowViaMap::iterator update = source_flows.begin(); update != source_flows.end();) {
			StationID next = update->first;
			int planned = update->second;
			assert(planned >= 0);

			Station *via = Station::GetIfValid(next);
			if (planned > 0 && via != NULL) {
				uint distance = GetMovingAverageLength(curr_station, via);
				if (next != this->station) {
					LinkStatMap & ls = curr_station->goods[cargo].link_stats;
					if (ls.find(next) != ls.end()) {
						via_set.insert(FlowStat(distance, next, planned, 0));
					}
				} else {
					via_set.insert(FlowStat(distance, next, planned, 0));
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
				FlowViaMap::iterator update = source_flows.find(flowset_it->Via());
				if (update != source_flows.end()) {
					assert(update->second >= 0);
					if (update->second > 0) {
						new_flows.insert(FlowStat(*flowset_it, update->second));
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

/**
 * Merge the current job's results into the main game state.
 */
void LinkGraph::Join()
{
	this->LinkGraphJob::Join();
	for(NodeID node_id = 0; node_id < this->GetSize(); ++node_id) {
		Node &node = this->GetNode(node_id);
		if (Station::IsValidID(node.station)) {
			node.ExportFlows(Station::Get(node.station)->goods[cargo].flows, cargo);
		}
	}
}

/**
 * Run all handlers for the given Job.
 * @param j a pointer to a link graph job
 */
/* static */ void LinkGraphJob::RunLinkGraphJob(void *j)
{
	LinkGraphJob *job = (LinkGraphJob *)j;
	for (HandlerList::iterator i = _handlers.begin(); i != _handlers.end(); ++i) {
		(*i)->Run(job);
	}
}

/**
 * Clear the handlers.
 */
/* static */ void LinkGraphJob::ClearHandlers()
{
	for(HandlerList::iterator i = _handlers.begin(); i != _handlers.end(); ++i) {
		delete (*i);
	}
	_handlers.clear();
}

void Path::Fork(Path *base, int cap, uint dist)
{
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

uint Path::AddFlow(uint f, LinkGraphComponent *graph, bool only_positive)
{
	if (parent != NULL) {
		Edge &edge = graph->GetEdge(parent->node, node);
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

void Path::UnFork()
{
	if (parent != NULL) {
		parent->num_children--;
		parent = NULL;
	}
}

Path::Path(NodeID n, bool source)  :
	distance(source ? 0 : UINT_MAX),
	capacity(source ? INT_MAX : INT_MIN),
	flow(0), node(n), origin(source ? n : INVALID_NODE),
	num_children(0), parent(NULL)
{}

/**
 * Spawn a thread if possible and run the link graph job in the thread. If
 * that's not possible run the job right now in the current thread.
 */
void LinkGraphJob::SpawnThread()
{
	if (!ThreadObject::New(&(LinkGraphJob::RunLinkGraphJob), this, &thread)) {
		thread = NULL;
		/* Of course this will hang a bit.
		 * On the other hand, if you want to play games which make this hang noticably
		 * on a platform without threads then you'll probably get other problems first.
		 * OK:
		 * If someone comes and tells me that this hangs for him/her, I'll implement a
		 * smaller grained "Step" method for all handlers and add some more ticks where
		 * "Step" is called. No problem in principle.
		 */
		LinkGraphJob::RunLinkGraphJob(this);
	}
}

/**
 * (Re-)Initialize the link graph: join all jobs and set current_station_id and
 * cargo to their start values.
 * @param cargo the new cargo ID for the link graph
 */
void LinkGraph::Init(CargoID cargo)
{
	this->LinkGraphJob::Join();
	this->LinkGraphComponent::Clear();

	this->current_station_id = 0;
	this->LinkGraphComponent::cargo = cargo;
}

Node::~Node()
{
	for (PathSet::iterator i = this->paths.begin(); i != this->paths.end(); ++i) {
		delete (*i);
	}
}

/**
 * Inititialize all link graphs. Used when loading a game.
 */
void InitializeLinkGraphs()
{
	LinkGraphJob::ClearHandlers();
	LinkGraphJob::AddHandler(new DemandCalculator);
	LinkGraphJob::AddHandler(new MCF1stPass);
	LinkGraphJob::AddHandler(new FlowMapper);
	LinkGraphJob::AddHandler(new MCF2ndPass);
	LinkGraphJob::AddHandler(new FlowMapper);

	for (CargoID c = CT_BEGIN; c != CT_END; ++c) _link_graphs[c].Init(c);
}
