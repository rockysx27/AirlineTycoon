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

struct RouteScore {
    DOUBLE score{};
    SLONG routeId{-1};
    SLONG planeTypeId{-1};
    SLONG planeId{-1};
    SLONG numPlanes{-1};
};

template <typename T> inline void eraseFirst(T &l, int val) {
    auto it = l.begin();
    while (it != l.end() && *it != val) {
        it++;
    }
    if (it != l.end()) {
        std::iter_swap(it, l.end() - 1);
        l.resize(l.size() - 1);
    }
}

void Bot::actionStartDay(__int64 moneyAvailable) {
    actionStartDayLaptop(moneyAvailable);

    /* always use tanks: We get discount from advisor and by using cheap kerosine */
    GameMechanic::setKerosinTankOpen(qPlayer, true);
}

void Bot::actionStartDayLaptop(__int64 moneyAvailable) {
    mDayStarted = true;

    /* check lists of planes, check which planes are available for service and which are not */
    checkPlaneLists();

    /* check routes */
    if (mDoRoutes) {
        checkLostRoutes();
        updateRouteInfo();
        requestPlanRoutes(true);
    } else if (qPlayer.RobotUse(ROBOT_USE_ROUTES)) {
        /* logic for switching to routes */
        if (qPlayer.RobotUse(ROBOT_USE_FORCEROUTES)) {
            mDoRoutes = true;
            hprintf("Bot::actionStartDay(): Switching to routes (forced).");
        } else if (mBestPlaneTypeId != -1) {
            const auto &bestPlaneType = PlaneTypes[mBestPlaneTypeId];
            SLONG costRouteAd = gWerbePrice[1 * 6 + 5];
            __int64 moneyNeeded = 2 * costRouteAd + bestPlaneType.Preis;
            SLONG numPlanes = mPlanesForJobs.size() + mPlanesForJobsUnassigned.size();
            if ((numPlanes >= kSwitchToRoutesNumPlanesMin && moneyAvailable >= moneyNeeded) || numPlanes >= kSwitchToRoutesNumPlanesMax) {
                mDoRoutes = true;
                hprintf("Bot::actionStartDay(): Switching to routes. Reserving 2*%ld + %ld for ads and plane.", costRouteAd, bestPlaneType.Preis);
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

    /* some conditions might have changed (plane availability) */
    forceReplanning();
}

void Bot::actionBuero() {
    if (mNeedToPlanRoutes) {
        planRoutes();
    }

    if (mNeedToPlanJobs) {
        planFlights();
    }
}

void Bot::actionCallInternational(bool areWeInOffice) {
    std::vector<int> cities;
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

    bool onlySecurity = (Sim.Difficulty == DIFF_ATFS02);

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
                redprintf("Bot.cpp: Default case should not be reached.");
                DebugBreak();
            }
        }
    }
    hprintf("Bot::actionUpgradePlanes(): We are reserving %s $ for plane upgrades, available money: %s $",
            (LPCTSTR)Insert1000erDots64(mMoneyReservedForUpgrades), (LPCTSTR)Insert1000erDots64(getMoneyAvailable()));
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
        return;
    }
    for (auto i : GameMechanic::buyPlane(qPlayer, bestPlaneTypeId, 1)) {
        assert(i >= 0x1000000);
        hprintf("Bot::actionBuyNewPlane(): Bought plane %s", Helper::getPlaneName(qPlayer.Planes[i]).c_str());
        if (mDoRoutes) {
            mPlanesForRoutesUnassigned.push_back(i);
            requestPlanRoutes(false);
        } else {
            if (checkPlaneAvailable(i, true)) {
                mPlanesForJobs.push_back(i);
                grabNewFlights();
            } else {
                mPlanesForJobsUnassigned.push_back(i);
            }
        }
    }
}

