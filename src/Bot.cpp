#include "Bot.h"

#include "AtNet.h"

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

// Öffnungszeiten:
extern SLONG timeDutyOpen;
extern SLONG timeDutyClose;
extern SLONG timeArabOpen;
extern SLONG timeLastClose;
extern SLONG timeMuseOpen;
extern SLONG timeReisClose;
extern SLONG timeMaklClose;
extern SLONG timeWerbOpen;

static const SLONG kMoneyEmergencyFund = 100000;
static const SLONG kSmallestAdCampaign = 3;

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

static CString getWeekday(UWORD date) { return StandardTexte.GetS(TOKEN_SCHED, 3010 + (date + Sim.StartWeekday) % 7); }

bool Bot::hoursPassed(SLONG room, SLONG hours) {
    const auto it = mLastTimeInRoom.find(room);
    if (it == mLastTimeInRoom.end()) {
        return true;
    }
    return (Sim.Time - it->second > hours * 60000);
}

bool Bot::checkRoomOpen(SLONG roomId) {
    SLONG time = Sim.Time;
    switch (roomId) {
    case ACTION_VISITDUTYFREE:
        return (time >= timeDutyOpen && ((Sim.Weekday != 5 && Sim.Weekday != 6) || time <= timeDutyClose - 60000));
    case ACTION_BUY_KEROSIN:
        [[fallthrough]];
    case ACTION_BUY_KEROSIN_TANKS:
        return (time >= timeArabOpen && Sim.Weekday != 6);
    case ACTION_CHECKAGENT1:
        return (time <= timeLastClose - 60000 && Sim.Weekday != 5);
    case ACTION_BUYUSEDPLANE:
        return (time >= timeMuseOpen && Sim.Weekday != 5 && Sim.Weekday != 6);
    case ACTION_CHECKAGENT2:
        return (time <= timeReisClose - 60000);
    case ACTION_BUYNEWPLANE:
        return (time <= timeMaklClose - 60000);
    case ACTION_WERBUNG:
        [[fallthrough]];
    case ACTION_WERBUNG_ROUTES:
        return (Sim.Difficulty >= DIFF_NORMAL || Sim.Difficulty == DIFF_FREEGAME) && (time >= timeWerbOpen && Sim.Weekday != 5 && Sim.Weekday != 6);
    case ACTION_VISITSECURITY:
        [[fallthrough]];
    case ACTION_VISITSECURITY2:
        return (Sim.nSecOutDays <= 0);
    default:
        return true;
    }
    return true;
}

