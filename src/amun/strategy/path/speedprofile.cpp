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

#include "speedprofile.h"

#include <iostream>
#include <cassert>

static float sign(float x)
{
    return x < 0.0f ? -1.0f : 1.0f;
}

// exponential slowdown calculation
const float MIN_ACC_FACTOR = 0.3f;

class ConstantAcceleration2D {
public:

    struct SegmentPrecomputation {
        float invSegmentTime;
    };

    ConstantAcceleration2D(float, float) {}

    inline Vector segmentOffset(const Trajectory::VT &first, const Trajectory::VT &second, SegmentPrecomputation) const
    {
        return (first.v + second.v) * (0.5f * (second.t - first.t));
    }

    inline std::pair<Vector, Vector> partialSegmentOffsetAndSpeed(const Trajectory::VT &first, const Trajectory::VT &second, SegmentPrecomputation precomp,
                                                                float transformedT0, float time) const
    {
        const float timeDiff = time - transformedT0;
        const float diff = second.t == first.t ? 1 : timeDiff * precomp.invSegmentTime;
        const Vector speed = first.v + (second.v - first.v) * diff;
        const Vector partDist = (first.v + speed) * (0.5f * timeDiff);
        return {partDist, speed};
    }

    inline float timeForSegment(const Trajectory::VT &first, const Trajectory::VT &second, SegmentPrecomputation) const
    {
        return second.t - first.t;
    }

    inline SegmentPrecomputation precomputeSegment(const Trajectory::VT &first, const Trajectory::VT &second) const
    {
        return {1.0f / (second.t - first.t)};
    }
};

class SlowdownAcceleration2D {
public:

    struct SegmentPrecomputation {
        ConstantAcceleration2D::SegmentPrecomputation constantPrecomputation;
        Vector v0{0, 0};
        Vector a0{0, 0};
        Vector a1{0, 0};
        float segmentTime{0}; // time of the segment (slow down part only)
        Vector partialDistance{0, 0};
    };

    SlowdownAcceleration2D(float totalSimpleTime, float slowDownTime) :
        slowDownStartTime(totalSimpleTime - slowDownTime),
        endTime(totalSimpleTime + Trajectory::SLOW_DOWN_TIME - slowDownTime),
        simpleAcceleration(totalSimpleTime, slowDownTime)
    { }

    inline Vector segmentOffset(const Trajectory::VT &first, const Trajectory::VT &second, const SegmentPrecomputation &precomp) const
    {
        if (second.t <= slowDownStartTime || first.t == second.t) {
            return simpleAcceleration.segmentOffset(first, second, precomp.constantPrecomputation);
        }
        const float t = precomp.segmentTime;
        const Vector speedDiff = second.v - precomp.v0;
        const Vector diffSign{sign(speedDiff.x), sign(speedDiff.y)};
        const Vector signedA0{diffSign.x * precomp.a0.x, diffSign.y * precomp.a0.y};
        const Vector aDiff = precomp.a1 - precomp.a0;
        const Vector signedADiff{diffSign.x * aDiff.x, diffSign.y * aDiff.y};
        const Vector d = precomp.v0 * t + signedA0 * (0.5f * t * t) + signedADiff * ((1.0f / 6.0f) * t * t);
        return precomp.partialDistance + d;
    }

    inline std::pair<Vector, Vector> partialSegmentOffsetAndSpeed(const Trajectory::VT &first, const Trajectory::VT &second, const SegmentPrecomputation &precomp,
                                                                float transformedT0, float time) const
    {
        if (time <= slowDownStartTime || first.t == second.t) {
            return simpleAcceleration.partialSegmentOffsetAndSpeed(first, second, precomp.constantPrecomputation, transformedT0, time);
        }
        const float slowdownT0 = first.t > slowDownStartTime ? transformedT0 : slowDownStartTime;
        const float tm = time - slowdownT0;
        const Vector speedDiff = second.v - precomp.v0;
        const Vector diffSign{sign(speedDiff.x), sign(speedDiff.y)};
        const Vector signedA0{diffSign.x * precomp.a0.x, diffSign.y * precomp.a0.y};
        const Vector aDiff = precomp.a1 - precomp.a0;
        const Vector signedADiff{diffSign.x * aDiff.x, diffSign.y * aDiff.y};
        const float invSegmentTime = 1.0f / precomp.segmentTime;
        const Vector speed = precomp.v0 + signedA0 * tm + signedADiff * (0.5f * tm * tm * invSegmentTime);
        const Vector d = precomp.v0 * tm + signedA0 * (0.5f * tm * tm) + signedADiff * ((1.0f / 6.0f) * tm * tm * tm * invSegmentTime);
        return {precomp.partialDistance + d, speed};
    }

