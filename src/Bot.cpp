#include "Bot.h"

#include "BotHelper.h"
#include "BotPlaner.h"

#include <algorithm>

const bool kAlwaysReplan = true;
const float kSchedulingMinScoreRatio = 16.0f;
const float kSchedulingMinScoreRatioLastMinute = 5.0f;
SLONG kSwitchToRoutesNumPlanesMin = 2;
SLONG kSwitchToRoutesNumPlanesMax = 4;
const SLONG kSmallestAdCampaign = 4;
const SLONG kMinimumImage = -4;
SLONG kMaximumRouteUtilization = 90;
const SLONG kMaximumPlaneUtilization = 70;
const DOUBLE kMaxTicketPriceFactor = 3.2;
const SLONG kTargetEmployeeHappiness = 90;
const SLONG kMinimumEmployeeSkill = 70;
const SLONG kPlaneMinimumZustand = 90;
const SLONG kPlaneTargetZustand = 100;
const SLONG kUsedPlaneMinimumScore = 40;
const DOUBLE kMaxKerosinQualiZiel = 1.2;
const SLONG kStockEmissionMode = 2;
const bool kReduceDividend = false;
const SLONG kMaxSabotageHints = 99;

const SLONG kMoneyEmergencyFund = 100000;
const SLONG kMoneyReserveRepairs = 0;
const SLONG kMoneyReservePlaneUpgrades = 2500 * 1000;
const SLONG kMoneyReserveBuyTanks = 200 * 1000;
const SLONG kMoneyReserveIncreaseDividend = 100 * 1000;
const SLONG kMoneyReservePaybackCredit = 1500 * 1000;
const SLONG kMoneyReserveBuyOwnShares = 2000 * 1000;
const SLONG kMoneyReserveBuyNemesisShares = 6000 * 1000;
const SLONG kMoneyReserveBossOffice = 10 * 1000;
const SLONG kMoneyReserveExpandAirport = 1000 * 1000;
const SLONG kMoneyReserveSabotage = 200 * 1000;

SLONG kPlaneScoreMode = 0;
SLONG kPlaneScoreForceBest = -1;
SLONG kTestMode = 0;

inline const char *getPrioName(Bot::Prio prio) {
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
    case Bot::Prio::Lowest:
        return "Lowest";
    case Bot::Prio::None:
        return "None";
    default:
        redprintf("Bot.cpp: Default case should not be reached.");
        DebugBreak();
        return "INVALID";
    }
    return "INVALID";
}
inline const char *getPrioName(SLONG prio) { return getPrioName(static_cast<Bot::Prio>(prio)); }

SLONG Bot::calcCurrentGainFromJobs() const {
    SLONG gain = 0;
    for (auto planeId : mPlanesForJobs) {
        gain += Helper::calculateScheduleInfo(qPlayer, planeId).gain;
    }
    return gain;
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
        redprintf("Bot::checkPlaneAvailable: Plane %s does not have enough crew members (%ld, need %ld).", Helper::getPlaneName(qPlane).c_str(),
                  qPlane.AnzBegleiter, qPlane.ptAnzBegleiter);
        return false;
    }
    if (qPlane.AnzPiloten < qPlane.ptAnzPiloten) {
        redprintf("Bot::checkPlaneAvailable: Plane %s does not have enough pilots (%ld, need %ld).", Helper::getPlaneName(qPlane).c_str(), qPlane.AnzPiloten,
                  qPlane.ptAnzPiloten);
        return false;
    }
    if (qPlane.Problem != 0) {
        redprintf("Bot::checkPlaneAvailable: Plane %s has a problem for the next %ld hours", Helper::getPlaneName(qPlane).c_str(), qPlane.Problem);
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
            redprintf("Bot::checkPlaneLists(): We lost the plane with ID = %ld", i);
            planesGoneMissing = true;
            continue;
        }
        if (uniquePlaneIds.find(i) != uniquePlaneIds.end()) {
            redprintf("Bot::checkPlaneLists(): Plane with ID = %ld appears in multiple lists: 1 and %ld.", i, uniquePlaneIds[i]);
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
            redprintf("Bot::checkPlaneLists(): We lost the plane with ID = %ld", i);
            removePlaneFromRoute(i);
            planesGoneMissing = true;
            continue;
        }
        if (uniquePlaneIds.find(i) != uniquePlaneIds.end()) {
            redprintf("Bot::checkPlaneLists(): Plane with ID = %ld appears in multiple lists: 2 and %ld.", i, uniquePlaneIds[i]);
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
            redprintf("Bot::checkPlaneLists(): We lost the plane with ID = %ld", i);
            planesGoneMissing = true;
            continue;
        }
        if (uniquePlaneIds.find(i) != uniquePlaneIds.end()) {
            redprintf("Bot::checkPlaneLists(): Plane with ID = %ld appears in multiple lists: 3 and %ld.", i, uniquePlaneIds[i]);
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
            redprintf("Bot::checkPlaneLists(): We lost the plane with ID = %ld", i);
            planesGoneMissing = true;
            continue;
        }
        if (uniquePlaneIds.find(i) != uniquePlaneIds.end()) {
            redprintf("Bot::checkPlaneLists(): Plane with ID = %ld appears in multiple lists: 4 and %ld.", i, uniquePlaneIds[i]);
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

    hprintf("Bot::checkPlaneLists(): Planes for jobs: %ld / %ld are available.", mPlanesForJobs.size(),
            mPlanesForJobs.size() + mPlanesForJobsUnassigned.size());
    hprintf("Bot::checkPlaneLists(): Planes for routes: %ld / %ld are available.", mPlanesForRoutes.size(),
            mPlanesForRoutes.size() + mPlanesForRoutesUnassigned.size());

    return foundProblem || planesGoneMissing;
}

