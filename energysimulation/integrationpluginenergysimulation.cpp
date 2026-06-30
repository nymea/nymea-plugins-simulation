// SPDX-License-Identifier: GPL-3.0-or-later

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
*
* Copyright (C) 2013 - 2024, nymea GmbH
* Copyright (C) 2024 - 2026, chargebyte austria GmbH
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

#include "integrationpluginenergysimulation.h"

#include "plugintimer.h"
#include "plugininfo.h"

#include <algorithm>
#include <QtMath>
#include <QRandomGenerator>

#define CIVIL_ZENITH  90.83333

namespace {

static const char *connectedVehiclePropertyName = "connectedVehicleThingId";
static const char *legacyConnectedCarPropertyName = "connectedCarThingId";
static const char *storedEnergyPropertyName = "storedEnergyKWh";
static const char *lastEnergyUpdatePropertyName = "lastEnergyUpdateTime";
static const char *pendingHlcVehiclePropertyName = "pendingHlcVehicleConnection";
static const char *pendingHlcVehicleMacAddressPropertyName = "pendingHlcVehicleMacAddress";
static const char *pendingPhaseSwitchPropertyName = "pendingPhaseSwitch";
static const char *pendingPhaseSwitchTargetPropertyName = "pendingPhaseSwitchTarget";

bool isElectricVehicleThing(Thing *thing)
{
    if (!thing) {
        return false;
    }

    const QStringList interfaces = thing->thingClass().interfaces();
    return interfaces.contains("electricvehicle")
            || interfaces.contains("electricvehiclehlc");
}

void setVehicleChargingState(Thing *vehicle, const QString &chargingState)
{
    if (!vehicle || !vehicle->hasState("chargingState")) {
        return;
    }

    vehicle->setStateValue("chargingState", chargingState);
}

bool supportsChargingInterface(Thing *vehicle, const QString &chargingInterface)
{
    if (!isElectricVehicleThing(vehicle) || !vehicle->hasState("chargingInterfaces")) {
        return false;
    }

    const QString interfaces = vehicle->stateValue("chargingInterfaces").toString();
    return interfaces == chargingInterface || interfaces == "acdc";
}

bool isDcChargeableVehicleThing(Thing *thing)
{
    return supportsChargingInterface(thing, "dc");
}

bool isAcChargeableVehicleThing(Thing *thing)
{
    return supportsChargingInterface(thing, "ac");
}

bool isAcChargerThing(Thing *charger)
{
    return charger && (charger->thingClassId() == wallboxThingClassId || charger->thingClassId() == wallboxNoMeterThingClassId);
}

bool thingNameLessThan(Thing *left, Thing *right)
{
    const int nameCompare = QString::localeAwareCompare(left->name(), right->name());
    if (nameCompare != 0) {
        return nameCompare < 0;
    }

    return left->id().toString() < right->id().toString();
}

bool isVehicleAvailable(Thing *vehicle)
{
    if (!isElectricVehicleThing(vehicle)) {
        return false;
    }

    if (vehicle->hasState("pluggedIn") && vehicle->stateValue("pluggedIn").toBool()) {
        return false;
    }

    if (vehicle->hasState("connectedChargerThingId") && !vehicle->stateValue("connectedChargerThingId").toString().isEmpty()) {
        return false;
    }

    if (vehicle->hasState("hlcSessionActive") && vehicle->stateValue("hlcSessionActive").toBool()) {
        return false;
    }

    return true;
}

bool isHlcChargerThing(Thing *charger)
{
    return charger && charger->thingClassId() == hlcDcChargerThingClassId;
}

bool isDcChargerThing(Thing *charger)
{
    return charger && (charger->thingClassId() == dcChargerThingClassId || charger->thingClassId() == hlcDcChargerThingClassId);
}

double intervalEnergy(double power)
{
    return (power / 1000.0) / 60.0 / 60.0 * 5.0;
}

void resetVehicleChargingSession(Thing *vehicle, const QDateTime &timestamp = QDateTime::currentDateTime())
{
    if (!vehicle) {
        return;
    }

    setVehicleChargingState(vehicle, "idle");
    vehicle->setProperty(lastEnergyUpdatePropertyName, timestamp);
}

double limitVehicleDcChargingPower(Thing *vehicle, double requestedPower)
{
    if (!vehicle || requestedPower <= 0 || !vehicle->hasState("dcMaxChargingPower")) {
        return requestedPower;
    }

    const double maxChargingPower = vehicle->stateValue("dcMaxChargingPower").toDouble();
    if (maxChargingPower <= 0) {
        return requestedPower;
    }

    return qMin(requestedPower, maxChargingPower);
}

double normalizeChargingPowerSetpoint(Thing *charger, double requestedPower)
{
    if (!charger) {
        return 0;
    }

    if (charger->thingClassId() == dcChargerThingClassId) {
        requestedPower = qMax(0.0, requestedPower);
        const double maxChargingPower = charger->stateValue(dcChargerMaxChargingPowerStateTypeId).toDouble();
        const double minChargingPower = charger->stateValue(dcChargerMinChargingPowerStateTypeId).toDouble();
        if (maxChargingPower > 0) {
            requestedPower = qMin(requestedPower, maxChargingPower);
        }
        if (requestedPower > 0 && minChargingPower > 0 && requestedPower < minChargingPower) {
            requestedPower = minChargingPower;
        }
        return requestedPower;
    }

    if (charger->thingClassId() == hlcDcChargerThingClassId) {
        if (requestedPower > 0) {
            const double maxChargingPower = charger->stateValue(hlcDcChargerMaxChargingPowerStateTypeId).toDouble();
            const double minChargingPower = charger->stateValue(hlcDcChargerMinChargingPowerStateTypeId).toDouble();
            if (maxChargingPower > 0) {
                requestedPower = qMin(requestedPower, maxChargingPower);
            }
            if (requestedPower > 0 && minChargingPower > 0 && requestedPower < minChargingPower) {
                requestedPower = minChargingPower;
            }
            return requestedPower;
        }

        if (requestedPower < 0) {
            const double maxDischargingPower = charger->stateValue(hlcDcChargerMaxDischargingPowerStateTypeId).toDouble();
            const double minDischargingPower = charger->stateValue(hlcDcChargerMinDischargingPowerStateTypeId).toDouble();
            if (maxDischargingPower > 0) {
                requestedPower = qMax(requestedPower, -maxDischargingPower);
            }
            if (minDischargingPower > 0 && qAbs(requestedPower) < minDischargingPower) {
                requestedPower = -minDischargingPower;
            }
        }
    }

    return requestedPower;
}

}


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

    if (thing->thingClassId() == wallboxThingClassId) {
        thing->setProperty(connectedVehiclePropertyName, QString());
        thing->setProperty(legacyConnectedCarPropertyName, QUuid());
        thing->setStateValue("pluggedIn", false);
        thing->setStateValue("charging", false);
        thing->setStateValue("currentPower", 0);
        thing->setStateValue("sessionEnergy", 0);
        thing->setStateValue("totalEnergyProduced", 0);
        clearWallboxPhaseMeasurements(thing);
        thing->setStateValue("connectedVehicleThingId", "");
        thing->setProperty(pendingPhaseSwitchPropertyName, false);
        thing->setStateMaxValue(wallboxMaxChargingCurrentStateTypeId, thing->setting(wallboxSettingsMaxChargingCurrentUpperLimitParamTypeId));

        connect(info->thing(), &Thing::settingChanged, this, [this, thing](const ParamTypeId &settingTypeId, const QVariant &value){
            if (settingTypeId == wallboxSettingsMaxChargingCurrentUpperLimitParamTypeId) {
                thing->setStateMaxValue(wallboxMaxChargingCurrentStateTypeId, value);
            }
            if (settingTypeId == wallboxSettingsPhaseParamTypeId) {
                if (value.toString() == "All") {
                    thing->setStatePossibleValues(wallboxDesiredPhaseCountStateTypeId, {1,3});
                } else {
                    thing->setStatePossibleValues(wallboxDesiredPhaseCountStateTypeId, {1});
                }
            }
            updateSimulation();
        });
    }

    if (thing->thingClassId() == wallboxNoMeterThingClassId) {
        thing->setProperty(connectedVehiclePropertyName, QString());
        thing->setProperty(legacyConnectedCarPropertyName, QUuid());
        thing->setStateValue(wallboxNoMeterPluggedInStateTypeId, false);
        thing->setStateValue(wallboxNoMeterChargingStateTypeId, false);
        thing->setProperty("currentPower", 0);
        thing->setStateValue("connectedVehicleThingId", "");
        thing->setProperty(pendingPhaseSwitchPropertyName, false);
        thing->setStateMaxValue(wallboxNoMeterMaxChargingCurrentStateTypeId, thing->setting(wallboxNoMeterSettingsMaxChargingCurrentUpperLimitParamTypeId));

        connect(info->thing(), &Thing::settingChanged, this, [this, thing](const ParamTypeId &settingTypeId, const QVariant &value) {
            if (settingTypeId == wallboxNoMeterSettingsMaxChargingCurrentUpperLimitParamTypeId) {
                thing->setStateMaxValue(wallboxNoMeterMaxChargingCurrentStateTypeId, value);
            }
            if (settingTypeId == wallboxNoMeterSettingsPhaseParamTypeId) {
                if (value.toString() == "All") {
                    thing->setStatePossibleValues(wallboxNoMeterDesiredPhaseCountStateTypeId, {1, 3});
                } else {
                    thing->setStatePossibleValues(wallboxNoMeterDesiredPhaseCountStateTypeId, {1});
                }
            }
            updateSimulation();
        });
    }

    if (isDcChargerThing(thing)) {
        thing->setStateValue("chargingCapabilities", isHlcChargerThing(thing) ? "bidirectional" : "charging");
        thing->setProperty(pendingHlcVehiclePropertyName, false);
        thing->setProperty(pendingHlcVehicleMacAddressPropertyName, QString());
    }

    if (thing->thingClassId() == stoveThingClassId) {
        // Init property for simulation
        thing->setProperty("simulationActive", false);
    }
    if (thing->thingClassId() == fridgeThingClassId) {
        thing->setProperty("simulationCycle", std::rand() % 360);
    }

    if (isElectricVehicleThing(thing)) {
        syncVehicleBatteryState(thing);

        if (thing->thingClassId() == apiCarThingClassId || thing->thingClassId() == genericCarThingClassId) {
            thing->setStateValue("pluggedIn", false);
            setVehicleChargingState(thing, "idle");
            if (thing->hasState("connectedChargerThingId")) {
                thing->setStateValue("connectedChargerThingId", "");
            }
            thing->setProperty(lastEnergyUpdatePropertyName, QDateTime::currentDateTime());
        }

        if (thing->thingClassId() == genericCarThingClassId || thing->thingClassId() == dcVehicleThingClassId) {
            connect(info->thing(), &Thing::settingChanged, this, [this, thing](const ParamTypeId &settingTypeId, const QVariant &value){
                Q_UNUSED(value)
                if (settingTypeId == genericCarSettingsPhaseCountParamTypeId
                        || settingTypeId == genericCarSettingsCapacityParamTypeId
                        || settingTypeId == dcVehicleSettingsCapacityParamTypeId) {
                    syncVehicleBatteryState(thing);
                }
            });
        }

        if (thing->thingClassId() == hlcVehicleThingClassId) {
            const QString vehicleMacAddress = thing->paramValue(hlcVehicleThingMacAddressParamTypeId).toString();
            qCDebug(dcEnergySimulation()) << "HLC vehicle configured:"
                                          << thing->name()
                                          << "thingId:" << thing->id()
                                          << "macAddress:" << vehicleMacAddress;
            foreach (Thing *charger, myThings().filterByThingClassId(hlcDcChargerThingClassId)) {
                if (charger->property(pendingHlcVehiclePropertyName).toBool()
                        && charger->property(pendingHlcVehicleMacAddressPropertyName).toString() == vehicleMacAddress
                        && !resolveConnectedVehicle(charger)) {
                    qCDebug(dcEnergySimulation()) << "Attaching freshly created HLC vehicle to pending charger:"
                                                  << "vehicleThingId:" << thing->id()
                                                  << "chargerThingId:" << charger->id();
                    attachVehicleToCharger(charger, thing);
                    break;
                }
            }
        }
    }
}


