#include "Bot.h"

#include "BotHelper.h"
#include "class.h"
#include "GameMechanic.h"
#include "global.h"
#include "TeakLibW.h"

// Preise verstehen sich pro Sitzplatz:
extern SLONG SeatCosts[];

__int64 Bot::getMoneyAvailable() const { return qPlayer.Money - kMoneyEmergencyFund; }

bool Bot::hoursPassed(SLONG room, SLONG hours) {
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

Bot::HowToPlan Bot::canWePlanFlights() {
    if (qPlayer.HasItem(ITEM_LAPTOP)) {
        if ((qPlayer.LaptopVirus == 1) && (qPlayer.HasItem(ITEM_DISKETTE) == 1)) {
            GameMechanic::useItem(qPlayer, ITEM_DISKETTE);
        }
        if (qPlayer.LaptopVirus == 0) {
            return HowToPlan::Laptop;
        }
    }
    if (qPlayer.OfficeState != 2) {
        return HowToPlan::Office;
    }
    return HowToPlan::None;
}

Bot::Prio Bot::condAll(SLONG actionId) {
    __int64 moneyAvailable = 0;
    switch (actionId) {
    case ACTION_RAISEMONEY:
        return condRaiseMoney(moneyAvailable);
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
        return condVisitMech(moneyAvailable);
    case ACTION_VISITDUTYFREE:
        return condVisitDutyFree(moneyAvailable);
    case ACTION_VISITAUFSICHT:
        return condVisitBoss(moneyAvailable);
    case ACTION_EXPANDAIRPORT:
        return condExpandAirport(moneyAvailable);
    case ACTION_VISITNASA:
        return condVisitNasa(moneyAvailable);
    case ACTION_VISITTELESCOPE:
        return condVisitMisc();
    case ACTION_VISITRICK:
        return condVisitRick();
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
        return condBuyShares(moneyAvailable, mDislike);
    case ACTION_SELLSHARES:
        return condSellShares(moneyAvailable);
    case ACTION_WERBUNG_ROUTES:
        return condBuyAdsForRoutes(moneyAvailable);
    case ACTION_CALL_INTERNATIONAL:
        return condCallInternational();
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
    if (!hoursPassed(ACTION_STARTDAY, 24)) {
        return Prio::None;
    }
    return Prio::Top;
}

Bot::Prio Bot::condBuero() {
    // not necesary to check hoursPassed()
    if (qPlayer.OfficeState == 2) {
        return Prio::None;
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
    if (HowToPlan::None == canWePlanFlights()) {
        return Prio::None;
    }
    return Prio::High;
}
Bot::Prio Bot::condCheckTravelAgency() {
    if (!hoursPassed(ACTION_CHECKAGENT2, 2)) {
        return Prio::None;
    }
    if (HowToPlan::None == canWePlanFlights()) {
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
    if (HowToPlan::None == canWePlanFlights()) {
        return Prio::None;
    }
    return Prio::Medium;
}

Bot::Prio Bot::condUpgradePlanes(__int64 &moneyAvailable) {
    moneyAvailable = getMoneyAvailable();
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
    moneyAvailable = getMoneyAvailable();
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

Bot::Prio Bot::condVisitHR() {
    if (!hoursPassed(ACTION_PERSONAL, 24)) {
        return Prio::None;
    }
    return Prio::Medium;
}

Bot::Prio Bot::condBuyKerosine(__int64 &moneyAvailable) {
    moneyAvailable = getMoneyAvailable();
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
    moneyAvailable = getMoneyAvailable();
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
    if (mTankRatioEmptiedYesterday < 0.5) {
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
    moneyAvailable = getMoneyAvailable();
    if (!hoursPassed(ACTION_SABOTAGE, 24)) {
        return Prio::None;
    }
    if (qPlayer.ArabTrust > 0 && (mItemAntiVirus == 1 || mItemAntiVirus == 2)) {
        return Prio::Low;
    }
    return Prio::None;
}

Bot::Prio Bot::condIncreaseDividend(__int64 &moneyAvailable) {
    moneyAvailable = getMoneyAvailable();
    if (!hoursPassed(ACTION_SET_DIVIDEND, 24)) {
        return Prio::None;
    }
    SLONG maxToEmit = (2500000 - qPlayer.MaxAktien) / 100 * 100;
    if (maxToEmit < 10000) {
        /* we cannot emit any shares anymore. We do not care about stock prices now. */
        return (qPlayer.Dividende > 0) ? Prio::Medium : Prio::None;
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
    moneyAvailable = getMoneyAvailable();
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
    moneyAvailable = getMoneyAvailable();
    if (!hoursPassed(ACTION_DROPMONEY, 24)) {
        return Prio::None;
    }
    moneyAvailable -= 1500 * 1000;
    if (moneyAvailable >= 0 && qPlayer.Credit > 0) {
        return Prio::Medium;
    }
    return Prio::None;
}

Bot::Prio Bot::condEmitShares() {
    if (!hoursPassed(ACTION_EMITSHARES, 24)) {
        return Prio::None;
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

Bot::Prio Bot::condBuyShares(__int64 &moneyAvailable, SLONG dislike) {
    return std::max(condBuyNemesisShares(moneyAvailable, dislike), condBuyOwnShares(moneyAvailable));
}

Bot::Prio Bot::condBuyOwnShares(__int64 &moneyAvailable) {
    moneyAvailable = getMoneyAvailable();
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
    moneyAvailable = getMoneyAvailable();
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
    moneyAvailable = getMoneyAvailable();
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
    moneyAvailable = getMoneyAvailable();
    if (!qPlayer.RobotUse(ROBOT_USE_NASA)) {
        return Prio::None;
    }
    return Prio::None;
}

Bot::Prio Bot::condVisitRick() {
    if (qPlayer.StrikeHours > 0 && qPlayer.StrikeEndType != 0 && mItemAntiStrike == 4) {
        return Prio::Top;
    }
    return condVisitMisc();
}

Bot::Prio Bot::condVisitMisc() {
    if (qPlayer.RobotUse(ROBOT_USE_NOCHITCHAT)) {
        return Prio::None;
    }
    return Prio::Low;
}

Bot::Prio Bot::condBuyUsedPlane(__int64 &moneyAvailable) {
    moneyAvailable = getMoneyAvailable();
    return Prio::None;
}

Bot::Prio Bot::condVisitDutyFree(__int64 &moneyAvailable) {
    moneyAvailable = getMoneyAvailable();
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
    moneyAvailable = getMoneyAvailable();
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

Bot::Prio Bot::condExpandAirport(__int64 &moneyAvailable) {
    moneyAvailable = getMoneyAvailable();
    if (!hoursPassed(ACTION_EXPANDAIRPORT, 24)) {
        return Prio::None;
    }
    if (GameMechanic::ExpandAirportResult::Ok != GameMechanic::canExpandAirport(qPlayer)) {
        return Prio::None;
    }
    DOUBLE gateUtilization = 0;
    for (auto util : qPlayer.Gates.Auslastung) {
        gateUtilization += util;
    }
    if (gateUtilization / (24 * 7) < (qPlayer.Gates.NumRented - 1)) {
        return Prio::None;
    }
    moneyAvailable -= 1000 * 1000;
    if (moneyAvailable < 0) {
        return Prio::None;
    }
    return Prio::Medium;
}

Bot::Prio Bot::condVisitRouteBoxPlanning() {
    if (!hoursPassed(ACTION_VISITROUTEBOX, 4)) {
        return Prio::None;
    }
    if (!qPlayer.RobotUse(ROBOT_USE_ROUTEBOX) || !mDoRoutes) {
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
    return Prio::Low;
}

Bot::Prio Bot::condVisitDesigner(__int64 &moneyAvailable) {
    moneyAvailable = getMoneyAvailable();
    if (!hoursPassed(ACTION_VISITDESIGNER, 24)) {
        return Prio::None;
    }
    if (!qPlayer.RobotUse(ROBOT_USE_DESIGNER)) {
        return Prio::None;
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
