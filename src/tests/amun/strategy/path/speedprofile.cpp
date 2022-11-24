/***************************************************************************
 *   Copyright 2020 Andreas Wendler                                        *
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

#include "gtest/gtest.h"
#include <iostream>
#include "path/alphatimetrajectory.h"
#include "core/rng.h"

// tests both SpeedProfile and AlphaTimeTrajectory

static Vector makePos(RNG &rng, float fieldSizeHalf) {
    return rng.uniformVectorIn(Vector(-fieldSizeHalf, -fieldSizeHalf), Vector(fieldSizeHalf, fieldSizeHalf));
}

static Vector makeSpeed(RNG &rng, float maxSpeed) {
    Vector v = rng.uniformVector() * maxSpeed - rng.uniformVector() * maxSpeed;
    while (v.length() >= maxSpeed) {
        v = rng.uniformVector() * maxSpeed - rng.uniformVector() * maxSpeed;
    }
    return v;
}

static void ASSERT_VECTOR_EQ(Vector v1, Vector v2) {
    ASSERT_LE(std::abs(v1.x - v2.x), 0.0001f);
    ASSERT_LE(std::abs(v1.y - v2.y), 0.0001f);
}

// checks without fast endspeed and without slowdown
// v0 and v1 must be slower than maxSpeed
static void checkTrajectorySimple(SpeedProfile trajectory, Vector v0, Vector v1, float acc, bool fastEndSpeed) {

    // check start speed
    ASSERT_VECTOR_EQ(trajectory.positionAndSpeedForTime(0).speed, v0);

    // check end speed
    if (!fastEndSpeed) {
        ASSERT_VECTOR_EQ(trajectory.positionAndSpeedForTime(trajectory.time()).speed, v1);
        ASSERT_VECTOR_EQ(trajectory.endSpeed(), v1);
    } else {
        ASSERT_LE(trajectory.endSpeed().length(), v1.length());
    }

    // TODO: test that end pos calculation matches

    const int SEGMENTS = 100;
    const float timeDiff = trajectory.time() / float(SEGMENTS - 1);
    const std::vector<RobotState> bulkPositions = trajectory.trajectoryPositions(SEGMENTS, timeDiff);


    Vector lastPos = trajectory.positionAndSpeedForTime(0).pos;
    Vector lastSpeed = trajectory.positionAndSpeedForTime(0).speed;
    for (int i = 0;i<SEGMENTS;i++) {
        const float time = i * timeDiff;
        const auto state = trajectory.positionAndSpeedForTime(time);
        const Vector speed = state.speed;

        ASSERT_LE((bulkPositions[i].pos).distance(state.pos), 0.01);
        ASSERT_LE((bulkPositions[i].speed).distance(state.speed), 0.01);

        // check acceleration limit
        const float diff = speed.distance(lastSpeed) / timeDiff;
        ASSERT_LE(diff, acc * 1.01f); // something extra for floating point


        // check if position is continuous
        const float posDiff = lastPos.distance(state.pos);
        if (posDiff > 0.001f) {
            ASSERT_LE(posDiff / timeDiff, std::max(lastSpeed.length(), speed.length()) * 1.2f);
        }

        lastSpeed = speed;
        lastPos = state.pos;
    }
}

static void checkMaxSpeed(SpeedProfile trajectory, float maxSpeed) {

    const float MAX_FASTER_FACTOR = std::sqrt(2);

    const int SEGMENTS = 100;
    for (int i = 0;i<SEGMENTS;i++) {
        const float time = i * trajectory.time() / float(SEGMENTS - 1);
        const Vector speed = trajectory.positionAndSpeedForTime(time).speed;

        ASSERT_LE(speed.length(), maxSpeed * MAX_FASTER_FACTOR);
    }
}

static void checkBoundingBox(SpeedProfile trajectory) {
    const auto manyPositions = trajectory.trajectoryPositions(1000, trajectory.time() / 999);
    BoundingBox fromPoints(manyPositions[0].pos, manyPositions[0].pos);
    for (const auto &p : manyPositions) {
        fromPoints.mergePoint(p.pos);
    }

    const BoundingBox direct = trajectory.calculateBoundingBox();
    ASSERT_LE(std::abs(fromPoints.left - direct.left), 0.01f);
    ASSERT_LE(std::abs(fromPoints.right - direct.right), 0.01f);
    ASSERT_LE(std::abs(fromPoints.top - direct.top), 0.01f);
    ASSERT_LE(std::abs(fromPoints.bottom - direct.bottom), 0.01f);
}

static void checkEndPosition(SpeedProfile trajectory, Vector expected) {
    const Vector endPos = trajectory.endPosition();
    ASSERT_VECTOR_EQ(endPos, expected);

    const float offset = 1e-6;
    const float time = trajectory.time();
    const auto closeToEnd = trajectory.positionAndSpeedForTime(time - offset);
    ASSERT_LE(closeToEnd.pos.distance(expected - closeToEnd.speed * offset), 0.001);
}

static void checkLimitToTime(const SpeedProfile profile, RNG &rng) {
    const float timeLimit = rng.uniformFloat(profile.time() * 0.1f, profile.time());
    SpeedProfile limited = profile;
    limited.limitToTime(timeLimit);
    ASSERT_FLOAT_EQ(limited.time(), timeLimit);
    for (int i = 0;i<100;i++) {
        const float t = i * timeLimit / 99.0f;
        const auto sp1 = profile.positionAndSpeedForTime(t);
        const auto sp2 = limited.positionAndSpeedForTime(t);

        ASSERT_VECTOR_EQ(sp1.pos, sp2.pos);
        ASSERT_VECTOR_EQ(sp1.speed, sp2.speed);
    }
}

static void checkDistanceIncrease(const Vector v0, const float time, const float maxSpeed, const float acc, const float angle) {

    // more time must result in more distance traveled
    const SpeedProfile p1 = AlphaTimeTrajectory::calculateTrajectory(RobotState(Vector(0, 0), v0), Vector(0, 0), time, angle, acc, maxSpeed, 0, false);
    const SpeedProfile p2 = AlphaTimeTrajectory::calculateTrajectory(RobotState(Vector(0, 0), v0), Vector(0, 0), time + 0.1, angle, acc, maxSpeed, 0, false);
    const SpeedProfile p3 = AlphaTimeTrajectory::calculateTrajectory(RobotState(Vector(0, 0), v0), Vector(0, 0), time + 0.2, angle, acc, maxSpeed, 0, false);

    ASSERT_LT((p2.endPosition() - p1.endPosition()).length(), (p3.endPosition() - p1.endPosition()).length());
}

static void checkBasic(RNG &rng, const SpeedProfile &profile, const Vector v0, const Vector v1, const float maxSpeed, const float acc, const float slowDownTime, const bool fastEndSpeed) {
    checkTrajectorySimple(profile, v0, v1, acc, fastEndSpeed);
    checkBoundingBox(profile);
    checkMaxSpeed(profile, maxSpeed);
    if (slowDownTime == 0) {
        checkLimitToTime(profile, rng);
    }
}

TEST(AlphaTimeTrajectory, calculateTrajectory) {
    RNG rng(1);

    for (int i = 0;i<10000;i++) {

        const float maxSpeed = rng.uniformFloat(0.3, 5);

        const Vector v0 = makeSpeed(rng, maxSpeed);
        const Vector v1 = rng.uniform() > 0.9 ? Vector(0, 0) : makeSpeed(rng, maxSpeed);
        const float time = rng.uniformFloat(0.005, 5);
        const float angle = rng.uniformFloat(0, 2 * M_PI);
        const float acc = rng.uniformFloat(0.5, 4);
        const float slowDown = rng.uniform() > 0.5 ? rng.uniformFloat(0, SpeedProfile::SLOW_DOWN_TIME) : 0;
        const bool fastEndSpeed = rng.uniform() > 0.5;

        const auto profile = AlphaTimeTrajectory::calculateTrajectory(RobotState(Vector(1, 2), v0), v1, time, angle, acc, maxSpeed, slowDown, fastEndSpeed);

        // generic checks
        checkBasic(rng, profile, v0, v1, maxSpeed, acc, slowDown, fastEndSpeed);
        checkDistanceIncrease(v0, time, maxSpeed, acc, angle);
    }
}

TEST(AlphaTimeTrajectory, findTrajectory) {
    constexpr int RUNS = 10'000;

    RNG rng(2);

    int fails = 0;
    for (int i = 0; i < RUNS; i++) {
        const float maxSpeed = rng.uniformFloat(0.3, 5);

        const Vector s0 = makePos(rng, 2);
        const Vector v0 = makeSpeed(rng, maxSpeed);
        const Vector s1 = rng.uniform() > 0.9 ? makePos(rng, 5) : (s0 + makePos(rng, 0.1));
        const Vector v1 = rng.uniform() > 0.9 ? Vector(0, 0) : makeSpeed(rng, maxSpeed);

        const float acc = rng.uniformFloat(0.5, 4);
        const float slowDownTime = rng.uniform() > 0.5 ? rng.uniformFloat(0, SpeedProfile::SLOW_DOWN_TIME) : 0;
        const bool highPrecision = (rng.uniform() > 0.5);
        const bool fastEndSpeed = rng.uniform() > 0.5;

        const auto profileOpt = AlphaTimeTrajectory::findTrajectory(RobotState(s0, v0), RobotState(s1, v1), acc, maxSpeed, slowDownTime, highPrecision, fastEndSpeed);
        if (!profileOpt) {
            fails += 1;
            continue;
        }
        const auto profile = profileOpt.value();

        // generic checks
        checkBasic(rng, profile, v0, v1, maxSpeed, acc, slowDownTime, fastEndSpeed);
        checkEndPosition(profile, s1);
    }

    ASSERT_LT((float)fails / RUNS, 0.01f);
}

// TODO: test total time