void IntegrationPluginEnergySimulation::thingRemoved(Thing *thing)
{
    if (isElectricVehicleThing(thing)) {
        if (Thing *charger = resolveConnectedCharger(thing)) {
            detachVehicleFromCharger(charger);
        }
        return;
    }

    if (isAcChargerThing(thing) || isDcChargerThing(thing)) {
        detachVehicleFromCharger(thing);
    }
}

void IntegrationPluginEnergySimulation::executeAction(ThingActionInfo *info)
{
    auto connectAcVehicle = [this, info](const ParamTypeId &thingIdParamTypeId) -> bool {
        if (resolveConnectedVehicle(info->thing())) {
            info->finish(Thing::ThingErrorThingInUse, "Wallbox already has a connected vehicle.");
            return false;
        }

        const QString vehicleThingIdValue = info->action().paramValue(thingIdParamTypeId).toString().trimmed();
        Thing *vehicle = nullptr;
        if (vehicleThingIdValue.isEmpty()) {
            vehicle = findFreeAcVehicle();
            if (!vehicle) {
                info->finish(Thing::ThingErrorHardwareNotAvailable, "No free AC-capable vehicle found.");
                return false;
            }
        } else {
            ThingId vehicleThingId(vehicleThingIdValue);
            vehicle = myThings().findById(vehicleThingId);
            if (!isAcChargeableVehicleThing(vehicle)) {
                info->finish(Thing::ThingErrorThingNotFound, "Vehicle thing not found.");
                return false;
            }

            if (!isVehicleAvailable(vehicle)) {
                info->finish(Thing::ThingErrorThingInUse, "Vehicle is already connected to another charger.");
                return false;
            }
        }

        attachVehicleToCharger(info->thing(), vehicle);
        return true;
    };

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
        if (info->action().actionTypeId() == wallboxConnectActionTypeId) {
            info->thing()->setStateValue(wallboxConnectedStateTypeId, true);
        }
        if (info->action().actionTypeId() == wallboxDisconnectActionTypeId) {
            info->thing()->setStateValue(wallboxConnectedStateTypeId, false);
        }
        const auto wallboxConnectVehicleActionType = info->thing()->thingClass().actionTypes().findByName("connectVehicle");
        if (info->action().actionTypeId() == wallboxConnectVehicleActionType.id()) {
            if (!connectAcVehicle(wallboxConnectVehicleActionType.paramTypes().findByName("thingId").id())) {
                return;
            }
        }
        if (info->action().actionTypeId() == info->thing()->thingClass().actionTypes().findByName("disconnectVehicle").id()) {
            detachVehicleFromCharger(info->thing());
        }

        if (info->action().actionTypeId() == wallboxDesiredPhaseCountActionTypeId) {
            uint desiredPhaseCount = info->action().paramValue(wallboxDesiredPhaseCountActionDesiredPhaseCountParamTypeId).toInt();
            qCDebug(dcEnergySimulation()) << "Setting desired phase count to" << desiredPhaseCount;
            schedulePhaseSwitchIfCharging(info->thing(), desiredPhaseCount);
            info->thing()->setStateValue(wallboxDesiredPhaseCountStateTypeId, desiredPhaseCount);
        }
    }

    if (info->thing()->thingClassId() == wallboxNoMeterThingClassId) {
        if (info->action().actionTypeId() == wallboxNoMeterPowerActionTypeId) {
            info->thing()->setStateValue(wallboxNoMeterPowerStateTypeId, info->action().paramValue(wallboxNoMeterPowerActionPowerParamTypeId).toBool());
        }
        if (info->action().actionTypeId() == wallboxNoMeterMaxChargingCurrentActionTypeId) {
            info->thing()->setStateValue(wallboxNoMeterMaxChargingCurrentStateTypeId, info->action().paramValue(wallboxNoMeterMaxChargingCurrentActionMaxChargingCurrentParamTypeId));
        }
        if (info->action().actionTypeId() == wallboxNoMeterConnectActionTypeId) {
            info->thing()->setStateValue(wallboxNoMeterConnectedStateTypeId, true);
        }
        if (info->action().actionTypeId() == wallboxNoMeterDisconnectActionTypeId) {
            info->thing()->setStateValue(wallboxNoMeterConnectedStateTypeId, false);
        }
        const auto wallboxNoMeterConnectVehicleActionType = info->thing()->thingClass().actionTypes().findByName("connectVehicle");
        if (info->action().actionTypeId() == wallboxNoMeterConnectVehicleActionType.id()) {
            if (!connectAcVehicle(wallboxNoMeterConnectVehicleActionType.paramTypes().findByName("thingId").id())) {
                return;
            }
        }
        if (info->action().actionTypeId() == info->thing()->thingClass().actionTypes().findByName("disconnectVehicle").id()) {
            detachVehicleFromCharger(info->thing());
        }
        if (info->action().actionTypeId() == wallboxNoMeterDesiredPhaseCountActionTypeId) {
            uint desiredPhaseCount = info->action().paramValue(wallboxNoMeterDesiredPhaseCountActionDesiredPhaseCountParamTypeId).toInt();
            qCDebug(dcEnergySimulation()) << "Setting desired phase count to" << desiredPhaseCount;
            schedulePhaseSwitchIfCharging(info->thing(), desiredPhaseCount);
            info->thing()->setStateValue(wallboxNoMeterDesiredPhaseCountStateTypeId, desiredPhaseCount);
        }
    }

    if (info->thing()->thingClassId() == dcChargerThingClassId) {
        if (info->action().actionTypeId() == dcChargerConnectActionTypeId) {
            info->thing()->setStateValue("connected", true);
        } else if (info->action().actionTypeId() == dcChargerDisconnectActionTypeId) {
            info->thing()->setStateValue("connected", false);
            info->thing()->setStateValue("currentPower", 0);
            resetVehicleChargingSession(resolveConnectedVehicle(info->thing()));
        } else if (info->action().actionTypeId() == dcChargerPowerActionTypeId) {
            bool powerEnabled = info->action().paramValue(dcChargerPowerActionPowerParamTypeId).toBool();
            info->thing()->setStateValue("power", powerEnabled);
            if (!powerEnabled) {
                info->thing()->setStateValue("currentPower", 0);
                resetVehicleChargingSession(resolveConnectedVehicle(info->thing()));
            }
        } else if (info->action().actionTypeId() == dcChargerChargingPowerActionTypeId) {
            double chargingPower = normalizeChargingPowerSetpoint(info->thing(), info->action().paramValue(dcChargerChargingPowerActionChargingPowerParamTypeId).toDouble());
            info->thing()->setStateValue("chargingPower", chargingPower);
        } else if (info->action().actionTypeId() == dcChargerConnectVehicleActionTypeId) {
            if (resolveConnectedVehicle(info->thing())) {
                info->finish(Thing::ThingErrorThingInUse, "DC charger already has a connected vehicle.");
                return;
            }

            const QString vehicleThingIdValue = info->action().paramValue(dcChargerConnectVehicleActionThingIdParamTypeId).toString().trimmed();
            Thing *vehicle = nullptr;
            if (vehicleThingIdValue.isEmpty()) {
                vehicle = findFreeDcVehicle();
                if (!vehicle) {
                    info->finish(Thing::ThingErrorHardwareNotAvailable, "No free DC-capable vehicle found.");
                    return;
                }
            } else {
                ThingId vehicleThingId(vehicleThingIdValue);
                vehicle = myThings().findById(vehicleThingId);
                if (!isDcChargeableVehicleThing(vehicle)) {
                    info->finish(Thing::ThingErrorThingNotFound, "Vehicle thing not found.");
                    return;
                }

                if (!isVehicleAvailable(vehicle)) {
                    info->finish(Thing::ThingErrorThingInUse, "Vehicle is already connected to another charger.");
                    return;
                }
            }

            attachVehicleToCharger(info->thing(), vehicle);
        } else if (info->action().actionTypeId() == dcChargerDisconnectVehicleActionTypeId) {
            detachVehicleFromCharger(info->thing());
        }
    }

    if (info->thing()->thingClassId() == hlcDcChargerThingClassId) {
        if (info->action().actionTypeId() == hlcDcChargerConnectActionTypeId) {
            info->thing()->setStateValue("connected", true);
        } else if (info->action().actionTypeId() == hlcDcChargerDisconnectActionTypeId) {
            info->thing()->setStateValue("connected", false);
            info->thing()->setStateValue("currentPower", 0);
            resetVehicleChargingSession(resolveConnectedVehicle(info->thing()));
        } else if (info->action().actionTypeId() == hlcDcChargerPowerActionTypeId) {
            bool powerEnabled = info->action().paramValue(hlcDcChargerPowerActionPowerParamTypeId).toBool();
            info->thing()->setStateValue("power", powerEnabled);
            if (!powerEnabled) {
                info->thing()->setStateValue("currentPower", 0);
                resetVehicleChargingSession(resolveConnectedVehicle(info->thing()));
            }
        } else if (info->action().actionTypeId() == hlcDcChargerChargingPowerActionTypeId) {
            double chargingPower = normalizeChargingPowerSetpoint(info->thing(), info->action().paramValue(hlcDcChargerChargingPowerActionChargingPowerParamTypeId).toDouble());
            info->thing()->setStateValue("chargingPower", chargingPower);
        } else if (info->action().actionTypeId() == hlcDcChargerConnectRandomHlcVehicleActionTypeId) {
            if (resolveConnectedVehicle(info->thing())) {
                info->finish(Thing::ThingErrorThingInUse, "HLC charger already has a connected vehicle.");
                return;
            }

            Thing *vehicle = findFreeHlcVehicle();
            if (vehicle) {
                qCDebug(dcEnergySimulation()) << "Reusing free HLC vehicle for charger connection:"
                                              << "vehicleThingId:" << vehicle->id()
                                              << "chargerThingId:" << info->thing()->id();
                attachVehicleToCharger(info->thing(), vehicle);
                info->finish(Thing::ThingErrorNoError);
                return;
            }

            ThingDescriptor descriptor = createHlcVehicleDescriptor();
            const QString macAddress = descriptor.params().paramValue(hlcVehicleThingMacAddressParamTypeId).toString();
            qCDebug(dcEnergySimulation()) << "No reusable HLC vehicle available. Emitting auto thing descriptor:"
                                          << descriptor.title()
                                          << "chargerThingId:" << info->thing()->id()
                                          << "macAddress:" << macAddress
                                          << "Configure the created vehicle using its thingId once setup completes.";
            info->thing()->setProperty(pendingHlcVehiclePropertyName, true);
            info->thing()->setProperty(pendingHlcVehicleMacAddressPropertyName, macAddress);
            emit autoThingsAppeared({descriptor});
            info->finish(Thing::ThingErrorNoError);
            return;
        } else if (info->action().actionTypeId() == hlcDcChargerDisconnectVehicleActionTypeId) {
            detachVehicleFromCharger(info->thing());
        }
    }

    if (info->thing()->thingClassId() == apiCarThingClassId || info->thing()->thingClassId() == genericCarThingClassId) {
        if (info->action().actionTypeId() == apiCarPluggedInActionTypeId || info->action().actionTypeId() == genericCarPluggedInActionTypeId) {
            ParamTypeId pluggedInParamTypeId = info->thing()->thingClass().actionTypes().findByName("pluggedIn").paramTypes().findByName("pluggedIn").id();
            if (info->action().paramValue(pluggedInParamTypeId).toBool()) {
                if (!isVehicleAvailable(info->thing())) {
                    info->finish(Thing::ThingErrorThingInUse, "Vehicle is already connected to another charger.");
                    return;
                }
                if (Thing *charger = findFreeAcCharger()) {
                    attachVehicleToCharger(charger, info->thing());
                    info->finish(Thing::ThingErrorNoError);
                    return;
                }

                info->finish(Thing::ThingErrorHardwareNotAvailable, "No free wallbox found");
                return;
            } else {
                if (Thing *charger = resolveConnectedCharger(info->thing())) {
                    detachVehicleFromCharger(charger);
                }
                info->thing()->setStateValue("pluggedIn", false);
                resetVehicleChargingSession(info->thing());
                info->finish(Thing::ThingErrorNoError);
                return;
            }
        } else if (info->action().actionTypeId() == info->thing()->thingClass().actionTypes().findByName("acMinChargingCurrent").id()) {
            ParamTypeId minChargingCurrentParamTypeId = info->thing()->thingClass().actionTypes().findByName("acMinChargingCurrent").paramTypes().findByName("acMinChargingCurrent").id();
            info->thing()->setStateValue("acMinChargingCurrent", info->action().paramValue(minChargingCurrentParamTypeId));
        } else if (info->action().actionTypeId() == genericCarBatteryLevelActionTypeId) {
            info->thing()->setStateValue(genericCarBatteryLevelStateTypeId, info->action().paramValue(genericCarBatteryLevelActionBatteryLevelParamTypeId).toInt());
            syncVehicleBatteryState(info->thing());
        }
    }

    if (info->thing()->thingClassId() == dcVehicleThingClassId) {
        if (info->action().actionTypeId() == dcVehicleBatteryLevelActionTypeId) {
            info->thing()->setStateValue(dcVehicleBatteryLevelStateTypeId, info->action().paramValue(dcVehicleBatteryLevelActionBatteryLevelParamTypeId).toInt());
            syncVehicleBatteryState(info->thing());
        }
    }
    if (info->thing()->thingClassId() == sgReadyHeatPumpThingClassId) {
        if (info->action().actionTypeId() == sgReadyHeatPumpSgReadyModeActionTypeId) {
            QString operatingMode = info->action().paramValue(sgReadyHeatPumpSgReadyModeActionSgReadyModeParamTypeId).toString();
            info->thing()->setStateValue(sgReadyHeatPumpSgReadyModeStateTypeId, operatingMode);
        }
    } else if (info->thing()->thingClassId() == simpleHeatPumpThingClassId) {
        if (info->action().actionTypeId() == simpleHeatPumpPowerActionTypeId) {
            info->thing()->setStateValue(simpleHeatPumpPowerStateTypeId, info->action().paramValue(simpleHeatPumpPowerActionPowerParamTypeId).toBool());
        }
    } else if (info->thing()->thingClassId() == manualConsumerThingClassId) {
        if (info->action().actionTypeId() == manualConsumerCurrentPowerActionTypeId) {
            info->thing()->setStateValue(manualConsumerCurrentPowerStateTypeId, info->action().paramValue(manualConsumerCurrentPowerActionCurrentPowerParamTypeId).toInt());
        }
    }

    info->finish(Thing::ThingErrorNoError);
}

