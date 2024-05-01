#include "Bot.h"

#include "BotHelper.h"
#include "class.h"
#include "GameMechanic.h"
#include "global.h"
#include "TeakLibW.h"

// Preise verstehen sich pro Sitzplatz:
extern SLONG SeatCosts[];

bool Bot::isOfficeUsable() const { return (qPlayer.OfficeState != 2); }

bool Bot::hoursPassed(SLONG room, SLONG hours) const {
    const auto it = mLastTimeInRoom.find(room);
    if (it == mLastTimeInRoom.end()) {
        return true;
    }
    return (Sim.Time - it->second > hours * 60000);
}

void Bot::grabNewFlights() {
    mLastTimeInRoom.erase(ACTION_CALL_INTERNATIONAL);
    mLastTimeInRoom.erase(ACTION_CALL_INTER_HANDY);
    mLastTimeInRoom.erase(ACTION_CHECKAGENT1);
    mLastTimeInRoom.erase(ACTION_CHECKAGENT2);
    mLastTimeInRoom.erase(ACTION_CHECKAGENT3);
}

bool Bot::haveDiscount() const {
    if (qPlayer.HasBerater(BERATERTYP_SICHERHEIT) >= 50 || Sim.Date > 7) {
        return true; /* wait until we have some discount */
    }
    return false;
}

Bot::HowToPlan Bot::canWePlanFlights() {
    if (!mDayStarted) {
        return HowToPlan::None;
    }
    if (qPlayer.HasItem(ITEM_LAPTOP)) {
        if ((qPlayer.LaptopVirus == 1) && (qPlayer.HasItem(ITEM_DISKETTE) == 1)) {
            GameMechanic::useItem(qPlayer, ITEM_DISKETTE);
        }
        if (qPlayer.LaptopVirus == 0) {
            return HowToPlan::Laptop;
        }
    }
    if (isOfficeUsable()) {
        return HowToPlan::Office;
    }
    return HowToPlan::None;
}

__int64 Bot::getMoneyAvailable() const {
    return qPlayer.Money - mMoneyReservedForRepairs - mMoneyReservedForUpgrades - mMoneyReservedForAuctions - kMoneyEmergencyFund;
}

Bot::AreWeBroke Bot::areWeBroke() const {
    if (mRunToFinalObjective == FinalPhase::TargetRun) {
        return AreWeBroke::Desperate;
    }

    auto moneyAvailable = getMoneyAvailable();
    if (moneyAvailable < 0) {
        return AreWeBroke::Yes;
    }

    /* no reason to get as much money as possible right now */
    if (moneyAvailable < qPlayer.Credit) {
        return AreWeBroke::Somewhat;
    }
    return AreWeBroke::No;
}

Bot::HowToGetMoney Bot::howToGetMoney() {
    auto broke = areWeBroke();
    if (broke == AreWeBroke::No) {
        return HowToGetMoney::None;
    }

    SLONG numShares = 0;
    SLONG numOwnShares = 0;
    for (SLONG c = 0; c < Sim.Players.AnzPlayers; c++) {
        if (c == qPlayer.PlayerNum) {
            numOwnShares += qPlayer.OwnsAktien[c];
        } else {
            numShares += qPlayer.OwnsAktien[c];
        }
    }

    if (broke < AreWeBroke::Desperate) {
        /* do not sell all shares to prevent hostile takeover */
        numOwnShares = std::max(0, numOwnShares - qPlayer.AnzAktien / 2 - 1);
    }

    if (broke == AreWeBroke::Somewhat) {
        return (numShares > 0) ? HowToGetMoney::SellShares : HowToGetMoney::None;
    }

    if (GameMechanic::canEmitStock(qPlayer) == GameMechanic::EmitStockResult::Ok) {
        return HowToGetMoney::EmitShares;
    }
    if (numShares > 0) {
        return HowToGetMoney::SellShares;
    }
    if (numOwnShares > 0) {
        return (broke == AreWeBroke::Desperate) ? HowToGetMoney::SellAllOwnShares : HowToGetMoney::SellOwnShares;
    }
    if (qPlayer.CalcCreditLimit() >= 1000) {
        return HowToGetMoney::IncreaseCredit;
    }
    return HowToGetMoney::None;
}

