/*
 * mcf.cpp
 *
 *  Created on: 20.03.2009
 *      Author: alve
 */

#include "mcf.h"
#include <cmath>

float MultiCommodityFlow::epsilon = 0.25;

void MultiCommodityFlow::Run(Component * g) {
	graph = g;
	edges.resize(graph->GetSize(), std::vector<McfEdge>(graph->GetSize()));
	CalcDelta();
	CalcInitialL();
	Prescale();
	Karakostas();
	//Postscale is unnecessary as we are only interested in the flow ratios
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
	CountEdges();
	delta =
		1.0 / (pow((1.0 + k * epsilon), (1.0 - epsilon) / epsilon))
		* pow((1.0 - epsilon) / m, 1.0 /epsilon);
}

void MultiCommodityFlow::CalcInitialL() {
	for (NodeID i = 0; i < graph->GetSize(); ++i) {
		McfEdge * last = NULL;
		for (NodeID j = 0; j < graph->GetSize(); ++j) {
			if (i == j) continue;
			Edge * e = &(graph->GetEdge(i, j));
			McfEdge * mcf = &edges[i][j];
			if (e->capacity > 0) {
				mcf->l = delta / ((float)e->capacity);
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


bool DistanceAnnotation::IsBetter(const DistanceAnnotation * base, float, float dist) const {
	return (base->distance + dist < distance);
}

bool CapacityAnnotation::IsBetter(const CapacityAnnotation * base, float cap, float) const {
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
			float capacity = graph->GetEdge(from, to).capacity;
			float distance = edge->l;
			ANNOTATION * dest = (ANNOTATION *)paths[to];
			if (dest->IsBetter(source, capacity, distance)) {
				annos.erase(dest);
				dest->Fork(source, capacity, distance);
				annos.insert(dest);
			}
		}
	}
}


void MultiCommodityFlow::Prescale() {
	// search for min(C_i/d_i)
	PathVector p;
	uint size = graph->GetSize();
	float min_c_d = std::numeric_limits<float>::max();
	for (NodeID from = 0; from < size; ++from) {
		Dijkstra<CapacityAnnotation>(0, p);
		for (NodeID to = 0; to < size; ++to) {
			Path * path = p[to];
			if (from != to) {
				float c_d = path->GetCapacity() / edges[from][to].d;
				min_c_d = min(c_d, min_c_d);
			}
			delete path;
			p[to] = NULL;
		}
	}
	// scale all demands
	float scale_factor = min_c_d / k;
	for (NodeID from = 0; from < size; ++from) {
		for (NodeID to = 0; to < size; ++to) {
			edges[from][to].d *= scale_factor;
		}
	}
}

void MultiCommodityFlow::CalcD() {
	d_l = 0;
	for (NodeID from = 0; from < graph->GetSize(); ++from) {
		for (McfEdge * edge = GetFirstEdge(from); edge != NULL; edge = edge->next) {
			d_l += edge->l * (float)graph->GetEdge(from, edge->to).capacity;
		}
	}
}

void MultiCommodityFlow::Karakostas() {
	CalcD();
	uint size = graph->GetSize();
	typedef std::set<McfEdge *> EdgeSet;
	EdgeSet unsatisfied_demands;
	PathVector paths;
	while(d_l < 1) {
		for (NodeID source = 0; source < size; ++source) {
			for (NodeID dest = 0; dest < size; ++dest) {
				McfEdge & edge = edges[source][dest];
				edge.dx = edge.d;
				if (edge.dx > 0) {
					unsatisfied_demands.insert(&edge);
				}
			}
			Dijkstra<DistanceAnnotation>(source, paths);
			float c = std::numeric_limits<float>::max();
			for (EdgeSet::iterator i = unsatisfied_demands.begin(); i != unsatisfied_demands.end(); ++i) {
				Path * path = paths[(*i)->to];
				if (path->capacity > 0) {
					c = min(path->capacity, c);
				}
			}
			for (EdgeSet::iterator i = unsatisfied_demands.begin(); i != unsatisfied_demands.end();) {
				McfEdge * edge = *i;
				Path * path = paths[edge->to];
				f_cq = min(edge->dx, c);
				edge->dx -= f_cq;
				path->AddFlow(f_cq);
				if (edge->dx <= 0) {
					unsatisfied_demands.erase(i++);
				} else {
					++i;
				}
			}
		}
	}
}
