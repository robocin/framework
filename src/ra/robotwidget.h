/***************************************************************************
 *   Copyright 2015 Michael Bleier, Michael Eischer, Philipp Nordhus       *
 *   Robotics Erlangen e.V.                                                *
 *   http://www.robotics-erlangen.de/                                      *
 *   info@robotics-erlangen.de                                             *
 *                                                                         *
 *   This program is free software: you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation, either version 3 of the License, or     *
 *   any later version.                                                    *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/

#ifndef ROBOTWIDGET_H
#define ROBOTWIDGET_H

#include "protobuf/status.pb.h"
#include <QActionGroup>
#include <QLabel>
#include <QToolButton>
#include <QTimer>

class InputManager;
class GuiTimer;

class RobotWidget : public QWidget
{
    Q_OBJECT

public:
    enum Team { NoTeam, Blue, Yellow, Mixed, PartialBlue, PartialYellow };

public:
    explicit RobotWidget(InputManager *inputManager, bool is_generation, QWidget *parent = 0);
    ~RobotWidget() override;

public:
    void setSpecs(const robot::Specs &specs);

signals:
    void addBinding(uint generation, uint id, const QString &device);
    void removeBinding(uint generation, uint id);
    void strategyControlled(uint generation, uint id, bool strategyControlled);
    void networkControlled(uint generation, uint id, bool networkControlled);
    void teamSelected(uint generation, uint id, RobotWidget::Team team);
    void inputDeviceSelected(uint generation, const QString &inputDevice);
    void ejectSdcard(uint generation, uint id);

public slots:
    void setTeam(uint generation, uint id, RobotWidget::Team team);
    void setInputDevice(uint generation, uint id, const QString &inputDevice);
    void handleResponse(const robot::RadioResponse &response);
    void generationChanged(uint generation, RobotWidget::Team team);

private slots:
    void selectInput();
    void selectInput(const QString &inputDevice);
    void disableInput();
    void setStrategyControlled(bool isControlled);
    void updateInputMenu();
    void selectTeam(QAction *action);
    void selectTeam(Team team);
    void sendEject();

    void updateRobotStatus();
    void hideRobotStatus();

private:
    void addTeamType(const QString &name, const RobotWidget::Team team);
    robot::Specs m_specs;
    bool m_isGeneration;

    QToolButton *m_team;
    QMenu *m_teamMenu;
    QActionGroup *m_teamGroup;
    Team m_teamId;

    QLabel *m_nameLabel;

    robot::RadioResponse m_mergedResponse;
    GuiTimer *m_guiUpdateTimer;
    robot::RadioResponse m_lastResponse;
    GuiTimer *m_guiResponseTimer;
    int m_statusCtr;

    QLabel *m_battery;
    QLabel *m_radio;
    QLabel *m_radioErrors;
    QLabel *m_ball;
    QLabel *m_motorWarning;
    QLabel *m_capCharged;

    InputManager *m_inputManager;
    QToolButton *m_btnControl;
    QMenu *m_inputMenu;
    QActionGroup *m_inputDeviceGroup;
    QString m_inputDevice;
    bool m_strategyControlled;
    QLabel *m_inputLabel;
};

#endif // ROBOTWIDGET_H