__int64 Bot::howMuchMoneyCanWeGet(bool extremMeasures) {
    __int64 valueCompetitorShares = 0;
    SLONG numOwnShares = 0;
    for (SLONG c = 0; c < Sim.Players.AnzPlayers; c++) {
        if (c == qPlayer.PlayerNum) {
            numOwnShares += qPlayer.OwnsAktien[c];
        } else {
            auto stockPrice = static_cast<__int64>(Sim.Players.Players[c].Kurse[0]);
            valueCompetitorShares += stockPrice * qPlayer.OwnsAktien[c];
        }
    }

    if (!extremMeasures) {
        numOwnShares = std::max(0, numOwnShares - qPlayer.AnzAktien / 2 - 1);
    }

    __int64 moneyEmit = 0;
    __int64 moneyStock = 0;
    __int64 moneyStockOwn = 0;
    auto stockPrice = static_cast<__int64>(qPlayer.Kurse[0]);
    if (GameMechanic::canEmitStock(qPlayer) == GameMechanic::EmitStockResult::Ok) {
        __int64 newStock = (qPlayer.MaxAktien - qPlayer.AnzAktien) / 100 * 100;
        __int64 emissionsKurs = 0;
        __int64 marktAktien = 0;
        if (kStockEmissionMode == 0) {
            emissionsKurs = stockPrice - 5;
            marktAktien = newStock;
        } else if (kStockEmissionMode == 1) {
            emissionsKurs = stockPrice - 3;
            marktAktien = newStock * 8 / 10;
        } else if (kStockEmissionMode == 2) {
            emissionsKurs = stockPrice - 1;
            marktAktien = newStock * 6 / 10;
        }
        moneyEmit = marktAktien * emissionsKurs - newStock * emissionsKurs / 10 / 100 * 100;
        hprintf("Bot::howMuchMoneyCanWeGet(): Can get %s $ by emitting stock", (LPCTSTR)Insert1000erDots(moneyEmit));
    }

    if (valueCompetitorShares > 0) {
        moneyStock = valueCompetitorShares - valueCompetitorShares / 10 - 100;
        hprintf("Bot::howMuchMoneyCanWeGet(): Can get %s $ by selling stock", (LPCTSTR)Insert1000erDots(moneyStock));
    }

    if (numOwnShares > 0) {
        auto value = stockPrice * numOwnShares;
        moneyStockOwn = value - value / 10 - 100;
        hprintf("Bot::howMuchMoneyCanWeGet(): Can get %s $ by selling our own stock", (LPCTSTR)Insert1000erDots(moneyStockOwn));
    }

    __int64 moneyForecast = qPlayer.Money + moneyEmit + moneyStock + moneyStockOwn - kMoneyEmergencyFund;
    __int64 credit = qPlayer.CalcCreditLimit(moneyForecast, qPlayer.Credit);
    if (credit >= 1000) {
        moneyForecast += credit;
        hprintf("Bot::howMuchMoneyCanWeGet(): Can get %s $ by taking a loan", (LPCTSTR)Insert1000erDots(credit));
    }

    hprintf("Bot::howMuchMoneyCanWeGet(): We can get %s $ in total!", (LPCTSTR)Insert1000erDots(moneyForecast));
    return moneyForecast;
}

bool Bot::canWeCallInternational() {
    if (!qPlayer.RobotUse(ROBOT_USE_ABROAD)) {
        return false;
    }

    if (qPlayer.TelephoneDown != 0) {
        return false;
    }

    if (mPlanesForJobs.empty()) {
        return false; /* no planes */
    }
    if (!mPlanerSolution.empty()) {
        return false; /* previously grabbed flights still not scheduled */
    }

    auto res = canWePlanFlights();
    if (HowToPlan::None == res) {
        return false;
    }
    if (HowToPlan::Office == res && Sim.GetHour() >= 17) {
        return false; /* might be too late to reach office */
    }

    for (SLONG c = 0; c < 4; c++) {
        if ((Sim.Players.Players[c].IsOut == 0) && (qPlayer.Kooperation[c] != 0)) {
            return true; /* we have a cooperation partner, we can check if they have a branch office */
        }
    }
    for (SLONG n = 0; n < Cities.AnzEntries(); n++) {
        if (n == Cities.find(Sim.HomeAirportId)) {
            continue;
        }
        if (qPlayer.RentCities.RentCities[n].Rang != 0U) {
            return true; /* we have our own branch office */
        }
    }
    return false;
}