void IntegrationPluginEnergySimulation::updateSimulation()
{
    qCDebug(dcEnergySimulation()) << "******************* Adjusting simulation" << QDateTime::currentDateTime().toString();

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
            double totalEnergyProduced = inverter->stateValue(solarInverterTotalEnergyProducedStateTypeId).toDouble();
            totalEnergyProduced += (currentProduction / 1000) / 60 / 60 * 5;
            inverter->setStateValue(solarInverterTotalEnergyProducedStateTypeId, totalEnergyProduced);

        } else {
            qCDebug(dcEnergySimulation()) << "* Inverter" << inverter->name() << "production:" << "0" << "W";
            inverter->setStateValue(solarInverterCurrentPowerStateTypeId, 0);
        }
    }

    QDateTime now = QDateTime::currentDateTime();

    // Update AC wallboxes
    foreach (Thing *evCharger, myThings().filterByThingClassId(wallboxThingClassId)) {
        Thing *car = resolveConnectedVehicle(evCharger);
        double effectivePower = 0;
        if (car && evCharger->stateValue(wallboxConnectedStateTypeId).toBool() && evCharger->stateValue(wallboxPowerStateTypeId).toBool()) {
            if (!consumePendingPhaseSwitch(evCharger, car, now)) {
                QDateTime lastEnergyUpdateTime = car->property(lastEnergyUpdatePropertyName).toDateTime();
                if (!lastEnergyUpdateTime.isValid()) {
                    car->setProperty(lastEnergyUpdatePropertyName, now);
                } else {
                    double maxChargingCurrent = evCharger->stateValue(wallboxMaxChargingCurrentStateTypeId).toDouble();
                    uint phaseCount = effectiveAcPhaseCount(evCharger, car);
                    double requestedPower = 230.0 * maxChargingCurrent * phaseCount;
                    double hours = lastEnergyUpdateTime.msecsTo(now) / 1000.0 / 60.0 / 60.0;

                    evCharger->setStateValue(wallboxPhaseCountStateTypeId, phaseCount);
                    effectivePower = updateEvBatteryStateFromElapsedEnergy(car, requestedPower, hours);
                    car->setProperty(lastEnergyUpdatePropertyName, now);
                }
            }
        } else if (car) {
            resetVehicleChargingSession(car, now);
            evCharger->setProperty(pendingPhaseSwitchPropertyName, false);
        }

        evCharger->setStateValue(wallboxChargingStateTypeId, effectivePower > 0);
        evCharger->setStateValue(wallboxCurrentPowerStateTypeId, effectivePower);
        evCharger->setStateValue("totalEnergyProduced", 0);
        if (effectivePower > 0) {
            double chargedEnergy = intervalEnergy(effectivePower);
            evCharger->setStateValue(wallboxTotalEnergyConsumedStateTypeId, evCharger->stateValue(wallboxTotalEnergyConsumedStateTypeId).toDouble() + chargedEnergy);
            evCharger->setStateValue(wallboxSessionEnergyStateTypeId, evCharger->stateValue(wallboxSessionEnergyStateTypeId).toDouble() + chargedEnergy);
            setWallboxPhaseMeasurements(evCharger, effectivePower, acPhaseConnection(evCharger, evCharger->stateValue(wallboxPhaseCountStateTypeId).toUInt()), chargedEnergy);
        } else {
            clearWallboxPhaseMeasurements(evCharger);
        }
    }

    foreach (Thing *evCharger, myThings().filterByThingClassId(wallboxNoMeterThingClassId)) {
        Thing *car = resolveConnectedVehicle(evCharger);
        double effectivePower = 0;
        if (car && evCharger->stateValue(wallboxNoMeterConnectedStateTypeId).toBool() && evCharger->stateValue(wallboxNoMeterPowerStateTypeId).toBool()) {
            if (!consumePendingPhaseSwitch(evCharger, car, now)) {
                QDateTime lastEnergyUpdateTime = car->property(lastEnergyUpdatePropertyName).toDateTime();
                if (!lastEnergyUpdateTime.isValid()) {
                    car->setProperty(lastEnergyUpdatePropertyName, now);
                } else {
                    double maxChargingCurrent = evCharger->stateValue(wallboxNoMeterMaxChargingCurrentStateTypeId).toDouble();
                    uint phaseCount = effectiveAcPhaseCount(evCharger, car);
                    double requestedPower = 230.0 * maxChargingCurrent * phaseCount;
                    double hours = lastEnergyUpdateTime.msecsTo(now) / 1000.0 / 60.0 / 60.0;

                    evCharger->setStateValue(wallboxNoMeterPhaseCountStateTypeId, phaseCount);
                    effectivePower = updateEvBatteryStateFromElapsedEnergy(car, requestedPower, hours);
                    car->setProperty(lastEnergyUpdatePropertyName, now);
                }
            }
        } else if (car) {
            resetVehicleChargingSession(car, now);
            evCharger->setProperty(pendingPhaseSwitchPropertyName, false);
        }

        evCharger->setStateValue(wallboxNoMeterChargingStateTypeId, effectivePower > 0);
        evCharger->setProperty("currentPower", effectivePower);
        if (effectivePower > 0) {
            evCharger->setProperty("totalEnergyConsumed", evCharger->property("totalEnergyConsumed").toDouble() + intervalEnergy(effectivePower));
        }
    }

    // Update DC chargers
    foreach (Thing *dcCharger, myThings().filterByThingClassId(dcChargerThingClassId)) {
        Thing *vehicle = resolveConnectedVehicle(dcCharger);
        double effectivePower = 0;
        if (vehicle && dcCharger->stateValue("connected").toBool() && dcCharger->stateValue("power").toBool()) {
            QDateTime lastEnergyUpdateTime = vehicle->property(lastEnergyUpdatePropertyName).toDateTime();
            if (!lastEnergyUpdateTime.isValid()) {
                vehicle->setProperty(lastEnergyUpdatePropertyName, now);
            } else {
                double requestedPower = normalizeChargingPowerSetpoint(dcCharger, dcCharger->stateValue("chargingPower").toDouble());
                requestedPower = limitVehicleDcChargingPower(vehicle, requestedPower);
                double hours = lastEnergyUpdateTime.msecsTo(now) / 1000.0 / 60.0 / 60.0;
                effectivePower = updateEvBatteryStateFromElapsedEnergy(vehicle, requestedPower, hours);
                vehicle->setProperty(lastEnergyUpdatePropertyName, now);
            }
        } else if (vehicle) {
            resetVehicleChargingSession(vehicle, now);
        }

        dcCharger->setStateValue("currentPower", effectivePower);
        dcCharger->setStateValue("charging", effectivePower != 0);
        if (effectivePower > 0) {
            dcCharger->setStateValue("totalEnergyConsumed", dcCharger->stateValue("totalEnergyConsumed").toDouble() + intervalEnergy(effectivePower));
        }
    }

    // Update bidirectional HLC DC chargers
    foreach (Thing *hlcCharger, myThings().filterByThingClassId(hlcDcChargerThingClassId)) {
        Thing *vehicle = resolveConnectedVehicle(hlcCharger);
        double effectivePower = 0;
        bool sessionActive = vehicle
                && hlcCharger->stateValue("connected").toBool()
                && hlcCharger->stateValue("power").toBool()
                && hlcCharger->stateValue("vehicleIdentified").toBool()
                && hlcCharger->stateValue("hlcSessionActive").toBool();

        if (sessionActive) {
            QDateTime lastEnergyUpdateTime = vehicle->property(lastEnergyUpdatePropertyName).toDateTime();
            if (!lastEnergyUpdateTime.isValid()) {
                vehicle->setProperty(lastEnergyUpdatePropertyName, now);
            } else {
                double requestedPower = normalizeChargingPowerSetpoint(hlcCharger, hlcCharger->stateValue("chargingPower").toDouble());
                requestedPower = limitVehicleDcChargingPower(vehicle, requestedPower);
                double hours = lastEnergyUpdateTime.msecsTo(now) / 1000.0 / 60.0 / 60.0;
                effectivePower = updateEvBatteryStateFromElapsedEnergy(vehicle, requestedPower, hours);
                vehicle->setProperty(lastEnergyUpdatePropertyName, now);
            }
        } else if (vehicle) {
            resetVehicleChargingSession(vehicle, now);
        }

        hlcCharger->setStateValue("currentPower", effectivePower);
        hlcCharger->setStateValue("charging", effectivePower != 0);
        if (effectivePower > 0) {
            hlcCharger->setStateValue("totalEnergyConsumed", hlcCharger->stateValue("totalEnergyConsumed").toDouble() + intervalEnergy(effectivePower));
        } else if (effectivePower < 0) {
            hlcCharger->setStateValue("totalEnergyProduced", hlcCharger->stateValue("totalEnergyProduced").toDouble() - intervalEnergy(effectivePower));
        }
    }

    // Reduce battery level on unplugged cars only.
    foreach (Thing *car, myThings()) {
        if (!isElectricVehicleThing(car)) {
            continue;
        }

        if (car->stateValue("pluggedIn").toBool()) {
            continue;
        }

        if (car->hasState("connectedChargerThingId") && !car->stateValue("connectedChargerThingId").toString().isEmpty()) {
            continue;
        }

        QDateTime lastEnergyUpdateTime = car->property(lastEnergyUpdatePropertyName).toDateTime();
        if (!lastEnergyUpdateTime.isValid()) {
            car->setProperty(lastEnergyUpdatePropertyName, now);
            continue;
        }

        double hours = lastEnergyUpdateTime.msecsTo(now) / 1000.0 / 60.0 / 60.0;
        updateEvBatteryStateFromElapsedEnergy(car, -100.0, hours);
        resetVehicleChargingSession(car, now);
    }

    // Update stove
    foreach (Thing *stove, myThings().filterByThingClassId(stoveThingClassId)) {
        QDateTime now = QDateTime::currentDateTime();

        if (stove->setting(stoveSettingsDailyUsageSimulationParamTypeId).toBool()) {
            // If daily usage simulation is enabled, we use the stove for breakfest, lunch and dinner cooking
            QDateTime breakfestTimeStart = QDateTime(now.date(), QTime::fromString("07:00", "HH:mm"));
            QDateTime breakfestTimeEnd = breakfestTimeStart.addSecs(60 * 10); // Cook for 10 min a coffe

            QDateTime lunchTimeStart = QDateTime(now.date(), QTime::fromString("11:50", "HH:mm"));
            QDateTime lunchTimeEnd = lunchTimeStart.addSecs(60 * 20); // Cook for 20 min

            QDateTime dinnerTimeStart = QDateTime(now.date(), QTime::fromString("18:10", "HH:mm"));
            QDateTime dinnerTimeEnd = dinnerTimeStart.addSecs(60 * 30); // Cook for 30 min

            bool simulationActive = false;
            if (now >= breakfestTimeStart && now < breakfestTimeEnd) {
                qCDebug(dcEnergySimulation()) << "* Stove cooking breakfest until" << breakfestTimeEnd.time().toString();
                simulationActive = true;
            } else if (now >= lunchTimeStart && now < lunchTimeEnd) {
                qCDebug(dcEnergySimulation()) << "* Stove cooking lunch until" << lunchTimeEnd.time().toString();
                simulationActive = true;
            } else if (now >= dinnerTimeStart && now < dinnerTimeEnd) {
                qCDebug(dcEnergySimulation()) << "* Stove cooking dinner until" << dinnerTimeEnd.time().toString();
                simulationActive = true;
            }

            if (stove->property("simulationActive").toBool() != simulationActive) {
                stove->setProperty("simulationActive", simulationActive);
                qCDebug(dcEnergySimulation()) << "Stove simulation changed to" << (simulationActive ? "active" : "inactive");
                stove->setStateValue(stovePowerStateTypeId, simulationActive);
            }
        }

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
        } else {
            stove->setStateValue(stoveCurrentPowerStateTypeId, 0);
        }
    }

    foreach (Thing *fridge, myThings().filterByThingClassId(fridgeThingClassId)) {
        int cycle = fridge->property("simulationCycle").toInt() % 900;
        if (cycle < 360) {
            double maxPower = fridge->setting(fridgeSettingsMaxPowerConsumptionParamTypeId).toDouble();
            double totalEnergyConsumed = fridge->stateValue(fridgeTotalEnergyConsumedStateTypeId).toDouble();
            totalEnergyConsumed += (maxPower / 1000) / 60 / 60 * 5;
            fridge->setStateValue(fridgeCurrentPowerStateTypeId, maxPower);
            fridge->setStateValue(fridgeTotalEnergyConsumedStateTypeId, totalEnergyConsumed);
        } else {
            fridge->setStateValue(fridgeCurrentPowerStateTypeId, 0);
        }
        fridge->setProperty("simulationCycle", cycle + 1);
    }

    // Update heat pumps
    foreach (Thing *heatPump, myThings().filterByThingClassId(sgReadyHeatPumpThingClassId)) {
        QString operatingMode = heatPump->stateValue(sgReadyHeatPumpSgReadyModeStateTypeId).toString();
        QString phase = heatPump->setting(sgReadyHeatPumpSettingsPhaseParamTypeId).toString();
        uint minConsumption = heatPump->setting(sgReadyHeatPumpSettingsMinConsumptionParamTypeId).toUInt();
        uint maxConsumption = heatPump->setting(sgReadyHeatPumpSettingsMaxConsumptionParamTypeId).toUInt();
        double currentPower = 0;
        if (operatingMode == "Off") {
            currentPower = 10  + (std::rand() % 5); // We need some energy since only the pump is off, not the controller
        } else if (operatingMode == "Low") {
            currentPower = minConsumption + (std::rand() % 20);
        } else if (operatingMode == "Standard") {
            // min + 60 % of the max min difference + 20W jitter
            currentPower = minConsumption + (maxConsumption - minConsumption) * 0.6 + (std::rand() % 20);
        } else if (operatingMode == "High") {
            currentPower = maxConsumption + (std::rand() % 20); // 20W jitter
        }

        int cycle = heatPump->property("simulationCycle").toInt() % 12;
        double totalEnergyConsumed = heatPump->stateValue(sgReadyHeatPumpTotalEnergyConsumedStateTypeId).toDouble();
        if (cycle < 4)
            totalEnergyConsumed += (currentPower / 1000) / 60 / 60 * 5;

        heatPump->setProperty("simulationCycle", cycle + 1);

        qCDebug(dcEnergySimulation()) << "* Heatpump" << heatPump->name() << "consumes" << currentPower << "W" << operatingMode << "Energy consumed" << totalEnergyConsumed << "kWh";
        heatPump->setStateValue(sgReadyHeatPumpCurrentPowerStateTypeId, currentPower);
        heatPump->setStateValue(sgReadyHeatPumpTotalEnergyConsumedStateTypeId, totalEnergyConsumed);
    }
    foreach (Thing *heatPump, myThings().filterByThingClassId(simpleHeatPumpThingClassId)) {
        bool heatpumpEnabled = heatPump->stateValue(simpleHeatPumpPowerStateTypeId).toBool();
        QString phase = heatPump->setting(simpleHeatPumpSettingsPhaseParamTypeId).toString();
        //uint minConsumption = heatPump->setting(simpleHeatPumpSettingsMinConsumptionParamTypeId).toUInt();
        uint maxConsumption = heatPump->setting(simpleHeatPumpSettingsMaxConsumptionParamTypeId).toUInt();
        double currentPower = 0;
        if (heatpumpEnabled) {
            currentPower = maxConsumption - (std::rand() % 50);
        } else {
            currentPower = std::rand() % 50;
        }

        int cycle = heatPump->property("simulationCycle").toInt() % 12;
        double totalEnergyConsumed = heatPump->stateValue(simpleHeatPumpTotalEnergyConsumedStateTypeId).toDouble();
        if (cycle < 4)
            totalEnergyConsumed += (currentPower / 1000) / 60 / 60 * 5;

        heatPump->setProperty("simulationCycle", cycle + 1);

        qCDebug(dcEnergySimulation()) << "* Heatpump" << heatPump->name() << "consumes" << currentPower << "W" << "Energy consumed" << totalEnergyConsumed << "kWh";
        heatPump->setStateValue(simpleHeatPumpCurrentPowerStateTypeId, currentPower);
        heatPump->setStateValue(simpleHeatPumpTotalEnergyConsumedStateTypeId, totalEnergyConsumed);
    }


    /////////////////////////////////////////////////////
    /// Energy meter
    ////////////////////////////////////////////////////


    // Sum up production from inverters
    QHash<QString, double> totalPhaseProduction = {
        {"A", 0},
        {"B", 0},
        {"C", 0}
    };
    foreach (Thing *inverter, myThings().filterByThingClassId(solarInverterThingClassId)) {
        addPowerToPhaseTotals(totalPhaseProduction, inverter->setting(solarInverterSettingsPhaseParamTypeId).toString(), inverter->stateValue(solarInverterCurrentPowerStateTypeId).toDouble());
    }


    // Sum up consumption of all consumers
    QHash<QString, double> totalPhasesConsumption = {
        {"A", 0},
        {"B", 0},
        {"C", 0}
    };
    // Simulate a base consumption of 300W (100 on each phase) + 10W jitter
    totalPhasesConsumption["A"] += 100 + (std::rand() % 10);
    totalPhasesConsumption["B"] += 100 + (std::rand() % 10);
    totalPhasesConsumption["C"] += 100 + (std::rand() % 10);

    // And add simulation devices consumption
    foreach (Thing *consumer, myThings()) {
        if (consumer->thingClass().interfaces().contains("smartmeterconsumer")) {
            addPowerToPhaseTotals(totalPhasesConsumption, consumer->setting("phase").toString(), consumer->stateValue("currentPower").toDouble());
        }
    }

    foreach (Thing *evCharger, myThings().filterByThingClassId(wallboxNoMeterThingClassId)) {
        addPowerToPhaseTotals(totalPhasesConsumption, evCharger->setting("phase").toString(), evCharger->property("currentPower").toDouble());
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
            addPowerToPhaseTotals(totalPhasesConsumption, phase, chargedWatts);

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
            addPowerToPhaseTotals(totalPhaseProduction, phase, -returnedWatts);
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
        // First set current power consumptions per phase
        qCDebug(dcEnergySimulation()) << "* Updating smart meter:" << smartMeter->name();
        smartMeter->setStateValue(smartMeterCurrentPowerPhaseAStateTypeId, totalPhasesConsumption["A"] + totalPhaseProduction["A"]);
        smartMeter->setStateValue(smartMeterCurrentPowerPhaseBStateTypeId, totalPhasesConsumption["B"] + totalPhaseProduction["B"]);
        smartMeter->setStateValue(smartMeterCurrentPowerPhaseCStateTypeId, totalPhasesConsumption["C"] + totalPhaseProduction["C"]);

        // Calculate voltage and ampere per phase
        smartMeter->setStateValue(smartMeterVoltagePhaseAStateTypeId, 230);
        smartMeter->setStateValue(smartMeterVoltagePhaseBStateTypeId, 230);
        smartMeter->setStateValue(smartMeterVoltagePhaseCStateTypeId, 230);

        smartMeter->setStateValue(smartMeterCurrentPhaseAStateTypeId, smartMeter->stateValue(smartMeterCurrentPowerPhaseAStateTypeId).toDouble() / 230);
        smartMeter->setStateValue(smartMeterCurrentPhaseBStateTypeId, smartMeter->stateValue(smartMeterCurrentPowerPhaseBStateTypeId).toDouble() / 230);
        smartMeter->setStateValue(smartMeterCurrentPhaseCStateTypeId, smartMeter->stateValue(smartMeterCurrentPowerPhaseCStateTypeId).toDouble() / 230);

        // Lastly set the grand total
        smartMeter->setStateValue(smartMeterCurrentPowerStateTypeId, grandTotal);

        // Add up total consumed/returned
        // Transform current power to kWh for the last 5 secs (simulation interval)
        double consumption = grandTotal / 1000 / 60 / 60 * 5;
        qCDebug(dcEnergySimulation()) << "Total consumption on root meter" << consumption;
        if (grandTotal > 0) {
            smartMeter->setStateValue(smartMeterTotalEnergyConsumedStateTypeId, smartMeter->stateValue(smartMeterTotalEnergyConsumedStateTypeId).toDouble() + consumption);
        } else {
            smartMeter->setStateValue(smartMeterTotalEnergyProducedStateTypeId, smartMeter->stateValue(smartMeterTotalEnergyProducedStateTypeId).toDouble() - consumption);
        }
    }
}

