/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
*
* Copyright 2013 - 2020, nymea GmbH
* Contact: contact@nymea.io
*
* This file is part of nymea.
* This project including source code and documentation is protected by
* copyright law, and remains the property of nymea GmbH. All rights, including
* reproduction, publication, editing and translation, are reserved. The use of
* this project is subject to the terms of a license agreement to be concluded
* with nymea GmbH in accordance with the terms of use of nymea GmbH, available
* under https://nymea.io/license
*
* GNU Lesser General Public License Usage
* Alternatively, this project may be redistributed and/or modified under the
* terms of the GNU Lesser General Public License as published by the Free
* Software Foundation; version 3. This project is distributed in the hope that
* it will be useful, but WITHOUT ANY WARRANTY; without even the implied
* warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this project. If not, see <https://www.gnu.org/licenses/>.
*
* For any further details and any questions please contact us under
* contact@nymea.io or see our FAQ/Licensing Information on
* https://nymea.io/license/faq
*
* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "integrationpluginenergysimulation.h"

#include "plugintimer.h"
#include "plugininfo.h"

#include <QtMath>

#define CIVIL_ZENITH  90.83333


IntegrationPluginEnergySimulation::IntegrationPluginEnergySimulation(QObject *parent): IntegrationPlugin (parent)
{

}

void IntegrationPluginEnergySimulation::discoverThings(ThingDiscoveryInfo *info)
{
    QTimer::singleShot(1000, info, [=]{
        ThingClass thingClass = IntegrationPlugin::thingClass(info->thingClassId());
        for (uint i = 0; i < configValue(energySimulationPluginDiscoveryResultCountParamTypeId).toUInt(); i++) {
            ThingDescriptor descriptor(info->thingClassId(), thingClass.displayName());
            info->addThingDescriptor(descriptor);
        }
        info->finish(Thing::ThingErrorNoError);
    });
}

void IntegrationPluginEnergySimulation::setupThing(ThingSetupInfo *info)
{
    Thing *thing = info->thing();
    info->finish(Thing::ThingErrorNoError);

    if (!m_timer) {
        m_timer = hardwareManager()->pluginTimerManager()->registerTimer(5);
        connect(m_timer, &PluginTimer::timeout, this, &IntegrationPluginEnergySimulation::updateSimulation);
    }

    if (!m_totalsTimer) {
        m_totalsTimer = hardwareManager()->pluginTimerManager()->registerTimer(60);
        connect(m_totalsTimer, &PluginTimer::timeout, this, [=](){
            foreach (Thing *smartMeter, m_pendingTotalConsumption.keys()) {
                double totalEnergyConsumed = smartMeter->stateValue(smartMeterTotalEnergyConsumedStateTypeId).toDouble();
                smartMeter->setStateValue(smartMeterTotalEnergyConsumedStateTypeId, totalEnergyConsumed + m_pendingTotalConsumption.value(smartMeter));
                m_pendingTotalConsumption[smartMeter] = 0;
            }
            foreach (Thing *smartMeter, m_pendingTotalProduction.keys()) {
                double totalEnergyReturned = smartMeter->stateValue(smartMeterTotalEnergyProducedStateTypeId).toDouble();
                smartMeter->setStateValue(smartMeterTotalEnergyProducedStateTypeId, totalEnergyReturned + m_pendingTotalProduction.value(smartMeter));
                m_pendingTotalProduction[smartMeter] = 0;
            }
        });
    }

    if (thing->thingClassId() == wallboxThingClassId) {
        connect(info->thing(), &Thing::settingChanged, this, [thing](const ParamTypeId &settingTypeId, const QVariant &value){
            if (settingTypeId == wallboxSettingsMaxChargingCurrentUpperLimitParamTypeId) {
                thing->setStateMaxValue(wallboxMaxChargingCurrentStateTypeId, value);
            }
        });
    }
}


void IntegrationPluginEnergySimulation::thingRemoved(Thing *thing)
{
    m_pendingTotalConsumption.remove(thing);
    m_pendingTotalProduction.remove(thing);
}

