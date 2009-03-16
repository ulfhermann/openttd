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

enum DistributionType {
	symmetric,
	antisymmetric,
	unhandled
};


class DemandCalculator : public ComponentHandler {
public:
	DemandCalculator(CargoID c) : cargo(c), max_distance(MapSizeX() + MapSizeY()) {}
	virtual void Run(Component * graph) {
		CalcSymmetric(graph);
		PrintDemandMatrix(graph);
	}
	void PrintDemandMatrix(Component * graph);
	virtual ~DemandCalculator() {}
private:
	CargoID cargo;
	uint max_distance;
	void CalcSymmetric(Component * graph);
	void CalcAntiSymmetric(Component * graph);
};

#endif /* DEMANDS_H_ */
