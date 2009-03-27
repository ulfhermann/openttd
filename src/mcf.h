/*
 * mcf.h
 *
 *  Created on: 20.03.2009
 *      Author: alve
 */

#ifndef MCF_H_
#define MCF_H_

#include "linkgraph.h"
#include <limits>
#include <vector>

/*
 * PathMapping will look like:
 *
 * class PathMapping {
 * 	Node * origin;
 *  Node * this_node;
 * 	map<Node *, float> flows;
 * }
 *
 * for each node there will be as many mappings as there are origins of flow passing this_node.
 * Translated to stations we will have another member of GoodsEntry like
 * map<StationID, map<StationID, float> > with the first StationID being origin and the second "via"
 *
 * Can this be computed from the PathList the MCF algo generates?
 * Yes:
 * - traverse all nodes and for each node collect all adjacent McfEdges
 * - Search the McfEdges for paths from the same origin
 * - build a flow mapping for this origin from the paths
 * - traverse all McfEdges and delete them. Deleting an edge deletes any paths associated with it
 * - deleting a path removes it from all McfEdges associated with it
 */


class McfEdge {
public:
	McfEdge() : l(0), d(0), dx(0), next(NULL), to(UINT_MAX) {}
	float l;
	float d;
	float dx;
	McfEdge * next;
	NodeID to;
};

class DistanceAnnotation : public Path {
public:
	DistanceAnnotation(NodeID n, bool source = false) : Path(n)
		{distance = source ? 0 : std::numeric_limits<float>::max();}
	bool IsBetter(const DistanceAnnotation * base, float cap, float dist) const;
	float GetAnnotation() const {return distance;}
	struct comp {
		bool operator()(const DistanceAnnotation * x, const DistanceAnnotation * y) const
		{ return x->GetAnnotation() < y->GetAnnotation(); }
	};
};

class CapacityAnnotation : public Path {
public:
	CapacityAnnotation(NodeID n, bool source = false) : Path(n)
		{capacity = source ? std::numeric_limits<float>::max() : 0;}
	bool IsBetter(const CapacityAnnotation * base, float cap, float dist) const;
	float GetAnnotation() const {return capacity;}
	struct comp {
		bool operator()(const CapacityAnnotation * x, const CapacityAnnotation * y) const
		{ return x->GetAnnotation() > y->GetAnnotation(); }
	};
};

typedef std::vector< std::vector <McfEdge> > McfGraph;
typedef std::vector<Path *> PathVector;

class MultiCommodityFlow : public ComponentHandler {
public:
	virtual void Run(Component * graph);
	MultiCommodityFlow() : graph(NULL), delta(0), k(0), m(0) {}
	virtual ~MultiCommodityFlow() {}
	static float epsilon;
private:
	McfEdge * GetFirstEdge(NodeID n) {return edges[n][n].next;}
	void CalcInitialL();
	void Prescale(); // scale the demands so that beta >= 1
	void CalcDelta();
	void CalcD();
	void CountEdges();
	template<class ANNOTATION>
		void Dijkstra(NodeID from, PathVector & paths);

	void Karakostas();
	McfGraph edges;
	Component * graph;
	float delta;
	float k;
	float m;
	float d_l; // actually D(l), but coding style overruled that
};

#endif /* MCF_H_ */
