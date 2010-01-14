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

MovingAverageRegistry _moving_averages;

void OnTick_MovingAverage()
{
	MovingAverageList &list = _moving_averages.front();
	for(MovingAverageList::iterator i = list.begin(); i != list.end(); ++i) {
		MovingAverageID id = *i;
		if (MovingAverage::IsValidID(id)) {
			MovingAverage *average = MovingAverage::Get(id);
			average->Decrease();
			average->Register();
		}
	}
	_moving_averages.pop_front();
}


void MovingAverage::Register() {
	assert(this->length > 0);
	/* moving_average_unit determines how often the moving averages are calculated.
	 * Setting each average "further away" in the registry delays its calculation.
	 * This is taken into account when retrieving the monthly value
	 */
	uint real_length = this->length * _settings_game.economy.moving_average_unit * DAY_TICKS;
	if (_moving_averages.size() <= real_length) {
		_moving_averages.resize(real_length + 1, MovingAverageList());
	}
	_moving_averages[real_length].push_back(this->index);
}

MovingAverage::MovingAverage(uint length) :
	value(0), length(length)
{
	this->Register();
}
