/***************************************************************************
 *   Copyright 2015 Michael Eischer, Philipp Nordhus                       *
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

#include "tracker.h"
#include "balltracker.h"
#include "protobuf/ssl_wrapper.pb.h"
#include "robotfilter.h"
#include "protobuf/debug.pb.h"
#include <QDebug>
#include <iostream>

Tracker::Tracker(qint64 startTime, float tdX, float tdY) : Tracker()
{
    m_touchdownX = tdX;
    m_touchdownY = tdY;
    m_startTime = startTime;
}

Tracker::Tracker() :
    m_cameraInfo(new CameraInfo),
    m_flip(false),
    m_systemDelay(0),
    m_resetTime(0),
    m_startTime(0),
    m_geometryUpdated(false),
    m_hasVisionData(false),
    m_lastUpdateTime(0),
    m_currentBallFilter(nullptr),
    m_aoiEnabled(false),
    m_aoi_x1(0.0f),
    m_aoi_y1(0.0f),
    m_aoi_x2(0.0f),
    m_aoi_y2(0.0f)
{
}

Tracker::~Tracker()
{
    reset();
    delete m_cameraInfo;
}

static bool isInAOI(float detectionX, float detectionY, bool flip, float x1, float y1, float x2, float y2)
{
    float x = -detectionY / 1000.0f;
    float y = detectionX / 1000.0f;
    if (flip) {
        x = -x;
        y = -y;
    }
    return (x > x1 && x < x2 && y > y1 && y < y2);
}

void Tracker::reset()
{
    foreach (const QList<RobotFilter*>& list, m_robotFilterYellow) {
        qDeleteAll(list);
    }
    m_robotFilterYellow.clear();

    foreach (const QList<RobotFilter*>& list, m_robotFilterBlue) {
        qDeleteAll(list);
    }
    m_robotFilterBlue.clear();

    qDeleteAll(m_ballFilter);
    m_ballFilter.clear();

    m_hasVisionData = false;
    m_resetTime = 0;
    m_lastUpdateTime = 0;
    m_visionPackets.clear();
}

void Tracker::setFlip(bool flip)
{
    // used to change goals between blue and yellow
    m_flip = flip;
}

void Tracker::process(qint64 currentTime)
{
    // reset time is used to immediatelly show robots after reset
    if (m_resetTime == 0) {
        m_resetTime = currentTime;
    }

    // remove outdated ball and robot filters
    invalidateBall(currentTime);
    invalidateRobots(m_robotFilterYellow, currentTime);
    invalidateRobots(m_robotFilterBlue, currentTime);

    //track geometry changes
    m_geometryUpdated = false;

    foreach (const Packet &p, m_visionPackets) {
        SSL_WrapperPacket wrapper;
        if (!wrapper.ParseFromArray(p.first.data(), p.first.size())) {
            continue;
        }

        if (wrapper.has_geometry()) {
            updateGeometry(wrapper.geometry().field());
            for (int i = 0; i < wrapper.geometry().calib_size(); ++i) {
                updateCamera(wrapper.geometry().calib(i));
            }
            m_geometryUpdated = true;
        }

        if (!wrapper.has_detection()) {
            continue;
        }

        const SSL_DetectionFrame &detection = wrapper.detection();
        const qint64 visionProcessingTime = (detection.t_sent() - detection.t_capture()) * 1E9;
        // time on the field for which the frame was captured
        // with Timer::currentTime being now
        const qint64 sourceTime = p.second - visionProcessingTime - m_systemDelay;

        // drop frames older than the current state
        if (sourceTime <= m_lastUpdateTime) {
            continue;
        }

        for (int i = 0; i < detection.robots_yellow_size(); i++) {
            trackRobot(m_robotFilterYellow, detection.robots_yellow(i), sourceTime, detection.camera_id());
        }

        for (int i = 0; i < detection.robots_blue_size(); i++) {
            trackRobot(m_robotFilterBlue, detection.robots_blue(i), sourceTime, detection.camera_id());
        }

        QList<RobotFilter *> bestRobots = getBestRobots(sourceTime);
        for (int i = 0; i < detection.balls_size(); i++) {
            trackBall(detection.balls(i), sourceTime, detection.camera_id(), bestRobots);
        }

        m_lastUpdateTime = sourceTime;
    }
    m_visionPackets.clear();
}

template<class Filter> static Filter* bestFilter(QList<Filter*> &filters, int minFrameCount)
{
    // get first filter that has the minFrameCount and move it to the front
    // this is required to ensure a stable result
    foreach (Filter* item, filters) {
        if (item->frameCounter() >= minFrameCount) {
            if (filters.first() != item) {
                filters.removeOne(item);
                filters.prepend(item);
            }
            return item;
        }
    }
    return NULL;
}


void Tracker::prioritizeBallFilters()
{
    // assures that the one with its camera closest to its last detection is taken.
    bool flying = m_ballFilter.contains(m_currentBallFilter) && m_currentBallFilter->isFlying();
    // when the current filter is tracking a flight, prioritize flight reconstruction
    auto cmp = [ flying ] ( BallTracker* fst, BallTracker* snd ) -> bool {
        float fstDist = fst->distToCamera(flying);
        float sndDist = snd->distToCamera(flying);
        return fstDist < sndDist;
    };
    std::sort(m_ballFilter.begin(), m_ballFilter.end(), cmp);
}

BallTracker* Tracker::bestBallFilter()
{
    // find oldest filter. if there are multiple with same initTime
    // (i.e. camera handover filters) this picks the first (prioritized) one.
    BallTracker* best = nullptr;
    qint64 oldestTime = 0;
    for (auto f : m_ballFilter) {
        if (best == nullptr || f->initTime() < oldestTime) {
            best = f;
            oldestTime = f->initTime();
        }
    }
    m_currentBallFilter = best;
    return m_currentBallFilter;
}

Status Tracker::worldState(qint64 currentTime)
{
    const qint64 resetTimeout = 500*1000*1000;
    // only return objects which have been tracked for more than minFrameCount frames
    // if the tracker was reset recently, allow for fast repopulation
    const int minFrameCount = (currentTime > m_resetTime + resetTimeout) ? 5: 0;

    // create world state for the given time
    Status status(new amun::Status);
    world::State *worldState = status->mutable_world_state();
    worldState->set_time(currentTime);
    worldState->set_has_vision_data(m_hasVisionData);

    BallTracker *ball = bestBallFilter();

    if (ball != NULL) {
        ball->update(currentTime);
        ball->get(worldState->mutable_ball(), m_flip);

#ifdef ENABLE_TRACKING_DEBUG
        if (m_touchdownX * m_touchdownY != 0 && ball->isFlying()
                 && worldState->ball().has_touchdown_x() && !worldState->ball().is_bouncing()) {
            Eigen::Vector2f tp(worldState->ball().touchdown_x(), worldState->ball().touchdown_y());
            Eigen::Vector2f ref(m_touchdownX, m_touchdownY);
            static double accu = 0;
            double dist = (tp-ref).norm();
            accu += dist;
            std::cout << accu << std::endl;
        }
#endif
    }


    for(RobotMap::iterator it = m_robotFilterYellow.begin(); it != m_robotFilterYellow.end(); ++it) {
        RobotFilter *robot = bestFilter(*it, minFrameCount);
        if (robot != NULL) {
            robot->update(currentTime);
            robot->get(worldState->add_yellow(), m_flip, false);
        }
    }

    for(RobotMap::iterator it = m_robotFilterBlue.begin(); it != m_robotFilterBlue.end(); ++it) {
        RobotFilter *robot = bestFilter(*it, minFrameCount);
        if (robot != NULL) {
            robot->update(currentTime);
            robot->get(worldState->add_blue(), m_flip, false);
        }
    }

    if (m_geometryUpdated) {
        status->mutable_geometry()->CopyFrom(m_geometry);
    }

    if (m_aoiEnabled) {
        amun::TrackingAOI *aoi = worldState->mutable_tracking_aoi();
        aoi->set_x1(m_aoi_x1);
        aoi->set_y1(m_aoi_y1);
        aoi->set_x2(m_aoi_x2);
        aoi->set_y2(m_aoi_y2);
    }

#ifdef ENABLE_TRACKING_DEBUG
    for (auto& filter : m_ballFilter) {
        if (filter == ball) {
            amun::DebugValue *debugValue = status->mutable_debug()->add_value();
            debugValue->set_key("active cam");
            debugValue->set_float_value(ball->primaryCamera());
            status->mutable_debug()->MergeFrom(filter->debugValues());
        } else {
            status->mutable_debug()->MergeFrom(filter->debugValues());
        }
        filter->clearDebugValues();
    }
    status->mutable_debug()->set_source(amun::StrategyYellow);
#endif

    return status;
}

void Tracker::updateGeometry(const SSL_GeometryFieldSize &g)
{
    m_geometry.set_line_width(g.line_width() / 1000.0f);
    m_geometry.set_field_width(g.field_width() / 1000.0f);
    m_geometry.set_field_height(g.field_length() / 1000.0f);
    m_geometry.set_boundary_width(g.boundary_width() / 1000.0f);
    m_geometry.set_referee_width(g.referee_width() / 1000.0f);
    m_geometry.set_goal_width(g.goal_width() / 1000.0f);
    m_geometry.set_goal_depth(g.goal_depth() / 1000.0f);
    m_geometry.set_goal_wall_width(g.goal_wall_width() / 1000.0f);
    m_geometry.set_center_circle_radius(g.center_circle_radius() / 1000.0f);
    m_geometry.set_defense_radius(g.defense_radius() / 1000.0f);
    m_geometry.set_defense_stretch(g.defense_stretch() / 1000.0f);
    m_geometry.set_free_kick_from_defense_dist(g.free_kick_from_defense_dist() / 1000.0f);
    m_geometry.set_penalty_spot_from_field_line_dist(g.penalty_spot_from_field_line_dist() / 1000.0f);
    m_geometry.set_penalty_line_from_spot_dist(g.penalty_line_from_spot_dist() / 1000.0f);
    m_geometry.set_goal_height(0.16f);
}

void Tracker::updateCamera(const SSL_GeometryCameraCalibration &c)
{
    if (!c.has_derived_camera_world_tx() || !c.has_derived_camera_world_ty()
            || !c.has_derived_camera_world_tz()) {
        return;
    }
    Eigen::Vector3f cameraPos;
    cameraPos(0) = -c.derived_camera_world_ty() / 1000.f;
    cameraPos(1) = c.derived_camera_world_tx() / 1000.f;
    cameraPos(2) = c.derived_camera_world_tz() / 1000.f;

    m_cameraInfo->cameraPosition[c.camera_id()] = cameraPos;
    m_cameraInfo->focalLength[c.camera_id()] = c.focal_length();
}

template<class Filter>
void Tracker::invalidate(QList<Filter*> &filters, const qint64 maxTime, const qint64 maxTimeLast, qint64 currentTime)
{
    const int minFrameCount = 5;

    // remove outdated filters
    QMutableListIterator<Filter*> it(filters);
    while (it.hasNext()) {
        Filter *filter = it.next();
        // last robot has more time, but only if it's visible yet
        const qint64 timeLimit = (filters.size() > 1 || filter->frameCounter() < minFrameCount) ? maxTime : maxTimeLast;
        if (filter->lastUpdate() + timeLimit < currentTime) {
            delete filter;
            it.remove();
        }
    }
}

void Tracker::invalidateBall(qint64 currentTime)
{
    // Maximum tracking time if multiple balls are visible
    const qint64 maxTime = .1E9; // 0.1 s
    // Maximum tracking time for last ball
    const qint64 maxTimeLast = 1E9; // 1 s
    // remove outdated balls
    invalidate(m_ballFilter, maxTime, maxTimeLast, currentTime);
}

void Tracker::invalidateRobots(RobotMap &map, qint64 currentTime)
{
    // Maximum tracking time if multiple robots with same id are visible
    // Usually only one robot with a given id is visible, so this value
    // is hardly ever used
    const qint64 maxTime = .2E9; // 0.2 s
    // Maximum tracking time for last robot
    const qint64 maxTimeLast = 1E9; // 1 s

    // iterate over team
    for(RobotMap::iterator it = map.begin(); it != map.end(); ++it) {
        // remove outdated robots
        invalidate(*it, maxTime, maxTimeLast, currentTime);
    }
}

QList<RobotFilter *> Tracker::getBestRobots(qint64 currentTime)
{
    const qint64 resetTimeout = 100*1000*1000;
    // only return objects which have been tracked for more than minFrameCount frames
    // if the tracker was reset recently, allow for fast repopulation
    const int minFrameCount = (currentTime > m_resetTime + resetTimeout) ? 5: 0;

    QList<RobotFilter *> filters;

    for(RobotMap::iterator it = m_robotFilterYellow.begin(); it != m_robotFilterYellow.end(); ++it) {
        RobotFilter *robot = bestFilter(*it, minFrameCount);
        if (robot != NULL) {
            robot->update(currentTime);
            filters.append(robot);
        }
    }
    for(RobotMap::iterator it = m_robotFilterBlue.begin(); it != m_robotFilterBlue.end(); ++it) {
        RobotFilter *robot = bestFilter(*it, minFrameCount);
        if (robot != NULL) {
            robot->update(currentTime);
            filters.append(robot);
        }
    }
    return filters;
}

static RobotInfo nearestRobotInfo(const QList<RobotFilter *> &robots, const SSL_DetectionBall &b) {
    Eigen::Vector2f ball(-b.y()/1000, b.x()/1000); // convert from ssl vision coordinates

    RobotInfo rInfo{Eigen::Vector2f(0,0), Eigen::Vector2f(0,0), false, false};

    float minDist = 10000; // big enough for the ball filter

    RobotFilter *best = nullptr;
    for (RobotFilter *filter : robots) {
        Eigen::Vector2f dribbler = filter->dribblerPos();
        const float dist = (ball - dribbler).norm();
        if (dist < minDist || best == nullptr) {
            minDist = dist;
            best = filter;
            rInfo = {filter->robotPos(), filter->dribblerPos(), filter->kickIsChip(), filter->kickIsLinear()};
        }
    }
    return rInfo;
}

void Tracker::trackBall(const SSL_DetectionBall &ball, qint64 receiveTime, quint32 cameraId, const QList<RobotFilter *> &bestRobots)
{

    if (m_aoiEnabled && !isInAOI(ball.x(), ball.y() , m_flip, m_aoi_x1, m_aoi_y1, m_aoi_x2, m_aoi_y2)) {
        return;
    }
    if (! m_cameraInfo->cameraPosition.contains(cameraId)) {
        return;
    }
    RobotInfo robotInfo = nearestRobotInfo(bestRobots, ball);

    bool acceptingFilterWithCamId = false;
    BallTracker *acceptingFilterWithOtherCamId = nullptr;
    foreach (BallTracker *filter, m_ballFilter) {
        filter->update(receiveTime);
        if (filter->acceptDetection(ball, receiveTime, cameraId, robotInfo)) {
            if (filter->primaryCamera() == cameraId) {
                filter->addVisionFrame(ball, receiveTime, cameraId, robotInfo);
                acceptingFilterWithCamId = true;
            } else {
                // remember filter for copying its state in case that no filter
                // for the current camera does accept the frame
                // ideally, you would choose which filter to use for this
                acceptingFilterWithOtherCamId = filter;
            }
        }
    }

    if (!acceptingFilterWithCamId) {
        BallTracker* bt;
        if (acceptingFilterWithOtherCamId != nullptr) {
            // copy filter from old camera
            bt = new BallTracker(*acceptingFilterWithOtherCamId, cameraId);
        } else {
            // create new Ball Filter without initial movement
            bt = new BallTracker(ball, receiveTime, cameraId, m_cameraInfo, robotInfo);
        }
        m_ballFilter.append(bt);
        bt->addVisionFrame(ball, receiveTime, cameraId, robotInfo);
    } else {
        // only prioritize when detection was accepted
        prioritizeBallFilters();
    }
}

void Tracker::trackRobot(RobotMap &robotMap, const SSL_DetectionRobot &robot, qint64 receiveTime, qint32 cameraId)
{
    if (!robot.has_robot_id()) {
        return;
    }

    if (m_aoiEnabled && !isInAOI(robot.x(), robot.y() , m_flip, m_aoi_x1, m_aoi_y1, m_aoi_x2, m_aoi_y2)) {
        return;
    }

    // Data association for robot
    // For each detected robot search for nearest predicted robot
    // with same id.
    // If no robot is closer than .5 m create a new Kalman Filter

    float nearest = 0.5;
    RobotFilter *nearestFilter = NULL;

    QList<RobotFilter*>& list = robotMap[robot.robot_id()];
    foreach (RobotFilter *filter, list) {
        filter->update(receiveTime);
        const float dist = filter->distanceTo(robot);
        if (dist < nearest) {
            nearest = dist;
            nearestFilter = filter;
        }
    }

    if (!nearestFilter) {
        nearestFilter = new RobotFilter(robot, receiveTime);
        list.append(nearestFilter);
    }

    nearestFilter->addVisionFrame(cameraId, robot, receiveTime);
}

void Tracker::queuePacket(const QByteArray &packet, qint64 time)
{
    m_visionPackets.append(qMakePair(packet, time));
    m_hasVisionData = true;
}

void Tracker::queueRadioCommands(const QList<robot::RadioCommand> &radio_commands, qint64 time)
{
    foreach (const robot::RadioCommand &radioCommand, radio_commands) {
        // skip commands for which the team is unknown
        if (!radioCommand.has_is_blue()) {
            continue;
        }

        // add radio responses to every available filter
        const RobotMap &teamMap = radioCommand.is_blue() ? m_robotFilterBlue : m_robotFilterYellow;
        const QList<RobotFilter*>& list = teamMap.value(radioCommand.id());
        foreach (RobotFilter *filter, list) {
            filter->addRadioCommand(radioCommand.command(), time);
        }
    }
}

void Tracker::handleCommand(const amun::CommandTracking &command)
{
    if (command.has_aoi_enabled()) {
        m_aoiEnabled = command.aoi_enabled();
    }

    if (command.has_aoi()) {
        m_aoi_x1 = command.aoi().x1();
        m_aoi_y1 = command.aoi().y1();
        m_aoi_x2 = command.aoi().x2();
        m_aoi_y2 = command.aoi().y2();
    }

    if (command.has_system_delay()) {
        m_systemDelay = command.system_delay();
    }

    // allows resetting by the strategy
    if (command.reset()) {
        reset();
    }
}