void IntegrationPluginEnergySimulation::executeAction(ThingActionInfo *info)
{
    if (info->thing()->thingClassId() == stoveThingClassId) {
        if (info->action().actionTypeId() == stovePowerActionTypeId) {
            info->thing()->setStateValue(stovePowerStateTypeId, info->action().paramValue(stovePowerActionPowerParamTypeId).toBool());
        }
    }
    if (info->thing()->thingClassId() == wallboxThingClassId) {
        if (info->action().actionTypeId() == wallboxPowerActionTypeId) {
            info->thing()->setStateValue(wallboxPowerStateTypeId, info->action().paramValue(wallboxPowerActionPowerParamTypeId).toBool());
        }
        if (info->action().actionTypeId() == wallboxMaxChargingCurrentActionTypeId) {
            info->thing()->setStateValue(wallboxMaxChargingCurrentStateTypeId, info->action().paramValue(wallboxMaxChargingCurrentActionMaxChargingCurrentParamTypeId));
        }
    }
    if (info->thing()->thingClassId() == carThingClassId) {
        if (info->action().actionTypeId() == carPluggedInActionTypeId) {
            if (info->action().paramValue(carPluggedInActionPluggedInParamTypeId).toBool()) {
                // Try to plug the car to the first free wallbox
                foreach (Thing *wallbox, myThings().filterByThingClassId(wallboxThingClassId)) {
                    if (wallbox->property("connectedCarThingId").toUuid().isNull()) {
                        // Found an empty wallbox, plugging it in
                        wallbox->setProperty("connectedCarThingId", info->thing()->id());
                        info->thing()->setStateValue(carPluggedInStateTypeId, true);
                        wallbox->setStateValue(wallboxPluggedInStateTypeId, true);
                        info->finish(Thing::ThingErrorNoError);
                        return;
                    }
                }
                // No wallbox found where we could plug into... Failing action
                info->finish(Thing::ThingErrorHardwareNotAvailable, "No free wallbox found");
                return;
            } else {
                info->thing()->setStateValue(carPluggedInStateTypeId, false);
                // Unplug from wallbox
                foreach (Thing *wallbox, myThings().filterByThingClassId(wallboxThingClassId)) {
                    if (wallbox->property("connectedCarThingId").toUuid() == info->thing()->id()) {
                        wallbox->setProperty("connectedCarThingId", QUuid());
                        wallbox->setStateValue(wallboxPluggedInStateTypeId, false);
                        break;
                    }
                }
                info->finish(Thing::ThingErrorNoError);
                return;
            }
        } else if (info->action().actionTypeId() == carMinChargingCurrentActionTypeId) {
            info->thing()->setStateValue(carMinChargingCurrentStateTypeId, info->action().paramValue(carMinChargingCurrentActionMinChargingCurrentParamTypeId));
        }
    }
    info->finish(Thing::ThingErrorNoError);
}

