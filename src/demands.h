/*
 * demands.h
 *
 *  Created on: 28.02.2009
 *      Author: alve
 */

#ifndef DEMANDS_H_
#define DEMANDS_H_

#include "stdafx.h"
#include "cargo_type.h"
#include "map_func.h"
#include "linkgraph.h"
#include "demand_settings.h"

class DemandCalculator : public ComponentHandler {
public:
	DemandCalculator() : max_distance(MapSizeX() + MapSizeY()) {}
	virtual void Run(Component * graph);
	void PrintDemandMatrix(Component * graph);
	virtual ~DemandCalculator() {}
private:
	uint max_distance;
	void CalcSymmetric(Component * graph);
	void CalcAntiSymmetric(Component * graph);
};

#endif /* DEMANDS_H_ */