Bot::Prio Bot::condAll(SLONG actionId) {
    __int64 moneyAvailable = 0;
    switch (actionId) {
    case ACTION_RAISEMONEY:
        return condTakeOutLoan();
    case ACTION_DROPMONEY:
        return condDropMoney(moneyAvailable);
    case ACTION_EMITSHARES:
        return condEmitShares();
    case ACTION_CHECKAGENT1:
        return condCheckLastMinute();
    case ACTION_CHECKAGENT2:
        return condCheckTravelAgency();
    case ACTION_CHECKAGENT3:
        return condCheckFreight();
    case ACTION_STARTDAY:
        return condStartDay();
    case ACTION_BUERO:
        return condBuero();
    case ACTION_PERSONAL:
        return condVisitHR();
    case ACTION_VISITKIOSK:
        return condVisitMisc();
    case ACTION_VISITMECH:
        return condVisitMech();
    case ACTION_VISITMUSEUM:
        return condVisitMuseum();
    case ACTION_VISITDUTYFREE:
        return condVisitDutyFree(moneyAvailable);
    case ACTION_VISITAUFSICHT:
        return condVisitBoss(moneyAvailable);
    case ACTION_VISITNASA:
        return condVisitNasa(moneyAvailable);
    case ACTION_VISITTELESCOPE:
        return condVisitMisc();
    case ACTION_VISITMAKLER:
        return condVisitMakler();
    case ACTION_VISITARAB:
        return condVisitArab();
    case ACTION_VISITRICK:
        return condVisitRick();
    case ACTION_VISITROUTEBOX:
        return condVisitRouteBoxPlanning();
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
        return condUpgradePlanes();
    case ACTION_BUY_KEROSIN:
        return condBuyKerosine(moneyAvailable);
    case ACTION_BUY_KEROSIN_TANKS:
        return condBuyKerosineTank(moneyAvailable);
    case ACTION_SET_DIVIDEND:
        return condIncreaseDividend(moneyAvailable);
    case ACTION_BUYSHARES:
        return condBuyShares(moneyAvailable, mNemesis);
    case ACTION_SELLSHARES:
        return condSellShares(moneyAvailable);
    case ACTION_WERBUNG_ROUTES:
        return condBuyAdsForRoutes(moneyAvailable);
    case ACTION_CALL_INTERNATIONAL:
        return condCallInternational();
    case ACTION_VISITROUTEBOX2:
        return condVisitRouteBoxRenting(moneyAvailable);
    case ACTION_EXPANDAIRPORT:
        return condExpandAirport(moneyAvailable);
    case ACTION_CALL_INTER_HANDY:
        return condCallInternationalHandy();
    case ACTION_STARTDAY_LAPTOP:
        return condStartDayLaptop();
    case ACTION_VISITADS:
        return condVisitAds();
    default:
        redprintf("Bot.cpp: Default case should not be reached.");
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
    /* not necesary to check hoursPassed() */
    if (mDayStarted || !isOfficeUsable()) {
        return Prio::None;
    }
    return Prio::Top;
}

Bot::Prio Bot::condStartDayLaptop() {
    /* not necesary to check hoursPassed() */
    if (mDayStarted || isOfficeUsable()) {
        return Prio::None;
    }
    if (qPlayer.HasItem(ITEM_LAPTOP) && qPlayer.LaptopVirus == 0) {
        return Prio::Top;
    }
    return Prio::None;
}

Bot::Prio Bot::condBuero() {
    /* not necesary to check hoursPassed() */
    if (!isOfficeUsable()) {
        return Prio::None; /* office is destroyed */
    }
    if (mNeedToPlanJobs) {
        return Prio::Top;
    }
    if (mNeedToPlanRoutes) {
        return Prio::Top;
    }
    return Prio::None;
}

Bot::Prio Bot::condCallInternational() {
    if (!hoursPassed(ACTION_CALL_INTERNATIONAL, 2)) {
        return Prio::None;
    }
    if (qPlayer.HasItem(ITEM_HANDY)) {
        return Prio::None; /* we rather use mobile to call */
    }
    if (!isOfficeUsable()) {
        return Prio::None; /* office is destroyed */
    }
    if (canWeCallInternational()) {
        return Prio::High;
    }
    return Prio::None;
}

Bot::Prio Bot::condCallInternationalHandy() {
    if (!hoursPassed(ACTION_CALL_INTER_HANDY, 2)) {
        return Prio::None;
    }
    if (!qPlayer.HasItem(ITEM_HANDY) || qPlayer.IsStuck != 0) {
        return Prio::None; /* cannot use mobile */
    }
    if (canWeCallInternational()) {
        return Prio::High;
    }
    return Prio::None;
}

Bot::Prio Bot::condCheckLastMinute() {
    if (!hoursPassed(ACTION_CHECKAGENT1, 2)) {
        return Prio::None;
    }

    if (mPlanesForJobs.empty()) {
        return Prio::None; /* no planes */
    }
    if (!mPlanerSolution.empty()) {
        return Prio::None; /* previously grabbed flights still not scheduled */
    }

    auto res = canWePlanFlights();
    if (HowToPlan::Office == res && Sim.GetHour() >= 17) {
        return Prio::None; /* might be too late to reach office */
    }
    if (HowToPlan::None != res) {
        return Prio::High;
    }

    return Prio::None;
}
Bot::Prio Bot::condCheckTravelAgency() {
    if (!hoursPassed(ACTION_CHECKAGENT2, 2)) {
        return Prio::None;
    }

    if (mPlanesForJobs.empty()) {
        return Prio::None; /* no planes */
    }
    if (!mPlanerSolution.empty()) {
        return Prio::None; /* previously grabbed flights still not scheduled */
    }

    auto res = canWePlanFlights();
    if (HowToPlan::Office == res && Sim.GetHour() >= 17) {
        return Prio::None; /* might be too late to reach office */
    }
    if (HowToPlan::None != res) {
        return qPlayer.RobotUse(ROBOT_USE_MUCH_FRACHT) ? Prio::High : Prio::Higher;
    }

    if (mItemAntiVirus == 0) {
        return Prio::Low; /* collect tarantula */
    }

    return Prio::None;
}
Bot::Prio Bot::condCheckFreight() {
    if (!hoursPassed(ACTION_CHECKAGENT3, 2)) {
        return Prio::None;
    }
    if (!qPlayer.RobotUse(ROBOT_USE_FRACHT)) {
        return Prio::None;
    }

    if (mPlanesForJobs.empty()) {
        return Prio::None; /* no planes */
    }
    if (!mPlanerSolution.empty()) {
        return Prio::None; /* previously grabbed flights still not scheduled */
    }

    auto res = canWePlanFlights();
    if (HowToPlan::Office == res && Sim.GetHour() >= 17) {
        return Prio::None; /* might be too late to reach office */
    }
    if (HowToPlan::None != res) {
        return qPlayer.RobotUse(ROBOT_USE_MUCH_FRACHT) ? Prio::Higher : Prio::High;
    }

    return Prio::None;
}

Bot::Prio Bot::condUpgradePlanes() {
    if (!hoursPassed(ACTION_UPGRADE_PLANES, 24)) {
        return Prio::None;
    }
    if (!mDayStarted) {
        return Prio::None; /* plane list not updated */
    }
    if (!isOfficeUsable()) {
        return Prio::None; /* office is destroyed */
    }
    if (!haveDiscount()) {
        return Prio::None; /* wait until we have some discount */
    }

    if (mRunToFinalObjective == FinalPhase::SaveMoney) {
        return Prio::None;
    }
    if (!qPlayer.RobotUse(ROBOT_USE_LUXERY) && mRunToFinalObjective > FinalPhase::No) {
        return Prio::None; /* not a mission where final objective is plane upgrades */
    }

    /* Plane upgrades happen asynchronously. Therefore, we earmark money in the variable mMoneyReservedForUpgrades.
     * We need to execute this action regularly even if we have no money since we
     * might cancel previously planned upgrades to have more available money. */
    SLONG minCost = 550 * (SeatCosts[2] - SeatCosts[0] / 2); /* assuming 550 seats (777) */
    if (getMoneyAvailable() < minCost && mMoneyReservedForUpgrades == 0) {
        return Prio::None;
    }
    if ((mRunToFinalObjective == FinalPhase::TargetRun) && qPlayer.RobotUse(ROBOT_USE_LUXERY)) {
        return Prio::High;
    }
    if (!mPlanesForRoutes.empty()) {
        return Prio::Medium;
    }
    return Prio::None;
}

Bot::Prio Bot::condBuyNewPlane(__int64 &moneyAvailable) {
    moneyAvailable = getMoneyAvailable();
    if (!hoursPassed(ACTION_BUYNEWPLANE, 2)) {
        return Prio::None;
    }
    if (!qPlayer.RobotUse(ROBOT_USE_MAKLER) || qPlayer.RobotUse(ROBOT_USE_DESIGNER_BUY)) {
        return Prio::None;
    }
    if (!mLongTermStrategy) {
        return Prio::None; /* we will only buy used planes */
    }
    if (mRunToFinalObjective > FinalPhase::No) {
        return Prio::None;
    }
    if (HowToPlan::None == canWePlanFlights()) {
        return Prio::None;
    }
    if (!haveDiscount()) {
        return Prio::None; /* wait until we have some discount */
    }

    SLONG bestPlaneTypeId = mDoRoutes ? mBuyPlaneForRouteId : mBestPlaneTypeId;
    if (bestPlaneTypeId < 0) {
        return Prio::None; /* no plane purchase planned */
    }
    for (auto planeId : mPlanesForRoutesUnassigned) {
        const auto &qPlane = qPlayer.Planes[planeId];
        if (qPlane.TypeId == bestPlaneTypeId) {
            return Prio::None; /* we already have an unused plane of desired type */
        }
    }
    if ((qPlayer.xPiloten < PlaneTypes[bestPlaneTypeId].AnzPiloten) || (qPlayer.xBegleiter < PlaneTypes[bestPlaneTypeId].AnzBegleiter)) {
        return Prio::None; /* not enough crew */
    }
    if (moneyAvailable >= PlaneTypes[bestPlaneTypeId].Preis) {
        return Prio::High; /* buy the plane (e.g. for a new route) before spending it on something else */
    }

    return Prio::None;
}

Bot::Prio Bot::condBuyUsedPlane(__int64 &moneyAvailable) {
    moneyAvailable = getMoneyAvailable();
    if (!hoursPassed(ACTION_BUYUSEDPLANE, 2)) {
        return Prio::None;
    }
    if (mLongTermStrategy) {
        return Prio::None; /* we will only buy new planes */
    }
    if (mRunToFinalObjective > FinalPhase::No) {
        return Prio::None;
    }
    if (HowToPlan::None == canWePlanFlights()) {
        return Prio::None;
    }
    if (!haveDiscount()) {
        return Prio::None; /* wait until we have some discount */
    }
    if (mBestUsedPlaneIdx < 0) {
        return Prio::None; /* no plane selected (ACTION_VISITMUSEUM) */
    }
    if ((qPlayer.xPiloten < Sim.UsedPlanes[mBestUsedPlaneIdx].ptAnzPiloten) || (qPlayer.xBegleiter < Sim.UsedPlanes[mBestUsedPlaneIdx].ptAnzBegleiter)) {
        return Prio::None; /* not enough crew */
    }
    if (moneyAvailable >= Sim.UsedPlanes[mBestUsedPlaneIdx].CalculatePrice()) {
        return Prio::High; /* buy the plane (e.g. for a new route) before spending it on something else */
    }

    return Prio::None;
}

Bot::Prio Bot::condVisitMuseum() {
    if (!hoursPassed(ACTION_VISITMUSEUM, 2)) {
        return Prio::None;
    }
    if (mLongTermStrategy) {
        return Prio::None; /* we will only buy new planes */
    }
    if (mRunToFinalObjective > FinalPhase::No) {
        return Prio::None;
    }
    if (HowToPlan::None == canWePlanFlights()) {
        return Prio::None;
    }
    if (!haveDiscount()) {
        return Prio::None; /* wait until we have some discount */
    }
    if (mBestUsedPlaneIdx < 0) {
        return Prio::High;
    }
    return Prio::None;
}

Bot::Prio Bot::condVisitHR() {
    if (!hoursPassed(ACTION_PERSONAL, 4)) {
        return Prio::None;
    }
    if (hoursPassed(ACTION_PERSONAL, 24)) {
        return Prio::Medium; /* hire new crew every day */
    }
    if (mItemPills >= 1 && qPlayer.HasItem(ITEM_TABLETTEN) == 0) {
        return Prio::Low; /* we need new pills */
    }
    return Prio::None;
}

Bot::Prio Bot::condBuyKerosine(__int64 &moneyAvailable) {
    moneyAvailable = getMoneyAvailable();
    if (!hoursPassed(ACTION_BUY_KEROSIN, 4)) {
        return Prio::None;
    }
    if (!qPlayer.RobotUse(ROBOT_USE_PETROLAIR)) {
        return Prio::None;
    }
    if (qPlayer.HasBerater(BERATERTYP_KEROSIN) < 30 || !mDayStarted) {
        return Prio::None; /* no access to advisor report */
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
    moneyAvailable = getMoneyAvailable();
    if (!hoursPassed(ACTION_BUY_KEROSIN_TANKS, 24)) {
        return Prio::None;
    }
    if (!qPlayer.RobotUse(ROBOT_USE_PETROLAIR) || !qPlayer.RobotUse(ROBOT_USE_TANKS)) {
        return Prio::None;
    }
    if (qPlayer.HasBerater(BERATERTYP_KEROSIN) < 30 || !mDayStarted) {
        return Prio::None; /* no access to advisor report */
    }
    if (!haveDiscount()) {
        return Prio::None; /* wait until we have some discount */
    }
    if (mRunToFinalObjective > FinalPhase::No) {
        return Prio::None;
    }
    if (mTankRatioEmptiedYesterday < 0.5) {
        return Prio::None;
    }
    auto nTankTypes = sizeof(TankSize) / sizeof(TankSize[0]);
    moneyAvailable -= kMoneyReserveBuyTanks;
    if (moneyAvailable > TankPrice[nTankTypes - 1]) {
        moneyAvailable = TankPrice[nTankTypes - 1]; /* do not spend more than 1x largest tank at once*/
    }
    if (moneyAvailable >= TankPrice[1]) {
        return Prio::Medium;
    }
    return Prio::None;
}

Bot::Prio Bot::condSabotage(__int64 &moneyAvailable) {
    moneyAvailable = getMoneyAvailable();
    if (!hoursPassed(ACTION_SABOTAGE, 24)) {
        return Prio::None;
    }
    if (qPlayer.ArabTrust == 0) {
        return Prio::None;
    }
    if (qPlayer.RobotUse(ROBOT_USE_SABOTAGE) && qPlayer.ArabHints < kMaxSabotageHints) {
        if (mNemesis != -1) {
            return Prio::Low;
        }
    }
    if (mItemAntiVirus == 1 || mItemAntiVirus == 2) {
        return Prio::Low; /* collect darts */
    }
    return Prio::None;
}

Bot::Prio Bot::condIncreaseDividend(__int64 &moneyAvailable) {
    moneyAvailable = getMoneyAvailable();
    if (!hoursPassed(ACTION_SET_DIVIDEND, 24)) {
        return Prio::None;
    }
    if (mRunToFinalObjective > FinalPhase::No) {
        return Prio::None;
    }

    SLONG maxToEmit = (2500000 - qPlayer.MaxAktien) / 100 * 100;
    if (kReduceDividend && maxToEmit < 10000) {
        /* we cannot emit any shares anymore. We do not care about stock prices now. */
        return (qPlayer.Dividende > 0) ? Prio::Medium : Prio::None;
    }
    if (qPlayer.Dividende >= 25) {
        return Prio::None;
    }
    moneyAvailable -= kMoneyReserveIncreaseDividend;
    if (moneyAvailable >= 0) {
        return Prio::Medium;
    }
    return Prio::None;
}

Bot::Prio Bot::condTakeOutLoan() {
    if (!hoursPassed(ACTION_RAISEMONEY, 1)) {
        return Prio::None;
    }
    if (howToGetMoney() == HowToGetMoney::IncreaseCredit) {
        return Prio::Top;
    }
    return Prio::None;
}

Bot::Prio Bot::condDropMoney(__int64 &moneyAvailable) {
    moneyAvailable = getMoneyAvailable();
    if (!hoursPassed(ACTION_DROPMONEY, 24)) {
        return Prio::None;
    }
    if (mRunToFinalObjective > FinalPhase::No) {
        return Prio::None;
    }

    moneyAvailable -= kMoneyReservePaybackCredit;
    if (moneyAvailable >= 0 && qPlayer.Credit > 0 && getWeeklyOpSaldo() > 1000 * 1000) {
        return Prio::Medium;
    }
    return Prio::None;
}

Bot::Prio Bot::condEmitShares() {
    if (!hoursPassed(ACTION_EMITSHARES, 24)) {
        return Prio::None;
    }
    if (howToGetMoney() == HowToGetMoney::EmitShares) {
        return Prio::Top;
    }
    if (GameMechanic::canEmitStock(qPlayer) == GameMechanic::EmitStockResult::Ok) {
        return Prio::Medium;
    }
    return Prio::None;
}

Bot::Prio Bot::condSellShares(__int64 &moneyAvailable) {
    moneyAvailable = getMoneyAvailable();
    if (!hoursPassed(ACTION_SELLSHARES, 1)) {
        return Prio::None;
    }

    auto res = howToGetMoney();
    if (res == HowToGetMoney::SellShares || res == HowToGetMoney::SellOwnShares || res == HowToGetMoney::SellAllOwnShares) {
        if (qPlayer.Money < 0) {
            return Prio::Top;
        } else {
            return Prio::High;
        }
    }
    return Prio::None;
}

Bot::Prio Bot::condBuyShares(__int64 &moneyAvailable, SLONG dislike) {
    if (!hoursPassed(ACTION_BUYSHARES, 4)) {
        return Prio::None;
    }
    if (mRunToFinalObjective > FinalPhase::No) {
        return Prio::None;
    }
    if (qPlayer.RobotUse(ROBOT_USE_DONTBUYANYSHARES)) {
        return Prio::None;
    }
    return std::max(condBuyNemesisShares(moneyAvailable, dislike), condBuyOwnShares(moneyAvailable));
}
Bot::Prio Bot::condBuyOwnShares(__int64 &moneyAvailable) {
    moneyAvailable = getMoneyAvailable() - kMoneyReserveBuyOwnShares;
    if (qPlayer.OwnsAktien[qPlayer.PlayerNum] >= qPlayer.AnzAktien / 4) {
        return Prio::None;
    }
    if ((moneyAvailable >= 0) && (qPlayer.Credit == 0)) {
        return Prio::Low;
    }
    return Prio::None;
}
Bot::Prio Bot::condBuyNemesisShares(__int64 &moneyAvailable, SLONG dislike) {
    return Prio::None; // TODO

    moneyAvailable = getMoneyAvailable() - kMoneyReserveBuyNemesisShares;
    if ((dislike != -1) && (moneyAvailable >= 0) && (qPlayer.Credit == 0)) {
        return Prio::Low;
    }
    return Prio::None;
}

Bot::Prio Bot::condVisitMech() {
    if (!hoursPassed(ACTION_VISITMECH, 4)) {
        return Prio::None;
    }
    /* Plane repairs happen asynchronously. Therefore, we earmark money in the variable mMoneyReservedForRepairs.
     * We need to execute this action regularly even if we have no money since we
     * might cancel previously planned repairs to have more available money. */
    if (getMoneyAvailable() < 0 && mMoneyReservedForRepairs == 0) {
        return Prio::None;
    }
    return Prio::Medium;
}

Bot::Prio Bot::condVisitNasa(__int64 &moneyAvailable) {
    moneyAvailable = getMoneyAvailable();
    if (!hoursPassed(ACTION_VISITNASA, 4)) {
        return Prio::None;
    }
    if (!qPlayer.RobotUse(ROBOT_USE_NASA)) {
        return Prio::None;
    }
    if (mRunToFinalObjective != FinalPhase::TargetRun) {
        return Prio::None;
    }

    const auto &qPrices = (Sim.Difficulty == DIFF_FINAL) ? RocketPrices : StationPrices;
    auto nRocketParts = sizeof(qPrices) / sizeof(qPrices[0]);
    for (SLONG i = 0; i < nRocketParts; i++) {
        if ((qPlayer.RocketFlags & (1 << i)) == 0 && moneyAvailable >= qPrices[i]) {
            return Prio::High;
        }
    }

    return Prio::None;
}

Bot::Prio Bot::condVisitMisc() {
    /* misc action, can do as often as the bot likes */
    if (qPlayer.RobotUse(ROBOT_USE_NOCHITCHAT)) {
        return Prio::None;
    }
    return Prio::Lowest;
}

Bot::Prio Bot::condVisitMakler() {
    if (!hoursPassed(ACTION_VISITMAKLER, 4)) {
        return Prio::None;
    }
    if (mBestPlaneTypeId == -1 && mLongTermStrategy) {
        return Prio::Low;
    }
    if (mItemAntiStrike == 0) {
        return Prio::Low; /* take BH */
    }
    return condVisitMisc();
}

Bot::Prio Bot::condVisitArab() {
    /* misc action, can do as often as the bot likes */
    if (!qPlayer.RobotUse(ROBOT_USE_PETROLAIR)) {
        return Prio::None;
    }
    if (qPlayer.HasItem(ITEM_MG)) {
        return Prio::Low;
    }
    return condVisitMisc();
}

Bot::Prio Bot::condVisitRick() {
    /* misc action, can do as often as the bot likes */
    if (qPlayer.StrikeHours > 0 && qPlayer.StrikeEndType == 0 && mItemAntiStrike >= 3) {
        return Prio::Top;
    }
    if (mItemAntiStrike == 3) {
        return Prio::Low; /* give horse shoe */
    }
    return condVisitMisc();
}

Bot::Prio Bot::condVisitDutyFree(__int64 &moneyAvailable) {
    moneyAvailable = getMoneyAvailable();
    moneyAvailable -= 100 * 1000;

    if (hoursPassed(ACTION_VISITDUTYFREE, 24) && moneyAvailable >= 0) {
        /* emergency shopping: Laptop because office is destroyed */
        if (!mDayStarted && !isOfficeUsable() && qPlayer.LaptopQuality == 0) {
            return Prio::Higher;
        }

        if (qPlayer.LaptopQuality < 4) {
            return Prio::Low;
        }
    }

    /* misc action, can do as often as the bot likes */
    if (moneyAvailable >= 0 && !qPlayer.HasItem(ITEM_HANDY)) {
        return Prio::Low;
    }
    if (mItemAntiStrike >= 1 && mItemAntiStrike <= 2) {
        return Prio::Low; /* we still need to aquire the horse shoe */
    }
    if (mItemArabTrust == 0 && Sim.Date > 0 && qPlayer.ArabTrust == 0) {
        return Prio::Low; /* we still need to aquire the MG */
    }

    return condVisitMisc();
}

Bot::Prio Bot::condVisitBoss(__int64 &moneyAvailable) {
    moneyAvailable = getMoneyAvailable();
    if (!hoursPassed(ACTION_VISITAUFSICHT, 2)) {
        return Prio::None;
    }

    moneyAvailable -= kMoneyReserveBossOffice;
    if ((mRunToFinalObjective == FinalPhase::No) && (moneyAvailable >= 0)) {
        if (Sim.Time > (16 * 60000) && (mBossNumCitiesAvailable > 0 || mBossGateAvailable)) {
            return Prio::High; /* check again right before end of day */
        }
        if (mBossGateAvailable || (mBossNumCitiesAvailable == -1)) { /* there is gate available (or we don't know yet) */
            return mOutOfGates ? Prio::High : Prio::Medium;
        }
        if (mBossNumCitiesAvailable > 0 && hoursPassed(ACTION_VISITAUFSICHT, 4)) {
            return Prio::Low;
        }
    }

    if (mItemPills == 0) {
        return Prio::Low; /* we still need to take the card */
    }
    return Prio::None;
}

Bot::Prio Bot::condExpandAirport(__int64 &moneyAvailable) {
    moneyAvailable = getMoneyAvailable();
    if (!hoursPassed(ACTION_EXPANDAIRPORT, 24)) {
        return Prio::None;
    }
    if (GameMechanic::ExpandAirportResult::Ok != GameMechanic::canExpandAirport(qPlayer)) {
        return Prio::None;
    }
    if (!mDayStarted) {
        return Prio::None; /* no access to gate utilization */
    }
    if (mRunToFinalObjective > FinalPhase::No) {
        return Prio::None;
    }

    DOUBLE gateUtilization = 0;
    for (auto util : qPlayer.Gates.Auslastung) {
        gateUtilization += util;
    }
    if (gateUtilization / (24 * 7) < (qPlayer.Gates.NumRented - 1)) {
        return Prio::None;
    }
    moneyAvailable -= kMoneyReserveExpandAirport;
    if (moneyAvailable >= 0) {
        return Prio::Medium;
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
    if (mRunToFinalObjective > FinalPhase::No) {
        return Prio::None;
    }
    if (HowToPlan::None == canWePlanFlights()) {
        return Prio::None;
    }
    if (mWantToRentRouteId != -1) {
        return Prio::None; /* we already want to rent a route */
    }
    if (!Helper::checkRoomOpen(ACTION_WERBUNG_ROUTES)) {
        return Prio::None; /* let's wait until we are able to buy ads for the route */
    }
    if (mRoutes.empty() || mRoutes[mRoutesSortedByUtilization[0]].routeUtilization >= kMaximumRouteUtilization) {
        return Prio::Medium;
    }
    return Prio::None;
}

Bot::Prio Bot::condVisitRouteBoxRenting(__int64 &moneyAvailable) {
    moneyAvailable = getMoneyAvailable();
    if (!hoursPassed(ACTION_VISITROUTEBOX2, 4)) {
        return Prio::None;
    }
    if (!qPlayer.RobotUse(ROBOT_USE_ROUTEBOX) || !mDoRoutes) {
        return Prio::None;
    }
    if (mRunToFinalObjective > FinalPhase::No) {
        return Prio::None;
    }
    if (HowToPlan::None == canWePlanFlights()) {
        return Prio::None;
    }
    if (mWantToRentRouteId != -1) {
        return Prio::High;
    }
    return Prio::None;
}

Bot::Prio Bot::condVisitSecurity(__int64 &moneyAvailable) {
    moneyAvailable = getMoneyAvailable();
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
    return Prio::Lowest;
}

Bot::Prio Bot::condVisitDesigner(__int64 &moneyAvailable) {
    moneyAvailable = getMoneyAvailable();
    if (!hoursPassed(ACTION_VISITDESIGNER, 2)) {
        return Prio::None;
    }
    if (!qPlayer.RobotUse(ROBOT_USE_DESIGNER) || !qPlayer.RobotUse(ROBOT_USE_DESIGNER_BUY)) {
        return Prio::None;
    }
    if (HowToPlan::None == canWePlanFlights()) {
        return Prio::None;
    }
    if (!haveDiscount()) {
        return Prio::None; /* wait until we have some discount */
    }

    if (mDesignerPlane.Name.empty() || !mDesignerPlane.IsBuildable()) {
        return Prio::None; /* no designer plane available */
    }
    if ((qPlayer.xPiloten < mDesignerPlane.CalcPiloten()) || (qPlayer.xBegleiter < mDesignerPlane.CalcBegleiter())) {
        return Prio::None; /* not enough crew */
    }
    if (moneyAvailable >= mDesignerPlane.CalcCost()) {
        return Prio::High; /* buy the plane (e.g. for a new route) before spending it on something else */
    }

    return Prio::None;
}

Bot::Prio Bot::condBuyAdsForRoutes(__int64 &moneyAvailable) {
    moneyAvailable = getMoneyAvailable();
    if (!hoursPassed(ACTION_WERBUNG_ROUTES, 4)) {
        return Prio::None;
    }

    if (!qPlayer.RobotUse(ROBOT_USE_WERBUNG)) {
        return Prio::None;
    }
    if (!haveDiscount()) {
        return Prio::None; /* wait until we have some discount */
    }
    if (!mDayStarted) {
        return Prio::None; /* routes not updated */
    }

    if (mRunToFinalObjective > FinalPhase::No) {
        return Prio::None;
    }

    auto minCost = gWerbePrice[1 * 6 + kSmallestAdCampaign];
    moneyAvailable -= minCost;
    if (moneyAvailable >= 0 && !mRoutesSortedByImage.empty()) {
        SLONG imageDelta = UBYTE(minCost / 30000);
        SLONG idx = mRoutesSortedByImage[0];
        if (idx < mRoutes.size() && mRoutes[idx].image + imageDelta <= 100) {
            return (mRoutes[idx].image < 80) ? Prio::High : Prio::Medium;
        }
    }
    return Prio::None;
}

Bot::Prio Bot::condBuyAds(__int64 &moneyAvailable) {
    moneyAvailable = getMoneyAvailable();
    if (!hoursPassed(ACTION_WERBUNG, 4)) {
        return Prio::None;
    }

    if (!qPlayer.RobotUse(ROBOT_USE_WERBUNG)) {
        return Prio::None;
    }
    if (!haveDiscount()) {
        return Prio::None; /* wait until we have some discount */
    }

    if (mRunToFinalObjective == FinalPhase::SaveMoney) {
        return Prio::None;
    }
    if (!qPlayer.RobotUse(ROBOT_USE_MUCHWERBUNG) && mRunToFinalObjective > FinalPhase::No) {
        return Prio::None; /* not a mission where we need to buy ads */
    }

    auto minCost = gWerbePrice[0 * 6 + kSmallestAdCampaign];
    moneyAvailable -= minCost;
    if (moneyAvailable >= 0) {
        if ((mRunToFinalObjective == FinalPhase::TargetRun) && qPlayer.RobotUse(ROBOT_USE_MUCHWERBUNG)) {
            return Prio::High;
        }
        if (qPlayer.Image < kMinimumImage || (mDoRoutes && qPlayer.Image < 300)) {
            return Prio::Medium;
        }
        auto imageDelta = minCost / 10000 * (kSmallestAdCampaign + 6) / 55;
        if (mDoRoutes && qPlayer.Image < (1000 - imageDelta)) {
            return Prio::Low;
        }
    }
    return Prio::None;
}

Bot::Prio Bot::condVisitAds() {
    if (mItemAntiVirus == 3) {
        return Prio::Low;
    }
    if (mItemAntiVirus == 4 && qPlayer.HasItem(ITEM_DISKETTE) == 0) {
        return Prio::Low;
    }
    return Prio::None;
}