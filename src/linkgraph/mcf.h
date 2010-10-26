/** @file mcf.h Declaration of Multi-Commodity-Flow solver */

#ifndef MCF_H_
#define MCF_H_

#include "linkgraph.h"
#include <vector>

/**
 * Distance-based annotation for use in the Dijkstra algorithm. This is close
 * to the original meaning of "annotation" in this context. Paths are rated
 * according to the sum of distances of their edges.
 */
class DistanceAnnotation : public Path {
public:

	DistanceAnnotation(NodeID n, bool source = false) : Path(n, source) {}

	bool IsBetter(const DistanceAnnotation *base, int cap, uint dist) const;

	struct comp {
		bool operator()(const DistanceAnnotation *x, const DistanceAnnotation *y) const;
	};
};

typedef std::vector<Path *> PathVector;

/**
 * MCF calculation. Saturates shortest paths first, creates new paths if needed,
 * eliminates cycles. This calculation is of exponential complexity in the
 * number of nodes but the constant factors are sufficiently small to make it
 * usable for most real-life link graph components. You can deal with
 * performance problems that might occur here in multiple ways:
 * - The overall accuracy is used here to determine how much flow is assigned
 *   in each loop. The lower the accuracy, the more flow is assigned, the less
 *   loops it takes to assign all flow.
 * - The short_path_saturation setting determines when this pass stops. The
 *   lower you set it, the less flow will be assigned in this pass, the less
 *   time it will take.
 * - You can increase the recalculation interval to allow for longer running
 *   times without creating lags.
 */
class MultiCommodityFlow {
protected:
	void Dijkstra(NodeID from, PathVector &paths);
	uint PushFlow(Edge &edge, Path *path, uint accuracy, bool positive_cap);
	void CleanupPaths(NodeID source, PathVector &paths);
	LinkGraphComponent *graph; ///< the component we're working with

private:
	bool EliminateCycles();
	bool EliminateCycles(PathVector &path, NodeID origin_id, NodeID next_id);
	void EliminateCycle(PathVector &path, Path *cycle_begin, uint flow);
	uint FindCycleFlow(const PathVector &path, const Path *cycle_begin);

public:
	MultiCommodityFlow(LinkGraphComponent *graph);
};

/**
 * Link graph handler for MCF. Creates MultiCommodityFlow instance according to
 * the template parameter.
 */
class MCFHandler : public ComponentHandler {
public:

	/**
	 * run the calculation.
	 * @param graph the component to be calculated.
	 */
	virtual void Run(LinkGraphComponent *graph) {MultiCommodityFlow mcf(graph);}

	virtual ~MCFHandler() {}
};

#endif /* MCF_H_ */
