/*
 * mcf.cpp
 *
 *  Created on: 20.03.2009
 *      Author: alve
 */

#include "mcf.h"
#include "debug.h"

MultiCommodityFlow::MultiCommodityFlow() :
	graph(NULL)
{}

void MultiCommodityFlow::Run(LinkGraphComponent * g) {
	assert(g->GetSettings().mcf_accuracy > 1);
	graph = g;
	SimpleSolver();
}

bool DistanceAnnotation::IsBetter(const DistanceAnnotation * base, uint cap, uint dist) const {
	return (cap > 0 && base->distance + dist < distance);
}

bool CapacityAnnotation::IsBetter(const CapacityAnnotation * base, uint cap, uint dist) const {
	return (dist < UINT_MAX && min(base->capacity, cap) > capacity);
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
			assert(edge.capacity >= edge.flow);
			uint capacity = edge.capacity - edge.flow;
			uint distance = edge.distance;
			ANNOTATION * dest = static_cast<ANNOTATION *>(paths[to]);
			if (dest->IsBetter(source, capacity, distance)) {
				annos.erase(dest);
				dest->Fork(source, capacity, distance);
				annos.insert(dest);
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
	uint demand_routed = 0;
	uint old_routed = 0;
	uint size = graph->GetSize();
	uint accuracy = graph->GetSettings().mcf_accuracy;
	do {
		old_routed = demand_routed;
		for (NodeID source = 0; source < size; ++source) {

			Dijkstra<CapacityAnnotation>(source, paths);

			for (NodeID dest = 0; dest < size; ++dest) {
				Edge & edge = graph->GetEdge(source, dest);
				if (edge.unsatisfied_demand > 0) {
					Path * path = paths[dest];
					uint flow = edge.unsatisfied_demand / accuracy;
					if (flow == 0) flow = 1;
					flow = path->AddFlow(flow, graph);
					edge.unsatisfied_demand -= flow;
					demand_routed += flow;
				}
			}

			CleanupPaths(paths);
		}
		if (accuracy > 1) --accuracy;
	} while (demand_routed > old_routed);
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