QString IntegrationPluginEnergySimulation::generateRandomMacAddress() const
{
    QStringList parts;
    for (int i = 0; i < 6; i++) {
        int byte = QRandomGenerator::global()->bounded(256);
        if (i == 0) {
            byte = (byte | 0x02) & 0xFE;
        }
        parts.append(QString("%1").arg(byte, 2, 16, QChar('0')).toUpper());
    }
    return parts.join(":");
}

Thing *IntegrationPluginEnergySimulation::findFreeHlcVehicle() const
{
    foreach (Thing *vehicle, myThings().filterByInterface("electricvehiclehlc")) {
        if (isVehicleAvailable(vehicle)) {
            return vehicle;
        }
    }

    return nullptr;
}

Thing *IntegrationPluginEnergySimulation::findFreeDcVehicle() const
{
    foreach (Thing *vehicle, myThings()) {
        if (isDcChargeableVehicleThing(vehicle) && isVehicleAvailable(vehicle)) {
            return vehicle;
        }
    }

    return nullptr;
}

Thing *IntegrationPluginEnergySimulation::findFreeAcVehicle() const
{
    QList<Thing *> candidates;
    foreach (Thing *vehicle, myThings()) {
        if (isAcChargeableVehicleThing(vehicle) && isVehicleAvailable(vehicle)) {
            candidates.append(vehicle);
        }
    }

    std::sort(candidates.begin(), candidates.end(), thingNameLessThan);
    return candidates.isEmpty() ? nullptr : candidates.first();
}

