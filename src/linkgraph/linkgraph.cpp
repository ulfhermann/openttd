/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file linkgraph.cpp Definition of link graph classes used for cargo distribution. */

#include "../stdafx.h"
#include "../core/bitmath_func.hpp"
#include "../core/pool_func.hpp"
#include "../core/random_func.hpp"
#include "../map_func.h"
#include "../debug.h"
#include "linkgraph.h"
#include "init.h"
#include "demands.h"
#include "mcf.h"

/* Initialize the link-graph-pool */
LinkGraphPool _link_graph_pool("LinkGraph");
INSTANTIATE_POOL_METHODS(LinkGraph)

/* Initialize the link-graph-job-pool */
LinkGraphJobPool _link_graph_job_pool("LinkGraphJob");
INSTANTIATE_POOL_METHODS(LinkGraphJob)


/**
 * Create a node or clear it.
 * @param st ID of the associated station.
 * @param sup Supply of cargo at the station last month.
 */
inline void Node::Init(StationID st, uint demand)
{
	this->supply = 0;
	this->demand = demand;
	this->station = st;
	this->last_update = INVALID_DATE;
}

/**
 * Create an edge.
 * @param distance Length of the link as manhattan distance.
 */
inline void Edge::Init(uint distance)
{
	this->distance = distance;
	this->capacity = 0;
	this->usage = 0;
	this->last_update = INVALID_DATE;
	this->next_edge = INVALID_NODE;
}

void LinkGraph::Compress()
{
	this->last_compression = (_date + this->last_compression) / 2;
	for (NodeID node1 = 0; node1 < this->num_nodes; ++node1) {
		this->nodes[node1].supply /= 2;
		for (NodeID node2 = 0; node2 < this->num_nodes; ++node2) {
			this->edges[node1][node2].capacity /= 2;
			this->edges[node1][node2].usage /= 2;
		}
	}
}

/**
 * Merge a link graph with another one.
 * @param other LinkGraph to be merged into this one.
 */
void LinkGraph::Merge(LinkGraph *other)
{
	Date age = _date - this->last_compression + 1;
	Date other_age = _date - other->last_compression + 1;
	NodeID first = this->num_nodes;
	for (NodeID node1 = 0; node1 < other->num_nodes; ++node1) {
		Station *st = Station::Get(other->nodes[node1].station);
		NodeID new_node = this->AddNode(st);
		this->nodes[new_node].supply = other->nodes[node1].supply * age / other_age;
		st->goods[this->cargo].link_graph = this->index;
		st->goods[this->cargo].node = new_node;
		for (NodeID node2 = 0; node2 < node1; ++node2) {
			Edge &forward = this->edges[new_node][first + node2];
			Edge &backward = this->edges[first + node2][new_node];
			forward = other->edges[node1][node2];
			backward = other->edges[node2][node1];
			forward.capacity = forward.capacity * age / other_age;
			forward.usage = forward.usage * age / other_age;
			if (forward.next_edge != INVALID_NODE) forward.next_edge += first;
			backward.capacity = backward.capacity * age / other_age;
			backward.usage = backward.usage * age / other_age;
			if (backward.next_edge != INVALID_NODE) backward.next_edge += first;
		}
		Edge &new_start = this->edges[new_node][new_node];
		new_start = other->edges[node1][node1];
		if (new_start.next_edge != INVALID_NODE) new_start.next_edge += first;
	}
	delete other;
}

/**
 * Remove a node from the link graph by overwriting it with the last node.
 * @param id ID of the node to be removed.
 */
void LinkGraph::RemoveNode(NodeID id)
{
	assert(id < this->num_nodes);

	for (NodeID i = 0; i <= this->num_nodes; ++i) {
		this->RemoveEdge(i, id);
		NodeID prev = i;
		NodeID next = this->GetFirstEdge(i);
		while (next != INVALID_NODE) {
			if (next == this->num_nodes - 1) {
				this->edges[i][prev].next_edge = id;
				break;
			}
		}
		this->edges[i][id] = this->edges[i][this->num_nodes - 1];
	}
	--this->num_nodes;
	this->nodes.Erase(this->nodes.Get(id));
	this->edges.EraseColumn(id);
	/* Not doing EraseRow here, as having the extra invalid row doesn't hurt
	 * and removing it would trigger a lot of memmove. The data has already
	 * been copied around in the loop above. */
}

