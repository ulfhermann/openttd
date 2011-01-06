/** @file mcf.cpp Definition of Multi-Commodity-Flow solver. */

#include "mcf.h"
#include "../core/math_func.hpp"

/**
 * Determines if an extension to the given Path with the given parameters is
 * better than this path.
 * @param base Other path.
 * @param cap Capacity of the new edge to be added to base.
 * @param dist Distance of the new edge.
 * @return True if base + the new edge would be better than the path associated
 * with this annotation.
 */
bool DistanceAnnotation::IsBetter(const DistanceAnnotation *base, uint cap,
		int free_cap, uint dist) const
{
	/* If any of the paths is disconnected, the other one is better. If both
	 * are disconnected, this path is better.
	 */
	if (base->distance == UINT_MAX) {
		return false;
	} else if (this->distance == UINT_MAX) {
		return true;
	}

	if (free_cap > 0 && base->free_capacity > 0) {
		/* If both paths have capacity left, compare their distances.
		 * If the other path has capacity left and this one hasn't, the
		 * other one's better.
		 */
		return this->free_capacity > 0 ? (base->distance + dist < this->distance) : true;
	} else {
		/* If the other path doesn't have capacity left, but this one has,
		 * this one is better.
		 * If both paths are out of capacity, do the regular distance
		 * comparison.
		 */
		return this->free_capacity > 0 ? false : (base->distance + dist < this->distance);
	}
}

/**
 * Determines if an extension to the given Path with the given parameters is
 * better than this path.
 * @param base Other path.
 * @param cap Capacity of the new edge to be added to base.
 * @param dist Distance of the new edge.
 * @return True if base + the new edge would be better than the path associated
 * with this annotation.
 */
bool CapacityAnnotation::IsBetter(const CapacityAnnotation *base, uint cap,
		int free_cap, uint dist) const
{
	int min_cap = (min(base->free_capacity, free_cap) << 4) / (min(base->capacity, cap) + 1);
	int this_cap = this->GetCapacityRatio();
	if (min_cap == this_cap) {
		/* If the capacities are the same and the other path isn't disconnected
		 * choose the shorter path.
		 */
		return base->distance == UINT_MAX ? false : (base->distance + dist < this->distance);
	} else {
		return min_cap > this_cap;
	}
}

/**
 * A slightly modified Dijkstra algorithm. Grades the paths not necessarily by
 * distance, but by the value Tannotation computes. It can also be configured
 * to only use paths already created before and not create new ones. If this is
 * not done it uses the short_path_saturation setting to artificially decrease
 * capacities. If a path has already been created is determined by checking the
 * flows associated with its nodes.
 * @tparam Tannotation Annotation to be used.
 * @param source_node Node where the algorithm starts.
 * @param paths Container for the paths to be calculated.
 * @param create_new_paths If false, only use paths already seen before,
 *                         otherwise artificially limit the capacity.
 */
template<class Tannotation>
void MultiCommodityFlow::Dijkstra(NodeID source_node, PathVector &paths,
		bool create_new_paths)
{
	typedef std::set<Tannotation *, typename Tannotation::comp> AnnoSet;
	uint size = this->graph->GetSize();
	StationID source_station = this->graph->GetNode(source_node).station;
	AnnoSet annos;
	paths.resize(size, NULL);
	for (NodeID node = 0; node < size; ++node) {
		Tannotation *anno = new Tannotation(node, node == source_node);
		annos.insert(anno);
		paths[node] = anno;
	}
	while (!annos.empty()) {
		typename AnnoSet::iterator i = annos.begin();
		Tannotation *source = *i;
		annos.erase(i);
		NodeID from = source->GetNode();
		NodeID to = this->graph->GetFirstEdge(from);
		while (to != INVALID_NODE) {
			Edge &edge = this->graph->GetEdge(from, to);
			assert(edge.distance < UINT_MAX);
			if (create_new_paths ||
					this->graph->GetNode(from).flows[source_station]
					[this->graph->GetNode(to).station] > 0) {
				uint capacity = edge.capacity;
				if (create_new_paths) {
					capacity *= this->graph->GetSettings().short_path_saturation;
					capacity /= 100;
					if (capacity == 0) capacity = 1;
				}
				/* punish in-between stops a little */
				uint distance = edge.distance + 1;
				Tannotation *dest = static_cast<Tannotation *>(paths[to]);
				if (dest->IsBetter(source, capacity, capacity - edge.flow, distance)) {
					annos.erase(dest);
					dest->Fork(source, capacity, capacity - edge.flow, distance);
					annos.insert(dest);
				}
			}
			to = edge.next_edge;
		}
	}
}

/**
 * Clean up paths that lead nowhere and the root path.
 * @param source_id ID of the root node.
 * @param paths Paths to be cleaned up.
 */
