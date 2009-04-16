/** @file mcf.h Declaration of Multi-Commodity-Flow solver */

#ifndef MCF_H_
#define MCF_H_

#include "linkgraph.h"
#include <vector>

class DistanceAnnotation : public Path {
public:
	DistanceAnnotation(NodeID n, bool source = false) : Path(n, source) {}
	bool IsBetter(const DistanceAnnotation * base, uint cap, uint dist) const;
	uint GetAnnotation() const {return distance;}
	struct comp {
		bool operator()(const DistanceAnnotation * x, const DistanceAnnotation * y) const;
	};
};

class CapacityAnnotation : public Path {
public:
	CapacityAnnotation(NodeID n, bool source = false) : Path(n, source) {}
	bool IsBetter(const CapacityAnnotation * base, uint cap, uint dist) const;
	uint GetAnnotation() const {return capacity;}
	struct comp {
		bool operator()(const CapacityAnnotation * x, const CapacityAnnotation * y) const;
	};
};


typedef std::vector<Path *> PathVector;

class MultiCommodityFlow : public ComponentHandler {
public:
	virtual void Run(LinkGraphComponent * graph);
	MultiCommodityFlow();
	virtual ~MultiCommodityFlow() {}
private:

	template<class ANNOTATION>
		void Dijkstra(NodeID from, PathVector & paths);
	void SimpleSolver();
	void CleanupPaths(PathVector & paths);

	LinkGraphComponent * graph;
};

#endif /* MCF_H_ */
