// Port of mxgraph/src/view/mxEdgeStyle.js (SegmentConnector :600,
// OrthConnector :1141 with its static tables, getJettySize :981) (draw.io,
// Apache-2.0), modified: view scale 1, translate 0, routing center 0, locals
// instead of the shared static scratch arrays, no `innerLoopWaypoints`.
//
// As upstream, `result` already holds absolutePoints[0] as its first entry.
// getRoutePattern/inlineRoutePatterns are not ported: nothing in this
// upstream version calls them (OrthConnector indexes routePatterns itself).

#include "routing_internal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace qraw::geometry::detail {

namespace {

constexpr double kOrthBuffer = 10; // mxEdgeStyle.orthBuffer

bool contains(const Rect& b, double x, double y)
{
    return b.x <= x && b.x + b.width >= x && b.y <= y && b.y + b.height >= y;
}

OptPoint scalePoint(const OptPoint& p)
{
    if (!p)
        return std::nullopt;
    return Point{round10(p->x), round10(p->y)};
}

// mxEdgeStyle.scaleCellState: terminal bounds rounded to 0.1.
std::optional<Rect> scaleTerminal(const Terminal* t)
{
    if (!t)
        return std::nullopt;
    return Rect{round10(t->bounds.x), round10(t->bounds.y), round10(t->bounds.width), round10(t->bounds.height)};
}

Point centerOf(const Rect& r) { return {r.x + r.width / 2, r.y + r.height / 2}; }

// mxEdgeStyle.getJettySize. nullopt in the request means "auto".
double jettySize(const EdgeRouteRequest& request, bool isSource)
{
    const std::optional<double>& specific = isSource ? request.sourceJettySize : request.targetJettySize;
    if (specific)
        return *specific;
    if (request.jettySize)
        return *request.jettySize;
    // Neither style set: upstream's default is orthBuffer, but draw.io
    // writes `auto` for fresh edges; the request cannot tell them apart and
    // documents nullopt as "auto".
    const double markerSize = isSource ? request.startMarkerSize : request.endMarkerSize;
    // A missing marker (size 0) and a small marker both give 2 * orthBuffer.
    return std::max(2.0, std::ceil((markerSize + kOrthBuffer) / kOrthBuffer)) * kOrthBuffer;
}

} // namespace

// ------------------------------------------------------------ SegmentConnector

