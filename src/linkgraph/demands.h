/** @file demands.h Declaration of demand calculating link graph handler. */

#ifndef DEMANDS_H_
#define DEMANDS_H_

#include "linkgraph.h"
#include "demand_settings.h"
#include "../stdafx.h"
#include "../cargo_type.h"
#include "../map_func.h"

class DemandCalculator : public ComponentHandler {
public:
	DemandCalculator() : max_distance(MapSizeX() + MapSizeY()) {}
	virtual void Run(LinkGraphComponent * graph);
	void PrintDemandMatrix(LinkGraphComponent * graph);
	virtual ~DemandCalculator() {}
private:
	uint max_distance;
	void CalcSymmetric(LinkGraphComponent * graph);
	void CalcAntiSymmetric(LinkGraphComponent * graph);
};

#endif /* DEMANDS_H_ */