Bot::Prio Bot::condAll(SLONG actionId, __int64 moneyAvailable, SLONG dislike, SLONG bestPlaneTypeId) {
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
        return condVisitRouteBox(moneyAvailable);
    case ACTION_VISITSECURITY:
        return condVisitSecurity(moneyAvailable);
    case ACTION_VISITDESIGNER:
        return condVisitDesigner(moneyAvailable);
    case ACTION_VISITSECURITY2:
        return condSabotageSecurity();
    case ACTION_BUYUSEDPLANE:
        return condBuyUsedPlane(moneyAvailable);
    case ACTION_BUYNEWPLANE:
        return condBuyNewPlane(moneyAvailable, bestPlaneTypeId);
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
        return std::max(condBuyNemesisShares(moneyAvailable, dislike), condBuyOwnShares(moneyAvailable));
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

Bot::Prio Bot::condStartDay() {
    if (!hoursPassed(ACTION_STARTDAY, 24)) {
        return Prio::None;
    }
    return Prio::Top;
}

Bot::Prio Bot::condCallInternational() {
    if (!qPlayer.RobotUse(ROBOT_USE_ABROAD)) {
        return Prio::None;
    }
    if (qPlayer.RobotUse(ROBOT_USE_MUCH_FRACHT) && Sim.GetHour() <= 9) {
        return Prio::None;
    }
    if (!hoursPassed(ACTION_CALL_INTERNATIONAL, 2)) {
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
    if (!qPlayer.RobotUse(ROBOT_USE_FRACHT) || true) { // TODO
        return Prio::None;
    }
    if (!hoursPassed(ACTION_CHECKAGENT3, 4)) {
        return Prio::None;
    }
    return Prio::Medium;
}

Bot::Prio Bot::condUpgradePlanes(__int64 &moneyAvailable) {
    if (!qPlayer.RobotUse(ROBOT_USE_LUXERY)) {
        return Prio::None;
    }
    if (!hoursPassed(ACTION_UPGRADE_PLANES, 6)) {
        return Prio::None;
    }
    moneyAvailable -= 1000 * 1000;
    if (mPlanesForRoutes.size() > 0 && moneyAvailable >= 0) {
        return Prio::Medium;
    }
    return Prio::None;
}

Bot::Prio Bot::condBuyNewPlane(__int64 &moneyAvailable, SLONG bestPlaneTypeId) {
    if (!qPlayer.RobotUse(ROBOT_USE_MAKLER)) {
        return Prio::None;
    }
    if (!hoursPassed(ACTION_BUYNEWPLANE, 6)) {
        return Prio::None;
    }
    if (bestPlaneTypeId < 0) {
        return Prio::None;
    }
    int numPlanes = mPlanesForJobs.size() + mPlanesForRoutes.size();
    if (qPlayer.RobotUse(ROBOT_USE_MAX4PLANES) && numPlanes >= 4) {
        return Prio::None;
    }
    if (qPlayer.RobotUse(ROBOT_USE_MAX5PLANES) && numPlanes >= 5) {
        return Prio::None;
    }
    if (qPlayer.RobotUse(ROBOT_USE_MAX10PLANES) && numPlanes >= 10) {
        return Prio::None;
    }
    const auto &bestPlaneType = PlaneTypes[bestPlaneTypeId];
    moneyAvailable -= bestPlaneType.Preis;
    return (moneyAvailable >= 0) ? Prio::Medium : Prio::None;
}

Bot::Prio Bot::condVisitHR() {
    if (!hoursPassed(ACTION_PERSONAL, 24)) {
        return Prio::None;
    }
    return Prio::Medium;
}

Bot::Prio Bot::condBuyKerosine(__int64 &moneyAvailable) {
    if (!qPlayer.RobotUse(ROBOT_USE_PETROLAIR)) {
        return Prio::None;
    }
    if (qPlayer.HasBerater(BERATERTYP_KEROSIN) < 30) {
        return Prio::None;
    }
    if (!hoursPassed(ACTION_BUY_KEROSIN, 4)) {
        return Prio::None;
    }
    moneyAvailable -= 200 * 1000;
    if (moneyAvailable >= 0) {
        return Prio::Medium;
    }
    return Prio::None;
}

Bot::Prio Bot::condBuyKerosineTank(__int64 &moneyAvailable) {
    if (!qPlayer.RobotUse(ROBOT_USE_PETROLAIR)) {
        return Prio::None;
    }
    if (!qPlayer.RobotUse(ROBOT_USE_TANKS) || qPlayer.HasBerater(BERATERTYP_KEROSIN) < 30) {
        return Prio::None;
    }
    if (!hoursPassed(ACTION_BUY_KEROSIN_TANKS, 24)) {
        return Prio::None;
    }
    if (mTanksFilledYesterday - mTanksFilledToday < 0.5 && !mTankWasEmpty) {
        return Prio::None;
    }
    moneyAvailable -= TankPrice[1];
    return (moneyAvailable >= 0) ? Prio::Medium : Prio::None;
}

Bot::Prio Bot::condSabotage(__int64 & /*moneyAvailable*/) {
    if (!hoursPassed(ACTION_SABOTAGE, 24)) {
        return Prio::None;
    }
    return Prio::None;
}

Bot::Prio Bot::condIncreaseDividend(__int64 &moneyAvailable) {
    if (!hoursPassed(ACTION_SET_DIVIDEND, 24)) {
        return Prio::None;
    }
    moneyAvailable -= 100 * 1000;
    if (qPlayer.RobotUse(ROBOT_USE_HIGHSHAREPRICE) || (moneyAvailable >= 0)) {
        return Prio::Medium;
    }
    return Prio::None;
}
Bot::Prio Bot::condRaiseMoney(__int64 & /*moneyAvailable*/) {
    if (!hoursPassed(ACTION_RAISEMONEY, 1)) {
        return Prio::None;
    }
    if (qPlayer.CalcCreditLimit() < 1000) {
        return Prio::None;
    }
    if (qPlayer.Money < 0) {
        return Prio::Top;
    }
    return Prio::None;
}
Bot::Prio Bot::condDropMoney(__int64 &moneyAvailable) {
    if (!hoursPassed(ACTION_DROPMONEY, 24)) {
        return Prio::None;
    }
    moneyAvailable -= 1500 * 1000;
    if (moneyAvailable >= 0 && qPlayer.Credit > 0) {
        return Prio::Medium;
    }
    return Prio::None;
}
Bot::Prio Bot::condEmitShares(__int64 & /*moneyAvailable*/) {
    if (!hoursPassed(ACTION_EMITSHARES, 24)) {
        return Prio::None;
    }
    if (GameMechanic::canEmitStock(qPlayer) != GameMechanic::EmitStockResult::Ok) {
        return Prio::None;
    }
    return Prio::Medium;
}
Bot::Prio Bot::condSellShares(__int64 &moneyAvailable) {
    if (!hoursPassed(ACTION_SELLSHARES, 1)) {
        return Prio::None;
    }
    if (qPlayer.Money <= DEBT_LIMIT / 2) {
        if (qPlayer.CalcCreditLimit() >= 1000) {
            return Prio::High; /* first take out credit */
        }
        return Prio::Top;
    }
    if (moneyAvailable - qPlayer.Credit <= 0) {
        return Prio::High;
    }
    return Prio::None;
}

Bot::Prio Bot::condBuyOwnShares(__int64 &moneyAvailable) {
    if (qPlayer.RobotUse(ROBOT_USE_DONTBUYANYSHARES)) {
        return Prio::None;
    }
    if (!hoursPassed(ACTION_BUYSHARES, 6)) {
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
    if (qPlayer.RobotUse(ROBOT_USE_DONTBUYANYSHARES)) {
        return Prio::None;
    }
    if (!hoursPassed(ACTION_BUYSHARES, 6)) {
        return Prio::None;
    }
    moneyAvailable -= 2000 * 1000;
    if ((dislike != -1) && (moneyAvailable >= 0) && (qPlayer.Credit == 0)) {
        return Prio::Low;
    }
    return Prio::None;
}

Bot::Prio Bot::condVisitMech(__int64 & /*moneyAvailable*/) {
    if (!hoursPassed(ACTION_VISITMECH, 6)) {
        return Prio::None;
    }
    return Prio::Low;
}

Bot::Prio Bot::condVisitNasa(__int64 & /*moneyAvailable*/) {
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

Bot::Prio Bot::condBuyUsedPlane(__int64 & /*moneyAvailable*/) { return Prio::None; }

Bot::Prio Bot::condVisitDutyFree(__int64 &moneyAvailable) {
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

Bot::Prio Bot::condVisitRouteBox(__int64 & /*moneyAvailable*/) {
    if (!qPlayer.RobotUse(ROBOT_USE_ROUTEBOX)) {
        return Prio::None;
    }
    if (!hoursPassed(ACTION_WERBUNG_ROUTES, 24)) {
        return Prio::None;
    }
    if (mWantToBuyNewRoute) {
        return Prio::Medium;
    }
    return Prio::None;
}

Bot::Prio Bot::condVisitSecurity(__int64 & /*moneyAvailable*/) {
    if (!qPlayer.RobotUse(ROBOT_USE_SECURTY_OFFICE)) {
        return Prio::None;
    }
    return Prio::None;
}

Bot::Prio Bot::condSabotageSecurity() {
    if (!qPlayer.RobotUse(ROBOT_USE_SECURTY_OFFICE)) {
        return Prio::None;
    }
    if (qPlayer.HasItem(ITEM_ZANGE) == 0) {
        return Prio::None;
    }
    return Prio::Low;
}

Bot::Prio Bot::condVisitDesigner(__int64 & /*moneyAvailable*/) {
    if (!qPlayer.RobotUse(ROBOT_USE_DESIGNER)) {
        return Prio::None;
    }
    return Prio::None;
}

Bot::Prio Bot::condBuyAdsForRoutes(__int64 &moneyAvailable) {
    if (!qPlayer.RobotUse(ROBOT_USE_WERBUNG)) {
        return Prio::None;
    }
    if (!hoursPassed(ACTION_WERBUNG_ROUTES, 4)) {
        return Prio::None;
    }
    auto minCost = gWerbePrice[1 * 6 + kSmallestAdCampaign];
    moneyAvailable -= minCost;
    if (moneyAvailable < 0) {
        return Prio::None;
    }
    if (mRoutesSortedByUtilization.size() > 0) {
        return (mRoutesSortedByUtilization[0].utilization < 50) ? Prio::High : Prio::Medium;
    }
    return Prio::None;
}
Bot::Prio Bot::condBuyAds(__int64 &moneyAvailable) {
    if (!qPlayer.RobotUse(ROBOT_USE_WERBUNG)) {
        return Prio::None;
    }
    if (!hoursPassed(ACTION_WERBUNG, 4)) {
        return Prio::None;
    }
    auto minCost = gWerbePrice[0 * 6 + kSmallestAdCampaign];
    moneyAvailable -= minCost;
    if (moneyAvailable < 0) {
        return Prio::None;
    }
    if (qPlayer.Image < 0 || ((mDoRoutes || mWantToBuyNewRoute) && qPlayer.Image < 300)) {
        return Prio::Medium;
    }
    auto imageDelta = minCost / 10000 * (kSmallestAdCampaign + 6) / 55;
    if ((mDoRoutes || mWantToBuyNewRoute) && qPlayer.Image < (1000 - imageDelta)) {
        return Prio::Medium;
    }
    return Prio::None;
}

std::pair<SLONG, SLONG> Bot::kerosineQualiOptimization(__int64 moneyAvailable, DOUBLE targetFillRatio) {
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
        res.first = static_cast<SLONG>(std::floor(amountGood));
        res.second = static_cast<SLONG>(std::floor(amountBad));
    }

    return res;
}

SLONG Bot::findBestAvailablePlaneType() {
    CDataTable planeTable;
    planeTable.FillWithPlaneTypes();
    SLONG bestId = -1;
    SLONG bestScore = 0;
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

        if (score > bestScore) {
            bestScore = score;
            bestId = i;
        }
    }
    const auto &bestPlaneType = PlaneTypes[bestId];
    hprintf("Bot::findBestAvailablePlaneType(): Best plane type is %s", (LPCTSTR)bestPlaneType.Name);
    return bestId;
}

SLONG Bot::calcNumberOfShares(__int64 moneyAvailable, DOUBLE kurs) { return static_cast<SLONG>(std::floor((moneyAvailable - 100) / (1.1 * kurs))); }

SLONG Bot::calcNumOfFreeShares(SLONG playerId) {
    auto &player = Sim.Players.Players[playerId];
    SLONG amount = player.AnzAktien;
    for (SLONG c = 0; c < 4; c++) {
        amount -= Sim.Players.Players[c].OwnsAktien[playerId];
    }
    return amount;
}

SLONG Bot::checkFlightJobs() {
    auto &qAuftraege = qPlayer.Auftraege;
    int nIncorrect = 0;
    for (SLONG c = 0; c < qPlayer.Planes.AnzEntries(); c++) {
        if (qPlayer.Planes.IsInAlbum(c) == 0) {
            continue;
        }

        auto &qPlane = qPlayer.Planes[c];
        CFlugplan *Plan = &qPlane.Flugplan;

        for (SLONG d = Plan->Flug.AnzEntries() - 1; d >= 0; d--) {
            if (Plan->Flug[d].ObjectType != 2) {
                continue;
            }

            auto &qAuftrag = qAuftraege[Plan->Flug[d].ObjectId];

            if (Plan->Flug[d].Okay != 0) {
                nIncorrect++;
                redprintf("Bot::checkFlightJobs(): Job (%s -> %s) for plane %s is not scheduled correctly", (LPCTSTR)Cities[qAuftrag.VonCity].Kuerzel,
                          (LPCTSTR)Cities[qAuftrag.NachCity].Kuerzel, (LPCTSTR)qPlane.Name);
            }

            if (qPlane.ptPassagiere < SLONG(qAuftrag.Personen)) {
                redprintf("Bot::checkFlightJobs(): Job (%s -> %s) exceeds number of seats for plane %s", (LPCTSTR)Cities[qAuftrag.VonCity].Kuerzel,
                          (LPCTSTR)Cities[qAuftrag.NachCity].Kuerzel, (LPCTSTR)qPlane.Name);
            }

            if (Plan->Flug[d].Startdate < qAuftrag.Date) {
                redprintf("Bot::checkFlightJobs(): Job (%s -> %s) starts too early (%s instead of %s) for plane %s", (LPCTSTR)Cities[qAuftrag.VonCity].Kuerzel,
                          (LPCTSTR)Cities[qAuftrag.NachCity].Kuerzel, (LPCTSTR)getWeekday(Plan->Flug[d].Startdate), (LPCTSTR)getWeekday(qAuftrag.Date),
                          (LPCTSTR)qPlane.Name);
            }

            if (Plan->Flug[d].Startdate > qAuftrag.BisDate) {
                redprintf("Bot::checkFlightJobs(): Job (%s -> %s) starts too late (%s instead of %s) for plane %s", (LPCTSTR)Cities[qAuftrag.VonCity].Kuerzel,
                          (LPCTSTR)Cities[qAuftrag.NachCity].Kuerzel, (LPCTSTR)getWeekday(Plan->Flug[d].Startdate), (LPCTSTR)getWeekday(qAuftrag.BisDate),
                          (LPCTSTR)qPlane.Name);
            }
        }
    }
    return nIncorrect;
}

Bot::Bot(PLAYER &player) : qPlayer(player) {}

void Bot::RobotInit() {
    hprintf("Bot.cpp: Enter RobotInit()");

    auto &qRobotActions = qPlayer.RobotActions;

    if (mFirstRun) {
        for (SLONG i = 0; i < qPlayer.Planes.AnzEntries(); i++) {
            if (qPlayer.Planes.IsInAlbum(i)) {
                mPlanesForJobs.push_back(i);
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

    mRoutesSortedByUtilization.clear();
    for (SLONG i = 0; i < qPlayer.RentRouten.RentRouten.AnzEntries(); i++) {
        if ((qPlayer.RentRouten.RentRouten[i].Rang != 0U)) {
            mRoutesSortedByUtilization.emplace_back(i, qPlayer.RentRouten.RentRouten[i].Auslastung);
        }
    }
    std::sort(mRoutesSortedByUtilization.begin(), mRoutesSortedByUtilization.end(),
              [](const RouteInfo &a, const RouteInfo &b) { return a.utilization < b.utilization; });

    RobotPlan();
}

void Bot::RobotPlan() {
    hprintf("Bot.cpp: Enter RobotPlan()");

    auto &qRobotActions = qPlayer.RobotActions;

    SLONG actions[] = {ACTION_STARTDAY,       ACTION_CALL_INTERNATIONAL, ACTION_CHECKAGENT1,    ACTION_CHECKAGENT2,   ACTION_CHECKAGENT3,
                       ACTION_UPGRADE_PLANES, ACTION_BUYNEWPLANE,        ACTION_PERSONAL,       ACTION_BUY_KEROSIN,   ACTION_BUY_KEROSIN_TANKS,
                       ACTION_SABOTAGE,       ACTION_SET_DIVIDEND,       ACTION_RAISEMONEY,     ACTION_DROPMONEY,     ACTION_EMITSHARES,
                       ACTION_SELLSHARES,     ACTION_BUYSHARES,          ACTION_VISITMECH,      ACTION_VISITNASA,     ACTION_VISITTELESCOPE,
                       ACTION_VISITRICK,      ACTION_VISITKIOSK,         ACTION_BUYUSEDPLANE,   ACTION_VISITDUTYFREE, ACTION_VISITAUFSICHT,
                       ACTION_VISITROUTEBOX,  ACTION_VISITSECURITY,      ACTION_VISITSECURITY2, ACTION_VISITDESIGNER, ACTION_WERBUNG_ROUTES,
                       ACTION_WERBUNG};

    if (qRobotActions[0].ActionId != ACTION_NONE || qRobotActions[1].ActionId != ACTION_NONE) {
        return;
    }
    qRobotActions[1].ActionId = ACTION_NONE;
    qRobotActions[2].ActionId = ACTION_NONE;

    /* data to make decision */
    mDislike = -1;
    mBestPlaneTypeId = findBestAvailablePlaneType();
    __int64 moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;

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
        if (!checkRoomOpen(action)) {
            continue;
        }
        auto prio = condAll(action, moneyAvailable, mDislike, mBestPlaneTypeId);
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
    for (const auto &i : prioList) {
        greenprintf("Bot.cpp: %s: %s", getRobotActionName(i.first), getPrioName(i.second));
    }

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
    while (prioList[startIdx].second == prioList[endIdx].second) {
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

    greenprintf("Action 1: %s with prio %s", getRobotActionName(qRobotActions[1].ActionId), getPrioName(prioAction1));
    greenprintf("Action 2: %s with prio %s", getRobotActionName(qRobotActions[2].ActionId), getPrioName(prioAction2));

    hprintf("Bot.cpp: Leaving RobotPlan()\n");
}

void Bot::RobotExecuteAction() {
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
                if (moneyAvailable >= moneyNeeded) {
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
                if (n == Cities.find(Sim.HomeAirportId)) {
                    continue;
                }

                if ((Sim.Players.Players[Sim.localPlayer].LocationWin != nullptr) &&
                    (Sim.Players.Players[Sim.localPlayer].LocationWin)->CurrentMenu == MENU_AUSLANDSAUFTRAG &&
                    Cities.GetIdFromIndex((Sim.Players.Players[Sim.localPlayer].LocationWin)->MenuPar1) == Cities.GetIdFromIndex(n)) {
                    continue; // Skip it, because Player is just phoning it.
                }

                bool goAhead = false;
                if (qPlayer.RentCities.RentCities[n].Rang != 0U) {
                    goAhead = true;
                }
                for (SLONG c = 0; c < 4; c++) {
                    if ((Sim.Players.Players[c].IsOut == 0) && (qPlayer.Kooperation[c] != 0) && (Sim.Players.Players[c].RentCities.RentCities[n].Rang != 0U)) {
                        goAhead = true;
                        break;
                    }
                }
                if (!goAhead) {
                    continue; // Skip that city, 'cause we're not present here
                }

                GameMechanic::refillFlightJobs(n);
                cities.push_back(n);
            }

            if (!cities.empty()) {
                // Normale Aufträge:
                BotPlaner planer(qPlayer, qPlanes, BotPlaner::JobOwner::International, cities);
                auto oldGain = mCurrentGain;
                mCurrentGain = planer.planFlights(mPlanesForJobs);
                hprintf("Gain from flight jobs changed %ld => %ld", oldGain, mCurrentGain);
                checkFlightJobs();

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

            BotPlaner planer(qPlayer, qPlanes, BotPlaner::JobOwner::LastMinute, {});
            auto oldGain = mCurrentGain;
            mCurrentGain = planer.planFlights(mPlanesForJobs);
            hprintf("Gain from flight jobs changed %ld => %ld", oldGain, mCurrentGain);
            checkFlightJobs();

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

            BotPlaner planer(qPlayer, qPlanes, BotPlaner::JobOwner::TravelAgency, {});
            auto oldGain = mCurrentGain;
            mCurrentGain = planer.planFlights(mPlanesForJobs);
            hprintf("Gain from flight jobs changed %ld => %ld", oldGain, mCurrentGain);
            checkFlightJobs();

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

            BotPlaner planer(qPlayer, qPlanes, BotPlaner::JobOwner::Freight, {});
            auto oldGain = mCurrentGain;
            mCurrentGain = planer.planFlights(mPlanesForJobs);
            hprintf("Gain from flight jobs changed %ld => %ld", oldGain, mCurrentGain);
            checkFlightJobs();

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

                    if (qPlane.SitzeTarget < 2 && SeatCosts[2] <= moneyAvailable) {
                        qPlane.SitzeTarget = 2;
                        moneyAvailable -= SeatCosts[qPlane.SitzeTarget];
                        hprintf("Bot::RobotExecuteAction(): Upgrading seats in %s.", (LPCTSTR)qPlane.Name);
                        continue;
                    }

                    if (qPlane.TablettsTarget < 2 && TrayCosts[2] <= moneyAvailable) {
                        qPlane.TablettsTarget = 2;
                        moneyAvailable -= TrayCosts[qPlane.TablettsTarget];
                        hprintf("Bot::RobotExecuteAction(): Upgrading tabletts in %s.", (LPCTSTR)qPlane.Name);
                        continue;
                    }

                    if (qPlane.DecoTarget < 2 && DecoCosts[2] <= moneyAvailable) {
                        qPlane.DecoTarget = 2;
                        moneyAvailable -= DecoCosts[qPlane.DecoTarget];
                        hprintf("Bot::RobotExecuteAction(): Upgrading deco in %s.", (LPCTSTR)qPlane.Name);
                        continue;
                    }

                    if (qPlane.ReifenTarget < 2 && ReifenCosts[2] <= moneyAvailable) {
                        qPlane.ReifenTarget = 2;
                        moneyAvailable -= ReifenCosts[qPlane.ReifenTarget];
                        hprintf("Bot::RobotExecuteAction(): Upgrading tires in %s.", (LPCTSTR)qPlane.Name);
                        continue;
                    }

                    if (qPlane.TriebwerkTarget < 2 && TriebwerkCosts[2] <= moneyAvailable) {
                        qPlane.TriebwerkTarget = 2;
                        moneyAvailable -= TriebwerkCosts[qPlane.TriebwerkTarget];
                        hprintf("Bot::RobotExecuteAction(): Upgrading engines in %s.", (LPCTSTR)qPlane.Name);
                        continue;
                    }

                    if (qPlane.SicherheitTarget < 2 && SicherheitCosts[2] <= moneyAvailable) {
                        qPlane.SicherheitTarget = 2;
                        moneyAvailable -= SicherheitCosts[qPlane.SicherheitTarget];
                        hprintf("Bot::RobotExecuteAction(): Upgrading safety in %s.", (LPCTSTR)qPlane.Name);
                        continue;
                    }

                    if (qPlane.ElektronikTarget < 2 && ElektronikCosts[2] <= moneyAvailable) {
                        qPlane.ElektronikTarget = 2;
                        moneyAvailable -= ElektronikCosts[qPlane.ElektronikTarget];
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
        if (condBuyNewPlane(moneyAvailable, mBestPlaneTypeId) != Prio::None) {
            hprintf("Bot::RobotExecuteAction(): Buying plane %s", PlaneTypes[mBestPlaneTypeId].Name);
            GameMechanic::buyPlane(qPlayer, mBestPlaneTypeId, 1);
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
            if (mBestPlaneTypeId >= 0) {
                const auto &bestPlaneType = PlaneTypes[mBestPlaneTypeId];
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
                } else if (qWorker.Typ == WORKER_STEWARDESS && qPlayer.xBegleiter < stewardessTarget) {
                    GameMechanic::hireWorker(qPlayer, c);
                }
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
                moneyToSpend = (moneyAvailable - 200 * 1000);
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
            __int64 m = min(qPlayer.Credit, moneyAvailable - 1500 * 1000);
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
            SLONG amountCanAfford = calcNumberOfShares((moneyAvailable - 2000 * 1000), qDislikedPlayer.Kurse[0]);
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
            if (moneyAvailable > 6000 * 1000 && (qPlayer.Credit == 0)) {
                SLONG amount = calcNumOfFreeShares(PlayerNum);
                SLONG amountToBuy = qPlayer.AnzAktien / 4 - qOwnsAktien[PlayerNum];
                SLONG amountCanAfford = calcNumberOfShares((moneyAvailable - 6000 * 1000), qKurse[0]);
                amount = min(amount, amountToBuy);
                amount = min(amount, amountCanAfford);

                if (amount > 0) {
                    hprintf("Bot::RobotExecuteAction(): Buying own stock: %ld", amount);
                    GameMechanic::buyStock(qPlayer, PlayerNum, amount);
                    moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
                }
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
        if (condVisitRouteBox(moneyAvailable) != Prio::None) {
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
            for (const auto &qRoute : mRoutesSortedByUtilization) {
                for (SLONG adCampaignSize = 5; adCampaignSize >= kSmallestAdCampaign; adCampaignSize--) {
                    SLONG cost = gWerbePrice[1 * 6 + adCampaignSize];
                    if (cost > moneyAvailable) {
                        continue;
                    }
                    if (qRoute.utilization >= 90) {
                        continue;
                    }
                    SLONG image = qRentRouten[qRoute.routeId].Image;
                    if (image + (cost / 30000) > 100) {
                        continue;
                    }
                    hprintf("Bot::RobotExecuteAction(): Buying advertisement for route %s - %s for %ld $", Cities[Routen[qRoute.routeId].VonCity].Kuerzel,
                            Cities[Routen[qRoute.routeId].NachCity].Kuerzel, cost);
                    GameMechanic::buyAdvertisement(qPlayer, 1, adCampaignSize, qRoute.routeId);
                    moneyAvailable = qPlayer.Money - kMoneyEmergencyFund;
                    hprintf("Bot::RobotExecuteAction(): Route image improved (%ld => %ld)", image, static_cast<SLONG>(qRentRouten[qRoute.routeId].Image));
                }
            }
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
        DebugBreak();
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

    File << bot.mDislike;
    File << bot.mBestPlaneTypeId;
    File << bot.mFirstRun;
    File << bot.mDoRoutes;
    File << bot.mWantToBuyNewRoute;
    File << bot.mSavingForPlane;
    File << bot.mOutOfGates;

    File << bot.mBossNumCitiesAvailable;
    File << bot.mBossGateAvailable;

    File << bot.mTanksFilledYesterday;
    File << bot.mTanksFilledToday;
    File << bot.mTankWasEmpty;

    File << bot.mCurrentGain;

    File << static_cast<SLONG>(bot.mRoutesSortedByUtilization.size());
    for (const auto &i : bot.mRoutesSortedByUtilization) {
        File << i.routeId << i.utilization;
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

    File >> bot.mDislike;
    File >> bot.mBestPlaneTypeId;
    File >> bot.mFirstRun;
    File >> bot.mDoRoutes;
    File >> bot.mWantToBuyNewRoute;
    File >> bot.mSavingForPlane;
    File >> bot.mOutOfGates;

    File >> bot.mBossNumCitiesAvailable;
    File >> bot.mBossGateAvailable;

    File >> bot.mTanksFilledYesterday;
    File >> bot.mTanksFilledToday;
    File >> bot.mTankWasEmpty;

    File >> bot.mCurrentGain;

    File >> size;
    bot.mRoutesSortedByUtilization.resize(size);
    for (SLONG i = 0; i < size; i++) {
        Bot::RouteInfo info;
        File >> info.routeId >> info.utilization;
        bot.mRoutesSortedByUtilization[i] = info;
    }

    SLONG magicnumber = 0;
    File >> magicnumber;
    assert(magicnumber == 0x42);

    return (File);
}