void segmentConnector(const StyleInput& in, PointList& result)
{
    constexpr double tol = 1;

    const OptPoint p0 = scalePoint(in.p0); // pts[0]
    const OptPoint pe = scalePoint(in.pe); // pts[lastInx]
    const std::optional<Rect> source = scaleTerminal(in.source);
    const std::optional<Rect> target = scaleTerminal(in.target);
    // getChannelBounds returns the state itself for ordinary shapes.
    const std::optional<Rect>& sourceChannel = source;
    const std::optional<Rect>& targetChannel = target;

    OptPoint pt = p0;
    if (!pt && source)
        pt = centerOf(*source);
    if (!pt)
        return; // upstream would dereference null: an end with neither terminal nor point

    std::vector<Point> tempPoints;
    OptPoint lastPushed = result.empty() ? std::nullopt : result.front();
    bool horizontal = true;
    std::optional<Point> hint;

    // Adds waypoints only if outside of the tolerance.
    auto pushPoint = [&](Point p) {
        p.x = round10(p.x);
        p.y = round10(p.y);
        if (!lastPushed || std::abs(lastPushed->x - p.x) >= tol || std::abs(lastPushed->y - p.y) >= tol) {
            result.emplace_back(p);
            lastPushed = p;
        }
    };

    if (!in.hints.empty()) {
        std::vector<Point> hints = in.hints;

        // Aligns source and target hint to the fixed points.
        if (std::abs(hints.front().x - pt->x) < tol)
            hints.front().x = pt->x;
        if (std::abs(hints.front().y - pt->y) < tol)
            hints.front().y = pt->y;
        if (pe) {
            if (std::abs(hints.back().x - pe->x) < tol)
                hints.back().x = pe->x;
            if (std::abs(hints.back().y - pe->y) < tol)
                hints.back().y = pe->y;
        }

        hint = hints.front();
        const std::optional<Rect>* currentChannel = &sourceChannel;
        OptPoint currentPt = p0;
        bool hozChan = false;
        bool vertChan = false;
        Point currentHint = *hint;
        bool haveTerm = !currentPt && source; // currentTerm != null

        // Check for alignment with fixed points and with channels at the
        // source and target segments only.
        for (int i = 0; i < 2; ++i) {
            const bool fixedVertAlign = currentPt && currentPt->x == currentHint.x;
            const bool fixedHozAlign = currentPt && currentPt->y == currentHint.y;
            const bool inHozChan = haveTerm && currentHint.y >= (*currentChannel)->y &&
                                   currentHint.y <= (*currentChannel)->y + (*currentChannel)->height;
            const bool inVertChan = haveTerm && currentHint.x >= (*currentChannel)->x &&
                                    currentHint.x <= (*currentChannel)->x + (*currentChannel)->width;
            hozChan = fixedHozAlign || (!currentPt && inHozChan);
            vertChan = fixedVertAlign || (!currentPt && inVertChan);

            // A hint in both channels of a floating end, or exactly on a
            // fixed point, says nothing about the orientation: try the
            // target end.
            if (i == 0 && ((hozChan && vertChan) || (fixedVertAlign && fixedHozAlign))) {
                // fall through to the target end
            } else {
                if (currentPt && (!fixedHozAlign && !fixedVertAlign) && (inHozChan || inVertChan)) {
                    horizontal = !inHozChan;
                    break;
                }
                if (vertChan || hozChan) {
                    horizontal = hozChan;
                    if (i == 1) // work back from the target end
                        horizontal = hints.size() % 2 == 0 ? hozChan : vertChan;
                    break;
                }
            }

            currentChannel = &targetChannel;
            currentPt = pe;
            haveTerm = !currentPt && target;
            currentHint = hints.back();
            if (fixedVertAlign && fixedHozAlign)
                hints.erase(hints.begin());
        }

        if (horizontal && ((p0 && p0->y != hint->y) ||
                           (!p0 && source && (hint->y < sourceChannel->y || hint->y > sourceChannel->y + sourceChannel->height)))) {
            tempPoints.push_back({pt->x, hint->y});
        } else if (!horizontal && ((p0 && p0->x != hint->x) ||
                                   (!p0 && source && (hint->x < sourceChannel->x || hint->x > sourceChannel->x + sourceChannel->width)))) {
            tempPoints.push_back({hint->x, pt->y});
        }

        if (horizontal)
            pt->y = hint->y;
        else
            pt->x = hint->x;

        for (const Point& h : hints) {
            horizontal = !horizontal;
            hint = h;
            if (horizontal)
                pt->y = h.y;
            else
                pt->x = h.x;
            tempPoints.push_back(*pt);
        }
    } else {
        hint = pt;
        horizontal = true;
    }

    // Adds the last point.
    OptPoint last = pe;
    if (!last && target)
        last = centerOf(*target);
    if (last && hint) {
        if (horizontal && ((pe && pe->y != hint->y) ||
                           (!pe && target && (hint->y < targetChannel->y || hint->y > targetChannel->y + targetChannel->height)))) {
            tempPoints.push_back({last->x, hint->y});
        } else if (!horizontal && ((pe && pe->x != hint->x) ||
                                   (!pe && target && (hint->x < targetChannel->x || hint->x > targetChannel->x + targetChannel->width)))) {
            tempPoints.push_back({hint->x, last->y});
        }
    }

    // Removes bends inside the terminals.
    if (!p0 && source) {
        while (!tempPoints.empty() && contains(*sourceChannel, tempPoints.front().x, tempPoints.front().y))
            tempPoints.erase(tempPoints.begin());
    }
    if (!pe && target) {
        while (!tempPoints.empty() && contains(*targetChannel, tempPoints.back().x, tempPoints.back().y))
            tempPoints.pop_back();
    }

    for (const Point& p : tempPoints)
        pushPoint(p);

    // Removes the last point if it is inside the tolerance of the end point.
    if (pe && !result.empty() && result.back() && std::abs(pe->x - result.back()->x) <= tol &&
        std::abs(pe->y - result.back()->y) <= tol) {
        result.pop_back();
        // Lines up the second last point with the end point.
        if (!result.empty() && result.back()) {
            if (std::abs(result.back()->x - pe->x) < tol)
                result.back()->x = pe->x;
            if (std::abs(result.back()->y - pe->y) < tol)
                result.back()->y = pe->y;
        }
    }
}