    inline float timeForSegment(const Trajectory::VT &first, const Trajectory::VT &second, const SegmentPrecomputation &precomp) const
    {
        if (second.t <= slowDownStartTime) {
            return second.t - first.t;
        } else if (first.t < slowDownStartTime) {
            return slowDownStartTime - first.t + precomp.segmentTime;
        } else {
            return precomp.segmentTime;
        }
    }

    inline SegmentPrecomputation precomputeSegment(const Trajectory::VT &first, const Trajectory::VT &second) const
    {
        SegmentPrecomputation result;
        result.constantPrecomputation = simpleAcceleration.precomputeSegment(first, second);
        if (second.t <= slowDownStartTime || first.t == second.t) {
            return result;
        }
        float t0;
        if (first.t < slowDownStartTime) {
            const auto partial = simpleAcceleration.partialSegmentOffsetAndSpeed(first, second, result.constantPrecomputation, first.t, slowDownStartTime);
            result.partialDistance = partial.first;
            result.v0 = partial.second;
            t0 = slowDownStartTime;
        } else {
            result.partialDistance = {0, 0};
            result.v0 = first.v;
            t0 = first.t;
        }
        const Vector baseAcc = (first.v - second.v).abs() / (second.t - first.t);
        const float accelerationFactor0 = computeAcceleration(endTime - t0);
        const float accelerationFactor1 = computeAcceleration(endTime - second.t);
        result.a0 = baseAcc * accelerationFactor0;
        result.a1 = baseAcc * accelerationFactor1;
        result.segmentTime = 2.0f * (second.t - t0) / (accelerationFactor0 + accelerationFactor1);

        return result;
    }


private:
    static inline float computeAcceleration(float timeToEnd)
    {
        const float totalTime = 2 / (1 + MIN_ACC_FACTOR);
        const float aFactor = (MIN_ACC_FACTOR - 1.0f) / totalTime;

        const float tFactor = 1 - timeToEnd / Trajectory::SLOW_DOWN_TIME;
        return std::sqrt(1 + 2 * tFactor * aFactor);
    }

public:
    const float slowDownStartTime;
    const float endTime;

    ConstantAcceleration2D simpleAcceleration;
};

void SpeedProfile1D::integrateTime()
{
    float totalTime = 0;
    for (std::size_t i = 0;i<profile.size();i++) {
        totalTime += profile[i].t;
        profile[i].t = totalTime;
    }
}

// trajectory calculation
static float constantDistance(float v, float time)
{
    return v * time;
}

static float dist(float v0, float v1, float acc)
{
    const float time = std::abs(v0 - v1) / acc;
    return 0.5f * (v0 + v1) * time;
}

std::pair<float, float> SpeedProfile1D::freeExtraTimeDistance(float v, float time, float acc, float vMax)
{
    const float toMaxTime = 2.0f * std::abs(vMax - v) / acc;
    if (toMaxTime < time) {
        return {2 * dist(v, vMax, acc) +
               constantDistance(vMax, time - toMaxTime), vMax};
    } else {
        const float v1 = (v > vMax ? -1.0f : 1.0f) * acc * time / 2 + v;
        return {2.0f * dist(v, v1, acc), v1};
    }
}

