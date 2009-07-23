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
	int GetAnnotation() const {return capacity;}
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
		void Dijkstra(NodeID from, PathVector & paths, bool create_new_paths);
	uint PushFlow(Edge & edge, Path * path, uint accuracy, bool positive_cap);
	void SetVia(NodeID source, Path * path);
	void CleanupPaths(NodeID source, PathVector & paths);
	LinkGraphComponent * graph;
};

class MCF1stPass : public MultiCommodityFlow {
private:
	bool EliminateCycles();
	bool EliminateCycles(PathVector & path, NodeID origin, Path * next);
	bool EliminateCycles(PathVector & path, NodeID origin_id, NodeID next_id);
	void EliminateCycle(PathVector & path, Path * cycle_begin, uint flow);
	uint FindCycleFlow(const PathVector & path, const Path * cycle_begin);
public:
	virtual void Run(LinkGraphComponent * graph);
};

class MCF2ndPass : public MultiCommodityFlow {
public:
	virtual void Run(LinkGraphComponent * graph);
};

#endif /* MCF_H_ */
