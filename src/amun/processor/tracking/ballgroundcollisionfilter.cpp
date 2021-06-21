/***************************************************************************
 *   Copyright 2021 Andreas Wendler                                       *
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
#include "ballgroundcollisionfilter.h"
#include <algorithm>
#include <QDebug>

BallGroundCollisionFilter::BallGroundCollisionFilter(const VisionFrame &frame, CameraInfo* cameraInfo) :
    AbstractBallFilter(frame, cameraInfo),
    m_groundFilter(frame, cameraInfo),
    m_pastFilter(frame, cameraInfo)
{

}

BallGroundCollisionFilter::BallGroundCollisionFilter(const BallGroundCollisionFilter& filter, qint32 primaryCamera) :
    AbstractBallFilter(filter, primaryCamera),
    m_groundFilter(filter.m_groundFilter, primaryCamera),
    m_pastFilter(filter.m_pastFilter, primaryCamera),
    m_lastVisionTime(filter.m_lastVisionTime),
    m_localBallOffset(filter.m_localBallOffset),
    m_insideRobotOffset(filter.m_insideRobotOffset),
    m_lastReportedBallPos(filter.m_lastReportedBallPos),
    m_resetFilters(filter.m_resetFilters)
{ }

void BallGroundCollisionFilter::processVisionFrame(const VisionFrame& frame)
{
    m_lastVisionTime = frame.time;
    if (m_resetFilters) {
        m_groundFilter.reset(frame);
        m_pastFilter.reset(frame);
        m_resetFilters = false;
    } else {
        m_groundFilter.processVisionFrame(frame);
        m_pastFilter.processVisionFrame(frame);
    }
}

bool BallGroundCollisionFilter::acceptDetection(const VisionFrame& frame)
{
    const float ACCEPT_BALL_DIST = 0.5f;
    const float reportedBallDist = (m_lastReportedBallPos - Eigen::Vector2f(frame.x, frame.y)).norm();
    return reportedBallDist < ACCEPT_BALL_DIST || m_groundFilter.acceptDetection(frame);
}

static auto intersectLineCircle(Eigen::Vector2f offset, Eigen::Vector2f dir, Eigen::Vector2f center, float radius)
    -> std::vector<std::pair<Eigen::Vector2f, float>>
{
    dir = dir.normalized();
    const Eigen::Vector2f constPart = offset - center;
    // |offset + lambda*dir - center| = radius
    // l^2 VxV + l 2(CxV) + CxC == R^2

    const float a = dir.dot(dir);
    const float b = 2 * dir.dot(constPart);
    const float c = constPart.dot(constPart) - radius * radius;

    const float det = b * b - 4 * a * c;

    if (det < 0) {
        return {};
    }

    if (det < 0.00001) {
        const float lambda1 = (-b) / (2 * a);
        return {{offset + dir * lambda1, lambda1}};
    }

    const float lambda1 = (-b + std::sqrt(det)) / (2 * a);
    const float lambda2 = (-b - std::sqrt(det)) / (2 * a);
    const Eigen::Vector2f point1 = offset + dir * lambda1;
    const Eigen::Vector2f point2 = offset + dir * lambda2;
    return {{point1, lambda1}, {point2, lambda2}};
}

static std::optional<Eigen::Vector2f> intersectLineSegmentCircle(Eigen::Vector2f p1, Eigen::Vector2f p2, Eigen::Vector2f center, float radius)
{
    const float dist = (p2 - p1).norm();
    auto intersections = intersectLineCircle(p1, p2 - p1, center, radius);
    if (intersections.size() == 0) {
        return {};
    }
    if (intersections.size() == 1) {
        if (intersections[0].second >= 0 && intersections[0].second <= dist) {
            return intersections[0].first;
        } else {
            return {};
        }
    }
    // TODO: is this complexity necessary?
    if (intersections[0].second > intersections[1].second) {
        std::swap(intersections[0], intersections[1]);
    }
    for (const auto &intersection : intersections) {
        if (intersection.second >= 0 && intersection.second <= dist) {
            return intersection.first;
        }
    }
    return {};
}

static Eigen::Vector2f perpendicular(const Eigen::Vector2f dir)
{
    return Eigen::Vector2f(dir.y(), -dir.x());
}

// return value is the lambda of the intersection point p, p = pos1 + dir1 * returnvalue
// and the same for the second line
std::optional<std::pair<float, float>> intersectLineLine(Eigen::Vector2f pos1, Eigen::Vector2f dir1, Eigen::Vector2f pos2, Eigen::Vector2f dir2)
{
    // check whether the directions are collinear
    if (std::abs(perpendicular(dir1).dot(dir2)) / (dir1.norm() * dir2.norm()) < 0.0001) {
        return {};
    }

    const Eigen::Vector2f normal1 = perpendicular(dir1);
    const Eigen::Vector2f normal2 = perpendicular(dir2);
    const Eigen::Vector2f diff = pos2 - pos1;
    const float t1 = normal2.dot(diff) / normal2.dot(dir1);
    const float t2 = -normal1.dot(diff) / normal1.dot(dir2);

    return {{t1, t2}};
}

// TODO: ball radius
static std::optional<Eigen::Vector2f> intersectLineSegmentRobot(Eigen::Vector2f p1, Eigen::Vector2f p2, const RobotInfo &robot, float robotRadius,
                                                                float robotSizeFactor = 1.0f)
{
    const float DRIBBLER_WIDTH = 0.07f;

    Eigen::Vector2f dribblerPos = robot.dribblerPos;
    if (robotSizeFactor != 1.0f) {
        robotRadius *= robotSizeFactor;
        dribblerPos = robot.robotPos + (robot.dribblerPos - robot.robotPos) * robotSizeFactor;
    }

    const auto toDribbler = (dribblerPos - robot.robotPos).normalized();
    const auto dribblerSideways = perpendicular(toDribbler);
    const auto dribblerIntersection = intersectLineLine(dribblerPos, dribblerSideways, p1, p2 - p1);
    std::optional<Eigen::Vector2f> dribblerIntersectionPos;
    if (dribblerIntersection.has_value() && std::abs(dribblerIntersection->first) <= DRIBBLER_WIDTH / 2.0f &&
            dribblerIntersection->second >= 0 && dribblerIntersection->second <= 1) {
        dribblerIntersectionPos = dribblerPos + dribblerSideways * dribblerIntersection->first;
        if ((p1 - dribblerPos).dot(toDribbler) >= 0) {
            // the line segment comes from in front of the robot, the line intersection is the correct one
            return dribblerIntersectionPos;
        }
    }
    auto hullIntersection = intersectLineSegmentCircle(p1, p2, robot.robotPos, robotRadius);
    if (dribblerIntersectionPos && hullIntersection) {
        // select the closer of the two intersections
        if ((*hullIntersection - p1).norm() < (*dribblerIntersectionPos - p1).norm()) {
            return hullIntersection;
        } else {
            return dribblerIntersectionPos;
        }
    }
    return hullIntersection;
}

static bool isInsideRobot(Eigen::Vector2f pos, const RobotInfo &robot, float robotRadius)
{
    if ((pos - robot.robotPos).norm() > robotRadius) {
        return false;
    }
    const auto toDribbler = (robot.dribblerPos - robot.robotPos).normalized();
    return (pos - robot.dribblerPos).dot(toDribbler) <= 0;
}

static bool isBallVisible(Eigen::Vector2f pos, const RobotInfo &robot, float robotRadius, float robotHeight, Eigen::Vector3f cameraPos)
{
    const float BALL_RADIUS = 0.0215f;
    const Eigen::Vector3f toBall = Eigen::Vector3f(pos.x(), pos.y(), BALL_RADIUS) - cameraPos;
    const float length = (cameraPos.z() - robotHeight) / (cameraPos.z() - BALL_RADIUS);
    const Eigen::Vector3f projected = cameraPos + toBall * length;
    const Eigen::Vector2f projected2D(projected.x(), projected.y());
    // TODO: this assumes that the ball is only invisible if the center is overshadowed
    const bool inRadius = (robot.robotPos - projected2D).norm() <= robotRadius;
    const bool frontOfDribbler = (projected2D - robot.dribblerPos).dot(robot.dribblerPos - robot.robotPos) > 0;
    const bool hasIntersection = intersectLineSegmentRobot(pos, projected2D, robot, robotRadius, 0.98f).has_value();
    return (!inRadius || frontOfDribbler) && !hasIntersection;
}

void BallGroundCollisionFilter::updateDribblingInfo(Eigen::Vector2f projectedBallPos, const RobotInfo &robot)
{
    BallOffsetInfo offset;
    offset.robotIdentifier = robot.identifier;
    const Eigen::Vector2f toDribbler = (robot.dribblerPos - robot.robotPos).normalized();
    offset.ballOffset = Eigen::Vector2f{(projectedBallPos - robot.robotPos).dot(toDribbler),
                                        (projectedBallPos - robot.robotPos).dot(perpendicular(toDribbler))};
    offset.pushingBallPos = projectedBallPos;
    m_localBallOffset = offset;
}

static Eigen::Vector2f unprojectRelativePosition(Eigen::Vector2f relativePos, const RobotInfo &robot)
{
    const Eigen::Vector2f toDribbler = (robot.dribblerPos - robot.robotPos).normalized();
    const Eigen::Vector2f relativeBallPos = relativePos.x() * toDribbler +
                                            relativePos.y() * perpendicular(toDribbler);
    return robot.robotPos + relativeBallPos;
}

static void setBallData(world::Ball *ball, Eigen::Vector2f pos, Eigen::Vector2f speed, bool writeSpeed)
{
    ball->set_p_x(pos.x());
    ball->set_p_y(pos.y());
    if (writeSpeed) {
        ball->set_v_x(speed.x());
        ball->set_v_y(speed.y());
    }
}

bool BallGroundCollisionFilter::checkFeasibleInvisibility(const QVector<RobotInfo> &robots)
{
    if (!m_localBallOffset) {
        return false;
    }
    int id = m_localBallOffset->robotIdentifier;
    auto robot = std::find_if(robots.begin(), robots.end(), [id](const RobotInfo &robot) { return robot.identifier == id; });
    if (robot == robots.end()) {
        return false;
    }
    if (!isBallVisible(m_localBallOffset->pushingBallPos, *robot, ROBOT_RADIUS, ROBOT_HEIGHT,
            m_cameraInfo->cameraPosition[m_primaryCamera])) {
        return true;
    }
    return !isBallVisible(m_lastReportedBallPos, *robot, ROBOT_RADIUS, ROBOT_HEIGHT,
                          m_cameraInfo->cameraPosition[m_primaryCamera]);
}

void BallGroundCollisionFilter::writeBallState(world::Ball *ball, qint64 time, const QVector<RobotInfo> &robots)
{
    computeBallState(ball, time, robots);
    m_lastReportedBallPos = Eigen::Vector2f(ball->p_x(), ball->p_y());
    m_feasiblyInvisible = checkFeasibleInvisibility(robots);
}

void BallGroundCollisionFilter::computeBallState(world::Ball *ball, qint64 time, const QVector<RobotInfo> &robots)
{
    const qint64 RESET_SPEED_TIME = 150; // ms
    const qint64 ACTIVATE_DRIBBLING_TIME = 80; // ms
    // TODO: robot data from specs?

    // TODO: test sides flipped (fieldtransform difference in robotinfo and groundfilter result?)
    m_groundFilter.writeBallState(ball, time, robots);
    // might be overwritten later
    debug("ground filter mode", "regular ground filter");

    world::Ball pastState;
    m_pastFilter.writeBallState(&pastState, m_lastVisionTime + 1, robots);

#ifdef ENABLE_TRACKING_DEBUG
    // prevent accumulation of debug values, since they are never read
    m_groundFilter.clearDebugValues();
    m_pastFilter.clearDebugValues();
#endif

    // remove this once all issues are fixed
    return;

    // TODO: time is with the added system delay time etc., these should be removed from the calculation
    const int invisibleTimeMs = (time - m_lastVisionTime) / 1000000;
    const bool writeBallSpeed = invisibleTimeMs > RESET_SPEED_TIME;
    debug("ball invisible time", invisibleTimeMs);

    if (invisibleTimeMs > ACTIVATE_DRIBBLING_TIME && m_localBallOffset) {
        const int identifier = m_localBallOffset->robotIdentifier;
        auto robot = std::find_if(robots.begin(), robots.end(), [identifier](const RobotInfo &robot) { return robot.identifier == identifier; });
        if (robot != robots.end()) {
            const Eigen::Vector2f ballPos = unprojectRelativePosition(m_localBallOffset->ballOffset, *robot);

            if (isInsideRobot(m_localBallOffset->pushingBallPos, *robot, ROBOT_RADIUS)) {
                m_localBallOffset->pushingBallPos = ballPos;
            }
            bool pushingPosVisible = isBallVisible(m_localBallOffset->pushingBallPos, *robot, ROBOT_RADIUS, ROBOT_HEIGHT,
                                                   m_cameraInfo->cameraPosition[m_primaryCamera]);
            if (pushingPosVisible) {
                // TODO: only allow this when the ball is near the dribbler not the robot body
                setBallData(ball, ballPos, robot->speed, writeBallSpeed);
                debug("ground filter mode", "dribbling");
            } else {
                setBallData(ball, m_localBallOffset->pushingBallPos, Eigen::Vector2f(0, 0), writeBallSpeed);
                debug("ground filter mode", "invisible standing ball");
            }
            m_resetFilters = true;
            return;
        }
    }
    if (invisibleTimeMs <= ACTIVATE_DRIBBLING_TIME) {
        m_localBallOffset.reset();
    }

    const Eigen::Vector2f pastPos{pastState.p_x(), pastState.p_y()};
    debugCircle("past ball state", pastState.p_x(), pastState.p_y(), 0.015);

    Eigen::Vector2f currentPos{ball->p_x(), ball->p_y()};
    debugCircle("current pos", currentPos.x(), currentPos.y(), 0.03);
    for (const RobotInfo &robot : robots) {
        if (isInsideRobot(pastPos, robot, ROBOT_RADIUS)) {

            if (m_insideRobotOffset && m_insideRobotOffset->robotIdentifier == robot.identifier) {
                Eigen::Vector2f ballPos = unprojectRelativePosition(m_insideRobotOffset->ballOffset, robot);
                setBallData(ball, ballPos, robot.speed, writeBallSpeed);
                debug("ground filter mode", "inside robot (keep projection)");
                m_localBallOffset = m_insideRobotOffset;
                return;
            }

            const Eigen::Vector2f pastSpeed{pastState.v_x(), pastState.v_y()};
            const Eigen::Vector2f relativeSpeed = pastSpeed - robot.speed;
            const Eigen::Vector2f projectDir = relativeSpeed.isZero(0.001f) ? Eigen::Vector2f(pastPos - robot.robotPos) : -relativeSpeed;
            const auto closeLineIntersection = intersectLineSegmentRobot(pastPos, projectDir * 1000.0f, robot, ROBOT_RADIUS);
            const auto farLineIntersection = intersectLineSegmentRobot(pastPos, -projectDir * 1000.0f, robot, ROBOT_RADIUS);
            if (closeLineIntersection && farLineIntersection) {
                const float closeDist = (*closeLineIntersection - pastPos).norm();
                const float farDist = (*farLineIntersection - pastPos).norm();
                Eigen::Vector2f projected;
                if (closeDist < farDist * 2) {
                    projected = *closeLineIntersection;
                } else {
                    projected = *farLineIntersection;
                }
                setBallData(ball, projected, robot.speed, writeBallSpeed);

                updateDribblingInfo(projected, robot);
                m_insideRobotOffset = m_localBallOffset;

                debugLine("ball line intersection", pastPos.x(), pastPos.y(), projected.x(), projected.y(), 2);
                debug("ground filter mode", "inside robot (new projection)");
                return;
            }
        }

        auto intersection = intersectLineSegmentRobot(pastPos, currentPos, robot, ROBOT_RADIUS);
        if (intersection) {
            debugLine("ball line intersection", pastPos.x(), pastPos.y(), currentPos.x(), currentPos.y(), 1);
            currentPos = *intersection;
            setBallData(ball, currentPos, robot.speed, writeBallSpeed);
            debug("ground filter mode", "outside robot projection");

            updateDribblingInfo(*intersection, robot);
        }
    }

    m_insideRobotOffset.reset();
}

std::size_t BallGroundCollisionFilter::chooseBall(const std::vector<VisionFrame> &frames)
{
    return m_groundFilter.chooseBall(frames);
}
