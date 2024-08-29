#include "Bot.h"

#include "BotHelper.h"
#include "BotPlaner.h"
#include "class.h"
#include "GameMechanic.h"
#include "global.h"
#include "TeakLibW.h"

#include <algorithm>
#include <cassert>
#include <climits>
#include <cmath>

// #define PRINT_ROUTE_DETAILS 1

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

template <typename T> inline bool eraseFirst(T &l, SLONG val) {
    auto it = l.begin();
    while (it != l.end() && *it != val) {
        it++;
    }
    if (it != l.end()) {
        std::iter_swap(it, l.end() - 1);
        l.resize(l.size() - 1);
        return true;
    }
    return false;
}

void Bot::actionStartDay(__int64 moneyAvailable) {
    actionStartDayLaptop(moneyAvailable);

    /* always use tanks: We get discount from advisor and by using cheap kerosine */
    GameMechanic::setKerosinTankOpen(qPlayer, true);
}

void Bot::actionStartDayLaptop(__int64 moneyAvailable) {
    mDayStarted = true;

    /*  invalidate cached info */
    mRoutesUpdated = false;
    mRoutesUtilizationUpdated = false;
    mRoutesNextStep = RoutesNextStep::None;
    mExtraPilots = -1;
    mExtraBegleiter = -1;

    mArabHintsTracker -= std::min(3, mArabHintsTracker);

    /* check lists of planes, check which planes are available for service and which are not */
    checkPlaneLists();

    /* check routes */
    if (mDoRoutes) {
        checkLostRoutes();
        updateRouteInfoOffice();
        requestPlanRoutes(true);
    } else if (qPlayer.RobotUse(ROBOT_USE_ROUTES) && (getNumRentedRoutes() == 0)) {
        /* logic for switching to routes. Before switching, make sure any initially rented routes have been cancelled */
        if (qPlayer.RobotUse(ROBOT_USE_FORCEROUTES)) {
            mDoRoutes = true;
            hprintf("Bot::RobotInit(): Switching to routes (forced).");
        } else if (mBestPlaneTypeId != -1) {
            const auto &bestPlaneType = PlaneTypes[mBestPlaneTypeId];
            SLONG costRouteAd = gWerbePrice[1 * 6 + 5];
            __int64 moneyNeeded = 2 * costRouteAd + bestPlaneType.Preis;
            SLONG numPlanes = mPlanesForJobs.size() + mPlanesForJobsUnassigned.size();
            if ((numPlanes >= kSwitchToRoutesNumPlanesMin && moneyAvailable >= moneyNeeded) || numPlanes >= kSwitchToRoutesNumPlanesMax) {
                mDoRoutes = true;
                hprintf("Bot::actionStartDay(): Switching to routes. Reserving 2*%d + %d for ads and plane.", costRouteAd, bestPlaneType.Preis);
            }
        }
    }
    if (mDoRoutes && !mLongTermStrategy) {
        mLongTermStrategy = true;
        hprintf("Bot::actionStartDay(): Switching to longterm strategy.");
    }

    /* logic deciding when to switch to final target run */
    determineNemesis();
    switchToFinalTarget();

    /* update how much kerosine was used */
    assert(mKerosineLevelLastChecked >= qPlayer.TankInhalt);
    mKerosineUsedTodaySoFar += (mKerosineLevelLastChecked - qPlayer.TankInhalt);
    mKerosineLevelLastChecked = qPlayer.TankInhalt;
    mTankRatioEmptiedYesterday = 1.0 * mKerosineUsedTodaySoFar / qPlayer.Tank;
    mKerosineUsedTodaySoFar = 0;

    /* starting jobs (check every day because of mission DIFF_ADDON09) */
    SLONG numToPlan = 0;
    for (int i = 0; i < qPlayer.Auftraege.AnzEntries(); i++) {
        if (qPlayer.Auftraege.IsInAlbum(i) == 0) {
            continue;
        }
        if (qPlayer.Auftraege[i].InPlan == 0) {
            numToPlan++;
        }
    }
    if (numToPlan > 0) {
        hprintf("Bot::RobotInit(): Have %d jobs to plan", numToPlan);
        BotPlaner planer(qPlayer, qPlayer.Planes);
        grabFlights(planer, true);
    }

    /* some conditions might have changed (plane availability) */
    forceReplanning();
}

void Bot::actionBuero() {
    if (mNeedToPlanJobs) {
        planFlights();
    }
    if (mDoRoutes) {
        updateRouteInfoOffice();
        assignPlanesToRoutes(true);
        if (mNeedToPlanRoutes) {
            planRoutes();
        }
    }
}

void Bot::actionCallInternational(bool areWeInOffice) {
    std::vector<SLONG> cities;
    for (SLONG n = 0; n < Cities.AnzEntries(); n++) {
        if (!GameMechanic::canCallInternational(qPlayer, n)) {
            continue;
        }

        GameMechanic::refillFlightJobs(n);
        cities.push_back(n);
    }

    hprintf("Bot::actionCallInternational(): There are %u cities we can call.", cities.size());
    if (!cities.empty()) {
        BotPlaner planer(qPlayer, qPlayer.Planes);
        planer.addJobSource(BotPlaner::JobOwner::International, cities);
        planer.addJobSource(BotPlaner::JobOwner::InternationalFreight, cities);
        grabFlights(planer, areWeInOffice);

        SLONG cost = cities.size();
        qPlayer.History.AddCallCost(cost);
        qPlayer.Money -= cost;
        qPlayer.Statistiken[STAT_A_SONSTIGES].AddAtPastDay(-cost);
        qPlayer.Bilanz.SonstigeAusgaben -= cost;
    }
}

void Bot::actionCheckLastMinute() {
    LastMinuteAuftraege.RefillForLastMinute();

    BotPlaner planer(qPlayer, qPlayer.Planes);
    planer.addJobSource(BotPlaner::JobOwner::LastMinute, {});
    grabFlights(planer, false);

    LastMinuteAuftraege.RefillForLastMinute();
}

void Bot::actionCheckTravelAgency() {
    if (mItemAntiVirus == 0) {
        if (GameMechanic::PickUpItemResult::PickedUp == GameMechanic::pickUpItem(qPlayer, ITEM_SPINNE)) {
            hprintf("Bot::actionCheckTravelAgency(): Picked up item tarantula");
            mItemAntiVirus = 1;
        }
        if (HowToPlan::None == canWePlanFlights()) {
            return; /* avoid warning in grabFlights(). We only came here for the item */
        }
    }

    ReisebueroAuftraege.RefillForReisebuero();

    BotPlaner planer(qPlayer, qPlayer.Planes);
    planer.addJobSource(BotPlaner::JobOwner::TravelAgency, {});
    grabFlights(planer, false);

    ReisebueroAuftraege.RefillForReisebuero();
}

