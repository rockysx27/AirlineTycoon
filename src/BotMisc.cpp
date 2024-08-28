#include "Bot.h"

#include "class.h"
#include "GameMechanic.h"
#include "TeakLibW.h"

SLONG Bot::calcCurrentGainFromJobs() const {
    SLONG gain = 0;
    for (auto planeId : mPlanesForJobs) {
        gain += Helper::calculateScheduleInfo(qPlayer, planeId).gain;
    }
    return gain;
}

SLONG Bot::calcRouteImageNeeded(const Bot::RouteInfo &routeInfo) const {
    auto routeImageTarget = std::min(100, (800 - getImage()) / 4);
    return (routeImageTarget - routeInfo.image);
}

void Bot::removePlaneFromRoute(SLONG planeId) {
    for (auto &route : mRoutes) {
        auto it = route.planeIds.begin();
        while (it != route.planeIds.end() && *it != planeId) {
            it++;
        }
        if (it != route.planeIds.end()) {
            route.planeIds.erase(it);
        }
    }
}

bool Bot::checkPlaneAvailable(SLONG planeId, bool printIfAvailable) const {
    const auto &qPlane = qPlayer.Planes[planeId];
    if (qPlane.AnzBegleiter < qPlane.ptAnzBegleiter) {
        redprintf("Bot::checkPlaneAvailable: Plane %s does not have enough crew members (%d, need %d).", Helper::getPlaneName(qPlane).c_str(),
                  qPlane.AnzBegleiter, qPlane.ptAnzBegleiter);
        return false;
    }
    if (qPlane.AnzPiloten < qPlane.ptAnzPiloten) {
        redprintf("Bot::checkPlaneAvailable: Plane %s does not have enough pilots (%d, need %d).", Helper::getPlaneName(qPlane).c_str(), qPlane.AnzPiloten,
                  qPlane.ptAnzPiloten);
        return false;
    }
    if (qPlane.Problem != 0) {
        redprintf("Bot::checkPlaneAvailable: Plane %s has a problem for the next %d hours", Helper::getPlaneName(qPlane).c_str(), qPlane.Problem);
        return false;
    }
    if (printIfAvailable) {
        hprintf("Bot::checkPlaneAvailable: Plane %s is available for service.", Helper::getPlaneName(qPlane).c_str());
    }
    return true;
}

