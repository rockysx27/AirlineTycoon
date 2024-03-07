#include "Bot.h"

#include "AtNet.h"
#include "BotHelper.h"
#include "BotPlaner.h"

#include <algorithm>

const SLONG kMoneyEmergencyFund = 100000;
const SLONG kSmallestAdCampaign = 4;
const SLONG kMaximumRouteUtilization = 90;
const SLONG kMaximumPlaneUtilization = 90;
const DOUBLE kMaxTicketPriceFactor = 3.0;
const SLONG kTargetEmployeeHappiness = 90;
const SLONG kPlaneMinimumZustand = 90;
const SLONG kMoneyNotForRepairs = 0;

static const char *getPrioName(Bot::Prio prio) {
    switch (prio) {
    case Bot::Prio::Top:
        return "Top";
    case Bot::Prio::Higher:
        return "Higher";
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

std::vector<SLONG> Bot::findBestAvailablePlaneType() const {
    CDataTable planeTable;
    planeTable.FillWithPlaneTypes();
    std::vector<std::pair<SLONG, __int64>> scores;
    for (const auto &i : planeTable.LineIndex) {
        if (!PlaneTypes.IsInAlbum(i)) {
            continue;
        }
        const auto &planeType = PlaneTypes[i];

        if (!GameMechanic::checkPlaneTypeAvailable(i)) {
            continue;
        }

        __int64 score = planeType.Passagiere * planeType.Passagiere;
        score *= planeType.Reichweite;
        score /= planeType.Verbrauch;
        scores.emplace_back(i, score);
    }
    std::sort(scores.begin(), scores.end(), [](const std::pair<SLONG, __int64> &a, const std::pair<SLONG, __int64> &b) { return a.second > b.second; });

    std::vector<SLONG> bestList;
    bestList.reserve(scores.size());
    for (const auto &i : scores) {
        // hprintf("Bot::findBestAvailablePlaneType(): Plane type %s has score %lld", (LPCTSTR)PlaneTypes[i.first].Name, i.second);
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

bool Bot::checkPlaneAvailable(SLONG planeId, bool printIfAvailable) const {
    const auto &qPlane = qPlayer.Planes[planeId];
    if (qPlane.AnzBegleiter < qPlane.ptAnzBegleiter) {
        redprintf("Bot::checkPlaneAvailable: Plane %s does not have enough crew members (%ld, need %ld).", (LPCTSTR)qPlane.Name, qPlane.AnzBegleiter,
                  qPlane.ptAnzBegleiter);
        return false;
    }
    if (qPlane.AnzPiloten < qPlane.ptAnzPiloten) {
        redprintf("Bot::checkPlaneAvailable: Plane %s does not have enough pilots (%ld, need %ld).", (LPCTSTR)qPlane.Name, qPlane.AnzPiloten,
                  qPlane.ptAnzPiloten);
        return false;
    }
    if (qPlane.Problem != 0) {
        redprintf("Bot::checkPlaneAvailable: Plane %s has a problem for the next %ld hours", (LPCTSTR)qPlane.Name, qPlane.Problem);
        return false;
    }
    if (printIfAvailable) {
        hprintf("Bot::checkPlaneAvailable: Plane %s is available for service.", (LPCTSTR)qPlane.Name);
    }
    return true;
}

bool Bot::checkPlaneLists() {
    bool foundProblem = false;
    std::unordered_map<SLONG, SLONG> uniquePlaneIds;
    for (const auto &i : mPlanesForJobs) {
        if (uniquePlaneIds.find(i) != uniquePlaneIds.end()) {
            redprintf("Bot::checkPlaneLists(): Plane with ID = %ld appears in multiple lists: 1 and %ld.", i, uniquePlaneIds[i]);
            foundProblem = true;
        }
        uniquePlaneIds[i] = 1;
    }
    for (const auto &i : mPlanesForRoutes) {
        if (uniquePlaneIds.find(i) != uniquePlaneIds.end()) {
            redprintf("Bot::checkPlaneLists(): Plane with ID = %ld appears in multiple lists: 2 and %ld.", i, uniquePlaneIds[i]);
            foundProblem = true;
        }
        uniquePlaneIds[i] = 2;
    }
    for (const auto &i : mPlanesForJobsUnassigned) {
        if (uniquePlaneIds.find(i) != uniquePlaneIds.end()) {
            redprintf("Bot::checkPlaneLists(): Plane with ID = %ld appears in multiple lists: 3 and %ld.", i, uniquePlaneIds[i]);
            foundProblem = true;
        }
        uniquePlaneIds[i] = 3;
    }
    for (const auto &i : mPlanesForRoutesUnassigned) {
        if (uniquePlaneIds.find(i) != uniquePlaneIds.end()) {
            redprintf("Bot::checkPlaneLists(): Plane with ID = %ld appears in multiple lists: 4 and %ld.", i, uniquePlaneIds[i]);
            foundProblem = true;
        }
        uniquePlaneIds[i] = 4;
    }
    for (SLONG i = 0; i < qPlayer.Planes.AnzEntries(); i++) {
        int id = qPlayer.Planes.GetIdFromIndex(i);
        if (qPlayer.Planes.IsInAlbum(i) && uniquePlaneIds.find(id) == uniquePlaneIds.end()) {
            const auto &qPlane = qPlayer.Planes[i];
            redprintf("Bot::checkPlaneLists(): Found new plane: %s (%lx).", (LPCTSTR)qPlane.Name, id);
            mPlanesForJobsUnassigned.push_back(id);
            foundProblem = true;
        }
    }

    /* check if we still have enough personal */
    findPlanesNotAvailableForService(mPlanesForJobs, mPlanesForJobsUnassigned);
    findPlanesNotAvailableForService(mPlanesForRoutes, mPlanesForRoutesUnassigned);

    /* maybe some planes now have crew? planes for routes will be checked in planRoutes() */
    findPlanesAvailableForService(mPlanesForJobsUnassigned, mPlanesForJobs);

    hprintf("Bot::checkPlaneLists(): Planes for jobs: %ld / %ld are available.", mPlanesForJobs.size(),
            mPlanesForJobs.size() + mPlanesForJobsUnassigned.size());
    hprintf("Bot::checkPlaneLists(): Planes for routes: %ld / %ld are available.", mPlanesForRoutes.size(),
            mPlanesForRoutes.size() + mPlanesForRoutesUnassigned.size());

    return foundProblem;
}

bool Bot::findPlanesNotAvailableForService(std::vector<SLONG> &listAvailable, std::deque<SLONG> &listUnassigned) {
    bool planesGoneMissing = false;
    std::vector<SLONG> newAvailable;
    for (const auto id : listAvailable) {
        if (!qPlayer.Planes.IsInAlbum(id)) {
            redprintf("Bot::findPlanesNotAvailableForService(): We lost the plane with ID = %ld", id);
            planesGoneMissing = true;
            continue;
        }

        auto &qPlane = qPlayer.Planes[id];
        hprintf("Bot::findPlanesNotAvailableForService(): Plane %s (%s): Zustand = %u, WorstZustand = %u", (LPCTSTR)qPlane.Name,
                (LPCTSTR)PlaneTypes[qPlane.TypeId].Name, qPlane.Zustand, qPlane.WorstZustand);

        int mode = 0; /* 0: keep plane in service */
        if (qPlane.Zustand <= kPlaneMinimumZustand) {
            hprintf("Bot::findPlanesNotAvailableForService(): Plane %s not available for service: Needs repairs.", (LPCTSTR)qPlane.Name);
            mode = 1; /* 1: phase plane out */
        }
        if (!checkPlaneAvailable(id, false)) {
            redprintf("Bot::findPlanesNotAvailableForService(): Plane %s not available for service: No crew.", (LPCTSTR)qPlane.Name);
            mode = 2; /* 2: remove plane immediately from service */
        }
        if (mode > 0) {
            listUnassigned.push_back(id);
            if (mode == 2) {
                GameMechanic::killFlightPlan(qPlayer, id);
            }

            /* remove plane from route */
            for (auto &route : mRoutes) {
                auto it = route.planeIds.begin();
                while (it != route.planeIds.end() && *it != id) {
                    it++;
                }
                if (it != route.planeIds.end()) {
                    route.planeIds.erase(it);
                }
            }
        } else {
            newAvailable.push_back(id);
        }
    }
    std::swap(listAvailable, newAvailable);
    return planesGoneMissing;
}

bool Bot::findPlanesAvailableForService(std::deque<SLONG> &listUnassigned, std::vector<SLONG> &listAvailable) {
    bool planesGoneMissing = false;
    std::deque<SLONG> newUnassigned;
    for (const auto id : listUnassigned) {
        if (!qPlayer.Planes.IsInAlbum(id)) {
            redprintf("Bot::findPlanesAvailableForService(): We lost the plane with ID = %ld", id);
            planesGoneMissing = true;
            continue;
        }

        auto &qPlane = qPlayer.Planes[id];
        hprintf("Bot::findPlanesNotAvailableForService(): Plane %s (%s): Zustand = %u, WorstZustand = %u", (LPCTSTR)qPlane.Name,
                (LPCTSTR)PlaneTypes[qPlane.TypeId].Name, qPlane.Zustand, qPlane.WorstZustand);

        if (qPlane.Zustand < 100 && (qPlane.Zustand < qPlane.WorstZustand + 20)) {
            hprintf("Bot::findPlanesAvailableForService(): Plane %s still not available for service: Needs repairs.", (LPCTSTR)qPlane.Name);
            newUnassigned.push_back(id);
        } else if (!checkPlaneAvailable(id, false)) {
            redprintf("Bot::findPlanesAvailableForService(): Plane %s still not available for service: No crew.", (LPCTSTR)qPlane.Name);
            newUnassigned.push_back(id);
        } else {
            hprintf("Bot::findPlanesAvailableForService(): Putting plane %s back into service.", (LPCTSTR)qPlane.Name);
            listAvailable.push_back(id);
        }
    }
    std::swap(listUnassigned, newUnassigned);
    return planesGoneMissing;
}

const CRentRoute &Bot::getRentRoute(const Bot::RouteInfo &routeInfo) const { return qPlayer.RentRouten.RentRouten[routeInfo.routeId]; }

const CRoute &Bot::getRoute(const Bot::RouteInfo &routeInfo) const { return Routen[routeInfo.routeId]; }

SLONG Bot::getDailyOpSaldo() const { return qPlayer.BilanzGestern.GetOpSaldo(); }

SLONG Bot::getWeeklyOpSaldo() const { return qPlayer.BilanzWoche.Hole().GetOpSaldo(); }

void Bot::forceReplanning() { qPlayer.RobotActions[1].ActionId = ACTION_NONE; }

Bot::Bot(PLAYER &player) : qPlayer(player) {}

void Bot::RobotInit() {
    hprintf("Bot.cpp: Enter RobotInit()");

    /* print inventory */
    printf("Inventory: ");
    for (SLONG d = 0; d < 6; d++) {
        if (qPlayer.Items[d] != 0xff) {
            printf("%s, ", Helper::getItemName(qPlayer.Items[d]));
        }
    }
    printf("\n");

    if (mFirstRun) {
        hprintf("Bot::RobotInit(): First run.");

        /* random source */
        LocalRandom.SRand(qPlayer.WaitWorkTill);

        for (SLONG i = 0; i < qPlayer.Planes.AnzEntries(); i++) {
            if (qPlayer.Planes.IsInAlbum(i)) {
                mPlanesForJobsUnassigned.push_back(qPlayer.Planes.GetIdFromIndex(i));
            }
        }
        mFirstRun = false;
    }

    for (auto &i : qPlayer.RobotActions) {
        i = {};
    }

    mLastTimeInRoom.clear();

    /* strategy state */
    mDislike = -1;
    mDayStarted = false;

    /* status boss office */
    mBossNumCitiesAvailable = -1;
    mBossGateAvailable = false;

    /* items */
    mIsSickToday = false;

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

    if (mIsSickToday && qPlayer.HasItem(ITEM_TABLETTEN)) {
        GameMechanic::useItem(qPlayer, ITEM_TABLETTEN);
        hprintf("Bot::RobotPlan(): Cured sickness using item pills");
        mIsSickToday = false;
    }

    auto &qRobotActions = qPlayer.RobotActions;

    SLONG actions[] = {ACTION_STARTDAY, ACTION_STARTDAY_LAPTOP,
                       /* repeated actions */
                       ACTION_BUERO, ACTION_CALL_INTERNATIONAL, ACTION_CALL_INTER_HANDY, ACTION_CHECKAGENT1, ACTION_CHECKAGENT2, ACTION_CHECKAGENT3,
                       ACTION_UPGRADE_PLANES, ACTION_BUYNEWPLANE, ACTION_PERSONAL, ACTION_BUY_KEROSIN, ACTION_BUY_KEROSIN_TANKS, ACTION_SABOTAGE,
                       ACTION_SET_DIVIDEND, ACTION_RAISEMONEY, ACTION_DROPMONEY, ACTION_EMITSHARES, ACTION_SELLSHARES, ACTION_BUYSHARES, ACTION_VISITMECH,
                       ACTION_VISITNASA, ACTION_VISITTELESCOPE, ACTION_VISITMAKLER, ACTION_VISITRICK, ACTION_VISITKIOSK, ACTION_BUYUSEDPLANE,
                       ACTION_VISITDUTYFREE, ACTION_VISITAUFSICHT, ACTION_EXPANDAIRPORT, ACTION_VISITROUTEBOX, ACTION_VISITROUTEBOX2, ACTION_VISITSECURITY,
                       ACTION_VISITSECURITY2, ACTION_VISITDESIGNER, ACTION_WERBUNG_ROUTES, ACTION_WERBUNG};

    if (qRobotActions[0].ActionId != ACTION_NONE || qRobotActions[1].ActionId != ACTION_NONE) {
        hprintf("Bot.cpp: Leaving RobotPlan() (actions already planned)\n");
        return;
    }
    qRobotActions[1].ActionId = ACTION_NONE;
    qRobotActions[2].ActionId = ACTION_NONE;

    /* data to make decision */
    {
        mDislike = -1;
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

    /* refuse to work outside working hours (game sometimes calls this too early) */
    if (Sim.GetHour() < 9 || (Sim.Time == 540000)) { /* check if it is precisely 09:00 or earlier */
        hprintf("Bot.cpp: Leaving RobotExecuteAction() (too early)\n");
        forceReplanning();
        return;
    }
    if (Sim.GetHour() >= 18) {
        hprintf("Bot.cpp: Leaving RobotExecuteAction() (too late)\n");
        forceReplanning();
        return;
    }

    /* handy references to player data (rw) */
    auto &qRobotActions = qPlayer.RobotActions;
    auto &qWorkCountdown = qPlayer.WorkCountdown;

    LocalRandom.Rand(2); // Sicherheitshalber, damit wir immer genau ein Random ausführen

    greenprintf("Bot.cpp: Enter RobotExecuteAction(): Executing %s, current time: %02d:%02d, money: %s $", getRobotActionName(qRobotActions[0].ActionId),
                Sim.GetHour(), Sim.GetMinute(), Insert1000erDots64(qPlayer.Money).c_str());

    mOnThePhone = 0;

    __int64 moneyAvailable = getMoneyAvailable();
    switch (qRobotActions[0].ActionId) {
    case 0:
        qWorkCountdown = 2;
        break;

    case ACTION_STARTDAY:
        if (condStartDay() != Prio::None) {
            actionStartDay(moneyAvailable);
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_STARTDAY_LAPTOP:
        if (condStartDayLaptop() != Prio::None) {
            actionStartDayLaptop(moneyAvailable);
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_BUERO:
        if (condBuero() != Prio::None) {
            actionBuero();
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_CALL_INTERNATIONAL:
        if (condCallInternational() != Prio::None) {
            hprintf("Bot::RobotExecuteAction(): Calling international using office phone.");
            actionCallInternational();
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_CALL_INTER_HANDY:
        if (condCallInternationalHandy() != Prio::None) {
            hprintf("Bot::RobotExecuteAction(): Calling international using mobile phone.");
            actionCallInternational();
            mOnThePhone = 30;
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

        // Last-Minute
    case ACTION_CHECKAGENT1:
        if (condCheckLastMinute() != Prio::None) {
            actionCheckLastMinute();
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

        // Reisebüro:
    case ACTION_CHECKAGENT2:
        if (condCheckTravelAgency() != Prio::None) {
            actionCheckTravelAgency();
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

        // Frachtbüro:
    case ACTION_CHECKAGENT3:
        if (condCheckFreight() != Prio::None) {
            gFrachten.Refill();

            // TODO

            gFrachten.Refill();
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

    case ACTION_UPGRADE_PLANES:
        if (condUpgradePlanes(moneyAvailable) != Prio::None) {
            actionUpgradePlanes(moneyAvailable);
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }

        qWorkCountdown = 20 * 5;
        break;

    case ACTION_BUYNEWPLANE:
        if (condBuyNewPlane(moneyAvailable) != Prio::None) {
            actionBuyNewPlane(moneyAvailable);
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_PERSONAL:
        if (condVisitHR() != Prio::None) {
            actionVisitHR();
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }

        qWorkCountdown = 20 * 5;
        break;

    case ACTION_BUY_KEROSIN:
        if (condBuyKerosine(moneyAvailable) != Prio::None) {
            actionBuyKerosine(moneyAvailable);
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_BUY_KEROSIN_TANKS:
        if (condBuyKerosineTank(moneyAvailable) != Prio::None) {
            actionBuyKerosineTank(moneyAvailable);
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_SABOTAGE:
        if (condSabotage(moneyAvailable) != Prio::None) {
            if (mItemAntiVirus == 1) {
                if (GameMechanic::useItem(qPlayer, ITEM_SPINNE)) {
                    hprintf("Bot::RobotExecuteAction(): Used item tarantula");
                    mItemAntiVirus = 2;
                }
            }
            if (mItemAntiVirus == 2) {
                if (GameMechanic::PickUpItemResult::PickedUp == GameMechanic::pickUpItem(qPlayer, ITEM_DART)) {
                    hprintf("Bot::RobotExecuteAction(): Picked up item dart");
                    mItemAntiVirus = 3;
                }
            }

            moneyAvailable = getMoneyAvailable();
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_SET_DIVIDEND:
        if (condIncreaseDividend(moneyAvailable) != Prio::None) {
            SLONG _dividende = qPlayer.Dividende;
            SLONG maxToEmit = (2500000 - qPlayer.MaxAktien) / 100 * 100;
            if (maxToEmit < 10000) {
                /* we cannot emit any shares anymore. We do not care about stock prices now. */
                _dividende = 0;
            } else if (qPlayer.RobotUse(ROBOT_USE_HIGHSHAREPRICE)) {
                _dividende = 25;
            } else if (LocalRandom.Rand(10) == 0) {
                if (LocalRandom.Rand(5) == 0) {
                    _dividende++;
                }
                if (LocalRandom.Rand(10) == 0) {
                    _dividende++;
                }
                Limit(5, _dividende, 25);
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
                moneyAvailable = getMoneyAvailable();
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
            moneyAvailable = getMoneyAvailable();
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_EMITSHARES:
        if (condEmitShares() != Prio::None) {
            actionEmitShares();
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 6;
        break;

    case ACTION_SELLSHARES:
        if (condSellShares(moneyAvailable) != Prio::None) {
            for (SLONG c = 0; c < Sim.Players.AnzPlayers; c++) {
                if (qPlayer.OwnsAktien[c] == 0 || c == qPlayer.PlayerNum) {
                    continue;
                }

                SLONG sells = min(qPlayer.OwnsAktien[c], 20 * 1000);
                hprintf("Bot::RobotExecuteAction(): Selling stock from player %ld: %ld", c, sells);
                GameMechanic::sellStock(qPlayer, c, sells);
                moneyAvailable = getMoneyAvailable();
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
        if (condBuyShares(moneyAvailable, mDislike) != Prio::None) {
            actionBuyShares(moneyAvailable);
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_VISITMECH:
        if (condVisitMech() != Prio::None) {
            actionVisitMech();
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_VISITNASA:
        // TODO
        if (condVisitNasa(moneyAvailable) != Prio::None) {
            moneyAvailable = getMoneyAvailable();
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_VISITTELESCOPE:
    case ACTION_VISITKIOSK:
        if (condVisitMisc() != Prio::None) {
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_VISITMAKLER:
        if (condVisitMakler() != Prio::None) {
            mBestPlaneTypeId = findBestAvailablePlaneType()[0];

            if (mItemAntiStrike == 0) {
                if (GameMechanic::PickUpItemResult::PickedUp == GameMechanic::pickUpItem(qPlayer, ITEM_BH)) {
                    hprintf("Bot::RobotExecuteAction(): Picked up item BH");
                    mItemAntiStrike = 1;
                }
            }
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_VISITRICK:
        if (condVisitRick() != Prio::None) {
            if (mItemAntiStrike == 3) {
                if (GameMechanic::useItem(qPlayer, ITEM_HUFEISEN)) {
                    hprintf("Bot::RobotExecuteAction(): Used item horse shoe");
                    mItemAntiStrike = 4;
                }
            }
            if (qPlayer.StrikeHours > 0 && qPlayer.StrikeEndType != 0 && mItemAntiStrike == 4) {
                hprintf("Bot::RobotExecuteAction(): Ended strike using drunk guy");
                GameMechanic::endStrike(qPlayer, GameMechanic::EndStrikeMode::Drunk);
            }
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_BUYUSEDPLANE:
        // TODO
        if (condBuyUsedPlane(moneyAvailable) != Prio::None) {
            moneyAvailable = getMoneyAvailable();
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        break;

    case ACTION_VISITDUTYFREE:
        if (condVisitDutyFree(moneyAvailable) != Prio::None) {
            actionVisitDutyFree(moneyAvailable);
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_VISITAUFSICHT:
        if (condVisitBoss(moneyAvailable) != Prio::None) {
            actionVisitBoss(moneyAvailable);
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

    case ACTION_EXPANDAIRPORT:
        if (condExpandAirport(moneyAvailable) != Prio::None) {
            hprintf("Bot::RobotExecuteAction(): Expanding Airport");
            GameMechanic::expandAirport(qPlayer);
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

    case ACTION_VISITROUTEBOX:
        if (condVisitRouteBoxPlanning() != Prio::None) {
            std::tie(mWantToRentRouteId, mBuyPlaneForRouteId) = actionFindBestRoute(LocalRandom);
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

    case ACTION_VISITROUTEBOX2:
        if (condVisitRouteBoxRenting(moneyAvailable) != Prio::None) {
            actionRentRoute(mWantToRentRouteId, mBuyPlaneForRouteId);
            mWantToRentRouteId = -1;
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
            actionBuyAdsForRoutes(moneyAvailable);
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

    case ACTION_WERBUNG:
        if (condBuyAds(moneyAvailable) != Prio::None) {
            actionBuyAds(moneyAvailable);
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

TEAKFILE &operator<<(TEAKFILE &File, const PlaneTime &planeTime) {
    File << static_cast<SLONG>(planeTime.getDate());
    File << static_cast<SLONG>(planeTime.getHour());
    return (File);
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

    File << static_cast<SLONG>(bot.mPlanesForJobsUnassigned.size());
    for (const auto &i : bot.mPlanesForJobsUnassigned) {
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
    File << bot.mOutOfGates;
    File << bot.mNeedToPlanJobs << bot.mNeedToPlanRoutes;

    File << bot.mBossNumCitiesAvailable;
    File << bot.mBossGateAvailable;

    File << bot.mTankRatioEmptiedYesterday;
    File << bot.mKerosineUsedTodaySoFar;
    File << bot.mKerosineLevelLastChecked;

    File << static_cast<SLONG>(bot.mRoutes.size());
    for (const auto &i : bot.mRoutes) {
        File << i.routeId << i.routeReverseId << i.planeTypeId;
        File << i.routeUtilization << i.image;
        File << i.planeUtilization << i.planeUtilizationFC;
        File << i.ticketCostFactor;

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

    File << bot.mNumEmployees;

    File << bot.mItemPills << bot.mItemAntiVirus << bot.mItemAntiStrike << bot.mItemArabTrust << bot.mIsSickToday;

    File << static_cast<SLONG>(bot.mPlanerSolution.size());
    for (const auto &solution : bot.mPlanerSolution) {
        File << static_cast<SLONG>(solution.jobs.size());
        for (const auto &i : solution.jobs) {
            File << i.jobIdx;
            File << i.start;
            File << i.end;
            File << i.objectId;
            File << i.bIsFreight;
        }

        File << solution.totalPremium;
        File << solution.planeId;
        File << solution.scheduleFromTime;
    }

    SLONG magicnumber = 0x42;
    File << magicnumber;

    return (File);
}

TEAKFILE &operator>>(TEAKFILE &File, PlaneTime &planeTime) {
    SLONG date;
    SLONG time;
    File >> date;
    File >> time;
    planeTime = {date, time};

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
    bot.mPlanesForJobsUnassigned.resize(size);
    for (SLONG i = 0; i < size; i++) {
        File >> bot.mPlanesForJobsUnassigned[i];
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
    File >> bot.mOutOfGates;
    File >> bot.mNeedToPlanJobs >> bot.mNeedToPlanRoutes;

    File >> bot.mBossNumCitiesAvailable;
    File >> bot.mBossGateAvailable;

    File >> bot.mTankRatioEmptiedYesterday;
    File >> bot.mKerosineUsedTodaySoFar;
    File >> bot.mKerosineLevelLastChecked;

    File >> size;
    bot.mRoutes.resize(size);
    for (SLONG i = 0; i < bot.mRoutes.size(); i++) {
        Bot::RouteInfo info;
        File >> info.routeId >> info.routeReverseId >> info.planeTypeId;
        File >> info.routeUtilization >> info.image;
        File >> info.planeUtilization >> info.planeUtilizationFC;
        File >> info.ticketCostFactor;

        File >> size;
        info.planeIds.resize(size);
        for (SLONG i = 0; i < info.planeIds.size(); i++) {
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

    File >> bot.mNumEmployees;

    File >> bot.mItemPills >> bot.mItemAntiVirus >> bot.mItemAntiStrike >> bot.mItemArabTrust >> bot.mIsSickToday;

    File >> size;
    bot.mPlanerSolution.resize(size);
    for (SLONG i = 0; i < bot.mPlanerSolution.size(); i++) {
        auto &solution = bot.mPlanerSolution[i];

        File >> size;
        for (SLONG j = 0; j < size; j++) {
            int jobId;
            PlaneTime start;
            PlaneTime end;
            File >> jobId;
            File >> start;
            File >> end;

            solution.jobs.emplace_back(jobId, start, end);
            File >> solution.jobs.back().objectId;
            File >> solution.jobs.back().bIsFreight;
        }

        File >> solution.totalPremium;
        File >> solution.planeId;
        File >> solution.scheduleFromTime;
    }

    SLONG magicnumber = 0;
    File >> magicnumber;
    assert(magicnumber == 0x42);

    return (File);
}
