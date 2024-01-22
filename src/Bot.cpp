#include "Bot.h"

#include "AtNet.h"
#include "BotHelper.h"

#include <unordered_map>

// Preise verstehen sich pro Sitzplatz:
extern SLONG SeatCosts[];
extern SLONG FoodCosts[];
extern SLONG TrayCosts[];
extern SLONG DecoCosts[];

// Preise pro Flugzeuge:
extern SLONG TriebwerkCosts[];
extern SLONG ReifenCosts[];
extern SLONG ElektronikCosts[];
extern SLONG SicherheitCosts[];

static const SLONG kMoneyEmergencyFund = 100000;
static const SLONG kSmallestAdCampaign = 3;
static const SLONG kMaximumRouteUtilization = 95;

struct RouteScore {
    DOUBLE score{};
    SLONG routeId{-1};
    SLONG planeTypeId{-1};
};

static const char *getPrioName(Bot::Prio prio) {
    switch (prio) {
    case Bot::Prio::Top:
        return "Top";
    case Bot::Prio::High:
        return "High";
    case Bot::Prio::Medium:
        return "Medium";
    case Bot::Prio::Low:
        return "Low";
    case Bot::Prio::None:
        return "None";
    default:
        redprintf("Bot.cpp: Default case should not be reached.");
        DebugBreak();
        return "INVALID";
    }
    return "INVALID";
}

bool Bot::hoursPassed(SLONG room, SLONG hours) const {
    const auto it = mLastTimeInRoom.find(room);
    if (it == mLastTimeInRoom.end()) {
        return true;
    }
    return (Sim.Time - it->second > hours * 60000);
}

bool Bot::haveDiscount() const {
    if (qPlayer.HasBerater(BERATERTYP_SICHERHEIT) >= 50 || Sim.Date >= 3) {
        return true; /* wait until we have some discount */
    }
    return false;
}

std::pair<SLONG, SLONG> Bot::kerosineQualiOptimization(__int64 moneyAvailable, DOUBLE targetFillRatio) const {
    hprintf("Bot::kerosineQualiOptimization(): Buying kerosine for no more than %lld $ and %.2f %% of capacity", moneyAvailable, targetFillRatio * 100);

    std::pair<SLONG, SLONG> res{};
    DOUBLE priceGood = Sim.HoleKerosinPreis(1);
    DOUBLE priceBad = Sim.HoleKerosinPreis(2);
    if (qPlayer.HasBerater(BERATERTYP_KEROSIN) < 70) {
        /* just buy normal kerosine */
        res.first = static_cast<SLONG>(std::floor(moneyAvailable / priceGood));
        res.second = 0;
        return res;
    }

    DOUBLE qualiZiel = 1.0;
    if (qPlayer.HasBerater(BERATERTYP_KEROSIN) >= 90) {
        qualiZiel = 1.3;
    } else if (qPlayer.HasBerater(BERATERTYP_KEROSIN) >= 80) {
        qualiZiel = 1.2;
    } else if (qPlayer.HasBerater(BERATERTYP_KEROSIN) >= 70) {
        qualiZiel = 1.1;
    }

    DOUBLE qualiStart = qPlayer.KerosinQuali;
    DOUBLE amountGood = 0;
    DOUBLE amountBad = 0;
    DOUBLE tankContent = qPlayer.TankInhalt;
    DOUBLE tankMax = qPlayer.Tank * targetFillRatio;

    if (tankContent >= tankMax) {
        return res;
    }

    // Definitions:
    // aG := amountGood, aB := amountBad, mA := moneyAvailable, pG := priceGood, pB := priceBaD,
    // T := tankMax, Ti := tankContent, qS := qualiStart, qZ := qualiZiel
    // Given are the following two equations:
    // I: aG*pG + aB*pB = mA    (spend all money for either good are bad kerosine)
    // II: qZ = (Ti*qS + aB*2 + aG) / (Ti + aB + aG)   (new quality qZ depends on amounts bought)
    // Solve I for aG:
    // aG = (mA - aB*pB) / pG
    // Solve II for aB:
    // qZ*(Ti + aB + aG) = Ti*qS + aB*2 + aG;
    // qZ*Ti + qZ*(aB + aG) = Ti*qS + aB*2 + aG;
    // qZ*Ti - Ti*qS = aB*2 + aG - qZ*(aB + aG);
    // Ti*(qZ - qS) = aB*(2 - qZ) + aG*(1 - qZ);
    // Insert I in II to eliminate aG
    // Ti*(qZ - qS) = aB*(2 - qZ) + ((mA - aB*pB) / pG)*(1 - qZ);
    // Ti*(qZ - qS) = aB*(2 - qZ) + mA*(1 - qZ) / pG - aB*pB*(1 - qZ) / pG;
    // Ti*(qZ - qS) - mA*(1 - qZ) / pG = aB*((2 - qZ) - pB*(1 - qZ) / pG);
    // (Ti*(qZ - qS) - mA*(1 - qZ) / pG) / ((2 - qZ) - pB*(1 - qZ) / pG) = aB;
    DOUBLE nominator = (tankContent * (qualiZiel - qualiStart) - moneyAvailable * (1 - qualiZiel) / priceGood);
    DOUBLE denominator = ((2 - qualiZiel) - priceBad * (1 - qualiZiel) / priceGood);

    // Limit:
    amountBad = std::max(0.0, nominator / denominator);               // equation II
    amountGood = (moneyAvailable - amountBad * priceBad) / priceGood; // equation I
    if (amountGood < 0) {
        amountGood = 0;
        amountBad = moneyAvailable / priceBad;
    }

    // Round:
    if (amountGood > INT_MAX) {
        amountGood = INT_MAX;
    }
    if (amountBad > INT_MAX) {
        amountBad = INT_MAX;
    }
    res.first = static_cast<SLONG>(std::floor(amountGood));
    res.second = static_cast<SLONG>(std::floor(amountBad));

    // we have more than enough money to fill the tank, calculate again using this for equation I:
    // I: aG = (T - Ti - aB)  (cannot exceed tank capacity)
    // Insert I in II
    // Ti*(qZ - qS) = aB*(2 - qZ) + (T - Ti - aB)*(1 - qZ);
    // Ti*(qZ - qS) = aB*(2 - qZ) + (T - Ti)*(1 - qZ) - aB*(1 - qZ);
    // Ti*(qZ - qS) - (T - Ti)*(1 - qZ) = aB*(2 - qZ) - aB*(1 - qZ);
    // Ti*(qZ - qS) - (T - Ti)*(1 - qZ) = aB*((2 - qZ) - (1 - qZ));
    // (Ti*(qZ - qS) - (T - Ti)*(1 - qZ)) / ((2 - qZ) - (1 - qZ)) = aB;
    if (res.first + res.second + tankContent > tankMax) {
        DOUBLE nominator = (tankContent * (qualiZiel - qualiStart) - (tankMax - tankContent) * (1 - qualiZiel));
        DOUBLE denominator = ((2 - qualiZiel) - (1 - qualiZiel));

        // Limit:
        amountBad = std::min(tankMax - tankContent, std::max(0.0, nominator / denominator));
        amountGood = (tankMax - tankContent - amountBad);

        // Round:
        if (amountGood > INT_MAX) {
            amountGood = INT_MAX;
        }
        if (amountBad > INT_MAX) {
            amountBad = INT_MAX;
        }
        res.first = static_cast<SLONG>(std::floor(amountGood));
        res.second = static_cast<SLONG>(std::floor(amountBad));
    }

    return res;
}

std::vector<SLONG> Bot::findBestAvailablePlaneType() const {
    CDataTable planeTable;
    planeTable.FillWithPlaneTypes();
    std::vector<std::pair<SLONG, SLONG>> scores;
    for (const auto &i : planeTable.LineIndex) {
        if (!PlaneTypes.IsInAlbum(i)) {
            continue;
        }
        const auto &planeType = PlaneTypes[i];

        if (!GameMechanic::checkPlaneTypeAvailable(i)) {
            continue;
        }

        SLONG score = planeType.Passagiere * planeType.Passagiere;
        score += planeType.Reichweite;
        score /= planeType.Verbrauch;
        scores.emplace_back(i, score);
    }
    std::sort(scores.begin(), scores.end(), [](const std::pair<SLONG, SLONG> &a, const std::pair<SLONG, SLONG> &b) { return a.second > b.second; });

    std::vector<SLONG> bestList;
    bestList.reserve(scores.size());
    for (const auto &i : scores) {
        bestList.push_back(i.first);
    }

    hprintf("Bot::findBestAvailablePlaneType(): Best plane type is %s", (LPCTSTR)PlaneTypes[bestList[0]].Name);
    return bestList;
}

SLONG Bot::calcCurrentGainFromJobs() const {
    SLONG gain = 0;
    for (auto planeId : mPlanesForJobs) {
        gain += Helper::calculateScheduleGain(qPlayer, planeId);
    }
    return gain;
}

void Bot::printGainFromJobs(SLONG oldGain) const {
    SLONG gain = 0;
    for (auto planeId : mPlanesForJobs) {
        gain += Helper::calculateScheduleGain(qPlayer, planeId);
    }
    hprintf("Bot::printGainFromJobs(): Improved gain from jobs from %ld to %ld.", oldGain, gain);
}

const CRentRoute &Bot::getRentRoute(const Bot::RouteInfo &routeInfo) const { return qPlayer.RentRouten.RentRouten[routeInfo.routeId]; }

const CRoute &Bot::getRoute(const Bot::RouteInfo &routeInfo) const { return Routen[routeInfo.routeId]; }