// --------------------------------------------------------------- OrthConnector

namespace {

// Route pattern masks. Low nibble: direction (WEST 1, NORTH 2, SOUTH 4,
// EAST 8); bits 5-8: side (LEFT_MASK 32 ... BOTTOM_MASK 256); CENTER 512;
// SOURCE 1024; TARGET 2048.
constexpr int kSideMask = 480;
constexpr int kCenterMask = 512;
constexpr int kSourceMask = 1024;
constexpr int kTargetMask = 2048;

using Pattern = std::vector<int>;

const Pattern& routePattern(int sourceIndex, int targetIndex)
{
    // mxEdgeStyle.routePatterns[sourceIndex - 1][targetIndex - 1]
    static const Pattern patterns[4][4] = {
        {{513, 2308, 2081, 2562},
         {513, 1090, 514, 2184, 2114, 2561},
         {513, 1090, 514, 2564, 2184, 2562},
         {513, 2308, 2561, 1090, 514, 2568, 2308}},
        {{514, 1057, 513, 2308, 2081, 2562},
         {514, 2184, 2114, 2561},
         {514, 2184, 2562, 1057, 513, 2564, 2184},
         {514, 1057, 513, 2568, 2308, 2561}},
        {{1090, 514, 1057, 513, 2308, 2081, 2562},
         {2114, 2561},
         {1090, 2562, 1057, 513, 2564, 2184},
         {1090, 514, 1057, 513, 2308, 2561, 2568}},
        {{2081, 2562},
         {1057, 513, 1090, 514, 2184, 2114, 2561},
         {1057, 513, 1090, 514, 2184, 2562, 2564},
         {1057, 2561, 1090, 514, 2568, 2308}},
    };
    return patterns[sourceIndex - 1][targetIndex - 1];
}

// mxEdgeStyle.dirVectors
constexpr int kDirVectors[7][2] = {{-1, 0}, {0, -1}, {1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 0}};

// mxUtils.getBoundingBox for a rotation around the rectangle's center.
Rect rotatedBoundingBox(const Rect& r, double rotation)
{
    const double rad = rotation * std::numbers::pi / 180;
    const double cos = std::cos(rad);
    const double sin = std::sin(rad);
    const Point c = centerOf(r);
    const std::array<Point, 4> corners{{{r.x, r.y}, {r.x + r.width, r.y}, {r.x + r.width, r.y + r.height}, {r.x, r.y + r.height}}};
    double minX = 0;
    double minY = 0;
    double maxX = 0;
    double maxY = 0;
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const double x = corners[i].x - c.x;
        const double y = corners[i].y - c.y;
        const double rx = x * cos - y * sin + c.x;
        const double ry = y * cos + x * sin + c.y;
        if (i == 0) {
            minX = maxX = rx;
            minY = maxY = ry;
        } else {
            minX = std::min(minX, rx);
            maxX = std::max(maxX, rx);
            minY = std::min(minY, ry);
            maxY = std::max(maxY, ry);
        }
    }
    return {minX, minY, maxX - minX, maxY - minY};
}

} // namespace

