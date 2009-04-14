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
public:
	FlowMapper() : component(NULL) {}
	void Run(LinkGraphComponent * c);
private:
	LinkGraphComponent * component;
};

#endif /* FLOWMAPPER_H_ */