bool Bot::findPlanesNotAvailableForService(std::vector<SLONG> &listAvailable, std::deque<SLONG> &listUnassigned) {
    bool planesGoneMissing = false;
    std::vector<SLONG> newAvailable;
    for (const auto id : listAvailable) {
        auto &qPlane = qPlayer.Planes[id];
        auto worstZustand = std::min(qPlane.WorstZustand, qPlane.Zustand);
        hprintf("Bot::findPlanesNotAvailableForService(): Plane %s: Zustand = %u, WorstZustand = %u, Baujahr = %ld", Helper::getPlaneName(qPlane).c_str(),
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
                GameMechanic::killFlightPlan(qPlayer, id);
            }

            /* remove plane from route */
            removePlaneFromRoute(id);
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
        auto &qPlane = qPlayer.Planes[id];
        auto worstZustand = std::min(qPlane.WorstZustand, qPlane.Zustand);
        hprintf("Bot::findPlanesAvailableForService(): Plane %s: Zustand = %u, WorstZustand = %u, Baujahr = %ld", Helper::getPlaneName(qPlane).c_str(),
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
    return planesGoneMissing;
}

const CRentRoute &Bot::getRentRoute(const Bot::RouteInfo &routeInfo) const { return qPlayer.RentRouten.RentRouten[routeInfo.routeId]; }

const CRoute &Bot::getRoute(const Bot::RouteInfo &routeInfo) const { return Routen[routeInfo.routeId]; }

__int64 Bot::getDailyOpSaldo() const { return qPlayer.BilanzGestern.GetOpSaldo(); }

__int64 Bot::getWeeklyOpSaldo() const { return qPlayer.BilanzWoche.Hole().GetOpSaldo(); }

void Bot::forceReplanning() { qPlayer.RobotActions[1].ActionId = ACTION_NONE; }

Bot::Bot(PLAYER &player) : qPlayer(player) {}

void Bot::printStatisticsLine(CString prefix, bool printHeader) {
    if (printHeader) {
        hprintf("%s: Tag, Geld, Kredit, Available, Saldo, Gewinn, Verlust, Auftraege, Fracht, Routen, Kerosin, Wartung, Planetype, Passagiere, "
                "PassZufrieden, Firmenwert, Flugzeuge, AnzRouten",
                (LPCTSTR)prefix);
    }
    SLONG count = 0;
    for (SLONG i = 0; i < qPlayer.Planes.AnzEntries(); i++) {
        if (qPlayer.Planes.IsInAlbum(i)) {
            if (kPlaneScoreForceBest + 0x10000000 == qPlayer.Planes[i].TypeId) {
                count++;
            }
        }
    }
    auto balance = qPlayer.BilanzWoche.Hole();
    hprintf("%s: %d, %lld, %lld, %lld, %lld, %lld, %lld, %lld, %lld, %lld, %lld, %lld, %ld, %lld, %lld, %lld, %lld, %lld", (LPCTSTR)prefix, Sim.Date,
            qPlayer.Money, qPlayer.Credit, getMoneyAvailable(), balance.GetOpSaldo(), balance.GetOpGewinn(), balance.GetOpVerlust(), balance.Auftraege,
            balance.FrachtAuftraege, balance.Tickets, balance.KerosinFlug + balance.KerosinVorrat, balance.Wartung, count,
            qPlayer.Statistiken[STAT_PASSAGIERE].GetAtPastDay(1), qPlayer.Statistiken[STAT_ZUFR_PASSAGIERE].GetAtPastDay(1),
            qPlayer.Statistiken[STAT_FIRMENWERT].GetAtPastDay(1), qPlayer.Statistiken[STAT_FLUGZEUGE].GetAtPastDay(1),
            qPlayer.Statistiken[STAT_ROUTEN].GetAtPastDay(1));
}

void Bot::RobotInit() {
    if (gQuickTestRun > 0) {
        printStatisticsLine("BotStatistics", (Sim.Date == 0));
    } else {
        auto balance = qPlayer.BilanzWoche.Hole();
        greenprintf("Bot.cpp: Enter RobotInit(): Current day: %d, money: %s $ (op saldo %s = %s %s)", Sim.Date, Insert1000erDots64(qPlayer.Money).c_str(),
                    Insert1000erDots64(balance.GetOpSaldo()).c_str(), Insert1000erDots64(balance.GetOpGewinn()).c_str(),
                    Insert1000erDots64(balance.GetOpVerlust()).c_str());
    }

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

        /* starting planes */
        for (SLONG i = 0; i < qPlayer.Planes.AnzEntries(); i++) {
            if (qPlayer.Planes.IsInAlbum(i)) {
                mPlanesForJobsUnassigned.push_back(qPlayer.Planes.GetIdFromIndex(i));
            }
        }

        /* starting routes */
        const auto &qRRouten = qPlayer.RentRouten.RentRouten;
        for (SLONG routeID = 0; routeID < qRRouten.AnzEntries(); routeID++) {
            if (qRRouten[routeID].Rang != 0) {
                GameMechanic::killRoute(qPlayer, routeID);
                hprintf("Bot::RobotInit(): Removing initial route %s", Helper::getRouteName(Routen[routeID]).c_str());
            }
        }

        /* mission specialization */
        if (Sim.Difficulty == DIFF_TUTORIAL) {
            mItemPills = -1;      /* item not available */
            mItemAntiStrike = -1; /* item not available */
        }
        if (Sim.Difficulty <= DIFF_FIRST) {
            mItemAntiVirus = -1; /* item not available */
        }

        if (Sim.Difficulty == DIFF_NORMAL) {
            kMaximumRouteUtilization = 20;
        }

        /* is this a mission that is usually very fast? */
        if (Sim.Difficulty != DIFF_FREEGAME && Sim.Difficulty != DIFF_FREEGAMEMAP) {
            if (Sim.Difficulty <= DIFF_EASY) {
                // TODO
                // mLongTermStrategy = false;
            }
        }

        if (qPlayer.RobotUse(ROBOT_USE_GROSSESKONTO)) {
            /* immediately start saving money */
            mRunToFinalObjective = FinalPhase::SaveMoney;
        }

        if (qPlayer.RobotUse(ROBOT_USE_DESIGNER_BUY)) {
            if (Sim.Difficulty == DIFF_ATFS05) {
                kSwitchToRoutesNumPlanesMin = std::max(3, kSwitchToRoutesNumPlanesMin);
                kSwitchToRoutesNumPlanesMax = std::max(3, kSwitchToRoutesNumPlanesMax);
                mDesignerPlaneFile = FullFilename("botplane_atfs05_1.plane", MyPlanePath);
            } else if (Sim.Difficulty == DIFF_ATFS08) {
                kSwitchToRoutesNumPlanesMin = std::max(5, kSwitchToRoutesNumPlanesMin);
                kSwitchToRoutesNumPlanesMax = std::max(5, kSwitchToRoutesNumPlanesMax);
                mDesignerPlaneFile = FullFilename("botplane_atfs08_1.plane", MyPlanePath);
            }
            if (mDesignerPlaneFile.empty() || DoesFileExist(mDesignerPlaneFile) == 0) {
                redprintf("Bot::RobotInit: We do not have a designer plane for this mission (filename = %s).", (LPCTSTR)mDesignerPlaneFile);
            } else {
                mDesignerPlane.Load(mDesignerPlaneFile);
            }
        }

        mFirstRun = false;
    }

    for (auto &i : qPlayer.RobotActions) {
        i = {};
    }

    mLastTimeInRoom.clear();
    mNumActionsToday = 0;

    /* strategy state */
    mBestUsedPlaneIdx = -1;
    mDayStarted = false;
    mNeedToShutdownSecurity = false;

    /* status boss office */
    mBossNumCitiesAvailable = -1;
    mBossGateAvailable = false;

    /* items */
    mIsSickToday = false;

    RobotPlan();
    hprintf("Bot.cpp: Leaving RobotInit()");
}

void Bot::RobotPlan() {
    // hprintf("Bot.cpp: Enter RobotPlan()");

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
                       ACTION_UPGRADE_PLANES, ACTION_BUYNEWPLANE, ACTION_BUYUSEDPLANE, ACTION_VISITMUSEUM, ACTION_PERSONAL, ACTION_BUY_KEROSIN,
                       ACTION_BUY_KEROSIN_TANKS, ACTION_SABOTAGE, ACTION_SET_DIVIDEND, ACTION_RAISEMONEY, ACTION_DROPMONEY, ACTION_EMITSHARES,
                       ACTION_SELLSHARES, ACTION_BUYSHARES, ACTION_VISITMECH, ACTION_VISITNASA, ACTION_VISITTELESCOPE, ACTION_VISITMAKLER, ACTION_VISITARAB,
                       ACTION_VISITRICK, ACTION_VISITKIOSK, ACTION_VISITDUTYFREE, ACTION_VISITAUFSICHT, ACTION_EXPANDAIRPORT, ACTION_VISITROUTEBOX,
                       ACTION_VISITROUTEBOX2, ACTION_VISITSECURITY, ACTION_VISITSECURITY2, ACTION_VISITDESIGNER, ACTION_WERBUNG_ROUTES, ACTION_WERBUNG,
                       ACTION_VISITADS};

    if (qRobotActions[0].ActionId != ACTION_NONE || qRobotActions[1].ActionId != ACTION_NONE) {
        hprintf("Bot.cpp: Leaving RobotPlan() (actions already planned)\n");
        return;
    }
    qRobotActions[1].ActionId = ACTION_NONE;
    qRobotActions[2].ActionId = ACTION_NONE;

    /* populate prio list. Will be sorted by priority (1st order) and then by score (2nd order)-
     * score depends on walking distance. */
    struct PrioListItem {
        SLONG actionId{-1};
        Prio prio{Prio::None};
        SLONG secondaryScore{0};
        SLONG walkingDistance{0};
    };
    std::vector<PrioListItem> prioList;
    for (auto &action : actions) {
        if (!Helper::checkRoomOpen(action)) {
            continue;
        }
        auto prio = condAll(action);
        if (prio == Prio::None) {
            continue;
        }

        SLONG p = qPlayer.PlayerNum;
        SLONG room = Helper::getRoomFromAction(p, action);
        SLONG score = qPlayer.PlayerWalkRandom.Rand(0, 100);
        prioList.emplace_back(PrioListItem{action, prio, score});

        if (prio >= Prio::Medium && room > 0 && (Sim.Time > 540000)) {
            /* factor in walking distance for more important actions */
            prioList.back().walkingDistance = Helper::getWalkDistance(p, room);
        }
    }

    if (prioList.size() < 2) {
        prioList.emplace_back(PrioListItem{ACTION_CHECKAGENT2, Prio::Medium, 10000});
    }
    if (prioList.size() < 2) {
        prioList.emplace_back(PrioListItem{ACTION_CHECKAGENT1, Prio::Medium, 10000});
    }

    /* sort by priority */
    std::sort(prioList.begin(), prioList.end(), [](const PrioListItem &a, const PrioListItem &b) {
        if (a.prio == b.prio) {
            return ((a.secondaryScore + a.walkingDistance) < (b.secondaryScore + b.walkingDistance));
        }
        return (a.prio > b.prio);
    });

    /*for (const auto &qAction : prioList) {
        hprintf("Bot::RobotPlan(): %s with prio %s (%d+%d)", getRobotActionName(qAction.actionId), getPrioName(qAction.prio), qAction.secondaryScore,
                qAction.walkingDistance);
    }*/

    qRobotActions[1].ActionId = prioList[0].actionId;
    qRobotActions[1].Running = (prioList[0].prio >= Prio::Medium);
    qRobotActions[1].Prio = static_cast<SLONG>(prioList[0].prio);
    qRobotActions[2].ActionId = prioList[1].actionId;
    qRobotActions[2].Running = (prioList[1].prio >= Prio::Medium);
    qRobotActions[2].Prio = static_cast<SLONG>(prioList[1].prio);

    // hprintf("Bot::RobotPlan(): Action 0: %s", getRobotActionName(qRobotActions[0].ActionId));
    // greenprintf("Bot::RobotPlan(): Action 1: %s with prio %s", getRobotActionName(qRobotActions[1].ActionId), getPrioName(prioList[0].prio));
    // greenprintf("Bot::RobotPlan(): Action 2: %s with prio %s", getRobotActionName(qRobotActions[2].ActionId), getPrioName(prioList[1].prio));

    if (qRobotActions[1].ActionId == ACTION_NONE) {
        redprintf("Did not plan action for slot #1");
    }
    if (qRobotActions[2].ActionId == ACTION_NONE) {
        redprintf("Did not plan action for slot #2");
    }

    // hprintf("Bot.cpp: Leaving RobotPlan()\n");
}