auto SpeedProfile1D::calculateEndPos1D(float v0, float v1, float hintDist, float acc, float vMax) -> TrajectoryPosInfo1D
{
    // basically the same as calculate1DTrajectory, but with position only
    // see the comments there if necessary
    const float desiredVMax = hintDist < 0 ? -vMax : vMax;
    if (hintDist == 0.0f) {
        return {dist(v0, v1, acc), std::max(v0, v1)};
    } else if ((v0 < desiredVMax) != (v1 < desiredVMax)) {
        return {dist(v0, v1, acc) + constantDistance(desiredVMax, std::abs(hintDist)), desiredVMax};
    } else {
        // check whether v0 or v1 is closer to the desired max speed
        const bool v0Closer = std::abs(v0 - desiredVMax) < std::abs(v1 - desiredVMax);
        const float closerSpeed = v0Closer ? v0 : v1;
        const auto extraDistance = freeExtraTimeDistance(closerSpeed, std::abs(hintDist), acc, desiredVMax);
        return {extraDistance.first + dist(v0, v1, acc), extraDistance.second};
    }
}

static SpeedProfile1D::VT adjustEndSpeed(float v0, float v1, float time, bool directionPositive, float acc)
{
    const float invAcc = 1.0f / acc;

    // idea: compute the speed that would be reached after accelerating in the desired direction
    const float speedAfterT = v0 + (directionPositive ? 1.0f : -1.0f) * (time * acc);
    // bound that speed to the allowed endspeed range [0, v1]
    const float boundedSpeed = std::max(std::min(speedAfterT, std::max(v1, 0.0f)), std::min(v1, 0.0f));
    // compute the time it would take to reach boundedSpeed from v0
    const float necessaryTime = std::abs(v0 - boundedSpeed) * invAcc;
    return {boundedSpeed, time - necessaryTime};
}

SpeedProfile1D::TrajectoryPosInfo1D SpeedProfile1D::calculateEndPos1DFastSpeed(float v0, float v1, float time, bool directionPositive, float acc, float vMax)
{
    const SpeedProfile1D::VT endValues = adjustEndSpeed(v0, v1, time, directionPositive, acc);
    if (endValues.t == 0.0f) {
        return {(v0 + endValues.v) * 0.5f * time, directionPositive ? std::max(v0, v1) : std::min(v0, v1)};
    } else {
        // TODO: remove the negative time in calculateEndPos1D
        return calculateEndPos1D(v0, endValues.v, directionPositive ? endValues.t : -endValues.t, acc, vMax);
    }
}

SpeedProfile1D SpeedProfile1D::calculate1DTrajectoryFastEndSpeed(float v0, float v1, float time, bool directionPositive, float acc, float vMax)
{
    const SpeedProfile1D::VT endValues = adjustEndSpeed(v0, v1, time, directionPositive, acc);
    if (endValues.t == 0.0f) {
        SpeedProfile1D result;
        result.profile.push_back({v0, 0});
        result.profile.push_back({endValues.v, std::abs(endValues.v - v0) / acc});
        return result;
    } else {
        return calculate1DTrajectory(v0, endValues.v, endValues.t, directionPositive, acc, vMax);
    }
}

void SpeedProfile1D::createFreeExtraTimeSegment(float beforeSpeed, float v, float nextSpeed, float time, float acc, float desiredVMax)
{
    const float toMaxTime = 2.0f * std::abs(desiredVMax - v) / acc;
    if (toMaxTime < time) {
        profile.push_back({desiredVMax, std::abs(desiredVMax - beforeSpeed) / acc});
        profile.push_back({desiredVMax, time - toMaxTime});
        profile.push_back({nextSpeed, std::abs(desiredVMax - nextSpeed) / acc});
    } else {
        const float v1 = (v > desiredVMax ? -1.0f : 1.0f) * acc * time / 2 + v;
        profile.push_back({v1, std::abs(beforeSpeed - v1) / acc});
        profile.push_back({nextSpeed, std::abs(nextSpeed - v1) / acc});
    }
}

