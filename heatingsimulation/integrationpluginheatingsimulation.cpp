// SPDX-License-Identifier: GPL-3.0-or-later

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
*
* Copyright (C) 2013 - 2024, nymea GmbH
* Copyright (C) 2024 - 2025, chargebyte austria GmbH
*
* This file is part of nymea-plugins-simulation.
*
* nymea-plugins-simulation is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* nymea-plugins-simulation is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with nymea-plugins-simulation. If not, see <https://www.gnu.org/licenses/>.
*
* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "integrationpluginheatingsimulation.h"
#include "plugininfo.h"

#include "plugintimer.h"

IntegrationPluginHeatingSimulation::IntegrationPluginHeatingSimulation(QObject *parent): IntegrationPlugin (parent)
{

}

IntegrationPluginHeatingSimulation::~IntegrationPluginHeatingSimulation()
{
    hardwareManager()->pluginTimerManager()->unregisterTimer(m_pluginTimer20Seconds);
    hardwareManager()->pluginTimerManager()->unregisterTimer(m_pluginTimer1Min);
}

void IntegrationPluginHeatingSimulation::init()
{
    // Change some values every 20 seconds
    m_pluginTimer20Seconds = hardwareManager()->pluginTimerManager()->registerTimer(20);
    connect(m_pluginTimer20Seconds, &PluginTimer::timeout, this, &IntegrationPluginHeatingSimulation::onPluginTimer20Seconds);

    // Change some values every min
    m_pluginTimer1Min = hardwareManager()->pluginTimerManager()->registerTimer(300);
    connect(m_pluginTimer1Min, &PluginTimer::timeout, this, &IntegrationPluginHeatingSimulation::onPluginTimer1Minute);
}

void IntegrationPluginHeatingSimulation::startMonitoringAutoThings()
{
}

void IntegrationPluginHeatingSimulation::setupThing(ThingSetupInfo *info)
{
    Thing *thing = info->thing();

    if (thing->thingClassId() == ventilationThingClassId) {
        thing->setStateValue(ventilationConnectedStateTypeId, true);
    }

    info->finish(Thing::ThingErrorNoError);
}


void IntegrationPluginHeatingSimulation::thingRemoved(Thing *thing)
{
    Q_UNUSED(thing)

    if (m_simulationTimers.contains(thing)) {
        QTimer *t = m_simulationTimers.take(thing);
        t->stop();
        t->deleteLater();
    }
}

void IntegrationPluginHeatingSimulation::executeAction(ThingActionInfo *info)
{
    Thing *thing = info->thing();
    Action action = info->action();

    if (thing->thingClassId() == heatingThingClassId) {

        // check if this is the "set power" action
        if (action.actionTypeId() == heatingPowerActionTypeId) {

            // get the param value
            Param powerParam = action.param(heatingPowerActionPowerParamTypeId);
            bool power = powerParam.value().toBool();
            qCDebug(dcHeatingSimulation()) << "Set power" << power << "for heating device" << thing->name();
            thing->setStateValue(heatingPowerStateTypeId, power);
            return info->finish(Thing::ThingErrorNoError);

        } else if (action.actionTypeId() == heatingPercentageActionTypeId) {

            // get the param value
            Param percentageParam = action.param(heatingPercentageActionPercentageParamTypeId);
            int percentage = percentageParam.value().toInt();

            qCDebug(dcHeatingSimulation()) << "Set target temperature percentage" << percentage << "for heating device" << thing->name();

            thing->setStateValue(heatingPercentageStateTypeId, percentage);
            return info->finish(Thing::ThingErrorNoError);
        }
        return info->finish(Thing::ThingErrorActionTypeNotFound);

    } else if (thing->thingClassId() == thermostatThingClassId) {
        if (action.actionTypeId() == thermostatBoostActionTypeId) {
            bool boost = action.param(thermostatBoostActionBoostParamTypeId).value().toBool();
            qCDebug(dcHeatingSimulation()) << "Set boost" << boost << "for thermostat device" << thing->name();
            thing->setStateValue(thermostatBoostStateTypeId, boost);
            QTimer *t = m_simulationTimers.value(thing);
            if (!t) {
                t = new QTimer(thing);
                m_simulationTimers.insert(thing, t);
            }
            t->setInterval(5 * 60 * 1000);
            t->setSingleShot(true);
            connect(t, &QTimer::timeout, t, &QTimer::deleteLater);
            connect(t, &QTimer::timeout, thing, [thing](){
                thing->setStateValue(thermostatBoostStateTypeId, false);
            });
            return info->finish(Thing::ThingErrorNoError);
        }
        if (action.actionTypeId() == thermostatTargetTemperatureActionTypeId) {
            double targetTemp = action.param(thermostatTargetTemperatureActionTargetTemperatureParamTypeId).value().toDouble();
            qCDebug(dcHeatingSimulation()) << "Set targetTemp" << targetTemp << "for thermostat device" << thing->name();
            thing->setStateValue(thermostatTargetTemperatureStateTypeId, targetTemp);
            return info->finish(Thing::ThingErrorNoError);
        }

    } else if (thing->thingClassId() == ventilationThingClassId) {
        if (action.actionTypeId() == ventilationFlowRateActionTypeId) {
            int flowRate = action.param(ventilationFlowRateActionFlowRateParamTypeId).value().toInt();
            qCDebug(dcHeatingSimulation()) << "Setting flowrate to" << flowRate;
            thing->setStateValue(ventilationFlowRateStateTypeId, flowRate);
        } else if (action.actionTypeId() == ventilationPowerActionTypeId) {
            bool power = action.param(ventilationPowerActionPowerParamTypeId).value().toBool();
            thing->setStateValue(ventilationPowerStateTypeId, power);
        } else if (action.actionTypeId() == ventilationAutoActionTypeId) {
            bool autoMode = action.param(ventilationAutoActionAutoParamTypeId).value().toBool();
            thing->setStateValue(ventilationAutoStateTypeId, autoMode);
        }
    }
    info->finish(Thing::ThingErrorNoError);
}