void orthConnector(const StyleInput& in, PointList& result)
{
    const OptPoint p0 = scalePoint(in.p0);
    const OptPoint pe = scalePoint(in.pe);
    const std::optional<Rect> source = scaleTerminal(in.source);
    const std::optional<Rect> target = scaleTerminal(in.target);
    if ((!source && !p0) || (!target && !pe))
        return; // an end with neither terminal nor point

    double sourceX = source ? source->x : p0->x;
    double sourceY = source ? source->y : p0->y;
    double sourceWidth = source ? source->width : 1;
    double sourceHeight = source ? source->height : 1;
    double targetX = target ? target->x : pe->x;
    double targetY = target ? target->y : pe->y;
    double targetWidth = target ? target->width : 1;
    double targetHeight = target ? target->height : 1;

    double sourceBuffer = jettySize(in.request, true);
    double targetBuffer = jettySize(in.request, false);
    if (source && in.request.isSelfLoop) {
        targetBuffer = std::max(sourceBuffer, targetBuffer);
        sourceBuffer = targetBuffer;
    }
    const double totalBuffer = targetBuffer + sourceBuffer;

    bool tooShort = false;
    if (p0 && pe) {
        const double dx = pe->x - p0->x;
        const double dy = pe->y - p0->y;
        tooShort = dx * dx + dy * dy < totalBuffer * totalBuffer;
    }

    // Too short, or the user placed waypoints: the segment connector
    // (orthPointsFallback is true upstream).
    if (tooShort || !in.hints.empty()) {
        segmentConnector(in, result);
        return;
    }

    int portConstraint[2] = {kMaskAll, kMaskAll};
    if (source) {
        portConstraint[0] = portConstraints(*in.source, kMaskAll);
        if (in.source->rotation != 0) {
            const Rect nr = rotatedBoundingBox({sourceX, sourceY, sourceWidth, sourceHeight}, in.source->rotation);
            sourceX = nr.x;
            sourceY = nr.y;
            sourceWidth = nr.width;
            sourceHeight = nr.height;
        }
    }
    if (target) {
        portConstraint[1] = portConstraints(*in.target, kMaskAll);
        if (in.target->rotation != 0) {
            const Rect nr = rotatedBoundingBox({targetX, targetY, targetWidth, targetHeight}, in.target->rotation);
            targetX = nr.x;
            targetY = nr.y;
            targetWidth = nr.width;
            targetHeight = nr.height;
        }
    }

    if (sourceWidth == 0 || sourceHeight == 0 || targetWidth == 0 || targetHeight == 0)
        return;

    int dir[2] = {0, 0};
    const double geo[2][4] = {{sourceX, sourceY, sourceWidth, sourceHeight}, {targetX, targetY, targetWidth, targetHeight}};
    const double buffer[2] = {sourceBuffer, targetBuffer};

    // limits[terminal][side]: the jetty lines around the terminal, indexed by
    // the LEFT 1, TOP 2, RIGHT 4, BOTTOM 8 side masks.
    double limits[2][9] = {};
    for (int i = 0; i < 2; ++i) {
        limits[i][1] = geo[i][0] - buffer[i];
        limits[i][2] = geo[i][1] - buffer[i];
        limits[i][4] = geo[i][0] + geo[i][2] + buffer[i];
        limits[i][8] = geo[i][1] + geo[i][3] + buffer[i];
    }

    // Quadrant of the target relative to the source (0..3).
    const double sourceCenX = geo[0][0] + geo[0][2] / 2.0;
    const double sourceCenY = geo[0][1] + geo[0][3] / 2.0;
    const double targetCenX = geo[1][0] + geo[1][2] / 2.0;
    const double targetCenY = geo[1][1] + geo[1][3] / 2.0;
    const double cdx = sourceCenX - targetCenX;
    const double cdy = sourceCenY - targetCenY;
    int quad = 0;
    if (cdx < 0) {
        quad = cdy < 0 ? 2 : 1;
    } else if (cdy <= 0) {
        quad = cdx == 0 ? 2 : 3;
    }

    // Fixed ends give the direction (the side the point is on) and the
    // fraction of the terminal.
    OptPoint currentTerm;
    if (source)
        currentTerm = p0;
    double constraint[2][2] = {{0.5, 0.5}, {0.5, 0.5}};
    if (!source)
        constraint[0][0] = constraint[0][1] = 0; // top-left corner (= exact p0)
    if (!target)
        constraint[1][0] = constraint[1][1] = 0; // top-left corner (= exact pe)

    for (int i = 0; i < 2; ++i) {
        if (currentTerm) {
            constraint[i][0] = (currentTerm->x - geo[i][0]) / geo[i][2];
            if (std::abs(currentTerm->x - geo[i][0]) <= 1)
                dir[i] = kMaskWest;
            else if (std::abs(currentTerm->x - geo[i][0] - geo[i][2]) <= 1)
                dir[i] = kMaskEast;

            constraint[i][1] = (currentTerm->y - geo[i][1]) / geo[i][3];
            if (std::abs(currentTerm->y - geo[i][1]) <= 1)
                dir[i] = kMaskNorth;
            else if (std::abs(currentTerm->y - geo[i][1] - geo[i][3]) <= 1)
                dir[i] = kMaskSouth;
        }
        currentTerm.reset();
        if (target)
            currentTerm = pe;
    }

    const double sourceTopDist = geo[0][1] - (geo[1][1] + geo[1][3]);
    const double sourceLeftDist = geo[0][0] - (geo[1][0] + geo[1][2]);
    const double sourceBottomDist = geo[1][1] - (geo[0][1] + geo[0][3]);
    const double sourceRightDist = geo[1][0] - (geo[0][0] + geo[0][2]);

    // Free space between the vertices beyond both jetties, per direction
    // index (1 west, 2 north, 3 east, 4 south).
    const double vertexSeparations[5] = {0, std::max(sourceLeftDist - totalBuffer, 0.0),
                                         std::max(sourceTopDist - totalBuffer, 0.0),
                                         std::max(sourceRightDist - totalBuffer, 0.0),
                                         std::max(sourceBottomDist - totalBuffer, 0.0)};

    int horPref[2];
    int vertPref[2];
    horPref[0] = sourceLeftDist >= sourceRightDist ? kMaskWest : kMaskEast;
    vertPref[0] = sourceTopDist >= sourceBottomDist ? kMaskNorth : kMaskSouth;
    horPref[1] = reversePortConstraints(horPref[0]);
    vertPref[1] = reversePortConstraints(vertPref[0]);

    const double preferredHorizDist = sourceLeftDist >= sourceRightDist ? sourceLeftDist : sourceRightDist;
    const double preferredVertDist = sourceTopDist >= sourceBottomDist ? sourceTopDist : sourceBottomDist;

    int prefOrdering[2][2] = {{0, 0}, {0, 0}};
    bool preferredOrderSet = false;

    // If the preferred side is not allowed by the port constraint, use the
    // opposite one.
    for (int i = 0; i < 2; ++i) {
        if (dir[i] != 0)
            continue;
        if ((horPref[i] & portConstraint[i]) == 0)
            horPref[i] = reversePortConstraints(horPref[i]);
        if ((vertPref[i] & portConstraint[i]) == 0)
            vertPref[i] = reversePortConstraints(vertPref[i]);
        prefOrdering[i][0] = vertPref[i];
        prefOrdering[i][1] = horPref[i];
    }

    if (preferredVertDist > 0 && preferredHorizDist > 0) {
        // Possibility of an L-shape route.
        if ((horPref[0] & portConstraint[0]) > 0 && (vertPref[1] & portConstraint[1]) > 0) {
            prefOrdering[0][0] = horPref[0];
            prefOrdering[0][1] = vertPref[0];
            prefOrdering[1][0] = vertPref[1];
            prefOrdering[1][1] = horPref[1];
            preferredOrderSet = true;
        } else if ((vertPref[0] & portConstraint[0]) > 0 && (horPref[1] & portConstraint[1]) > 0) {
            prefOrdering[0][0] = vertPref[0];
            prefOrdering[0][1] = horPref[0];
            prefOrdering[1][0] = horPref[1];
            prefOrdering[1][1] = vertPref[1];
            preferredOrderSet = true;
        }
    }
    if (preferredVertDist > 0 && !preferredOrderSet) {
        prefOrdering[0][0] = vertPref[0];
        prefOrdering[0][1] = horPref[0];
        prefOrdering[1][0] = vertPref[1];
        prefOrdering[1][1] = horPref[1];
        preferredOrderSet = true;
    }
    if (preferredHorizDist > 0 && !preferredOrderSet) {
        prefOrdering[0][0] = horPref[0];
        prefOrdering[0][1] = vertPref[0];
        prefOrdering[1][0] = horPref[1];
        prefOrdering[1][1] = vertPref[1];
        preferredOrderSet = true;
    }

    // dirPref packs four preferences per terminal, one per byte.
    int dirPref[2] = {0, 0};
    for (int i = 0; i < 2; ++i) {
        if (dir[i] != 0)
            continue;
        if ((prefOrdering[i][0] & portConstraint[i]) == 0)
            prefOrdering[i][0] = prefOrdering[i][1];

        dirPref[i] = prefOrdering[i][0] & portConstraint[i];
        dirPref[i] |= (prefOrdering[i][1] & portConstraint[i]) << 8;
        dirPref[i] |= (prefOrdering[1 - i][i] & portConstraint[i]) << 16;
        dirPref[i] |= (prefOrdering[1 - i][1 - i] & portConstraint[i]) << 24;

        if ((dirPref[i] & 0xF) == 0)
            dirPref[i] = dirPref[i] << 8;
        if ((dirPref[i] & 0xF00) == 0)
            dirPref[i] = (dirPref[i] & 0xF) | (dirPref[i] >> 8);
        if ((dirPref[i] & 0xF0000) == 0)
            dirPref[i] = (dirPref[i] & 0xFFFF) | ((dirPref[i] & 0xF000000) >> 8);

        dir[i] = dirPref[i] & 0xF;

        if (portConstraint[i] == kMaskWest || portConstraint[i] == kMaskNorth || portConstraint[i] == kMaskEast ||
            portConstraint[i] == kMaskSouth)
            dir[i] = portConstraint[i];
    }

    int sourceIndex = dir[0] == kMaskEast ? 3 : dir[0];
    int targetIndex = dir[1] == kMaskEast ? 3 : dir[1];
    sourceIndex -= quad;
    targetIndex -= quad;
    if (sourceIndex < 1)
        sourceIndex += 4;
    if (targetIndex < 1)
        targetIndex += 4;
    if (sourceIndex > 4 || targetIndex > 4)
        return; // unreachable for the direction values above; keeps the table lookup safe

    const Pattern& pattern = routePattern(sourceIndex, targetIndex);

    double wayPoints[12][2] = {};
    wayPoints[0][0] = geo[0][0];
    wayPoints[0][1] = geo[0][1];
    switch (dir[0]) {
    case kMaskWest:
        wayPoints[0][0] -= sourceBuffer;
        wayPoints[0][1] += constraint[0][1] * geo[0][3];
        break;
    case kMaskSouth:
        wayPoints[0][0] += constraint[0][0] * geo[0][2];
        wayPoints[0][1] += geo[0][3] + sourceBuffer;
        break;
    case kMaskEast:
        wayPoints[0][0] += geo[0][2] + sourceBuffer;
        wayPoints[0][1] += constraint[0][1] * geo[0][3];
        break;
    case kMaskNorth:
        wayPoints[0][0] += constraint[0][0] * geo[0][2];
        wayPoints[0][1] -= sourceBuffer;
        break;
    default:
        break;
    }

    int currentIndex = 0;
    // Orientation of the last way point: 0 = horizontal (x), 1 = vertical (y).
    int lastOrientation = (dir[0] & (kMaskEast | kMaskWest)) > 0 ? 0 : 1;
    const int initialOrientation = lastOrientation;
    int currentOrientation = 0;

    for (const int step : pattern) {
        const int nextDirection = step & 0xF;
        int directionIndex = nextDirection == kMaskEast ? 3 : nextDirection;
        directionIndex += quad;
        if (directionIndex > 4)
            directionIndex -= 4;

        const int* direction = kDirVectors[directionIndex - 1];
        currentOrientation = (directionIndex % 2 > 0) ? 0 : 1;

        if (currentOrientation != lastOrientation) {
            ++currentIndex;
            // Copy the previous way point.
            wayPoints[currentIndex][0] = wayPoints[currentIndex - 1][0];
            wayPoints[currentIndex][1] = wayPoints[currentIndex - 1][1];
        }

        const bool tar = (step & kTargetMask) > 0;
        const bool sou = (step & kSourceMask) > 0;
        int side = (step & kSideMask) >> 5;
        side = side << quad;
        if (side > 0xF)
            side = side >> 4;
        const bool center = (step & kCenterMask) > 0;

        if ((sou || tar) && side < 9) {
            double limit = 0;
            const int souTar = sou ? 0 : 1;
            if (center && currentOrientation == 0)
                limit = geo[souTar][0] + constraint[souTar][0] * geo[souTar][2];
            else if (center)
                limit = geo[souTar][1] + constraint[souTar][1] * geo[souTar][3];
            else
                limit = limits[souTar][side];

            if (currentOrientation == 0) {
                const double deltaX = (limit - wayPoints[currentIndex][0]) * direction[0];
                if (deltaX > 0)
                    wayPoints[currentIndex][0] += direction[0] * deltaX;
            } else {
                const double deltaY = (limit - wayPoints[currentIndex][1]) * direction[1];
                if (deltaY > 0)
                    wayPoints[currentIndex][1] += direction[1] * deltaY;
            }
        } else if (center) {
            // Half the free space between the vertices in this direction.
            const double half = std::abs(vertexSeparations[directionIndex] / 2);
            wayPoints[currentIndex][0] += direction[0] * half;
            wayPoints[currentIndex][1] += direction[1] * half;
        }

        if (currentIndex > 0 && wayPoints[currentIndex][currentOrientation] == wayPoints[currentIndex - 1][currentOrientation])
            --currentIndex;
        else
            lastOrientation = currentOrientation;
    }

    for (int i = 0; i <= currentIndex; ++i) {
        if (i == currentIndex) {
            // Check the last way point: it must leave the target in the
            // target direction, else it is dropped.
            const int targetOrientation = (dir[1] & (kMaskEast | kMaskWest)) > 0 ? 0 : 1;
            const int sameOrient = targetOrientation == initialOrientation ? 0 : 1;
            if (sameOrient != (currentIndex + 1) % 2)
                break;
        }
        result.emplace_back(Point{round10(wayPoints[i][0]), round10(wayPoints[i][1])});
    }

    // Removes duplicate way points (including the one against absolutePoints[0]).
    std::size_t index = 1;
    while (index < result.size()) {
        if (!result[index - 1] || !result[index] || result[index - 1]->x != result[index]->x ||
            result[index - 1]->y != result[index]->y)
            ++index;
        else
            result.erase(result.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

} // namespace qraw::geometry::detail