/**
 * Add a node to the component and create empty edges associated with it. Set
 * the station's last_component to this component. Calculate the distances to all
 * other nodes. The distances to _all_ nodes are important as the demand
 * calculator relies on their availability.
 * @param st New node's station.
 * @return New node's ID.
 */
NodeID LinkGraph::AddNode(const Station *st)
{
	const GoodsEntry &good = st->goods[this->cargo];

	if (this->nodes.Length() == this->num_nodes) {
		this->nodes.Append();
		/* Avoid reducing the height of the matrix as that is expensive and we
		 * most likely will increase it again later which is again expensive. */
		this->edges.Resize(this->num_nodes + 1,
				max(uint(this->num_nodes + 1), this->edges.Height()));
	}

	this->nodes[this->num_nodes].Init(st->index,
			HasBit(good.acceptance_pickup, GoodsEntry::GES_ACCEPTANCE));

	Edge *new_edges = this->edges[this->num_nodes];

	/* Reset the first edge starting at the new node */
	new_edges[this->num_nodes].next_edge = INVALID_NODE;

	for (NodeID i = 0; i <= this->num_nodes; ++i) {
		uint distance = DistanceManhattan(st->xy, Station::Get(this->nodes[i].station)->xy);
		new_edges[i].Init(distance);
		this->edges[i][this->num_nodes].Init(distance);
	}
	return this->num_nodes++;
}

/**
 * Fill an edge with values from a link.
 * @param from Source node of the link.
 * @param to Destination node of the link.
 * @param capacity Capacity of the link.
 */
inline void LinkGraph::AddEdge(NodeID from, NodeID to, uint capacity)
{
	assert(from != to && from < this->num_nodes && to < this->num_nodes);
	Edge &edge = this->edges[from][to];
	Edge &first = this->edges[from][from];
	edge.capacity = capacity;
	edge.next_edge = first.next_edge;
	first.next_edge = to;
	edge.last_update = _date;
}

void LinkGraph::RemoveEdge(NodeID from, NodeID to)
{
	if (from == to) return;
	Edge &edge = this->edges[from][to];
	edge.capacity = 0;
	edge.last_update = INVALID_DATE;
	edge.usage = 0;

	NodeID prev = from;
	NodeID next = this->GetFirstEdge(from);
	while (next != INVALID_NODE) {
		if (next == to) {
			/* Will be removed, skip it. */
			next = this->edges[from][prev].next_edge = edge.next_edge;
			break;
		} else {
			prev = next;
			next = this->edges[from][next].next_edge;
		}
	}
}

/**
 * Create a new edge or update an existing one. If usage is UINT_MAX refresh
 * the edge to have at least the given capacity, otherwise add the capacity.
 * @param from Start node of the edge.
 * @param to End node of the edge.
 * @param capacity Capacity to be added/updated.
 * @param usage Usage to be added or UINT_MAX.
 */
void LinkGraph::UpdateEdge(NodeID from, NodeID to, uint capacity, uint usage)
{
	Edge &edge = this->edges[from][to];
	if (edge.capacity == 0) {
		this->AddEdge(from, to, capacity);
		if (usage != UINT_MAX) edge.usage += usage;
	} else {
		if (usage == UINT_MAX) {
			edge.capacity = max(edge.capacity, capacity);
		} else {
			assert(capacity >= usage);
			edge.capacity += capacity;
			edge.usage += usage;
		}
		edge.last_update = _date;
	}
}

/**
 * Resize the component and fill it with empty nodes and edges. Used when
 * loading from save games.
 *
 * WARNING: The nodes and edges are expected to contain garbage while
 * num_nodes is expected to contain the desired size. Normally this is an
 * invalid state, but just after loading the component's structure it is valid.
 * This method should only be called from Load_LGRP and Load_LGRJ.
 */