bool Bot::checkPlaneLists() {
    auto &qPlanes = qPlayer.Planes;
    bool foundProblem = false;
    bool planesGoneMissing = false;
    std::unordered_map<SLONG, SLONG> uniquePlaneIds;

    std::vector<SLONG> newPlanesForJobs;
    for (const auto &i : mPlanesForJobs) {
        if (!qPlayer.Planes.IsInAlbum(i)) {
            redprintf("Bot::checkPlaneLists(): We lost the plane with ID = %d", i);
            planesGoneMissing = true;
            continue;
        }
        if (uniquePlaneIds.find(i) != uniquePlaneIds.end()) {
            redprintf("Bot::checkPlaneLists(): Plane with ID = %d appears in multiple lists: 1 and %d.", i, uniquePlaneIds[i]);
            foundProblem = true;
            continue;
        }
        uniquePlaneIds[i] = 1;
        newPlanesForJobs.push_back(i);
        hprintf("Bot::checkPlaneLists(): Jobs: Plane %s gains %s $", Helper::getPlaneName(qPlanes[i]).c_str(),
                (LPCTSTR)Insert1000erDots64(qPlanes[i].GetSaldo()));
    }
    std::swap(mPlanesForJobs, newPlanesForJobs);

    std::vector<SLONG> newPlanesForRoutes;
    for (const auto &i : mPlanesForRoutes) {
        if (!qPlayer.Planes.IsInAlbum(i)) {
            redprintf("Bot::checkPlaneLists(): We lost the plane with ID = %d", i);
            removePlaneFromRoute(i);
            planesGoneMissing = true;
            continue;
        }
        if (uniquePlaneIds.find(i) != uniquePlaneIds.end()) {
            redprintf("Bot::checkPlaneLists(): Plane with ID = %d appears in multiple lists: 2 and %d.", i, uniquePlaneIds[i]);
            foundProblem = true;
            continue;
        }
        uniquePlaneIds[i] = 2;
        newPlanesForRoutes.push_back(i);
        hprintf("Bot::checkPlaneLists(): Routes: Plane %s gains %s $", Helper::getPlaneName(qPlanes[i]).c_str(),
                (LPCTSTR)Insert1000erDots64(qPlanes[i].GetSaldo()));
    }
    std::swap(mPlanesForRoutes, newPlanesForRoutes);

    std::deque<SLONG> newPlanesJobsUnassigned;
    for (const auto &i : mPlanesForJobsUnassigned) {
        if (!qPlayer.Planes.IsInAlbum(i)) {
            redprintf("Bot::checkPlaneLists(): We lost the plane with ID = %d", i);
            planesGoneMissing = true;
            continue;
        }
        if (uniquePlaneIds.find(i) != uniquePlaneIds.end()) {
            redprintf("Bot::checkPlaneLists(): Plane with ID = %d appears in multiple lists: 3 and %d.", i, uniquePlaneIds[i]);
            foundProblem = true;
            continue;
        }
        uniquePlaneIds[i] = 3;
        newPlanesJobsUnassigned.push_back(i);
        hprintf("Bot::checkPlaneLists(): Jobs unassigned: Plane %s gains %s $", Helper::getPlaneName(qPlanes[i]).c_str(),
                (LPCTSTR)Insert1000erDots64(qPlanes[i].GetSaldo()));
    }
    std::swap(mPlanesForJobsUnassigned, newPlanesJobsUnassigned);

    std::deque<SLONG> newPlanesRoutesUnassigned;
    for (const auto &i : mPlanesForRoutesUnassigned) {
        if (!qPlayer.Planes.IsInAlbum(i)) {
            redprintf("Bot::checkPlaneLists(): We lost the plane with ID = %d", i);
            planesGoneMissing = true;
            continue;
        }
        if (uniquePlaneIds.find(i) != uniquePlaneIds.end()) {
            redprintf("Bot::checkPlaneLists(): Plane with ID = %d appears in multiple lists: 4 and %d.", i, uniquePlaneIds[i]);
            foundProblem = true;
            continue;
        }
        uniquePlaneIds[i] = 4;
        newPlanesRoutesUnassigned.push_back(i);
        hprintf("Bot::checkPlaneLists(): Routes unassigned: Plane %s gains %s $", Helper::getPlaneName(qPlanes[i]).c_str(),
                (LPCTSTR)Insert1000erDots64(qPlanes[i].GetSaldo()));
    }
    std::swap(mPlanesForRoutesUnassigned, newPlanesRoutesUnassigned);

    for (SLONG i = 0; i < qPlayer.Planes.AnzEntries(); i++) {
        SLONG id = qPlayer.Planes.GetIdFromIndex(i);
        if (qPlayer.Planes.IsInAlbum(i) && uniquePlaneIds.find(id) == uniquePlaneIds.end()) {
            const auto &qPlane = qPlayer.Planes[i];
            redprintf("Bot::checkPlaneLists(): Found new plane: %s (%lx).", Helper::getPlaneName(qPlane).c_str(), id);
            mPlanesForJobsUnassigned.push_back(id);
            foundProblem = true;
        }
    }

    /* check if we still have enough personal */
    findPlanesNotAvailableForService(mPlanesForJobs, mPlanesForJobsUnassigned);
    findPlanesNotAvailableForService(mPlanesForRoutes, mPlanesForRoutesUnassigned);

    /* maybe some planes now have crew? planes for routes will be checked in planRoutes() */
    findPlanesAvailableForService(mPlanesForJobsUnassigned, mPlanesForJobs);

    hprintf("Bot::checkPlaneLists(): Planes for jobs: %d / %d are available.", mPlanesForJobs.size(), mPlanesForJobs.size() + mPlanesForJobsUnassigned.size());
    hprintf("Bot::checkPlaneLists(): Planes for routes: %d / %d are available.", mPlanesForRoutes.size(),
            mPlanesForRoutes.size() + mPlanesForRoutesUnassigned.size());

    return foundProblem || planesGoneMissing;
}