std::pair<SLONG, SLONG> Bot::findBestRoute(TEAKRAND &rnd) const {
    auto isBuyable = GameMechanic::getBuyableRoutes(qPlayer);
    auto bestPlanes = findBestAvailablePlaneType();

    std::vector<RouteScore> bestRoutes;
    RouteScore bestForMission;
    for (SLONG c = 0; c < Routen.AnzEntries(); c++) {
        if (isBuyable[c] == 0) {
            continue;
        }

        SLONG planeTypeId = -1;
        for (SLONG i : bestPlanes) {
            if (Cities.CalcDistance(Routen[c].VonCity, Routen[c].NachCity) > PlaneTypes[i].Reichweite * 1000) {
                continue; /* too far */
            }
            if (Cities.CalcFlugdauer(Routen[c].VonCity, Routen[c].NachCity, PlaneTypes[i].Geschwindigkeit) >= 24) {
                continue; /* takes too long */
            }
            planeTypeId = i;
            break;
        }
        if (planeTypeId == -1) {
            continue;
        }

        DOUBLE score = Routen[c].AnzPassagiere();
        score += Cities.CalcDistance(Routen[c].VonCity, Routen[c].NachCity);
        score /= Routen[c].Miete;
        bestRoutes.emplace_back(RouteScore{score, c, planeTypeId});

        // Ist die Route für die Mission wichtig?
        if (qPlayer.RobotUse(ROBOT_USE_ROUTEMISSION)) {
            SLONG d = 0;

            for (d = 0; d < 6; d++) {
                if ((Routen[c].VonCity == static_cast<ULONG>(Sim.HomeAirportId) && Routen[c].NachCity == static_cast<ULONG>(Sim.MissionCities[d])) ||
                    (Routen[c].NachCity == static_cast<ULONG>(Sim.HomeAirportId) && Routen[c].VonCity == static_cast<ULONG>(Sim.MissionCities[d]))) {
                    bestForMission.routeId = c;
                    bestForMission.planeTypeId = planeTypeId;
                    if (rnd.Rand(2) == 0) {
                        break;
                    }
                }
            }
            if (bestForMission.routeId != -1) {
                hprintf("Bot::findBestRoute(): Best route for mission (using plane type %s) is: ", (LPCTSTR)PlaneTypes[bestForMission.routeId].Name);
                Helper::printRoute(Routen[bestForMission.routeId]);
                return {bestForMission.routeId, bestForMission.planeTypeId};
            }
        }
    }

    std::sort(bestRoutes.begin(), bestRoutes.end(), [](const RouteScore &a, const RouteScore &b) { return a.score > b.score; });
    for (const auto &i : bestRoutes) {
        hprintf("Bot::findBestRoute(): Score of route %s (using plane type %s) is: %.2f", Helper::getRouteName(Routen[i.routeId]).c_str(),
                (LPCTSTR)PlaneTypes[i.planeTypeId].Name, i.score);
    }

    hprintf("Bot::findBestRoute(): Best route (using plane type %s) is: ", (LPCTSTR)PlaneTypes[bestRoutes[0].planeTypeId].Name);
    Helper::printRoute(Routen[bestRoutes[0].routeId]);
    return {bestRoutes[0].routeId, bestRoutes[0].planeTypeId};
}

void Bot::rentRoute(SLONG routeA, SLONG planeTypeId) {
    assert(routeA != -1);
    assert(planeTypeId != -1);

    /* find route in reverse direction */
    SLONG routeB = -1;
    for (SLONG c = 0; c < Routen.AnzEntries(); c++) {
        if ((Routen.IsInAlbum(c) != 0) && Routen[c].VonCity == Routen[routeA].NachCity && Routen[c].NachCity == Routen[routeA].VonCity) {
            routeB = c;
            break;
        }
    }
    if (-1 == routeB) {
        redprintf("Bot::rentRoute: Unable to find route in reverse direction.");
        return;
    }

    if (GameMechanic::rentRoute(qPlayer, routeA)) {
        mRoutes.emplace_back(routeA, routeB, planeTypeId);
        hprintf("Bot::rentRoute(): Renting route %s (using plane type %s): ", Helper::getRouteName(getRoute(mRoutes.back())).c_str(),
                (LPCTSTR)PlaneTypes[planeTypeId].Name);

        updateRouteInfo();
    }
}

void Bot::updateRouteInfo() {
    SLONG numRented = 0;
    for (const auto &rentRoute : qPlayer.RentRouten.RentRouten) {
        if (rentRoute.Rang != 0) {
            numRented++;
        }
    }

    assert(numRented % 2 == 0);
    assert(numRented / 2 <= mRoutes.size());
    if (numRented / 2 < mRoutes.size()) {
        redprintf("We lost %ld routes!", mRoutes.size() - numRented);
    }

    for (auto &route : mRoutes) {
        route.image = getRentRoute(route).Image;
        route.utilization = getRentRoute(route).Auslastung;
    }

    if (mRoutes.empty()) {
        return;
    }

    mRoutesSortedByUtilization.resize(mRoutes.size());
    mRoutesSortedByImage.resize(mRoutes.size());
    for (int i = 0; i < mRoutes.size(); i++) {
        mRoutesSortedByUtilization[i] = i;
        mRoutesSortedByImage[i] = i;
    }
    std::sort(mRoutesSortedByUtilization.begin(), mRoutesSortedByUtilization.end(),
              [&](SLONG a, SLONG b) { return mRoutes[a].utilization < mRoutes[b].utilization; });
    std::sort(mRoutesSortedByImage.begin(), mRoutesSortedByImage.end(), [&](SLONG a, SLONG b) { return mRoutes[a].image < mRoutes[b].image; });

    auto lowImage = mRoutesSortedByImage[0];
    auto lowUtil = mRoutesSortedByUtilization[0];
    hprintf("Bot::updateRouteInfo(): Route %s has lowest image: %ld", Helper::getRouteName(getRoute(mRoutes[lowImage])).c_str(), mRoutes[lowImage].image);
    hprintf("Bot::updateRouteInfo(): Route %s has lowest utilization: %ld", Helper::getRouteName(getRoute(mRoutes[lowUtil])).c_str(),
            mRoutes[lowUtil].utilization);

    if (mRoutes[lowUtil].utilization < kMaximumRouteUtilization) {
        mWantToRentRouteId = -1;
        mBuyPlaneForRouteId = mRoutes[lowUtil].planeTypeId;
        hprintf("Bot::rentRoute(): Need to buy another %s for route %s: ", (LPCTSTR)PlaneTypes[mBuyPlaneForRouteId].Name,
                Helper::getRouteName(getRoute(mRoutes[lowUtil])).c_str());
    }
}

void Bot::planRoutes() {
    if (mRoutes.empty()) {
        return;
    }

    /* assign planes to routes */
    SLONG numUnassigned = mPlanesForRoutesUnassigned.size();
    for (int i = 0; i < numUnassigned; i++) {
        if (mRoutes[mRoutesSortedByImage[0]].utilization >= kMaximumRouteUtilization) {
            break; /* No more underutilized routes */
        }

        SLONG planeId = mPlanesForRoutesUnassigned.front();
        const auto &qPlane = qPlayer.Planes[planeId];
        mPlanesForRoutesUnassigned.pop_front();

        SLONG targetRouteIdx = -1;
        for (SLONG routeIdx : mRoutesSortedByUtilization) {
            auto &qRoute = mRoutes[routeIdx];
            if (qRoute.utilization >= kMaximumRouteUtilization) {
                break; /* No more underutilized routes */
            }
            if (qRoute.planeTypeId != qPlane.TypeId) {
                continue;
            }
            targetRouteIdx = routeIdx;
            break;
        }
        if (targetRouteIdx != -1) {
            auto &qRoute = mRoutes[targetRouteIdx];
            qRoute.planeIds.push_back(planeId);
            mPlanesForRoutes.push_back(planeId);
            hprintf("Assigning plane %s (%s) to route %s", (LPCTSTR)qPlane.Name, (LPCTSTR)PlaneTypes[qPlane.TypeId].Name,
                    Helper::getRouteName(getRoute(qRoute)).c_str());
        } else {
            mPlanesForRoutesUnassigned.push_back(planeId);
        }
    }

    /* plan route flights */
    for (auto &qRoute : mRoutes) {
        SLONG durationA = Cities.CalcFlugdauer(getRoute(qRoute).VonCity, getRoute(qRoute).NachCity, PlaneTypes[qRoute.planeTypeId].Geschwindigkeit);
        SLONG durationB = Cities.CalcFlugdauer(getRoute(qRoute).NachCity, getRoute(qRoute).VonCity, PlaneTypes[qRoute.planeTypeId].Geschwindigkeit);
        SLONG roundTripDuration = durationA + durationB;

        int timeSlot = 0;
        for (auto planeId : qRoute.planeIds) {
            const auto &qPlane = qPlayer.Planes[planeId];

            /* where is the plane right now and when can it be in the origin city? */
            PlaneTime availTime;
            SLONG availCity{};
            std::tie(availTime, availCity) = Helper::getPlaneAvailableTimeLoc(qPlane);
            if (availCity != getRoute(qRoute).VonCity) {
                availTime += kDurationExtra + Cities.CalcFlugdauer(availCity, getRoute(qRoute).VonCity, qPlane.ptGeschwindigkeit);
            }

            /* planes on same route fly with 3 hours inbetween */
            SLONG h = availTime.getDate() * 24 + availTime.getHour();
            h = (h + roundTripDuration - 1) / roundTripDuration * roundTripDuration;
            h += 3 * (timeSlot++);
            availTime = {h / 24, h % 24};

            /* always schedule A=>B and B=>A, for the next 5 days */
            SLONG numScheduled = 0;
            PlaneTime curTime = availTime;
            while (curTime.getDate() <= Sim.Date + 5) {
                if (!GameMechanic::planRouteJob(qPlayer, planeId, qRoute.routeId, curTime.getDate(), curTime.getHour())) {
                    redprintf("Bot::planRoutes(): GameMechanic::planRouteJob returned error!");
                    return;
                }
                curTime += durationA;
                if (!GameMechanic::planRouteJob(qPlayer, planeId, qRoute.routeReverseId, curTime.getDate(), curTime.getHour())) {
                    redprintf("Bot::planRoutes(): GameMechanic::planRouteJob returned error!");
                    return;
                }
                curTime += durationB;
                numScheduled++;
            }
            hprintf("Scheduled route %s %ld times for plane %s, starting at %s %ld", Helper::getRouteName(getRoute(qRoute)).c_str(), numScheduled,
                    (LPCTSTR)qPlane.Name, (LPCTSTR)Helper::getWeekday(availTime.getDate()), availTime.getHour());
            Helper::printFlightJobs(qPlayer, planeId);
        }
    }
}

SLONG Bot::calcNumberOfShares(__int64 moneyAvailable, DOUBLE kurs) const { return static_cast<SLONG>(std::floor((moneyAvailable - 100) / (1.1 * kurs))); }

