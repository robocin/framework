/***************************************************************************
 *   Copyright 2015 Florian Bauer, Michael Eischer, Jan Kallwies,          *
 *       Philipp Nordhus                                                   *
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

#include "path.h"
#include "kdtree.h"
#include "core/rng.h"
#include <cstdlib>
#include <sys/time.h>
#include <QDebug>


/*!
 * \class Path
 * \ingroup path
 * \brief Path-planner
 */

float Path::Circle::distance(const Vector &v) const
{
    return v.distance(center) - radius;
}

float Path::Circle::distance(const LineSegment &segment) const
{
    return segment.distance(center) - radius;
}

float Path::Line::distance(const Vector &v) const
{
    return segment.distance(v) - width;
}

float Path::Line::distance(const LineSegment &segment) const
{
    return segment.distance(this->segment) - width;
}

float Path::Rect::distance(const Vector &v) const
{
    float distX = std::max(bottom_left.x - v.x, v.x - top_right.x);
    float distY = std::max(bottom_left.y - v.y, v.y - top_right.y);

    if (distX >= 0 && distY >= 0) { // distance to corner
        return std::sqrt(distX*distX + distY*distY);
    } else if (distX < 0 && distY < 0) { // inside
        return std::max(distX, distY);
    } else if (distX < 0) {
        return distY; // distance to nearest side of the rectangle
    } else {
        return distX;
    }
}

float Path::Rect::distance(const LineSegment &segment) const
{
    // check if end is inside the rectangle
    if (segment.end().x >= bottom_left.x && segment.end().x <= top_right.x
            && segment.end().y >= bottom_left.y && segment.end().y <= top_right.y) {
        return 0;
    }
    // check if start is inside the rectangle
    if (segment.start().x >= bottom_left.x && segment.start().x <= top_right.x
            && segment.start().y >= bottom_left.y && segment.start().y <= top_right.y) {
        return 0;
    }

    Vector bottom_right(top_right.x, bottom_left.y);
    Vector top_left(bottom_left.x, top_right.y);

    float distTop = segment.distance(LineSegment(top_left, top_right));
    float distBottom = segment.distance(LineSegment(bottom_left, bottom_right));
    float distLeft = segment.distance(LineSegment(top_left, bottom_left));
    float distRight = segment.distance(LineSegment(top_right, bottom_right));

    return std::min(std::min(distTop, distBottom), std::min(distLeft, distRight));
}

float Path::Triangle::distance(const Vector &v) const
{
    // positive det == left, negative det == right
    const float det1 = Vector::det(p2, p3, v) / p2.distance(p3);
    const float det2 = Vector::det(p3, p1, v) / p3.distance(p1);
    const float det3 = Vector::det(p1, p2, v) / p1.distance(p2);
    float distance;

    // v lies inside the triangle
    // 3 positive dets
    if (det1 >= 0 && det2 >= 0 && det3 >= 0) {
        distance = -std::min(det1, std::min(det2, det3));
    }

    // v lies closest to a side
    // 2 positive dets, 1 negative det
    else if (det1 * det2 * det3 < 0) {
        distance = -std::min(det1, std::min(det2, det3));
    }

    // v lies closest to a corner
    // 1 positive det, 2 negative dets
    else if (det1 > 0) {
        distance = p1.distance(v);
    }
    else if (det2 > 0) {
        distance = p2.distance(v);
    }
    else if (det3 > 0) {
        distance = p3.distance(v);
    }

    else {
        qDebug() << "Error in Path::Triangle::distance()" << det1 << det2 << det3;
        return 42;
    }

    return distance - lineWidth;
}

float Path::Triangle::distance(const LineSegment &segment) const
{
    // at least one segment intersects a triangle side
    const LineSegment seg1(p1, p2);
    const LineSegment seg2(p2, p3);
    const LineSegment seg3(p3, p1);
    const float dseg1 = seg1.distance(segment);
    const float dseg2 = seg2.distance(segment);
    const float dseg3 = seg3.distance(segment);
    if (dseg1 * dseg2 * dseg3 == 0) {
        return 0;
    }

    // the segment lies entirely inside the triangle
    const float dstart = distance(segment.start());
    const float dend = distance(segment.end());
    if (dstart < 0 && dend < 0) {
        return 0;
    }

    // the segment lies entirely outside the triangle
    return std::max(std::min(dseg1, std::min(dseg2, dseg3)) - lineWidth, 0.f);
}

