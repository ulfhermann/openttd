/** @file mcf.cpp Definition of Multi-Commodity-Flow solver */

#include "mcf.h"

MultiCommodityFlow::MultiCommodityFlow() :
	graph(NULL)
{}

void MultiCommodityFlow::Run(LinkGraphComponent * g) {
	assert(g->GetSettings().mcf_accuracy > 1);
	graph = g;
	SimpleSolver();
}

bool DistanceAnnotation::IsBetter(const DistanceAnnotation * base, int cap, uint dist) const {
	if (cap > 0 && base->capacity > 0) {
		if (capacity <= 0) {
			return true; // if the other path has capacity left and this one hasn't, the other one's better
		} else {
			return base->distance + dist < distance;
		}
	} else {
		return false; // if the other path doesn't have capacity left, this one is always better
	}
}

bool CapacityAnnotation::IsBetter(const CapacityAnnotation * base, int cap, uint dist) const {
	int min_cap = min(base->capacity, cap);
	if (min_cap == capacity) { // if the capacities are the same, choose the shorter path
		return (base->distance + dist < distance);
	} else {
		return min_cap > capacity;
	}
}


template<class ANNOTATION>
void MultiCommodityFlow::Dijkstra(NodeID from, PathVector & paths) {
	typedef std::set<ANNOTATION *, typename ANNOTATION::comp> AnnoSet;
	uint size = graph->GetSize();
	AnnoSet annos;
	paths.resize(size, NULL);
	for (NodeID node = 0; node < size; ++node) {
		ANNOTATION * anno = new ANNOTATION(node, node == from);
		annos.insert(anno);
		paths[node] = anno;
	}

	while(!annos.empty()) {
		typename AnnoSet::iterator i = annos.begin();
		ANNOTATION * source = *i;
		NodeID from = source->GetNode();
		annos.erase(i);
		NodeID to = graph->GetFirstEdge(from);
		while (to != Node::INVALID) {
			Edge & edge = graph->GetEdge(from, to);
			if (edge.capacity > 0) {
				int capacity = edge.capacity - edge.flow;
				uint distance = edge.distance;
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


void MultiCommodityFlow::SimpleSolver() {
	PathVector paths;
	uint size = graph->GetSize();
	uint accuracy = graph->GetSettings().mcf_accuracy;
	bool positive_cap = true;
	bool demand_routed = true;
	while (positive_cap || demand_routed) {
		demand_routed = false;
		for (NodeID source = 0; source < size; ++source) {
			if (positive_cap) {
				/* first saturate the shortest paths */
				Dijkstra<DistanceAnnotation>(source, paths);
			} else {
				/* then overload all paths equally with the remaining demand */
				Dijkstra<CapacityAnnotation>(source, paths);
			}

			for (NodeID dest = 0; dest < size; ++dest) {
				Edge & edge = graph->GetEdge(source, dest);
				if (edge.unsatisfied_demand > 0) {
					Path * path = paths[dest];
					uint flow = edge.unsatisfied_demand / accuracy;
					if (flow == 0) flow = 1;
					flow = path->AddFlow(flow, graph, positive_cap);
					if (flow > 0) {
						demand_routed = true;
						edge.unsatisfied_demand -= flow;
					}
				}
			}

			CleanupPaths(paths);
		}
		if (accuracy > 1) --accuracy;
		if (positive_cap && !demand_routed) {
			positive_cap = false;
		}
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