void Bot::actionCheckFreightDepot() {
    gFrachten.Refill();

    BotPlaner planer(qPlayer, qPlayer.Planes);
    planer.addJobSource(BotPlaner::JobOwner::Freight, {});
    grabFlights(planer, false);

    gFrachten.Refill();
}

void Bot::actionUpgradePlanes() {
    /* which planes to upgrade */
    std::vector<SLONG> planes = mPlanesForRoutes;
    if ((mRunToFinalObjective == FinalPhase::TargetRun) && qPlayer.RobotUse(ROBOT_USE_LUXERY)) {
        planes = getAllPlanes();
    }

    bool onlySecurity = (Sim.Difficulty == DIFF_ATFS02) && (mRunToFinalObjective == FinalPhase::TargetRun);

    /* cancel all currently planned plane ugprades */
    mMoneyReservedForUpgrades = 0;
    for (SLONG c = 0; c < planes.size(); c++) {
        auto &qPlane = qPlayer.Planes[planes[c]];

        qPlane.SitzeTarget = qPlane.Sitze;
        qPlane.TablettsTarget = qPlane.Tabletts;
        qPlane.DecoTarget = qPlane.Deco;
        qPlane.ReifenTarget = qPlane.Reifen;
        qPlane.TriebwerkTarget = qPlane.Triebwerk;
        qPlane.SicherheitTarget = qPlane.Sicherheit;
        qPlane.ElektronikTarget = qPlane.Elektronik;
    }

    /* plan new plane ugprades until we run out of money */
    auto randOffset = LocalRandom.Rand(planes.size());
    for (SLONG upgradeWhat = 0; upgradeWhat < 7; upgradeWhat++) {
        auto moneyAvailable = getMoneyAvailable() - kMoneyReservePlaneUpgrades;
        if (moneyAvailable < 0) {
            break;
        }

        for (SLONG c = 0; c < planes.size(); c++) {
            SLONG idx = (c + randOffset) % planes.size();
            CPlane &qPlane = qPlayer.Planes[planes[idx]];
            auto ptPassagiere = qPlane.ptPassagiere;

            moneyAvailable = getMoneyAvailable() - kMoneyReservePlaneUpgrades;
            if (moneyAvailable < 0) {
                break;
            }

            SLONG cost = 0;
            switch (upgradeWhat) {
            case 0:
                cost = ptPassagiere * (SeatCosts[2] - SeatCosts[qPlane.Sitze] / 2);
                if (qPlane.SitzeTarget < 2 && cost <= moneyAvailable && !onlySecurity) {
                    qPlane.SitzeTarget = 2;
                    mMoneyReservedForUpgrades += cost;
                    hprintf("Bot::actionUpgradePlanes(): Upgrading seats in %s.", Helper::getPlaneName(qPlane).c_str());
                }
                break;
            case 1:
                cost = ptPassagiere * (TrayCosts[2] - TrayCosts[qPlane.Tabletts] / 2);
                if (qPlane.TablettsTarget < 2 && cost <= moneyAvailable && !onlySecurity) {
                    qPlane.TablettsTarget = 2;
                    mMoneyReservedForUpgrades += cost;
                    hprintf("Bot::actionUpgradePlanes(): Upgrading tabletts in %s.", Helper::getPlaneName(qPlane).c_str());
                }
                break;
            case 2:
                cost = ptPassagiere * (DecoCosts[2] - DecoCosts[qPlane.Deco] / 2);
                if (qPlane.DecoTarget < 2 && cost <= moneyAvailable && !onlySecurity) {
                    qPlane.DecoTarget = 2;
                    mMoneyReservedForUpgrades += cost;
                    hprintf("Bot::actionUpgradePlanes(): Upgrading deco in %s.", Helper::getPlaneName(qPlane).c_str());
                }
                break;
            case 3:
                cost = (ReifenCosts[2] - ReifenCosts[qPlane.Reifen] / 2);
                if (qPlane.ReifenTarget < 2 && cost <= moneyAvailable) {
                    qPlane.ReifenTarget = 2;
                    mMoneyReservedForUpgrades += cost;
                    hprintf("Bot::actionUpgradePlanes(): Upgrading tires in %s.", Helper::getPlaneName(qPlane).c_str());
                }
                break;
            case 4:
                cost = (TriebwerkCosts[2] - TriebwerkCosts[qPlane.Triebwerk] / 2);
                if (qPlane.TriebwerkTarget < 2 && cost <= moneyAvailable) {
                    qPlane.TriebwerkTarget = 2;
                    mMoneyReservedForUpgrades += cost;
                    hprintf("Bot::actionUpgradePlanes(): Upgrading engines in %s.", Helper::getPlaneName(qPlane).c_str());
                }
                break;
            case 5:
                cost = (SicherheitCosts[2] - SicherheitCosts[qPlane.Sicherheit] / 2);
                if (qPlane.SicherheitTarget < 2 && cost <= moneyAvailable) {
                    qPlane.SicherheitTarget = 2;
                    mMoneyReservedForUpgrades += cost;
                    hprintf("Bot::actionUpgradePlanes(): Upgrading safety in %s.", Helper::getPlaneName(qPlane).c_str());
                }
                break;
            case 6:
                cost = (ElektronikCosts[2] - ElektronikCosts[qPlane.Elektronik] / 2);
                if (qPlane.ElektronikTarget < 2 && cost <= moneyAvailable) {
                    qPlane.ElektronikTarget = 2;
                    mMoneyReservedForUpgrades += cost;
                    hprintf("Bot::actionUpgradePlanes(): Upgrading electronics in %s.", Helper::getPlaneName(qPlane).c_str());
                }
                break;
            default:
                redprintf("Bot::actionUpgradePlanes(): Default case should not be reached.");
                DebugBreak();
            }
        }
    }
    hprintf("Bot::actionUpgradePlanes(): We are reserving %s $ for plane upgrades, available money: %s $",
            (LPCTSTR)Insert1000erDots64(mMoneyReservedForUpgrades), (LPCTSTR)Insert1000erDots64(getMoneyAvailable()));

    mRoutesNextStep = RoutesNextStep::None;
}

void Bot::updateExtraWorkers() {
    if (qPlayer.HasBerater(BERATERTYP_PERSONAL) > 0) {
        mExtraPilots = qPlayer.xPiloten;
        mExtraBegleiter = qPlayer.xBegleiter;
    } else {
        mExtraPilots = -1;
        mExtraBegleiter = -1;
    }
    hprintf("Bot::updateExtraWorkers(): We have %d extra pilots and %d extra attendants", mExtraPilots, mExtraBegleiter);
}