void Bot::RobotExecuteAction() {
    if (mFirstRun) {
        redprintf("Bot::RobotExecuteAction(): Bot was not initialized!");
        RobotInit();
        hprintf("Bot.cpp: Leaving RobotExecuteAction() (not initialized)\n");
        return;
    }

    /* refuse to work outside working hours (game sometimes calls this too early) */
    if (Sim.Time <= 540000) { /* check if it is precisely 09:00 or earlier */
        hprintf("Bot.cpp: Leaving RobotExecuteAction() (too early)\n");
        forceReplanning();
        return;
    }
    if (Sim.GetHour() >= 18) {
        hprintf("Bot.cpp: Leaving RobotExecuteAction() (too late)\n");
        forceReplanning();
        return;
    }

    if (kAlwaysReplan) {
        forceReplanning();
    }

    /* handy references to player data (rw) */
    auto &qAction = qPlayer.RobotActions[0];
    auto &qWorkCountdown = qPlayer.WorkCountdown;

    LocalRandom.Rand(2); // Sicherheitshalber, damit wir immer genau ein Random ausführen

    mNumActionsToday += 1;
    greenprintf("Bot::RobotExecuteAction(): Executing %s (#%d, %s), current time: %02d:%02d, money: %s $ (available: %s $)",
                getRobotActionName(qAction.ActionId), mNumActionsToday, getPrioName(qAction.Prio), Sim.GetHour(), Sim.GetMinute(),
                (LPCTSTR)Insert1000erDots64(qPlayer.Money), (LPCTSTR)Insert1000erDots64(getMoneyAvailable()));

    mOnThePhone = 0;

    __int64 moneyAvailable = getMoneyAvailable();
    switch (qAction.ActionId) {
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
            actionCallInternational(true);
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_CALL_INTER_HANDY:
        if (condCallInternationalHandy() != Prio::None) {
            hprintf("Bot::RobotExecuteAction(): Calling international using mobile phone.");
            actionCallInternational(false);
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
            actionCheckFreightDepot();
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

    case ACTION_UPGRADE_PLANES:
        if (condUpgradePlanes() != Prio::None) {
            actionUpgradePlanes();
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

    case ACTION_BUYUSEDPLANE:
        if (condBuyUsedPlane(moneyAvailable) != Prio::None) {
            actionBuyUsedPlane(moneyAvailable);
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        break;

    case ACTION_VISITMUSEUM:
        if (condVisitMuseum() != Prio::None) {
            mBestUsedPlaneIdx = findBestAvailableUsedPlane();
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
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
            actionSabotage(moneyAvailable);
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_SET_DIVIDEND:
        if (condIncreaseDividend(moneyAvailable) != Prio::None) {
            SLONG _dividende = qPlayer.Dividende;
            SLONG maxToEmit = (2500000 - qPlayer.MaxAktien) / 100 * 100;
            if (kReduceDividend && maxToEmit < 10000) {
                /* we cannot emit any shares anymore. We do not care about stock prices now. */
                _dividende = 0;
            } else if (qPlayer.RobotUse(ROBOT_USE_HIGHSHAREPRICE)) {
                _dividende = 25;
            } else if (LocalRandom.Rand(10) == 0) {
                _dividende++;
                Limit(5, _dividende, 25);
            }

            if (_dividende != qPlayer.Dividende) {
                hprintf("Bot::RobotExecuteAction(): Setting dividend to %ld", _dividende);
                GameMechanic::setDividend(qPlayer, _dividende);
            }
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_RAISEMONEY:
        if (condTakeOutLoan() != Prio::None) {
            __int64 limit = qPlayer.CalcCreditLimit();
            __int64 moneyRequired = -getMoneyAvailable();
            __int64 m = std::min(limit, moneyRequired);
            m = std::max(m, 1000LL);
            if (mRunToFinalObjective == FinalPhase::TargetRun) {
                m = limit;
            }
            if (m > 0) {
                hprintf("Bot::RobotExecuteAction(): Taking loan: %s $", (LPCTSTR)Insert1000erDots64(m));
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
            __int64 m = std::min({qPlayer.Credit, moneyAvailable, getWeeklyOpSaldo()});
            m = std::max(m, 1000LL);
            hprintf("Bot::RobotExecuteAction(): Paying back loan: %s $", (LPCTSTR)Insert1000erDots64(m));
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
            actionSellShares(moneyAvailable);
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 6;
        break;

    case ACTION_BUYSHARES:
        // buy shares from nemesis
        if (condBuyShares(moneyAvailable, mNemesis) != Prio::None) {
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
        if (condVisitNasa(moneyAvailable) != Prio::None) {
            const auto &qPrices = (Sim.Difficulty == DIFF_FINAL) ? RocketPrices : StationPrices;
            auto nParts = sizeof(qPrices) / sizeof(qPrices[0]);
            for (SLONG i = 0; i < nParts; i++) {
                if ((qPlayer.RocketFlags & (1 << i)) == 0 && moneyAvailable >= qPrices[i]) {
                    qPlayer.ChangeMoney(-qPrices[i], 3400, "");
                    PlayFanfare();
                    qPlayer.RocketFlags |= (1 << i);
                }
                moneyAvailable = getMoneyAvailable();
            }
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
            mBestPlaneTypeId = findBestAvailablePlaneType(false)[0];

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

    case ACTION_VISITARAB:
        if (condVisitArab() != Prio::None) {
            if (mItemArabTrust == 1) {
                if (GameMechanic::useItem(qPlayer, ITEM_MG)) {
                    hprintf("Bot::RobotExecuteAction(): Used item MG");
                    mItemArabTrust = 2;
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
            if (qPlayer.StrikeHours > 0 && qPlayer.StrikeEndType == 0 && mItemAntiStrike == 4) {
                hprintf("Bot::RobotExecuteAction(): Ended strike using drunk guy");
                GameMechanic::endStrike(qPlayer, GameMechanic::EndStrikeMode::Drunk);
            }
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
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
            actionVisitBoss();
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
            actionFindBestRoute();
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

    case ACTION_VISITROUTEBOX2:
        if (condVisitRouteBoxRenting(moneyAvailable) != Prio::None) {
            actionRentRoute();
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
            if (GameMechanic::sabotageSecurityOffice(qPlayer)) {
                hprintf("Bot::RobotExecuteAction(): Successfully sabotaged security office.");
            } else {
                redprintf("Bot::RobotExecuteAction(): Failed to sabotage security office!");
            }
            mNeedToShutdownSecurity = false;
            mLastTimeInRoom.erase(ACTION_SABOTAGE); /* allow sabotage again */
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 1;
        break;

    case ACTION_VISITDESIGNER:
        if (condVisitDesigner(moneyAvailable) != Prio::None) {
            actionBuyDesignerPlane(moneyAvailable);
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

    case ACTION_VISITADS:
        if (condVisitAds() != Prio::None) {
            if (mItemAntiVirus == 3) {
                if (GameMechanic::useItem(qPlayer, ITEM_DART)) {
                    hprintf("Bot::RobotExecuteAction(): Used item darts");
                    mItemAntiVirus = 4;
                }
            }
            if (mItemAntiVirus == 4) {
                if (qPlayer.HasItem(ITEM_DISKETTE) == 0) {
                    GameMechanic::pickUpItem(qPlayer, ITEM_DISKETTE);
                    hprintf("Bot::RobotExecuteAction(): Picked up item floppy disk");
                }
            }
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

    default:
        redprintf("Bot::RobotExecuteAction(): Trying to execute invalid action: %s", getRobotActionName(qAction.ActionId));
        // DebugBreak();
    }

    mLastTimeInRoom[qAction.ActionId] = Sim.Time;

    if (qPlayer.RobotUse(ROBOT_USE_WORKQUICK_2) && qWorkCountdown > 2) {
        qWorkCountdown /= 2;
    }

    if (qPlayer.RobotUse(ROBOT_USE_WORKVERYQUICK) && qWorkCountdown > 4) {
        qWorkCountdown /= 4;
    } else if (qPlayer.RobotUse(ROBOT_USE_WORKQUICK) && qWorkCountdown > 2) {
        qWorkCountdown /= 2;
    }

    // hprintf("Bot.cpp: Leaving RobotExecuteAction()\n");
    hprintf("");
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

    File << bot.mNumActionsToday;

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

    File << bot.mLongTermStrategy;
    File << bot.mBestPlaneTypeId << bot.mBestUsedPlaneIdx << bot.mBuyPlaneForRouteId << bot.mUsePlaneForRouteId;
    File << bot.mWantToRentRouteId;
    File << bot.mFirstRun << bot.mDayStarted << bot.mDoRoutes;
    File << static_cast<SLONG>(bot.mRunToFinalObjective) << bot.mMoneyForFinalObjective;
    File << bot.mOutOfGates;
    File << bot.mNeedToPlanJobs << bot.mNeedToPlanRoutes;
    File << bot.mMoneyReservedForRepairs << bot.mMoneyReservedForUpgrades;
    File << bot.mMoneyReservedForAuctions << bot.mMoneyReservedForFines;
    File << bot.mNemesis << bot.mNemesisScore << bot.mNeedToShutdownSecurity << bot.mNemesisSabotaged;

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

    File << bot.mOnThePhone;

    File << bot.mDesignerPlane;
    File << bot.mDesignerPlaneFile;

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

    File >> bot.mNumActionsToday;

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

    SLONG runToFinalObjective = 0;
    File >> bot.mLongTermStrategy;
    File >> bot.mBestPlaneTypeId >> bot.mBestUsedPlaneIdx >> bot.mBuyPlaneForRouteId >> bot.mUsePlaneForRouteId;
    File >> bot.mWantToRentRouteId;
    File >> bot.mFirstRun >> bot.mDayStarted >> bot.mDoRoutes;
    File >> runToFinalObjective >> bot.mMoneyForFinalObjective;
    bot.mRunToFinalObjective = static_cast<Bot::FinalPhase>(runToFinalObjective);
    File >> bot.mOutOfGates;
    File >> bot.mNeedToPlanJobs >> bot.mNeedToPlanRoutes;
    File >> bot.mMoneyReservedForRepairs >> bot.mMoneyReservedForUpgrades;
    File >> bot.mMoneyReservedForAuctions >> bot.mMoneyReservedForFines;
    File >> bot.mNemesis >> bot.mNemesisScore >> bot.mNeedToShutdownSecurity >> bot.mNemesisSabotaged;

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
            SLONG jobId;
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

    File >> bot.mOnThePhone;

    File >> bot.mDesignerPlane;
    File >> bot.mDesignerPlaneFile;

    SLONG magicnumber = 0;
    File >> magicnumber;
    assert(magicnumber == 0x42);

    return (File);
}
