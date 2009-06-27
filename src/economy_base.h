/* $Id$ */

/** @file economy_type.h Classes related to the economy. */

#ifndef ECONOMY_BASE_H_
#define ECONOMY_BASE_H_

#include "cargopacket.h"
#include "core/smallvec_type.hpp"

typedef SmallVector<Industry *, 16> SmallIndustryList;

class Payment {
private:
	CompanyID old_company;
	Vehicle * front;
	Money transfer_pay;
	Money final_pay;
	Money vehicle_profit;
	SmallIndustryList *industries;
	CargoID current_cargo;
	StationID current_station;
public:
	Payment(Vehicle * v, StationID station, SmallIndustryList &ind);
	void SetCargo(CargoID cargo) {current_cargo = cargo;}
	void PayTransfer(CargoPacket * cp, uint count);
	void PayFinal(CargoPacket * cp, uint count);
	void PlaySoundIfProfit();
	Money GetSumFinal() {return final_pay;}
	Money GetSumTransfer() {return transfer_pay;}
	Money GetVehicleProfit() {return vehicle_profit;}
	~Payment();
};


#endif /* ECONOMY_BASE_H_ */