void IntegrationPluginHeatingSimulation::onPluginTimer1Minute()
{
    foreach (Thing *thing, myThings()) {

        if (thing->thingClassId() == ventilationThingClassId) {
            bool autoVentilation = thing->stateValue(ventilationAutoStateTypeId).toBool();
            if (autoVentilation) {
                int newLevel = std::rand() % 4;
                thing->setStateValue(ventilationPowerStateTypeId, newLevel > 0);
                if (newLevel > 0) {
                    thing->setStateValue(ventilationFlowRateStateTypeId, newLevel);
                }
            }
        }
    }
}

void IntegrationPluginHeatingSimulation::onPluginTimer20Seconds()
{
    foreach (Thing *thing, myThings()) {
        if (thing->thingClassId() == thermostatThingClassId) {
            double targetTemp = thing->stateValue(thermostatTargetTemperatureStateTypeId).toDouble();
            double currentTemp = thing->stateValue(thermostatTemperatureStateTypeId).toDouble();
            bool heatingOn = thing->stateValue(thermostatHeatingOnStateTypeId).toBool();
            bool coolingOn = thing->stateValue(thermostatCoolingOnStateTypeId).toBool();
            bool boost = thing->stateValue(thermostatBoostStateTypeId).toBool();

            // When we're heating, temp increases slowly until it's up on par with target temp
            if (heatingOn) {
                double diff = targetTemp - currentTemp;
                currentTemp += 0.005 + diff * (boost ? 0.2 : 0.1);
                if (currentTemp >= targetTemp) {
                    thing->setStateValue(thermostatHeatingOnStateTypeId, false);
                }
            } else {
                // Decrease 1% per interval to simulate drop of temperature (assuming it's cold outside)
                currentTemp = currentTemp * 0.995;

                // Start heating when we're more than 2 degrees lower than what we should be
                if (currentTemp < targetTemp - 2) {
                    thing->setStateValue(thermostatHeatingOnStateTypeId, true);
                }
            }

            if (coolingOn) {
                double diff = targetTemp - currentTemp;
                currentTemp += diff * 0.1;
                if (currentTemp <= targetTemp) {
                    thing->setStateValue(thermostatCoolingOnStateTypeId, false);
                }
            } else {
                if (currentTemp > targetTemp + 2) {
                    thing->setStateValue(thermostatCoolingOnStateTypeId, true);
                }
            }

            thing->setStateValue(thermostatTemperatureStateTypeId, currentTemp);
        }
    }
}

void IntegrationPluginHeatingSimulation::simulationTimerTimeout()
{
    QTimer *t = static_cast<QTimer*>(sender());
    Thing *thing = m_simulationTimers.key(t);
    if (thing->thingClassId() == thermostatThingClassId) {
        thing->setStateValue(thermostatBoostStateTypeId, false);
        t->stop();
    }
}
