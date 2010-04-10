/** @file demands.h Declaration of demand calculating link graph handler. */

#ifndef DEMANDS_H_
#define DEMANDS_H_

#include "linkgraph.h"
#include "../stdafx.h"
#include "../cargo_type.h"
#include "../map_func.h"

/**
 * Calculate the demands. This class has a state, but is recreated for each
 * call to of DemandHandler::Run.
 */
class DemandCalculator {
public:
	DemandCalculator(LinkGraphComponent *graph);

private:
	int32 max_distance; ///< maximum distance possible on the map
	int32 mod_size;     ///< size modifier or "symmetricity"
	int32 mod_dist;     ///< distance modifier, determines how much demands decrease with distance
	int32 accuracy;     ///< accuracy of the calculation

	void CalcDemand(LinkGraphComponent *graph);
};

/**
 * Stateless, thread safe demand hander. Doesn't do anything but call DemandCalculator.
 */
class DemandHandler : public ComponentHandler {
public:

	/**
	 * Call the demand calculator on the given component-
	 * @param graph the component to calculate the demands for.
	 */
	virtual void Run(LinkGraphComponent *graph)
		{DemandCalculator c(graph);}

	/**
	 * Virtual destructor has to be defined because of virtual Run().
	 */
	virtual ~DemandHandler() {}
};

#endif /* DEMANDS_H_ */
