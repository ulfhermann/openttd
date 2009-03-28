/*
 * mcf.h
 *
 *  Created on: 20.03.2009
 *      Author: alve
 */

#ifndef MCF_H_
#define MCF_H_

#include "linkgraph.h"
#include "settings_type.h"
#include <vector>

class McfEdge {
public:
	McfEdge() : l(0), d(0), dx(0), f_cq(0), next(NULL), to(UINT_MAX) {}
	float l;
	float d;
	float dx;
	float f_cq;
	McfEdge * next;
	NodeID to;
};

class DistanceAnnotation : public Path {
public:
	DistanceAnnotation(NodeID n, bool source = false) : Path(n, source) {}
	bool IsBetter(const DistanceAnnotation * base, float cap, float dist) const;
	float GetAnnotation() const {return distance;}
	struct comp {
		bool operator()(const DistanceAnnotation * x, const DistanceAnnotation * y) const;
	};
};

class CapacityAnnotation : public Path {
public:
	CapacityAnnotation(NodeID n, bool source = false) : Path(n, source) {}
	bool IsBetter(const CapacityAnnotation * base, float cap, float dist) const;
	float GetAnnotation() const {return capacity;}
	struct comp {
		bool operator()(const CapacityAnnotation * x, const CapacityAnnotation * y) const;
	};
};

typedef std::vector< std::vector <McfEdge> > McfGraph;
typedef std::vector<Path *> PathVector;

class MultiCommodityFlow : public ComponentHandler {
public:
	virtual void Run(Component * graph);
	MultiCommodityFlow();
	virtual ~MultiCommodityFlow() {}
private:
	McfEdge * GetFirstEdge(NodeID n) {return edges[n][n].next;}
	void CalcInitialL();
	void Prescale(); // scale the demands so that beta >= 1
	void CalcDelta();
	void CalcD();
	void CountEdges();
	template<class ANNOTATION>
		void Dijkstra(NodeID from, PathVector & paths);
	void IncreaseL(Path * path, float f_cq);
	void Karakostas();
	void CleanupPaths(PathVector & paths);
	float epsilon;
	McfGraph edges;
	Component * graph;
	float delta;
	float k;
	float m;
	float d_l; // actually D(l), but coding style overruled that
};

#endif /* MCF_H_ */
