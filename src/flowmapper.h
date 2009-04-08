/*
 * flowmapper.h
 *
 *  Created on: 31.03.2009
 *      Author: alve
 */

#ifndef FLOWMAPPER_H_
#define FLOWMAPPER_H_

#include "linkgraph.h"

class FlowMapper : public ComponentHandler {
	typedef std::vector<Number> ScaleFactorVector;
public:
	FlowMapper() : component(NULL) {}
	void Run(Component * c);
private:
	void CalcScaleFactors();
	ScaleFactorVector scale_factors;
	Component * component;
};

#endif /* FLOWMAPPER_H_ */
