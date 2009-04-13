/*
 * mcf.cpp
 *
 *  Created on: 20.03.2009
 *      Author: alve
 */

#include "mcf.h"
#include "debug.h"
#include <limits>
#include <cmath>

MultiCommodityFlow::MultiCommodityFlow() :
	epsilon(0.5), graph(NULL), delta(0), k(0), m(0)
{}

void MultiCommodityFlow::Run(LinkGraphComponent * g) {
	assert(g->GetSettings().mcf_accuracy > 0);
	epsilon = 1.0 / (Number)g->GetSettings().mcf_accuracy;
	graph = g;
	CountEdges();
	if (k == 0) return;
	edges.resize(graph->GetSize(), std::vector<McfEdge>(graph->GetSize()));
	CalcDelta();
	CalcInitialL();
	if (Prescale()) {
		/* paths were found */
		Karakostas();
	}
	/* Postscale is unnecessary as we are only interested in the flow ratios */
}

void MultiCommodityFlow::CountEdges() {
	m = 0;
	k = 0;
	for (NodeID i = 0; i < graph->GetSize(); ++i) {
		for (NodeID j = 0; j < graph->GetSize(); ++j) {
			if (i == j) continue;
			Edge & e = graph->GetEdge(i, j);
			if (e.capacity > 0) m++;
			if (e.demand > 0) k++;
		}
	}
}

void MultiCommodityFlow::CalcDelta() {
	delta =
		1.0 / (pow((1.0 + k * epsilon), (1.0 - epsilon) / epsilon))
		* pow((1.0 - epsilon) / m, 1.0 /epsilon);
}

void MultiCommodityFlow::HandleInstability() {
	Number inverse_epsilon = 1.0 / epsilon - 1.0;
	if (inverse_epsilon < 1.0) {
		DEBUG(misc, 0, "explosion of numeric instability, giving up");
		throw LinkGraphJob::Exception();
	} else {
		epsilon = 1.0 / inverse_epsilon;
		DEBUG(misc, 0, "numeric instability detected, increasing epsilon: %f", epsilon);
	}
}

void MultiCommodityFlow::CalcInitialL() {
	for (NodeID i = 0; i < graph->GetSize(); ++i) {
		McfEdge * last = NULL;
		for (NodeID j = 0; j < graph->GetSize(); ++j) {
			if (i == j) continue;
			Edge * e = &(graph->GetEdge(i, j));
			McfEdge * mcf = &edges[i][j];
			if (e->capacity > 0) {
				mcf->l = delta / ((Number)e->capacity);
				if (!mcf->l > 0) {
					HandleInstability();
					CalcDelta();
					CalcInitialL();
					return;
				}
				if (last != NULL) {
					last->next = mcf;
				} else {
					edges[i][i].next = mcf;
				}
				last = mcf;
			}
			mcf->d = e->demand;
			mcf->to = j;
		}
	}
}


bool DistanceAnnotation::IsBetter(const DistanceAnnotation * base, Number, Number dist) const {
	return (base->distance + dist < distance);
}

bool CapacityAnnotation::IsBetter(const CapacityAnnotation * base, Number cap, Number) const {
	return (min(base->capacity, cap) > capacity);
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
		for (McfEdge * edge = GetFirstEdge(from); edge != NULL; edge = edge->next) {
			NodeID to = edge->to;
			Number capacity = graph->GetEdge(from, to).capacity;
			Number distance = edge->l;
			ANNOTATION * dest = static_cast<ANNOTATION *>(paths[to]);
			if (dest->IsBetter(source, capacity, distance)) {
				annos.erase(dest);
				dest->Fork(source, capacity, distance);
				annos.insert(dest);
			}
		}
	}
}


bool MultiCommodityFlow::Prescale() {
	// search for min(C_i/d_i)
	PathVector p;
	uint size = graph->GetSize();
	Number max_num = std::numeric_limits<Number>::max();
	Number min_c_d = max_num;
	for (NodeID from = 0; from < size; ++from) {
		Dijkstra<CapacityAnnotation>(from, p);
		for (NodeID to = 0; to < size; ++to) {
			Path * path = p[to];
			if (from != to) {
				Number cap = path->GetCapacity();
				if (cap > 0) {
					Number c_d = cap / edges[from][to].d;
					min_c_d = min(c_d, min_c_d);
				}
			}
			delete path;
			p[to] = NULL;
		}
	}
	if (!(min_c_d < max_num)) {
		DEBUG(misc, 3, "no paths found. giving up");
		return false;
	}
	// scale all demands
	Number scale_factor = min_c_d / k;
	if (scale_factor > 1) {
		DEBUG(misc, 3, "very high scale factor: %f", scale_factor);
	}
	for (NodeID from = 0; from < size; ++from) {
		for (NodeID to = 0; to < size; ++to) {
			edges[from][to].d *= scale_factor;
		}
	}
	return true;
}