Thing *IntegrationPluginEnergySimulation::findFreeAcCharger() const
{
    QList<Thing *> candidates;
    foreach (Thing *charger, myThings()) {
        if (isAcChargerThing(charger) && !resolveConnectedVehicle(charger)) {
            candidates.append(charger);
        }
    }

    std::sort(candidates.begin(), candidates.end(), thingNameLessThan);
    return candidates.isEmpty() ? nullptr : candidates.first();
}

ThingDescriptor IntegrationPluginEnergySimulation::createHlcVehicleDescriptor() const
{
    const QString macAddress = generateRandomMacAddress();
    ThingDescriptor descriptor(hlcVehicleThingClassId, QString("Simulated HLC vehicle (%1)").arg(macAddress));
    ParamList params;
    params.append(Param(hlcVehicleThingMacAddressParamTypeId, macAddress));
    descriptor.setParams(params);
    qCDebug(dcEnergySimulation()) << "Created HLC vehicle descriptor with MAC address:" << macAddress;
    return descriptor;
}

Thing *IntegrationPluginEnergySimulation::resolveConnectedVehicle(Thing *charger) const
{
    if (!charger) {
        return nullptr;
    }

    QString connectedVehicleThingId = charger->property(connectedVehiclePropertyName).toString();
    if (connectedVehicleThingId.isEmpty() && charger->hasState("connectedVehicleThingId")) {
        connectedVehicleThingId = charger->stateValue("connectedVehicleThingId").toString();
    }
    if (connectedVehicleThingId.isEmpty()
            && (charger->thingClassId() == wallboxThingClassId || charger->thingClassId() == wallboxNoMeterThingClassId)) {
        connectedVehicleThingId = charger->property(legacyConnectedCarPropertyName).toString();
    }

    if (connectedVehicleThingId.isEmpty()) {
        return nullptr;
    }

    return myThings().findById(ThingId(connectedVehicleThingId));
}