void LinkGraph::SetSize()
{
	if (this->nodes.Length() < this->num_nodes) {
		this->edges.Resize(this->num_nodes, this->num_nodes);
		this->nodes.Resize(this->num_nodes);
	}

	for (uint i = 0; i < this->num_nodes; ++i) {
		this->nodes[i].Init();
		Edge *column = this->edges[i];
		for (uint j = 0; j < this->num_nodes; ++j) {
			column[j].Init();
		}
	}
}

/**
 * Create a link graph job from a link graph. The link graph will be copied so
 * that the calculations don't interfer with the normal operations on the
 * original. The job is immediately started.
 * @param orig Original LinkGraph to be copied.
 */
LinkGraphJob::LinkGraphJob(const LinkGraph &orig) :
		/* Copying the link graph here also copies its index member.
		 * This is on purpose. */
		link_graph(orig),
		settings(_settings_game.linkgraph),
		thread(NULL),
		join_date(_date + _settings_game.linkgraph.recalc_time)
{
}

/**
 * Join the link graph job and destroy it.
 */
LinkGraphJob::~LinkGraphJob()
{
	assert(this->thread == NULL);
}

/**
 * Start the next job in the schedule.
 */
void LinkGraphSchedule::SpawnNext()
{
	if (this->schedule.empty()) return;
	LinkGraph *next = this->schedule.front();
	assert(next == LinkGraph::Get(next->index));
	this->schedule.pop_front();
	if (LinkGraphJob::CanAllocateItem()) {
		LinkGraphJob *job = new LinkGraphJob(*next);
		job->SpawnThread();
		this->running.push_back(job);
	} else {
		DEBUG(misc, 0, "Can't allocate link graph job");
	}
}

/**
 * Join the next finished job, if available.
 */
void LinkGraphSchedule::JoinNext()
{
	if (this->running.empty()) return;
	LinkGraphJob *next = this->running.front();
	if (!next->IsFinished()) return;
	this->running.pop_front();
	LinkGraphID id = next->Graph().index;
	next->JoinThread();
	delete next;
	if (LinkGraph::IsValidID(id)) {
		LinkGraph *lg = LinkGraph::Get(id);
		this->Unqueue(lg); // Unqueue to avoid double-queueing recycled IDs.
		this->Queue(lg);
	}
}

/**
 * Run all handlers for the given Job.
 * @param j Pointer to a link graph job.
 */
/* static */ void LinkGraphSchedule::Run(void *j)
{
	LinkGraphJob *job = (LinkGraphJob *)j;
	LinkGraphSchedule *schedule = LinkGraphSchedule::Instance();
	for (uint i = 0; i < lengthof(schedule->handlers); ++i) {
		schedule->handlers[i]->Run(job);
	}
}

/**
 * Join the calling thread with this job's thread if threading is enabled.
 */
void LinkGraphJob::JoinThread()
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
	if (!ThreadObject::New(&(LinkGraphSchedule::Run), this, &(this->thread))) {
		this->thread = NULL;
		/* Of course this will hang a bit.
		 * On the other hand, if you want to play games which make this hang noticably
		 * on a platform without threads then you'll probably get other problems first.
		 * OK:
		 * If someone comes and tells me that this hangs for him/her, I'll implement a
		 * smaller grained "Step" method for all handlers and add some more ticks where
		 * "Step" is called. No problem in principle.
		 */
		LinkGraphSchedule::Run(this);
	}
}

/**
 * Start all threads in the running list. This is only useful for save/load.
 * Usually threads are started when the job is created.
 */
void LinkGraphSchedule::SpawnAll()
{
	for (JobList::iterator i = this->running.begin(); i != this->running.end(); ++i) {
		(*i)->SpawnThread();
	}
}

/**
 * Clear all link graphs and jobs from the schedule.
 */
/* static */ void LinkGraphSchedule::Clear()
{
	LinkGraphSchedule *inst = LinkGraphSchedule::Instance();
	for (JobList::iterator i(inst->running.begin()); i != inst->running.end(); ++i) {
		(*i)->JoinThread();
	}
	inst->running.clear();
	inst->schedule.clear();
}

