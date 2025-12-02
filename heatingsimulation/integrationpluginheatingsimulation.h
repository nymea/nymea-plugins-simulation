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

#ifndef INTEGRATIONPLUGINHEATINGSIMULATION_H
#define INTEGRATIONPLUGINHEATINGSIMULATION_H

#include "integrations/integrationplugin.h"

#include "extern-plugininfo.h"

#include <QTimer>

class PluginTimer;

class IntegrationPluginHeatingSimulation: public IntegrationPlugin
{
    Q_OBJECT

    Q_PLUGIN_METADATA(IID "io.nymea.IntegrationPlugin" FILE "integrationpluginheatingsimulation.json")
    Q_INTERFACES(IntegrationPlugin)

public:
    explicit IntegrationPluginHeatingSimulation(QObject *parent = nullptr);
    ~IntegrationPluginHeatingSimulation();

    void init() override;
    void startMonitoringAutoThings() override;
    void setupThing(ThingSetupInfo *info) override;
    void thingRemoved(Thing *thing) override;
    void executeAction(ThingActionInfo *info) override;

private slots:
    void onPluginTimer20Seconds();
    void onPluginTimer1Minute();
    void simulationTimerTimeout();

private:
    PluginTimer *m_pluginTimer20Seconds = nullptr;
    PluginTimer *m_pluginTimer1Min = nullptr;
    QHash<Thing*, QTimer*> m_simulationTimers;
};

#endif // INTEGRATIONPLUGINHEATINGSIMULATION_H
