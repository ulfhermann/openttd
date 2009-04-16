/** @file flowmapper.h Declaration of flow mapper; merges paths into flows at nodes */

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