SpeedProfile1D SpeedProfile1D::calculate1DTrajectory(float v0, float v1, float extraTime, bool directionPositive, float acc, float vMax)
{
    SpeedProfile1D result;
    result.profile.push_back({v0, 0});

    const float desiredVMax = directionPositive ? vMax : -vMax;
    if (extraTime == 0.0f) {
        result.profile.push_back({v1, std::abs(v0 - v1) / acc});
    } else if ((v0 < desiredVMax) != (v1 < desiredVMax)) {
        // we need to cross the maximum speed because either abs(v0) or abs(v1) exceed it
        // therefore, a segment reaching desiredMax from v0 is created,
        // one segment staying at desiredVMax for the given extra time
        // and one going from desiredVMax to v1
        const float accInv = 1.0f / acc;

        result.profile.push_back({desiredVMax, std::abs(v0 - desiredVMax) * accInv});
        result.profile.push_back({desiredVMax, extraTime});
        result.profile.push_back({v1, std::abs(v1 - desiredVMax) * accInv});
    } else {
        // check whether v0 or v1 is closer to the desired max speed
        const bool v0Closer = std::abs(v0 - desiredVMax) < std::abs(v1 - desiredVMax);
        const float closerSpeed = v0Closer ? v0 : v1;
        result.createFreeExtraTimeSegment(v0, closerSpeed, v1, extraTime, acc, desiredVMax);
    }
    return result;
}

// equation must be solvable
static float solveSq(float a, float b, float c)
{
    if (a == 0) {
        if (b == 0) {
            assert(false);
        } else {
            return -c / b;
        }
    }

    float det = b * b - 4 * a * c;
    if (det < 0) {
        assert(false);
    } else if (det == 0) {
        return -b / (2 * a);
    }
    det = std::sqrt(det);
    const float t2 = (-b - std::copysign(det, b)) / (2 * a);
    const float t1 = c / (a * t2);

    return std::max(t1, t2);
}

SpeedProfile1D SpeedProfile1D::create1DAccelerationByDistance(float v0, float v1, float time, float distance)
{
    assert(std::signbit(v0) == std::signbit(distance) && (std::signbit(v1) == std::signbit(distance) || v1 == 0));

    // necessary condition for this function to work correctly:
    // const float directAcc = 0.5f * (v0 + v1) * std::abs(v0 - v1) / distance;
    // const float directTime = std::abs(v0 - v1) / directAcc;
    // assert(directTime > time || std::abs(v0) < 0.0001f);

    const float a = 1.0f / distance;
    const float b = -2.0f / time;
    const float v0Abs = std::abs(v0);
    const float v1Abs = std::abs(v1);
    const float c = 1.0f / time * (v0Abs + v1Abs) - 1.0f / (2.0f * distance) * (v0Abs * v0Abs + v1Abs * v1Abs);
    const float solution = solveSq(a, b, c);
    const float midSpeed = std::copysign(solution, v0);

    const float acc = 1.0f / (2.0f * distance) * (2.0f * midSpeed * midSpeed - v0Abs * v0Abs - v1Abs * v1Abs);
    const float accInv = 1.0f / acc;

    SpeedProfile1D result;
    result.profile.push_back({v0, 0});
    result.profile.push_back({midSpeed, std::abs(v0 - midSpeed) * accInv});
    result.profile.push_back({v1, std::abs(v1 - midSpeed) * accInv});
    return result;
}

SpeedProfile1D SpeedProfile1D::createLinearSpeedSegment(float v0, float v1, float time)
{
    SpeedProfile1D result;
    result.profile.push_back({v0, 0});
    result.profile.push_back({v1, time});
    return result;
}

static float speedForTime(SpeedProfile1D::VT first, SpeedProfile1D::VT second, float time)
{
    const float timeDiff = time - first.t;
    const float diff = second.t == first.t ? 1 : timeDiff / (second.t - first.t);
    const float speed = first.v + diff * (second.v - first.v);
    return speed;
}

