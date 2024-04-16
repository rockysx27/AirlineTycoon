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

static constexpr int ceil_div(int a, int b) {
    assert(b != 0);
    return a / b + (a % b != 0);
}

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

void Bot::determineNemesis() {
    auto nemesisOld = mNemesis;
    mNemesis = -1;
    mNemesisScore = 0;
    for (SLONG p = 0; p < 4; p++) {
        auto &qTarget = Sim.Players.Players[p];
        if (p == qPlayer.PlayerNum || qTarget.IsOut != 0) {
            continue;
        }

        __int64 score = 0;
        if (Sim.Difficulty == DIFF_FREEGAME) {
            score = qTarget.BilanzWoche.Hole().GetOpSaldo();
        } else {
            /* for missions */
            if (Sim.Difficulty == DIFF_FINAL || Sim.Difficulty == DIFF_ADDON10) {
                /* better than GetMissionRating(): Calculate sum of part cost instead of just number of parts */
                const auto &qPrices = (Sim.Difficulty == DIFF_FINAL) ? RocketPrices : StationPrices;
                auto nParts = sizeof(qPrices) / sizeof(qPrices[0]);
                for (SLONG i = 0; i < nParts; i++) {
                    if ((qTarget.RocketFlags & (1 << i)) != 0) {
                        score += qPrices[i];
                    }
                }
            } else {
                score = qTarget.GetMissionRating();
            }
        }
        if (score > mNemesisScore) {
            mNemesis = p;
            mNemesisScore = score;
        }
    }
    if (-1 != mNemesis) {
        if (nemesisOld != mNemesis) {
            hprintf("Bot::determineNemesis(): Our nemesis now is %s with a score of %s", (LPCTSTR)Sim.Players.Players[mNemesis].AirlineX,
                    (LPCTSTR)Insert1000erDots64(mNemesisScore));
        } else {
            hprintf("Bot::determineNemesis(): Our nemesis is still %s with a score of %s", (LPCTSTR)Sim.Players.Players[mNemesis].AirlineX,
                    (LPCTSTR)Insert1000erDots64(mNemesisScore));
        }
    }
}