SLONG Bot::calcNumOfFreeShares(SLONG playerId) const {
    auto &player = Sim.Players.Players[playerId];
    SLONG amount = player.AnzAktien;
    for (SLONG c = 0; c < 4; c++) {
        amount -= Sim.Players.Players[c].OwnsAktien[playerId];
    }
    return amount;
}

Bot::Prio Bot::condAll(SLONG actionId) {
    __int64 moneyAvailable = 0;
    switch (actionId) {
    case ACTION_RAISEMONEY:
        return condRaiseMoney(moneyAvailable);
    case ACTION_DROPMONEY:
        return condDropMoney(moneyAvailable);
    case ACTION_EMITSHARES:
        return condEmitShares(moneyAvailable);
    case ACTION_CHECKAGENT1:
        return condCheckLastMinute();
    case ACTION_CHECKAGENT2:
        return condCheckTravelAgency();
    case ACTION_CHECKAGENT3:
        return condCheckFreight();
    case ACTION_STARTDAY:
        return condStartDay();
    case ACTION_PERSONAL:
        return condVisitHR();
    case ACTION_VISITKIOSK:
        return condVisitMisc();
    case ACTION_VISITMECH:
        return condVisitMech(moneyAvailable);
    case ACTION_VISITDUTYFREE:
        return condVisitDutyFree(moneyAvailable);
    case ACTION_VISITAUFSICHT:
        return condVisitBoss(moneyAvailable);
    case ACTION_VISITNASA:
        return condVisitNasa(moneyAvailable);
    case ACTION_VISITTELESCOPE:
        return condVisitMisc();
    case ACTION_VISITRICK:
        return condVisitMisc();
    case ACTION_VISITROUTEBOX:
        return condVisitRouteBoxPlanning();
    case ACTION_VISITROUTEBOX2:
        return condVisitRouteBoxRenting(moneyAvailable);
    case ACTION_VISITSECURITY:
        return condVisitSecurity(moneyAvailable);
    case ACTION_VISITDESIGNER:
        return condVisitDesigner(moneyAvailable);
    case ACTION_VISITSECURITY2:
        return condSabotageSecurity();
    case ACTION_BUYUSEDPLANE:
        return condBuyUsedPlane(moneyAvailable);
    case ACTION_BUYNEWPLANE:
        return condBuyNewPlane(moneyAvailable);
    case ACTION_WERBUNG:
        return condBuyAds(moneyAvailable);
    case ACTION_SABOTAGE:
        return condSabotage(moneyAvailable);
    case ACTION_UPGRADE_PLANES:
        return condUpgradePlanes(moneyAvailable);
    case ACTION_BUY_KEROSIN:
        return condBuyKerosine(moneyAvailable);
    case ACTION_BUY_KEROSIN_TANKS:
        return condBuyKerosineTank(moneyAvailable);
    case ACTION_SET_DIVIDEND:
        return condIncreaseDividend(moneyAvailable);
    case ACTION_BUYSHARES:
        return std::max(condBuyNemesisShares(moneyAvailable, mDislike), condBuyOwnShares(moneyAvailable));
    case ACTION_SELLSHARES:
        return condSellShares(moneyAvailable);
    case ACTION_WERBUNG_ROUTES:
        return condBuyAdsForRoutes(moneyAvailable);
    case ACTION_CALL_INTERNATIONAL:
        return condCallInternational();
    default:
        redprintf("Bot.cpp: Default case should not be reached.");
        DebugBreak();
        return Prio::None;
    }
    return Prio::None;
}

/** How to write condXYZ() function
 * If action costs money: Initialize moneyAvailable
 * Check if enough hours passed since last time action was executed.
 * Check if all other conditions for this actions are met (e.g. RobotUse(), is action even possible right now?)
 * If actions costs money:
 *      Reduce moneyAvailable by amount that should not be spent for this action
 *      Check if moneyAvailable is still larger/equal than minimum amount necessary for action (don't modify moneyAvailable again!)
 * Return priority of this action.
 */

Bot::Prio Bot::condStartDay() {
    if (!hoursPassed(ACTION_STARTDAY, 24)) {
        return Prio::None;
    }
    return Prio::Top;
}

Bot::Prio Bot::condCallInternational() {
    if (!hoursPassed(ACTION_CALL_INTERNATIONAL, 2)) {
        return Prio::None;
    }
    if (!qPlayer.RobotUse(ROBOT_USE_ABROAD)) {
        return Prio::None;
    }
    if (qPlayer.RobotUse(ROBOT_USE_MUCH_FRACHT) && Sim.GetHour() <= 9) {
        return Prio::None;
    }
    if (mPlanesForJobs.empty()) {
        return Prio::None;
    }
    for (SLONG c = 0; c < 4; c++) {
        if ((Sim.Players.Players[c].IsOut == 0) && (qPlayer.Kooperation[c] != 0)) {
            return Prio::High; /* we have a cooperation partner, we can check if they have a branch office */
        }
    }
    for (SLONG n = 0; n < Cities.AnzEntries(); n++) {
        if (n == Cities.find(Sim.HomeAirportId)) {
            continue;
        }
        if (qPlayer.RentCities.RentCities[n].Rang != 0U) {
            return Prio::High; /* we have our own branch office */
        }
    }
    return Prio::None;
}

Bot::Prio Bot::condCheckLastMinute() {
    if (!hoursPassed(ACTION_CHECKAGENT1, 2)) {
        return Prio::None;
    }
    return Prio::High;
}
Bot::Prio Bot::condCheckTravelAgency() {
    if (!hoursPassed(ACTION_CHECKAGENT2, 2)) {
        return Prio::None;
    }
    return Prio::High;
}
Bot::Prio Bot::condCheckFreight() {
    if (!hoursPassed(ACTION_CHECKAGENT3, 2)) {
        return Prio::None;
    }
    if (!qPlayer.RobotUse(ROBOT_USE_FRACHT) || true) { // TODO
        return Prio::None;
    }
    return Prio::Medium;
}

Bot::Prio Bot::condUpgradePlanes(__int64 &moneyAvailable) {
    moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
    if (!hoursPassed(ACTION_UPGRADE_PLANES, 4)) {
        return Prio::None;
    }
    if (!qPlayer.RobotUse(ROBOT_USE_LUXERY)) {
        return Prio::None;
    }
    if (!haveDiscount()) {
        return Prio::None; /* wait until we have some discount */
    }
    moneyAvailable -= 1000 * 1000;
    SLONG minCost = 550 * (SeatCosts[2] - SeatCosts[0] / 2); /* assuming 550 seats (777) */
    if (!mPlanesForRoutes.empty() && moneyAvailable >= minCost) {
        return Prio::Medium;
    }
    return Prio::None;
}

Bot::Prio Bot::condBuyNewPlane(__int64 &moneyAvailable) {
    moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
    if (!hoursPassed(ACTION_BUYNEWPLANE, 2)) {
        return Prio::None;
    }
    if (!qPlayer.RobotUse(ROBOT_USE_MAKLER)) {
        return Prio::None;
    }
    if (qPlayer.RobotUse(ROBOT_USE_MAX4PLANES) && numPlanes() >= 4) {
        return Prio::None;
    }
    if (qPlayer.RobotUse(ROBOT_USE_MAX5PLANES) && numPlanes() >= 5) {
        return Prio::None;
    }
    if (qPlayer.RobotUse(ROBOT_USE_MAX10PLANES) && numPlanes() >= 10) {
        return Prio::None;
    }
    if (!haveDiscount()) {
        return Prio::None; /* wait until we have some discount */
    }
    if (mDoRoutes && !mPlanesForRoutesUnassigned.empty()) {
        return Prio::None;
    }
    SLONG bestPlaneTypeId = mDoRoutes ? mBuyPlaneForRouteId : mBestPlaneTypeId;
    if (bestPlaneTypeId < 0) {
        return Prio::None; /* no plane purchase planned */
    }
    if ((qPlayer.xPiloten < PlaneTypes[bestPlaneTypeId].AnzPiloten) || (qPlayer.xBegleiter < PlaneTypes[bestPlaneTypeId].AnzBegleiter)) {
        return Prio::None; /* not enough crew */
    }
    if (moneyAvailable >= PlaneTypes[bestPlaneTypeId].Preis) {
        return Prio::Medium;
    }
    return Prio::None;
}

Bot::Prio Bot::condVisitHR() {
    if (!hoursPassed(ACTION_PERSONAL, 24)) {
        return Prio::None;
    }
    return Prio::Medium;
}

Bot::Prio Bot::condBuyKerosine(__int64 &moneyAvailable) {
    moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
    if (!hoursPassed(ACTION_BUY_KEROSIN, 4)) {
        return Prio::None;
    }
    if (!qPlayer.RobotUse(ROBOT_USE_PETROLAIR)) {
        return Prio::None;
    }
    if (qPlayer.HasBerater(BERATERTYP_KEROSIN) < 30) {
        return Prio::None;
    }
    if (!haveDiscount()) {
        return Prio::None; /* wait until we have some discount */
    }
    if ((qPlayer.TankInhalt * 100) / qPlayer.Tank > 90) {
        return Prio::None;
    }
    if (moneyAvailable >= 0) {
        return Prio::Medium;
    }
    return Prio::None;
}

Bot::Prio Bot::condBuyKerosineTank(__int64 &moneyAvailable) {
    moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
    if (!hoursPassed(ACTION_BUY_KEROSIN_TANKS, 24)) {
        return Prio::None;
    }
    if (!qPlayer.RobotUse(ROBOT_USE_PETROLAIR)) {
        return Prio::None;
    }
    if (!qPlayer.RobotUse(ROBOT_USE_TANKS) || qPlayer.HasBerater(BERATERTYP_KEROSIN) < 30) {
        return Prio::None;
    }
    if (!haveDiscount()) {
        return Prio::None; /* wait until we have some discount */
    }
    if (mTanksFilledYesterday - mTanksFilledToday < 0.5 && !mTankWasEmpty) {
        return Prio::None;
    }
    auto nTankTypes = sizeof(TankSize) / sizeof(TankSize[0]);
    moneyAvailable -= 200 * 1000;
    if (moneyAvailable > TankPrice[nTankTypes - 1]) {
        moneyAvailable = TankPrice[nTankTypes - 1]; /* do not spend more than 1x largest tank at once*/
    }
    if (moneyAvailable >= TankPrice[1]) {
        return Prio::Medium;
    }
    return Prio::None;
}