Path::Path(uint32_t rng_seed) :
    m_p_dest(0.1),
    m_p_wp(0.4),
    m_radius(-1.f),
    m_stepSize(0.1f),
    m_cacheSize(200),
    m_rng(new RNG(rng_seed)),
    m_treeStart(NULL),
    m_treeEnd(NULL)
{ }

Path::~Path()
{
    reset();
    delete m_rng;
}

void Path::reset()
{
    delete m_treeStart;
    m_treeStart = NULL;
    delete m_treeEnd;
    m_treeEnd = NULL;

    clearObstacles();
    m_waypoints.clear();
}

void Path::setRadius(float r)
{
    m_radius = r;
}

void Path::setBoundary(float x1, float y1, float x2, float y2)
{
    m_boundary.bottom_left.x = std::min(x1, x2);
    m_boundary.bottom_left.y = std::min(y1, y2);
    m_boundary.top_right.x = std::max(x1, x2);
    m_boundary.top_right.y = std::max(y1, y2);
}

void Path::addSeedTarget(float x, float y)
{
    m_seedTargets.append(Vector(x, y));
}

void Path::clearObstacles()
{
    m_circleObstacles.clear();
    m_rectObstacles.clear();
    m_triangleObstacles.clear();
    m_lineObstacles.clear();

    m_seedTargets.clear();
}

void Path::addCircle(float x, float y, float radius, const char* name, int prio)
{
    Circle c;
    c.center.x = x;
    c.center.y = y;
    c.radius = radius;
    c.name = name;
    c.prio = prio;
    m_circleObstacles.append(c);
}

void Path::addLine(float x1, float y1, float x2, float y2, float width, const char* name, int prio)
{
    Line l(Vector(x1, y1), Vector(x2, y2));
    l.width = width;
    l.name = name;
    l.prio = prio;
    m_lineObstacles.append(l);
}

void Path::addRect(float x1, float y1, float x2, float y2, const char* name, int prio)
{
    Rect r;
    r.bottom_left.x = std::min(x1, x2);
    r.bottom_left.y = std::min(y1, y2);
    r.top_right.x = std::max(x1, x2);
    r.top_right.y = std::max(y1, y2);
    r.name = name;
    r.prio = prio;
    m_rectObstacles.append(r);
}

void Path::addTriangle(float x1, float y1, float x2, float y2, float x3, float y3, float lineWidth, const char *name, int prio)
{
    Triangle t;
    t.lineWidth = lineWidth;
    t.name = name;
    t.prio = prio;

    // ensure that the triangle is oriented counter-clockwise
    const Vector a(x1, y1);
    const Vector b(x2, y2);
    const Vector c(x3, y3);
    const float det = Vector::det(a, b, c);
    if (det > 0) {
        t.p1 = a;
        t.p2 = b;
        t.p3 = c;
    } else {
        t.p1 = a;
        t.p2 = c;
        t.p3 = b;
    }
    m_triangleObstacles.append(t);
}


bool Path::testSpline(const robot::Spline &spline, float radius) const
{
    // check if any parts of the given spline collides with an obstacle
    const float start = spline.t_start();
    const float end = spline.t_end();
    if (std::isnan(start) || std::isinf(start)
            || std::isnan(end) || std::isinf(end)
            || end <= start) {
        return false;
    }
    const int steps = 10;
    const float step_size = (end - start) / steps;

    QVector<Vector> points;
    points.reserve(steps);

    float t = start;
    for (int i = 0; i < steps; ++i) {
        points << evalSpline(spline, t);
        t += step_size;
    }

    collectObstacles();
    for (int i = 1; i < points.size(); i++) {
        if (points[i - 1] == points[i]) {
            continue;
        }

        if (!test(LineSegment(points[i-1], points[i]), radius)) {
            return false;
        }
    }

    return true;
}

