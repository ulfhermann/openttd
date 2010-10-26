/** @file mcf.cpp Definition of Multi-Commodity-Flow solver */

#include "mcf.h"
#include "../core/math_func.hpp"

/**
 * Determines if an extension to the given Path with the given parameters is
 * better than this path.
 * @param base the other path
 * @param cap the capacity of the new edge to be added to base
 * @param dist the distance of the new edge
 * @return true if base + the new edge would be better than the path associated
 * with this annotation.
 */
bool DistanceAnnotation::IsBetter(const DistanceAnnotation *base, int cap,
		uint dist) const
{
	/* If any of the paths is disconnected, the other one is better. If both
	 * are disconnected, this path is better.
	 */
	if (base->distance == UINT_MAX) {
		return false;
	} else if (this->distance == UINT_MAX) {
		return true;
	}

	if (cap > 0 && base->capacity > 0) {
		if (this->capacity > 0) {
			/* If both paths have capacity left, compare their distances. */
			return base->distance + dist < this->distance;
		} else {
			/* If the other path has capacity left and this one hasn't, the
			 * other one's better.
			 */
			return true;
		}
	} else {
		if (this->capacity > 0) {
			/* If the other path doesn't have capacity left, but this one has,
			 * this one is better.
			 */
			return false;
		} else {
			/* If both paths are out of capacity, do the regular distance
			 * comparison.
			 */
			return base->distance + dist < this->distance;
		}
	}
}

/**
 * A slightly modified Dijkstra algorithm. It uses the short_path_saturation
 * setting to artificially decrease capacities.
 * @param source_node the node where the algorithm starts.
 * @param paths a container for the paths to be calculated
 */
void MultiCommodityFlow::Dijkstra(NodeID source_node, PathVector &paths)
{
	typedef std::set<DistanceAnnotation *, DistanceAnnotation::comp> AnnoSet;
	uint size = this->graph->GetSize();
	AnnoSet annos;
	paths.resize(size, NULL);
	for (NodeID node = 0; node < size; ++node) {
		DistanceAnnotation *anno = new DistanceAnnotation(node, node == source_node);
		annos.insert(anno);
		paths[node] = anno;
	}
	while(!annos.empty()) {
		AnnoSet::iterator i = annos.begin();
		DistanceAnnotation *source = *i;
		annos.erase(i);
		NodeID from = source->GetNode();
		NodeID to = this->graph->GetFirstEdge(from);
		while (to != INVALID_NODE) {
			Edge &edge = this->graph->GetEdge(from, to);
			assert(edge.distance < UINT_MAX);

			int capacity = edge.capacity;
			capacity *=	this->graph->GetSettings().short_path_saturation;
			capacity /= 100;
			if (capacity == 0) capacity = 1;
			assert(capacity > 0);

			capacity -= edge.flow;
			/* punish in-between stops a little */
			uint distance = edge.distance + 1;
			DistanceAnnotation *dest = static_cast<DistanceAnnotation *>(paths[to]);
			if (dest->IsBetter(source, capacity, distance)) {
				annos.erase(dest);
				dest->Fork(source, capacity, distance);
				annos.insert(dest);
			}

			to = edge.next_edge;
		}
	}
}

/**
 * Clean up paths that lead nowhere and the root path.
 * @param source_id ID of the root node
 * @param paths the paths to be cleaned up
 */
void MultiCommodityFlow::CleanupPaths(NodeID source_id, PathVector &paths) {
	Path *source = paths[source_id];
	paths[source_id] = NULL;
	for(PathVector::iterator i = paths.begin(); i != paths.end(); ++i) {
		Path *path = *i;
		if (path != NULL) {
			if (path->GetParent() == source) path->UnFork();
			while (path != source && path != NULL && path->GetFlow() == 0) {
				Path *parent = path->GetParent();
				path->UnFork();
				if (path->GetNumChildren() == 0) {
					paths[path->GetNode()] = NULL;
					delete path;
				}
				path = parent;
			}
		}
	}
	delete source;
	paths.clear();
}

/**
 * Push flow along a path and update the unsatisfied_demand of the associated
 * edge.
 * @param edge the edge whose ends the path connects
 * @param path the end of the path the flow should be pushed on
 * @param accuracy the accuracy of the calculation
 * @param positive_cap if true only push flow up to the paths capacity,
 *                     otherwise the path can be "overloaded".
 */
uint MultiCommodityFlow::PushFlow(Edge &edge, Path *path, uint accuracy,
		bool positive_cap)
{
	assert(edge.unsatisfied_demand > 0);
	uint flow = Clamp(edge.demand / accuracy, 1, edge.unsatisfied_demand);
	flow = path->AddFlow(flow, this->graph, positive_cap);
	edge.unsatisfied_demand -= flow;
	return flow;
}

/**
 * Find the flow along a cycle including cycle_begin in path.
 * @param path the set of paths that form the cycle
 * @param cycle_begin the path to start at
 * @return the flow along the cycle
 */
uint MultiCommodityFlow::FindCycleFlow(const PathVector &path, const Path *cycle_begin)
{
	uint flow = UINT_MAX;
	const Path *cycle_end = cycle_begin;
	do {
		flow = min(flow, cycle_begin->GetFlow());
		cycle_begin = path[cycle_begin->GetNode()];
	} while(cycle_begin != cycle_end);
	return flow;
}

/**
 * Eliminate a cycle of the given flow in the given set of paths.
 * @param path the set of paths containing the cycle
 * @param cycle_begin a part the cycle to start at
 * @param flow the flow along the cycle
 */
