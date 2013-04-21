/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file linkgraphjob.cpp Definition of link graph job classes used for cargo distribution. */

#include "../stdafx.h"
#include "../core/pool_func.hpp"
#include "../window_func.h"
#include "linkgraphjob.h"

/* Initialize the link-graph-job-pool */
LinkGraphJobPool _link_graph_job_pool("LinkGraphJob");
INSTANTIATE_POOL_METHODS(LinkGraphJob)

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
	uint size = this->Size();
	for (NodeID node_id = 0; node_id < size; ++node_id) {
		StationID station = (*this)[node_id].Station();
		if (!Station::IsValidID(station)) {
			/* Station got removed during the link graph run. We have to remove all
			 * flows pointing to it now. This is costly but it should be rare. */
			for (NodeID from_id = 0; from_id < size; ++from_id) {
				if ((*this)[from_id][node_id].Capacity() > 0) {
					FlowStatMap &flows = this->nodes[from_id].flows;
					for (FlowStatMap::iterator it(flows.begin()); it != flows.end();) {
						it->second.ChangeShare(station, INT_MIN);
						if (it->second.GetShares()->empty()) {
							flows.erase(it++);
						} else {
							++it;
						}
					}
				}
			}
			station = INVALID_STATION;
		}
	}
	for (NodeID node_id = 0; node_id < size; ++node_id) {
		StationID station = (*this)[node_id].Station();
		if (Station::IsValidID(station)) {
			InvalidateWindowData(WC_STATION_VIEW, station, this->Cargo());
			Station::Get(station)->goods[this->Cargo()].flows.
					swap(this->nodes[node_id].flows);
		}
	}
}

/**
 * Initialize the link graph job: Resize nodes and edges and populate them.
 * This is done after the constructor so that we can do it in the calculation
 * thread without delaying the main game.
 */
void LinkGraphJob::Init()
{
	uint size = this->Size();
	this->nodes.Resize(size);
	this->edges.Resize(size, size);
	for (uint i = 0; i < size; ++i) {
		this->nodes[i].Init(this->link_graph[i].Supply());
		EdgeAnnotation *node_edges = this->edges[i];
		for (uint j = 0; j < size; ++j) {
			node_edges[j].Init();
		}
	}
}

/**
 * Initialize a linkgraph job edge.
 */
void LinkGraphJob::EdgeAnnotation::Init()
{
	this->demand = 0;
	this->flow = 0;
	this->unsatisfied_demand = 0;
}

/**
 * Initialize a Linkgraph job node. The underlying memory is expected to be
 * freshly allocated, without any constructors having been called.
 * @param supply Initial undelivered supply.
 */
void LinkGraphJob::NodeAnnotation::Init(uint supply)
{
	this->undelivered_supply = supply;
	new (&this->flows) FlowStatMap;
	new (&this->paths) PathSet;
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
uint Path::AddFlow(uint new_flow, LinkGraphJob &job, uint max_saturation)
{
	if (this->parent != NULL) {
		LinkGraphJob::Edge edge = job[this->parent->node][this->node];
		if (max_saturation != UINT_MAX) {
			uint usable_cap = edge.Capacity() * max_saturation / 100;
			if (usable_cap > edge.Flow()) {
				new_flow = min(new_flow, usable_cap - edge.Flow());
			} else {
				return 0;
			}
		}
		new_flow = this->parent->AddFlow(new_flow, job, max_saturation);
		if (new_flow > 0) {
			job[this->parent->node].Paths().insert(this);
		}
		edge.AddFlow(new_flow);
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

