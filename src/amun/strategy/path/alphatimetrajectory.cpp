/***************************************************************************
 *   Copyright 2019 Andreas Wendler                                        *
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

#include "alphatimetrajectory.h"
#include <QDebug>

// helper functions
static float sign(float x)
{
    return x < 0.0f ? -1.0f : 1.0f;
}

static float normalizeAnglePositive(float angle)
{
    while (angle < 0) angle += float(2 * M_PI);
    while (angle >= float(2 * M_PI)) angle -= float(2 * M_PI);
    return angle;
}

static float adjustAngle(Vector startSpeed, Vector endSpeed, float time, float angle, float acc)
{
    // case 1: only startSpeed.x is != 0
    // this results in 2 cases where the angle is invalid, in a range around 0 and 180 degree
    // both ranges have the same size
    // therefore, calculate the size.
    // Calculate the smallest angle x in [0, 2 * pi] that is possible:
    // time - |startSpeed.x| / |acc * sin(x)| = 0 => time - |startSpeed.x| / (acc * sin(x)) = 0
    // => acc * sin(x) * time = |startSpeed.x| => sin(x) = |startSpeed.x| / (time * acc)
    // => x = sin^-1(|startSpeed.x| / (time * acc))
    // WARNING: only solvable when |startSpeed.x| <= time
    // -> this also applies to all other cases, see below
    Vector diff = endSpeed - startSpeed;
    Vector absDiff(std::abs(diff.x), std::abs(diff.y));
    if (absDiff.x > time * acc || absDiff.y > time * acc) {
        // TODO: the trajectory is not solvable
        return angle;
    }
    // offset to ensure that values directly on the border of an invalid segment are not treated as invalid later
    const float FLOATING_POINT_OFFSET = 0.001f;
    float gapSizeHalfX = std::asin(absDiff.x / (time * acc)) + FLOATING_POINT_OFFSET;
    // solution gaps are now [-fS, fS] and [pi - fS, pi + fS]
    float gapSizeHalfY = std::asin(absDiff.y / (time * acc)) + FLOATING_POINT_OFFSET;

    float circleCircumference = float(2 * M_PI) - gapSizeHalfX * 4 - gapSizeHalfY * 4;
    float circumferenceFactor = circleCircumference / float(2 * M_PI);
    angle = normalizeAnglePositive(angle);
    angle *= circumferenceFactor;

    angle += gapSizeHalfX;
    if (angle > float(M_PI / 2) - gapSizeHalfY) {
        angle += gapSizeHalfY * 2.0f;
    }
    if (angle > float(M_PI) - gapSizeHalfX) {
        angle += gapSizeHalfX * 2.0f;
    }
    if (angle > float(M_PI * 1.5) - gapSizeHalfY) {
        angle += gapSizeHalfY * 2.0f;
    }
    return angle;
}

static float adjustAngleFastEndSpeed(Vector startSpeed, Vector endSpeed, float time, float angle, float acc)
{
    // use endspeed as the closest value of startSpeed on [0, endSpeed]
    float endSpeedX = std::max(std::min(startSpeed.x, std::max(endSpeed.x, 0.0f)), std::min(endSpeed.x, 0.0f));
    float endSpeedY = std::max(std::min(startSpeed.y, std::max(endSpeed.y, 0.0f)), std::min(endSpeed.y, 0.0f));
    return adjustAngle(startSpeed, Vector(endSpeedX, endSpeedY), time, angle, acc);
}

float AlphaTimeTrajectory::minTimeExactEndSpeed(Vector v0, Vector v1, float acc)
{
    Vector diff = v1 - v0;
    return diff.length() / acc;
}

float AlphaTimeTrajectory::minTimeFastEndSpeed(Vector v0, Vector v1, float acc)
{
    float endSpeedX = std::max(std::min(v0.x, std::max(v1.x, 0.0f)), std::min(v1.x, 0.0f));
    float endSpeedY = std::max(std::min(v0.y, std::max(v1.y, 0.0f)), std::min(v1.y, 0.0f));
    return minTimeExactEndSpeed(v0, Vector(endSpeedX, endSpeedY), acc);
}




AlphaTimeTrajectory::TrajectoryPosInfo2D AlphaTimeTrajectory::calculatePositionFastEndSpeed(Vector v0, Vector v1, float time, float angle, float acc, float vMax)
{
    angle = adjustAngleFastEndSpeed(v0, v1, time, angle, acc);
    float alphaX = std::sin(angle);
    float alphaY = std::cos(angle);

    auto xInfo = SpeedProfile1D::calculateEndPos1DFastSpeed(v0.x, v1.x, time, alphaX > 0, acc * std::abs(alphaX), vMax * std::abs(alphaX));
    auto yInfo = SpeedProfile1D::calculateEndPos1DFastSpeed(v0.y, v1.y, time, alphaY > 0, acc * std::abs(alphaY), vMax * std::abs(alphaY));
    return {Vector(xInfo.endPos, yInfo.endPos), Vector(xInfo.increaseAtSpeed, yInfo.increaseAtSpeed)};
}

AlphaTimeTrajectory::TrajectoryPosInfo2D AlphaTimeTrajectory::calculatePositionExactEndSpeed(Vector v0, Vector v1, float time, float angle, float acc, float vMax)
{
    angle = adjustAngle(v0, v1, time, angle, acc);
    float alphaX = std::sin(angle);
    float alphaY = std::cos(angle);

    Vector diff = v1 - v0;
    float restTimeX = (time - std::abs(diff.x) / (acc * std::abs(alphaX)));
    float restTimeY = (time - std::abs(diff.y) / (acc * std::abs(alphaY)));

    // calculate position for x and y
    auto xInfo = SpeedProfile1D::calculateEndPos1D(v0.x, v1.x, sign(alphaX) * restTimeX, acc * std::abs(alphaX), vMax * std::abs(alphaX));
    auto yInfo = SpeedProfile1D::calculateEndPos1D(v0.y, v1.y, sign(alphaY) * restTimeY, acc * std::abs(alphaY), vMax * std::abs(alphaY));
    return {Vector(xInfo.endPos, yInfo.endPos), Vector(xInfo.increaseAtSpeed, yInfo.increaseAtSpeed)};
}

SpeedProfile AlphaTimeTrajectory::calculateTrajectoryFastEndSpeed(Vector v0, Vector v1, float time, float angle, float acc, float vMax, float minTime)
{
    if (minTime < 0) {
        minTime = minTimeFastEndSpeed(v0, v1, acc);
    }
    time += minTime;

    angle = adjustAngleFastEndSpeed(v0, v1, time, angle, acc);
    float alphaX = std::sin(angle);
    float alphaY = std::cos(angle);

    SpeedProfile result;
    result.xProfile.calculate1DTrajectoryFastEndSpeed(v0.x, v1.x, time, alphaX > 0, acc * std::abs(alphaX), vMax * std::abs(alphaX));
    result.yProfile.calculate1DTrajectoryFastEndSpeed(v0.y, v1.y, time, alphaY > 0, acc * std::abs(alphaY), vMax * std::abs(alphaY));
    result.xProfile.integrateTime();
    result.yProfile.integrateTime();
    return result;
}

SpeedProfile AlphaTimeTrajectory::calculateTrajectoryExactEndSpeed(Vector v0, Vector v1, float time, float angle, float acc, float vMax, float minTime)
{
    if (minTime < 0) {
        minTime = minTimeExactEndSpeed(v0, v1, acc);
    }
    time += minTime;

    angle = adjustAngle(v0, v1, time, angle, acc);
    float alphaX = std::sin(angle);
    float alphaY = std::cos(angle);

    Vector diff = v1 - v0;
    float restTimeX = (time - std::abs(diff.x) / (acc * std::abs(alphaX)));
    float restTimeY = (time - std::abs(diff.y) / (acc * std::abs(alphaY)));

    SpeedProfile result;
    result.xProfile.calculate1DTrajectory(v0.x, v1.x, alphaX > 0 ? restTimeX : -restTimeX, acc * std::abs(alphaX), vMax * std::abs(alphaX));
    result.yProfile.calculate1DTrajectory(v0.y, v1.y, alphaY > 0 ? restTimeY : -restTimeY, acc * std::abs(alphaY), vMax * std::abs(alphaY));
    result.xProfile.integrateTime();
    result.yProfile.integrateTime();
    return result;
}

// functions for position search
static Vector fastEndSpeedCenterTimePos(Vector startSpeed, Vector endSpeed, float time)
{
    float endSpeedX = std::max(std::min(startSpeed.x, std::max(endSpeed.x, 0.0f)), std::min(endSpeed.x, 0.0f));
    float endSpeedY = std::max(std::min(startSpeed.y, std::max(endSpeed.y, 0.0f)), std::min(endSpeed.y, 0.0f));
    return (startSpeed + Vector(endSpeedX, endSpeedY)) * (0.5f * time);
}

static Vector centerTimePos(Vector startSpeed, Vector endSpeed, float time)
{
    return (startSpeed + endSpeed) * (0.5f * time);
}

Vector AlphaTimeTrajectory::minTimePos(Vector v0, Vector v1, float acc, float slowDownTime)
{
    float minTime = minTimeExactEndSpeed(v0, v1, acc);
    if (slowDownTime == 0.0f) {
        return (v0 + v1) * (minTime * 0.5f);
    } else {
        // construct speed profile for slowing down to zero
        SpeedProfile profile;
        profile.xProfile.counter = 2;
        profile.xProfile.profile[0] = {v0.x, 0};
        profile.xProfile.profile[1] = {v1.x, minTime};
        profile.xProfile.acc = std::abs(v0.x - v1.x) / minTime;

        profile.yProfile.counter = 2;
        profile.yProfile.profile[0] = {v0.y, 0};
        profile.yProfile.profile[1] = {v1.y, minTime};
        profile.yProfile.acc = std::abs(v0.y - v1.y) / minTime;

        return profile.calculateSlowDownPos(slowDownTime);
    }
}

// normalize between [-pi, pi]
static float angleDiff(float a1, float a2)
{
    float angle = a1 - a2;
    while (angle < -float(M_PI)) angle += float(2 * M_PI);
    while (angle >= float(M_PI)) angle -= float(2 * M_PI);
    return angle;
}

SpeedProfile AlphaTimeTrajectory::findTrajectoryFastEndSpeed(Vector v0, Vector v1, Vector position, float acc, float vMax, float slowDownTime, bool highPrecision)
{
    if (v1.x == 0.0f && v1.y == 0.0f) {
        return findTrajectoryExactEndSpeed(v0, v1, position, acc, vMax, slowDownTime, highPrecision);
    }
    SpeedProfile result;
    // TODO: custom minTimePos for fast endspeed mode
    float minTimeDistance = position.distance(minTimePos(v0, v1, acc, 0));

    // estimate rough time from distance
    // TODO: improve this estimate?
    float estimatedTime = minTimeDistance / acc;

    Vector estimateCenterPos = fastEndSpeedCenterTimePos(v0, v1, estimatedTime);

    float estimatedAngle = normalizeAnglePositive((position - estimateCenterPos).angle());
    // calculate better estimate for the time
    estimatedTime = std::max(estimatedTime, 0.001f);

    // TODO: can this even still occur??
    if (std::isnan(estimatedTime)) {
        estimatedTime = 3;
    }
    if (std::isnan(estimatedAngle)) {
        // 0 is floating point instable, dont use that
        estimatedAngle = 0.05f;
    }

    // cache for usage in calculateTrajectoryFastEndSpeed
    float minimumTime = minTimeFastEndSpeed(v0, v1, acc);

    float currentTime = estimatedTime;
    float currentAngle = estimatedAngle;

    float distanceFactor = 0.8f;
    float lastCenterDistanceDiff = 0;

    float angleFactor = 0.8f;
    float lastAngleDiff = 0;

    const int ITERATIONS = highPrecision ? HIGH_PRECISION_ITERATIONS : MAX_SEARCH_ITERATIONS;
    for (int i = 0;i<ITERATIONS;i++) {
        currentTime = std::max(currentTime, 0.0f);

        Vector endPos;
        float assumedSpeed;
        if (slowDownTime > 0) {
            result = calculateTrajectoryFastEndSpeed(v0, v1, currentTime, currentAngle, acc, vMax, minimumTime);
            endPos = result.calculateSlowDownPos(slowDownTime);
            Vector continuationSpeed = result.continuationSpeed();
            assumedSpeed = std::max(std::abs(continuationSpeed.x), std::abs(continuationSpeed.y));
        } else {
            auto trajectoryInfo = calculatePositionFastEndSpeed(v0, v1, currentTime + minimumTime, currentAngle, acc, vMax);
            endPos = trajectoryInfo.endPos;
            assumedSpeed = std::max(std::abs(trajectoryInfo.increaseAtSpeed.x), std::abs(trajectoryInfo.increaseAtSpeed.y));
        }

        float targetDistance = position.distance(endPos);
        if (targetDistance < (highPrecision ? HIGH_QUALITY_TARGET_PRECISION : REGULAR_TARGET_PRECISION)) {
            if (slowDownTime <= 0) {
                result = calculateTrajectoryFastEndSpeed(v0, v1, currentTime, currentAngle, acc, vMax, minimumTime);
            }
            return result;
        }

        Vector currentCenterTimePos = fastEndSpeedCenterTimePos(v0, v1, currentTime + minimumTime);
        float newDistance = endPos.distance(currentCenterTimePos);
        float targetCenterDistance = currentCenterTimePos.distance(position);
        float currentCenterDistanceDiff = targetCenterDistance - newDistance;
        if ((lastCenterDistanceDiff < 0) != (currentCenterDistanceDiff < 0)) {
            distanceFactor *= 0.9f;
        } else {
            distanceFactor *= 1.05f;
        }
        lastCenterDistanceDiff = currentCenterDistanceDiff;
        currentTime += currentCenterDistanceDiff * distanceFactor / std::max(0.5f, assumedSpeed);

        // correct angle
        float newAngle = (endPos - currentCenterTimePos).angle();
        float targetCenterAngle = (position - currentCenterTimePos).angle();
        float currentAngleDiff = angleDiff(targetCenterAngle, newAngle);
        if (i >= 4 && (currentAngleDiff < 0) != (lastAngleDiff < 0)) {
            angleFactor *= 0.5f;
        }
        lastAngleDiff = currentAngleDiff;
        currentAngle += currentAngleDiff * angleFactor;
        //currentAngle += vectorAngleDiff((position - currentCenterTimePos), (endPos - currentCenterTimePos));
    }
    result.valid = false;
    return result;
}

static Vector necessaryAcceleration(Vector v0, Vector distance)
{
    // solve dist(v0, 0) == d
    // 0.5 * v0 * abs(v0) / acc = d
    // acc = 0.5 * v0 * abs(v0) / d = acc
    return Vector(v0.x * std::abs(v0.x) * 0.5f / distance.x,
                  v0.y * std::abs(v0.y) * 0.5f / distance.y);
}

SpeedProfile AlphaTimeTrajectory::findTrajectoryExactEndSpeed(Vector v0, Vector v1, Vector position, float acc, float vMax, float slowDownTime, bool highPrecision)
{
    const float MAX_ACCELERATION_FACTOR = 1.2f;
    SpeedProfile result;
    if (v1 == Vector(0, 0)) {
        Vector necessaryAcc = necessaryAcceleration(v0, position);
        float accLength = necessaryAcc.length();
        float timeDiff = std::abs(std::abs(v0.x) / necessaryAcc.x - std::abs(v0.y) / necessaryAcc.y);
        if (accLength > acc && accLength < acc * MAX_ACCELERATION_FACTOR && timeDiff < 0.1f) {
            result.valid = true;
            result.xProfile.acc = necessaryAcc.x;
            result.xProfile.counter = 2;
            result.xProfile.profile[0] = {v0.x, 0};
            result.xProfile.profile[1] = {0, std::abs(v0.x / necessaryAcc.x)};
            result.yProfile.acc = necessaryAcc.y;
            result.yProfile.counter = 2;
            result.yProfile.profile[0] = {v0.y, 0};
            result.yProfile.profile[1] = {0, std::abs(v0.y / necessaryAcc.y)};
            return result;
        }
    }

    Vector minPos = minTimePos(v0, v1, acc, slowDownTime);
    float minTimeDistance = position.distance(minPos);

    const bool useMinTimePosForCenterPos = minTimeDistance < 0.1f;

    // estimate rough time from distance
    // TODO: improve this estimate?
    float estimatedTime = minTimeDistance / acc;

    Vector estimateCenterPos = centerTimePos(v0, v1, estimatedTime);

    float estimatedAngle = normalizeAnglePositive((position - estimateCenterPos).angle());
    // calculate better estimate for the time
    estimatedTime = std::max(estimatedTime, 0.01f);

    // TODO: can this even still occur??
    if (std::isnan(estimatedTime)) {
        estimatedTime = 3;
    }
    if (std::isnan(estimatedAngle)) {
        // 0 is floating point instable, dont use that
        estimatedAngle = 0.05f;
    }

    // cached for usage in calculateTrajectoryExactEndSpeed
    float minimumTime = minTimeExactEndSpeed(v0, v1, acc);

    float currentTime = estimatedTime;
    float currentAngle = estimatedAngle;

    float distanceFactor = 0.8f;
    float lastCenterDistanceDiff = 0;

    float angleFactor = 0.8f;
    float lastAngleDiff = 0;

    const int ITERATIONS = highPrecision ? HIGH_PRECISION_ITERATIONS : MAX_SEARCH_ITERATIONS;
    for (int i = 0;i<ITERATIONS;i++) {
        currentTime = std::max(currentTime, 0.0f);

        Vector endPos;
        float assumedSpeed;
        if (slowDownTime > 0) {
            result = calculateTrajectoryExactEndSpeed(v0, v1, currentTime, currentAngle, acc, vMax, minimumTime);
            endPos = result.calculateSlowDownPos(slowDownTime);
            Vector continuationSpeed = result.continuationSpeed();
            assumedSpeed = std::max(std::abs(continuationSpeed.x), std::abs(continuationSpeed.y));
        } else {
            auto trajectoryInfo = calculatePositionExactEndSpeed(v0, v1, currentTime + minimumTime, currentAngle, acc, vMax);
            endPos = trajectoryInfo.endPos;
            assumedSpeed = std::max(std::abs(trajectoryInfo.increaseAtSpeed.x), std::abs(trajectoryInfo.increaseAtSpeed.y));
        }

        float targetDistance = position.distance(endPos);
        if (targetDistance < (highPrecision ? HIGH_QUALITY_TARGET_PRECISION : REGULAR_TARGET_PRECISION)) {
            if (slowDownTime <= 0) {
                result = calculateTrajectoryExactEndSpeed(v0, v1, currentTime, currentAngle, acc, vMax, minimumTime);
            }
            return result;
        }

        Vector currentCenterTimePos = useMinTimePosForCenterPos ? minPos : centerTimePos(v0, v1, currentTime + minimumTime);
        float newDistance = endPos.distance(currentCenterTimePos);
        float targetCenterDistance = currentCenterTimePos.distance(position);
        float currentCenterDistanceDiff = targetCenterDistance - newDistance;
        if ((lastCenterDistanceDiff < 0) != (currentCenterDistanceDiff < 0)) {
            distanceFactor *= 0.85f;
        } else {
            distanceFactor *= 1.05f;
        }
        lastCenterDistanceDiff = currentCenterDistanceDiff;
        currentTime += currentCenterDistanceDiff * distanceFactor / std::max(0.5f, assumedSpeed);

        // correct angle
        float newAngle = (endPos - currentCenterTimePos).angle();
        float targetCenterAngle = (position - currentCenterTimePos).angle();
        //currentAngle += vectorAngleDiff((position - currentCenterTimePos), (endPos - currentCenterTimePos));
        float currentAngleDiff = angleDiff(targetCenterAngle, newAngle);
        if (i >= 4 && (currentAngleDiff < 0) != (lastAngleDiff < 0)) {
            angleFactor *= 0.5f;
        }
        lastAngleDiff = currentAngleDiff;
        currentAngle += currentAngleDiff * angleFactor;
    }
    result.valid = false;
    return result;
}
