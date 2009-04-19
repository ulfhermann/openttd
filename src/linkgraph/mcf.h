/** @file mcf.h Declaration of Multi-Commodity-Flow solver */

#ifndef MCF_H_
#define MCF_H_

#include "linkgraph.h"
#include <vector>

class DistanceAnnotation : public Path {
public:
	DistanceAnnotation(NodeID n, bool source = false) : Path(n, source) {}
	bool IsBetter(const DistanceAnnotation * base, int cap, uint dist) const;
	uint GetAnnotation() const {return distance;}
	struct comp {
		bool operator()(const DistanceAnnotation * x, const DistanceAnnotation * y) const;
	};
};

class CapacityAnnotation : public Path {
public:
	CapacityAnnotation(NodeID n, bool source = false) : Path(n, source) {}
	bool IsBetter(const CapacityAnnotation * base, int cap, uint dist) const;
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
protected:
	template<class ANNOTATION>
		void Dijkstra(NodeID from, PathVector & paths, uint max_hops, bool create_new_paths);
	void PushFlow(Edge & edge, Path * path, uint accuracy, bool positive_cap);
	void CleanupPaths(PathVector & paths);
	LinkGraphComponent * graph;
};

class MCF1stPass : public MultiCommodityFlow {
public:
	virtual void Run(LinkGraphComponent * graph);
};

class MCF2ndPass : public MultiCommodityFlow {
public:
	virtual void Run(LinkGraphComponent * graph);
};

#endif /* MCF_H_ */