void Bot::switchToFinalTarget() {
    if (mRunToFinalObjective == FinalPhase::TargetRun) {
        hprintf("Bot::switchToFinalTarget(): We are in final target run.");
        return;
    }

    __int64 requiredMoney = 0;
    bool forceSwitch = false;
    DOUBLE nemesisRatio = 0;
    if (qPlayer.RobotUse(ROBOT_USE_NASA) && (Sim.Difficulty == DIFF_FINAL || Sim.Difficulty == DIFF_ADDON10)) {
        const auto &qPrices = (Sim.Difficulty == DIFF_FINAL) ? RocketPrices : StationPrices;
        auto nParts = sizeof(qPrices) / sizeof(qPrices[0]);
        SLONG numRequired = 0;
        for (SLONG i = 0; i < nParts; i++) {
            if ((qPlayer.RocketFlags & (1 << i)) == 0) {
                requiredMoney += qPrices[i];
                numRequired++;
            }
        }
        hprintf("Bot::switchToFinalTarget(): Need %lld to buy %ld missing pieces.", requiredMoney, numRequired);

        nemesisRatio = 1.0 * mNemesisScore / requiredMoney;
        if (nemesisRatio > 0.5) {
            forceSwitch = true;
        }
    } else if (qPlayer.RobotUse(ROBOT_USE_MUCHWERBUNG) && Sim.Difficulty == DIFF_HARD) {
        /* formula that calculates image gain from largest campaign */
        SLONG adCampaignSize = 5;
        SLONG numCampaignsRequired = ceil_div((TARGET_IMAGE - qPlayer.Image) * 55, (adCampaignSize + 6) * (gWerbePrice[adCampaignSize] / 10000));
        requiredMoney = numCampaignsRequired * gWerbePrice[adCampaignSize];
        hprintf("Bot::switchToFinalTarget(): Need %lld to buy %ld ad campaigns.", requiredMoney, numCampaignsRequired);

        nemesisRatio = 1.0 * mNemesisScore / TARGET_IMAGE;
        if (nemesisRatio > 0.7) {
            forceSwitch = true;
        }
    } else if (qPlayer.RobotUse(ROBOT_USE_LUXERY) && Sim.Difficulty == DIFF_ADDON05) {
        /* how many service points can we get by upgrading? */
        SLONG servicePointsStart = qPlayer.GetMissionRating();
        SLONG servicePoints = servicePointsStart;
        for (SLONG upgradeWhat = 0; upgradeWhat < 7; upgradeWhat++) {
            if (servicePoints > TARGET_SERVICE) {
                break;
            }

            auto planes = getAllPlanes();
            for (SLONG c = 0; c < planes.size(); c++) {
                CPlane &qPlane = qPlayer.Planes[planes[c]];
                auto ptPassagiere = qPlane.ptPassagiere;

                if (servicePoints > TARGET_SERVICE) {
                    break;
                }

                switch (upgradeWhat) {
                case 0:
                    if (qPlane.SitzeTarget < 2) {
                        requiredMoney += ptPassagiere * (SeatCosts[2] - SeatCosts[qPlane.Sitze] / 2);
                        servicePoints += (2 - qPlane.Sitze);
                    }
                    break;
                case 1:
                    if (qPlane.TablettsTarget < 2) {
                        requiredMoney += ptPassagiere * (TrayCosts[2] - TrayCosts[qPlane.Tabletts] / 2);
                        servicePoints += (2 - qPlane.Tabletts);
                    }
                    break;
                case 2:
                    if (qPlane.DecoTarget < 2) {
                        requiredMoney += ptPassagiere * (DecoCosts[2] - DecoCosts[qPlane.Deco] / 2);
                        servicePoints += (2 - qPlane.Deco);
                    }
                    break;
                case 3:
                    if (qPlane.ReifenTarget < 2) {
                        requiredMoney += (ReifenCosts[2] - ReifenCosts[qPlane.Reifen] / 2);
                        servicePoints += (2 - qPlane.Reifen);
                    }
                    break;
                case 4:
                    if (qPlane.TriebwerkTarget < 2) {
                        requiredMoney += (TriebwerkCosts[2] - TriebwerkCosts[qPlane.Triebwerk] / 2);
                        servicePoints += (2 - qPlane.Triebwerk);
                    }
                    break;
                case 5:
                    if (qPlane.SicherheitTarget < 2) {
                        requiredMoney += (SicherheitCosts[2] - SicherheitCosts[qPlane.Sicherheit] / 2);
                        servicePoints += (2 - qPlane.Sicherheit);
                    }
                    break;
                case 6:
                    if (qPlane.ElektronikTarget < 2) {
                        requiredMoney += (ElektronikCosts[2] - ElektronikCosts[qPlane.Elektronik] / 2);
                        servicePoints += (2 - qPlane.Elektronik);
                    }
                    break;
                default:
                    redprintf("Bot.cpp: Default case should not be reached.");
                    DebugBreak();
                }
            }
        }

        nemesisRatio = 1.0 * mNemesisScore / TARGET_SERVICE;
        if (servicePoints <= TARGET_SERVICE) {
            hprintf("Bot::switchToFinalTarget(): Can only get %ld service points in total (+ %ld) by upgrading existing planes.", servicePoints,
                    servicePoints - servicePointsStart);
            requiredMoney = LLONG_MAX; /* we cannot reach target yet */
        } else {
            hprintf("Bot::switchToFinalTarget(): Need %lld to upgrade existing planes.", requiredMoney);
            if (nemesisRatio > 0.7) {
                forceSwitch = true;
            }
        }

    } else {
        /* no race to finish for this mission */
        return;
    }

    if (nemesisRatio > 0) {
        hprintf("Bot::switchToFinalTarget(): Most dangerous competitor is %s with %.1f %% of goal achieved.", (LPCTSTR)Sim.Players.Players[mNemesis].AirlineX,
                nemesisRatio * 100);
    }
    if (forceSwitch) {
        hprintf("Bot::switchToFinalTarget(): Competitor too close, forcing switch.");
    }

    auto availableMoney = howMuchMoneyCanWeGet(true);
    auto cash = qPlayer.Money - kMoneyEmergencyFund;
    if ((availableMoney > requiredMoney) || forceSwitch) {
        mRunToFinalObjective = FinalPhase::TargetRun;
        mMoneyForFinalObjective = requiredMoney;
        hprintf("Bot::switchToFinalTarget(): Switching to final target run. Need %s $, got %s $ (+ %s $).", (LPCTSTR)Insert1000erDots64(requiredMoney),
                (LPCTSTR)Insert1000erDots64(cash), (LPCTSTR)Insert1000erDots64(availableMoney - cash));
    } else if (1.0 * availableMoney / requiredMoney >= 0.8) {
        mRunToFinalObjective = FinalPhase::SaveMoney;
        mMoneyForFinalObjective = requiredMoney;
        hprintf("Bot::switchToFinalTarget(): Switching to money saving phase. Need %s $, got %s $ (+ %s $).", (LPCTSTR)Insert1000erDots64(requiredMoney),
                (LPCTSTR)Insert1000erDots64(cash), (LPCTSTR)Insert1000erDots64(availableMoney - cash));
    } else if (mRunToFinalObjective == FinalPhase::No) {
        if (requiredMoney < LLONG_MAX) {
            hprintf("Bot::switchToFinalTarget(): Cannot switch to final target run. Need %s $, got %s $ (+ %s $).", (LPCTSTR)Insert1000erDots64(requiredMoney),
                    (LPCTSTR)Insert1000erDots64(cash), (LPCTSTR)Insert1000erDots64(availableMoney - cash));
        } else {
            hprintf("Bot::switchToFinalTarget(): Cannot switch to final target run, target not in reach.");
        }
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
    } else {
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
                hprintf("Bot::actionStartDay(): Switching to routes.");
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

        // Frachtaufträge:
        // RobotUse(ROBOT_USE_MUCH_FRACHT)
        // TODO

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

void Bot::grabFlights(BotPlaner &planer, bool areWeInOffice) {
    auto res = canWePlanFlights();
    if (HowToPlan::None == res) {
        redprintf("Bot::grabFlights(): Tried to grab plans without ability to plan them");
        return;
    }

    int extraBufferTime = kAvailTimeExtra;
    if (!areWeInOffice && HowToPlan::Office == res) {
        extraBufferTime += 1;
        if (Sim.GetMinute() >= 30) {
            extraBufferTime += 1;
        }
        hprintf("Bot::grabFlights(): Extra time planned for walking to office: %d", extraBufferTime);
    }

    planer.setMinimumScoreRatio(kSchedulingMinScoreRatio);

    /* configure weighting for special missions */
    switch (Sim.Difficulty) {
    case DIFF_TUTORIAL:
        planer.setConstBonus(1000 * 1000); /* need to schedule 10 jobs ASAP, so premium does not really matter */
        break;
    case DIFF_FIRST:
        planer.setPassengerFactor(10 * 1000); /* need to fly as many passengers as possible */
        break;
    case DIFF_ADDON03:
        planer.setFreeFreightBonus(5000 * 1000);
        break;
    case DIFF_ADDON04:
        // planer.setDistanceFactor(1); TODO
        break;
    case DIFF_ADDON06:
        planer.setConstBonus(1000 * 1000);
        break;
    case DIFF_ADDON09:
        planer.setUhrigBonus(1000 * 1000);
        break;
    default:
        break;
    }

    mPlanerSolution = planer.planFlights(mPlanesForJobs, extraBufferTime);
    if (!mPlanerSolution.empty()) {
        requestPlanFlights(areWeInOffice);
    }
}

void Bot::requestPlanFlights(bool areWeInOffice) {
    auto res = canWePlanFlights();
    if (res == HowToPlan::Laptop) {
        planFlights();
        hprintf("Bot::requestPlanFlights(): Planning using laptop");
    } else {
        mNeedToPlanJobs = true;
        hprintf("Bot::requestPlanFlights(): No laptop, need to go to office");
        forceReplanning();
    }

    if (res == HowToPlan::Office && areWeInOffice) {
        planFlights();
        hprintf("Bot::requestPlanFlights(): Already in office, planning right now");
        mNeedToPlanJobs = false;
    }
}

void Bot::planFlights() {
    mNeedToPlanJobs = false;

    SLONG oldGain = calcCurrentGainFromJobs();
    if (!BotPlaner::applySolution(qPlayer, mPlanerSolution)) {
        redprintf("Bot::planFlights(): Solution does not apply! Need to re-plan.");

        BotPlaner planer(qPlayer, qPlayer.Planes);
        mPlanerSolution = planer.planFlights(mPlanesForJobs, kAvailTimeExtra);
        if (!mPlanerSolution.empty()) {
            BotPlaner::applySolution(qPlayer, mPlanerSolution);
        }
    }

    SLONG newGain = calcCurrentGainFromJobs();
    SLONG diff = newGain - oldGain;
    if (diff > 0) {
        hprintf("Total gain improved: %s $ (+%s $)", Insert1000erDots(newGain).c_str(), Insert1000erDots(diff).c_str());
    } else if (diff == 0) {
        hprintf("Total gain did not change: %s $", Insert1000erDots(newGain).c_str());
    } else {
        hprintf("Total gain got worse: %s $ (%s $)", Insert1000erDots(newGain).c_str(), Insert1000erDots(diff).c_str());
    }
    Helper::checkFlightJobs(qPlayer, false, true);

    mPlanerSolution = {};

    forceReplanning();
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
        hprintf("Bot::actionStartDay(): Increases salaray of %ld employees", salaryIncreases);
    }
}

std::pair<SLONG, SLONG> Bot::kerosineQualiOptimization(__int64 moneyAvailable, DOUBLE targetFillRatio) const {
    hprintf("Bot::kerosineQualiOptimization(): Buying kerosine for no more than %lld $ and %.2f %% of capacity", moneyAvailable, targetFillRatio * 100);

    std::pair<SLONG, SLONG> res{};
    DOUBLE priceGood = Sim.HoleKerosinPreis(1);
    DOUBLE priceBad = Sim.HoleKerosinPreis(2);
    DOUBLE tankContent = qPlayer.TankInhalt;
    DOUBLE tankMax = qPlayer.Tank * targetFillRatio;

    if (qPlayer.HasBerater(BERATERTYP_KEROSIN) < 70) {
        /* just buy normal kerosine */
        DOUBLE amount = moneyAvailable / priceGood; /* simply buy normal kerosine */
        if (amount + tankContent > tankMax) {
            amount = tankMax - tankContent;
        }
        if (amount > INT_MAX) {
            amount = INT_MAX;
        }
        res.first = static_cast<SLONG>(std::floor(amount));
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
    qualiZiel = std::min(qualiZiel, kMaxKerosinQualiZiel);

    DOUBLE qualiStart = qPlayer.KerosinQuali;
    DOUBLE amountGood = 0;
    DOUBLE amountBad = 0;

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

    if (!qPlayer.RobotUse(ROBOT_USE_SABOTAGE) || mNemesis == -1) {
        return;
    }
    GameMechanic::setSaboteurTarget(qPlayer, mNemesis);

    if (SabotagePrice2[1 - 1] <= moneyAvailable) {
        auto res = GameMechanic::checkPrerequisitesForSaboteurJob(qPlayer, 1, 1).result;
        if (res == GameMechanic::CheckSabotageResult::Ok) {
            GameMechanic::activateSaboteurJob(qPlayer);
            hprintf("Bot::actionSabotage(): Sabotaging nemesis %s using pills", (LPCTSTR)Sim.Players.Players[mNemesis].AirlineX);
        }
    }

    moneyAvailable = getMoneyAvailable();
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

void Bot::actionEmitShares() {
    SLONG newStock = (qPlayer.MaxAktien - qPlayer.AnzAktien) / 100 * 100;
    hprintf("Bot::actionEmitShares(): Emitting stock: %ld", newStock);
    GameMechanic::emitStock(qPlayer, newStock, kStockEmissionMode);
    auto moneyAvailable = getMoneyAvailable();

    // Direkt wieder auf ein Viertel aufkaufen
    SLONG amountToBuy = qPlayer.AnzAktien / 4 - qPlayer.OwnsAktien[qPlayer.PlayerNum];
    amountToBuy = std::min(amountToBuy, calcBuyShares(moneyAvailable, qPlayer.Kurse[0]));
    if (amountToBuy > 0) {
        hprintf("Bot::actionEmitShares(): Buying own stock: %ld", amountToBuy);
        GameMechanic::buyStock(qPlayer, qPlayer.PlayerNum, amountToBuy);
        moneyAvailable = getMoneyAvailable();
    }
}

void Bot::actionBuyShares(__int64 moneyAvailable) {
    if (condBuyNemesisShares(moneyAvailable, mNemesis) != Prio::None) {
        auto &qDislikedPlayer = Sim.Players.Players[mNemesis];
        SLONG amount = calcNumOfFreeShares(mNemesis);
        SLONG amountCanAfford = calcBuyShares(moneyAvailable, qDislikedPlayer.Kurse[0]);
        amount = std::min(amount, amountCanAfford);

        if (amount > 0) {
            hprintf("Bot::actionBuyShares(): Buying nemesis stock: %ld", amount);
            GameMechanic::buyStock(qPlayer, mNemesis, amount);

            moneyAvailable = getMoneyAvailable();
        }
    }

    if (condBuyOwnShares(moneyAvailable) != Prio::None) {
        SLONG amount = calcNumOfFreeShares(qPlayer.PlayerNum);
        SLONG amountToBuy = qPlayer.AnzAktien / 4 - qPlayer.OwnsAktien[qPlayer.PlayerNum];
        SLONG amountCanAfford = calcBuyShares(moneyAvailable, qPlayer.Kurse[0]);
        amount = std::min({amount, amountToBuy, amountCanAfford});

        if (amount > 0) {
            hprintf("Bot::actionBuyShares(): Buying own stock: %ld", amount);
            GameMechanic::buyStock(qPlayer, qPlayer.PlayerNum, amount);
            moneyAvailable = getMoneyAvailable();
        }
    }
}

void Bot::actionSellShares(__int64 moneyAvailable) {
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
                hprintf("Bot::RobotExecuteAction(): Selling own stock: %ld", sells);
                GameMechanic::sellStock(qPlayer, c, sells);
            }
        } else if (res == HowToGetMoney::SellAllOwnShares) {
            SLONG c = qPlayer.PlayerNum;
            SLONG sellsNeeded = calcSellShares(howMuchToRaise, qPlayer.Kurse[0]);
            SLONG sells = std::min(qPlayer.OwnsAktien[c], sellsNeeded);
            if (sells > 0) {
                hprintf("Bot::RobotExecuteAction(): Selling all own stock: %ld", sells);
                GameMechanic::sellStock(qPlayer, c, sells);
            }
        } else if (res == HowToGetMoney::SellShares) {
            for (SLONG c = 0; c < Sim.Players.AnzPlayers; c++) {
                if (qPlayer.OwnsAktien[c] == 0 || c == qPlayer.PlayerNum) {
                    continue;
                }

                SLONG sellsNeeded = calcSellShares(howMuchToRaise, Sim.Players.Players[c].Kurse[0]);
                SLONG sells = std::min(qPlayer.OwnsAktien[c], sellsNeeded);
                hprintf("Bot::RobotExecuteAction(): Selling stock from player %ld: %ld", c, sells);
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

        /* WorstZustand more than 20 lower than repair target means extra cost! */
        GameMechanic::setPlaneTargetZustand(qPlayer, c, std::min(100, qPlane.WorstZustand + 20));

        if (qPlanes[c].TargetZustand == 100) {
            continue; /* this plane won't cost extra */
        }

        planeList.emplace_back(c, qPlane.TargetZustand);
    }

    /* distribute available money for repair extra costs */
    mMoneyReservedForRepairs = 0;
    auto moneyAvailable = getMoneyAvailable() - kMoneyReserveRepairs;
    bool keepGoing = true;
    while (keepGoing && moneyAvailable >= 0) {
        keepGoing = false;
        for (const auto &iter : planeList) {
            auto &qPlane = qPlanes[iter.first];
            SLONG cost = (qPlane.TargetZustand + 1 > (qPlane.WorstZustand + 20)) ? (qPlane.ptPreis / 110) : 0;
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
        if (qPlane.TargetZustand > iter.second) {
            hprintf("Bot::actionVisitMech(): Increasing repair target of plane %s: %ld => %ld (Zustand = %u, WorstZustand = %u)",
                    Helper::getPlaneName(qPlane, 1).c_str(), iter.second, qPlane.TargetZustand, qPlane.Zustand, qPlane.WorstZustand);
        } else if (qPlane.TargetZustand < iter.second) {
            hprintf("Bot::actionVisitMech(): Lowering repair target of plane %s: %ld => %ld (Zustand = %u, WorstZustand = %u)",
                    Helper::getPlaneName(qPlane, 1).c_str(), iter.second, qPlane.TargetZustand, qPlane.Zustand, qPlane.WorstZustand);
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

    // #ifdef PRINT_ROUTE_DETAILS
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
    // #endif

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

SLONG Bot::getRouteTurnAroundDuration(const CRoute &qRoute, SLONG planeTypeId) const {
    SLONG durationA = kDurationExtra + Cities.CalcFlugdauer(qRoute.VonCity, qRoute.NachCity, PlaneTypes[planeTypeId].Geschwindigkeit);
    SLONG durationB = kDurationExtra + Cities.CalcFlugdauer(qRoute.NachCity, qRoute.VonCity, PlaneTypes[planeTypeId].Geschwindigkeit);
    return (durationA + durationB);
}

void Bot::checkLostRoutes() {
    if (mRoutes.empty()) {
        return;
    }

    SLONG numRented = 0;
    const auto &qRRouten = qPlayer.RentRouten.RentRouten;
    for (const auto &rentRoute : qRRouten) {
        if (rentRoute.Rang != 0) {
            numRented++;
        }
    }

    assert(numRented % 2 == 0);
    assert(numRented / 2 <= mRoutes.size());
    if (numRented / 2 < mRoutes.size()) {
        redprintf("We lost %ld routes!", mRoutes.size() - numRented / 2);

        std::vector<RouteInfo> routesNew;
        std::vector<SLONG> planesForRoutesNew;
        for (const auto &route : mRoutes) {
            if (qRRouten[route.routeId].Rang != 0) {
                /* route still exists */
                routesNew.emplace_back(route);
                for (auto planeId : route.planeIds) {
                    planesForRoutesNew.push_back(planeId);
                }
            } else {
                /* route is gone! move planes from route back into the "unassigned" pile */
                for (auto planeId : route.planeIds) {
                    mPlanesForRoutesUnassigned.push_back(planeId);
                    GameMechanic::killFlightPlan(qPlayer, planeId);
                    hprintf("Bot::checkLostRoutes(): Plane %s does not have a route anymore.", Helper::getPlaneName(qPlayer.Planes[planeId]).c_str());
                }
            }
        }
        std::swap(mRoutes, routesNew);
        std::swap(mPlanesForRoutes, planesForRoutesNew);
    }
}

void Bot::updateRouteInfo() {
    if (mRoutes.empty()) {
        return;
    }

    /* copy most import information from routes */
    for (auto &route : mRoutes) {
        route.image = getRentRoute(route).Image;
        route.routeUtilization = getRentRoute(route).RoutenAuslastung;
        route.planeUtilization = getRentRoute(route).Auslastung;
        route.planeUtilizationFC = getRentRoute(route).AuslastungFC;

        DOUBLE luxusSumme = 0;
        for (auto i : route.planeIds) {
            const auto &qPlane = qPlayer.Planes[i];
            luxusSumme += qPlane.Sitze + qPlane.Essen + qPlane.Tabletts + qPlane.Deco;
            luxusSumme += qPlane.Triebwerk + qPlane.Reifen + qPlane.Elektronik + qPlane.Sicherheit;
        }
        luxusSumme /= route.planeIds.size();

        hprintf("Bot::updateRouteInfo(): Route %s has image=%ld and utilization=%ld (%ld planes with average utilization=%ld/%ld and luxus=%.2f)",
                Helper::getRouteName(getRoute(route)).c_str(), route.image, route.routeUtilization, route.planeIds.size(), route.planeUtilization,
                route.planeUtilizationFC, luxusSumme);
    }

    /* sort routes by utilization and by image */
    mRoutesSortedByUtilization.resize(mRoutes.size());
    mRoutesSortedByImage.resize(mRoutes.size());
    for (SLONG i = 0; i < mRoutes.size(); i++) {
        mRoutesSortedByUtilization[i] = i;
        mRoutesSortedByImage[i] = i;
    }
    std::sort(mRoutesSortedByUtilization.begin(), mRoutesSortedByUtilization.end(),
              [&](SLONG a, SLONG b) { return mRoutes[a].routeUtilization < mRoutes[b].routeUtilization; });
    std::sort(mRoutesSortedByImage.begin(), mRoutesSortedByImage.end(), [&](SLONG a, SLONG b) { return mRoutes[a].image < mRoutes[b].image; });

    auto lowImage = mRoutesSortedByImage[0];
    auto lowUtil = mRoutesSortedByUtilization[0];
    hprintf("Bot::updateRouteInfo(): Route %s has lowest image: %ld", Helper::getRouteName(getRoute(mRoutes[lowImage])).c_str(), mRoutes[lowImage].image);
    hprintf("Bot::updateRouteInfo(): Route %s has lowest utilization: %ld", Helper::getRouteName(getRoute(mRoutes[lowUtil])).c_str(),
            mRoutes[lowUtil].routeUtilization);

    /* do something about underutilized routes */
    if (mRoutes[lowUtil].routeUtilization < kMaximumRouteUtilization()) {
        mWantToRentRouteId = -1;
        mBuyPlaneForRouteId = mRoutes[lowUtil].planeTypeId;
        hprintf("Bot::updateRouteInfo(): Need to buy another %s for route %s: ", (LPCTSTR)PlaneTypes[mBuyPlaneForRouteId].Name,
                Helper::getRouteName(getRoute(mRoutes[lowUtil])).c_str());
    }

    /* idle planes? */
    if (!mPlanesForRoutesUnassigned.empty()) {
        hprintf("Bot::updateRouteInfo(): There are %lu unassigned planes with no route ", mPlanesForRoutesUnassigned.size());
    }
}

void Bot::requestPlanRoutes(bool areWeInOffice) {
    auto res = canWePlanFlights();
    if (res == HowToPlan::Laptop) {
        planRoutes();
        hprintf("Bot::requestPlanRoutes(): Planning using laptop");
    } else {
        mNeedToPlanRoutes = true;
        hprintf("Bot::requestPlanRoutes(): No laptop, need to go to office");
        forceReplanning();
    }

    if (res == HowToPlan::Office && areWeInOffice) {
        planRoutes();
        hprintf("Bot::requestPlanRoutes(): Already in office, planning right now");
        mNeedToPlanRoutes = false;
    }
}

void Bot::planRoutes() {
    mNeedToPlanRoutes = false;
    if (mRoutes.empty()) {
        return;
    }

    /* assign planes to routes */
    SLONG numUnassigned = mPlanesForRoutesUnassigned.size();
    for (SLONG i = 0; i < numUnassigned; i++) {
        if (mRoutes[mRoutesSortedByUtilization[0]].routeUtilization >= kMaximumRouteUtilization()) {
            break; /* No more underutilized routes */
        }

        SLONG planeId = mPlanesForRoutesUnassigned.front();
        const auto &qPlane = qPlayer.Planes[planeId];
        mPlanesForRoutesUnassigned.pop_front();

        if (!checkPlaneAvailable(planeId, true)) {
            mPlanesForRoutesUnassigned.push_back(planeId);
            continue;
        }

        SLONG targetRouteIdx = -1;
        for (SLONG routeIdx : mRoutesSortedByUtilization) {
            auto &qRoute = mRoutes[routeIdx];
            if (qRoute.routeUtilization >= kMaximumRouteUtilization()) {
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
            hprintf("Bot::planRoutes(): Assigning plane %s to route %s", Helper::getPlaneName(qPlane).c_str(), Helper::getRouteName(getRoute(qRoute)).c_str());
        } else {
            mPlanesForRoutesUnassigned.push_back(planeId);
        }
    }

    /* plan route flights */
    for (auto &qRoute : mRoutes) {
        SLONG fromCity = Cities.find(getRoute(qRoute).VonCity);
        SLONG toCity = Cities.find(getRoute(qRoute).NachCity);
        SLONG durationA = kDurationExtra + Cities.CalcFlugdauer(fromCity, toCity, PlaneTypes[qRoute.planeTypeId].Geschwindigkeit);
        SLONG durationB = kDurationExtra + Cities.CalcFlugdauer(toCity, fromCity, PlaneTypes[qRoute.planeTypeId].Geschwindigkeit);
        // TODO: SLONG roundTripDuration = durationA + durationB;
        // TODO: int timeSlot = 0;
        for (auto planeId : qRoute.planeIds) {
            const auto &qPlane = qPlayer.Planes[planeId];

#ifdef PRINT_ROUTE_DETAILS
            hprintf("Bot::planRoutes(): =================== Plane %s ===================", Helper::getPlaneName(qPlane).c_str());
            Helper::printFlightJobs(qPlayer, planeId);
#endif

            /* re-plan anything after 3 days because of spurious route flights appearing */
            GameMechanic::killFlightPlanFrom(qPlayer, planeId, Sim.Date + 3, 0);

            /* where is the plane right now and when can it be in the origin city? */
            PlaneTime availTime;
            SLONG availCity{};
            std::tie(availTime, availCity) = Helper::getPlaneAvailableTimeLoc(qPlane, {}, {});
            availCity = Cities.find(availCity);
#ifdef PRINT_ROUTE_DETAILS
            hprintf("Bot::planRoutes(): Plane %s is in %s @ %s %ld", Helper::getPlaneName(qPlane).c_str(), (LPCTSTR)Cities[availCity].Kuerzel,
                    (LPCTSTR)Helper::getWeekday(availTime.getDate()), availTime.getHour());
#endif

            /* leave room for auto flight, if necessary */
            if (availCity != fromCity && availCity != toCity) {
                SLONG autoFlightDuration = kDurationExtra + Cities.CalcFlugdauer(availCity, fromCity, qPlane.ptGeschwindigkeit);
                availTime += autoFlightDuration;
                hprintf("Bot::planRoutes(): Plane %s: Adding buffer of %ld hours for auto flight from %s to %s", Helper::getPlaneName(qPlane).c_str(),
                        autoFlightDuration, (LPCTSTR)Cities[availCity].Kuerzel, (LPCTSTR)Cities[fromCity].Kuerzel);
            }

            if (availTime.getDate() >= Sim.Date + 5) {
                continue;
            }

            /* planes on same route fly with 3 hours inbetween */
            // TODO:
            // SLONG h = availTime.getDate() * 24 + availTime.getHour();
            // h = ceil_div(h, roundTripDuration) * roundTripDuration;
            // h += (3 * (timeSlot++)) % 24;
            // availTime = {h / 24, h % 24};
            // hprintf("BotPlaner::planRoutes(): Plane %s: Setting availTime to %s %ld to meet timeSlot=%ld", Helper::getPlaneName(qPlane).c_str(),
            //         (LPCTSTR)Helper::getWeekday(availTime.getDate()), availTime.getHour(), timeSlot - 1);

            // Helper::printFlightJobs(qPlayer, planeId);

            /* if in B, schedule one instance of B=>A */
            SLONG numScheduled = 0;
            PlaneTime curTime = availTime;
            if (availCity == toCity) {
                if (!GameMechanic::planRouteJob(qPlayer, planeId, qRoute.routeReverseId, curTime.getDate(), curTime.getHour())) {
                    redprintf("Bot::planRoutes(): GameMechanic::planRouteJob returned error!");
                    return;
                }
                curTime += durationB;
                numScheduled++;
            }

            /* always schedule A=>B and B=>A, for the next 5 days */
            while (curTime.getDate() < Sim.Date + 5) {
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
            hprintf("Bot::planRoutes(): Scheduled route %s %ld times for plane %s, starting at %s %ld", Helper::getRouteName(getRoute(qRoute)).c_str(),
                    numScheduled, Helper::getPlaneName(qPlane).c_str(), (LPCTSTR)Helper::getWeekday(availTime.getDate()), availTime.getHour());
            Helper::checkPlaneSchedule(qPlayer, planeId, false);
        }
    }
    Helper::checkFlightJobs(qPlayer, false, true);

    /* adjust ticket prices */
    for (auto &qRoute : mRoutes) {
        SLONG priceOld = getRentRoute(qRoute).Ticketpreis;
        DOUBLE factorOld = qRoute.ticketCostFactor;
        SLONG costs = CalculateFlightCost(getRoute(qRoute).VonCity, getRoute(qRoute).NachCity, 800, 800, -1) * 3 / 180 * 2;
        if (qRoute.planeUtilization > kMaximumPlaneUtilization) {
            qRoute.ticketCostFactor += 0.1;
        } else {
            /* planes are not fully utilized */
            if (qRoute.routeUtilization < kMaximumRouteUtilization()) {
                /* decrease one time per each 25% missing */
                SLONG numDecreases = ceil_div(kMaximumPlaneUtilization - qRoute.planeUtilization, 25);
                qRoute.ticketCostFactor -= (0.1 * numDecreases);
            }
        }
        Limit(0.5, qRoute.ticketCostFactor, kMaxTicketPriceFactor);

        SLONG priceNew = costs * qRoute.ticketCostFactor;
        priceNew = priceNew / 10 * 10;
        if (priceNew != priceOld) {
            GameMechanic::setRouteTicketPriceBoth(qPlayer, qRoute.routeId, priceNew, priceNew * 2);
            hprintf("Bot::planRoutes(): Changing ticket prices for route %s: %ld => %ld (%.2f => %.2f)", Helper::getRouteName(getRoute(qRoute)).c_str(),
                    priceOld, priceNew, factorOld, qRoute.ticketCostFactor);
        }
    }
}
