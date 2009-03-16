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

enum DistributionType {
	symmetric,
	antisymmetric,
	unhandled
};


class DemandCalculator {
public:
	DemandCalculator(CargoID c) : cargo(c) {
		if (maxDistance == 0) maxDistance = MapSizeX() + MapSizeY();
	}
	void calcDemands(Component & graph) {
		calcSymmetric(graph);
		printDemandMatrix(graph);
	}
	void printDemandMatrix(Component & graph);
private:
	CargoID cargo;
	static uint maxDistance;
	void calcSymmetric(Component & graph);
};

#endif /* DEMANDS_H_ */