void Bot::actionBuyNewPlane(__int64 /*moneyAvailable*/) {
    if (mItemAntiStrike == 0 && (rand() % 2 == 0)) { /* rand() because human player has same chance of item appearing */
        if (GameMechanic::PickUpItemResult::PickedUp == GameMechanic::pickUpItem(qPlayer, ITEM_BH)) {
            hprintf("Bot::actionBuyNewPlane(): Picked up item BH");
            mItemAntiStrike = 1;
        }
    }

    SLONG bestPlaneTypeId = mDoRoutes ? mBuyPlaneForRouteId : mBestPlaneTypeId;
    if (bestPlaneTypeId < 0) {
        redprintf("Bot::actionBuyNewPlane(): No plane was selected!");
        return;
    }
    if (qPlayer.xPiloten < PlaneTypes[bestPlaneTypeId].AnzPiloten || qPlayer.xBegleiter < PlaneTypes[bestPlaneTypeId].AnzBegleiter) {
        redprintf("Bot::actionBuyNewPlane(): Not enough crew for selected plane!");
    }

    auto list = GameMechanic::buyPlane(qPlayer, bestPlaneTypeId, 1);
    if (list.empty()) {
        redprintf("Bot::actionBuyNewPlane(): Gamemechanic returned error!");
        return;
    }
    assert(list.size() == 1);
    assert(list[0] >= 0x1000000);

    auto planeId = list[0];
    auto &qPlane = qPlayer.Planes[planeId];
    qPlane.MaxBegleiter = qPlane.ptAnzBegleiter;
    hprintf("Bot::actionBuyNewPlane(): Bought plane %s", Helper::getPlaneName(qPlane).c_str());
    if (mDoRoutes) {
        if (mRoutesNextStep == RoutesNextStep::BuyMorePlanes) {
            assert(mImproveRouteId != -1);
            auto &qRoute = mRoutes[mImproveRouteId];
            qRoute.planeIds.push_back(planeId);
            mPlanesForRoutes.push_back(planeId);
            hprintf("Bot::actionBuyNewPlane(): Assigning new plane %s to route %s", Helper::getPlaneName(qPlane).c_str(),
                    Helper::getRouteName(getRoute(qRoute)).c_str());
            mRoutesNextStep = RoutesNextStep::None;
        } else {
            mPlanesForRoutesUnassigned.push_back(planeId);
        }
        requestPlanRoutes(false);
        mBuyPlaneForRouteId = -1;
    } else {
        if (checkPlaneAvailable(planeId, true, false)) {
            mPlanesForJobs.push_back(planeId);
            grabNewFlights();
        } else {
            mPlanesForJobsUnassigned.push_back(planeId);
        }
        mBestPlaneTypeId = -1;
    }

    updateExtraWorkers();
}

void Bot::actionBuyUsedPlane(__int64 /*moneyAvailable*/) {
    if (mBestUsedPlaneIdx < 0) {
        redprintf("Bot::actionBuyUsedPlane(): We have not yet checked which plane to buy!");
        return;
    }

    if (qPlayer.xPiloten < Sim.UsedPlanes[mBestUsedPlaneIdx].ptAnzPiloten || qPlayer.xBegleiter < Sim.UsedPlanes[mBestUsedPlaneIdx].ptAnzBegleiter) {
        redprintf("Bot::actionBuyUsedPlane(): Not enough crew for selected plane!");
    }

    SLONG planeId = GameMechanic::buyUsedPlane(qPlayer, mBestUsedPlaneIdx);
    assert(planeId >= 0x1000000);

    auto &qPlane = qPlayer.Planes[planeId];
    qPlane.MaxBegleiter = qPlane.ptAnzBegleiter;
    hprintf("Bot::actionBuyUsedPlane(): Bought used plane %s", Helper::getPlaneName(qPlane).c_str());
    hprintf("Bot::actionBuyUsedPlane(): Passengers = %d, fuel = %d, year = %d", qPlane.ptPassagiere, qPlane.ptVerbrauch, qPlane.Baujahr);
    SLONG improvementNeeded = std::max(0, 80 - qPlane.WorstZustand);
    SLONG repairCost = improvementNeeded * (qPlane.ptPreis / 110);
    hprintf("Bot::actionBuyUsedPlane(): WorstZustand = %u, cost = %d", qPlane.WorstZustand, repairCost);

    /* we always repair used planes first */
    mPlanesForJobsUnassigned.push_back(planeId);

    updateExtraWorkers();
}

void Bot::actionBuyDesignerPlane(__int64 /*moneyAvailable*/) {
    if (mDesignerPlane.Name.empty() || !mDesignerPlane.IsBuildable()) {
        redprintf("Bot::actionBuyDesignerPlane(): No designer plane!");
        return;
    }

    if (qPlayer.xPiloten < mDesignerPlane.CalcPiloten() || qPlayer.xBegleiter < mDesignerPlane.CalcBegleiter()) {
        redprintf("Bot::actionBuyDesignerPlane(): Not enough crew for selected plane!");
    }

    auto list = GameMechanic::buyXPlane(qPlayer, mDesignerPlaneFile, 1);
    if (list.empty()) {
        redprintf("Bot::actionBuyDesignerPlane(): Gamemechanic returned error!");
        return;
    }
    assert(list.size() == 1);
    assert(list[0] >= 0x1000000);

    auto planeId = list[0];
    auto &qPlane = qPlayer.Planes[planeId];
    qPlane.MaxBegleiter = qPlane.ptAnzBegleiter;
    hprintf("Bot::actionBuyDesignerPlane(): Bought plane %s", Helper::getPlaneName(qPlane).c_str());
    if (mDoRoutes) {
        if (mRoutesNextStep == RoutesNextStep::BuyMorePlanes) {
            assert(mImproveRouteId != -1);
            auto &qRoute = mRoutes[mImproveRouteId];
            qRoute.planeIds.push_back(planeId);
            mPlanesForRoutes.push_back(planeId);
            hprintf("Bot::actionBuyDesignerPlane(): Assigning new plane %s to route %s", Helper::getPlaneName(qPlane).c_str(),
                    Helper::getRouteName(getRoute(qRoute)).c_str());
            mRoutesNextStep = RoutesNextStep::None;
        } else {
            mPlanesForRoutesUnassigned.push_back(planeId);
        }
        requestPlanRoutes(false);
    } else {
        if (checkPlaneAvailable(planeId, true, false)) {
            mPlanesForJobs.push_back(planeId);
            grabNewFlights();
        } else {
            mPlanesForJobsUnassigned.push_back(planeId);
        }
    }

    updateExtraWorkers();
}