/**
 * Create a link graph schedule and initialize its handlers.
 */
LinkGraphSchedule::LinkGraphSchedule()
{
	this->handlers[0] = new InitHandler;
	this->handlers[1] = new DemandHandler;
	this->handlers[2] = new MCFHandler<MCF1stPass>;
	this->handlers[3] = new MCFHandler<MCF2ndPass>;
}

/**
 * Delete a link graph schedule and its handlers.
 */
LinkGraphSchedule::~LinkGraphSchedule()
{
	this->Clear();
	for (uint i = 0; i < lengthof(this->handlers); ++i) {
		delete this->handlers[i];
	}
}

/**
 * Retriebe the link graph schedule or create it if necessary.
 */
/* static */ LinkGraphSchedule *LinkGraphSchedule::Instance()
{
	static LinkGraphSchedule inst;
	return &inst;
}

/**
 * Spawn or join a link graph job if any link graph is due to do so.
 */
void OnTick_LinkGraph()
{
	if (_date_fract == LinkGraphSchedule::SPAWN_JOIN_TICK) {
		Date offset = _date % _settings_game.linkgraph.recalc_interval;
		if (offset == 0) {
			LinkGraphSchedule::Instance()->SpawnNext();
		} else if (offset == _settings_game.linkgraph.recalc_interval / 2) {
			LinkGraphSchedule::Instance()->JoinNext();
		}
	} else if (_date_fract == LinkGraph::COMPRESSION_TICK) {
		LinkGraph *lg;
		/* Compress graphs after 256 to 512 days; approximately once a year. */
		int cutoff = RandomRange(256) + 256;
		FOR_ALL_LINK_GRAPHS(lg) {
			if (_date - lg->GetLastCompression() > cutoff) {
				lg->Compress();
				break;
			}
		}
	}
}

/**
 * Add this path as a new child to the given base path, thus making this path
 * a "fork" of the base path.
 * @param base Path to fork from.
 * @param cap Maximum capacity of the new leg.
 * @param free_cap Remaining free capacity of the new leg.
 * @param dist Distance of the new leg.
 */
void Path::Fork(Path *base, uint cap, int free_cap, uint dist)
{
	this->capacity = min(base->capacity, cap);
	this->free_capacity = min(base->free_capacity, free_cap);
	this->distance = base->distance + dist;
	assert(this->distance > 0);
	if (this->parent != base) {
		this->Detach();
		this->parent = base;
		this->parent->num_children++;
	}
	this->origin = base->origin;
}

/**
 * Push some flow along a path and register the path in the nodes it passes if
 * successful.
 * @param new_flow Amount of flow to push.
 * @param job Link graph job this node belongs to.
 * @param max_saturation Maximum saturation of edges.
 * @return Amount of flow actually pushed.
 */
uint Path::AddFlow(uint new_flow, LinkGraphJob *job, uint max_saturation)
{
	if (this->parent != NULL) {
		EdgeAnnotation &edge = job->GetEdge(this->parent->node, this->node);
		if (max_saturation != UINT_MAX) {
			uint usable_cap = job->Graph().GetEdge(this->parent->node, this->node).capacity *
					max_saturation / 100;
			if (usable_cap > edge.flow) {
				new_flow = min(new_flow, usable_cap - edge.flow);
			} else {
				return 0;
			}
		}
		new_flow = this->parent->AddFlow(new_flow, job, max_saturation);
		if (new_flow > 0) {
			job->GetNode(this->parent->node).paths.insert(this);
		}
		edge.flow += new_flow;
	}
	this->flow += new_flow;
	return new_flow;
}

/**
 * Create a leg of a path in the link graph.
 * @param n Id of the link graph node this path passes.
 * @param source If true, this is the first leg of the path.
 */
Path::Path(NodeID n, bool source) :
	distance(source ? 0 : UINT_MAX),
	capacity(0),
	free_capacity(source ? INT_MAX : INT_MIN),
	flow(0), node(n), origin(source ? n : INVALID_NODE),
	num_children(0), parent(NULL)
{}

