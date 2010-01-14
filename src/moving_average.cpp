/*
 * moving_average.cpp
 *
 *  Created on: Jan 13, 2010
 *      Author: alve
 */

#include "moving_average.h"
#include "variables.h"
#include "core/pool_func.hpp"

/* Initialize the movingaverage-pool */
MovingAveragePool _movingaverage_pool("MovingAverage");
INSTANTIATE_POOL_METHODS(MovingAverage)

void CallMovingAverageTick()
{
	uint interval = _settings_game.economy.moving_average_unit * DAY_TICKS;
	for(uint ma_id = _tick_counter % interval; ma_id < MovingAverage::GetPoolSize(); ++ma_id) {
		MovingAverage *ma = MovingAverage::GetIfValid(ma_id);
		if (ma != NULL) {
			ma->Decrease();
		}
	}
}


MovingAverage::MovingAverage(uint length) :
	value(0), length(length)
{}