void Bot::actionVisitHR() {
    if (mItemPills == 1) {
        if (GameMechanic::useItem(qPlayer, ITEM_POSTKARTE)) {
            hprintf("Bot::actionVisitHR(): Used item card");
            mItemPills = 2;
        }
    }
    if (mItemPills == 2) {
        if (qPlayer.HasItem(ITEM_TABLETTEN) == 0) {
            GameMechanic::pickUpItem(qPlayer, ITEM_TABLETTEN);
            hprintf("Bot::actionVisitHR(): Picked up item pills");
        }
    }

    /* advisors */
    std::vector<SLONG> wantedAdvisors = {BERATERTYP_PERSONAL, BERATERTYP_KEROSIN, BERATERTYP_GELD,      BERATERTYP_INFO,
                                         BERATERTYP_FLUGZEUG, BERATERTYP_FITNESS, BERATERTYP_SICHERHEIT};
    for (auto advisorType : wantedAdvisors) {
        SLONG bestCandidateId = -1;
        SLONG bestCandidateSkill = 0;
        for (SLONG c = 0; c < Workers.Workers.AnzEntries(); c++) {
            const auto &qWorker = Workers.Workers[c];
            if (qWorker.Typ != advisorType) {
                continue;
            }
            if (bestCandidateSkill < qWorker.Talent) {
                if (qWorker.Employer == qPlayer.PlayerNum) {
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
            hprintf("Bot::actionVisitHR(): Upgrading advisor %d to skill level %d", advisorType, bestCandidateSkill);
            /* fire existing adivsors */
            for (SLONG c = 0; c < Workers.Workers.AnzEntries(); c++) {
                const auto &qWorker = Workers.Workers[c];
                if (qWorker.Employer == qPlayer.PlayerNum && qWorker.Typ == advisorType) {
                    if (GameMechanic::fireWorker(qPlayer, c)) {
                        mNumEmployees--;
                    }
                }
            }
            /* hire new advisor */
            if (GameMechanic::hireWorker(qPlayer, bestCandidateId)) {
                mNumEmployees++;
            }
        }
    }

    /* crew */
    SLONG pilotsTarget = 3;     /* sensible default */
    SLONG stewardessTarget = 6; /* sensible default */
    SLONG numPilotsHired = 0;
    SLONG numStewardessHired = 0;
    if (mLongTermStrategy) {
        SLONG bestPlaneTypeId = mDoRoutes ? mBuyPlaneForRouteId : mBestPlaneTypeId;
        if (bestPlaneTypeId >= 0) {
            const auto &bestPlaneType = PlaneTypes[bestPlaneTypeId];
            pilotsTarget = bestPlaneType.AnzPiloten;
            stewardessTarget = bestPlaneType.AnzBegleiter;
        }
    } else {
        if (mBestUsedPlaneIdx != -1) {
            const auto &bestPlane = Sim.UsedPlanes[mBestUsedPlaneIdx];
            pilotsTarget = bestPlane.ptAnzPiloten;
            stewardessTarget = bestPlane.ptAnzBegleiter;
        }
    }
    if (!mDesignerPlane.Name.empty()) {
        pilotsTarget = std::max(pilotsTarget, mDesignerPlane.CalcPiloten());
        stewardessTarget = std::max(stewardessTarget, mDesignerPlane.CalcBegleiter());
    }

    for (SLONG c = 0; c < Workers.Workers.AnzEntries(); c++) {
        const auto &qWorker = Workers.Workers[c];
        if (qWorker.Employer != WORKER_JOBLESS) {
            continue;
        }
        if (qWorker.Talent < kMinimumEmployeeSkill) {
            continue;
        }
        if (qWorker.Typ == WORKER_PILOT && qPlayer.xPiloten < pilotsTarget) {
            if (GameMechanic::hireWorker(qPlayer, c)) {
                mNumEmployees++;
                numPilotsHired++;
            }
        } else if (qWorker.Typ == WORKER_STEWARDESS && qPlayer.xBegleiter < stewardessTarget) {
            if (GameMechanic::hireWorker(qPlayer, c)) {
                mNumEmployees++;
                numStewardessHired++;
            }
        }
    }
    if (numPilotsHired > 0 || numStewardessHired > 0) {
        hprintf("Bot::actionVisitHR(): Hiring %d pilots and %d attendants", numPilotsHired, numStewardessHired);
    }

    /* check whether we lost employees / increase salary for unhappy employees once */
    SLONG nWorkers = 0;
    SLONG salaryIncreases = 0;
    for (SLONG c = 0; c < Workers.Workers.AnzEntries(); c++) {
        auto &qWorker = Workers.Workers[c];
        if (qWorker.Employer != qPlayer.PlayerNum) {
            continue;
        }

        nWorkers++;

        /* unhappy? */
        if (qWorker.Happyness < kTargetEmployeeHappiness && qWorker.Gehalt <= qWorker.OriginalGehalt) {
            qWorker.Gehaltsaenderung(true);
            salaryIncreases++;
        }
    }
    if (mNumEmployees > nWorkers) {
        hprintf("Bot::actionVisitHR(): We lost %d employees", mNumEmployees - nWorkers);
    }
    if (salaryIncreases > 0) {
        hprintf("Bot::actionVisitHR(): Increases salary of %d employees", salaryIncreases);
    }

    mNumEmployees = nWorkers;
    mExtraPilots = qPlayer.xPiloten;
    mExtraBegleiter = qPlayer.xBegleiter;
    hprintf("Bot::actionVisitHR(): We have %d extra pilots and %d extra attendants", mExtraPilots, mExtraBegleiter);
}

void Bot::actionBuyKerosine(__int64 moneyAvailable) {
    if (mItemArabTrust == 1) {
        if (GameMechanic::useItem(qPlayer, ITEM_MG)) {
            hprintf("Bot::actionBuyKerosine(): Used item MG");
            mItemArabTrust = 2;
        }
    }

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

    /* update how much kerosine was used */
    assert(mKerosineLevelLastChecked >= qPlayer.TankInhalt);
    mKerosineUsedTodaySoFar += (mKerosineLevelLastChecked - qPlayer.TankInhalt);
    mKerosineLevelLastChecked = qPlayer.TankInhalt;

    if (moneyToSpend > 0) {
        auto res = kerosineQualiOptimization(moneyToSpend, targetFillRatio);
        hprintf("Bot::actionBuyKerosine(): Buying %lld good and %lld bad kerosine", res.first, res.second);

        auto qualiOld = qPlayer.KerosinQuali;
        auto amountOld = qPlayer.TankInhalt;
        GameMechanic::buyKerosin(qPlayer, 1, res.first);
        GameMechanic::buyKerosin(qPlayer, 2, res.second);
        mKerosineLevelLastChecked = qPlayer.TankInhalt;

        hprintf("Bot::actionBuyKerosine(): Kerosine quantity: %d => %d", amountOld, qPlayer.TankInhalt);
        hprintf("Bot::actionBuyKerosine(): Kerosine quality: %.2f => %.2f", qualiOld, qPlayer.KerosinQuali);
    }
}

void Bot::actionBuyKerosineTank(__int64 moneyAvailable) {
    auto nTankTypes = sizeof(TankSize) / sizeof(TankSize[0]);
    for (SLONG i = nTankTypes - 1; i >= 1; i--) // avoid cheapest tank (not economical)
    {
        if (moneyAvailable >= TankPrice[i]) {
            SLONG amount = std::min(3LL, moneyAvailable / TankPrice[i]);
            hprintf("Bot::actionBuyKerosineTank(): Buying %d times tank type %d", amount, i);
            GameMechanic::buyKerosinTank(qPlayer, i, amount);
            moneyAvailable = getMoneyAvailable() - kMoneyReserveBuyTanks;
            break;
        }
    }
}

void Bot::actionSabotage(__int64 moneyAvailable) {
    if (mItemAntiVirus == 1) {
        if (GameMechanic::useItem(qPlayer, ITEM_SPINNE)) {
            hprintf("Bot::actionSabotage(): Used item tarantula");
            mItemAntiVirus = 2;
        }
    }
    if (mItemAntiVirus == 2) {
        if (GameMechanic::PickUpItemResult::PickedUp == GameMechanic::pickUpItem(qPlayer, ITEM_DART)) {
            hprintf("Bot::actionSabotage(): Picked up item dart");
            mItemAntiVirus = 3;
        }
    }
    if (qPlayer.HasItem(ITEM_ZANGE) == 0 && Sim.ItemZange == 1) {
        if (GameMechanic::PickUpItemResult::PickedUp == GameMechanic::pickUpItem(qPlayer, ITEM_ZANGE)) {
            hprintf("Bot::actionSabotage(): Picked up item pliers");
        }
    }

    if (!qPlayer.RobotUse(ROBOT_USE_EXTREME_SABOTAGE) || mNemesis == -1) {
        return;
    }

    /* decide which saboge. Default: Spiked coffee
     * money/hints already checked in condition */
    SLONG jobType = 1;
    SLONG jobNumber = 1;
    SLONG jobHints = 8;

    /* sabotage planes to damage enemy stock price in stock price competitions */
    bool stockPriceSabotage = (Sim.Difficulty == DIFF_ADDON08 || Sim.Difficulty == DIFF_ATFS07);
    /* sabotage plane tire to delay next start in miles&more mission */
    bool delaySabotage = (Sim.Difficulty == DIFF_ADDON04);
    if (stockPriceSabotage || delaySabotage) {
        std::array<SLONG, 5> hintArray{2, 4, 10, 20, 100};
        jobType = 0;
        jobNumber = std::min((stockPriceSabotage ? 4 : 3), qPlayer.ArabTrust);
        jobHints = hintArray[jobNumber - 1];

        const auto &qNemesisPlanes = Sim.Players.Players[mNemesis].Planes;
        qPlayer.ArabPlaneSelection = qNemesisPlanes.GetRandomUsedIndex(&LocalRandom);
        hprintf("Bot::actionSabotage(): Selecting plane %s", Helper::getPlaneName(qNemesisPlanes[qPlayer.ArabPlaneSelection]).c_str());
    }

    /* check preconditions */
    auto minCost = SabotagePrice[jobNumber - 1];
    if (mArabHintsTracker + jobHints > kMaxSabotageHints) {
        return; /* wait until we won't be caught */
    }
    if (minCost > moneyAvailable) {
        return; /* wait until we have enough money */
    }

    /* exceute sabotage */
    const auto &nemesisName = Sim.Players.Players[mNemesis].AirlineX;
    auto ret = GameMechanic::setSaboteurTarget(qPlayer, mNemesis);
    if (ret != mNemesis) {
        redprintf("Bot::actionSabotage(): Cannot sabotage nemesis %s: Cannot set as target", (LPCTSTR)nemesisName);
        return;
    }

    auto res = GameMechanic::checkPrerequisitesForSaboteurJob(qPlayer, jobType, jobNumber, FALSE).result;
    switch (res) {
    case GameMechanic::CheckSabotageResult::Ok:
        GameMechanic::activateSaboteurJob(qPlayer, FALSE);
        hprintf("Bot::actionSabotage(): Sabotaging nemesis %s (jobType = %d, jobNumber = %d)", (LPCTSTR)nemesisName, jobType, jobNumber);
        mNemesisSabotaged = mNemesis; /* ensures that we do not sabotage him again tomorrow */
        mArabHintsTracker += jobHints;
        break;
    case GameMechanic::CheckSabotageResult::DeniedInvalidParam:
        redprintf("Bot::actionSabotage(): Cannot sabotage nemesis %s: Invalid param", (LPCTSTR)nemesisName);
        break;
    case GameMechanic::CheckSabotageResult::DeniedSaboteurBusy:
        hprintf("Bot::actionSabotage(): Cannot sabotage nemesis %s: Saboteur busy", (LPCTSTR)nemesisName);
        break;
    case GameMechanic::CheckSabotageResult::DeniedSecurity:
        mNeedToShutdownSecurity = true;
        hprintf("Bot::actionSabotage(): Cannot sabotage nemesis %s: Blocked by security", (LPCTSTR)nemesisName);
        break;
    case GameMechanic::CheckSabotageResult::DeniedNotEnoughMoney:
        redprintf("Bot::actionSabotage(): Cannot sabotage nemesis %s: Not enough money", (LPCTSTR)nemesisName);
        break;
    case GameMechanic::CheckSabotageResult::DeniedNoLaptop:
        redprintf("Bot::actionSabotage(): Cannot sabotage nemesis %s: Enemy has no laptop", (LPCTSTR)nemesisName);
        break;
    case GameMechanic::CheckSabotageResult::DeniedTrust:
        redprintf("Bot::actionSabotage(): Cannot sabotage nemesis %s: Not enough trust", (LPCTSTR)nemesisName);
        break;
    default:
        redprintf("Bot::actionSabotage(): Cannot sabotage nemesis %s: Unknown error", (LPCTSTR)nemesisName);
    }
}

__int64 Bot::calcBuyShares(__int64 moneyAvailable, DOUBLE kurs) const { return static_cast<__int64>(std::floor((moneyAvailable - 100) / (1.1 * kurs))); }
__int64 Bot::calcSellShares(__int64 moneyToGet, DOUBLE kurs) const { return static_cast<__int64>(std::floor((moneyToGet + 100) / (0.9 * kurs))); }

__int64 Bot::calcNumOfFreeShares(SLONG playerId) const {
    auto &player = Sim.Players.Players[playerId];
    __int64 amount = player.AnzAktien;
    for (SLONG c = 0; c < 4; c++) {
        amount -= Sim.Players.Players[c].OwnsAktien[playerId];
    }
    return amount;
}

__int64 Bot::calcAmountToBuy(SLONG buyFromPlayerId, SLONG desiredRatio, __int64 moneyAvailable) const {
    auto &player = Sim.Players.Players[buyFromPlayerId];
    __int64 amount = player.AnzAktien;
    __int64 targetAmount = amount * desiredRatio / 100;
    __int64 amountFree = calcNumOfFreeShares(buyFromPlayerId);
    __int64 amountWanted = targetAmount - qPlayer.OwnsAktien[buyFromPlayerId];
    __int64 amountCanAfford = calcBuyShares(moneyAvailable, player.Kurse[0]);
    return std::min({amountFree, amountWanted, amountCanAfford});
}

void Bot::actionEmitShares() {
    __int64 newStock = (qPlayer.MaxAktien - qPlayer.AnzAktien) / 100 * 100;
    hprintf("Bot::actionEmitShares(): Emitting stock: %lld", newStock);
    GameMechanic::emitStock(qPlayer, newStock, kStockEmissionMode);

    /* rebuy some to reach target ratio of own stock */
    if (mRunToFinalObjective < FinalPhase::TargetRun) {
        auto moneyAvailable = getMoneyAvailable() - kMoneyReserveBuyOwnShares;
        auto amount = calcAmountToBuy(qPlayer.PlayerNum, kOwnStockPosessionRatio, moneyAvailable);
        if (amount > 0) {
            hprintf("Bot::actionEmitShares(): Buying own stock: %lld", amount);
            GameMechanic::buyStock(qPlayer, qPlayer.PlayerNum, amount);
        }
    }
}

void Bot::actionBuyNemesisShares(__int64 moneyAvailable) {
    for (SLONG dislike = 0; dislike < 4; dislike++) {
        auto &qTarget = Sim.Players.Players[dislike];
        if (dislike == qPlayer.PlayerNum || qTarget.IsOut != 0) {
            continue;
        }
        auto amount = calcAmountToBuy(dislike, 50, moneyAvailable);
        if (amount > 0) {
            hprintf("Bot::actionBuyNemesisShares(): Buying enemy stock from %s: %lld", (LPCTSTR)qTarget.AirlineX, amount);
            GameMechanic::buyStock(qPlayer, dislike, amount);
            moneyAvailable = getMoneyAvailable() - kMoneyReserveBuyOwnShares;
        }
    }
}

void Bot::actionBuyOwnShares(__int64 moneyAvailable) {
    auto amount = calcAmountToBuy(qPlayer.PlayerNum, kOwnStockPosessionRatio, moneyAvailable);
    if (amount > 0) {
        hprintf("Bot::actionBuyOwnShares(): Buying own stock: %lld", amount);
        GameMechanic::buyStock(qPlayer, qPlayer.PlayerNum, amount);
    }
}

void Bot::actionOvertakeAirline() {
    SLONG airline = -1;
    for (SLONG p = 0; p < 4; p++) {
        auto &qTarget = Sim.Players.Players[p];
        if (p == qPlayer.PlayerNum || qTarget.IsOut != 0) {
            continue;
        }
        if (GameMechanic::OvertakeAirlineResult::Ok != GameMechanic::canOvertakeAirline(qPlayer, p)) {
            continue;
        }
        if (airline == -1 || p == mNemesis) {
            airline = p;
        }
    }
    if (airline == -1) {
        redprintf("Bot::actionOvertakeAirline(): There is no airline we can overtake");
        return;
    }
    hprintf("Bot::actionOvertakeAirline(): Liquidating %s", (LPCTSTR)Sim.Players.Players[airline].AirlineX);
    GameMechanic::overtakeAirline(qPlayer, airline, true);
}

void Bot::actionSellShares(__int64 moneyAvailable) {
    if (qPlayer.RobotUse(ROBOT_USE_MAX20PERCENT)) {
        /* do never own more than 20 % of own stock */
        SLONG c = qPlayer.PlayerNum;
        __int64 sells = (qPlayer.OwnsAktien[c] - qPlayer.AnzAktien * kOwnStockPosessionRatio / 100);
        if (sells > 0) {
            hprintf("Bot::actionSellShares(): Selling own stock to have no more than %d %%: %lld", kOwnStockPosessionRatio, sells);
            GameMechanic::sellStock(qPlayer, c, sells);
            return;
        }
    }

    SLONG pass = 0;
    for (; pass < 10; pass++) {
        __int64 howMuchToRaise = -(moneyAvailable - qPlayer.Credit);
        if (mRunToFinalObjective == FinalPhase::TargetRun) {
            howMuchToRaise = std::max(howMuchToRaise, mMoneyForFinalObjective);
        }
        if (howMuchToRaise <= 0) {
            break;
        }

        auto res = howToGetMoney();
        if (res == HowToGetMoney::SellOwnShares) {
            SLONG c = qPlayer.PlayerNum;
            __int64 sellsNeeded = calcSellShares(howMuchToRaise, qPlayer.Kurse[0]);
            __int64 sellsMax = std::max(0, qPlayer.OwnsAktien[c] - qPlayer.AnzAktien / 2 - 1);
            auto sells = std::min(sellsMax, sellsNeeded);
            if (sells > 0) {
                hprintf("Bot::actionSellShares(): Selling own stock: %lld", sells);
                GameMechanic::sellStock(qPlayer, c, sells);
            }
        } else if (res == HowToGetMoney::SellAllOwnShares) {
            SLONG c = qPlayer.PlayerNum;
            __int64 sellsNeeded = calcSellShares(howMuchToRaise, qPlayer.Kurse[0]);
            __int64 sellsMax = qPlayer.OwnsAktien[c];
            auto sells = std::min(sellsMax, sellsNeeded);
            if (sells > 0) {
                hprintf("Bot::actionSellShares(): Selling all own stock: %lld", sells);
                GameMechanic::sellStock(qPlayer, c, sells);
            }
        } else if (res == HowToGetMoney::SellShares) {
            for (SLONG c = 0; c < Sim.Players.AnzPlayers; c++) {
                if (qPlayer.OwnsAktien[c] == 0 || c == qPlayer.PlayerNum) {
                    continue;
                }

                __int64 sellsNeeded = calcSellShares(howMuchToRaise, Sim.Players.Players[c].Kurse[0]);
                __int64 sellsMax = qPlayer.OwnsAktien[c];
                __int64 sells = std::min(sellsMax, sellsNeeded);
                hprintf("Bot::actionSellShares(): Selling stock from player %d: %lld", c, sells);
                GameMechanic::sellStock(qPlayer, c, sells);
                break;
            }
        } else {
            break;
        }
    }
    if (pass == 0) {
        redprintf("Bot::actionSellShares(): We do not actually need money");
    }
}

void Bot::actionVisitMech() {
    if (qPlayer.MechMode != 3) {
        hprintf("Bot::actionVisitMech(): Setting mech mode to 3");
        GameMechanic::setMechMode(qPlayer, 3);
    }

    const auto &qPlanes = qPlayer.Planes;

    /* save old repair targets */
    std::vector<std::pair<SLONG, UBYTE>> planeList;
    for (SLONG c = 0; c < qPlanes.AnzEntries(); c++) {
        if (qPlanes.IsInAlbum(c) == 0) {
            continue;
        }

        auto &qPlane = qPlanes[c];
        auto oldTarget = qPlane.TargetZustand;

        /* WorstZustand more than 20 lower than repair target means extra cost! */
        auto worstZustand = std::min(qPlane.WorstZustand, qPlane.Zustand);
        auto worstZustandNoCost = std::min(100, worstZustand + 20);
        GameMechanic::setPlaneTargetZustand(qPlayer, c, worstZustandNoCost);

        if (qPlanes[c].TargetZustand == 100) {
            continue; /* this plane won't cost extra */
        }

        planeList.emplace_back(c, oldTarget);
    }

    /* distribute available money for repair extra costs */
    mMoneyReservedForRepairs = 0;
    auto moneyAvailable = getMoneyAvailable() - kMoneyReserveRepairs;
    bool keepGoing = true;
    while (keepGoing && moneyAvailable >= 0) {
        keepGoing = false;
        for (const auto &iter : planeList) {
            auto &qPlane = qPlanes[iter.first];
            auto worstZustand = std::min(qPlane.WorstZustand, qPlane.Zustand);
            SLONG cost = (qPlane.TargetZustand + 1 > (worstZustand + 20)) ? (qPlane.ptPreis / 110) : 0;
            if (qPlane.TargetZustand < kPlaneTargetZustand && moneyAvailable >= cost) {
                GameMechanic::setPlaneTargetZustand(qPlayer, iter.first, qPlane.TargetZustand + 1);
                keepGoing = true;
                mMoneyReservedForRepairs += cost;
                moneyAvailable = getMoneyAvailable() - kMoneyReserveRepairs;
            }
            if (moneyAvailable < 0) {
                break;
            }
        }
    }

    for (const auto &iter : planeList) {
        auto &qPlane = qPlanes[iter.first];
        auto worstZustand = std::min(qPlane.WorstZustand, qPlane.Zustand);
        if (qPlane.TargetZustand > iter.second) {
            hprintf("Bot::actionVisitMech(): Increasing repair target of plane %s: %u => %u (Zustand = %u, WorstZustand = %u)",
                    Helper::getPlaneName(qPlane, 1).c_str(), iter.second, qPlane.TargetZustand, qPlane.Zustand, worstZustand);
        } else if (qPlane.TargetZustand < iter.second) {
            hprintf("Bot::actionVisitMech(): Lowering repair target of plane %s: %u => %u (Zustand = %u, WorstZustand = %u)",
                    Helper::getPlaneName(qPlane, 1).c_str(), iter.second, qPlane.TargetZustand, qPlane.Zustand, worstZustand);
        }
    }
    hprintf("Bot::actionVisitMech(): We are reserving %s $ for repairs, available money: %s $", (LPCTSTR)Insert1000erDots64(mMoneyReservedForRepairs),
            (LPCTSTR)Insert1000erDots64(getMoneyAvailable()));
}

void Bot::actionVisitDutyFree(__int64 moneyAvailable) {
    if (mItemAntiStrike == 1) {
        if (GameMechanic::useItem(qPlayer, ITEM_BH)) {
            hprintf("Bot::actionVisitDutyFree(): Used item BH");
            mItemAntiStrike = 2;
        }
    }
    if (mItemAntiStrike == 2) {
        if (GameMechanic::PickUpItemResult::PickedUp == GameMechanic::pickUpItem(qPlayer, ITEM_HUFEISEN)) {
            hprintf("Bot::actionVisitDutyFree(): Picked up item horse shoe");
            mItemAntiStrike = 3;
        }
    }

    if (mItemArabTrust == 0 && Sim.Date > 0 && qPlayer.ArabTrust == 0) {
        if (GameMechanic::BuyItemResult::Ok == GameMechanic::buyDutyFreeItem(qPlayer, ITEM_MG)) {
            mItemArabTrust = 1;
        }
    }

    __int64 money = qPlayer.Money;
    if (qPlayer.LaptopQuality < 4) {
        auto quali = qPlayer.LaptopQuality;
        GameMechanic::buyDutyFreeItem(qPlayer, ITEM_LAPTOP);
        hprintf("Bot::actionVisitDutyFree(): Buying laptop (%d => %d)", quali, qPlayer.LaptopQuality);
    }
    __int64 moneySpent = std::max(0LL, (money - qPlayer.Money));
    moneyAvailable -= moneySpent;

    if (moneyAvailable > 0 && !qPlayer.HasItem(ITEM_HANDY)) {
        hprintf("Bot::actionVisitDutyFree(): Buying cell phone");
        GameMechanic::buyDutyFreeItem(qPlayer, ITEM_HANDY);
    }
}

void Bot::actionVisitBoss() {
    if (mItemPills == 0 && Sim.ItemPostcard == 1) {
        if (GameMechanic::PickUpItemResult::PickedUp == GameMechanic::pickUpItem(qPlayer, ITEM_POSTKARTE)) {
            hprintf("Bot::actionVisitBoss(): Picked up item card");
            mItemPills = 1;
        }
    }

    /* what is available? how much money are we currently bidding in total? */
    mMoneyReservedForAuctions = 0;
    mBossGateAvailable = false;
    for (SLONG c = 0; c < 7; c++) {
        const auto &qZettel = TafelData.Gate[c];
        if (qZettel.ZettelId < 0) {
            continue;
        }
        mBossGateAvailable = true;
        if (qZettel.Player == qPlayer.PlayerNum) {
            mMoneyReservedForAuctions += qZettel.Preis;
        }
    }
    mBossNumCitiesAvailable = 0;
    for (SLONG c = 0; c < 7; c++) {
        const auto &qZettel = TafelData.City[c];
        if (qZettel.ZettelId < 0) {
            continue;
        }
        mBossNumCitiesAvailable++;
        if (qZettel.Player == qPlayer.PlayerNum) {
            mMoneyReservedForAuctions += qZettel.Preis;
        }
    }

    auto moneyAvailable = getMoneyAvailable() - kMoneyReserveBossOffice;

    /* auction for gates */
    for (SLONG c = 0; c < 7; c++) {
        const auto &qZettel = TafelData.Gate[c];
        if (qZettel.ZettelId < 0) {
            continue;
        }
        if (qZettel.Player == qPlayer.PlayerNum) {
            continue;
        }
        if (qZettel.Preis > moneyAvailable) {
            continue;
        }

        GameMechanic::bidOnGate(qPlayer, c);
        mMoneyReservedForAuctions += qZettel.Preis;
        moneyAvailable = getMoneyAvailable() - kMoneyReserveBossOffice;
        hprintf("Bot::actionVisitBoss(): Bidding on gate: %d $", qZettel.Preis);
    }

    /* auction for subsidiaries */
    mBossNumCitiesAvailable = 0;
    for (SLONG c = 0; c < 7; c++) {
        const auto &qZettel = TafelData.City[c];
        if (qZettel.ZettelId < 0) {
            continue;
        }
        mBossNumCitiesAvailable++;
        if (qZettel.Player == qPlayer.PlayerNum) {
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
            GameMechanic::bidOnCity(qPlayer, c);
            mMoneyReservedForAuctions += qZettel.Preis;
            moneyAvailable = getMoneyAvailable() - kMoneyReserveBossOffice;
            hprintf("Bot::actionVisitBoss(): Bidding on city %s: %d $", (LPCTSTR)Cities[qZettel.ZettelId].Name, qZettel.Preis);
        }
    }
}

void Bot::actionVisitRouteBox() {
    updateRouteInfoBoard();
    assignPlanesToRoutes(false);
    findBestRoute();
}

void Bot::actionRentRoute() {
    if (!mDoRoutes) {
        /* kill routes rented at beginning of game */
        const auto &qRRouten = qPlayer.RentRouten.RentRouten;
        for (SLONG routeID = 0; routeID < qRRouten.AnzEntries(); routeID++) {
            if (qRRouten[routeID].Rang != 0) {
                GameMechanic::killRoute(qPlayer, routeID);
                hprintf("Bot::RobotInit(): Removing initial route %s", Helper::getRouteName(Routen[routeID]).c_str());
            }
        }
        return;
    }

    auto routeA = mWantToRentRouteId;
    mWantToRentRouteId = -1;
    assert(routeA != -1);

    /* find route in reverse direction */
    SLONG routeB = -1;
    for (SLONG c = 0; c < Routen.AnzEntries(); c++) {
        if ((Routen.IsInAlbum(c) != 0) && Routen[c].VonCity == Routen[routeA].NachCity && Routen[c].NachCity == Routen[routeA].VonCity) {
            routeB = c;
            break;
        }
    }
    if (-1 == routeB) {
        redprintf("Bot::actionRentRoute: Unable to find route in reverse direction.");
        return;
    }

    /* rent route */
    if (!GameMechanic::rentRoute(qPlayer, routeA)) {
        redprintf("Bot::actionRentRoute: Failed to rent route.");
        return;
    }
    mRoutes.emplace_back(routeA, routeB, mPlaneTypeForNewRoute);
    mRoutes.back().ticketCostFactor = kDefaultTicketPriceFactor;
    hprintf("Bot::actionRentRoute(): Renting route %s (using plane type %s): ", Helper::getRouteName(getRoute(mRoutes.back())).c_str(),
            (LPCTSTR)PlaneTypes[mPlaneTypeForNewRoute].Name);
    mPlaneTypeForNewRoute = -1;

    /* update sorted list */
    assert(mRoutes.size() > 0);
    mRoutesSortedByOwnUtilization.resize(mRoutes.size());
    for (SLONG i = mRoutesSortedByOwnUtilization.size() - 1; i >= 1; i--) {
        mRoutesSortedByOwnUtilization[i] = mRoutesSortedByOwnUtilization[i - 1];
    }
    mRoutesSortedByOwnUtilization[0] = mRoutes.size() - 1;

    /* use existing planes */
    for (auto id : mPlanesForNewRoute) {
        const auto &qPlane = qPlayer.Planes[id];
        hprintf("Bot::actionRentRoute(): Using existing plane: %s", Helper::getPlaneName(qPlane).c_str());

        mRoutes.back().planeIds.push_back(id);
        mPlanesForRoutes.push_back(id);
        bool erased = eraseFirst(mPlanesForRoutesUnassigned, id);
        if (!erased) {
            redprintf("Bot::actionRentRoute(): Plane with ID = %d should have been in unassigned list", id);
        }
    }
    mPlanesForNewRoute.clear();

    mRoutesNextStep = RoutesNextStep::None;
    updateRouteInfoBoard();

    requestPlanRoutes(false);
}

void Bot::actionBuyAdsForRoutes(__int64 moneyAvailable) {
    mCurrentImage = qPlayer.Image;
    hprintf("Bot::actionBuyAdsForRoutes(): Checked company image: %d", mCurrentImage);

    if (mRoutesNextStep != RoutesNextStep::BuyAdsForRoute) {
        orangeprintf("Bot::actionBuyAdsForRoutes(): Conditions not met anymore.");
        return;
    }

    assert(mImproveRouteId != -1);
    auto &qRoute = mRoutes[mImproveRouteId];

    const SLONG largestAdCampaign = 5;

    SLONG cost = 0;
    SLONG adCampaignSize = kSmallestAdCampaign;
    for (; adCampaignSize <= largestAdCampaign; adCampaignSize++) {
        cost = gWerbePrice[1 * 6 + adCampaignSize];
        SLONG imageDelta = (cost / 30000);
        if (cost > moneyAvailable) {
            adCampaignSize -= 1;
            break;
        }
        if (getRentRoute(qRoute).Image + imageDelta > 100) {
            break;
        }
    }
    if (adCampaignSize < kSmallestAdCampaign) {
        return; /* not enough money */
    }
    adCampaignSize = std::min(adCampaignSize, largestAdCampaign);

    SLONG oldImage = getRentRoute(qRoute).Image;
    GameMechanic::buyAdvertisement(qPlayer, 1, adCampaignSize, qRoute.routeId);
    SLONG newImage = getRentRoute(qRoute).Image;
    hprintf("Bot::actionBuyAdsForRoutes(): Buying advertisement for route %s for %d $ (image improved %d => %d)",
            Helper::getRouteName(getRoute(qRoute)).c_str(), cost, oldImage, newImage);

    qRoute.image = newImage;

    mRoutesNextStep = RoutesNextStep::None;
}

void Bot::actionBuyAds(__int64 moneyAvailable) {
    mCurrentImage = qPlayer.Image;
    hprintf("Bot::actionBuyAds(): Checked company image: %d", mCurrentImage);

    assert(kSmallestAdCampaign >= 1);
    for (SLONG adCampaignSize = 5; adCampaignSize >= kSmallestAdCampaign; adCampaignSize--) {
        SLONG cost = gWerbePrice[0 * 6 + adCampaignSize];
        SLONG cost2 = gWerbePrice[0 * 6 + (adCampaignSize - 1)];
        SLONG imageDelta2 = cost2 / 10000 * ((adCampaignSize - 1) + 6) / 55;

        while (moneyAvailable > cost && qPlayer.Image < 1000 && (qPlayer.Image + imageDelta2 < 1000)) {
            SLONG oldImage = qPlayer.Image;
            hprintf("Bot::actionBuyAds(): Buying advertisement for airline for %d $", cost);
            GameMechanic::buyAdvertisement(qPlayer, 0, adCampaignSize);
            moneyAvailable = getMoneyAvailable();

            hprintf("Bot::actionBuyAds(): Airline image improved (%d => %d)", oldImage, qPlayer.Image);
            if (!qPlayer.RobotUse(ROBOT_USE_WERBUNG)) {
                return;
            }
        }
    }

    if (mRoutesNextStep == RoutesNextStep::ImproveAirlineImage) {
        mRoutesNextStep = RoutesNextStep::None;
    }
}