void Bot::actionBuyUsedPlane(__int64 /*moneyAvailable*/) {
    if (mBestUsedPlaneIdx < 0) {
        redprintf("Bot::actionBuyUsedPlane(): We have not yet checked which plane to buy!");
        return;
    }

    if (qPlayer.xPiloten < Sim.UsedPlanes[mBestUsedPlaneIdx].ptAnzPiloten || qPlayer.xBegleiter < Sim.UsedPlanes[mBestUsedPlaneIdx].ptAnzBegleiter) {
        redprintf("Bot::actionBuyUsedPlane(): Not enough crew for selected plane!");
        return;
    }
    SLONG planeId = GameMechanic::buyUsedPlane(qPlayer, mBestUsedPlaneIdx);
    assert(planeId >= 0x1000000);

    const auto &qPlane = qPlayer.Planes[planeId];
    hprintf("Bot::actionBuyUsedPlane(): Bought used plane %s", Helper::getPlaneName(qPlane).c_str());
    hprintf("Bot::actionBuyUsedPlane(): Passengers = %ld, fuel = %ld, year = %d", qPlane.ptPassagiere, qPlane.ptVerbrauch, qPlane.Baujahr);
    SLONG improvementNeeded = std::max(0, 80 - qPlane.WorstZustand);
    SLONG repairCost = improvementNeeded * (qPlane.ptPreis / 110);
    hprintf("Bot::actionBuyUsedPlane(): WorstZustand = %u, cost = %d", qPlane.WorstZustand, repairCost);

    /* we always repair used planes first */
    mPlanesForJobsUnassigned.push_back(planeId);
}

void Bot::actionBuyDesignerPlane(__int64 /*moneyAvailable*/) {
    if (mDesignerPlane.Name.empty() || !mDesignerPlane.IsBuildable()) {
        redprintf("Bot::actionBuyDesignerPlane(): No designer plane!");
        return;
    }

    if (qPlayer.xPiloten < mDesignerPlane.CalcPiloten() || qPlayer.xBegleiter < mDesignerPlane.CalcBegleiter()) {
        redprintf("Bot::actionBuyDesignerPlane(): Not enough crew for selected plane!");
        return;
    }
    for (auto i : GameMechanic::buyXPlane(qPlayer, mDesignerPlaneFile, 1)) {
        assert(i >= 0x1000000);
        hprintf("Bot::actionBuyDesignerPlane(): Bought plane %s", Helper::getPlaneName(qPlayer.Planes[i]).c_str());
        if (mDoRoutes) {
            mPlanesForRoutesUnassigned.push_back(i);
            requestPlanRoutes(false);
        } else {
            if (checkPlaneAvailable(i, true)) {
                mPlanesForJobs.push_back(i);
                grabNewFlights();
            } else {
                mPlanesForJobsUnassigned.push_back(i);
            }
        }
    }
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
    std::vector<int> wantedAdvisors = {BERATERTYP_FITNESS, BERATERTYP_SICHERHEIT, BERATERTYP_KEROSIN, BERATERTYP_GELD, BERATERTYP_FLUGZEUG};
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
            hprintf("Bot::actionVisitHR(): Upgrading advisor %ld to skill level %ld", advisorType, bestCandidateSkill);
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
        hprintf("Bot::actionVisitHR(): Hiring %ld pilots and %ld attendants", numPilotsHired, numStewardessHired);
    }

    hprintf("Bot::actionVisitHR(): We have %ld extra pilots and %ld extra attendants", qPlayer.xPiloten, qPlayer.xBegleiter);

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
        hprintf("Bot::actionStartDay(): We lost %ld employees", mNumEmployees - nWorkers);
    }
    mNumEmployees = nWorkers;
    if (salaryIncreases > 0) {
        hprintf("Bot::actionStartDay(): Increases salary of %ld employees", salaryIncreases);
    }
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
        moneyAvailable = getMoneyAvailable();
        mKerosineLevelLastChecked = qPlayer.TankInhalt;

        hprintf("Bot::actionBuyKerosine(): Kerosine quantity: %ld => %ld", amountOld, qPlayer.TankInhalt);
        hprintf("Bot::actionBuyKerosine(): Kerosine quality: %.2f => %.2f", qualiOld, qPlayer.KerosinQuali);
    }
}