Bot::Prio Bot::condSabotage(__int64 &moneyAvailable) {
    moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
    if (!hoursPassed(ACTION_SABOTAGE, 24)) {
        return Prio::None;
    }
    return Prio::None;
}

Bot::Prio Bot::condIncreaseDividend(__int64 &moneyAvailable) {
    moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
    if (!hoursPassed(ACTION_SET_DIVIDEND, 24)) {
        return Prio::None;
    }
    if (qPlayer.Dividende >= 25) {
        return Prio::None;
    }
    moneyAvailable -= 100 * 1000;
    if (moneyAvailable >= 0) {
        return Prio::Medium;
    }
    return Prio::None;
}
Bot::Prio Bot::condRaiseMoney(__int64 &moneyAvailable) {
    moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
    if (!hoursPassed(ACTION_RAISEMONEY, 1)) {
        return Prio::None;
    }
    if (qPlayer.CalcCreditLimit() < 1000) {
        return Prio::None;
    }
    if (moneyAvailable < 0) {
        return Prio::Top;
    }
    return Prio::None;
}
Bot::Prio Bot::condDropMoney(__int64 &moneyAvailable) {
    moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
    if (!hoursPassed(ACTION_DROPMONEY, 24)) {
        return Prio::None;
    }
    moneyAvailable -= 1500 * 1000;
    if (moneyAvailable >= 0 && qPlayer.Credit > 0) {
        return Prio::Medium;
    }
    return Prio::None;
}
Bot::Prio Bot::condEmitShares(__int64 &moneyAvailable) {
    moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
    if (!hoursPassed(ACTION_EMITSHARES, 24)) {
        return Prio::None;
    }
    if (GameMechanic::canEmitStock(qPlayer) == GameMechanic::EmitStockResult::Ok) {
        return Prio::Medium;
    }
    return Prio::None;
}
Bot::Prio Bot::condSellShares(__int64 &moneyAvailable) {
    moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
    if (!hoursPassed(ACTION_SELLSHARES, 1)) {
        return Prio::None;
    }
    if (qPlayer.Money < 0) {
        if (qPlayer.CalcCreditLimit() >= 1000) {
            return Prio::High; /* first take out credit */
        }
        return Prio::Top;
    }
    if (moneyAvailable - qPlayer.Credit < 0) {
        return Prio::High;
    }
    return Prio::None;
}

Bot::Prio Bot::condBuyOwnShares(__int64 &moneyAvailable) {
    moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
    if (!hoursPassed(ACTION_BUYSHARES, 4)) {
        return Prio::None;
    }
    if (qPlayer.RobotUse(ROBOT_USE_DONTBUYANYSHARES)) {
        return Prio::None;
    }
    if (qPlayer.OwnsAktien[qPlayer.PlayerNum] >= qPlayer.AnzAktien / 4) {
        return Prio::None;
    }
    moneyAvailable -= 6000 * 1000;
    if ((moneyAvailable >= 0) && (qPlayer.Credit == 0)) {
        return Prio::Low;
    }
    return Prio::None;
}
Bot::Prio Bot::condBuyNemesisShares(__int64 &moneyAvailable, SLONG dislike) {
    moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
    if (!hoursPassed(ACTION_BUYSHARES, 4)) {
        return Prio::None;
    }
    if (qPlayer.RobotUse(ROBOT_USE_DONTBUYANYSHARES)) {
        return Prio::None;
    }
    moneyAvailable -= 2000 * 1000;
    if ((dislike != -1) && (moneyAvailable >= 0) && (qPlayer.Credit == 0)) {
        return Prio::Low;
    }
    return Prio::None;
}

Bot::Prio Bot::condVisitMech(__int64 &moneyAvailable) {
    moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
    if (!hoursPassed(ACTION_VISITMECH, 4)) {
        return Prio::None;
    }
    moneyAvailable -= 2000 * 1000;
    if (moneyAvailable >= 0) {
        return Prio::Low;
    }
    return Prio::None;
}

Bot::Prio Bot::condVisitNasa(__int64 &moneyAvailable) {
    moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
    if (!qPlayer.RobotUse(ROBOT_USE_NASA)) {
        return Prio::None;
    }
    return Prio::None;
}

Bot::Prio Bot::condVisitMisc() {
    if (qPlayer.RobotUse(ROBOT_USE_NOCHITCHAT)) {
        return Prio::None;
    }
    return Prio::Low;
}

Bot::Prio Bot::condBuyUsedPlane(__int64 &moneyAvailable) {
    moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
    return Prio::None;
}

Bot::Prio Bot::condVisitDutyFree(__int64 &moneyAvailable) {
    moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
    if (!hoursPassed(ACTION_VISITDUTYFREE, 24)) {
        return Prio::None;
    }
    moneyAvailable -= 100 * 1000;
    if (moneyAvailable >= 0 && (qPlayer.LaptopQuality < 4)) {
        return Prio::Low;
    }
    if (moneyAvailable >= 0 && !qPlayer.HasItem(ITEM_HANDY)) {
        return Prio::Low;
    }
    return Prio::None;
}

Bot::Prio Bot::condVisitBoss(__int64 &moneyAvailable) {
    moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
    if (!hoursPassed(ACTION_VISITAUFSICHT, 1)) {
        return Prio::None;
    }
    moneyAvailable -= 10 * 1000;
    if (moneyAvailable < 0) {
        return Prio::None;
    }
    if (Sim.Time > (16 * 60000) && (mBossNumCitiesAvailable > 0 || mBossGateAvailable)) {
        return Prio::High; /* check again right before end of day */
    }
    if (mBossGateAvailable || (mBossNumCitiesAvailable == -1)) { /* there is gate available (or we don't know yet) */
        return mOutOfGates ? Prio::High : Prio::Medium;
    }
    if (mBossNumCitiesAvailable > 0 && hoursPassed(ACTION_VISITAUFSICHT, 4)) {
        return Prio::Low;
    }
    return Prio::None;
}

Bot::Prio Bot::condVisitRouteBoxPlanning() {
    if (!hoursPassed(ACTION_VISITROUTEBOX, 4)) {
        return Prio::None;
    }
    if (!qPlayer.RobotUse(ROBOT_USE_ROUTEBOX) || !mDoRoutes) {
        return Prio::None;
    }
    if (mWantToRentRouteId != -1) {
        return Prio::None; /* we already want to rent a route */
    }
    if (!Helper::checkRoomOpen(ACTION_WERBUNG_ROUTES)) {
        return Prio::None; /* let's wait until we are able to buy ads for the route */
    }
    if (mRoutes.empty() || mRoutes[mRoutesSortedByUtilization[0]].utilization >= kMaximumRouteUtilization) {
        return Prio::Medium;
    }
    return Prio::None;
}

Bot::Prio Bot::condVisitRouteBoxRenting(__int64 &moneyAvailable) {
    moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
    if (!hoursPassed(ACTION_VISITROUTEBOX2, 4)) {
        return Prio::None;
    }
    if (!qPlayer.RobotUse(ROBOT_USE_ROUTEBOX) || !mDoRoutes) {
        return Prio::None;
    }
    if (mWantToRentRouteId != -1) {
        return Prio::High;
    }
    return Prio::None;
}

Bot::Prio Bot::condVisitSecurity(__int64 &moneyAvailable) {
    moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
    if (!hoursPassed(ACTION_VISITSECURITY, 24)) {
        return Prio::None;
    }
    if (!qPlayer.RobotUse(ROBOT_USE_SECURTY_OFFICE)) {
        return Prio::None;
    }
    return Prio::None;
}

Bot::Prio Bot::condSabotageSecurity() {
    if (!hoursPassed(ACTION_VISITSECURITY2, 24)) {
        return Prio::None;
    }
    if (!qPlayer.RobotUse(ROBOT_USE_SECURTY_OFFICE)) {
        return Prio::None;
    }
    if (qPlayer.HasItem(ITEM_ZANGE) == 0) {
        return Prio::None;
    }
    return Prio::Low;
}

Bot::Prio Bot::condVisitDesigner(__int64 &moneyAvailable) {
    moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
    if (!hoursPassed(ACTION_VISITDESIGNER, 24)) {
        return Prio::None;
    }
    if (!qPlayer.RobotUse(ROBOT_USE_DESIGNER)) {
        return Prio::None;
    }
    return Prio::None;
}

Bot::Prio Bot::condBuyAdsForRoutes(__int64 &moneyAvailable) {
    moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
    if (!hoursPassed(ACTION_WERBUNG_ROUTES, 4)) {
        return Prio::None;
    }
    if (!qPlayer.RobotUse(ROBOT_USE_WERBUNG)) {
        return Prio::None;
    }
    if (!haveDiscount()) {
        return Prio::None; /* wait until we have some discount */
    }
    auto minCost = gWerbePrice[1 * 6 + kSmallestAdCampaign];
    moneyAvailable -= minCost;
    if (moneyAvailable < 0 || mRoutesSortedByImage.empty()) {
        return Prio::None;
    }
    SLONG imageDelta = UBYTE(minCost / 30000);
    if (mRoutes[mRoutesSortedByImage[0]].image + imageDelta <= 100) {
        return (mRoutes[mRoutesSortedByImage[0]].image < 80) ? Prio::High : Prio::Medium;
    }
    return Prio::None;
}

Bot::Prio Bot::condBuyAds(__int64 &moneyAvailable) {
    moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
    if (!hoursPassed(ACTION_WERBUNG, 4)) {
        return Prio::None;
    }
    if (!qPlayer.RobotUse(ROBOT_USE_WERBUNG)) {
        return Prio::None;
    }
    if (!haveDiscount()) {
        return Prio::None; /* wait until we have some discount */
    }
    auto minCost = gWerbePrice[0 * 6 + kSmallestAdCampaign];
    moneyAvailable -= minCost;
    if (moneyAvailable < 0) {
        return Prio::None;
    }
    if (qPlayer.Image < 0 || (mDoRoutes && qPlayer.Image < 300)) {
        return Prio::Medium;
    }
    auto imageDelta = minCost / 10000 * (kSmallestAdCampaign + 6) / 55;
    if (mDoRoutes && qPlayer.Image < (1000 - imageDelta)) {
        return Prio::Low;
    }
    return Prio::None;
}

