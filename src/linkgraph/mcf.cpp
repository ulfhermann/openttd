/** @file mcf.cpp Definition of Multi-Commodity-Flow solver */

#include "mcf.h"
#include "../core/math_func.hpp"

MultiCommodityFlow::MultiCommodityFlow() :
	graph(NULL)
{}

void MultiCommodityFlow::Run(LinkGraphComponent * g) {
	assert(g->GetSettings().mcf_accuracy >= 1);
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
			return false; // if the other path doesn't have capacity left, but this one has, this one is always better
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
			if (create_new_paths || graph->GetNode(from).flows[source_node][to] > 0) {
				int capacity = edge.capacity - edge.flow;
				uint distance = edge.distance;
				ANNOTATION * dest = static_cast<ANNOTATION *>(paths[to]);
				if (dest->IsBetter(source, capacity, distance)) {
					bool is_circle = false;
					if (create_new_paths) {
						for (Path * path = source; path->GetParent() != NULL; path = path->GetParent()) {
							ViaSet &forbidden = graph->GetEdge(source_node, path->GetNode()).paths_via;
							if (forbidden.find(dest->GetNode()) != forbidden.end()) {
								is_circle = true;
								break;
							}
						}
					}
					if (!is_circle) {
						annos.erase(dest);
						dest->Fork(source, capacity, distance);
						annos.insert(dest);
					}
				}
			}
			to = edge.next_edge;
		}
	}
}



void MultiCommodityFlow::CleanupPaths(PathVector & paths) {
	for(PathVector::iterator i = paths.begin(); i != paths.end(); ++i) {
		Path * path = *i;
		while (path != NULL && path->GetFlow() == 0) {
			Path * parent = path->GetParent();
			path->UnFork();
			if (path->GetNumChildren() == 0) {
				NodeID node = path->GetNode();
				delete path;
				paths[node] = NULL;
			}
			path = parent;
		}
	}
	paths.clear();
}

uint MultiCommodityFlow::PushFlow(Edge &edge, Path * path, uint accuracy, bool positive_cap) {
	uint flow = edge.unsatisfied_demand / accuracy;
	if (flow == 0) flow = 1;
	flow = path->AddFlow(flow, graph, positive_cap);
	edge.unsatisfied_demand -= flow;
	return flow;
}

void MultiCommodityFlow::SetVia(NodeID source, Path * path) {
	Path * parent = path->GetParent();
	if (parent == NULL) {
		return;
	}
	SetVia(source, parent);
	ViaSet &predecessors = graph->GetEdge(source, path->GetNode()).paths_via;
	while(parent != NULL) {
		predecessors.insert(parent->GetNode());
		path = parent;
		parent = path->GetParent();
	}
}


void MCF1stPass::Run(LinkGraphComponent * graph) {
	MultiCommodityFlow::Run(graph);
	PathVector paths;
	uint size = graph->GetSize();
	uint accuracy = graph->GetSettings().mcf_accuracy;
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
						SetVia(source, path);
						if (edge.unsatisfied_demand > 0) {
							/* if a path has been found there is a chance we can find more */
							more_loops = true;
						}
					} else if (edge.unsatisfied_demand == edge.demand && path->GetCapacity() > INT_MIN) {
						PushFlow(edge, path, accuracy, false);
						SetVia(source, path);
					}
				}
			}
			CleanupPaths(paths);
		}

		if (accuracy > 1) --accuracy;
	}
}

void MCF2ndPass::Run(LinkGraphComponent * graph) {
	MultiCommodityFlow::Run(graph);
	PathVector paths;
	uint size = graph->GetSize();
	uint accuracy = graph->GetSettings().mcf_accuracy;
	bool demand_left = true;
	while (demand_left) {
		demand_left = false;
		for (NodeID source = 0; source < size; ++source) {
			/* Then assign all remaining demands */
			Dijkstra<CapacityAnnotation>(source, paths, false);
			for (NodeID dest = 0; dest < size; ++dest) {
				Edge & edge = graph->GetEdge(source, dest);
				if (edge.unsatisfied_demand > 0) {
					Path * path = paths[dest];
					PushFlow(edge, path, accuracy, false);
					if (edge.unsatisfied_demand > 0) {
						demand_left = true;
					}
				}
			}
			CleanupPaths(paths);
		}
		if (accuracy > 1) --accuracy;
	}
}

/**
 * avoid accidentally deleting different paths of the same capacity/distance in a set.
 * When the annotation is the same the pointers themselves are compared, so there are no equal ranges.
 * (The problem might have been something else ... but this isn't expensive I guess)
 */
bool greater(uint x_anno, uint y_anno, const Path * x, const Path * y) {
	if (x_anno > y_anno) {
		return true;
	} else if (x_anno < y_anno) {
		return false;
	} else {
		return x > y;
	}
}

bool CapacityAnnotation::comp::operator()(const CapacityAnnotation * x, const CapacityAnnotation * y) const {
	return greater(x->GetAnnotation(), y->GetAnnotation(), x, y);
}

bool DistanceAnnotation::comp::operator()(const DistanceAnnotation * x, const DistanceAnnotation * y) const {
	return x != y && !greater(x->GetAnnotation(), y->GetAnnotation(), x, y);
}