Vector Path::evalSpline(const robot::Spline &spline, float t) const
{
    Vector p;
    p.x = spline.x().a0() + (spline.x().a1() + (spline.x().a2() + spline.x().a3() * t) * t) * t;
    p.y = spline.y().a0() + (spline.y().a1() + (spline.y().a2() + spline.y().a3() * t) * t) * t;
    return p;
}

void Path::collectObstacles() const
{
    m_obstacles.clear();
    for (const Circle &c: m_circleObstacles) { m_obstacles.append(&c); }
    for (const Rect &r: m_rectObstacles) { m_obstacles.append(&r); }
    for (const Triangle &t: m_triangleObstacles) { m_obstacles.append(&t); }
    for (const Line &l: m_lineObstacles) { m_obstacles.append(&l); }
}

//! @brief calculate how far we are standing in the (multiple) obstacles
float Path::calculateObstacleCoverage(const Vector &v, const QVector<const Obstacle*> &obstacles, float robotRadius) const {
    float d_sum = 0;
    for (QVector<const Obstacle*>::const_iterator it = obstacles.constBegin();
                it != obstacles.constEnd(); ++it) {
        float d = (*it)->distance(v) - robotRadius;
        if (d < 0) {
            d_sum += std::min(2*robotRadius, -d);
        }
    }
    return d_sum;
}

bool Path::checkMovementRelativeToObstacles(const LineSegment &segment, const QVector<const Obstacle*> &obstacles, float radius) const {
    Vector p = segment.start();
    Vector step = segment.end() - segment.start();
    const float l = step.length();

    // Invalid line segment
    if (l == 0) {
        return false;
    }

    // only allow moving further inside the field
    if (outsidePlayfieldCoverage(segment.end(), radius) > outsidePlayfieldCoverage(segment.start(), radius)) {
        return false;
    }

    // split obstacle lists, the amount of start obstacles is decreasing for each tree
    QVector<const Obstacle *> startObstacles;
    QVector<const Obstacle *> tmpObstacles;
    QVector<const Obstacle *> otherObstacles;
    int maxObstaclePrio = -1;
    // nearly all obstacles should go in here
    otherObstacles.reserve(obstacles.size() - 1);
    // allow moving from an obstacle with high prio into one with lower prio
    foreach (const Obstacle *o, obstacles) {
        if (o->distance(p) < radius) {
            if (o->prio > maxObstaclePrio) {
                startObstacles.clear();
                maxObstaclePrio = o->prio;
            }
            startObstacles.append(o);
        } else {
            tmpObstacles.append(o);
        }
    }
    for (const Obstacle *o: tmpObstacles) {
        if (o->prio >= maxObstaclePrio) {
            otherObstacles.append(o);
        }
    }
    tmpObstacles.clear();

    if (startObstacles.size() == 1) {
        float stepSize = std::min(1E-3f, l);
        step *= stepSize / l;

        // check that the robot doesn't enter the obstacle any further
        // the obstacle is assumed to be convex and that distance inside an obstacle
        // is calculated as the distance to the closest point on the obstacle border
        // then the obstacle distance at start is increasing iff the obstacle is left
        float start_d_sum = calculateObstacleCoverage(p, startObstacles, radius);
        float step_d_sum = calculateObstacleCoverage(p+step, startObstacles, radius);

        if (step_d_sum > start_d_sum) {
            return false;
        }
    } else if (startObstacles.size() > 1) {
        float stepSize = 2E-3; // step size used to check for obstacles
        const int numSteps = ceil(l / stepSize); // parts path is split into
        stepSize = l / numSteps;
        // Adjust vector to stepsize
        if (l > stepSize) {
            step *= stepSize / l;
        }

        // check that the robot doesn't enter the obstacles any further
        // the obstacle coverage is limited to twice the robot radius
        // that is if the robot is completely covered it can freely move around
        // the robot can swing between the covered obstacles as the sum may stay the same
        float last_d_sum = INFINITY;
        for (int i = 0; i < numSteps + 1; i++) { // begin at segment start
            // Sum up distances to obstacles
            float d_sum = calculateObstacleCoverage(p, startObstacles, radius);

            // Cancel as soon as the coverage by obstacles is getting bigger (sucks!)
            if (d_sum > last_d_sum) {
                return false;
            } else if (d_sum == 0.f && i < numSteps) {
                if (!test(LineSegment(p, segment.end()), radius, startObstacles)) {
                    return false;
                }
                break;
            }

            last_d_sum = d_sum; // Save last value
            p = p + step; // Next step
        }
    }
    // new obstacles musn't be entered
    return test(segment, radius, otherObstacles);
}