void Bot::actionWerbungRoutes(__int64 moneyAvailable) {
    auto prioEntry = condBuyAdsForRoutes(moneyAvailable);
    while (condBuyAdsForRoutes(moneyAvailable) == prioEntry) {
        const auto &qRoute = mRoutes[mRoutesSortedByImage[0]];

        SLONG cost = 0;
        SLONG adCampaignSize = 5;
        for (; adCampaignSize >= kSmallestAdCampaign; adCampaignSize--) {
            cost = gWerbePrice[1 * 6 + adCampaignSize];
            SLONG imageDelta = UBYTE(cost / 30000);
            if (qRoute.image + imageDelta > 100) {
                continue;
            }
            if (cost <= moneyAvailable) {
                break;
            }
        }
        if (adCampaignSize < kSmallestAdCampaign) {
            return;
        }
        hprintf("Bot::RobotExecuteAction(): Buying advertisement for route %s - %s for %ld $", (LPCTSTR)Cities[getRoute(qRoute).VonCity].Kuerzel,
                (LPCTSTR)Cities[getRoute(qRoute).NachCity].Kuerzel, cost);
        GameMechanic::buyAdvertisement(qPlayer, 1, adCampaignSize, qRoute.routeId);
        moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;

        SLONG oldImage = qRoute.image;
        updateRouteInfo();
        hprintf("Bot::RobotExecuteAction(): Route image improved (%ld => %ld)", oldImage, static_cast<SLONG>(qRoute.image));
    }
}

Bot::Bot(PLAYER &player) : qPlayer(player) {}

void Bot::RobotInit() {
    hprintf("Bot.cpp: Enter RobotInit()");

    auto &qRobotActions = qPlayer.RobotActions;

    if (mFirstRun) {
        for (SLONG i = 0; i < qPlayer.Planes.AnzEntries(); i++) {
            if (qPlayer.Planes.IsInAlbum(i)) {
                mPlanesForJobs.push_back(qPlayer.Planes.GetIdFromIndex(i));
            }
        }
        mFirstRun = false;

        for (auto &i : qRobotActions) {
            i = {};
        }
    }

    mLastTimeInRoom.clear();
    mBossNumCitiesAvailable = -1;
    mBossGateAvailable = false;

    updateRouteInfo();

    RobotPlan();
    hprintf("Bot.cpp: Leaving RobotInit()");
}

void Bot::RobotPlan() {
    hprintf("Bot.cpp: Enter RobotPlan()");

    if (mFirstRun) {
        redprintf("Bot::RobotPlan(): Bot was not initialized!");
        RobotInit();
        hprintf("Bot.cpp: Leaving RobotPlan() (not initialized)\n");
        return;
    }

    auto &qRobotActions = qPlayer.RobotActions;

    SLONG actions[] = {ACTION_STARTDAY,       ACTION_CALL_INTERNATIONAL, ACTION_CHECKAGENT1,    ACTION_CHECKAGENT2,
                       ACTION_CHECKAGENT3,    ACTION_UPGRADE_PLANES,     ACTION_BUYNEWPLANE,    ACTION_PERSONAL,
                       ACTION_BUY_KEROSIN,    ACTION_BUY_KEROSIN_TANKS,  ACTION_SABOTAGE,       ACTION_SET_DIVIDEND,
                       ACTION_RAISEMONEY,     ACTION_DROPMONEY,          ACTION_EMITSHARES,     ACTION_SELLSHARES,
                       ACTION_BUYSHARES,      ACTION_VISITMECH,          ACTION_VISITNASA,      ACTION_VISITTELESCOPE,
                       ACTION_VISITRICK,      ACTION_VISITKIOSK,         ACTION_BUYUSEDPLANE,   ACTION_VISITDUTYFREE,
                       ACTION_VISITAUFSICHT,  ACTION_VISITROUTEBOX,      ACTION_VISITROUTEBOX2, ACTION_VISITSECURITY,
                       ACTION_VISITSECURITY2, ACTION_VISITDESIGNER,      ACTION_WERBUNG_ROUTES, ACTION_WERBUNG};

    if (qRobotActions[0].ActionId != ACTION_NONE || qRobotActions[1].ActionId != ACTION_NONE) {
        hprintf("Bot.cpp: Leaving RobotPlan() (actions already planned)\n");
        return;
    }
    qRobotActions[1].ActionId = ACTION_NONE;
    qRobotActions[2].ActionId = ACTION_NONE;

    /* data to make decision */
    mDislike = -1;
    mBestPlaneTypeId = findBestAvailablePlaneType()[0];

    {
        SLONG n = 0;
        for (SLONG c = 0; c < 4; c++) {
            if (qPlayer.Sympathie[c] < 0 && (Sim.Players.Players[c].IsOut == 0)) {
                n++;
            }
        }

        if (n != 0) {
            n = (qPlayer.PlayerExtraRandom.Rand(n)) + 1;
            for (SLONG c = 0; c < 4; c++) {
                if (qPlayer.Sympathie[c] >= 0 || (Sim.Players.Players[c].IsOut == 1)) {
                    continue;
                }
                n--;
                if (n == 0) {
                    mDislike = c;
                    break;
                }
            }
        }
    }

    /* populate prio list */
    std::vector<std::pair<SLONG, Prio>> prioList;
    for (auto &action : actions) {
        if (!Helper::checkRoomOpen(action)) {
            continue;
        }
        auto prio = condAll(action);
        if (prio == Prio::None) {
            continue;
        }
        prioList.emplace_back(action, prio);
    }
    if (prioList.size() < 2) {
        prioList.emplace_back(ACTION_CHECKAGENT2, Prio::Medium);
    }
    if (prioList.size() < 2) {
        prioList.emplace_back(ACTION_CHECKAGENT1, Prio::Medium);
    }

    /* sort by priority */
    std::sort(prioList.begin(), prioList.end(), [](const std::pair<SLONG, Prio> &a, const std::pair<SLONG, Prio> &b) { return a.second > b.second; });
    /* for (const auto &i : prioList) {
        hprintf("Bot.cpp: %s: %s", getRobotActionName(i.first), getPrioName(i.second));
    }*/

    /* randomized tie-breaking */
    SLONG startIdx = 0;
    SLONG endIdx = 0;
    std::vector<SLONG> tieBreak;
    Prio prioAction1 = Prio::None;
    Prio prioAction2 = Prio::None;
    if (prioList[0].second != prioList[1].second) {
        prioAction1 = prioList[0].second;
        qRobotActions[1].ActionId = prioList[0].first;
        qRobotActions[1].Running = (prioAction1 >= Prio::Medium);

        startIdx++;
        endIdx++;
    }
    while (prioList[startIdx].second == prioList[endIdx].second && endIdx < prioList.size()) {
        endIdx++;
    }
    endIdx--;

    /* assign actions */
    if (qRobotActions[1].ActionId == ACTION_NONE && endIdx >= startIdx) {
        auto idx = qPlayer.PlayerWalkRandom.Rand(startIdx, endIdx);
        prioAction1 = prioList[idx].second;
        qRobotActions[1].ActionId = prioList[idx].first;
        qRobotActions[1].Running = (prioAction1 >= Prio::Medium);

        std::swap(prioList[idx], prioList[endIdx]);
        endIdx--;
    }
    if (qRobotActions[2].ActionId == ACTION_NONE && endIdx >= startIdx) {
        auto idx = qPlayer.PlayerWalkRandom.Rand(startIdx, endIdx);
        prioAction2 = prioList[idx].second;
        qRobotActions[2].ActionId = prioList[idx].first;
        qRobotActions[2].Running = (prioAction2 >= Prio::Medium);

        std::swap(prioList[idx], prioList[endIdx]);
        endIdx--;
    }

    hprintf("Action 0: %s", getRobotActionName(qRobotActions[0].ActionId));
    greenprintf("Action 1: %s with prio %s", getRobotActionName(qRobotActions[1].ActionId), getPrioName(prioAction1));
    greenprintf("Action 2: %s with prio %s", getRobotActionName(qRobotActions[2].ActionId), getPrioName(prioAction2));

    hprintf("Bot.cpp: Leaving RobotPlan()\n");
}