Trajectory::Trajectory(const SpeedProfile1D &xProfile, const SpeedProfile1D &yProfile,
                       Vector startPos, float slowDownTime) :
    s0(startPos),
    // 0 would be at the exact end of the trajectory, thus sometimes creating problems
    slowDownTime(slowDownTime == 0 ? -1 : slowDownTime)
{
    const float SAME_POINT_EPSILON = 0.0001f;

    std::size_t xIndex = 0;
    std::size_t yIndex = 0;

    const auto &x = xProfile.profile;
    const auto &y = yProfile.profile;

    while (xIndex < x.size() && yIndex < y.size()) {
        const float xNext = x[xIndex].t;
        const float yNext = y[yIndex].t;


        if (std::abs(xNext - yNext) < SAME_POINT_EPSILON) {
            const float time = (xNext + yNext) * 0.5f;
            const Vector speed{x[xIndex].v, y[yIndex].v};
            profile.push_back({speed, time});
            xIndex++;
            yIndex++;
        } else if (xNext < yNext) {
            const float vy = speedForTime(y[yIndex - 1], y[yIndex], xNext);
            const Vector speed{x[xIndex].v, vy};
            profile.push_back({speed, xNext});
            xIndex++;
        } else {
            const float vx = speedForTime(x[xIndex - 1], x[xIndex], yNext);
            const Vector speed{vx, y[yIndex].v};
            profile.push_back({speed, yNext});
            yIndex++;
        }
    }

    while (xIndex < x.size()) {
        profile.push_back({Vector{x[xIndex].v, y.back().v}, x[xIndex].t});
        xIndex++;
    }
    while (yIndex < y.size()) {
        profile.push_back({Vector{x.back().v, y[yIndex].v}, y[yIndex].t});
        yIndex++;
    }
}

float Trajectory::time() const {
    if (slowDownTime == -1) {
        return profile.back().t;
    }

    float time = 0;
    SlowdownAcceleration2D acceleration(profile.back().t, slowDownTime);
    for (unsigned int i = 0;i<profile.size()-1;i++) {
        const auto precomputation = acceleration.precomputeSegment(profile[i], profile[i+1]);
        time += acceleration.timeForSegment(profile[i], profile[i+1], precomputation);
    }
    return time;
}

void Trajectory::limitToTime(float time)
{
    for (unsigned int i = 0;i<profile.size()-1;i++) {
        if (profile[i+1].t >= time) {
            const float diff = profile[i+1].t == profile[i].t ? 1 : (time - profile[i].t) / (profile[i+1].t - profile[i].t);
            const Vector speed = profile[i].v + (profile[i+1].v - profile[i].v) * diff;
            profile[i+1] = {speed, time};
            profile.resize(i+2);
            return;
        }
    }
}

Vector Trajectory::endPosition() const
{
    SlowdownAcceleration2D acceleration(profile.back().t, slowDownTime);

    Vector offset = s0;
    float totalTime = 0;
    for (unsigned int i = 0;i<profile.size()-1;i++) {
        const auto precomputation = acceleration.precomputeSegment(profile[i], profile[i+1]);
        offset += acceleration.segmentOffset(profile[i], profile[i+1], precomputation);
        totalTime += acceleration.timeForSegment(profile[i], profile[i+1], precomputation);
    }
    return offset + correctionOffsetPerSecond * totalTime;
}

RobotState Trajectory::stateAtTime(float time) const
{
    SlowdownAcceleration2D acceleration(profile.back().t, slowDownTime);

    Vector offset = s0;
    float totalTime = 0;
    for (unsigned int i = 0;i<profile.size()-1;i++) {
        const auto precomputation = acceleration.precomputeSegment(profile[i], profile[i+1]);
        const float segmentTime = acceleration.timeForSegment(profile[i], profile[i+1], precomputation);
        if (totalTime + segmentTime > time) {
            const auto inf = acceleration.partialSegmentOffsetAndSpeed(profile[i], profile[i+1], precomputation, totalTime, time);
            return {offset + correctionOffsetPerSecond * time + inf.first, inf.second};
        }
        offset += acceleration.segmentOffset(profile[i], profile[i+1], precomputation);
        totalTime += segmentTime;
    }
    return {offset + correctionOffsetPerSecond * totalTime, profile.back().v};
}