void IntegrationPluginEnergySimulation::updateSimulation()
{
    qCDebug(dcEnergySimulation()) << "*******************  Adjusting simulation" << QDate::currentDate().toString();

    // Update solar inverters
    foreach (Thing* inverter, myThings().filterByThingClassId(solarInverterThingClassId)) {
        QDateTime now = QDateTime::currentDateTime();
        int hoursOffset = inverter->setting(solarInverterSettingsHoursOffsetParamTypeId).toInt();
        now = now.addSecs(hoursOffset * 60 * 60);

        QPair<QDateTime, QDateTime> sunriseSunset = calculateSunriseSunset(48, 10, now);
        QDateTime sunrise = sunriseSunset.first;
        QDateTime sunset = sunriseSunset.second;

        if (sunrise < now && now < sunset) {
            qlonglong msecsOfLight = sunriseSunset.second.toMSecsSinceEpoch() - sunriseSunset.first.toMSecsSinceEpoch();
            qlonglong currentMSecOfLight = now.toMSecsSinceEpoch() - sunrise.toMSecsSinceEpoch();
            qreal degrees = (currentMSecOfLight * 180 / msecsOfLight) - 90;

            double currentProduction = qCos(qDegreesToRadians(degrees)) * inverter->setting(solarInverterSettingsMaxCapacityParamTypeId).toDouble();
            qCDebug(dcEnergySimulation()) << "* Inverter" << inverter->name() << "production:" << currentProduction << "W";
            inverter->setStateValue(solarInverterCurrentPowerStateTypeId, -currentProduction);
        } else {
            qCDebug(dcEnergySimulation()) << "* Inverter" << inverter->name() << "production:" << "0" << "W";
            inverter->setStateValue(solarInverterCurrentPowerStateTypeId, 0);
        }
    }

    // Update evchargers
    foreach (Thing* evCharger, myThings().filterByThingClassId(wallboxThingClassId)) {
        if (evCharger->stateValue(wallboxPluggedInStateTypeId).toBool() && evCharger->stateValue(wallboxPowerStateTypeId).toBool()) {
            ThingId connectedCarThingId = evCharger->property("connectedCarThingId").toUuid();
            Thing *car = myThings().findById(connectedCarThingId);
            qCDebug(dcEnergySimulation()) << "* Evaluating wallbox:" << evCharger->name() << "Connected car:" << (car ? car->name() : "none");
            if (car && car->stateValue(carBatteryLevelStateTypeId).toInt() < 100) {
                evCharger->setStateValue(wallboxChargingStateTypeId, true);
                QDateTime lastChargeUpdateTime = car->property("lastChargeUpdateTime").toDateTime();
                if (lastChargeUpdateTime.isNull()) {
                    car->setProperty("lastChargeUpdateTime", QDateTime::currentDateTime());
                    break;
                }
                uint maxChargingCurrent = evCharger->stateValue(wallboxMaxChargingCurrentStateTypeId).toUInt();
                double chargingPower = 230 * maxChargingCurrent;
                double chargingTimeHours = 1.0 * lastChargeUpdateTime.msecsTo(QDateTime::currentDateTime()) / 1000 / 60 / 60;
                double chargedWattHours = chargingPower * chargingTimeHours;
                double carCapacity = car->stateValue(carCapacityStateTypeId).toDouble();
                // cWH : cap = x : 100
                double chargedPercentage = chargedWattHours / 1000 * 100 / carCapacity;
                qCDebug(dcEnergySimulation()) << "* #### Car charging info:";
                qCDebug(dcEnergySimulation()) << "* # max charging current:" << maxChargingCurrent << "A";
                qCDebug(dcEnergySimulation()) << "* # time passed since last update:" << chargingTimeHours;
                qCDebug(dcEnergySimulation()) << "* # charged" << chargedWattHours << "Wh," << chargedPercentage << "%";

                evCharger->setStateValue(wallboxCurrentPowerStateTypeId, chargingPower);

                if (chargedPercentage >= 1) {
                    car->setProperty("lastChargeUpdateTime", QDateTime::currentDateTime());

                    car->setStateValue(carBatteryLevelStateTypeId, car->stateValue(carBatteryLevelStateTypeId).toInt() + chargedPercentage);
                    car->setStateValue(carBatteryCriticalStateTypeId, car->stateValue(carBatteryLevelStateTypeId).toInt() < 10);
                }
            } else {
                qCDebug(dcEnergySimulation()) << "* Ev charger using 0 (Car already full)";
                evCharger->setStateValue(wallboxChargingStateTypeId, false);
                evCharger->setStateValue(wallboxCurrentPowerStateTypeId, 0);
            }
        } else {
            qCDebug(dcEnergySimulation()) << "* Ev charger using 0 (Car not plugged in or charging disabled)";
            evCharger->setStateValue(wallboxChargingStateTypeId, false);
            evCharger->setStateValue(wallboxCurrentPowerStateTypeId, 0);
        }
    }

    // Reduce battery level on all unplugged cars
    foreach (Thing *car, myThings().filterByThingClassId(carThingClassId)) {
        if (!car->stateValue(carPluggedInStateTypeId).toBool() && car->stateValue(carBatteryLevelStateTypeId).toInt() > 0) {
            car->setStateValue(carBatteryLevelStateTypeId, car->stateValue(carBatteryLevelStateTypeId).toInt() - 1);
            car->setStateValue(carBatteryCriticalStateTypeId, car->stateValue(carBatteryLevelStateTypeId).toInt() < 10);
        }
    }

    // Update stove
    foreach (Thing *stove, myThings().filterByThingClassId(stoveThingClassId)) {
        if (stove->stateValue(stovePowerStateTypeId).toBool()) {
            int cycle = stove->property("simulationCycle").toInt() % 12;
            double maxPower = stove->setting(stoveSettingsMaxPowerConsumptionParamTypeId).toDouble();
            double currentPower = 0;
            double totalEnergyConsumed = stove->stateValue(stoveTotalEnergyConsumedStateTypeId).toDouble();
            if (cycle < 4) {
                currentPower = maxPower;
                totalEnergyConsumed += (maxPower / 1000) / 60 / 60 * 5;
            }
            stove->setStateValue(stoveCurrentPowerStateTypeId, currentPower);
            stove->setStateValue(stoveTotalEnergyConsumedStateTypeId, totalEnergyConsumed);
            stove->setProperty("simulationCycle", cycle + 1);
            qCDebug(dcEnergySimulation()) << "* Stove using" << currentPower << "W";
        }
    }



    // Sum up production from inverters
    QHash<QString, double> totalPhaseProduction = {
        {"A", 0},
        {"B", 0},
        {"C", 0}
    };
    foreach (Thing *inverter, myThings().filterByThingClassId(solarInverterThingClassId)) {
        QString phase = inverter->setting(solarInverterSettingsPhaseParamTypeId).toString();
        double production = inverter->stateValue(solarInverterCurrentPowerStateTypeId).toDouble();
        if (phase == "All") {
            totalPhaseProduction["A"] += production / 3;
            totalPhaseProduction["B"] += production / 3;
            totalPhaseProduction["C"] += production / 3;
        } else {
            totalPhaseProduction[phase] += production;
        }
    }


    // Sum up consumption of all consumers
    QHash<QString, double> totalPhasesConsumption = {
        {"A", 0},
        {"B", 0},
        {"C", 0}
    };
    // Simulate a base consumption of 300W (100 on each phase) + 10W jitter
    totalPhasesConsumption["A"] += 100 + (qrand() % 10);
    totalPhasesConsumption["B"] += 100 + (qrand() % 10);
    totalPhasesConsumption["C"] += 100 + (qrand() % 10);



    // And add simulation devices consumption
    foreach (Thing *consumer, myThings()) {
        if (consumer->thingClass().interfaces().contains("smartmeterconsumer")) {
            QString phase = consumer->setting("phase").toString();
            double currentPower = consumer->stateValue("currentPower").toDouble();
            if (phase == "All") {
                totalPhasesConsumption["A"] += currentPower / 3;
                totalPhasesConsumption["B"] += currentPower / 3;
                totalPhasesConsumption["C"] += currentPower / 3;
            } else {
                totalPhasesConsumption[phase] += currentPower;
            }
        }
    }

    // Add evchargers
    foreach (Thing *evCharger, myThings().filterByThingClassId(wallboxThingClassId)) {
        Thing *connectedCar = myThings().findById(evCharger->property("connectedCarThingId").toUuid());
        if (evCharger->stateValue(wallboxPowerStateTypeId).toBool()
                && evCharger->stateValue(wallboxPluggedInStateTypeId).toBool()
                && connectedCar && connectedCar->stateValue(carBatteryLevelStateTypeId).toInt() < 100) {
            uint maxChargingCurrent = evCharger->stateValue(wallboxMaxChargingCurrentStateTypeId).toUInt();
            double currentConsumption = maxChargingCurrent * 230; // One phase

            evCharger->setStateValue(wallboxChargingStateTypeId, true);

            qCDebug(dcEnergySimulation()) << "* Wallbox" << evCharger->name() << "consumes" << currentConsumption << "W";
            QString phase = evCharger->setting(wallboxSettingsPhaseParamTypeId).toString();
            if (phase == "All") {
                totalPhasesConsumption["A"] += currentConsumption;
                totalPhasesConsumption["B"] += currentConsumption;
                totalPhasesConsumption["C"] += currentConsumption;
                evCharger->setStateValue(wallboxPhaseCountStateTypeId, 3);
            } else {
                totalPhasesConsumption[phase] += currentConsumption;
                evCharger->setStateValue(wallboxPhaseCountStateTypeId, 1);
            }
        } else {
            evCharger->setStateValue(wallboxChargingStateTypeId, false);
        }
    }


    // Sum up all phases for the total consumption/production (momentary, in Watt)
    double totalProduction = 0;
    foreach (double phaseProduction, totalPhaseProduction) {
        totalProduction += phaseProduction;
    }
    double totalConsumption = 0;
    foreach (double phaseConsumption, totalPhasesConsumption) {
        totalConsumption += phaseConsumption;
    }
    double grandTotal = totalConsumption + totalProduction; // Note: production is negative


    // Charge/discharge batteries depending on totals so far
    foreach (Thing *battery, myThings().filterByThingClassId(batteryThingClassId)) {
        int batteryLevel = battery->stateValue(batteryBatteryLevelStateTypeId).toInt();
        QString phase = battery->setting(batterySettingsPhaseParamTypeId).toString();
        double chargeRate = battery->setting(batterySettingsChargingRateParamTypeId).toDouble();

        if (grandTotal < 0 && batteryLevel < 100) {
            double chargedWatts = qMin(chargeRate, -grandTotal);
            if (phase == "All") {
                totalPhasesConsumption["A"] += chargedWatts / 3;
                totalPhasesConsumption["B"] += chargedWatts / 3;
                totalPhasesConsumption["C"] += chargedWatts / 3;
            } else {
                totalPhasesConsumption[phase] += chargedWatts;
            }

            battery->setStateValue(batteryChargingStateStateTypeId, "charging");
            battery->setStateValue(batteryCurrentPowerStateTypeId, chargedWatts);

            double pendingChargedWh = battery->property("pendingChargedWh").toDouble();
            QDateTime lastUpdate = battery->property("lastUpdate").toDateTime();
            double hoursSinceLastUpdate = 1.0 * lastUpdate.msecsTo(QDateTime::currentDateTime()) / 1000 / 60 / 60;
            pendingChargedWh += chargedWatts * hoursSinceLastUpdate;
            double whPerPercent = battery->setting(batterySettingsCapacityParamTypeId).toDouble() / 100 * 1000;
            qCDebug(dcEnergySimulation()) << "* Charging battery with" << chargedWatts << "W";
            if (pendingChargedWh > whPerPercent) {
                battery->setStateValue(batteryBatteryLevelStateTypeId, batteryLevel + 1);
                battery->setStateValue(batteryBatteryCriticalStateTypeId, batteryLevel < 10);
                pendingChargedWh = 0;
            }
            battery->setProperty("pendingChargedWh", pendingChargedWh);
            battery->setProperty("lastUpdate", QDateTime::currentDateTime());

        } else if (grandTotal > 0 && batteryLevel > 0) {
            double returnedWatts = qMin(chargeRate, grandTotal);
            if (phase == "All") {
                totalPhaseProduction["A"] += -returnedWatts / 3;
                totalPhaseProduction["B"] += -returnedWatts / 3;
                totalPhaseProduction["C"] += -returnedWatts / 3;
            } else {
                totalPhaseProduction[phase] += -returnedWatts;
            }
            battery->setStateValue(batteryChargingStateStateTypeId, "discharging");
            battery->setStateValue(batteryCurrentPowerStateTypeId, -returnedWatts);


            double pendingDischargedWh = battery->property("pendingDischargedWh").toDouble();
            QDateTime lastUpdate = battery->property("lastUpdate").toDateTime();
            double hoursSinceLastUpdate = 1.0 * lastUpdate.msecsTo(QDateTime::currentDateTime()) / 1000 / 60 / 60;
            pendingDischargedWh += returnedWatts * hoursSinceLastUpdate;
            double whPerPercent = battery->setting(batterySettingsCapacityParamTypeId).toDouble() / 100 * 1000;
            qCDebug(dcEnergySimulation()) << "* Using from battery with" << returnedWatts << "W";
            if (pendingDischargedWh > whPerPercent) {
                battery->setStateValue(batteryBatteryLevelStateTypeId, batteryLevel - 1);
                battery->setStateValue(batteryBatteryCriticalStateTypeId, batteryLevel < 10);
                pendingDischargedWh = 0;
            }
            battery->setProperty("pendingDischargedWh", pendingDischargedWh);
            battery->setProperty("lastUpdate", QDateTime::currentDateTime());

        } else {
            battery->setStateValue(batteryChargingStateStateTypeId, "idle");
            battery->setStateValue(batteryCurrentPowerStateTypeId, 0);
        }
    }


    // Sum up all phases *again* after the battery has been facored in
    totalProduction = 0;
    foreach (double phaseProduction, totalPhaseProduction) {
        totalProduction += phaseProduction;
    }
    totalConsumption = 0;
    foreach (double phaseConsumption, totalPhasesConsumption) {
        totalConsumption += phaseConsumption;
    }
    grandTotal = totalConsumption + totalProduction; // Note: production is negative

    qCDebug(dcEnergySimulation()) << "* Grand total power consumption:" << grandTotal << "W";

    // Update the smart meter totals
    foreach (Thing *smartMeter, myThings().filterByThingClassId(smartMeterThingClassId)) {
        // First set current power consumptions
        qCDebug(dcEnergySimulation()) << "* Updating smart meter:" << smartMeter->name();
        smartMeter->setStateValue(smartMeterCurrentPowerPhaseAStateTypeId, totalPhasesConsumption["A"] + totalPhaseProduction["A"]);
        smartMeter->setStateValue(smartMeterCurrentPowerPhaseBStateTypeId, totalPhasesConsumption["B"] + totalPhaseProduction["B"]);
        smartMeter->setStateValue(smartMeterCurrentPowerPhaseCStateTypeId, totalPhasesConsumption["C"] + totalPhaseProduction["C"]);
        smartMeter->setStateValue(smartMeterCurrentPowerStateTypeId, grandTotal);

        // Add up total consumed/returned
        // Transform current power to kWh for the last 5 secs (simulation interval)
        double consumption = grandTotal / 1000 / 60 / 60 * 5;
        if (grandTotal > 0) {
            m_pendingTotalConsumption[smartMeter] += consumption;
        } else {
            m_pendingTotalProduction[smartMeter] -= consumption;
        }
    }
}