void Path::setProbabilities(float p_dest, float p_wp)
{
    m_p_dest = p_dest;
    m_p_wp = p_wp;
}

Path::List Path::get(float start_x, float start_y, float end_x, float end_y)
{
    const int extendMultiSteps = 4;

    collectObstacles();

    const Vector start(start_x, start_y);
    const Vector end(end_x, end_y);

    const Vector middle = (start + end) / 2;
    // symmetric sampling around middle between start and end, that includes the complete field
    const float x_half = std::max(middle.x - m_boundary.bottom_left.x, m_boundary.top_right.x - middle.x);
    const float y_half = std::max(middle.y - m_boundary.bottom_left.y, m_boundary.top_right.y - middle.y);
    m_sampleRect.bottom_left = Vector(middle.x - x_half, middle.y - y_half);
    m_sampleRect.top_right = Vector(middle.x + x_half, middle.y + y_half);

    bool startingInObstacle = !pointInPlayfield(start, m_radius) || !test(start, m_radius, m_obstacles);
    bool endingInObstacle = !pointInPlayfield(end, m_radius) || !test(end, m_radius, m_obstacles);

    // setup tree rooted at the start
    delete m_treeStart;
    m_treeStart = new KdTree(start, startingInObstacle);
    // setup tree rooted at the end
    delete m_treeEnd;
    m_treeEnd = new KdTree(end, endingInObstacle);

    bool pathCompleted = false;
    // only use shortcuts if start and end point are not inside any obstacle or outside the playfield
    if (!startingInObstacle && !endingInObstacle) {
        // Test if direct connection from start to end is possible
        // If start and end-point are the same we are finished
        if (start == end) {
            pathCompleted = true;
        // otherwise we have to test if the direct way is free
        } else if (test(LineSegment(start, end), m_radius)) {
            pathCompleted = true;
            const KdTree::Node *nearestNode = m_treeStart->nearest(start);
            // raster path for usage as waypoint cache
            rasterPath(LineSegment(start, end), nearestNode, m_stepSize);
        }
    }

    KdTree *treeA = m_treeStart;
    KdTree *treeB = m_treeEnd;
    const KdTree::Node *mergerNode = NULL; // node where both trees have met

    if (!pathCompleted && m_seedTargets.size() > 0) {
        for (Vector seedTarget: m_seedTargets) {
            const KdTree::Node *nearestNode = m_treeStart->nearest(start);
            rasterPath(LineSegment(start, seedTarget), nearestNode, m_stepSize);
        }
    }

    // as the trees are rooted at the start and the end, the rrt will
    // leave obstacles at start and end before trying to merge the trees
    for (int iteration = 1; iteration < 300 && !pathCompleted; iteration++) {
        // Get a random target point (always inside the playfield)
        // the start tree should extend towards the end and vice versa
        Vector target = getTarget((treeA == m_treeStart)? end : start);
        // Find the node next to the target point
        const KdTree::Node *nearestNode = treeA->nearest(target);

        // extend towards the target
        nearestNode = extend(treeA, nearestNode, target, m_radius, m_stepSize);

        if (nearestNode != NULL) {
            // extend the other tree towards the new point
            target = treeA->position(nearestNode);
            nearestNode = treeB->nearest(target);
        }

        // extend for extendMultiSteps or until an obstacle is hit
        for (int i = 0; i < extendMultiSteps && nearestNode != NULL; ++i) {
            // Extend path towards the target by a short distance
            nearestNode = extend(treeB, nearestNode, target, m_radius, m_stepSize);
            if (nearestNode == NULL) {
                break;
            }

            // check if destination was reached
            const Vector &extended = treeB->position(nearestNode);
            float dist = extended.distance(target);
            // trees touched
            if (dist <= 0.00001f && !treeB->inObstacle(nearestNode)) {
                pathCompleted = true;
                mergerNode = nearestNode;
                break;
            }
        }
        std::swap(treeA, treeB);
    }


    Vector mid;
    const KdTree::Node *nearestNode;
    if (mergerNode != NULL) {
        // both trees have touched
        mid = m_treeStart->position(mergerNode);
        nearestNode = m_treeStart->nearest(mid);
    } else {
        // the trees didn't connect, just use the start tree
        nearestNode = m_treeStart->nearest(end);
        mid = m_treeStart->position(nearestNode);
    }

    QVector<Vector> points;
    {
        QVector<Vector> inversePoints;
        // traverse the start tree
        while (nearestNode) {
            inversePoints.append(m_treeStart->position(nearestNode));
            nearestNode = m_treeStart->previous(nearestNode);
        }
        points.reserve(inversePoints.length());
        for (int i = inversePoints.length() - 1; i >= 0; --i) {
            points.append(inversePoints.at(i));
        }
    }

    nearestNode = m_treeEnd->nearest(mid);
    // don't add the end tree if the trees aren't connected
    if (mergerNode != NULL) {
        // traverse the end tree, but skip the merger node
        nearestNode = m_treeEnd->previous(nearestNode);
        // add all nodes until entering an obstacle
        while (nearestNode && !m_treeEnd->inObstacle(nearestNode)) {
            points.append(m_treeEnd->position(nearestNode));
            nearestNode = m_treeEnd->previous(nearestNode);
        }
        // try to get as close to the target as possible if it's not reached yet
        if (nearestNode != NULL) {
            const Vector lineStart = points.last();
            Vector bestPos = findValidPoint(
                        LineSegment(lineStart, m_treeEnd->position(nearestNode)), m_radius);
            if (lineStart != bestPos && pointInPlayfield(bestPos, m_radius)
                    && test(LineSegment(lineStart, bestPos), m_radius)) {
                points.append(bestPos);
            }
        }
    }

    // don't keep more waypoints for a longer path
    float normalizedWaypointCount = std::ceil(start.distance(end) * 1.05 / m_stepSize);
    float keepProbability = qBound(0.f, (points.size() == 0) ? 0 : (normalizedWaypointCount / points.size()), 1.f);

    // update waypoint cache
    for (const Vector &pos: points) {
        float rand = float(m_rng->uniformInt()) / 0xffffffffU;
        if (rand <= keepProbability) {
            addToWaypointCache(pos);
        }
    }

    // add remaing points to the waypoint cache
    while (nearestNode) {
        addToWaypointCache(m_treeEnd->position(nearestNode));
        nearestNode = m_treeEnd->previous(nearestNode);
    }

    // cut corners serveral times
    for (int i = 0; i < 3; ++i) {
        simplify(points, m_radius);
        cutCorners(points, m_radius);
    }
    // final cleanup
    simplify(points, m_radius);

    Path::List list;
    list.reserve(points.size());
    foreach (const Vector &p, points) {
        Waypoint wp;
        wp.x = p.x;
        wp.y = p.y;
        list.append(wp);
    }

    return list;
}