void MultiCommodityFlow::CalcD() {
	d_l = 0;
	for (NodeID from = 0; from < graph->GetSize(); ++from) {
		for (McfEdge * edge = GetFirstEdge(from); edge != NULL; edge = edge->next) {
			d_l += edge->l * (Number)graph->GetEdge(from, edge->to).capacity;
		}
	}
}

void MultiCommodityFlow::IncreaseL(Path * path, Number sum_f_cq) {
	Path * parent = path->GetParent();
	while (parent != NULL) {
		NodeID to = path->GetNode();
		NodeID from = parent->GetNode();
		McfEdge & mcf = edges[from][to];
		Edge & edge = graph->GetEdge(from, to);
		Number capacity = edge.capacity;
		Number difference = mcf.l * epsilon * sum_f_cq / capacity;
		assert(!(difference < 0));
		mcf.l += difference;
		assert(mcf.l > 0);
		d_l += difference * capacity;
		path = parent;
		parent = path->GetParent();
	}
}

void MultiCommodityFlow::CleanupPaths(PathVector & paths) {
	for(PathVector::iterator i = paths.begin(); i != paths.end(); ++i) {
		Path * path = *i;
		while (path != NULL && path->GetFlow() <= 0) {
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

void MultiCommodityFlow::Karakostas() {
	CalcD();
	uint size = graph->GetSize();
	typedef std::set<McfEdge *> EdgeSet;
	EdgeSet unsatisfied_demands;
	PathVector paths;
	Number last_d_l = 1;
	uint loops = 0; // TODO: when #loops surpasses a certain threshold double all d's to speed things up
	while(d_l < 1 && d_l < last_d_l) {
		last_d_l = d_l;
		for (NodeID source = 0; source < size && d_l < 1; ++source) {
			for (NodeID dest = 0; dest < size; ++dest) {
				McfEdge & edge = edges[source][dest];
				edge.dx = edge.d;
				if (edge.dx > 0) {
					unsatisfied_demands.insert(&edge);
				}
			}
			Dijkstra<DistanceAnnotation>(source, paths);
			Number c = std::numeric_limits<Number>::max();
			for (EdgeSet::iterator i = unsatisfied_demands.begin(); i != unsatisfied_demands.end(); ++i) {
				Path * path = paths[(*i)->to];
				if (path->GetCapacity() > 0) {
					c = min(path->GetCapacity(), c);
				}
			}
			while(!unsatisfied_demands.empty() && d_l < 1) {
				for (EdgeSet::iterator i = unsatisfied_demands.begin(); i != unsatisfied_demands.end();) {
					McfEdge * edge = *i;
					Path * path = paths[edge->to];
					Number f_cq = min(edge->dx, c);
					edge->dx -= f_cq;
					IncreaseL(path, f_cq);
					path->AddFlow(f_cq, graph);
					if (edge->dx <= 0) {
						unsatisfied_demands.erase(i++);
					} else {
						++i;
					}
				}
			}
			CleanupPaths(paths);
			loops++;
		}
	}
	if (loops < size) {
		DEBUG(misc, 3, "fewer loops than origin nodes: %d/%d", loops, size);
	}
}

void MultiCommodityFlow::SimpleSolver() {
	typedef std::list<std::pair<Edge *, StationID> > EdgeList;

	EdgeList unsatisfied_demands;
	PathVector paths;
	uint demand_routed = 0;
	uint old_routed = 0;
	uint size = graph->GetSize();
	uint accuracy = graph->GetSettings().mcf_accuracy;
	do {
		old_routed = demand_routed;
		for (NodeID source = 0; source < size; ++source) {
			for (NodeID dest = 0; dest < size; ++dest) {
				Edge & edge = graph->GetEdge(source, dest);
				if (edge.demand - edge.flow > 0) {
					unsatisfied_demands.push_back(std::make_pair(&edge, dest));
				}
			}

			Dijkstra<DistanceAnnotation>(source, paths);
			uint max_flow = UINT_MAX;
			for (EdgeList::iterator i = unsatisfied_demands.begin(); i != unsatisfied_demands.end(); ++i) {
				Path * path = paths[i->second];
				if (path->GetCapacity() > 0) {
					max_flow = min(path->GetCapacity(), max_flow);
				}
			}

			max_flow /= accuracy;
			if (max_flow == 0) max_flow = 1;
			if (accuracy > 1) --accuracy;

			for (EdgeList::iterator i = unsatisfied_demands.begin(); i != unsatisfied_demands.end();) {
				Edge * edge = i->first;
				Path * path = paths[i->second];
				uint flow = min(edge->demand - edge->flow, max_flow);
				edge->flow += flow;
				demand_routed += flow;
				path->AddFlow(flow, graph);
				unsatisfied_demands.erase(i++);
			}

			CleanupPaths(paths);
		}
	} while (demand_routed > old_routed);
}

/**
 * avoid accidentally deleting different paths of the same capacity/distance in a set.
 * When the annotation is the same the pointers themselves are compared, so there are no equal ranges.
 * (The problem might have been something else ... but this isn't expensive I guess)
 */
bool greater(Number x_anno, Number y_anno, const Path * x, const Path * y) {
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
