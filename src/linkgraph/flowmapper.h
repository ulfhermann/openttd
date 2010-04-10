/** @file flowmapper.h Declaration of flow mapper; merges paths into flows at nodes */

#ifndef FLOWMAPPER_H_
#define FLOWMAPPER_H_

#include "linkgraph.h"

class FlowMapper : public ComponentHandler {
public:
	virtual void Run(LinkGraphComponent *component);
	virtual ~FlowMapper() {}
};

#endif /* FLOWMAPPER_H_ */