Thing *IntegrationPluginEnergySimulation::resolveConnectedCharger(Thing *vehicle) const
{
    if (!vehicle) {
        return nullptr;
    }

    if (vehicle->hasState("connectedChargerThingId")) {
        const QString connectedChargerThingId = vehicle->stateValue("connectedChargerThingId").toString();
        if (!connectedChargerThingId.isEmpty()) {
            if (Thing *charger = myThings().findById(ThingId(connectedChargerThingId))) {
                return charger;
            }
        }
    }

    foreach (Thing *charger, myThings()) {
        if (resolveConnectedVehicle(charger) == vehicle) {
            return charger;
        }
    }

    return nullptr;
}

void IntegrationPluginEnergySimulation::attachVehicleToCharger(Thing *charger, Thing *vehicle)
{
    if (!charger || !vehicle) {
        return;
    }

    charger->setProperty(connectedVehiclePropertyName, vehicle->id().toString());
    charger->setProperty(pendingHlcVehiclePropertyName, false);
    charger->setProperty(pendingHlcVehicleMacAddressPropertyName, QString());
    if (charger->thingClassId() == wallboxThingClassId || charger->thingClassId() == wallboxNoMeterThingClassId) {
        charger->setProperty(legacyConnectedCarPropertyName, vehicle->id());
    }

    if (charger->thingClassId() == wallboxThingClassId) {
        charger->setStateValue(wallboxPluggedInStateTypeId, true);
        charger->setStateValue(wallboxSessionEnergyStateTypeId, 0);
        clearWallboxPhaseMeasurements(charger);
    } else if (charger->thingClassId() == wallboxNoMeterThingClassId) {
        charger->setStateValue(wallboxNoMeterPluggedInStateTypeId, true);
        charger->setProperty("currentPower", 0);
    } else {
        charger->setStateValue("pluggedIn", true);
    }

    if (charger->hasState("connectedVehicleThingId")) {
        charger->setStateValue("connectedVehicleThingId", vehicle->id().toString());
    }
    if (charger->hasState("vehicleIdentified")) {
        charger->setStateValue("vehicleIdentified", isHlcChargerThing(charger));
    }
    if (charger->hasState("hlcSessionActive")) {
        charger->setStateValue("hlcSessionActive", isHlcChargerThing(charger));
    }

    vehicle->setStateValue("pluggedIn", true);
    setVehicleChargingState(vehicle, "idle");
    if (vehicle->hasState("connectedChargerThingId")) {
        vehicle->setStateValue("connectedChargerThingId", charger->id().toString());
    }
    if (vehicle->hasState("hlcSessionActive")) {
        vehicle->setStateValue("hlcSessionActive", isHlcChargerThing(charger));
    }
    vehicle->setProperty(lastEnergyUpdatePropertyName, QDateTime::currentDateTime());

    qCDebug(dcEnergySimulation()) << "Vehicle connected to charger:"
                                  << "vehicleName:" << vehicle->name()
                                  << "vehicleThingId:" << vehicle->id()
                                  << "chargerName:" << charger->name()
                                  << "chargerThingId:" << charger->id();
}

