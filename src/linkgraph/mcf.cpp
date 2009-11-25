/** @file mcf.cpp Definition of Multi-Commodity-Flow solver */

#include "mcf.h"
#include "../core/math_func.hpp"

MultiCommodityFlow::MultiCommodityFlow() :
	graph(NULL)
{}

void MultiCommodityFlow::Run(LinkGraphComponent * g) {
	assert(g->GetSettings().accuracy >= 1);
	graph = g;
}

bool DistanceAnnotation::IsBetter(const DistanceAnnotation * base, int cap, uint dist) const {
	if (cap > 0 && base->capacity > 0) {
		if (capacity <= 0) {
			return true; // if the other path has capacity left and this one hasn't, the other one's better
		} else {
			return base->distance + dist < distance;
		}
	} else {
		if (capacity > 0 || base->distance == UINT_MAX) {
			return false; // if the other path doesn't have capacity left or is disconnected, but this one has, this one is always better
		} else {
			/* if both paths are out of capacity, do the regular distance comparison again */
			return base->distance + dist < distance;
		}
	}
}

bool CapacityAnnotation::IsBetter(const CapacityAnnotation * base, int cap, uint dist) const {
	int min_cap = min(base->capacity, cap);
	if (min_cap == capacity) {
		if (base->distance != UINT_MAX) { // if the capacities are the same, choose the shorter path
			return (base->distance + dist < distance);
		} else {
			return false;
		}
	} else {
		return min_cap > capacity;
	}
}

template<class ANNOTATION>
void MultiCommodityFlow::Dijkstra(NodeID source_node, PathVector & paths, bool create_new_paths) {
	typedef std::set<ANNOTATION *, typename ANNOTATION::comp> AnnoSet;
	uint size = graph->GetSize();
	StationID source_station = graph->GetNode(source_node).station;
	AnnoSet annos;
	paths.resize(size, NULL);
	for (NodeID node = 0; node < size; ++node) {
		ANNOTATION * anno = new ANNOTATION(node, node == source_node);
		annos.insert(anno);
		paths[node] = anno;
	}
	while(!annos.empty()) {
		typename AnnoSet::iterator i = annos.begin();
		ANNOTATION * source = *i;
		annos.erase(i);
		NodeID from = source->GetNode();
		NodeID to = graph->GetFirstEdge(from);
		while (to != Node::INVALID) {
			Edge & edge = graph->GetEdge(from, to);
			assert(edge.capacity > 0 && edge.distance < UINT_MAX);
			if (create_new_paths || graph->GetNode(from).flows[source_station][graph->GetNode(to).station] > 0) {
				int capacity = edge.capacity;
				if (create_new_paths) {
					capacity *= graph->GetSettings().short_path_saturation;
					capacity /= 100;
					if (capacity == 0) {
						capacity = 1;
					}
				}
				capacity -= edge.flow;
				uint distance = edge.distance + 1; // punish in-between stops a little
				ANNOTATION * dest = static_cast<ANNOTATION *>(paths[to]);
				if (dest->IsBetter(source, capacity, distance)) {
					annos.erase(dest);
					dest->Fork(source, capacity, distance);
					annos.insert(dest);
				}
			}
			to = edge.next_edge;
		}
	}
}



