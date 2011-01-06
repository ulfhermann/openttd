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

	bool IsBetter(const DistanceAnnotation *base, uint cap, int free_cap, uint dist) const;

	/**
	 * Return the actual value of the annotation, in this case the distance.
	 * @return Distance.
	 */
	FORCEINLINE  uint GetAnnotation() const {return this->distance;}

	struct comp {
		bool operator()(const DistanceAnnotation *x, const DistanceAnnotation *y) const;
	};
};

/**
 * Capacity-based annotation for use in the Dijkstra algorithm. This annotation
 * rates paths according to the maximum capacity of their edges. The Dijkstra
 * algorithm still gives meaningful results like this as the capacity of a path
 * can only decrease or stay the same if you add more edges.
 */
class CapacityAnnotation : public Path {
public:

	CapacityAnnotation(NodeID n, bool source = false) : Path(n, source) {}

	bool IsBetter(const CapacityAnnotation *base, uint cap, int free_cap, uint dist) const;

	/**
	 * Return the actual value of the annotation, in this case the capacity.
	 * @return Capacity.
	 */
	FORCEINLINE int GetAnnotation() const {return this->GetCapacityRatio();}

	struct comp {
		bool operator()(const CapacityAnnotation *x, const CapacityAnnotation *y) const;
	};
};


typedef std::vector<Path *> PathVector;

/**
 * Multi-commodity flow calculating base class.
 */
class MultiCommodityFlow {
protected:
	MultiCommodityFlow(LinkGraphComponent *graph) : graph(graph) {}

	template<class ANNOTATION> void Dijkstra(NodeID from, PathVector &paths, bool create_new_paths);

	uint PushFlow(Edge &edge, Path *path, uint accuracy, bool positive_cap);

	void CleanupPaths(NodeID source, PathVector &paths);

	LinkGraphComponent *graph; ///< Component we're working with.
};

/**
 * First pass of the MCF calculation. Saturates shortest paths first, creates
 * new paths if needed, eliminates cycles. This calculation is of exponential
 * complexity in the number of nodes but the constant factors are sufficiently
 * small to make it usable for most real-life link graph components. You can
 * deal with performance problems that might occur here in multiple ways:
 * - The overall accuracy is used here to determine how much flow is assigned
 *   in each loop. The lower the accuracy, the more flow is assigned, the less
 *   loops it takes to assign all flow.
 * - The short_path_saturation setting determines when this pass stops. The
 *   lower you set it, the less flow will be assigned in this pass, the less
 *   time it will take.
 * - You can increase the recalculation interval to allow for longer running
 *   times without creating lags.
 */
class MCF1stPass : public MultiCommodityFlow {
private:
	bool EliminateCycles();
	bool EliminateCycles(PathVector &path, NodeID origin_id, NodeID next_id);
	void EliminateCycle(PathVector &path, Path *cycle_begin, uint flow);
	uint FindCycleFlow(const PathVector &path, const Path *cycle_begin);
public:
	MCF1stPass(LinkGraphComponent *graph);
};

/**
 * Second pass of the MCF calculation. Saturates paths with most capacity left
 * first and doesn't create any paths along edges that haven't been visited in
 * the first pass. This is why it doesn't have to do any cycle detection and
 * elimination. As cycle detection is the most intense problem in the first
 * pass this pass is cheaper. The accuracy is used here, too.
 */
class MCF2ndPass : public MultiCommodityFlow {
public:
	MCF2ndPass(LinkGraphComponent *graph);
};

/**
 * Link graph handler for MCF. Creates MultiCommodityFlow instance according to
 * the template parameter.
 */
template<class Tpass>
class MCFHandler : public ComponentHandler {
public:

	/**
	 * Run the calculation.
	 * @param graph Component to be calculated.
	 */
	virtual void Run(LinkGraphComponent *graph) {Tpass pass(graph);}

	virtual ~MCFHandler() {}
};

#endif /* MCF_H_ */
