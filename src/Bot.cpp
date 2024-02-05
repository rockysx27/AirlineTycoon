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

void Bot::printGainFromJobs(SLONG oldGain) const {
    SLONG gain = 0;
    for (auto planeId : mPlanesForJobs) {
        gain += Helper::calculateScheduleGain(qPlayer, planeId);
    }
    hprintf("Bot::printGainFromJobs(): Improved gain from jobs from %ld to %ld.", oldGain, gain);
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

bool Bot::findPlanesWithoutCrew(std::vector<SLONG> &listAvailable, std::deque<SLONG> &listUnassigned) {
    hprintf("Bot::findPlanesWithoutCrew: Checking for planes with not enough crew members");
    bool found = false;
    std::vector<SLONG> newAvailable;
    for (const auto id : listAvailable) {
        if (checkPlaneAvailable(id, false)) {
            newAvailable.push_back(id);
        } else {
            listUnassigned.push_back(id);
            found = true;
            GameMechanic::killFlightPlan(qPlayer, id);
            for (auto &route : mRoutes) {
                auto it = route.planeIds.begin();
                while (it != route.planeIds.end() && *it != id) {
                    it++;
                }
                if (it != route.planeIds.end()) {
                    route.planeIds.erase(it);
                }
            }
        }
    }
    std::swap(listAvailable, newAvailable);
    return found;
}

bool Bot::findPlanesWithCrew(std::deque<SLONG> &listUnassigned, std::vector<SLONG> &listAvailable) {
    hprintf("Bot::findPlanesWithCrew: Checking for planes that now have enough crew members");
    bool found = false;
    std::deque<SLONG> newUnassigned;
    for (const auto id : listUnassigned) {
        if (checkPlaneAvailable(id, false)) {
            listAvailable.push_back(id);
        } else {
            newUnassigned.push_back(id);
            found = true;
        }
    }
    std::swap(listUnassigned, newUnassigned);
    return found;
}

const CRentRoute &Bot::getRentRoute(const Bot::RouteInfo &routeInfo) const { return qPlayer.RentRouten.RentRouten[routeInfo.routeId]; }

const CRoute &Bot::getRoute(const Bot::RouteInfo &routeInfo) const { return Routen[routeInfo.routeId]; }

SLONG Bot::getDailyOpSaldo() const { return qPlayer.BilanzGestern.GetOpSaldo(); }

SLONG Bot::getWeeklyOpSaldo() const { return qPlayer.BilanzWoche.Hole().GetOpSaldo(); }

Bot::Bot(PLAYER &player) : qPlayer(player) {}

void Bot::RobotInit() {
    hprintf("Bot.cpp: Enter RobotInit()");

    auto &qRobotActions = qPlayer.RobotActions;

    if (mFirstRun) {
        /* random source */
        LocalRandom.SRand(qPlayer.WaitWorkTill);

        for (SLONG i = 0; i < qPlayer.Planes.AnzEntries(); i++) {
            if (qPlayer.Planes.IsInAlbum(i)) {
                mPlanesForJobsUnassigned.push_back(qPlayer.Planes.GetIdFromIndex(i));
            }
        }
        mFirstRun = false;
    }

    for (auto &i : qRobotActions) {
        i = {};
    }

    mLastTimeInRoom.clear();
    mDislike = -1;
    mBestPlaneTypeId = -1;
    mBossNumCitiesAvailable = -1;
    mBossGateAvailable = false;

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

    SLONG actions[] = {ACTION_STARTDAY,          ACTION_BUERO,          ACTION_CALL_INTERNATIONAL, ACTION_CHECKAGENT1,    ACTION_CHECKAGENT2,
                       ACTION_CHECKAGENT3,       ACTION_UPGRADE_PLANES, ACTION_BUYNEWPLANE,        ACTION_PERSONAL,       ACTION_BUY_KEROSIN,
                       ACTION_BUY_KEROSIN_TANKS, ACTION_SABOTAGE,       ACTION_SET_DIVIDEND,       ACTION_RAISEMONEY,     ACTION_DROPMONEY,
                       ACTION_EMITSHARES,        ACTION_SELLSHARES,     ACTION_BUYSHARES,          ACTION_VISITMECH,      ACTION_VISITNASA,
                       ACTION_VISITTELESCOPE,    ACTION_VISITRICK,      ACTION_VISITKIOSK,         ACTION_BUYUSEDPLANE,   ACTION_VISITDUTYFREE,
                       ACTION_VISITAUFSICHT,     ACTION_EXPANDAIRPORT,  ACTION_VISITROUTEBOX,      ACTION_VISITROUTEBOX2, ACTION_VISITSECURITY,
                       ACTION_VISITSECURITY2,    ACTION_VISITDESIGNER,  ACTION_WERBUNG_ROUTES,     ACTION_WERBUNG};

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

    /* handy references to player data (rw) */
    auto &qRobotActions = qPlayer.RobotActions;
    auto &qWorkCountdown = qPlayer.WorkCountdown;

    LocalRandom.Rand(2); // Sicherheitshalber, damit wir immer genau ein Random ausführen

    /* temporary data */
    __int64 moneyAvailable = getMoneyAvailable();

    greenprintf("Bot.cpp: Enter RobotExecuteAction(): Executing %s", getRobotActionName(qRobotActions[0].ActionId));

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

    case ACTION_BUERO:
        if (condBuero() != Prio::None) {
            actionBuero();
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
            actionUpgradePlanes(moneyAvailable);
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }

        qWorkCountdown = 20 * 5;
        break;

    case ACTION_BUYNEWPLANE:
        if (condBuyNewPlane(moneyAvailable) != Prio::None) {
            SLONG bestPlaneTypeId = mDoRoutes ? mBuyPlaneForRouteId : mBestPlaneTypeId;
            assert(bestPlaneTypeId >= 0);
            assert(qPlayer.xPiloten >= PlaneTypes[bestPlaneTypeId].AnzPiloten);
            assert(qPlayer.xBegleiter >= PlaneTypes[bestPlaneTypeId].AnzBegleiter);
            for (auto i : GameMechanic::buyPlane(qPlayer, bestPlaneTypeId, 1)) {
                assert(i >= 0x1000000);
                hprintf("Bot::RobotExecuteAction(): Bought plane (%s) %s", (LPCTSTR)qPlanes[i].Name, (LPCTSTR)PlaneTypes[bestPlaneTypeId].Name);
                if (mDoRoutes) {
                    mPlanesForRoutesUnassigned.push_back(i);
                    mNeedToDoPlanning = true;
                } else {
                    if (checkPlaneAvailable(i, true)) {
                        mPlanesForJobs.push_back(i);
                    } else {
                        mPlanesForJobsUnassigned.push_back(i);
                    }
                }
            }
            moneyAvailable = getMoneyAvailable();
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
        qPlayer.ArabTrust = max(1, qPlayer.ArabTrust); // Computerspieler müssen Araber nicht bestechen
        if (condSabotage(moneyAvailable) != Prio::None) {
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
            moneyAvailable = getMoneyAvailable();
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
            moneyAvailable = getMoneyAvailable();
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        break;

    case ACTION_VISITDUTYFREE:
        if (condVisitDutyFree(moneyAvailable) != Prio::None) {
            if (qPlayer.LaptopQuality < 4) {
                auto quali = qPlayer.LaptopQuality;
                GameMechanic::buyDutyFreeItem(qPlayer, ITEM_LAPTOP);
                moneyAvailable = getMoneyAvailable();
                hprintf("Bot::RobotExecuteAction(): Buying laptop (%ld => %ld)", quali, qPlayer.LaptopQuality);
            } else if (!qPlayer.HasItem(ITEM_HANDY)) {
                hprintf("Bot::RobotExecuteAction(): Buying cell phone");
                GameMechanic::buyDutyFreeItem(qPlayer, ITEM_HANDY);
                moneyAvailable = getMoneyAvailable();
            }
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
            actionWerbungRoutes(moneyAvailable);
        } else {
            redprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

    case ACTION_WERBUNG:
        if (condBuyAds(moneyAvailable) != Prio::None) {
            actionWerbung(moneyAvailable);
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
    File << bot.mNeedToDoPlanning;

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
    File >> bot.mNeedToDoPlanning;

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

    SLONG magicnumber = 0;
    File >> magicnumber;
    assert(magicnumber == 0x42);

    return (File);
}