void MultiCommodityFlow::CleanupPaths(NodeID source_id, PathVector &paths) {
	Path *source = paths[source_id];
	paths[source_id] = NULL;
	for(PathVector::iterator i = paths.begin(); i != paths.end(); ++i) {
		Path *path = *i;
		if (path != NULL) {
			if (path->GetParent() == source) path->Detach();
			while (path != source && path != NULL && path->GetFlow() == 0) {
				Path *parent = path->GetParent();
				path->Detach();
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
 * @param edge Edge whose ends the path connects.
 * @param path End of the path the flow should be pushed on.
 * @param accuracy Accuracy of the calculation.
 * @param positive_cap If true only push flow up to the paths capacity,
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
 * @param path Set of paths that form the cycle.
 * @param cycle_begin Path to start at.
 * @return Flow along the cycle.
 */
uint MCF1stPass::FindCycleFlow(const PathVector &path, const Path *cycle_begin)
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
 * @param path Set of paths containing the cycle.
 * @param cycle_begin Part of the cycle to start at.
 * @param flow Flow along the cycle.
 */
void MCF1stPass::EliminateCycle(PathVector &path, Path *cycle_begin, uint flow)
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
 * @param path Paths checked in parent calls to this method.
 * @param origin_id Origin of the paths to be checked.
 * @param next_id Next node to be checked.
 * @return If any cycles have been found and eliminated.
 */
bool MCF1stPass::EliminateCycles(PathVector &path, NodeID origin_id, NodeID next_id)
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
		for (PathViaMap::iterator via_it = next_hops.begin();
				via_it != next_hops.end(); ++via_it)
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
		path[next_id] = found ? NULL : invalid_path;
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
 * @return If any cycles have been found and eliminated.
 */
bool MCF1stPass::EliminateCycles()
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
 * Run the first pass of the MCF calculation.
 * @param graph Component to calculate.
 */
MCF1stPass::MCF1stPass(LinkGraphComponent *graph) : MultiCommodityFlow(graph)
{
	PathVector paths;
	uint size = this->graph->GetSize();
	uint accuracy = this->graph->GetSettings().accuracy;
	bool more_loops = true;

	while (more_loops) {
		more_loops = false;

		for (NodeID source = 0; source < size; ++source) {
			/* first saturate the shortest paths */
			this->Dijkstra<DistanceAnnotation>(source, paths, true);

			for (NodeID dest = 0; dest < size; ++dest) {
				Edge &edge = this->graph->GetEdge(source, dest);
				if (edge.unsatisfied_demand > 0) {
					Path *path = paths[dest];
					assert(path != NULL);
					/* generally only allow paths that don't exceed the
					 * available capacity. But if no demand has been assigned
					 * yet, make an exception and allow any valid path *once*.
					 */
					if (path->GetFreeCapacity() > 0 && this->PushFlow(edge, path,
							accuracy, true) > 0) {
						/* if a path has been found there is a chance we can
						 * find more
						 */
						more_loops = (edge.unsatisfied_demand > 0);
					} else if (edge.unsatisfied_demand == edge.demand &&
							path->GetFreeCapacity() > INT_MIN) {
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
 * Run the second pass of the MCF calculation.
 * @param graph Component to calculate.
 */
MCF2ndPass::MCF2ndPass(LinkGraphComponent *graph) : MultiCommodityFlow(graph)
{
	PathVector paths;
	uint size = this->graph->GetSize();
	uint accuracy = this->graph->GetSettings().accuracy;
	bool demand_left = true;
	while (demand_left) {
		demand_left = false;
		for (NodeID source = 0; source < size; ++source) {
			/* Then assign all remaining demands */
			this->Dijkstra<CapacityAnnotation>(source, paths, false);
			for (NodeID dest = 0; dest < size; ++dest) {
				Edge &edge = this->graph->GetEdge(source, dest);
				Path *path = paths[dest];
				if (edge.unsatisfied_demand > 0 && path->GetFreeCapacity() > INT_MIN)
				{
					this->PushFlow(edge, path, accuracy, false);
					if (edge.unsatisfied_demand > 0) demand_left = true;
				}
			}
			CleanupPaths(source, paths);
		}
	}
}

/**
 * Relation that creates a weak order without duplicates.
 * Avoid accidentally deleting different paths of the same capacity/distance in
 * a set. When the annotation is the same node IDs are compared, so there are
 * no equal ranges.
 * @tparam T Type to be compared on.
 * @param x_anno First value.
 * @param y_anno Second value.
 * @param x Node id associated with the first value.
 * @param y Node id associated with the second value.
 */
template <typename T>
bool greater(T x_anno, T y_anno, NodeID x, NodeID y) {
	if (x_anno > y_anno) {
		return true;
	} else if (x_anno < y_anno) {
		return false;
	} else {
		return x > y;
	}
}

/**
 * Compare two capacity annotations.
 * @param x First capacity annotation.
 * @param y Second capacity annotation.
 * @return If x is better than y.
 */
bool CapacityAnnotation::comp::operator()(const CapacityAnnotation *x,
		const CapacityAnnotation *y) const
{
	return x != y && greater<int>(x->GetAnnotation(), y->GetAnnotation(),
			x->GetNode(), y->GetNode());
}

/**
 * Compare two distance annotations.
 * @param x First distance annotation.
 * @param y Second distance annotation.
 * @return If x is better than y.
 */
bool DistanceAnnotation::comp::operator()(const DistanceAnnotation *x,
		const DistanceAnnotation *y) const
{
	return x != y && !greater<uint>(x->GetAnnotation(), y->GetAnnotation(),
			x->GetNode(), y->GetNode());
}