const KdTree::Node * Path::rasterPath(const LineSegment &segment, const KdTree::Node *lastNode, float step_size) {
    // assumes that the collision check for segment was successfull
    const int steps = ceil(segment.start().distance(segment.end()) / step_size);
    for (int i = 0; i < steps; ++i) {
        lastNode = extend(m_treeStart, lastNode, segment.end(), m_radius, step_size);
        if (lastNode == NULL) { // target not reachable
            return lastNode;
        }
    }
    return lastNode;
}

void Path::simplify(QVector<Vector> &points, float radius)
{
    // every point before this index is inside the start obstacles
    int split = points.size();
    for (int i = 0; i < points.size(); ++i) {
        if (pointInPlayfield(points[i], m_radius) && test(points[i], radius, m_obstacles)) {
            split = i;
            break;
        }
    }

    for (int start_index = 0; start_index < points.size(); start_index++) {
        for (int end_index = points.size() - 1; end_index > start_index + 1; end_index--) {
            // common points in start and end tree, remove everything inbetween
            if (points[start_index] == points[end_index]) {
                split -= std::min(std::max(0, split - start_index), end_index - start_index);
                for (int i = 0; i < end_index - start_index; i++) {
                    points.removeAt(start_index);
                }
                break;
            }
            // if start point is in obstacle check that the robot leaves the obstacles
            // otherwise use the default check
            LineSegment seg(points[start_index], points[end_index]);
            if ((start_index < split && checkMovementRelativeToObstacles(seg, m_obstacles, radius))
                    || (start_index >= split && test(seg, radius))) {
                split -= std::min(std::max(0, split - start_index - 1), end_index - start_index - 1);
                for (int i = 0; i < end_index - start_index - 1; i++) {
                    points.removeAt(start_index + 1);
                }
                break;
            }
        }
    }
}

