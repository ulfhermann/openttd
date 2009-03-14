/*
 * demands.h
 *
 *  Created on: 28.02.2009
 *      Author: alve
 */

#ifndef DEMANDS_H_
#define DEMANDS_H_

#include "cargodist.h"
#include "cargo_type.h"
#include "map_func.h"

enum DistributionType {
	symmetric,
	antisymmetric,
	unhandled
};


class DemandCalculator {
public:
	DemandCalculator(CargoID pCargo) : cargo(pCargo) {
		if (maxDistance == 0) maxDistance = MapSizeX() + MapSizeY();
	}
	void calcDemands(CargoDistGraph & graph) {
		calcSymmetric(graph);
		printDemandMatrix(graph);
	}
	void printDemandMatrix(CargoDistGraph & graph);
private:
	CargoID cargo;
	static uint maxDistance;
	void calcSymmetric(CargoDistGraph & graph);
};

#endif /* DEMANDS_H_ */