std::vector<TrajectoryPoint> Trajectory::trajectoryPositions(std::size_t count, float timeInterval, float timeOffset) const
{
    SlowdownAcceleration2D acceleration(profile.back().t, slowDownTime);

    std::vector<TrajectoryPoint> result(count);
    for (std::size_t i = 0;i<count;i++) {
        result[i].time = timeOffset + i * timeInterval;
    }

    Vector offset = s0;
    float totalTime = 0;

    float nextDesiredTime = 0;
    std::size_t resultCounter = 0;
    for (unsigned int i = 0;i<profile.size()-1;i++) {
        const auto precomputation = acceleration.precomputeSegment(profile[i], profile[i+1]);
        const float segmentTime = acceleration.timeForSegment(profile[i], profile[i+1], precomputation);
        while (totalTime + segmentTime >= nextDesiredTime) {
            const auto inf = acceleration.partialSegmentOffsetAndSpeed(profile[i], profile[i+1], precomputation, totalTime, nextDesiredTime);
            result[resultCounter].state.pos = offset + inf.first + correctionOffsetPerSecond * nextDesiredTime;
            result[resultCounter].state.speed = inf.second;
            resultCounter++;
            nextDesiredTime += timeInterval;

            if (resultCounter == result.size()) {
                return result;
            }
        }
        offset += acceleration.segmentOffset(profile[i], profile[i+1], precomputation);
        totalTime += segmentTime;
    }

    while (resultCounter < result.size()) {
        result[resultCounter].state.pos = offset + correctionOffsetPerSecond * totalTime;
        result[resultCounter].state.speed = profile.back().v;
        resultCounter++;
    }

    return result;
}

BoundingBox Trajectory::calculateBoundingBox() const
{
    SlowdownAcceleration2D acceleration(profile.back().t, slowDownTime);

    Vector minPos = s0;
    Vector maxPos = s0;

    Vector offset = s0;
    for (unsigned int i = 0;i<profile.size()-1;i++) {
        // check segments crossing zero speed, the trajectory makes a curve here
        for (int j : {0, 1}) {
            if ((profile[i].v[j] > 0) != (profile[i+1].v[j] > 0)) {
                const float proportion = std::abs(profile[i].v[j]) / (std::abs(profile[i].v[j]) + std::abs(profile[i+1].v[j]));
                const float relTime = (profile[i+1].t - profile[i].t) * proportion;
                const float totalTime = profile[i].t + relTime;
                const Trajectory::VT zeroSegment{Vector{0, 0}, totalTime};

                const auto precomputation = acceleration.precomputeSegment(profile[i], zeroSegment);
                const Vector partialOffset = offset + acceleration.segmentOffset(profile[i], zeroSegment, precomputation) + correctionOffsetPerSecond * relTime;
                minPos[j] = std::min(minPos[j], partialOffset[j]);
                maxPos[j] = std::max(maxPos[j], partialOffset[j]);
            }
        }

        const auto precomputation = acceleration.precomputeSegment(profile[i], profile[i+1]);
        offset += acceleration.segmentOffset(profile[i], profile[i+1], precomputation) + correctionOffsetPerSecond * (profile[i+1].t - profile[i].t);
        minPos.x = std::min(minPos.x, offset.x);
        minPos.y = std::min(minPos.y, offset.y);
        maxPos.x = std::max(maxPos.x, offset.x);
        maxPos.y = std::max(maxPos.y, offset.y);
    }
    return {minPos, maxPos};
}

std::vector<TrajectoryPoint> Trajectory::getTrajectoryPoints() const
{
    SlowdownAcceleration2D acceleration(profile.back().t, slowDownTime);

    std::vector<TrajectoryPoint> result;
    result.reserve(profile.size() + 1);

    result.emplace_back(RobotState{s0, profile[0].v}, 0);

    Vector offset = s0;
    float time = 0;
    for (unsigned int i = 0;i<profile.size()-1;i++) {
        const auto precomputation = acceleration.precomputeSegment(profile[i], profile[i+1]);
        offset += acceleration.segmentOffset(profile[i], profile[i+1], precomputation);
        time += acceleration.timeForSegment(profile[i], profile[i+1], precomputation);

        result.emplace_back(RobotState{offset, profile[i+1].v}, time);
    }

    // compensate for the missing exponential slowdown by adding a segment with zero speed
    if (slowDownTime != -1) {
        result.emplace_back(RobotState{offset, profile.back().v}, time);
    }

    return result;
}

void Trajectory::printDebug() const
{
    for (std::size_t i = 0;i<profile.size();i++) {
        std::cout <<"("<<profile[i].t<<": "<<profile[i].v<<") ";
    }
    std::cout <<std::endl;
}