Vector Path::randomState() const
{
    Vector v(float(m_rng->uniformInt()) / 0xffffffffU, float(m_rng->uniformInt()) / 0xffffffffU);

    v.x = v.x * (m_sampleRect.top_right.x - m_sampleRect.bottom_left.x) + m_sampleRect.bottom_left.x;
    v.y = v.y * (m_sampleRect.top_right.y - m_sampleRect.bottom_left.y) + m_sampleRect.bottom_left.y;
    return v;
}

Vector Path::getTarget(const Vector &end)
{
    const float p = float(m_rng->uniformInt()) / 0xffffffffU;
    if (p < m_p_dest) {
        return end;
    } else if ((p < m_p_dest + m_p_wp) && !m_waypoints.isEmpty()) {
        int ofs = m_rng->uniformInt() % m_waypoints.size();
        return m_waypoints[ofs];
    } else {
        return randomState();
    }
}

void Path::addToWaypointCache(const Vector &pos)
{
    if (m_waypoints.size() < m_cacheSize) {
        // fill cache with m_cacheSize entries
        m_waypoints.append(pos);
    } else {
        // randomly replace waypoints
        int idx = m_rng->uniformInt() % m_cacheSize;
        m_waypoints[idx] = pos;
    }
}

bool Path::pointInPlayfield(const Vector &point, float radius) const {
    if (point.x - radius < m_boundary.bottom_left.x ||
           point.x + radius > m_boundary.top_right.x ||
           point.y - radius < m_boundary.bottom_left.y ||
           point.y + radius > m_boundary.top_right.y) {
        return false;
    }
    return true;
}

float Path::outsidePlayfieldCoverage(const Vector &point, float radius) const
{
    return std::max(0.f,
        std::max(
            std::max(m_boundary.bottom_left.x - point.x + radius, point.x + radius - m_boundary.top_right.x),
            std::max(m_boundary.bottom_left.y - point.y + radius, point.y + radius - m_boundary.top_right.y)
    ));
}

const KdTree::Node * Path::extend(KdTree *tree, const KdTree::Node *fromNode, const Vector &to, float radius, float stepSize)
{
    const Vector &from = tree->position(fromNode);
    const bool inObstacle = tree->inObstacle(fromNode);
    Vector d = to - from;
    const float l = d.length();
    if (l == 0) { // point already reached
        return NULL;
    } else if (l > stepSize) { // can't reach in one step
        d *= stepSize / l;
    }

    const Vector extended = from + d;

    // Check if the extended path is OK regarding obstacles
    bool success;

    // We are standing "in" an obstacle
    if (inObstacle) {
        // The new point is only valid if its farther away from the obstacles than right now
        // checking for outsidePlayfieldCoverage is not neccessary as target is always inside the playfield
        // and thus extended can't leave it
        success = checkMovementRelativeToObstacles(LineSegment(from, extended), m_obstacles, radius);
    } else { // otherwise test the new path for obstacles
        success = pointInPlayfield(extended, m_radius) && test(LineSegment(from, extended), radius);
    }

    // No valid path
    if (!success) {
        return NULL;
    }

    bool newInObstacle = false;
    // once every obstacle was left, reentering one is impossible
    // thus only test obstacleCoverage if we're currently in an obstacle
    if (inObstacle) {
        newInObstacle = !pointInPlayfield(extended, m_radius) || !test(extended, radius, m_obstacles);
    }
    // Extend tree
    return tree->insert(extended, newInObstacle, fromNode);
}

