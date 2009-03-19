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

void InitializeDemands();

enum DistributionType {
	DT_BEGIN = 0,
	DT_SYMMETRIC = 0,
	DT_ANTISYMMETRIC,
	DT_UNHANDLED,
	DT_NUM
};

/** It needs to be 8bits, because we save and load it as such
 * Define basic enum properties */
template <> struct EnumPropsT<DistributionType> : MakeEnumPropsT<DistributionType, byte, DT_BEGIN, DT_NUM, DT_NUM> {};
typedef TinyEnumT<DistributionType> DistributionTypeByte; // typedefing-enumification of DistributionType

class DemandCalculator : public ComponentHandler {
public:
	DemandCalculator(CargoID c) : cargo(c) {}
	virtual void Run(Component * graph);
	void PrintDemandMatrix(Component * graph);
	virtual ~DemandCalculator() {}
	static DistributionType _distribution_types[NUM_CARGO];
private:
	friend void InitializeDemands();
	CargoID cargo;
	static uint _max_distance;
	void CalcSymmetric(Component * graph);
	void CalcAntiSymmetric(Component * graph);
};

#endif /* DEMANDS_H_ */