void Bot::RobotExecuteAction() {
    if (mFirstRun) {
        redprintf("Bot::RobotExecuteAction(): Bot was not initialized!");
        RobotInit();
        hprintf("Bot.cpp: Leaving RobotExecuteAction() (not initialized)\n");
        return;
    }

    /* handy references to player data (ro) */
    const auto &qPlanes = qPlayer.Planes;
    const auto &qKurse = qPlayer.Kurse;
    const auto &qOwnsAktien = qPlayer.OwnsAktien;
    const auto PlayerNum = qPlayer.PlayerNum;
    const auto qRentRouten = qPlayer.RentRouten.RentRouten;

    /* handy references to player data (rw) */
    auto &qRobotActions = qPlayer.RobotActions;
    auto &qWorkCountdown = qPlayer.WorkCountdown;

    /* random source */
    TEAKRAND LocalRandom;
    LocalRandom.SRand(qPlayer.WaitWorkTill);
    LocalRandom.Rand(2); // Sicherheitshalber, damit wir immer genau ein Random ausführen

    /* temporary data */
    __int64 moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
    std::vector<int> wantedAdvisors = {BERATERTYP_FITNESS, BERATERTYP_SICHERHEIT, BERATERTYP_KEROSIN};

    greenprintf("Bot.cpp: Enter RobotExecuteAction(): Executing %s", getRobotActionName(qRobotActions[0].ActionId));

    switch (qRobotActions[0].ActionId) {
    case 0:
        qWorkCountdown = 2;
        break;

    case ACTION_STARTDAY:
        if (condStartDay() != Prio::None) {
            // Logik für wechsel zu Routen und sparen für Rakete oder Flugzeug:
            if (!mDoRoutes && mBestPlaneTypeId != -1) {
                const auto &bestPlaneType = PlaneTypes[mBestPlaneTypeId];
                SLONG costRouteAd = gWerbePrice[1 * 6 + 5];
                __int64 moneyNeeded = 2 * costRouteAd + bestPlaneType.Preis + kMoneyEmergencyFund;
                if (mPlanesForJobs.size() >= 4 && moneyAvailable >= moneyNeeded) {
                    mDoRoutes = TRUE;
                    hprintf("Bot::RobotExecuteAction(): Switching to routes.");
                }

                if (qPlayer.RobotUse(ROBOT_USE_FORCEROUTES)) {
                    mDoRoutes = TRUE;
                    hprintf("Bot::RobotExecuteAction(): Switching to routes (forced).");
                }
            }

            mTanksFilledYesterday = mTanksFilledToday;
            mTanksFilledToday = 1.0 * qPlayer.TankInhalt / qPlayer.Tank;
            mTankWasEmpty = (qPlayer.TankInhalt < 10);

            // open tanks?
            auto Preis = Sim.HoleKerosinPreis(1); /* range: 300 - 700 */
            GameMechanic::setKerosinTankOpen(qPlayer, (Preis > 350));

            planRoutes();
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_CALL_INTERNATIONAL:
        // Im Ausland anrufen:
        if (condCallInternational() != Prio::None) {
            std::vector<int> cities;
            for (SLONG n = 0; n < Cities.AnzEntries(); n++) {
                if (!GameMechanic::canCallInternational(qPlayer, n)) {
                    continue;
                }

                GameMechanic::refillFlightJobs(n);
                cities.push_back(n);
            }

            if (!cities.empty()) {
                // Normale Aufträge:
                SLONG oldGain = calcCurrentGainFromJobs();
                BotPlaner planer(qPlayer, qPlanes, BotPlaner::JobOwner::International, cities);
                planer.planFlights(mPlanesForJobs);
                printGainFromJobs(oldGain);
                Helper::checkFlightJobs(qPlayer);

                // Frachtaufträge:
                // RobotUse(ROBOT_USE_MUCH_FRACHT)
                // RobotUse(ROBOT_USE_MUCH_FRACHT_BONUS)
                // TODO
            }
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

        // Last-Minute
    case ACTION_CHECKAGENT1:
        if (condCheckLastMinute() != Prio::None) {
            LastMinuteAuftraege.RefillForLastMinute();

            SLONG oldGain = calcCurrentGainFromJobs();
            BotPlaner planer(qPlayer, qPlanes, BotPlaner::JobOwner::LastMinute, {});
            planer.planFlights(mPlanesForJobs);
            printGainFromJobs(oldGain);
            Helper::checkFlightJobs(qPlayer);

            LastMinuteAuftraege.RefillForLastMinute();
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

        // Reisebüro:
    case ACTION_CHECKAGENT2:
        if (condCheckTravelAgency() != Prio::None) {
            ReisebueroAuftraege.RefillForReisebuero();

            SLONG oldGain = calcCurrentGainFromJobs();
            BotPlaner planer(qPlayer, qPlanes, BotPlaner::JobOwner::TravelAgency, {});
            planer.planFlights(mPlanesForJobs);
            printGainFromJobs(oldGain);
            Helper::checkFlightJobs(qPlayer);

            ReisebueroAuftraege.RefillForReisebuero();
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

        // Frachtbüro:
    case ACTION_CHECKAGENT3:
        if (condCheckFreight() != Prio::None) {
            gFrachten.Refill();

            SLONG oldGain = calcCurrentGainFromJobs();
            BotPlaner planer(qPlayer, qPlanes, BotPlaner::JobOwner::Freight, {});
            planer.planFlights(mPlanesForJobs);
            printGainFromJobs(oldGain);
            Helper::checkFlightJobs(qPlayer);

            gFrachten.Refill();
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

    case ACTION_UPGRADE_PLANES:
        if (condUpgradePlanes(moneyAvailable) != Prio::None) {
            SLONG anzahl = 4;
            while (moneyAvailable > 0 && anzahl-- > 0) {
                for (SLONG bpass = 0; bpass < 3; bpass++) {
                    auto idx = LocalRandom.Rand(mPlanesForRoutes.size());
                    CPlane &qPlane = const_cast<CPlane &>(qPlanes[mPlanesForRoutes[idx]]);
                    auto ptPassagiere = qPlane.ptPassagiere;

                    SLONG cost = ptPassagiere * (SeatCosts[2] - SeatCosts[qPlane.Sitze] / 2);
                    if (qPlane.SitzeTarget < 2 && cost <= moneyAvailable) {
                        qPlane.SitzeTarget = 2;
                        moneyAvailable -= cost;
                        hprintf("Bot::RobotExecuteAction(): Upgrading seats in %s.", (LPCTSTR)qPlane.Name);
                        continue;
                    }

                    cost = ptPassagiere * (TrayCosts[2] - TrayCosts[qPlane.Tabletts] / 2);
                    if (qPlane.TablettsTarget < 2 && cost <= moneyAvailable) {
                        qPlane.TablettsTarget = 2;
                        moneyAvailable -= cost;
                        hprintf("Bot::RobotExecuteAction(): Upgrading tabletts in %s.", (LPCTSTR)qPlane.Name);
                        continue;
                    }

                    cost = ptPassagiere * (DecoCosts[2] - DecoCosts[qPlane.Deco] / 2);
                    if (qPlane.DecoTarget < 2 && cost <= moneyAvailable) {
                        qPlane.DecoTarget = 2;
                        moneyAvailable -= cost;
                        hprintf("Bot::RobotExecuteAction(): Upgrading deco in %s.", (LPCTSTR)qPlane.Name);
                        continue;
                    }

                    cost = (ReifenCosts[2] - ReifenCosts[qPlane.Reifen] / 2);
                    if (qPlane.ReifenTarget < 2 && cost <= moneyAvailable) {
                        qPlane.ReifenTarget = 2;
                        moneyAvailable -= cost;
                        hprintf("Bot::RobotExecuteAction(): Upgrading tires in %s.", (LPCTSTR)qPlane.Name);
                        continue;
                    }

                    cost = (TriebwerkCosts[2] - TriebwerkCosts[qPlane.Triebwerk] / 2);
                    if (qPlane.TriebwerkTarget < 2 && cost <= moneyAvailable) {
                        qPlane.TriebwerkTarget = 2;
                        moneyAvailable -= cost;
                        hprintf("Bot::RobotExecuteAction(): Upgrading engines in %s.", (LPCTSTR)qPlane.Name);
                        continue;
                    }

                    cost = (SicherheitCosts[2] - SicherheitCosts[qPlane.Sicherheit] / 2);
                    if (qPlane.SicherheitTarget < 2 && cost <= moneyAvailable) {
                        qPlane.SicherheitTarget = 2;
                        moneyAvailable -= cost;
                        hprintf("Bot::RobotExecuteAction(): Upgrading safety in %s.", (LPCTSTR)qPlane.Name);
                        continue;
                    }

                    cost = (ElektronikCosts[2] - ElektronikCosts[qPlane.Elektronik] / 2);
                    if (qPlane.ElektronikTarget < 2 && cost <= moneyAvailable) {
                        qPlane.ElektronikTarget = 2;
                        moneyAvailable -= cost;
                        hprintf("Bot::RobotExecuteAction(): Upgrading electronics in %s.", (LPCTSTR)qPlane.Name);
                        continue;
                    }
                }
            }
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }

        qWorkCountdown = 20 * 5;
        break;

    case ACTION_BUYNEWPLANE:
        if (condBuyNewPlane(moneyAvailable) != Prio::None) {
            SLONG bestPlaneTypeId = mDoRoutes ? mBuyPlaneForRouteId : mBestPlaneTypeId;
            assert(bestPlaneTypeId >= 0);
            hprintf("Bot::RobotExecuteAction(): Buying plane %s", (LPCTSTR)PlaneTypes[bestPlaneTypeId].Name);
            assert(qPlayer.xPiloten >= PlaneTypes[bestPlaneTypeId].AnzPiloten);
            assert(qPlayer.xBegleiter >= PlaneTypes[bestPlaneTypeId].AnzBegleiter);
            for (auto i : GameMechanic::buyPlane(qPlayer, bestPlaneTypeId, 1)) {
                if (mDoRoutes) {
                    mPlanesForRoutesUnassigned.push_back(i);
                } else {
                    mPlanesForJobs.push_back(i);
                }
            }
            moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_PERSONAL:
        // Hire advisors
        if (condVisitHR() != Prio::None) {
            /* advisors */
            for (auto advisorType : wantedAdvisors) {
                SLONG bestCandidateId = -1;
                SLONG bestCandidateSkill = 0;
                for (SLONG c = 0; c < Workers.Workers.AnzEntries(); c++) {
                    const auto &qWorker = Workers.Workers[c];
                    if (qWorker.Typ != advisorType) {
                        continue;
                    }
                    if (bestCandidateSkill < qWorker.Talent) {
                        if (qWorker.Employer == PlayerNum) {
                            /* best advisor that is currently employed */
                            bestCandidateSkill = qWorker.Talent;
                            bestCandidateId = -1;
                        } else if (qWorker.Employer == WORKER_JOBLESS) {
                            /* better candidates has applied */
                            bestCandidateSkill = qWorker.Talent;
                            bestCandidateId = c;
                        }
                    }
                }
                /* hire best candidate and fire everyone of same type */
                if (bestCandidateId != -1) {
                    hprintf("Bot::RobotExecuteAction(): Upgrading advisor %ld to skill level %ld", advisorType, bestCandidateSkill);
                    /* fire existing adivsors */
                    for (SLONG c = 0; c < Workers.Workers.AnzEntries(); c++) {
                        const auto &qWorker = Workers.Workers[c];
                        if (qWorker.Employer == PlayerNum && qWorker.Typ == advisorType) {
                            GameMechanic::fireWorker(qPlayer, c);
                        }
                    }
                    /* hire new advisor */
                    GameMechanic::hireWorker(qPlayer, bestCandidateId);
                }
            }

            /* crew */
            SLONG pilotsTarget = 0;
            SLONG stewardessTarget = 0;
            SLONG numPilotsHires = 0;
            SLONG numStewardessHires = 0;
            SLONG bestPlaneTypeId = mDoRoutes ? mBuyPlaneForRouteId : mBestPlaneTypeId;
            if (bestPlaneTypeId >= 0) {
                const auto &bestPlaneType = PlaneTypes[bestPlaneTypeId];
                pilotsTarget = bestPlaneType.AnzPiloten;
                stewardessTarget = bestPlaneType.AnzBegleiter;
            }
            for (SLONG c = 0; c < Workers.Workers.AnzEntries(); c++) {
                const auto &qWorker = Workers.Workers[c];
                if (qWorker.Employer != WORKER_JOBLESS) {
                    continue;
                }
                if (qWorker.Talent < 70) {
                    continue;
                }
                if (qWorker.Typ == WORKER_PILOT && qPlayer.xPiloten < pilotsTarget) {
                    GameMechanic::hireWorker(qPlayer, c);
                    numPilotsHires++;
                } else if (qWorker.Typ == WORKER_STEWARDESS && qPlayer.xBegleiter < stewardessTarget) {
                    GameMechanic::hireWorker(qPlayer, c);
                    numStewardessHires++;
                }
            }
            if (numPilotsHires > 0 || numStewardessHires > 0) {
                hprintf("Bot::RobotExecuteAction(): Hiring %ld pilots and %ld attendants", numPilotsHires, numStewardessHires);
            }
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }

        qWorkCountdown = 20 * 5;
        break;

    case ACTION_BUY_KEROSIN:
        if (condBuyKerosine(moneyAvailable) != Prio::None) {
            auto Preis = Sim.HoleKerosinPreis(1); /* range: 300 - 700 */
            __int64 moneyToSpend = (moneyAvailable - 2500 * 1000);
            DOUBLE targetFillRatio = 0.5;
            if (Preis < 500) {
                moneyToSpend = (moneyAvailable - 1500 * 1000);
                targetFillRatio = 0.7;
            }
            if (Preis < 450) {
                moneyToSpend = (moneyAvailable - 1000 * 1000);
                targetFillRatio = 0.8;
            }
            if (Preis < 400) {
                moneyToSpend = (moneyAvailable - 500 * 1000);
                targetFillRatio = 0.9;
            }
            if (Preis < 350) {
                moneyToSpend = moneyAvailable;
                targetFillRatio = 1.0;
            }

            if (qPlayer.TankInhalt < 10) {
                mTankWasEmpty = true;
            }

            if (moneyToSpend > 0) {
                auto res = kerosineQualiOptimization(moneyToSpend, targetFillRatio);
                hprintf("Bot::RobotExecuteAction(): Buying %lld good and %lld bad kerosine", res.first, res.second);
                auto qualiOld = qPlayer.KerosinQuali;
                GameMechanic::buyKerosin(qPlayer, 1, res.first);
                GameMechanic::buyKerosin(qPlayer, 2, res.second);
                hprintf("Bot::RobotExecuteAction(): Kerosine quality: %.2f => %.2f", qualiOld, qPlayer.KerosinQuali);
                moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
            }
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_BUY_KEROSIN_TANKS:
        if (condBuyKerosineTank(moneyAvailable) != Prio::None) {
            auto nTankTypes = sizeof(TankSize) / sizeof(TankSize[0]);
            for (SLONG i = nTankTypes - 1; i >= 1; i--) // avoid cheapest tank (not economical)
            {
                if (moneyAvailable >= TankPrice[i]) {
                    SLONG amount = std::min(3LL, moneyAvailable / TankPrice[i]);
                    hprintf("Bot::RobotExecuteAction(): Buying %ld times tank type %ld", amount, i);
                    GameMechanic::buyKerosinTank(qPlayer, i, amount);
                    moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
                    break;
                }
            }
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_SABOTAGE:
        qPlayer.ArabTrust = max(1, qPlayer.ArabTrust); // Computerspieler müssen Araber nicht bestechen
        if (condSabotage(moneyAvailable) != Prio::None) {
            moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_SET_DIVIDEND:
        if (condIncreaseDividend(moneyAvailable) != Prio::None) {
            SLONG _dividende = qPlayer.Dividende;
            if (qPlayer.RobotUse(ROBOT_USE_HIGHSHAREPRICE)) {
                _dividende = 25;
            } else if (LocalRandom.Rand(10) == 0) {
                if (LocalRandom.Rand(5) == 0) {
                    _dividende++;
                }
                if (LocalRandom.Rand(10) == 0) {
                    _dividende++;
                }

                if (_dividende < 5) {
                    _dividende = 5;
                }
                if (_dividende > 25) {
                    _dividende = 25;
                }
            }

            if (_dividende != qPlayer.Dividende) {
                hprintf("Bot::RobotExecuteAction(): Setting divident to %ld", _dividende);
                GameMechanic::setDividend(qPlayer, _dividende);
            }
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_RAISEMONEY:
        if (condRaiseMoney(moneyAvailable) != Prio::None) {
            __int64 limit = qPlayer.CalcCreditLimit();
            __int64 m = min(limit, (kMoneyEmergencyFund + kMoneyEmergencyFund / 2) - qPlayer.Money);
            if (m > 0) {
                hprintf("Bot::RobotExecuteAction(): Taking loan: %lld", m);
                GameMechanic::takeOutCredit(qPlayer, m);
                moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
            }
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_DROPMONEY:
        if (condDropMoney(moneyAvailable) != Prio::None) {
            __int64 m = min(qPlayer.Credit, moneyAvailable);
            hprintf("Bot::RobotExecuteAction(): Paying back loan: %lld", m);
            GameMechanic::payBackCredit(qPlayer, m);
            moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_EMITSHARES:
        if (condEmitShares(moneyAvailable) != Prio::None) {
            SLONG neueAktien = (qPlayer.MaxAktien - qPlayer.AnzAktien) / 100 * 100;
            hprintf("Bot::RobotExecuteAction(): Emitting stock: %ld", neueAktien);
            GameMechanic::emitStock(qPlayer, neueAktien, 2);
            moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;

            // Direkt wieder auf ein Viertel aufkaufen
            SLONG amountToBuy = qPlayer.AnzAktien / 4 - qOwnsAktien[PlayerNum];
            amountToBuy = min(amountToBuy, calcNumberOfShares(moneyAvailable, qKurse[0]));
            if (amountToBuy > 0) {
                hprintf("Bot::RobotExecuteAction(): Buying own stock: %ld", amountToBuy);
                GameMechanic::buyStock(qPlayer, PlayerNum, amountToBuy);
                moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
            }
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 6;
        break;

    case ACTION_SELLSHARES:
        if (condSellShares(moneyAvailable) != Prio::None) {
            for (SLONG c = 0; c < Sim.Players.AnzPlayers; c++) {
                if (qOwnsAktien[c] == 0 || c == PlayerNum) {
                    continue;
                }

                SLONG sells = min(qOwnsAktien[c], 20 * 1000);
                hprintf("Bot::RobotExecuteAction(): Selling stock from player %ld: %ld", c, sells);
                GameMechanic::sellStock(qPlayer, c, sells);
                moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
                if (condSellShares(moneyAvailable) == Prio::None) {
                    break;
                }
            }
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 6;
        break;

    case ACTION_BUYSHARES:
        // buy shares from nemesis
        if (condBuyNemesisShares(moneyAvailable, mDislike) != Prio::None) {
            auto &qDislikedPlayer = Sim.Players.Players[mDislike];
            SLONG amount = calcNumOfFreeShares(mDislike);
            SLONG amountCanAfford = calcNumberOfShares(moneyAvailable, qDislikedPlayer.Kurse[0]);
            amount = min(amount, amountCanAfford);

            if (amount > 0) {
                hprintf("Bot::RobotExecuteAction(): Buying nemesis stock: %ld", amount);
                GameMechanic::buyStock(qPlayer, mDislike, amount);

                if (mDislike == Sim.localPlayer && (Sim.Players.Players[Sim.localPlayer].HasBerater(BERATERTYP_INFO) >= LocalRandom.Rand(100))) {
                    Sim.Players.Players[Sim.localPlayer].Messages.AddMessage(
                        BERATERTYP_INFO, bprintf(StandardTexte.GetS(TOKEN_ADVICE, 9005), (LPCTSTR)qPlayer.NameX, (LPCTSTR)qPlayer.AirlineX, amount));
                }

                moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
            }
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }

        // buy own shares
        if (condBuyOwnShares(moneyAvailable) != Prio::None) {
            SLONG amount = calcNumOfFreeShares(PlayerNum);
            SLONG amountToBuy = qPlayer.AnzAktien / 4 - qOwnsAktien[PlayerNum];
            SLONG amountCanAfford = calcNumberOfShares(moneyAvailable, qKurse[0]);
            amount = min(amount, amountToBuy);
            amount = min(amount, amountCanAfford);

            if (amount > 0) {
                hprintf("Bot::RobotExecuteAction(): Buying own stock: %ld", amount);
                GameMechanic::buyStock(qPlayer, PlayerNum, amount);
                moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
            }
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }

        qWorkCountdown = 20 * 5;
        break;

    case ACTION_VISITMECH:
        if (condVisitMech(moneyAvailable) != Prio::None) {
            for (SLONG c = 0; c < qPlanes.AnzEntries(); c++) {
                if (qPlanes.IsInAlbum(c) == 0) {
                    continue;
                }
                auto target = min(qPlanes[c].TargetZustand + 2, 100);
                if (qPlanes[c].TargetZustand < target) {
                    hprintf("Bot::RobotExecuteAction(): Setting repair target of plane %s to %ld", (LPCTSTR)qPlanes[c].Name, target);
                    GameMechanic::setPlaneTargetZustand(qPlayer, c, target);
                }
            }

            if (qPlayer.MechMode != 3) {
                hprintf("Bot::RobotExecuteAction(): Setting mech mode to 3");
                GameMechanic::setMechMode(qPlayer, 3);
            }
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }

        qWorkCountdown = 20 * 5;
        break;

    case ACTION_VISITNASA:
        // TODO
        if (condVisitNasa(moneyAvailable) != Prio::None) {
            moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_VISITTELESCOPE:
    case ACTION_VISITRICK:
    case ACTION_VISITKIOSK:
        if (condVisitMisc() != Prio::None) {
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_BUYUSEDPLANE:
        // TODO
        if (condBuyUsedPlane(moneyAvailable) != Prio::None) {
            moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        break;

    case ACTION_VISITDUTYFREE:
        if (condVisitDutyFree(moneyAvailable) != Prio::None) {
            if (qPlayer.LaptopQuality < 4) {
                auto quali = qPlayer.LaptopQuality;
                GameMechanic::buyDutyFreeItem(qPlayer, ITEM_LAPTOP);
                moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
                hprintf("Bot::RobotExecuteAction(): Buying laptop (%ld => %ld)", quali, qPlayer.LaptopQuality);
            } else if (!qPlayer.HasItem(ITEM_HANDY)) {
                hprintf("Bot::RobotExecuteAction(): Buying cell phone");
                GameMechanic::buyDutyFreeItem(qPlayer, ITEM_HANDY);
                moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
            }
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_VISITAUFSICHT:
        if (condVisitBoss(moneyAvailable) != Prio::None) {
            for (SLONG c = 0; c < 7; c++) {
                const auto &qZettel = TafelData.Gate[c];
                if (qZettel.ZettelId < 0) {
                    continue;
                }
                mBossGateAvailable = true;
                if (qZettel.Player == PlayerNum) {
                    moneyAvailable -= qZettel.Preis;
                    continue;
                }
                if (qZettel.Preis > moneyAvailable) {
                    continue;
                }

                if (qZettel.Player == Sim.localPlayer && (Sim.Players.Players[Sim.localPlayer].HasBerater(BERATERTYP_INFO) >= LocalRandom.Rand(100))) {
                    Sim.Players.Players[Sim.localPlayer].Messages.AddMessage(
                        BERATERTYP_INFO, bprintf(StandardTexte.GetS(TOKEN_ADVICE, 9001), (LPCTSTR)qPlayer.NameX, (LPCTSTR)qPlayer.AirlineX));
                }
                GameMechanic::bidOnGate(qPlayer, c);
                moneyAvailable -= qZettel.Preis;
                hprintf("Bot::RobotExecuteAction(): Bidding on gate: %ld $", qZettel.Preis);
            }

            // Niederlassung erwerben:
            mBossNumCitiesAvailable = 0;
            for (SLONG c = 0; c < 7; c++) {
                const auto &qZettel = TafelData.City[c];
                if (qZettel.ZettelId < 0) {
                    continue;
                }
                mBossNumCitiesAvailable++;
                if (qZettel.Player == PlayerNum) {
                    moneyAvailable -= qZettel.Preis;
                    continue;
                }
                if (qPlayer.RentCities.RentCities[Cities(qZettel.ZettelId)].Rang != 0) {
                    continue;
                }
                if (qZettel.Preis > moneyAvailable) {
                    continue;
                }

                if (qZettel.Player == -1 || LocalRandom.Rand(3) == 0) {
                    if (qZettel.Player == Sim.localPlayer && (Sim.Players.Players[Sim.localPlayer].HasBerater(BERATERTYP_INFO) >= LocalRandom.Rand(100))) {
                        Sim.Players.Players[Sim.localPlayer].Messages.AddMessage(BERATERTYP_INFO,
                                                                                 bprintf(StandardTexte.GetS(TOKEN_ADVICE, 9002), (LPCTSTR)qPlayer.NameX,
                                                                                         (LPCTSTR)qPlayer.AirlineX, (LPCTSTR)Cities[qZettel.ZettelId].Name));
                    }
                    GameMechanic::bidOnCity(qPlayer, c);
                    moneyAvailable -= qZettel.Preis;
                    hprintf("Bot::RobotExecuteAction(): Bidding on city %s: %ld $", (LPCTSTR)Cities[qZettel.ZettelId].Name, qZettel.Preis);
                }
            }
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

    case ACTION_VISITROUTEBOX:
        if (condVisitRouteBoxPlanning() != Prio::None) {
            std::tie(mWantToRentRouteId, mBuyPlaneForRouteId) = findBestRoute(LocalRandom);
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

    case ACTION_VISITROUTEBOX2:
        if (condVisitRouteBoxRenting(moneyAvailable) != Prio::None) {
            rentRoute(mWantToRentRouteId, mBuyPlaneForRouteId);
            mWantToRentRouteId = -1;

            planRoutes();
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

    case ACTION_VISITSECURITY:
        if (condVisitSecurity(moneyAvailable) != Prio::None) {
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

    case ACTION_VISITSECURITY2:
        if (condSabotageSecurity() != Prio::None) {
            GameMechanic::sabotageSecurityOffice(qPlayer);
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 1;
        break;

    case ACTION_VISITDESIGNER:
        if (condVisitDesigner(moneyAvailable) != Prio::None) {
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

    case ACTION_WERBUNG_ROUTES:
        if (condBuyAdsForRoutes(moneyAvailable) != Prio::None) {
            actionWerbungRoutes(moneyAvailable);
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

    case ACTION_WERBUNG:
        if (condBuyAds(moneyAvailable) != Prio::None) {
            for (SLONG adCampaignSize = 5; adCampaignSize >= kSmallestAdCampaign; adCampaignSize--) {
                SLONG cost = gWerbePrice[0 * 6 + adCampaignSize];
                if (cost > moneyAvailable) {
                    continue;
                }
                SLONG imageDelta = cost / 10000 * (adCampaignSize + 6) / 55;
                if (qPlayer.Image + imageDelta > 1000) {
                    continue;
                }
                hprintf("Bot::RobotExecuteAction(): Buying advertisement for airline for %ld $", cost);
                GameMechanic::buyAdvertisement(qPlayer, 0, adCampaignSize);
                moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
                break;
            }
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

    default:
        redprintf("Bot::RobotExecuteAction(): Trying to execute invalid action: %s", getRobotActionName(qRobotActions[0].ActionId));
        // DebugBreak();
    }

    mLastTimeInRoom[qRobotActions[0].ActionId] = Sim.Time;

    if (qWorkCountdown > 2) {
        qWorkCountdown /= 2;
    }

    if (qPlayer.RobotUse(ROBOT_USE_WORKVERYQUICK) && qWorkCountdown > 4) {
        qWorkCountdown /= 4;
    } else if (qPlayer.RobotUse(ROBOT_USE_WORKQUICK) && qWorkCountdown > 2) {
        qWorkCountdown /= 2;
    }

    hprintf("Bot.cpp: Leaving RobotExecuteAction()\n");
}

TEAKFILE &operator<<(TEAKFILE &File, const Bot &bot) {
    File << static_cast<SLONG>(bot.mLastTimeInRoom.size());
    for (const auto &i : bot.mLastTimeInRoom) {
        File << i.first << i.second;
    }

    File << static_cast<SLONG>(bot.mPlanesForJobs.size());
    for (const auto &i : bot.mPlanesForJobs) {
        File << i;
    }

    File << static_cast<SLONG>(bot.mPlanesForRoutes.size());
    for (const auto &i : bot.mPlanesForRoutes) {
        File << i;
    }

    File << static_cast<SLONG>(bot.mPlanesForRoutesUnassigned.size());
    for (const auto &i : bot.mPlanesForRoutesUnassigned) {
        File << i;
    }

    File << bot.mDislike;
    File << bot.mBestPlaneTypeId;
    File << bot.mBuyPlaneForRouteId;
    File << bot.mWantToRentRouteId;
    File << bot.mFirstRun;
    File << bot.mDoRoutes;
    File << bot.mSavingForPlane;
    File << bot.mOutOfGates;

    File << bot.mBossNumCitiesAvailable;
    File << bot.mBossGateAvailable;

    File << bot.mTanksFilledYesterday;
    File << bot.mTanksFilledToday;
    File << bot.mTankWasEmpty;

    File << static_cast<SLONG>(bot.mRoutes.size());
    for (const auto &i : bot.mRoutes) {
        File << i.routeId << i.routeReverseId << i.planeTypeId;
        File << i.utilization << i.image;

        File << static_cast<SLONG>(i.planeIds.size());
        for (const auto &j : i.planeIds) {
            File << j;
        }
    }

    File << static_cast<SLONG>(bot.mRoutesSortedByUtilization.size());
    for (const auto &i : bot.mRoutesSortedByUtilization) {
        File << i;
    }

    File << static_cast<SLONG>(bot.mRoutesSortedByImage.size());
    for (const auto &i : bot.mRoutesSortedByImage) {
        File << i;
    }

    SLONG magicnumber = 0x42;
    File << magicnumber;

    return (File);
}

TEAKFILE &operator>>(TEAKFILE &File, Bot &bot) {
    SLONG size;

    File >> size;
    bot.mLastTimeInRoom.clear();
    for (SLONG i = 0; i < size; i++) {
        SLONG key, value;
        File >> key >> value;
        bot.mLastTimeInRoom[key] = value;
    }
    assert(bot.mLastTimeInRoom.size() == size);

    File >> size;
    bot.mPlanesForJobs.resize(size);
    for (SLONG i = 0; i < size; i++) {
        File >> bot.mPlanesForJobs[i];
    }

    File >> size;
    bot.mPlanesForRoutes.resize(size);
    for (SLONG i = 0; i < size; i++) {
        File >> bot.mPlanesForRoutes[i];
    }

    File >> size;
    bot.mPlanesForRoutesUnassigned.resize(size);
    for (SLONG i = 0; i < size; i++) {
        File >> bot.mPlanesForRoutesUnassigned[i];
    }

    File >> bot.mDislike;
    File >> bot.mBestPlaneTypeId;
    File >> bot.mBuyPlaneForRouteId;
    File >> bot.mWantToRentRouteId;
    File >> bot.mFirstRun;
    File >> bot.mDoRoutes;
    File >> bot.mSavingForPlane;
    File >> bot.mOutOfGates;

    File >> bot.mBossNumCitiesAvailable;
    File >> bot.mBossGateAvailable;

    File >> bot.mTanksFilledYesterday;
    File >> bot.mTanksFilledToday;
    File >> bot.mTankWasEmpty;

    File >> size;
    bot.mRoutes.resize(size);
    for (SLONG i = 0; i < size; i++) {
        Bot::RouteInfo info;
        File >> info.routeId >> info.routeReverseId >> info.planeTypeId;
        File >> info.utilization >> info.image;

        File >> size;
        info.planeIds.resize(size);
        for (SLONG i = 0; i < size; i++) {
            File >> info.planeIds[i];
        }

        bot.mRoutes[i] = info;
    }

    File >> size;
    bot.mRoutesSortedByUtilization.resize(size);
    for (SLONG i = 0; i < size; i++) {
        File >> bot.mRoutesSortedByUtilization[i];
    }

    File >> size;
    bot.mRoutesSortedByImage.resize(size);
    for (SLONG i = 0; i < size; i++) {
        File >> bot.mRoutesSortedByImage[i];
    }

    SLONG magicnumber = 0;
    File >> magicnumber;
    assert(magicnumber == 0x42);

    return (File);
}