bool Path::test(const Vector &v, float radius, const QVector<const Obstacle*> &obstacles) const {
    if (!pointInPlayfield(v, radius)) {
        return false;
    }
    for(QVector<const Obstacle*>::const_iterator it = obstacles.constBegin();
                it != obstacles.constEnd(); ++it) {
        if ((*it)->distance(v) < radius) {
            return false;
        }
    }

    return true;
}

bool Path::test(const LineSegment &segment, float radius, const QVector<const Obstacle*> &obstacles) const
{
    for (QVector<const Obstacle*>::const_iterator it = obstacles.constBegin();
                it != obstacles.constEnd(); ++it) {
        if ((*it)->distance(segment) < radius) {
            return false;
        }
    }

    return true;
}

bool Path::test(const LineSegment &segment, float radius) const
{
    return test(segment, radius, m_obstacles);
}

Vector Path::findValidPoint(const LineSegment &segment, float radius) const
{
    // find the point using a binary search
    const Vector &lineStart = segment.start();
    Vector start = lineStart;
    Vector end = segment.end();
    float dist = start.distance(end);

    while (dist > 0.001f) {
        Vector mid = (end + start) / 2;
        if (pointInPlayfield(mid, m_radius) && test(LineSegment(lineStart, mid), radius)) {
            start = mid;
        } else {
            end = mid;
        }
        dist /= 2.0f;
    }

    return (start + end)/2;
}

void Path::cutCorners(QVector<Vector> &points, float radius)
{
    for (int i = 1; i < points.size() - 1; i++) {
        const Vector left = points[i - 1];
        const Vector mid = points[i];
        const Vector right = points[i + 1];

        Vector diffLeft = left - mid;
        Vector diffRight = right - mid;
        // max corner cutting distance
        float step = std::min(diffLeft.length(), diffRight.length());
        diffLeft = diffLeft.normalized();
        diffRight = diffRight.normalized();

        // start in the middle of [0; step] = step/2, the first change of dist will be +- step/4
        // just pretend a binary search will work, however there may be multiple seperate ranges
        // the found one will not neccessarily be the best
        step /= 2;
        float dist = step;
        float lastGood = 0.f;
        while (step > 0.01f) {
            // symmetrical corner cutting
            LineSegment line(mid + diffLeft * dist, mid + diffRight * dist);
            step /= 2;
            // don't check whether the new points are inside the playfield
            // only obstacles are important here, thus paths into the playfield can be smoothed
            if (test(line, radius)) {
                lastGood = dist;
                dist += step;
            } else {
                dist -= step;
            }
        }

        if (lastGood > 0.f) { // cut corner using last known good left and right pos
            points[i] = mid + diffLeft * lastGood;
            points.insert(++i, mid + diffRight * lastGood);
        }
    }
}

/*
void Path::calculateCorridor(const Vector &start, List &list, float radius)
{
    Vector previous = start;
    for (int i = 0; i < list.size(); i++) {
        const Vector p(list[i].x, list[i].y);
        float left = 10;
        float right = 10;
        const LineSegment line(previous, p);
        foreach (const Circle &c, m_circles) {
            const float r = radius + c.radius;
            float d = line.distanceDirect(c.center);
            if (d > 0) {
                d -= r;
                if (d < right)
                    right = d;
            } else {
                d = -d;
                d -= r;
                if (d < left)
                    left = d;
            }
        }
        list[i].l = left;
        list[i].r = right;

        previous = p;
    }
}
*/