void MultiCommodityFlow::CleanupPaths(NodeID source_id, PathVector & paths) {
	Path * source = paths[source_id];
	paths[source_id] = NULL;
	for(PathVector::iterator i = paths.begin(); i != paths.end(); ++i) {
		Path * path = *i;
		if (path != NULL) {
			if (path->GetParent() == source) {
				path->UnFork();
			}
			while (path != source && path != NULL && path->GetFlow() == 0) {
				Path * parent = path->GetParent();
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

uint MultiCommodityFlow::PushFlow(Edge &edge, Path * path, uint accuracy, bool positive_cap) {
	uint flow = edge.unsatisfied_demand / accuracy;
	if (flow == 0) flow = 1;
	flow = path->AddFlow(flow, graph, positive_cap);
	edge.unsatisfied_demand -= flow;
	return flow;
}

uint MCF1stPass::FindCycleFlow(const PathVector & path, const Path * cycle_begin)
{
	uint flow = UINT_MAX;
	const Path * cycle_end = cycle_begin;
	do {
		flow = min(flow, cycle_begin->GetFlow());
		cycle_begin = path[cycle_begin->GetNode()];
	} while(cycle_begin != cycle_end);
	return flow;
}

void MCF1stPass::EliminateCycle(PathVector & path, Path * cycle_begin, uint flow)
{
	Path * cycle_end = cycle_begin;
	do {
		NodeID prev = cycle_begin->GetNode();
		cycle_begin->ReduceFlow(flow);
		cycle_begin = path[cycle_begin->GetNode()];
		Edge & edge = this->graph->GetEdge(prev, cycle_begin->GetNode());
		edge.flow -= flow;
	} while(cycle_begin != cycle_end);
}

bool MCF1stPass::EliminateCycles(PathVector & path, NodeID origin_id, NodeID next_id)
{
	static Path * invalid_path = new Path(Node::INVALID, true);
	Path * at_next_pos = path[next_id];
	if (at_next_pos == invalid_path) {
		/* this node has already been searched */
		return false;
	} else if (at_next_pos == NULL) {
		/* summarize paths; add up the paths with the same source and next hop in one path each */
		PathSet & paths = this->graph->GetNode(next_id).paths;
		PathViaMap next_hops;
		for(PathSet::iterator i = paths.begin(); i != paths.end(); ++i) {
			Path * new_child = *i;
			if (new_child->GetOrigin() == origin_id) {
				PathViaMap::iterator via_it = next_hops.find(new_child->GetNode());
				if (via_it == next_hops.end()) {
					next_hops[new_child->GetNode()] = new_child;
				} else {
					Path * child = via_it->second;
					uint new_flow = new_child->GetFlow();
					child->AddFlow(new_flow);
					new_child->ReduceFlow(new_flow);
				}
			}
		}
		bool found = false;
		/* search the next hops for nodes we have already visited */
		for (PathViaMap::iterator via_it = next_hops.begin(); via_it != next_hops.end(); ++via_it) {
			Path * child = via_it->second;
			if (child->GetFlow() > 0) {
				/* push one child into the path vector and search this child's children */
				path[next_id] = child;
				found = EliminateCycles(path, origin_id, child->GetNode()) || found;
			}
		}
		/* All paths departing from this node have been searched. Mark as resolved if no cycles found.
		 * If cycles were found further cycles could be found in this branch, thus it has to be
		 * searched again next time we spot it.
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
		uint flow = FindCycleFlow(path, at_next_pos);
		if (flow > 0) {
			EliminateCycle(path, at_next_pos, flow);
			return true;
		} else {
			return false;
		}
	}
}

bool MCF1stPass::EliminateCycles()
{
	bool cycles_found = false;
	uint size = this->graph->GetSize();
	PathVector path(size, NULL);
	for (NodeID node = 0; node < size; ++node) {
		/* starting at each node in the graph find all cycles involving this node */
		std::fill(path.begin(), path.end(), (Path *)NULL);
		cycles_found = EliminateCycles(path, node, node) || cycles_found;
	}
	return cycles_found;
}

void MCF1stPass::Run(LinkGraphComponent * graph) {
	MultiCommodityFlow::Run(graph);
	PathVector paths;
	uint size = graph->GetSize();
	uint accuracy = graph->GetSettings().accuracy;
	bool more_loops = true;

	while (more_loops) {
		more_loops = false;

		for (NodeID source = 0; source < size; ++source) {
			/* first saturate the shortest paths */
			Dijkstra<DistanceAnnotation>(source, paths, true);

			for (NodeID dest = 0; dest < size; ++dest) {
				Edge & edge = graph->GetEdge(source, dest);
				if (edge.unsatisfied_demand > 0) {
					Path * path = paths[dest];
					/* generally only allow paths that don't exceed the available capacity.
					 * but if no demand has been assigned yet, make an exception and allow
					 * any valid path *once*.
					 */
					if (path->GetCapacity() > 0) {
						PushFlow(edge, path, accuracy, true);
						if (edge.unsatisfied_demand > 0) {
							/* if a path has been found there is a chance we can find more */
							more_loops = true;
						}
					} else if (edge.unsatisfied_demand == edge.demand && path->GetCapacity() > INT_MIN) {
						PushFlow(edge, path, accuracy, false);
					}
				}
			}
			CleanupPaths(source, paths);
		}
		if (!more_loops) {
			more_loops = EliminateCycles();
		}
		if (accuracy > 1) --accuracy;
	}
}

void MCF2ndPass::Run(LinkGraphComponent * graph) {
	MultiCommodityFlow::Run(graph);
	PathVector paths;
	uint size = graph->GetSize();
	uint accuracy = graph->GetSettings().accuracy;
	bool demand_left = true;
	while (demand_left) {
		demand_left = false;
		for (NodeID source = 0; source < size; ++source) {
			/* Then assign all remaining demands */
			Dijkstra<CapacityAnnotation>(source, paths, false);
			for (NodeID dest = 0; dest < size; ++dest) {
				Edge & edge = graph->GetEdge(source, dest);
				Path * path = paths[dest];
				if (edge.unsatisfied_demand > 0 && path->GetCapacity() > INT_MIN) {
					PushFlow(edge, path, accuracy, false);
					if (edge.unsatisfied_demand > 0) {
						demand_left = true;
					}
				}
			}
			CleanupPaths(source, paths);
		}
		if (accuracy > 1) --accuracy;
	}
}

/**
 * avoid accidentally deleting different paths of the same capacity/distance in a set.
 * When the annotation is the same node IDs are compared, so there are no equal ranges.
 */
template <typename T>
bool greater(T x_anno, T y_anno, const Path * x, const Path * y) {
	if (x_anno > y_anno) {
		return true;
	} else if (x_anno < y_anno) {
		return false;
	} else {
		return x->GetNode() > y->GetNode();
	}
}

bool CapacityAnnotation::comp::operator()(const CapacityAnnotation * x, const CapacityAnnotation * y) const {
	return greater<int>(x->GetAnnotation(), y->GetAnnotation(), x, y);
}

bool DistanceAnnotation::comp::operator()(const DistanceAnnotation * x, const DistanceAnnotation * y) const {
	return x != y && !greater<uint>(x->GetAnnotation(), y->GetAnnotation(), x, y);
}