void IntegrationPluginEnergySimulation::detachVehicleFromCharger(Thing *charger)
{
    if (!charger) {
        return;
    }

    Thing *vehicle = resolveConnectedVehicle(charger);

    charger->setProperty(connectedVehiclePropertyName, QString());
    charger->setProperty(pendingHlcVehiclePropertyName, false);
    charger->setProperty(pendingHlcVehicleMacAddressPropertyName, QString());
    charger->setProperty(legacyConnectedCarPropertyName, QUuid());

    if (charger->thingClassId() == wallboxThingClassId) {
        charger->setStateValue(wallboxPluggedInStateTypeId, false);
        charger->setStateValue(wallboxChargingStateTypeId, false);
        charger->setStateValue(wallboxCurrentPowerStateTypeId, 0);
        charger->setStateValue(wallboxSessionEnergyStateTypeId, 0);
        clearWallboxPhaseMeasurements(charger);
    } else if (charger->thingClassId() == wallboxNoMeterThingClassId) {
        charger->setStateValue(wallboxNoMeterPluggedInStateTypeId, false);
        charger->setStateValue(wallboxNoMeterChargingStateTypeId, false);
        charger->setProperty("currentPower", 0);
    } else {
        charger->setStateValue("pluggedIn", false);
        charger->setStateValue("currentPower", 0);
    }

    if (charger->hasState("connectedVehicleThingId")) {
        charger->setStateValue("connectedVehicleThingId", "");
    }
    charger->setProperty(pendingPhaseSwitchPropertyName, false);
    if (charger->hasState("vehicleIdentified")) {
        charger->setStateValue("vehicleIdentified", false);
    }
    if (charger->hasState("hlcSessionActive")) {
        charger->setStateValue("hlcSessionActive", false);
    }

    if (!vehicle) {
        return;
    }

    vehicle->setStateValue("pluggedIn", false);
    setVehicleChargingState(vehicle, "idle");
    if (vehicle->hasState("connectedChargerThingId")) {
        vehicle->setStateValue("connectedChargerThingId", "");
    }
    if (vehicle->hasState("hlcSessionActive")) {
        vehicle->setStateValue("hlcSessionActive", false);
    }
    vehicle->setProperty(lastEnergyUpdatePropertyName, QDateTime::currentDateTime());

    qCDebug(dcEnergySimulation()) << "Vehicle disconnected from charger:"
                                  << "vehicleName:" << vehicle->name()
                                  << "vehicleThingId:" << vehicle->id()
                                  << "chargerName:" << charger->name()
                                  << "chargerThingId:" << charger->id();
}

double IntegrationPluginEnergySimulation::updateEvBatteryStateFromElapsedEnergy(Thing *vehicle, double requestedPower, double hours)
{
    if (!vehicle) {
        return 0;
    }

    syncVehicleCapacityState(vehicle);

    const double capacity = vehicle->stateValue("capacity").toDouble();
    if (capacity <= 0) {
        setVehicleChargingState(vehicle, "idle");
        return 0;
    }

    double storedEnergy = vehicle->property(storedEnergyPropertyName).toDouble();
    if (!vehicle->property(storedEnergyPropertyName).isValid()) {
        storedEnergy = capacity * vehicle->stateValue("batteryLevel").toInt() / 100.0;
    }
    storedEnergy = qBound(0.0, storedEnergy, capacity);

    double effectivePower = requestedPower;
    if (requestedPower > 0 && qFuzzyCompare(storedEnergy + 1.0, capacity + 1.0)) {
        effectivePower = 0;
    } else if (requestedPower < 0 && qFuzzyIsNull(storedEnergy)) {
        effectivePower = 0;
    }

    if (hours > 0) {
        const double requestedEnergy = effectivePower * hours / 1000.0;
        const double updatedEnergy = qBound(0.0, storedEnergy + requestedEnergy, capacity);
        const double effectiveEnergy = updatedEnergy - storedEnergy;
        storedEnergy = updatedEnergy;
        effectivePower = effectiveEnergy * 1000.0 / hours;
    }

    const int batteryLevel = qBound(0, qRound((storedEnergy / capacity) * 100.0), 100);
    vehicle->setStateValue("batteryLevel", batteryLevel);
    vehicle->setStateValue("batteryCritical", batteryLevel < 10);
    if (effectivePower > 0.01) {
        setVehicleChargingState(vehicle, "charging");
    } else if (effectivePower < -0.01) {
        setVehicleChargingState(vehicle, "discharging");
    } else {
        setVehicleChargingState(vehicle, "idle");
    }
    vehicle->setProperty(storedEnergyPropertyName, storedEnergy);

    return effectivePower;
}

void IntegrationPluginEnergySimulation::syncVehicleCapacityState(Thing *vehicle)
{
    if (!vehicle) {
        return;
    }

    if (vehicle->thingClassId() == genericCarThingClassId) {
        vehicle->setStateValue("acPhaseCount", vehicle->setting(genericCarSettingsPhaseCountParamTypeId));
        vehicle->setStateValue(genericCarCapacityStateTypeId, vehicle->setting(genericCarSettingsCapacityParamTypeId));
    } else if (vehicle->thingClassId() == dcVehicleThingClassId) {
        vehicle->setStateValue(dcVehicleCapacityStateTypeId, vehicle->setting(dcVehicleSettingsCapacityParamTypeId));
    }
}

