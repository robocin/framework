/***************************************************************************
 *   Copyright 2018 Andreas Wendler                                        *
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

#include "robot.h"

void robotSetDefault(robot::Specs *specs)
{
    specs->set_radius(0.088f);
    specs->set_height(0.148f);
    specs->set_generation(3);
    specs->set_year(2021);
    specs->set_id(0);
    specs->set_type(robot::Specs_GenerationType_Regular);
    specs->set_mass(2.45f);

    float ratio = 0.0756 / 0.088;

    specs->set_angle(2 * acosf(ratio));
    specs->set_v_max(3.7f);
    specs->set_omega_max(26.2);
    specs->set_shot_linear_max(10);
    
    float angle = 45.f / 180 * M_PI;
    float dirFloor = std::cos(angle);
    float dirUp = std::sin(angle);
    float gravity = 9.81;
    float targetDist = 2 * 8 * 8 * dirFloor * dirUp / gravity;

    specs->set_shot_chip_max(targetDist);
    specs->set_dribbler_width(0.072f);
    specs->set_ir_param(40);
    specs->set_shoot_radius(0.0669f);
    specs->set_dribbler_height(0.04f);

    robot::LimitParameters *acceleration = specs->mutable_acceleration();
    
    acceleration->set_a_speedup_f_max(4.7f);
    acceleration->set_a_speedup_s_max(4.7f);
    acceleration->set_a_speedup_phi_max(50.8f);
    acceleration->set_a_brake_f_max(5.9);
    acceleration->set_a_brake_s_max(5.9);
    acceleration->set_a_brake_phi_max(51.6);

    robot::LimitParameters *strategy = specs->mutable_strategy();
    strategy->set_a_speedup_f_max(4.9f);
    strategy->set_a_speedup_s_max(4.9f);
    strategy->set_a_speedup_phi_max(55);
    strategy->set_a_brake_f_max(6);
    strategy->set_a_brake_s_max(6);
    strategy->set_a_brake_phi_max(55);
}
