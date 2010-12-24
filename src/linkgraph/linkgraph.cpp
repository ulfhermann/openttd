/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file linkgraph.cpp Definition of link graph classes used for cargo distribution. */

#include "linkgraph.h"
#include "demands.h"
#include "mcf.h"
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
 * Create a node or clear it.
 * @param st ID of the associated station
 * @param sup supply of cargo at the station last month
 * @param dem acceptance for cargo at the station
 */
void Node::Init(StationID st, uint sup, uint dem)
{
	this->supply = sup;
	this->undelivered_supply = sup;
	this->demand = dem;
	this->station = st;

	for (PathSet::iterator i = this->paths.begin(); i != this->paths.end(); ++i) {
		delete (*i);
	}
	this->paths.clear();
	this->flows.clear();
}

/**
 * Create an edge.
 * @param distance length of the link as manhattan distance
 * @param capacity capacity of the link
 */
FORCEINLINE void Edge::Init(uint distance, uint capacity)
{
	this->distance = distance;
	this->capacity = capacity;
	this->demand = 0;
	this->unsatisfied_demand = 0;
	this->flow = 0;
	this->next_edge = INVALID_NODE;
}


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

		const LinkStatMap &links = source->goods[this->cargo].link_stats;
		for(LinkStatMap::const_iterator i = links.begin(); i != links.end(); ++i) {
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
	if (this->GetSize() > 0) return; // don't mess with running jobs (might happen when changing interval)
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

		/* This creates a fair distribution of all link graphs' turns over
		 * the available dates.
		 */
		for (uint cargo = _date % _settings_game.linkgraph.recalc_interval; cargo < NUM_CARGO;
				cargo += _settings_game.linkgraph.recalc_interval) {

			/* don't calculate a link graph if the distribution is manual */
			if (_settings_game.linkgraph.GetDistributionType(cargo) == DT_MANUAL) continue;

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

	std::vector<Edge> &new_edges = this->edges[this->num_nodes];

	/* reset the first edge starting at the new node */
	new_edges[this->num_nodes].next_edge = INVALID_NODE;

	for(NodeID i = 0; i < this->num_nodes; ++i) {
		uint distance = DistanceManhattan(st->xy, Station::Get(this->nodes[i].station)->xy);
		if (do_resize) this->edges[i].push_back(Edge());
		new_edges[i].Init(distance);
		this->edges[i][this->num_nodes].Init(distance);
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
 * WARNING: The nodes and edges are expected to contain anything while
 * num_nodes is expected to contain the desired size. Normally this is an
 * invalid state, but just after loading the component's structure it is valid.
 * This method should only be called from Load_LGRP.
 */
void LinkGraphComponent::SetSize()
{
	if (this->nodes.size() < this->num_nodes) {
		for (EdgeMatrix::iterator i = this->edges.begin(); i != this->edges.end(); ++i) {
			i->resize(this->num_nodes);
		}
		this->nodes.resize(this->num_nodes);
		this->edges.resize(this->num_nodes, std::vector<Edge>(this->num_nodes));
	}

	for(uint i = 0; i < this->num_nodes; ++i) {
		this->nodes[i].Init();
		for (uint j = 0; j < this->num_nodes; ++j) {
			this->edges[i][j].Init();
		}
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
	assert(this->num_nodes == 0);
	this->index = id;
	this->settings = _settings_game.linkgraph;
}

/**
 * Merge the current job's results into the main game state.
 */
void LinkGraph::Join()
{
	this->LinkGraphJob::Join();

	this->LinkGraphComponent::Clear();
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

/**
 * add this path as a new child to the given base path, thus making this path
 * a "fork" of the base path.
 * @param base the path to fork from
 * @param cap maximum capacity of the new path
 * @param dist distance of the new leg
 */
void Path::Fork(Path *base, uint cap, int free_cap, uint dist)
{
	this->capacity = min(base->capacity, cap);
	this->free_capacity = min(base->free_capacity, free_cap);
	this->distance = base->distance + dist;
	assert(this->distance > 0);
	if (this->parent != base) {
		this->UnFork();
		this->parent = base;
		this->parent->num_children++;
	}
	this->origin = base->origin;
}

/**
 * Push some flow along a path and register the path in the nodes it passes if
 * successful.
 * @param new_flow amount of flow to push
 * @param graph the link graph component this node belongs to
 * @param only_positive if true, don't push more flow than there is capacity
 * @return the amount of flow actually pushed
 */
uint Path::AddFlow(uint new_flow, LinkGraphComponent *graph, bool only_positive)
{
	if (this->parent != NULL) {
		Edge &edge = graph->GetEdge(this->parent->node, this->node);
		if (only_positive) {
			uint usable_cap = edge.capacity * graph->GetSettings().short_path_saturation / 100;
			if(usable_cap > edge.flow) {
				new_flow = min(new_flow, usable_cap - edge.flow);
			} else {
				return 0;
			}
		}
		new_flow = this->parent->AddFlow(new_flow, graph, only_positive);
		if (new_flow > 0) {
			graph->GetNode(this->parent->node).paths.insert(this);
		}
		edge.flow += new_flow;
	}
	this->flow += new_flow;
	return new_flow;
}

/**
 * create a leg of a path in the link graph.
 * @param n id of the link graph node this path passes
 * @param source if true, this is the first leg of the path
 */
Path::Path(NodeID n, bool source)  :
	distance(source ? 0 : UINT_MAX),
	capacity(0),
	free_capacity(source ? INT_MAX : INT_MIN),
	flow(0), node(n), origin(source ? n : INVALID_NODE),
	num_children(0), parent(NULL)
{}

/**
 * Join the calling thread with this job's thread if threading is enabled.
 */
FORCEINLINE void LinkGraphJob::Join()
{
	if (this->thread != NULL) {
		this->thread->Join();
		delete this->thread;
		this->thread = NULL;
	}
}

/**
 * Spawn a thread if possible and run the link graph job in the thread. If
 * that's not possible run the job right now in the current thread.
 */
void LinkGraphJob::SpawnThread()
{
	assert(this->thread == NULL);
	if (!ThreadObject::New(&(LinkGraphJob::RunLinkGraphJob), this, &(this->thread))) {
		this->thread = NULL;
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

/**
 * Initialize all link graphs. Used when loading a game.
 */
void InitializeLinkGraphs()
{
	for (CargoID c = 0; c < NUM_CARGO; ++c) _link_graphs[c].Init(c);

	LinkGraphJob::ClearHandlers();
	LinkGraphJob::AddHandler(new DemandHandler);
	LinkGraphJob::AddHandler(new MCFHandler<MCF1stPass>);
	LinkGraphJob::AddHandler(new MCFHandler<MCF2ndPass>);
}