void IntegrationPluginEnergySimulation::syncVehicleBatteryState(Thing *vehicle)
{
    if (!vehicle) {
        return;
    }

    syncVehicleCapacityState(vehicle);

    const int batteryLevel = qBound(0, vehicle->stateValue("batteryLevel").toInt(), 100);
    const double capacity = vehicle->stateValue("capacity").toDouble();

    vehicle->setStateValue("batteryLevel", batteryLevel);
    vehicle->setStateValue("batteryCritical", batteryLevel < 10);
    if (vehicle->hasState("chargingState") && vehicle->stateValue("chargingState").toString().isEmpty()) {
        vehicle->setStateValue("chargingState", "idle");
    }
    if (capacity > 0) {
        vehicle->setProperty(storedEnergyPropertyName, capacity * batteryLevel / 100.0);
    }
    vehicle->setProperty(lastEnergyUpdatePropertyName, QDateTime::currentDateTime());
}

QStringList IntegrationPluginEnergySimulation::phasesForConnection(const QString &phase) const
{
    if (phase == "All" || phase == "ABC") {
        return {"A", "B", "C"};
    }
    if (phase == "AB") {
        return {"A", "B"};
    }
    if (phase == "AC") {
        return {"A", "C"};
    }
    if (phase == "BC") {
        return {"B", "C"};
    }
    if (phase == "A" || phase == "B" || phase == "C") {
        return {phase};
    }

    return {"A"};
}

uint IntegrationPluginEnergySimulation::effectiveAcPhaseCount(Thing *charger, Thing *vehicle) const
{
    if (!charger) {
        return 1;
    }

    uint desiredPhaseCount = 1;
    uint connectedPhaseCount = 1;
    if (charger->thingClassId() == wallboxThingClassId) {
        desiredPhaseCount = charger->stateValue(wallboxDesiredPhaseCountStateTypeId).toUInt();
        connectedPhaseCount = charger->setting(wallboxSettingsPhaseParamTypeId).toString() == "All" ? 3 : 1;
    } else if (charger->thingClassId() == wallboxNoMeterThingClassId) {
        desiredPhaseCount = charger->stateValue(wallboxNoMeterDesiredPhaseCountStateTypeId).toUInt();
        connectedPhaseCount = charger->setting(wallboxNoMeterSettingsPhaseParamTypeId).toString() == "All" ? 3 : 1;
    }

    uint vehiclePhaseCount = 1;
    if (vehicle && vehicle->hasState("acPhaseCount")) {
        vehiclePhaseCount = vehicle->stateValue("acPhaseCount").toUInt();
    }

    return qMax(1u, qMin(desiredPhaseCount, qMin(connectedPhaseCount, vehiclePhaseCount)));
}

QString IntegrationPluginEnergySimulation::acPhaseConnection(Thing *charger, uint phaseCount) const
{
    if (!charger) {
        return "A";
    }

    if (phaseCount >= 3) {
        return "ABC";
    }

    const QString configuredPhase = charger->setting("phase").toString();
    if (configuredPhase == "A" || configuredPhase == "B" || configuredPhase == "C") {
        return configuredPhase;
    }

    return "A";
}

void IntegrationPluginEnergySimulation::setWallboxPhaseMeasurements(Thing *charger, double power, const QString &phaseConnection, double consumedEnergy)
{
    if (!charger || charger->thingClassId() != wallboxThingClassId) {
        return;
    }

    const QStringList phases = phasesForConnection(phaseConnection);
    const double phasePower = phases.isEmpty() ? 0 : power / phases.count();
    const double phaseEnergy = phases.isEmpty() ? 0 : consumedEnergy / phases.count();

    clearWallboxPhaseMeasurements(charger);
    charger->setStateValue("totalEnergyProduced", 0);
    charger->setStateValue("energyProducedPhaseA", 0);
    charger->setStateValue("energyProducedPhaseB", 0);
    charger->setStateValue("energyProducedPhaseC", 0);

    foreach (const QString &phase, phases) {
        charger->setStateValue(QString("currentPowerPhase%1").arg(phase), phasePower);
        charger->setStateValue(QString("currentPhase%1").arg(phase), phasePower / 230.0);
        charger->setStateValue(QString("voltagePhase%1").arg(phase), 230);

        const QString energyStateName = QString("energyConsumedPhase%1").arg(phase);
        charger->setStateValue(energyStateName, charger->stateValue(energyStateName).toDouble() + phaseEnergy);
    }
}

void IntegrationPluginEnergySimulation::clearWallboxPhaseMeasurements(Thing *charger)
{
    if (!charger || charger->thingClassId() != wallboxThingClassId) {
        return;
    }

    charger->setStateValue("currentPowerPhaseA", 0);
    charger->setStateValue("currentPowerPhaseB", 0);
    charger->setStateValue("currentPowerPhaseC", 0);
    charger->setStateValue("currentPhaseA", 0);
    charger->setStateValue("currentPhaseB", 0);
    charger->setStateValue("currentPhaseC", 0);
    charger->setStateValue("voltagePhaseA", 0);
    charger->setStateValue("voltagePhaseB", 0);
    charger->setStateValue("voltagePhaseC", 0);
    charger->setStateValue("totalEnergyProduced", 0);
    charger->setStateValue("energyProducedPhaseA", 0);
    charger->setStateValue("energyProducedPhaseB", 0);
    charger->setStateValue("energyProducedPhaseC", 0);
}

void IntegrationPluginEnergySimulation::schedulePhaseSwitchIfCharging(Thing *charger, uint desiredPhaseCount)
{
    if (!isAcChargerThing(charger)) {
        return;
    }

    uint currentDesiredPhaseCount = 0;
    if (charger->thingClassId() == wallboxThingClassId) {
        currentDesiredPhaseCount = charger->stateValue(wallboxDesiredPhaseCountStateTypeId).toUInt();
    } else if (charger->thingClassId() == wallboxNoMeterThingClassId) {
        currentDesiredPhaseCount = charger->stateValue(wallboxNoMeterDesiredPhaseCountStateTypeId).toUInt();
    }

    if (currentDesiredPhaseCount == desiredPhaseCount) {
        return;
    }

    Thing *vehicle = resolveConnectedVehicle(charger);
    const uint connectedPhaseCount = charger->setting("phase").toString() == "All" ? 3 : 1;
    uint vehiclePhaseCount = 1;
    if (vehicle && vehicle->hasState("acPhaseCount")) {
        vehiclePhaseCount = vehicle->stateValue("acPhaseCount").toUInt();
    }

    const uint currentEffectivePhaseCount = effectiveAcPhaseCount(charger, vehicle);
    const uint requestedEffectivePhaseCount = qMax(1u, qMin(desiredPhaseCount, qMin(connectedPhaseCount, vehiclePhaseCount)));

    if (currentEffectivePhaseCount == requestedEffectivePhaseCount) {
        charger->setProperty(pendingPhaseSwitchPropertyName, false);
        return;
    }

    const bool pendingPhaseSwitch = charger->property(pendingPhaseSwitchPropertyName).toBool();
    bool charging = charger->stateValue(wallboxNoMeterChargingStateTypeId).toBool();
    if (charger->thingClassId() == wallboxThingClassId) {
        charging = charger->stateValue(wallboxChargingStateTypeId).toBool();
    }

    if (pendingPhaseSwitch || charging) {
        charger->setProperty(pendingPhaseSwitchPropertyName, true);
        charger->setProperty(pendingPhaseSwitchTargetPropertyName, desiredPhaseCount);
    }
}

bool IntegrationPluginEnergySimulation::consumePendingPhaseSwitch(Thing *charger, Thing *vehicle, const QDateTime &now)
{
    if (!isAcChargerThing(charger) || !charger->property(pendingPhaseSwitchPropertyName).toBool()) {
        return false;
    }

    qCDebug(dcEnergySimulation()) << "Pausing AC charging for phase switch"
                                  << charger->name()
                                  << "target phase count:" << charger->property(pendingPhaseSwitchTargetPropertyName).toUInt();
    charger->setProperty(pendingPhaseSwitchPropertyName, false);

    if (vehicle) {
        setVehicleChargingState(vehicle, "idle");
        vehicle->setProperty(lastEnergyUpdatePropertyName, now);
    }

    return true;
}

void IntegrationPluginEnergySimulation::addPowerToPhaseTotals(QHash<QString, double> &phaseTotals, const QString &phase, double power) const
{
    const QStringList phases = phasesForConnection(phase);
    const double phasePower = power / phases.count();

    foreach (const QString &connectionPhase, phases) {
        phaseTotals[connectionPhase] += phasePower;
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

    return QPair<QDateTime, QDateTime>(sunrise, sunset);
}