QPair<QDateTime, QDateTime> IntegrationPluginEnergySimulation::calculateSunriseSunset(qreal latitude, qreal longitude, const QDateTime &dateTime)
{
    int dayOfYear = dateTime.date().dayOfYear();
    int offset = dateTime.offsetFromUtc() / 60 / 60;

    // Convert the longitude to hour value and calculate an approximate time
    qreal longitudeHour = longitude / 15;
    qreal tRise = dayOfYear + ((6 - longitudeHour) / 24);
    qreal tSet = dayOfYear + ((18 - longitudeHour) / 24);

    // Calculate the Sun's mean anomaly
    qreal mRise = (0.9856 * tRise) - 3.289;
    qreal mSet = (0.9856 * tSet) - 3.289;

    // Calculate the Sun's true longitude, and adjust angle to be between 0 and 360
    qreal tmp = mRise + (1.916 * qSin(qDegreesToRadians(mRise))) + (0.020 * qSin(qDegreesToRadians(2 * mRise))) + 282.634;
    qreal lRise = qFloor(tmp + 360) % 360 + (tmp - qFloor(tmp));
    tmp = mSet + (1.916 * qSin(qDegreesToRadians(mSet))) + (0.020 * qSin(qDegreesToRadians(2 * mSet))) + 282.634;
    qreal lSet = qFloor(tmp + 360) % 360 + (tmp - qFloor(tmp));

    // Calculate the Sun's right ascension, and adjust angle to be between 0 and 360
    tmp = qRadiansToDegrees(qAtan(0.91764 * qTan(qDegreesToRadians(1.0 * lRise))));
    qreal raRise = qFloor(tmp + 360) % 360 + (tmp - qFloor(tmp));
    tmp = qRadiansToDegrees(qAtan(0.91764 * qTan(qDegreesToRadians(1.0 * lSet))));
    qreal raSet = qRound(tmp + 360) % 360 + (tmp - qFloor(tmp));

    // Right ascension value needs to be in the same quadrant as L
    qlonglong lQuadrantRise  = qFloor(lRise/90) * 90;
    qlonglong raQuadrantRise = qFloor(raRise/90) * 90;
    raRise = raRise + (lQuadrantRise - raQuadrantRise);

    qlonglong lQuadrantSet  = qFloor(lSet/90) * 90;
    qlonglong raQuadrantSet = qFloor(raSet/90) * 90;
    raSet = raSet + (lQuadrantSet - raQuadrantSet);

    // Right ascension value needs to be converted into hours
    raRise = raRise / 15;
    raSet = raSet / 15;

    // Calculate the Sun's declination
    qreal sinDecRise = 0.39782 * qSin(qDegreesToRadians(1.0 * lRise));
    qreal cosDecRise = qCos(qAsin(sinDecRise));

    qreal sinDecSet = 0.39782 * qSin(qDegreesToRadians(1.0 * lSet));
    qreal cosDecSet = qCos(qAsin(sinDecSet));

    // Calculate the Sun's local hour angle
    qreal cosZenith = qCos(qDegreesToRadians(CIVIL_ZENITH));
    qreal radianLat = qDegreesToRadians(latitude);
    qreal sinLatitude = qSin(radianLat);
    qreal cosLatitude = qCos(radianLat);
    qreal cosHRise = (cosZenith - (sinDecRise * sinLatitude)) / (cosDecRise * cosLatitude);
    qreal cosHSet = (cosZenith - (sinDecSet * sinLatitude)) / (cosDecSet * cosLatitude);

    // Finish calculating H and convert into hours
    qreal hRise = (360 - qRadiansToDegrees(qAcos(cosHRise))) / 15;
    qreal hSet = qRadiansToDegrees(qAcos(cosHSet)) / 15;

    // Calculate local mean time of rising/setting
    tRise = hRise + raRise - (0.06571 * tRise) - 6.622;
    tSet = hSet + raSet - (0.06571 * tSet) - 6.622;

    // Adjust back to UTC, and keep the time between 0 and 24
    tmp = tRise - longitudeHour;
    qreal utRise = qFloor(tmp + 24) % 24 + (tmp - qFloor(tmp));
    tmp = tSet - longitudeHour;
    qreal utSet = qFloor(tmp + 24) % 24 + (tmp - qFloor(tmp));

    // Adjust again to localtime
    tmp = utRise + offset;
    qreal localtRise = qFloor(tmp + 24) % 24 + (tmp - qFloor(tmp));
    tmp = utSet + offset;
    qreal localtSet = qFloor(tmp + 24) % 24 + (tmp - qFloor(tmp));

    // Conversion
    int hourRise = qFloor(localtRise);
    int minuteRise = qFloor((localtRise - qFloor(localtRise)) * 60);
    int hourSet = qFloor(localtSet);
    int minuteSet = qFloor((localtSet - qFloor(localtSet)) * 60);

    QDateTime sunrise(dateTime.date(), QTime(hourRise, minuteRise));
    QDateTime sunset(dateTime.date(), QTime(hourSet, minuteSet));

    return qMakePair<QDateTime, QDateTime>(sunrise, sunset);
}
