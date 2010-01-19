/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file moving_average.h Utility class for moving averages. */

#ifndef MOVING_AVERAGE_H_
#define MOVING_AVERAGE_H_

#include "stdafx.h"
#include "settings_type.h"
#include "core/math_func.hpp"

template<class Tvalue = uint>
class MovingAverage {
protected:
	uint length;

public:
	FORCEINLINE MovingAverage(uint length = _settings_game.economy.moving_average_length) : length(length) 
		{assert(this->length > 0);}

	FORCEINLINE uint Length() const
		{return this->length;}

	FORCEINLINE Tvalue Monthly(const Tvalue &value) const
		{return value * 30 / this->length / _settings_game.economy.moving_average_unit;}

	FORCEINLINE Tvalue Decrease(const Tvalue &value) const
		{return value * (this->length) / (this->length + 1);}
};

template<class Titem> void RunAverages();

#endif /* MOVING_AVERAGE_H_ */