void MultiCommodityFlow::EliminateCycle(PathVector &path, Path *cycle_begin, uint flow)
{
	Path *cycle_end = cycle_begin;
	do {
		NodeID prev = cycle_begin->GetNode();
		cycle_begin->ReduceFlow(flow);
		cycle_begin = path[cycle_begin->GetNode()];
		Edge &edge = this->graph->GetEdge(prev, cycle_begin->GetNode());
		edge.flow -= flow;
	} while(cycle_begin != cycle_end);
}

/**
 * Eliminate cycles for origin_id in the graph. Start searching at next_id and
 * work recursively. Also "summarize" paths: Add up the flows along parallel
 * paths in one.
 * @param path the paths checked in parent calls to this method
 * @param origin_id the origin of the paths to be checked
 * @param next_id the next node to be checked
 * @return if any cycles have been found and eliminated
 */
bool MultiCommodityFlow::EliminateCycles(PathVector &path, NodeID origin_id,
		NodeID next_id)
{
	static Path *invalid_path = new Path(INVALID_NODE, true);
	Path *at_next_pos = path[next_id];
	if (at_next_pos == invalid_path) {
		/* this node has already been searched */
		return false;
	} else if (at_next_pos == NULL) {
		/* summarize paths; add up the paths with the same source and next hop
		 * in one path each
		 */
		PathSet &paths = this->graph->GetNode(next_id).paths;
		PathViaMap next_hops;
		for(PathSet::iterator i = paths.begin(); i != paths.end(); ++i) {
			Path *new_child = *i;
			if (new_child->GetOrigin() == origin_id) {
				PathViaMap::iterator via_it = next_hops.find(new_child->GetNode());
				if (via_it == next_hops.end()) {
					next_hops[new_child->GetNode()] = new_child;
				} else {
					Path *child = via_it->second;
					uint new_flow = new_child->GetFlow();
					child->AddFlow(new_flow);
					new_child->ReduceFlow(new_flow);
				}
			}
		}
		bool found = false;
		/* search the next hops for nodes we have already visited */
		for (PathViaMap::iterator via_it = next_hops.begin(); via_it != next_hops.end(); ++via_it)
		{
			Path *child = via_it->second;
			if (child->GetFlow() > 0) {
				/* push one child into the path vector and search this child's
				 * children
				 */
				path[next_id] = child;
				found = this->EliminateCycles(path, origin_id, child->GetNode()) || found;
			}
		}
		/* All paths departing from this node have been searched. Mark as
		 * resolved if no cycles found. If cycles were found further cycles
		 * could be found in this branch, thus it has to be searched again next
		 * time we spot it.
		 */
		if (found) {
			path[next_id] = NULL;
		} else {
			path[next_id] = invalid_path;
		}
		return found;
	} else {
		/* this node has already been visited => we have a cycle
		 * backtrack to find the exact flow
		 */
		uint flow = this->FindCycleFlow(path, at_next_pos);
		if (flow > 0) {
			this->EliminateCycle(path, at_next_pos, flow);
			return true;
		} else {
			return false;
		}
	}
}

/**
 * Eliminate all cycles in the graph. Check paths starting at each node for
 * potential cycles.
 * @return if any cycles have been found and eliminated.
 */
bool MultiCommodityFlow::EliminateCycles()
{
	bool cycles_found = false;
	uint size = this->graph->GetSize();
	PathVector path(size, NULL);
	for (NodeID node = 0; node < size; ++node) {
		/* starting at each node in the graph find all cycles involving this
		 * node
		 */
		std::fill(path.begin(), path.end(), (Path *)NULL);
		cycles_found = this->EliminateCycles(path, node, node) || cycles_found;
	}
	return cycles_found;
}

/**
 * Run the MCF calculation.
 * @param graph the component to calculate.
 */
MultiCommodityFlow::MultiCommodityFlow(LinkGraphComponent *graph) : graph(graph)
{
	PathVector paths;
	uint size = this->graph->GetSize();
	uint accuracy = this->graph->GetSettings().accuracy;
	bool more_loops = true;

	while (more_loops) {
		more_loops = false;

		for (NodeID source = 0; source < size; ++source) {
			/* saturate the shortest paths */
			this->Dijkstra(source, paths);

			for (NodeID dest = 0; dest < size; ++dest) {
				Edge &edge = this->graph->GetEdge(source, dest);
				if (edge.unsatisfied_demand > 0) {
					Path *path = paths[dest];
					assert(path != NULL);
					/* generally only allow paths that don't exceed the
					 * available capacity. But if no demand has been assigned
					 * yet, make an exception and allow any valid path *once*.
					 */
					if (path->GetCapacity() > 0 && this->PushFlow(edge, path,
							accuracy, true) > 0)
					{
						/* if a path has been found there is a chance we can
						 * find more
						 */
						more_loops = (edge.unsatisfied_demand > 0);
					} else if (edge.unsatisfied_demand == edge.demand &&
							path->GetCapacity() > INT_MIN)
					{
						this->PushFlow(edge, path, accuracy, false);
					}
				}
			}
			CleanupPaths(source, paths);
		}
		if (!more_loops) more_loops = EliminateCycles();
	}
}

/**
 * Compare two distance annotations. Implements a relation that creates a weak
 * order without duplicates. Avoid accidentally deleting different paths of the
 * same distance in a set. When the annotation is the same node IDs are
 * compared, so there are no equal ranges.
 * @param x the first distance annotation
 * @param y the second distance annotation
 * @return if x is better than y
 */
bool DistanceAnnotation::comp::operator()(const DistanceAnnotation *x,
		const DistanceAnnotation *y) const
{
	if (x == y) {
		return false;
	} else if (x->GetDistance() > y->GetDistance()) {
		return true;
	} else if (x->GetDistance() < y->GetDistance()) {
		return false;
	} else {
		return x->GetNode() > y->GetNode();
	}
}
