#include "Bot.h"

#include "BotHelper.h"
#include "class.h"
#include "GameMechanic.h"
#include "global.h"
#include "TeakLibW.h"

#include <algorithm>

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

void Bot::determineNemesis() {
    auto nemesisOld = mNemesis;
    mNemesis = -1;
    mNemesisScore = INT_MIN;
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
            } else if (Sim.Difficulty == DIFF_ADDON01) {
                /* negative: lower score is better! */
                score = -qTarget.GetMissionRating();
            } else {
                score = qTarget.GetMissionRating();
            }
        }
        if (score > mNemesisScore && p != mNemesisSabotaged) {
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
    mNemesisSabotaged = -1;
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
    } else if (qPlayer.RobotUse(ROBOT_USE_LUXERY) && Sim.Difficulty == DIFF_ATFS02) {
        /* how much money do we need to upgrade everything safety-related? */
        auto planes = getAllPlanes();
        for (SLONG c = 0; c < planes.size(); c++) {
            CPlane &qPlane = qPlayer.Planes[planes[c]];
            if (qPlane.ReifenTarget < 2) {
                requiredMoney += (ReifenCosts[2] - ReifenCosts[qPlane.Reifen] / 2);
            }
            if (qPlane.TriebwerkTarget < 2) {
                requiredMoney += (TriebwerkCosts[2] - TriebwerkCosts[qPlane.Triebwerk] / 2);
            }
            if (qPlane.SicherheitTarget < 2) {
                requiredMoney += (SicherheitCosts[2] - SicherheitCosts[qPlane.Sicherheit] / 2);
            }
            if (qPlane.ElektronikTarget < 2) {
                requiredMoney += (ElektronikCosts[2] - ElektronikCosts[qPlane.Elektronik] / 2);
            }
        }

        if (qPlayer.Planes.GetNumUsed() < 5) {
            hprintf("Bot::switchToFinalTarget(): We do not have enough planes yet: %ld (need 5)", qPlayer.Planes.GetNumUsed());
            requiredMoney = LLONG_MAX; /* we cannot reach target yet */
        } else {
            hprintf("Bot::switchToFinalTarget(): Need %lld to upgrade existing planes.", requiredMoney);
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

    /* is our target in reach at all? */
    if (requiredMoney == LLONG_MAX) {
        hprintf("Bot::switchToFinalTarget(): Cannot switch to final target run, target not in reach.");
        if (mRunToFinalObjective == FinalPhase::SaveMoney) {
            redprintf("Bot::switchToFinalTarget(): We switched to 'save money' phase too early!");
        } else if (mRunToFinalObjective == FinalPhase::TargetRun) {
            redprintf("Bot::switchToFinalTarget(): We switched to 'target run' phase too early!");
        }
        return;
    }

    /* switch to final phase if we have enough money */
    auto availableMoney = howMuchMoneyCanWeGet(true);
    auto cash = qPlayer.Money - kMoneyEmergencyFund;
    if (mRunToFinalObjective < FinalPhase::TargetRun) {
        if ((availableMoney > requiredMoney) || forceSwitch) {
            mRunToFinalObjective = FinalPhase::TargetRun;
            mMoneyForFinalObjective = requiredMoney;
            hprintf("Bot::switchToFinalTarget(): Switching to final target run. Need %s $, got %s $ (+ %s $).", (LPCTSTR)Insert1000erDots64(requiredMoney),
                    (LPCTSTR)Insert1000erDots64(cash), (LPCTSTR)Insert1000erDots64(availableMoney - cash));
            return;
        }
    }

    /* switch to money saving phase if we have already 80% */
    if (mRunToFinalObjective < FinalPhase::SaveMoney) {
        if (1.0 * availableMoney / requiredMoney >= 0.8) {
            mRunToFinalObjective = FinalPhase::SaveMoney;
            mMoneyForFinalObjective = requiredMoney;
            hprintf("Bot::switchToFinalTarget(): Switching to money saving phase. Need %s $, got %s $ (+ %s $).", (LPCTSTR)Insert1000erDots64(requiredMoney),
                    (LPCTSTR)Insert1000erDots64(cash), (LPCTSTR)Insert1000erDots64(availableMoney - cash));
            return;
        }
    }

    hprintf("Bot::switchToFinalTarget(): Cannot switch to final target run. Need %s $, got %s $ (+ %s $).", (LPCTSTR)Insert1000erDots64(requiredMoney),
            (LPCTSTR)Insert1000erDots64(cash), (LPCTSTR)Insert1000erDots64(availableMoney - cash));
}

std::vector<SLONG> Bot::findBestAvailablePlaneType(bool forRoutes) const {
    CDataTable planeTable;
    planeTable.FillWithPlaneTypes();
    std::vector<std::pair<SLONG, DOUBLE>> scores;
    for (const auto &i : planeTable.LineIndex) {
        if (!PlaneTypes.IsInAlbum(i)) {
            continue;
        }
        const auto &planeType = PlaneTypes[i];

        if (!GameMechanic::checkPlaneTypeAvailable(i)) {
            continue;
        }

        DOUBLE score = 1.0; /* multiplication (geometric mean) because values have wildly different ranges */
        score = 1.0 * planeType.Passagiere;
        if (!forRoutes) {
            score *= planeType.Reichweite;
        }
        score /= planeType.Verbrauch;

        scores.emplace_back(i, score);
    }
    std::sort(scores.begin(), scores.end(), [](const std::pair<SLONG, DOUBLE> &a, const std::pair<SLONG, DOUBLE> &b) { return a.second > b.second; });

    /* build list */
    std::vector<SLONG> bestList;
    bestList.reserve(scores.size());

    /* exception: Force specified plane type to be best */
    if (kPlaneScoreForceBest != -1) {
        SLONG bestType = kPlaneScoreForceBest + 0x10000000;
        hprintf("Bot::findBestAvailablePlaneType(): Forcing best plane type to be %s", (LPCTSTR)PlaneTypes[bestType].Name);
        bestList.push_back(bestType);
    }

    /* exception: Have atleast one gulfstream for jobs */
    SLONG numGulfstream = 0;
    SLONG gulfstreamType = 119 + 0x10000000;
    for (SLONG i = 0; i < qPlayer.Planes.AnzEntries(); i++) {
        if (qPlayer.Planes.IsInAlbum(i) && qPlayer.Planes[i].TypeId == gulfstreamType) {
            numGulfstream++;
        }
    }
    if (numGulfstream == 0 && !mDoRoutes) {
        hprintf("Bot::findBestAvailablePlaneType(): Forcing best plane type to be %s", (LPCTSTR)PlaneTypes[gulfstreamType].Name);
        bestList.push_back(gulfstreamType);
    }

    for (const auto &i : scores) {
        hprintf("Bot::findBestAvailablePlaneType(): Plane type %s has score %.2e", (LPCTSTR)PlaneTypes[i.first].Name, i.second);
        bestList.push_back(i.first);
    }

    hprintf("Bot::findBestAvailablePlaneType(): Best plane type is %s", (LPCTSTR)PlaneTypes[bestList[0]].Name);
    return bestList;
}

SLONG Bot::findBestAvailableUsedPlane() const {
    SLONG bestIdx = -1;
    DOUBLE bestScore = kUsedPlaneMinimumScore;
    for (SLONG c = 0; c < 3; c++) {
        const auto &qPlane = Sim.UsedPlanes[0x1000000 + c];
        if (qPlane.Name.GetLength() <= 0) {
            continue;
        }

        DOUBLE score = 1.0 * 1e9; /* multiplication (geometric mean) because values have wildly different ranges */
        score *= qPlane.ptPassagiere * qPlane.ptPassagiere;
        score /= qPlane.ptVerbrauch;
        score /= (2015 - qPlane.Baujahr);

        auto worstZustand = qPlane.Zustand - 20;
        SLONG improvementNeeded = std::max(0, 80 - worstZustand);
        SLONG repairCost = improvementNeeded * (qPlane.ptPreis / 110);
        if (qPlayer.HasBerater(BERATERTYP_FLUGZEUG) > 0) {
            score /= repairCost;
        }

        hprintf("Bot::findBestAvailableUsedPlane(): Used plane %s has score %.2f", Helper::getPlaneName(qPlane).c_str(), score);
        hprintf("\t\tPassengers = %ld, fuel = %ld, year = %d", qPlane.ptPassagiere, qPlane.ptVerbrauch, qPlane.Baujahr);
        if (qPlayer.HasBerater(BERATERTYP_FLUGZEUG) > 0) {
            hprintf("\t\tWorstZustand = %u, cost = %d", worstZustand, repairCost);
        }

        if (score > bestScore) {
            bestScore = score;
            bestIdx = c;
        }
    }
    return bestIdx;
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

    planer.setMinScoreRatio(kSchedulingMinScoreRatio);
    planer.setMinScoreRatioLastMinute(kSchedulingMinScoreRatioLastMinute);

    /* configure weighting for special missions */
    switch (Sim.Difficulty) {
    case DIFF_TUTORIAL:
        planer.setConstBonus(1000 * 1000); /* need to schedule 10 jobs ASAP, so premium does not really matter */
        break;
    case DIFF_FIRST:
        planer.setPassengerFactor(10 * 1000); /* need to fly as many passengers as possible */
        break;
    case DIFF_ADDON02:
        planer.setFreightBonus(5000 * 1000);
        break;
    case DIFF_ADDON04:
        // planer.setDistanceFactor(1); TODO
        break;
    case DIFF_ADDON06:
        // planer.setConstBonus(1000 * 1000); TODO
        break;
    case DIFF_ADDON09:
        planer.setUhrigBonus(1000 * 1000);
        break;
    case DIFF_ATFS03:
        if (qPlayer.Planes.GetNumUsed() >= 4) {
            planer.setDistanceFactor(-1);         /* prefer short flights */
            planer.setPassengerFactor(10 * 1000); /* need to fly as many passengers as possible */
        }
        break;
    default:
        break;
    }

    if (qPlayer.RobotUse(ROBOT_USE_FREE_FRACHT)) {
        planer.setFreeFreightBonus(5000 * 1000);
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

    /* check whether we will incur any fines */
    SLONG num = 0;
    mMoneyReservedForFines = 0;
    for (int i = 0; i < qPlayer.Auftraege.AnzEntries(); i++) {
        if (qPlayer.Auftraege.IsInAlbum(i) == 0) {
            continue;
        }
        const auto &job = qPlayer.Auftraege[i];
        if ((job.VonCity == job.NachCity) || (job.InPlan != 0) || (job.Praemie < 0)) {
            continue;
        }
        mMoneyReservedForFines += job.Strafe;
        num++;
    }
    for (int i = 0; i < qPlayer.Frachten.AnzEntries(); i++) {
        if (qPlayer.Frachten.IsInAlbum(i) == 0) {
            continue;
        }
        const auto &job = qPlayer.Frachten[i];
        if ((job.VonCity == job.NachCity) || (job.InPlan != 0) || (job.Praemie < 0)) {
            continue;
        }
        mMoneyReservedForFines += job.Strafe;
        num++;
    }
    if (mMoneyReservedForFines > 0) {
        redprintf("Bot::planFlights(): %ld jobs not planned, need to reserve %s $ for future fines, available money: %s $", num,
                  (LPCTSTR)Insert1000erDots64(mMoneyReservedForFines), (LPCTSTR)Insert1000erDots64(getMoneyAvailable()));
    }

    forceReplanning();
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
    if (mRoutes[lowUtil].routeUtilization < kMaximumRouteUtilization) {
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
        if (mRoutes[mRoutesSortedByUtilization[0]].routeUtilization >= kMaximumRouteUtilization) {
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
            if (qRoute.routeUtilization >= kMaximumRouteUtilization) {
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
        for (auto planeId : qRoute.planeIds) {
            const auto &qPlane = qPlayer.Planes[planeId];
            SLONG durationA = kDurationExtra + Cities.CalcFlugdauer(fromCity, toCity, qPlane.ptGeschwindigkeit);
            SLONG durationB = kDurationExtra + Cities.CalcFlugdauer(toCity, fromCity, qPlane.ptGeschwindigkeit);
            // TODO: SLONG roundTripDuration = durationA + durationB;
            // TODO: int timeSlot = 0;

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
            if (qRoute.routeUtilization < kMaximumRouteUtilization) {
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
