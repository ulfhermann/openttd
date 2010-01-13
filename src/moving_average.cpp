/*
 * moving_average.cpp
 *
 *  Created on: Jan 13, 2010
 *      Author: alve
 */

#include "moving_average.h"
#include "core/pool_func.hpp"

/* Initialize the movingaverage-pool */
MovingAveragePool _movingaverage_pool("MovingAverage");
INSTANTIATE_POOL_METHODS(MovingAverage)

MovingAverage::MovingAverageDeque MovingAverage::_moving_averages;

/* static */ void MovingAverage::Tick()
{
	MovingAverageList &list = _moving_averages.front();
	for(MovingAverageList::iterator i = list.begin(); i != list.end(); ++i) {
		MovingAverageID id = *i;
		if (MovingAverage::IsValidID(id)) {
			MovingAverage *average = MovingAverage::Get(id);
			average->Decrease();
			average->InsertIntoDeque();
		}
	}
	_moving_averages.pop_front();
}


void MovingAverage::InsertIntoDeque() {
	assert(this->length > 0);
	if (_moving_averages.size() <= this->length) {
		_moving_averages.resize(this->length + 1, MovingAverageList());
	}
	_moving_averages[this->length].push_back(this->index);
}

MovingAverage::MovingAverage(uint length) :
	value(0), length(length)
{
	this->InsertIntoDeque();
}
