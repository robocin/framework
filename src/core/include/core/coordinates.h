/****************************************************************************
 *   Copyright 2021 Tobias Heineken                                        *
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

#ifndef CORE_COORDINATES.h
#define CORE_COORDINATES.h

#include "vector.h"


namespace core {
    namespace internal {

        template<class T>
        auto set(T& t, float x, float y) -> decltype(t.first = x, void()) {
            t.first = x;
            t.second = y;
        }


        template<class T>
        auto set(T& t, float x, float y) -> decltype(t.x = x, void()) {
            t.x = x;
            t.y = y;
        }
        template<class T>
        auto setPos(T& t, float x, float y) -> decltype(t.set_x(x), void()) {
            t.set_x(x);
            t.set_y(y);
        }
        template<class T>
        auto setPos(T& t, float x, float y) -> decltype(set(t,x,y), void()) {
            set(t,x,y);
        }

        template<class T>
        auto setVel(T& t, float x, float y) -> decltype(set(t,x,y), void()) {
            set(t,x,y);
        }

        template<class T>
        auto setVel(T& t, float x, float y) -> decltype(t.set_v_x(x), void()) {
            t.set_v_x(x);
            t.set_v_y(y);
        }

        template<class T>
        auto get(const T& t, float& x, float& y) -> decltype(x = t.first, void()) {
            x = t.first;
            y = t.second;
        }


        template<class T>
        auto get(const T& t, float& x, float& y) -> decltype(x = t.x, void()) {
            x = t.x;
            y = t.y;
        }


        template<class T>
        auto getPos(const T& t, float& x, float& y) -> decltype(x = t.x(), void()) {
            x = t.x();
            y = t.y();
        }

        template<class T>
        auto getPos(const T& t, float& x, float& y) -> decltype(get(t, x, y)) {
            get(t,x,y);
        }

        template<class T>
        auto getVel(const T& t, float& x, float& y) -> decltype(get(t, x, y)) {
            get(t,x,y);
        }

        template<class T>
        auto getVel(const T& t, float& x, float& y) -> decltype(x = t.v_x(), void()) {
            x = t.v_x();
            y = t.v_y();
        }
    }
}


namespace coordinates {
    template<class F, class T>
    void fromVision(const F& from, T& to) {
        float detectionX, detectionY;
        core::internal::getPos(from, detectionX, detectionY);
        float x = -detectionY / 1000.0f;
        float y = detectionX / 1000.0f;
        core::internal::setPos(to, x, y);
    }

    template<class F, class T>
    void toVision(const F& from, T& to) {
        float x, y;
        core::internal::getPos(from, x, y);
        float visionX, visionY;
        visionX = y * 1000.f;
        visionY = -x * 1000.f;
        core::internal::setPos(to, visionX, visionY);
    }
}

#endif