void Bot::findPlanesNotAvailableForService(std::vector<SLONG> &listAvailable, std::deque<SLONG> &listUnassigned) {
    std::vector<SLONG> newAvailable;
    for (const auto id : listAvailable) {
        auto &qPlane = qPlayer.Planes[id];
        auto worstZustand = std::min(qPlane.WorstZustand, qPlane.Zustand);
        hprintf("Bot::findPlanesNotAvailableForService(): Plane %s: Zustand = %u, WorstZustand = %u, Baujahr = %d", Helper::getPlaneName(qPlane).c_str(),
                qPlane.Zustand, worstZustand, qPlane.Baujahr);

        SLONG mode = 0; /* 0: keep plane in service */
        if (qPlane.Zustand <= kPlaneMinimumZustand) {
            hprintf("Bot::findPlanesNotAvailableForService(): Plane %s not available for service: Needs repairs.", Helper::getPlaneName(qPlane).c_str());
            mode = 1; /* 1: phase plane out */
        }
        if (!checkPlaneAvailable(id, false)) {
            redprintf("Bot::findPlanesNotAvailableForService(): Plane %s not available for service: No crew.", Helper::getPlaneName(qPlane).c_str());
            mode = 2; /* 2: remove plane immediately from service */
        }
        if (mode > 0) {
            listUnassigned.push_back(id);
            if (mode == 2) {
                GameMechanic::clearFlightPlan(qPlayer, id);
            }

            /* remove plane from route */
            removePlaneFromRoute(id);
        } else {
            newAvailable.push_back(id);
        }
    }
    std::swap(listAvailable, newAvailable);
}

void Bot::findPlanesAvailableForService(std::deque<SLONG> &listUnassigned, std::vector<SLONG> &listAvailable) {
    std::deque<SLONG> newUnassigned;
    for (const auto id : listUnassigned) {
        auto &qPlane = qPlayer.Planes[id];
        auto worstZustand = std::min(qPlane.WorstZustand, qPlane.Zustand);
        hprintf("Bot::findPlanesAvailableForService(): Plane %s: Zustand = %u, WorstZustand = %u, Baujahr = %d", Helper::getPlaneName(qPlane).c_str(),
                qPlane.Zustand, worstZustand, qPlane.Baujahr);

        if (qPlane.Zustand < 100 && (qPlane.Zustand < qPlane.TargetZustand)) {
            hprintf("Bot::findPlanesAvailableForService(): Plane %s still not available for service: Needs repairs.", Helper::getPlaneName(qPlane).c_str());
            newUnassigned.push_back(id);
        } else if (!checkPlaneAvailable(id, false)) {
            redprintf("Bot::findPlanesAvailableForService(): Plane %s still not available for service: No crew.", Helper::getPlaneName(qPlane).c_str());
            newUnassigned.push_back(id);
        } else {
            hprintf("Bot::findPlanesAvailableForService(): Putting plane %s back into service.", Helper::getPlaneName(qPlane).c_str());
            listAvailable.push_back(id);
        }
    }
    std::swap(listUnassigned, newUnassigned);
}

const CRentRoute &Bot::getRentRoute(const Bot::RouteInfo &routeInfo) const { return qPlayer.RentRouten.RentRouten[routeInfo.routeId]; }

const CRoute &Bot::getRoute(const Bot::RouteInfo &routeInfo) const { return Routen[routeInfo.routeId]; }

__int64 Bot::getDailyOpSaldo() const { return qPlayer.BilanzGestern.GetOpSaldo(); }

__int64 Bot::getWeeklyOpSaldo() const { return qPlayer.BilanzWoche.Hole().GetOpSaldo(); }

SLONG Bot::getImage() const { return (qPlayer.HasBerater(BERATERTYP_GELD) < 50) ? mCurrentImage : qPlayer.Image; }

void Bot::forceReplanning() { qPlayer.RobotActions[1].ActionId = ACTION_NONE; }