void Bot::actionBuyKerosineTank(__int64 moneyAvailable) {
    auto nTankTypes = sizeof(TankSize) / sizeof(TankSize[0]);
    for (SLONG i = nTankTypes - 1; i >= 1; i--) // avoid cheapest tank (not economical)
    {
        if (moneyAvailable >= TankPrice[i]) {
            SLONG amount = std::min(3LL, moneyAvailable / TankPrice[i]);
            hprintf("Bot::actionBuyKerosineTank(): Buying %ld times tank type %ld", amount, i);
            GameMechanic::buyKerosinTank(qPlayer, i, amount);
            moneyAvailable = getMoneyAvailable();
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
    if (qPlayer.HasItem(ITEM_ZANGE) == 0) {
        if (GameMechanic::PickUpItemResult::PickedUp == GameMechanic::pickUpItem(qPlayer, ITEM_ZANGE)) {
            hprintf("Bot::actionSabotage(): Picked up item pliers");
        }
    }

    if (!qPlayer.RobotUse(ROBOT_USE_EXTREME_SABOTAGE) || mNemesis == -1) {
        return;
    }

    /* decide which saboge. Default: Spiked coffee */
    SLONG jobType = 1;
    SLONG jobNumber = 1;
    if (Sim.Difficulty == DIFF_ADDON08 || Sim.Difficulty == DIFF_ATFS07) {
        /* sabotage planes to damage enemy stock price in stock price competitions */
        jobType = 0;
        std::array<SLONG, 5> hintArray{2, 4, 10, 20, 100};
        for (jobNumber = 4; jobNumber >= 1; jobNumber--) {
            auto minCost = SabotagePrice[jobNumber - 1];
            SLONG hints = hintArray[jobNumber - 1];
            if ((jobNumber <= qPlayer.ArabTrust) && (qPlayer.ArabHints + hints <= kMaxSabotageHints) && (minCost <= moneyAvailable)) {
                break;
            }
        }
        if (jobNumber < 1) {
            return;
        }

        qPlayer.ArabPlaneSelection = Sim.Players.Players[mNemesis].Planes.GetRandomUsedIndex(&LocalRandom);
    }

    /* exceute sabotage */
    GameMechanic::setSaboteurTarget(qPlayer, mNemesis);
    auto res = GameMechanic::checkPrerequisitesForSaboteurJob(qPlayer, jobType, jobNumber, FALSE).result;
    switch (res) {
    case GameMechanic::CheckSabotageResult::Ok:
        GameMechanic::activateSaboteurJob(qPlayer, FALSE);
        hprintf("Bot::actionSabotage(): Sabotaging nemesis %s (jobType = %ld, jobNumber = %ld)", (LPCTSTR)Sim.Players.Players[mNemesis].AirlineX, jobType,
                jobNumber);
        mNemesisSabotaged = mNemesis; /* ensures that we do not sabotage him again tomorrow */
        break;
    case GameMechanic::CheckSabotageResult::DeniedInvalidParam:
        redprintf("Bot::actionSabotage(): Cannot sabotage nemesis %s: Invalid param", (LPCTSTR)Sim.Players.Players[mNemesis].AirlineX);
        break;
    case GameMechanic::CheckSabotageResult::DeniedSaboteurBusy:
        hprintf("Bot::actionSabotage(): Cannot sabotage nemesis %s: Saboteur busy", (LPCTSTR)Sim.Players.Players[mNemesis].AirlineX);
        break;
    case GameMechanic::CheckSabotageResult::DeniedSecurity:
        mNeedToShutdownSecurity = true;
        hprintf("Bot::actionSabotage(): Cannot sabotage nemesis %s: Blocked by security", (LPCTSTR)Sim.Players.Players[mNemesis].AirlineX);
        break;
    case GameMechanic::CheckSabotageResult::DeniedNotEnoughMoney:
        redprintf("Bot::actionSabotage(): Cannot sabotage nemesis %s: Not enough money", (LPCTSTR)Sim.Players.Players[mNemesis].AirlineX);
        break;
    case GameMechanic::CheckSabotageResult::DeniedNoLaptop:
        redprintf("Bot::actionSabotage(): Cannot sabotage nemesis %s: Enemy has no laptop", (LPCTSTR)Sim.Players.Players[mNemesis].AirlineX);
        break;
    case GameMechanic::CheckSabotageResult::DeniedTrust:
        redprintf("Bot::actionSabotage(): Cannot sabotage nemesis %s: Not enough trust", (LPCTSTR)Sim.Players.Players[mNemesis].AirlineX);
        break;
    default:
        redprintf("Bot::actionSabotage(): Cannot sabotage nemesis %s: Unknown error", (LPCTSTR)Sim.Players.Players[mNemesis].AirlineX);
    }
}

SLONG Bot::calcBuyShares(__int64 moneyAvailable, DOUBLE kurs) const { return static_cast<SLONG>(std::floor((moneyAvailable - 100) / (1.1 * kurs))); }
SLONG Bot::calcSellShares(__int64 moneyToGet, DOUBLE kurs) const { return static_cast<SLONG>(std::floor((moneyToGet + 100) / (0.9 * kurs))); }

SLONG Bot::calcNumOfFreeShares(SLONG playerId) const {
    auto &player = Sim.Players.Players[playerId];
    SLONG amount = player.AnzAktien;
    for (SLONG c = 0; c < 4; c++) {
        amount -= Sim.Players.Players[c].OwnsAktien[playerId];
    }
    return amount;
}

SLONG Bot::calcAmountToBuy(SLONG buyFromPlayerId, SLONG desiredRatio, SLONG moneyAvailable) const {
    auto &player = Sim.Players.Players[buyFromPlayerId];
    SLONG targetAmount = player.AnzAktien * desiredRatio / 100;
    SLONG amountFree = calcNumOfFreeShares(buyFromPlayerId);
    SLONG amountWanted = targetAmount - qPlayer.OwnsAktien[buyFromPlayerId];
    SLONG amountCanAfford = calcBuyShares(moneyAvailable, player.Kurse[0]);
    return std::min({amountFree, amountWanted, amountCanAfford});
}

void Bot::actionEmitShares() {
    SLONG newStock = (qPlayer.MaxAktien - qPlayer.AnzAktien) / 100 * 100;
    hprintf("Bot::actionEmitShares(): Emitting stock: %ld", newStock);
    GameMechanic::emitStock(qPlayer, newStock, kStockEmissionMode);
    auto moneyAvailable = getMoneyAvailable();

    // Direkt wieder auf ein Viertel aufkaufen
    auto amount = calcAmountToBuy(qPlayer.PlayerNum, kOwnStockPosessionRatio, moneyAvailable);
    if (amount > 0) {
        hprintf("Bot::actionEmitShares(): Buying own stock: %ld", amount);
        GameMechanic::buyStock(qPlayer, qPlayer.PlayerNum, amount);
        moneyAvailable = getMoneyAvailable();
    }
}

void Bot::actionBuyNemesisShares(__int64 moneyAvailable) {
    auto amount = calcAmountToBuy(mNemesis, 50, moneyAvailable);
    if (amount > 0) {
        hprintf("Bot::actionBuyShares(): Buying nemesis stock: %ld", amount);
        GameMechanic::buyStock(qPlayer, mNemesis, amount);
        moneyAvailable = getMoneyAvailable();
    }
}

void Bot::actionBuyOwnShares(__int64 moneyAvailable) {
    auto amount = calcAmountToBuy(qPlayer.PlayerNum, kOwnStockPosessionRatio, moneyAvailable);
    if (amount > 0) {
        hprintf("Bot::actionBuyShares(): Buying own stock: %ld", amount);
        GameMechanic::buyStock(qPlayer, qPlayer.PlayerNum, amount);
        moneyAvailable = getMoneyAvailable();
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
        redprintf("Bot::actionOvertakeAirline():  There is no airline we can overtake");
        return;
    }
    hprintf("Bot::actionOvertakeAirline():  Liquidating %s", (LPCTSTR)Sim.Players.Players[airline].AirlineX);
    GameMechanic::overtakeAirline(qPlayer, airline, true);
}

void Bot::actionSellShares(__int64 moneyAvailable) {
    if (qPlayer.RobotUse(ROBOT_USE_MAX20PERCENT)) {
        /* do never own more than 20 % of own stock */
        SLONG c = qPlayer.PlayerNum;
        SLONG sells = (qPlayer.OwnsAktien[c] - qPlayer.AnzAktien * kOwnStockPosessionRatio / 100);
        if (sells > 0) {
            hprintf("Bot::actionSellShares(): Selling own stock to have no more than %d %%: %ld", kOwnStockPosessionRatio, sells);
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
            SLONG sellsNeeded = calcSellShares(howMuchToRaise, qPlayer.Kurse[0]);
            SLONG sellsMax = std::max(0, qPlayer.OwnsAktien[c] - qPlayer.AnzAktien / 2 - 1);
            SLONG sells = std::min(sellsMax, sellsNeeded);
            if (sells > 0) {
                hprintf("Bot::actionSellShares(): Selling own stock: %ld", sells);
                GameMechanic::sellStock(qPlayer, c, sells);
            }
        } else if (res == HowToGetMoney::SellAllOwnShares) {
            SLONG c = qPlayer.PlayerNum;
            SLONG sellsNeeded = calcSellShares(howMuchToRaise, qPlayer.Kurse[0]);
            SLONG sells = std::min(qPlayer.OwnsAktien[c], sellsNeeded);
            if (sells > 0) {
                hprintf("Bot::actionSellShares(): Selling all own stock: %ld", sells);
                GameMechanic::sellStock(qPlayer, c, sells);
            }
        } else if (res == HowToGetMoney::SellShares) {
            for (SLONG c = 0; c < Sim.Players.AnzPlayers; c++) {
                if (qPlayer.OwnsAktien[c] == 0 || c == qPlayer.PlayerNum) {
                    continue;
                }

                SLONG sellsNeeded = calcSellShares(howMuchToRaise, Sim.Players.Players[c].Kurse[0]);
                SLONG sells = std::min(qPlayer.OwnsAktien[c], sellsNeeded);
                hprintf("Bot::actionSellShares(): Selling stock from player %ld: %ld", c, sells);
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
                moneyAvailable = getMoneyAvailable();
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
            hprintf("Bot::actionVisitMech(): Increasing repair target of plane %s: %ld => %ld (Zustand = %u, WorstZustand = %u)",
                    Helper::getPlaneName(qPlane, 1).c_str(), iter.second, qPlane.TargetZustand, qPlane.Zustand, worstZustand);
        } else if (qPlane.TargetZustand < iter.second) {
            hprintf("Bot::actionVisitMech(): Lowering repair target of plane %s: %ld => %ld (Zustand = %u, WorstZustand = %u)",
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
        hprintf("Bot::actionVisitDutyFree(): Buying laptop (%ld => %ld)", quali, qPlayer.LaptopQuality);
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
        hprintf("Bot::actionVisitBoss(): Bidding on gate: %ld $", qZettel.Preis);
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
            hprintf("Bot::actionVisitBoss(): Bidding on city %s: %ld $", (LPCTSTR)Cities[qZettel.ZettelId].Name, qZettel.Preis);
        }
    }
}

void Bot::actionFindBestRoute() {
    auto isBuyable = GameMechanic::getBuyableRoutes(qPlayer);
    auto bestPlanes = findBestAvailablePlaneType(true); // TODO: Technically not possible to check plane types here

    mWantToRentRouteId = -1;
    mBuyPlaneForRouteId = -1;
    mUsePlaneForRouteId = -1;

    /* use existing plane for mission where we need to use routes immediately */
    std::vector<std::pair<SLONG, __int64>> existingPlaneIds;
    if (qPlayer.RobotUse(ROBOT_USE_FORCEROUTES)) {
        for (const auto id : mPlanesForJobsUnassigned) {
            auto &qPlane = qPlayer.Planes[id];
            __int64 score = qPlane.ptPassagiere * qPlane.ptPassagiere / qPlane.ptVerbrauch;
            existingPlaneIds.emplace_back(id, score);
        }
        for (const auto id : mPlanesForJobs) {
            auto &qPlane = qPlayer.Planes[id];
            __int64 score = qPlane.ptPassagiere * qPlane.ptPassagiere / qPlane.ptVerbrauch;
            existingPlaneIds.emplace_back(id, score);
        }
        std::sort(existingPlaneIds.begin(), existingPlaneIds.end(),
                  [](const std::pair<SLONG, __int64> &a, const std::pair<SLONG, __int64> &b) { return a.second > b.second; });
    }

    std::vector<RouteScore> bestRoutes;
    for (SLONG c = 0; c < Routen.AnzEntries(); c++) {
        if (isBuyable[c] == 0) {
            continue;
        }

        SLONG distance = Cities.CalcDistance(Routen[c].VonCity, Routen[c].NachCity);

        SLONG planeTypeId = -1;
        SLONG useExistingPlaneId = -1;
        for (SLONG i : bestPlanes) {
            SLONG duration = Cities.CalcFlugdauer(Routen[c].VonCity, Routen[c].NachCity, PlaneTypes[i].Geschwindigkeit);
            if (distance <= PlaneTypes[i].Reichweite * 1000 && duration < 24) {
                planeTypeId = i;
                break;
            }
        }

        /* also check existing planes if they can be used for routes */
        for (const auto &i : existingPlaneIds) {
            auto &qPlane = qPlayer.Planes[i.first];
            SLONG duration = Cities.CalcFlugdauer(Routen[c].VonCity, Routen[c].NachCity, qPlane.ptGeschwindigkeit);
            if (distance <= qPlane.ptReichweite * 1000 && duration < 24) {
                planeTypeId = qPlane.TypeId;
                useExistingPlaneId = i.first;
                break;
            }
        }

        if (planeTypeId == -1) {
            continue;
        }

        /* calc score for route (more passengers always good, longer routes tend to be also more worth it) */
        DOUBLE score = Routen[c].AnzPassagiere();
        score *= (Cities.CalcDistance(Routen[c].VonCity, Routen[c].NachCity) / 1000);
        score /= Routen[c].Miete;

        /* calculate how many planes would be need to get 10% route utilization */
        /* TODO: What about factor 4.27 */
        SLONG roundTripDuration = getRouteTurnAroundDuration(Routen[c], planeTypeId);
        SLONG numTripsPerWeek = 24 * 7 / roundTripDuration;
        SLONG passengersPerWeek = ceil_div(7 * Routen[c].AnzPassagiere(), 10);
        SLONG numPlanesRequired = ceil_div(passengersPerWeek, numTripsPerWeek * PlaneTypes[planeTypeId].Passagiere);

        // Ist die Route für die Mission wichtig?
        if (qPlayer.RobotUse(ROBOT_USE_ROUTEMISSION)) {
            for (SLONG d = 0; d < 6; d++) {
                if ((Routen[c].VonCity == static_cast<ULONG>(Sim.HomeAirportId) && Routen[c].NachCity == static_cast<ULONG>(Sim.MissionCities[d])) ||
                    (Routen[c].NachCity == static_cast<ULONG>(Sim.HomeAirportId) && Routen[c].VonCity == static_cast<ULONG>(Sim.MissionCities[d]))) {

                    hprintf("Bot::actionFindBestRoute(): Route %s is important for mission, increasing score.", Helper::getRouteName(Routen[c]).c_str());
                    score *= 10;
                }
            }
        }

        if (useExistingPlaneId != -1) {
            numPlanesRequired--;
        }

        bestRoutes.emplace_back(RouteScore{score, c, planeTypeId, useExistingPlaneId, numPlanesRequired});
    }

    /* sort routes by score */
    std::sort(bestRoutes.begin(), bestRoutes.end(), [](const RouteScore &a, const RouteScore &b) { return a.score > b.score; });

    for (const auto &i : bestRoutes) {
        if (i.planeId != -1) {
            const auto &qPlane = qPlayer.Planes[i.planeId];
            hprintf("Bot::actionFindBestRoute(): Score of route %s (using existing plane %s, need %ld) is: %.2f",
                    Helper::getRouteName(Routen[i.routeId]).c_str(), Helper::getPlaneName(qPlane).c_str(), i.numPlanes, i.score);
        } else {
            hprintf("Bot::actionFindBestRoute(): Score of route %s (using plane type %s, need %ld) is: %.2f", Helper::getRouteName(Routen[i.routeId]).c_str(),
                    (LPCTSTR)PlaneTypes[i.planeTypeId].Name, i.numPlanes, i.score);
        }
    }

    /* pick best route we can afford */
    __int64 moneyAvailable = qPlayer.Money + getWeeklyOpSaldo();
    for (const auto &candidate : bestRoutes) {
        __int64 planeCost = PlaneTypes[candidate.planeTypeId].Preis;
        if (candidate.numPlanes * planeCost > moneyAvailable) {
            hprintf("Bot::actionFindBestRoute(): We cannot afford route %s (plane costs %ld, need %ld), our available money is %lld",
                    Helper::getRouteName(Routen[candidate.routeId]).c_str(), planeCost, candidate.numPlanes, moneyAvailable);
            continue;
        }
        hprintf("Bot::actionFindBestRoute(): Best route (using plane type %s) is: ", (LPCTSTR)PlaneTypes[candidate.planeTypeId].Name);
        Helper::printRoute(Routen[candidate.routeId]);

        mWantToRentRouteId = candidate.routeId;
        if (candidate.planeId == -1) {
            mBuyPlaneForRouteId = candidate.planeTypeId; /* buy new plane */
        } else {
            mUsePlaneForRouteId = candidate.planeId; /* use existing plane */
        }
        return;
    }

    hprintf("Bot::actionFindBestRoute(): No routes match criteria.");
}

void Bot::actionRentRoute() {
    auto routeA = mWantToRentRouteId;
    assert(routeA != -1);
    assert(mBuyPlaneForRouteId != -1 || mUsePlaneForRouteId != -1);

    mWantToRentRouteId = -1;

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

    if (!GameMechanic::rentRoute(qPlayer, routeA)) {
        redprintf("Bot::actionRentRoute: Failed to rent route.");
        return;
    }

    if (mBuyPlaneForRouteId != -1) {
        mRoutes.emplace_back(routeA, routeB, mBuyPlaneForRouteId);
        hprintf("Bot::actionRentRoute(): Renting route %s (using plane type %s, need to buy): ", Helper::getRouteName(getRoute(mRoutes.back())).c_str(),
                (LPCTSTR)PlaneTypes[mBuyPlaneForRouteId].Name);
    } else {
        assert(qPlayer.Planes.IsInAlbum(mUsePlaneForRouteId));
        const auto &qPlane = qPlayer.Planes[mUsePlaneForRouteId];
        mRoutes.emplace_back(routeA, routeB, qPlane.TypeId);
        hprintf("Bot::actionRentRoute(): Renting route %s (using plane type %s, using existing plane %s): ",
                Helper::getRouteName(getRoute(mRoutes.back())).c_str(), (LPCTSTR)PlaneTypes[qPlane.TypeId].Name, Helper::getPlaneName(qPlane).c_str());

        eraseFirst(mPlanesForJobs, mUsePlaneForRouteId);
        eraseFirst(mPlanesForJobsUnassigned, mUsePlaneForRouteId);
        mPlanesForRoutesUnassigned.push_back(mUsePlaneForRouteId);
        requestPlanRoutes(false);
    }

    updateRouteInfo();
    requestPlanRoutes(false);
}

void Bot::actionBuyAdsForRoutes(__int64 moneyAvailable) {
    if (mRoutesSortedByImage.empty()) {
        return;
    }

    auto prioEntry = condBuyAdsForRoutes(moneyAvailable);
    while (condBuyAdsForRoutes(moneyAvailable) == prioEntry) {
        const auto &qRoute = mRoutes[mRoutesSortedByImage[0]];

        SLONG cost = 0;
        SLONG adCampaignSize = 5;
        for (; adCampaignSize >= kSmallestAdCampaign; adCampaignSize--) {
            cost = gWerbePrice[1 * 6 + adCampaignSize];
            SLONG imageDelta = (cost / 30000);
            if (getRentRoute(qRoute).Image + imageDelta > 100) {
                continue;
            }
            if (cost <= moneyAvailable) {
                break;
            }
        }
        if (adCampaignSize < kSmallestAdCampaign) {
            return;
        }
        SLONG oldImage = getRentRoute(qRoute).Image;
        hprintf("Bot::actionBuyAdsForRoutes(): Buying advertisement for route %s for %ld $", Helper::getRouteName(getRoute(qRoute)).c_str(), cost);
        GameMechanic::buyAdvertisement(qPlayer, 1, adCampaignSize, qRoute.routeId);
        moneyAvailable = getMoneyAvailable();

        hprintf("Bot::actionBuyAdsForRoutes(): Route image improved (%ld => %ld)", oldImage, getRentRoute(qRoute).Image);
        updateRouteInfo();
    }
}

void Bot::actionBuyAds(__int64 moneyAvailable) {
    for (SLONG adCampaignSize = 5; adCampaignSize >= kSmallestAdCampaign; adCampaignSize--) {
        SLONG cost = gWerbePrice[0 * 6 + adCampaignSize];
        while (moneyAvailable > cost) {
            SLONG imageDelta = cost / 10000 * (adCampaignSize + 6) / 55;
            if (qPlayer.Image + imageDelta > 1000) {
                break;
            }
            SLONG oldImage = qPlayer.Image;
            hprintf("Bot::actionBuyAds(): Buying advertisement for airline for %ld $", cost);
            GameMechanic::buyAdvertisement(qPlayer, 0, adCampaignSize);
            moneyAvailable = getMoneyAvailable();

            hprintf("Bot::actionBuyAds(): Airline image improved (%ld => %ld)", oldImage, qPlayer.Image);
            if (!qPlayer.RobotUse(ROBOT_USE_WERBUNG)) {
                return;
            }
        }
    }
}
