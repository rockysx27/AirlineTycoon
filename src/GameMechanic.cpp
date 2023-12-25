#include "StdAfx.h"
#include "AtNet.h"
#include "Sabotage.h"

SLONG TankSize[4] = {100000, 1000000, 10000000, 100000000};
SLONG TankPrice[4] = {100000, 800000, 7000000, 60000000};

SLONG SabotagePrice[5] = {1000, 5000, 10000, 50000, 100000};
SLONG SabotagePrice2[4] = {10000, 25000, 50000, 250000};
SLONG SabotagePrice3[6] = {100000, 500000, 1000000, 2000000, 5000000, 8000000};

SLONG RocketPrices[10] = {200000, 400000, 600000, 5000000, 8000000, 10000000, 20000000, 25000000, 50000000, 85000000};

SLONG StationPrices[10] = {1000000, 2000000, 3000000, 2000000, 10000000, 20000000, 35000000, 50000000, 35000000, 80000000};

void GameMechanic::bankruptPlayer(PLAYER &qPlayer) {
    qPlayer.IsOut = TRUE;

    for (SLONG c = 0; c < Sim.Players.Players.AnzEntries(); c++) {
        Sim.Players.Players[c].OwnsAktien[qPlayer.PlayerNum] = 0;
        qPlayer.OwnsAktien[c] = 0;
    }
}

bool GameMechanic::buyKerosinTank(PLAYER &qPlayer, SLONG type, SLONG amount) {
    if (type < 0 || type >= sizeof(TankSize) || type >= sizeof(TankPrice)) {
        hprintf("GameMechanic::buyKerosinTank: Invalid tank type.");
        return false;
    }
    if (amount < 0) {
        hprintf("GameMechanic::calcKerosinPrice: Negative amount.");
        return false;
    }

    SLONG cost = TankPrice[type];
    SLONG size = TankSize[type];

    if (qPlayer.Money - cost * amount < DEBT_LIMIT) {
        return false;
    }

    qPlayer.Tank += size / 1000 * amount;
    qPlayer.NetUpdateKerosin();

    qPlayer.ChangeMoney(-cost * amount, 2091, CString(bitoa(size)), const_cast<char *>((LPCTSTR)CString(bitoa(amount))));
    SIM::SendSimpleMessage(ATNET_CHANGEMONEY, 0, Sim.localPlayer, -cost * amount, -1);

    qPlayer.DoBodyguardRabatt(cost * amount);
    return true;
}

bool GameMechanic::toggleKerosinTank(PLAYER &qPlayer) {
    qPlayer.TankOpen ^= 1;
    qPlayer.NetUpdateKerosin();
    return true;
}

bool GameMechanic::buyKerosin(PLAYER &qPlayer, SLONG type, SLONG amount) {
    if (type < 0 || type >= 3) {
        hprintf("GameMechanic::calcKerosinPrice: Invalid kerosine type.");
        return false;
    }
    if (amount < 0) {
        hprintf("GameMechanic::buyKerosin: Negative amount.");
        return false;
    }
    if (qPlayer.TankInhalt + amount > qPlayer.Tank) {
        hprintf("GameMechanic::buyKerosin: Amount too high.");
        return false;
    }

    auto transaction = calcKerosinPrice(qPlayer, type, amount);
    auto cost = transaction.Kosten - transaction.Rabatt;
    amount = transaction.Menge;

    if ((cost <= 0) || qPlayer.Money - cost < DEBT_LIMIT) {
        return false;
    }

    SLONG OldInhalt = qPlayer.TankInhalt;
    qPlayer.TankInhalt += amount;

    qPlayer.TankPreis = (OldInhalt * qPlayer.TankPreis + cost) / qPlayer.TankInhalt;
    qPlayer.KerosinQuali = (OldInhalt * qPlayer.KerosinQuali + amount * qPlayer.KerosinKind) / qPlayer.TankInhalt;
    qPlayer.NetUpdateKerosin();

    qPlayer.ChangeMoney(-cost, 2020, "");
    SIM::SendSimpleMessage(ATNET_CHANGEMONEY, 0, Sim.localPlayer, -cost, -1);

    qPlayer.DoBodyguardRabatt(cost);
    return true;
}

GameMechanic::KerosinTransaction GameMechanic::calcKerosinPrice(PLAYER &qPlayer, __int64 type, __int64 amount) {
    if (type < 0 || type >= 3) {
        hprintf("GameMechanic::calcKerosinPrice: Invalid kerosine type.");
        return {};
    }
    if (amount < 0) {
        hprintf("GameMechanic::calcKerosinPrice: Negative amount.");
        return {};
    }

    __int64 kerosinpreis = Sim.HoleKerosinPreis(type);
    __int64 kosten = kerosinpreis * amount;
    __int64 rabatt = 0;

    if (amount >= 50000) {
        rabatt = kosten / 10;
    } else if (amount >= 10000) {
        rabatt = kosten / 20;
    }

    // Geldmenge begrenzen, damit man sich nichts in den Bankrott schießt
    if (kosten - rabatt > qPlayer.Money - (DEBT_LIMIT)) {
        amount = (qPlayer.Money - (DEBT_LIMIT)) / kerosinpreis;
        if (amount < 0) {
            amount = 0;
        }
        kosten = kerosinpreis * amount;

        if (amount >= 50000) {
            rabatt = kosten / 10;
        } else if (amount >= 10000) {
            rabatt = kosten / 20;
        } else {
            rabatt = 0;
        }
    }
    return {kosten, rabatt, amount};
}

SLONG GameMechanic::setSaboteurTarget(PLAYER &qPlayer, SLONG target) {
    if (target < 0 || target >= 4) {
        hprintf("GameMechanic::setSaboteurTarget: Invalid target.");
        return -1;
    }

    if (Sim.Players.Players[target].IsOut != 0) {
        return -1;
    }

    qPlayer.ArabOpferSelection = target;
    return target;
}

GameMechanic::CheckSabotage GameMechanic::checkPrerequisitesForSaboteurJob(PLAYER &qPlayer, SLONG type, SLONG number) {
    if (checkSaboteurBusy(qPlayer)) {
        return {CheckSabotageResult::DeniedInvalidParam, 0};
    }

    auto victimID = qPlayer.ArabOpferSelection;
    if (type == 0) {
        if (number < 1 || number >= 6) {
            hprintf("GameMechanic::checkPrerequisitesForSaboteurJob: Invalid job number.");
            return {CheckSabotageResult::DeniedInvalidParam, 0};
        }

        /* check security */
        if (number == 1 && ((Sim.Players.Players[victimID].SecurityFlags & (1 << 6)) != 0U)) {
            return {CheckSabotageResult::DeniedSecurity, 2096, Sim.Players.Players[victimID].AirlineX};
        } else if (number == 2 && ((Sim.Players.Players[victimID].SecurityFlags & (1 << 6)) != 0U)) {
            return {CheckSabotageResult::DeniedSecurity, 2096, Sim.Players.Players[victimID].AirlineX};
        } else if (number == 3 && ((Sim.Players.Players[victimID].SecurityFlags & (1 << 7)) != 0U)) {
            return {CheckSabotageResult::DeniedSecurity, 2097, Sim.Players.Players[victimID].AirlineX};
        } else if (number == 4 && ((Sim.Players.Players[victimID].SecurityFlags & (1 << 7)) != 0U)) {
            return {CheckSabotageResult::DeniedSecurity, 2097, Sim.Players.Players[victimID].AirlineX};
        } else if (number == 5 && ((Sim.Players.Players[victimID].SecurityFlags & (1 << 6)) != 0U)) {
            return {CheckSabotageResult::DeniedSecurity, 2096, Sim.Players.Players[victimID].AirlineX};
        } else if (qPlayer.Money - SabotagePrice[number - 1] < DEBT_LIMIT) {
            return {CheckSabotageResult::DeniedNotEnoughMoney, 6000};
        }

        qPlayer.ArabModeSelection = {type, number};
        return {CheckSabotageResult::Ok, 1060};
    }
    if (type == 1) {
        if (number < 1 || number >= 5) {
            hprintf("GameMechanic::checkPrerequisitesForSaboteurJob: Invalid job number.");
            return {CheckSabotageResult::DeniedInvalidParam, 0};
        }

        /* check security */
        if (number == 1 && ((Sim.Players.Players[victimID].SecurityFlags & (1 << 0)) != 0U)) {
            return {CheckSabotageResult::DeniedSecurity, 2090, Sim.Players.Players[victimID].AirlineX};
        } else if (number == 2 && ((Sim.Players.Players[victimID].SecurityFlags & (1 << 1)) != 0U)) {
            return {CheckSabotageResult::DeniedSecurity, 2091, Sim.Players.Players[victimID].AirlineX};
        } else if (number == 3 && ((Sim.Players.Players[victimID].SecurityFlags & (1 << 0)) != 0U)) {
            return {CheckSabotageResult::DeniedSecurity, 2090, Sim.Players.Players[victimID].AirlineX};
        } else if (number == 4 && ((Sim.Players.Players[victimID].SecurityFlags & (1 << 2)) != 0U)) {
            return {CheckSabotageResult::DeniedSecurity, 2092, Sim.Players.Players[victimID].AirlineX};
        } else if (qPlayer.Money - SabotagePrice2[number - 1] < DEBT_LIMIT) {
            return {CheckSabotageResult::DeniedNotEnoughMoney, 6000};
        } else if (number == 2 && (Sim.Players.Players[victimID].HasItem(ITEM_LAPTOP) == 0)) {
            return {CheckSabotageResult::DeniedNoLaptop, 1300};
        }

        qPlayer.ArabModeSelection = {type, number};
        return {CheckSabotageResult::Ok, 1160};
    }
    if (type == 2) {
        if (number < 1 || number >= 7) {
            hprintf("GameMechanic::checkPrerequisitesForSaboteurJob: Invalid job number.");
            return {CheckSabotageResult::DeniedInvalidParam, 0};
        }

        /* check security */
        if (number == 1 && ((Sim.Players.Players[victimID].SecurityFlags & (1 << 8)) != 0U)) {
            return {CheckSabotageResult::DeniedSecurity, 2098, Sim.Players.Players[victimID].AirlineX};
        } else if (number == 2 && ((Sim.Players.Players[victimID].SecurityFlags & (1 << 5)) != 0U)) {
            return {CheckSabotageResult::DeniedSecurity, 2095, Sim.Players.Players[victimID].AirlineX};
        } else if (number == 3 && ((Sim.Players.Players[victimID].SecurityFlags & (1 << 5)) != 0U)) {
            return {CheckSabotageResult::DeniedSecurity, 2095, Sim.Players.Players[victimID].AirlineX};
        } else if (number == 4 && ((Sim.Players.Players[victimID].SecurityFlags & (1 << 3)) != 0U)) {
            return {CheckSabotageResult::DeniedSecurity, 2093, Sim.Players.Players[victimID].AirlineX};
        } else if (number == 5 && ((Sim.Players.Players[victimID].SecurityFlags & (1 << 8)) != 0U)) {
            return {CheckSabotageResult::DeniedSecurity, 2098, Sim.Players.Players[victimID].AirlineX};
        } else if (number == 6 && ((Sim.Players.Players[victimID].SecurityFlags & (1 << 4)) != 0U)) {
            return {CheckSabotageResult::DeniedSecurity, 2094, Sim.Players.Players[victimID].AirlineX};
        } else if (qPlayer.Money - SabotagePrice3[number - 1] < DEBT_LIMIT) {
            return {CheckSabotageResult::DeniedNotEnoughMoney, 6000};
        }

        qPlayer.ArabModeSelection = {type, number};
        return {CheckSabotageResult::Ok, 1160};
    }
    hprintf("GameMechanic::checkPrerequisitesForSaboteurJob: Invalid type.");
    return {CheckSabotageResult::DeniedInvalidParam, 0};
}

bool GameMechanic::activateSaboteurJob(PLAYER &qPlayer) {
    SLONG type = qPlayer.ArabModeSelection.first;
    SLONG number = qPlayer.ArabModeSelection.second;
    auto ret = checkPrerequisitesForSaboteurJob(qPlayer, type, number);
    if (ret.result != CheckSabotageResult::Ok) {
        return false;
    }

    auto victimID = qPlayer.ArabOpferSelection;
    if (type == 0) {
        if (number < 1 || number >= 6) {
            hprintf("GameMechanic::activateSaboteurJob: Invalid job number.");
            return false;
        }
        if (qPlayer.ArabPlaneSelection == -1) {
            hprintf("GameMechanic::activateSaboteurJob: Need plane or route ID for this job");
            return false;
        }

        if (number == 1) {
            Sim.Players.Players[victimID].SecurityNeeded |= (1 << 6);
        } else if (number == 2) {
            Sim.Players.Players[victimID].SecurityNeeded |= (1 << 6);
        } else if (number == 3) {
            Sim.Players.Players[victimID].SecurityNeeded |= (1 << 7);
        } else if (number == 4) {
            Sim.Players.Players[victimID].SecurityNeeded |= (1 << 7);
        } else if (number == 5) {
            Sim.Players.Players[victimID].SecurityNeeded |= (1 << 6);
        }

        qPlayer.ArabOpfer = victimID;
        qPlayer.ArabOpfer2 = victimID;
        qPlayer.ArabOpfer3 = victimID;
        qPlayer.ArabOpferSelection = {};

        qPlayer.ArabMode = number;
        qPlayer.ArabModeSelection = {};

        qPlayer.ArabActive = FALSE;
        qPlayer.ArabPlane = qPlayer.ArabPlaneSelection;
        qPlayer.ArabPlaneSelection = -1;

        qPlayer.ArabTrust = min(6, qPlayer.ArabMode + 1);

        qPlayer.ChangeMoney(-SabotagePrice[qPlayer.ArabMode - 1], 2080, "");
        qPlayer.DoBodyguardRabatt(SabotagePrice[qPlayer.ArabMode - 1]);
        SIM::SendSimpleMessage(ATNET_CHANGEMONEY, 0, Sim.localPlayer, -SabotagePrice[qPlayer.ArabMode - 1], -1);

        qPlayer.NetSynchronizeSabotage();
    } else if (type == 1) {
        if (number < 1 || number >= 5) {
            hprintf("GameMechanic::activateSaboteurJob: Invalid job number.");
            return false;
        }

        if (number == 1) {
            Sim.Players.Players[victimID].SecurityNeeded |= (1 << 0);
        } else if (number == 2) {
            Sim.Players.Players[victimID].SecurityNeeded |= (1 << 1);
        } else if (number == 3) {
            Sim.Players.Players[victimID].SecurityNeeded |= (1 << 0);
        } else if (number == 4) {
            Sim.Players.Players[victimID].SecurityNeeded |= (1 << 2);
        }

        qPlayer.ArabOpfer = victimID;
        qPlayer.ArabOpfer2 = victimID;
        qPlayer.ArabOpfer3 = victimID;
        qPlayer.ArabOpferSelection = {};

        qPlayer.ArabMode2 = number;
        qPlayer.ArabModeSelection = {};

        qPlayer.ArabActive = FALSE;
        qPlayer.ArabPlane = qPlayer.ArabPlaneSelection;
        qPlayer.ArabPlaneSelection = -1;

        qPlayer.ArabTrust = min(6, qPlayer.ArabMode2 + 1);

        qPlayer.ChangeMoney(-SabotagePrice2[qPlayer.ArabMode2 - 1], 2080, "");
        qPlayer.DoBodyguardRabatt(SabotagePrice2[qPlayer.ArabMode2 - 1]);
        SIM::SendSimpleMessage(ATNET_CHANGEMONEY, 0, Sim.localPlayer, -SabotagePrice2[qPlayer.ArabMode2 - 1], -1);

        qPlayer.NetSynchronizeSabotage();
    } else if (type == 2) {
        if (number < 1 || number >= 5) {
            hprintf("GameMechanic::activateSaboteurJob: Invalid job number.");
            return false;
        }
        if (number >= 5 && qPlayer.ArabPlaneSelection == -1) {
            hprintf("GameMechanic::activateSaboteurJob: Need plane or route ID for this job");
            return false;
        }

        if (number == 1) {
            Sim.Players.Players[victimID].SecurityNeeded |= (1 << 8);
        } else if (number == 2) {
            Sim.Players.Players[victimID].SecurityNeeded |= (1 << 5);
        } else if (number == 3) {
            Sim.Players.Players[victimID].SecurityNeeded |= (1 << 5);
        } else if (number == 4) {
            Sim.Players.Players[victimID].SecurityNeeded |= (1 << 3);
        }

        qPlayer.ArabOpfer = victimID;
        qPlayer.ArabOpfer2 = victimID;
        qPlayer.ArabOpfer3 = victimID;
        qPlayer.ArabOpferSelection = {};

        qPlayer.ArabMode3 = number;
        qPlayer.ArabModeSelection = {};

        qPlayer.ArabActive = FALSE;
        qPlayer.ArabPlane = qPlayer.ArabPlaneSelection;
        qPlayer.ArabPlaneSelection = -1;

        qPlayer.ArabTrust = min(6, qPlayer.ArabMode3 + 1);

        qPlayer.ChangeMoney(-SabotagePrice3[qPlayer.ArabMode3 - 1], 2080, "");
        qPlayer.DoBodyguardRabatt(SabotagePrice3[qPlayer.ArabMode3 - 1]);
        SIM::SendSimpleMessage(ATNET_CHANGEMONEY, 0, Sim.localPlayer, -SabotagePrice3[qPlayer.ArabMode3 - 1], -1);

        qPlayer.NetSynchronizeSabotage();
    } else {
        hprintf("GameMechanic::activateSaboteurJob: Invalid type.");
        return false;
    }
    return true;
}

void GameMechanic::paySaboteurFine(SLONG player, SLONG opfer) {
    auto fine = Sim.Players.Players[player].ArabHints * 10000;
    Sim.Players.Players[player].ChangeMoney(-fine, 2200, "");
    Sim.Players.Players[opfer].ChangeMoney(fine, 2201, "");
}

bool GameMechanic::takeOutCredit(PLAYER &qPlayer, SLONG amount) {
    if (amount < 1000 || amount > qPlayer.CalcCreditLimit()) {
        hprintf("GameMechanic::takeOutCredit: Invalid amount.");
        return false;
    }
    qPlayer.Credit += amount;
    qPlayer.ChangeMoney(amount, 2003, "");
    PLAYER::NetSynchronizeMoney();
    return true;
}

bool GameMechanic::payBackCredit(PLAYER &qPlayer, SLONG amount) {
    if (amount < 1000 || amount > qPlayer.Credit) {
        hprintf("GameMechanic::payBackCredit: Invalid amount.");
        return false;
    }
    qPlayer.Credit -= amount;
    qPlayer.ChangeMoney(-amount, 2004, "");
    PLAYER::NetSynchronizeMoney();
    return true;
}

void GameMechanic::setPlaneTargetZustand(PLAYER &qPlayer, SLONG idx, SLONG zustand) {
    if (!qPlayer.Planes.IsInAlbum(idx)) {
        hprintf("GameMechanic::setPlaneTargetZustand: Invalid plane index.");
        return;
    }

    qPlayer.Planes[idx].TargetZustand = UBYTE(zustand);
    qPlayer.NetUpdatePlaneProps(idx);
}

bool GameMechanic::toggleSecurity(PLAYER &qPlayer, SLONG securityType) {
    if (securityType < 0 || securityType >= 9) {
        hprintf("GameMechanic::toggleSecurity: Invalid security type.");
        return false;
    }

    if (securityType == 1 && (qPlayer.HasItem(ITEM_LAPTOP) == 0)) {
        return false;
    }

    if (securityType == 8) {
        if ((qPlayer.SecurityFlags & (1 << 11)) != 0U) {
            qPlayer.SecurityFlags = qPlayer.SecurityFlags & ~(1 << 8) & ~(1 << 10) & ~(1 << 11);
        } else if ((qPlayer.SecurityFlags & (1 << 10)) != 0U) {
            qPlayer.SecurityFlags = qPlayer.SecurityFlags | (1 << 11);
        } else if ((qPlayer.SecurityFlags & (1 << 8)) != 0U) {
            qPlayer.SecurityFlags = qPlayer.SecurityFlags | (1 << 10);
        } else {
            qPlayer.SecurityFlags = qPlayer.SecurityFlags | (1 << 8);
        }
    } else {
        qPlayer.SecurityFlags ^= (1 << securityType);
    }

    PLAYER::NetSynchronizeFlags();
    return true;
}

bool GameMechanic::GameMechanic::buyPlane(PLAYER &qPlayer, SLONG planeType, SLONG amount) {
    if (!PlaneTypes.IsInAlbum(planeType)) {
        hprintf("GameMechanic::buyPlane: Invalid plane type.");
        return false;
    }

    if (qPlayer.Money - PlaneTypes[planeType].Preis * amount < DEBT_LIMIT) {
        return false;
    }

    SLONG idx = planeType - 0x10000000;
    TEAKRAND rnd;
    rnd.SRand(Sim.Date);

    for (SLONG c = 0; c < amount; c++) {
        qPlayer.BuyPlane(idx, &rnd);
    }

    SIM::SendSimpleMessage(ATNET_BUY_NEW, 0, qPlayer.PlayerNum, amount, idx);

    qPlayer.DoBodyguardRabatt(PlaneTypes[planeType].Preis * amount);
    qPlayer.MapWorkers(FALSE);
    qPlayer.UpdatePersonalberater(1);

    return true;
}

bool GameMechanic::buyUsedPlane(PLAYER &qPlayer, SLONG planeID) {
    if (!Sim.UsedPlanes.IsInAlbum(planeID)) {
        hprintf("GameMechanic::buyUsedPlane: Invalid plane index.");
        return false;
    }

    if (qPlayer.Money - Sim.UsedPlanes[planeID].CalculatePrice() < DEBT_LIMIT) {
        return false;
    }

    if (qPlayer.Planes.GetNumFree() == 0) {
        qPlayer.Planes.ReSize(qPlayer.Planes.AnzEntries() + 10);
        qPlayer.Planes.RepairReferences();
    }
    Sim.UsedPlanes[planeID].WorstZustand = UBYTE(Sim.UsedPlanes[planeID].Zustand - 20);
    Sim.UsedPlanes[planeID].GlobeAngle = 0;
    qPlayer.Planes += Sim.UsedPlanes[planeID];

    SLONG cost = Sim.UsedPlanes[planeID].CalculatePrice();
    qPlayer.ChangeMoney(-cost, 2010, Sim.UsedPlanes[planeID].Name);
    SIM::SendSimpleMessage(ATNET_CHANGEMONEY, 0, Sim.localPlayer, -cost, STAT_A_SONSTIGES);

    qPlayer.DoBodyguardRabatt(cost);

    SLONG idx = planeID - 0x10000000;
    if (Sim.bNetwork != 0) {
        SIM::SendSimpleMessage(ATNET_ADVISOR, 0, 1, qPlayer.PlayerNum, idx);
        SIM::SendSimpleMessage(ATNET_BUY_USED, 0, qPlayer.PlayerNum, idx, Sim.Time);
    }

    Sim.UsedPlanes[planeID].Name.Empty();
    Sim.TickMuseumRefill = 0;

    qPlayer.MapWorkers(FALSE);
    qPlayer.UpdatePersonalberater(1);

    return true;
}

bool GameMechanic::sellPlane(PLAYER &qPlayer, SLONG planeID) {
    if (!qPlayer.Planes.IsInAlbum(planeID)) {
        hprintf("GameMechanic::sellPlane: Invalid plane index.");
        return false;
    }

    SLONG cost = qPlayer.Planes[planeID].CalculatePrice() * 9 / 10;

    qPlayer.ChangeMoney(cost, 2011, qPlayer.Planes[planeID].Name);
    if (qPlayer.PlayerNum == Sim.localPlayer) {
        SIM::SendSimpleMessage(ATNET_CHANGEMONEY, 0, Sim.localPlayer, cost, STAT_E_SONSTIGES);
    }

    qPlayer.Planes -= planeID;
    if (qPlayer.Planes.IsInAlbum(qPlayer.ReferencePlane) == 0) {
        qPlayer.ReferencePlane = -1;
    }

    SIM::SendSimpleMessage(ATNET_SELL_USED, 0, planeID, planeID);
    PLAYER::NetSynchronizeMoney();

    qPlayer.MapWorkers(FALSE);
    qPlayer.UpdatePersonalberater(1);

    return true;
}

bool GameMechanic::buyXPlane(PLAYER &qPlayer, const CString &filename, SLONG amount) {
    CString fn = FullFilename(filename, MyPlanePath);
    if (fn.empty()) {
        hprintf("GameMechanic::buyXPlane: Invalid filename.");
        return false;
    }

    CXPlane plane;
    plane.Load(fn);

    if (qPlayer.Money - plane.CalcCost() * amount < DEBT_LIMIT) {
        return false;
    }

    TEAKRAND rnd;
    rnd.SRand(Sim.Date);

    for (SLONG c = 0; c < amount; c++) {
        qPlayer.BuyPlane(plane, &rnd);
    }

    qPlayer.NetBuyXPlane(amount, plane);

    qPlayer.DoBodyguardRabatt(plane.CalcCost() * amount);
    qPlayer.MapWorkers(FALSE);
    qPlayer.UpdatePersonalberater(1);

    return true;
}

GameMechanic::OvertakeAirlineResult GameMechanic::canOvertakeAirline(PLAYER &qPlayer, SLONG targetAirline) {
    if (targetAirline < 0 || targetAirline >= 4) {
        hprintf("GameMechanic::canOvertakeAirline: Invalid targetAirline.");
        return OvertakeAirlineResult::DeniedInvalidParam;
    }

    if (targetAirline == qPlayer.PlayerNum) {
        return OvertakeAirlineResult::DeniedYourAirline;
    } else if (Sim.Players.Players[targetAirline].IsOut != 0) {
        return OvertakeAirlineResult::DeniedAlreadyGone;
    } else if (qPlayer.OwnsAktien[targetAirline] == 0) {
        return OvertakeAirlineResult::DeniedNoStock;
    } else if (qPlayer.OwnsAktien[targetAirline] < Sim.Players.Players[targetAirline].AnzAktien / 2) {
        return OvertakeAirlineResult::DeniedNotEnoughStock;
    } else if (Sim.Players.Players[targetAirline].OwnsAktien[qPlayer.PlayerNum] >= qPlayer.AnzAktien * 3 / 10) {
        return OvertakeAirlineResult::DeniedEnemyStock;
    }
    return OvertakeAirlineResult::Ok;
}

bool GameMechanic::overtakeAirline(PLAYER &qPlayer, SLONG targetAirline, bool liquidate) {
    if (OvertakeAirlineResult::Ok != canOvertakeAirline(qPlayer, targetAirline)) {
        hprintf("GameMechanic::overtakeAirline: Conditions not met.");
        return false;
    }

    Sim.Overtake = liquidate ? 2 : 1;
    Sim.OvertakenAirline = targetAirline;
    Sim.OvertakerAirline = qPlayer.PlayerNum;
    Sim.NetSynchronizeOvertake();
    return true;
}

GameMechanic::EmitStockResult GameMechanic::canEmitStock(PLAYER &qPlayer) {
    auto tmp = (qPlayer.MaxAktien - qPlayer.AnzAktien) / 100 * 100;
    if (tmp < 10000) {
        return EmitStockResult::DeniedTooMuch;
    } else if (qPlayer.Kurse[0] < 10) {
        return EmitStockResult::DeniedValueTooLow;
    }
    return EmitStockResult::Ok;
}

bool GameMechanic::emitStock(PLAYER &qPlayer, SLONG neueAktien, SLONG mode) {
    if (EmitStockResult::Ok != canEmitStock(qPlayer)) {
        hprintf("GameMechanic::emitStock: Conditions not met.");
        return false;
    }

    SLONG emissionsKurs = 0;
    SLONG marktAktien = 0;
    if (mode == 0) {
        emissionsKurs = SLONG(qPlayer.Kurse[0]) - 5;
        marktAktien = neueAktien;
    } else if (mode == 1) {
        emissionsKurs = SLONG(qPlayer.Kurse[0]) - 3;
        marktAktien = neueAktien * 8 / 10;
    } else if (mode == 2) {
        emissionsKurs = SLONG(qPlayer.Kurse[0]) - 1;
        marktAktien = neueAktien * 6 / 10;
    } else {
        hprintf("GameMechanic::emitStock: Invalid mode.");
        return false;
    }

    DOUBLE alterKurs = qPlayer.Kurse[0];
    __int64 emissionsWert = __int64(marktAktien) * emissionsKurs;
    __int64 emissionsGebuehr = __int64(neueAktien) * emissionsKurs / 10 / 100 * 100;

    qPlayer.ChangeMoney(emissionsWert, 3162, "");
    qPlayer.ChangeMoney(-emissionsGebuehr, 3160, "");

    __int64 preis = emissionsWert - emissionsGebuehr;
    if (qPlayer.PlayerNum == Sim.localPlayer) {
        SIM::SendSimpleMessage(ATNET_CHANGEMONEY, 0, Sim.localPlayer, preis, STAT_E_SONSTIGES);
    }

    qPlayer.Kurse[0] = (qPlayer.Kurse[0] * __int64(qPlayer.AnzAktien) + emissionsWert) / (qPlayer.AnzAktien + marktAktien);
    if (qPlayer.Kurse[0] < 0) {
        qPlayer.Kurse[0] = 0;
    }

    // Entschädigung +/-
    auto kursDiff = (alterKurs - qPlayer.Kurse[0]);
    qPlayer.ChangeMoney(-SLONG((qPlayer.AnzAktien - qPlayer.OwnsAktien[qPlayer.PlayerNum]) * kursDiff), 3161, "");
    for (SLONG c = 0; c < Sim.Players.Players.AnzEntries(); c++) {
        if (c != qPlayer.PlayerNum) {
            auto entschaedigung = SLONG(Sim.Players.Players[c].OwnsAktien[qPlayer.PlayerNum] * kursDiff);

            Sim.Players.Players[c].ChangeMoney(entschaedigung, 3163, "");
            SIM::SendSimpleMessage(ATNET_CHANGEMONEY, 0, c, entschaedigung, STAT_E_SONSTIGES);
        }
    }

    if (Sim.bNetwork != 0) {
        SIM::SendSimpleMessage(ATNET_ADVISOR, 0, 3, qPlayer.PlayerNum, neueAktien);
    }

    qPlayer.AnzAktien += neueAktien;
    qPlayer.OwnsAktien[qPlayer.PlayerNum] += (neueAktien - marktAktien);
    PLAYER::NetSynchronizeMoney();

    return true;
}

GameMechanic::ExpandAirportResult GameMechanic::canExpandAirport(PLAYER &qPlayer) {
    if (Airport.GetNumberOfFreeGates() != 0) {
        return ExpandAirportResult::DeniedFreeGates;
    } else if (Sim.CheckIn >= 5 || (Sim.CheckIn >= 2 && Sim.Difficulty <= DIFF_NORMAL && Sim.Difficulty != DIFF_FREEGAME)) {
        return ExpandAirportResult::DeniedLimitReached;
    } else if (Sim.Date < 8) {
        return ExpandAirportResult::DeniedTooEarly;
    } else if (Sim.Date - Sim.LastExpansionDate < 3) {
        return ExpandAirportResult::DeniedAlreadyExpanded;
    } else if (Sim.ExpandAirport != 0) {
        return ExpandAirportResult::DeniedExpandingRightNow;
    }
    return ExpandAirportResult::Ok;
}

bool GameMechanic::expandAirport(PLAYER &qPlayer) {
    if (ExpandAirportResult::Ok != canExpandAirport(qPlayer)) {
        hprintf("GameMechanic::expandAirport: Conditions not met.");
        return false;
    }

    Sim.ExpandAirport = TRUE;
    SIM::SendSimpleMessage(ATNET_EXPAND_AIRPORT);
    qPlayer.ChangeMoney(-1000000, 3170, "");
    return true;
}

SLONG GameMechanic::setMechMode(PLAYER &qPlayer, SLONG mode) {
    qPlayer.MechMode = mode;
    qPlayer.NetUpdatePlaneProps();
    return gRepairPrice[qPlayer.MechMode] * qPlayer.Planes.GetNumUsed() / 30;
}

void GameMechanic::increaseAllSalaries(PLAYER &qPlayer) {
    Workers.Gehaltsaenderung(1, qPlayer.PlayerNum);
    qPlayer.StrikePlanned = FALSE;

    while ((Workers.GetAverageHappyness(qPlayer.PlayerNum) - static_cast<SLONG>(Workers.GetMinHappyness(qPlayer.PlayerNum) < 0) * 10 < 20) ||
           (Workers.GetAverageHappyness(qPlayer.PlayerNum) - static_cast<SLONG>(Workers.GetMinHappyness(qPlayer.PlayerNum) < 0) * 10 < 0)) {
        Workers.AddHappiness(qPlayer.PlayerNum, 10);
    }
}

void GameMechanic::decreaseAllSalaries(PLAYER &qPlayer) { Workers.Gehaltsaenderung(0, qPlayer.PlayerNum); }

void GameMechanic::endStrike(PLAYER &qPlayer, EndStrikeMode mode) {
    if (mode == EndStrikeMode::Salary) {
        qPlayer.StrikeEndType = 2; // Streik beendet durch Gehaltserhöhung
        qPlayer.StrikeEndCountdown = 2;
        increaseAllSalaries(qPlayer);
    } else if (mode == EndStrikeMode::Threat) {
        qPlayer.StrikeEndType = 1; // Streik beendet durch Drohung
        qPlayer.StrikeEndCountdown = 4;
        Workers.AddHappiness(qPlayer.PlayerNum, -20);
    } else if (mode == EndStrikeMode::Drunk) {
        qPlayer.StrikeEndType = 3; // Streik beendet durch Trinker
        qPlayer.StrikeEndCountdown = 4;
    } else {
        hprintf("GameMechanic::endStrike: Invalid EndStrikeMode");
    }
}

bool GameMechanic::buyAdvertisement(PLAYER &qPlayer, SLONG adCampaignType, SLONG adCampaignSize, SLONG routeID) {
    if (adCampaignType < 0 || adCampaignType >= 3) {
        hprintf("GameMechanic::buyAdvertisement: Invalid adCampaignType.");
        return false;
    }
    if (adCampaignSize < 0 || adCampaignSize >= 6) {
        hprintf("GameMechanic::buyAdvertisement: Invalid adCampaignSize.");
        return false;
    }
    if (adCampaignType == 1 && routeID == -1) {
        hprintf("GameMechanic::buyAdvertisement: Invalid routeID.");
        return false;
    }
    if (adCampaignType != 1 && routeID != -1) {
        hprintf("GameMechanic::buyAdvertisement: RouteID must not be given for this mode.");
        return false;
    }

    SLONG cost = gWerbePrice[adCampaignType * 6 + adCampaignSize];
    if (qPlayer.Money - cost < DEBT_LIMIT) {
        return false;
    }

    if (adCampaignType == 0) {
        qPlayer.Image += cost / 10000 * (adCampaignSize + 6) / 55;
        Limit(SLONG(-1000), qPlayer.Image, SLONG(1000));

        if (adCampaignSize == 0) {
            for (SLONG c = 0; c < Sim.Players.AnzPlayers; c++) {
                if ((Sim.Players.Players[c].Owner == 0U) && (Sim.Players.Players[c].IsOut == 0)) {
                    Sim.Players.Players[c].Letters.AddLetter(
                        TRUE, CString(bprintf(StandardTexte.GetS(TOKEN_LETTER, 9900), (LPCTSTR)qPlayer.AirlineX)),
                        CString(bprintf(StandardTexte.GetS(TOKEN_LETTER, 9901), (LPCTSTR)qPlayer.AirlineX)),
                        CString(bprintf(StandardTexte.GetS(TOKEN_LETTER, 9902), (LPCTSTR)qPlayer.NameX, (LPCTSTR)qPlayer.AirlineX)), -1);
                }
            }
        }
    } else if (adCampaignType == 1) {
        auto &qRoute = qPlayer.RentRouten.RentRouten[routeID];
        qRoute.Image += UBYTE(cost / 30000);
        Limit(static_cast<UBYTE>(0), qRoute.Image, static_cast<UBYTE>(100));

        /*for (SLONG c = qPlayer.RentRouten.RentRouten.AnzEntries() - 1; c >= 0; c--) {
            if (Routen.IsInAlbum(c) != 0) {
                if (Routen[c].VonCity == Routen[routeID].NachCity && Routen[c].NachCity == Routen[routeID].VonCity) {
                    qPlayer.RentRouten.RentRouten[c].Image += UBYTE(cost / 30000);
                    Limit(static_cast<UBYTE>(0), qPlayer.RentRouten.RentRouten[c].Image, static_cast<UBYTE>(100));
                    break;
                }
            }
        }*/

        if (adCampaignSize == 0) {
            for (SLONG c = 0; c < Sim.Players.AnzPlayers; c++) {
                if ((Sim.Players.Players[c].Owner == 0U) && (Sim.Players.Players[c].IsOut == 0)) {
                    Sim.Players.Players[c].Letters.AddLetter(
                        TRUE,
                        CString(bprintf(StandardTexte.GetS(TOKEN_LETTER, 9910), (LPCTSTR)Cities[Routen[routeID].VonCity].Name,
                                        (LPCTSTR)Cities[Routen[routeID].NachCity].Name, (LPCTSTR)qPlayer.AirlineX)),
                        CString(bprintf(StandardTexte.GetS(TOKEN_LETTER, 9911), (LPCTSTR)Cities[Routen[routeID].VonCity].Name,
                                        (LPCTSTR)Cities[Routen[routeID].NachCity].Name, (LPCTSTR)qPlayer.AirlineX)),
                        CString(bprintf(StandardTexte.GetS(TOKEN_LETTER, 9912), (LPCTSTR)qPlayer.NameX, (LPCTSTR)qPlayer.AirlineX)), -1);
                }
            }
        }
    } else if (adCampaignType == 2) {
        qPlayer.Image += cost / 15000 * (adCampaignSize + 6) / 55;
        Limit(SLONG(-1000), qPlayer.Image, SLONG(1000));

        for (SLONG c = 0; c < qPlayer.RentRouten.RentRouten.AnzEntries(); c++) {
            if (qPlayer.RentRouten.RentRouten[c].Rang != 0U) {
                qPlayer.RentRouten.RentRouten[c].Image += UBYTE(cost * (adCampaignSize + 6) / 6 / 120000);
                Limit(static_cast<UBYTE>(0), qPlayer.RentRouten.RentRouten[c].Image, static_cast<UBYTE>(100));
            }
        }

        if (adCampaignSize == 0) {
            for (SLONG c = 0; c < Sim.Players.AnzPlayers; c++) {
                if ((Sim.Players.Players[c].Owner == 0U) && (Sim.Players.Players[c].IsOut == 0)) {
                    Sim.Players.Players[c].Letters.AddLetter(
                        TRUE, CString(bprintf(StandardTexte.GetS(TOKEN_LETTER, 9920), (LPCTSTR)qPlayer.AirlineX)),
                        CString(bprintf(StandardTexte.GetS(TOKEN_LETTER, 9921), (LPCTSTR)qPlayer.AirlineX)),
                        CString(bprintf(StandardTexte.GetS(TOKEN_LETTER, 9922), (LPCTSTR)qPlayer.NameX, (LPCTSTR)qPlayer.AirlineX)), -1);
                }
            }
        }
    }

    qPlayer.ChangeMoney(-cost, adCampaignSize + 3120, "");
    if (qPlayer.PlayerNum == Sim.localPlayer) {
        SIM::SendSimpleMessage(ATNET_CHANGEMONEY, 0, Sim.localPlayer, -cost, STAT_A_SONSTIGES);
    }

    PLAYER::NetSynchronizeImage();
    qPlayer.DoBodyguardRabatt(cost);

    return true;
}

bool GameMechanic::takeFlightJob(PLAYER &qPlayer, SLONG par1, SLONG par2, SLONG &outObjectId) {
    auto &qAuftrag = AuslandsAuftraege[par1][par2];
    if (qAuftrag.Praemie <= 0) {
        return false;
    }
    if (qPlayer.Auftraege.GetNumFree() < 3) {
        qPlayer.Auftraege.ReSize(qPlayer.Auftraege.AnzEntries() + 10);
    }

    outObjectId = (qPlayer.Auftraege += qAuftrag);
    qPlayer.NetUpdateOrder(qAuftrag);
    qPlayer.Statistiken[STAT_AUFTRAEGE].AddAtPastDay(1);

    qAuftrag.Praemie = 0;
    qPlayer.NetUpdateTook(4, par2, par1);

    return true;
}

bool GameMechanic::takeFreightJob(PLAYER &qPlayer, SLONG par1, SLONG par2, SLONG &outObjectId) {
    auto &qFracht = AuslandsFrachten[par1][par2];
    if (qFracht.Praemie <= 0) {
        return false;
    }
    if (qPlayer.Frachten.GetNumFree() < 3) {
        qPlayer.Frachten.ReSize(qPlayer.Frachten.AnzEntries() + 10);
    }

    outObjectId = (qPlayer.Frachten += qFracht);
    qPlayer.NetUpdateFreightOrder(qFracht);
    qPlayer.Statistiken[STAT_AUFTRAEGE].AddAtPastDay(1);

    qFracht.Praemie = 0;
    qPlayer.NetUpdateTook(5, par2, par1);

    return true;
}

bool GameMechanic::refillFlightJobs(SLONG cityNum) {
    AuslandsAuftraege[cityNum].RefillForAusland(cityNum);
    AuslandsFrachten[cityNum].RefillForAusland(cityNum);

    return true;
}

bool GameMechanic::hireWorker(PLAYER &qPlayer, CWorker &qWorker) {
    if (qWorker.Employer != WORKER_JOBLESS) {
        hprintf("GameMechanic::fireWorker: Conditions not met.");
        return false;
    }

    qWorker.Employer = qPlayer.PlayerNum;
    qWorker.PlaneId = -1;
    qPlayer.MapWorkers(TRUE);

    return true;
}

bool GameMechanic::fireWorker(PLAYER &qPlayer, CWorker &qWorker) {
    if (qWorker.Employer != qPlayer.PlayerNum) {
        hprintf("GameMechanic::fireWorker: Conditions not met.");
        return false;
    }

    qWorker.Employer = WORKER_JOBLESS;
    if (qWorker.TimeInPool > 0) {
        qWorker.TimeInPool = 0;
    }
    qPlayer.MapWorkers(TRUE);

    return true;
}

void GameMechanic::executeAirlineOvertake() {
    SLONG c = 0;
    SLONG d = 0;
    PLAYER &Overtaker = Sim.Players.Players[Sim.OvertakerAirline];
    PLAYER &Overtaken = Sim.Players.Players[Sim.OvertakenAirline];

    Overtaken.ArabMode = 0;
    Overtaken.ArabMode2 = 0;
    Overtaken.ArabMode3 = 0;

    Sim.ShowExtrablatt = Sim.OvertakerAirline * 4 + Sim.OvertakenAirline;

    for (c = Sim.Persons.AnzEntries() - 1; c >= 0; c--) {
        if (Sim.Persons.IsInAlbum(c) != 0) {
            if (Clans[static_cast<SLONG>(Sim.Persons[c].ClanId)].Type < CLAN_PLAYER1 && Sim.Persons[c].Reason == REASON_FLYING &&
                Sim.Persons[c].FlightAirline == Sim.OvertakenAirline) {
                Sim.Persons -= c;
            }
        }
    }

    if (Sim.Overtake == 1) // Schlucken
    {
        SLONG Piloten = 0;
        SLONG Begleiter = 0;

        // Arbeiter übernehmen:
        for (c = 0; c < Workers.Workers.AnzEntries(); c++) {
            if (Workers.Workers[c].Employer == Sim.OvertakenAirline) {
                Workers.Workers[c].Employer = Sim.OvertakerAirline;
                Workers.Workers[c].PlaneId = -1;
            }
        }

        // Flugzeuge übernehmen, Flugpläne löschen, alle nach Berlin setzen, ggf. Leute dafür einstellen
        for (c = 0; c < Overtaken.Planes.AnzEntries(); c++) {
            if (Overtaken.Planes.IsInAlbum(c) != 0) {
                Overtaken.Planes[c].ClearSaldo();

                for (d = 0; d < Overtaken.Planes[c].Flugplan.Flug.AnzEntries(); d++) {
                    Overtaken.Planes[c].Flugplan.Flug[d].ObjectType = 0;
                }

                Overtaken.Planes[c].Ort = Sim.HomeAirportId;
                Overtaken.Planes[c].Position = Cities[Sim.HomeAirportId].GlobusPosition;
                Overtaken.Planes[c].Flugplan.StartCity = Sim.HomeAirportId;
                Overtaken.Planes[c].Flugplan.NextFlight = -1;
                Overtaken.Planes[c].Flugplan.NextStart = -1;

                for (d = 0; d < Sim.Persons.AnzEntries(); d++) {
                    if (Sim.Persons.IsInAlbum(d) != 0) {
                        if (Sim.Persons[d].FlightAirline == Sim.OvertakenAirline) {
                            Sim.Persons[d].State = PERSON_LEAVING;
                        }
                    }
                }

                // Piloten+=PlaneTypes[Overtaken.Planes[c].TypeId].AnzPiloten;
                Piloten += Overtaken.Planes[c].ptAnzPiloten;
                Begleiter += Overtaken.Planes[c].ptAnzBegleiter;
                // Begleiter+=PlaneTypes[Overtaken.Planes[c].TypeId].AnzBegleiter;

                if (Overtaker.Planes.GetNumFree() <= 0) {
                    Overtaker.Planes.ReSize(Overtaker.Planes.AnzEntries() + 5);
                    Overtaker.Planes.RepairReferences();
                }

                d = (Overtaker.Planes += CPlane());
                Overtaker.Planes[d] = Overtaken.Planes[c];
            }
        }
        Overtaken.Planes.ReSize(0);
        Overtaken.Planes.RepairReferences();

        // Ggf. virtuelle Arbeiter erzeugen:
        if (Overtaken.Owner != 0) {
            for (c = 0; c < Workers.Workers.AnzEntries(); c++) {
                if (Workers.Workers[c].Employer == WORKER_RESERVE && Workers.Workers[c].Typ == WORKER_PILOT && Piloten > 0) {
                    Workers.Workers[c].Employer = Sim.OvertakerAirline;
                    Piloten--;
                } else if (Workers.Workers[c].Employer == WORKER_RESERVE && Workers.Workers[c].Typ == WORKER_STEWARDESS && Begleiter > 0) {
                    Workers.Workers[c].Employer = Sim.OvertakerAirline;
                    Begleiter--;
                }
            }
        }

        // Das nicht Broadcasten. Das bekommen die anderen Spieler auch selber mit:
        BOOL bOldNetwork = Sim.bNetwork;
        Sim.bNetwork = 0;
        Overtaker.MapWorkers(FALSE);
        Sim.bNetwork = bOldNetwork;

        // Geld und Aktien übernehmen:
        Overtaker.ChangeMoney(Overtaken.Money, 3180, "");
        Overtaker.Credit += Overtaken.Credit;
        Overtaker.AnzAktien += Overtaken.AnzAktien;
        for (c = 0; c < 4; c++) {
            Overtaker.OwnsAktien[c] += Overtaken.OwnsAktien[c];
            Overtaker.AktienWert[c] += Overtaken.AktienWert[c];
        }

        for (c = 0; c < Sim.Players.Players.AnzEntries(); c++) {
            if (Sim.Players.Players[c].IsOut == 0) {
                if (c != Sim.OvertakenAirline && c != Sim.OvertakerAirline) {
                    Sim.Players.Players[c].OwnsAktien[Sim.OvertakerAirline] += Sim.Players.Players[c].OwnsAktien[Sim.OvertakenAirline];
                    Sim.Players.Players[c].AktienWert[Sim.OvertakerAirline] += Sim.Players.Players[c].AktienWert[Sim.OvertakenAirline];
                }

                Sim.Players.Players[c].OwnsAktien[Sim.OvertakenAirline] = 0;
                Sim.Players.Players[c].AktienWert[Sim.OvertakenAirline] = 0;
            }
        }

        // Von dem Übernommenen hat keiner mehr Aktien:
        for (c = 0; c < Sim.Players.Players.AnzEntries(); c++) {
            Sim.Players.Players[c].OwnsAktien[Sim.OvertakenAirline] = 0;
        }

        // Gates übernehmen:
        for (c = 0; c < Overtaken.Gates.Gates.AnzEntries(); c++) {
            if (Overtaken.Gates.Gates[c].Miete != -1) {
                for (d = 0; d < Overtaker.Gates.Gates.AnzEntries(); d++) {
                    if (Overtaker.Gates.Gates[d].Miete == -1) {
                        Overtaker.Gates.Gates[d].Miete = Overtaken.Gates.Gates[c].Miete;
                        Overtaker.Gates.Gates[d].Nummer = Overtaken.Gates.Gates[c].Nummer;
                        Overtaker.Gates.NumRented++;
                        break;
                    }
                }

                Overtaken.Gates.Gates[c].Miete = -1;
            }
        }

        // Routen übernehmen:
        for (c = 0; c < Overtaker.RentRouten.RentRouten.AnzEntries(); c++) {
            if (Overtaker.RentRouten.RentRouten[c].Rang == 0 && Overtaken.RentRouten.RentRouten[c].Rang != 0) {
                Overtaker.RentRouten.RentRouten[c] = Overtaken.RentRouten.RentRouten[c];
                Overtaken.RentRouten.RentRouten[c].Rang = 0;
            } else if (Overtaker.RentRouten.RentRouten[c].Rang != 0 && Overtaken.RentRouten.RentRouten[c].Rang != 0 &&
                       Overtaker.RentRouten.RentRouten[c].Rang > Overtaken.RentRouten.RentRouten[c].Rang) {
                for (d = 0; d < Sim.Players.Players.AnzEntries(); d++) {
                    if ((Sim.Players.Players[d].IsOut == 0) && d != Sim.OvertakerAirline && d != Sim.OvertakenAirline &&
                        Sim.Players.Players[d].RentRouten.RentRouten[c].Rang > Overtaken.RentRouten.RentRouten[c].Rang) {
                        Sim.Players.Players[d].RentRouten.RentRouten[c].Rang--;
                    }
                }

                Overtaker.RentRouten.RentRouten[c].Rang = Overtaken.RentRouten.RentRouten[c].Rang;
                Overtaken.RentRouten.RentRouten[c].Rang = 0;
            }
        }

        // Städte übernehmen:
        for (c = 0; c < Overtaker.RentCities.RentCities.AnzEntries(); c++) {
            if (Overtaker.RentCities.RentCities[c].Rang == 0 && Overtaken.RentCities.RentCities[c].Rang != 0) {
                Overtaker.RentCities.RentCities[c] = Overtaken.RentCities.RentCities[c];
                Overtaken.RentCities.RentCities[c].Rang = 0;
            } else if (Overtaker.RentCities.RentCities[c].Rang != 0 && Overtaken.RentCities.RentCities[c].Rang != 0 &&
                       Overtaker.RentCities.RentCities[c].Rang > Overtaken.RentCities.RentCities[c].Rang) {
                for (d = 0; d < Sim.Players.Players.AnzEntries(); d++) {
                    if ((Sim.Players.Players[d].IsOut == 0) && d != Sim.OvertakerAirline && d != Sim.OvertakenAirline &&
                        Sim.Players.Players[d].RentCities.RentCities[c].Rang > Overtaken.RentCities.RentCities[c].Rang) {
                        Sim.Players.Players[d].RentCities.RentCities[c].Rang--;
                    }
                }

                Overtaker.RentCities.RentCities[c].Rang = Overtaken.RentCities.RentCities[c].Rang;
                Overtaken.RentCities.RentCities[c].Rang = 0;
            }
        }
    } else if (Sim.Overtake == 2) // Liquidieren
    {
        // Leute rauswerfen:
        Sim.Players.Players[Sim.OvertakenAirline].SackWorkers();

        // Flugzeuge verkaufen:
        for (c = 0; c < Overtaken.Planes.AnzEntries(); c++) {
            if (Overtaken.Planes.IsInAlbum(c) != 0) {
                Overtaken.Money += Overtaken.Planes[c].CalculatePrice();
            }
        }
        Overtaken.Planes.ReSize(0);
        Overtaken.Planes.RepairReferences();

        // Gates freigeben:
        for (c = 0; c < Overtaken.Gates.Gates.AnzEntries(); c++) {
            if (Overtaken.Gates.Gates[c].Miete != -1) {
                Overtaken.Gates.Gates[c].Miete = -1;
            }
        }

        // Routen freigeben:
        for (c = 0; c < Overtaken.RentRouten.RentRouten.AnzEntries(); c++) {
            if (Overtaken.RentRouten.RentRouten[c].Rang != 0) {
                for (d = 0; d < Sim.Players.Players.AnzEntries(); d++) {
                    if ((Sim.Players.Players[d].IsOut == 0) && d != Sim.OvertakenAirline &&
                        Sim.Players.Players[d].RentRouten.RentRouten[c].Rang > Overtaken.RentRouten.RentRouten[c].Rang) {
                        Sim.Players.Players[d].RentRouten.RentRouten[c].Rang--;
                    }
                }

                Overtaken.RentRouten.RentRouten[c].Rang = 0;
            }
        }

        // Cities freigeben
        for (c = 0; c < Overtaken.RentCities.RentCities.AnzEntries(); c++) {
            if (Overtaken.RentCities.RentCities[c].Rang != 0) {
                for (d = 0; d < Sim.Players.Players.AnzEntries(); d++) {
                    if ((Sim.Players.Players[d].IsOut == 0) && d != Sim.OvertakenAirline &&
                        Sim.Players.Players[d].RentCities.RentCities[c].Rang > Overtaken.RentCities.RentCities[c].Rang) {
                        Sim.Players.Players[d].RentCities.RentCities[c].Rang--;
                    }
                }

                Overtaken.RentCities.RentCities[c].Rang = 0;
            }
        }

        // Aktien verkaufen:
        for (c = 0; c < 4; c++) {
            if (Overtaken.OwnsAktien[c] != 0) {
                Overtaken.Money += SLONG(Overtaken.OwnsAktien[c] * Sim.Players.Players[c].Kurse[0]);
                Overtaken.OwnsAktien[c] = 0;
            }
        }

        // Geld verteilen
        for (c = d = 0; c < 4; c++) {
            if ((Sim.Players.Players[c].IsOut == 0) && (Sim.Players.Players[c].OwnsAktien[Sim.OvertakenAirline] != 0)) {
                d += Sim.Players.Players[c].OwnsAktien[Sim.OvertakenAirline];
            }
        }

        for (c = 0; c < 4; c++) {
            if ((Sim.Players.Players[c].IsOut == 0) && (Sim.Players.Players[c].OwnsAktien[Sim.OvertakenAirline] != 0)) {
                Sim.Players.Players[c].ChangeMoney(
                    __int64((Overtaken.Money - Overtaken.Credit) * static_cast<__int64>(Sim.Players.Players[c].OwnsAktien[Sim.OvertakenAirline]) / d), 3181,
                    (LPCTSTR)Overtaken.AirlineX);
            }
        }
        //                            Changed: ^ war SLONG und damit vermutlich für einen Bug verantwortlich

        // Von dem Übernommenen hat keiner mehr Aktien:
        for (c = 0; c < Sim.Players.Players.AnzEntries(); c++) {
            Sim.Players.Players[c].OwnsAktien[Sim.OvertakenAirline] = 0;
        }
    }

    Airport.CreateGateMapper();

    Overtaker.UpdateStatistics();
    Overtaken.UpdateStatistics();

    Sim.Overtake = 0;
}

void GameMechanic::executeSabotageMode1() {
    PLAYER &qLocalPlayer = Sim.Players.Players[Sim.localPlayer];

    for (SLONG c = 0; c < Sim.Players.Players.AnzEntries(); c++) {
        auto &qPlayer = Sim.Players.Players[c];
        if (qPlayer.ArabMode == 0) {
            continue;
        }

        PLAYER &qOpfer = Sim.Players.Players[qPlayer.ArabOpfer];

        if (qOpfer.Planes.IsInAlbum(qPlayer.ArabPlane) == 0) {
            qPlayer.ArabMode = 0; // Flugzeug gibt es nicht mehr
            continue;
        }

        if (qPlayer.ArabActive == FALSE && qOpfer.Planes[qPlayer.ArabPlane].Ort >= 0) {
            qPlayer.ArabActive = TRUE;
        }

        if (qPlayer.ArabActive != TRUE) {
            continue;
        }

        BOOL ActNow = FALSE;

        if (qPlayer.ArabMode != 3 && qOpfer.Planes[qPlayer.ArabPlane].Ort < 0) {
            ActNow = TRUE;
        }
        if (qPlayer.ArabMode == 3 && qOpfer.Planes[qPlayer.ArabPlane].Ort >= 0) {
            CPlane &qPlane = qOpfer.Planes[qPlayer.ArabPlane];
            SLONG e = qPlane.Flugplan.NextStart;

            if (e != -1 && qPlane.Flugplan.Flug[e].Startdate * 24 + qPlane.Flugplan.Flug[e].Startzeit == Sim.Date * 24 + Sim.GetHour()) {
                ActNow = TRUE;
            }
        }

        if (ActNow == FALSE) {
            continue;
        }

        // Die Anschläge ausführen:
        __int64 PictureId = 0;
        CPlane &qPlane = qOpfer.Planes[qPlayer.ArabPlane];

        qOpfer.Statistiken[STAT_UNFAELLE].AddAtPastDay(1);
        qPlayer.Statistiken[STAT_SABOTIERT].AddAtPastDay(1);

        if (qPlane.Flugplan.NextFlight == -1) {
            continue;
        }

        CFlugplanEintrag &qFPE = qPlane.Flugplan.Flug[qPlane.Flugplan.NextFlight];

        bool bFremdsabotage = false;
        if (qPlayer.ArabMode < 0) {
            qPlayer.ArabMode = -qPlayer.ArabMode;
            bFremdsabotage = true;
        }

        switch (qPlayer.ArabMode) {
        case 1:
            if (!bFremdsabotage) {
                qPlayer.ArabHints += 2;
            }
            qOpfer.Kurse[0] *= 0.95;
            qOpfer.TrustedDividende -= 1;
            qOpfer.Sympathie[c] -= 5;
            if (qFPE.ObjectType == 1) {
                qOpfer.RentRouten.RentRouten[Routen(qFPE.ObjectId)].Image = qOpfer.RentRouten.RentRouten[Routen(qFPE.ObjectId)].Image * 99 / 100;
            }
            if (qOpfer.TrustedDividende < 0) {
                qOpfer.TrustedDividende = 0;
            }
            PictureId = GetIdFromString("SALZ");
            break;

        case 2:
            if (!bFremdsabotage) {
                qPlayer.ArabHints += 4;
            }
            qOpfer.ChangeMoney(-2000, 3500, "");
            qOpfer.Kurse[0] *= 0.9;
            qOpfer.TrustedDividende -= 2;
            qOpfer.Sympathie[c] -= 15;
            if (qOpfer.TrustedDividende < 0) {
                qOpfer.TrustedDividende = 0;
            }
            qOpfer.Image -= 2;
            if (qFPE.ObjectType == 1) {
                qOpfer.RentRouten.RentRouten[Routen(qFPE.ObjectId)].Image = qOpfer.RentRouten.RentRouten[Routen(qFPE.ObjectId)].Image * 95 / 100;
            }
            PictureId = GetIdFromString("SKELETT");
            break;

        case 3: // Platter Reifen:
        {
            if (!bFremdsabotage) {
                qPlayer.ArabHints += 10;
            }
            qOpfer.ChangeMoney(-8000, 3500, "");
            qOpfer.Kurse[0] *= 0.85;
            qOpfer.TrustedDividende -= 3;
            qOpfer.Sympathie[c] -= 35;
            if (qOpfer.TrustedDividende < 0) {
                qOpfer.TrustedDividende = 0;
            }
            qOpfer.Image -= 3;
            if (qFPE.ObjectType == 1) {
                qOpfer.RentRouten.RentRouten[Routen(qFPE.ObjectId)].Image = qOpfer.RentRouten.RentRouten[Routen(qFPE.ObjectId)].Image * 90 / 100;
            }

            SLONG e = qPlane.Flugplan.NextStart;

            qPlane.Flugplan.Flug[e].Startzeit++;
            qPlane.Flugplan.UpdateNextFlight();
            qPlane.Flugplan.UpdateNextStart();
            if (!bFremdsabotage) {
                qPlayer.Statistiken[STAT_VERSPAETUNG].AddAtPastDay(1);
            }

            if (qPlane.Flugplan.Flug[e].Startzeit == 24) {
                qPlane.Flugplan.Flug[e].Startzeit = 0;
                qPlane.Flugplan.Flug[e].Startdate++;
            }
            qPlane.CheckFlugplaene(qPlayer.ArabOpfer);
            qOpfer.UpdateAuftragsUsage();
            qOpfer.Messages.AddMessage(BERATERTYP_GIRL, bprintf(StandardTexte.GetS(TOKEN_ADVICE, 2304), (LPCTSTR)qPlane.Name));
            PictureId = GetIdFromString("REIFEN");
        } break;

        case 4:
            if (!bFremdsabotage) {
                qPlayer.ArabHints += 20;
            }
            qOpfer.ChangeMoney(-40000, 3500, "");
            qOpfer.Kurse[0] *= 0.75;
            qOpfer.TrustedDividende -= 4;
            qOpfer.Sympathie[c] -= 80;
            if (qOpfer.TrustedDividende < 0) {
                qOpfer.TrustedDividende = 0;
            }
            qOpfer.Image -= 8;
            if (qFPE.ObjectType == 1) {
                qOpfer.RentRouten.RentRouten[Routen(qFPE.ObjectId)].Image = qOpfer.RentRouten.RentRouten[Routen(qFPE.ObjectId)].Image * 50 / 100;
            }
            PictureId = GetIdFromString("FEUER");
            break;

        case 5:
            if (!bFremdsabotage) {
                qPlayer.ArabHints += 100;
            }
            qOpfer.ChangeMoney(-70000, 3500, "");
            qOpfer.Kurse[0] *= 0.70;
            qOpfer.TrustedDividende -= 5;
            qOpfer.Sympathie[c] -= 200;
            if (qOpfer.TrustedDividende < 0) {
                qOpfer.TrustedDividende = 0;
            }
            PictureId = GetIdFromString("SUPERMAN");
            break;
        default:
            hprintf("GameMechanic.cpp: Default case should not be reached.");
            DebugBreak();
        }
        if (qOpfer.Kurse[0] < 0) {
            qOpfer.Kurse[0] = 0;
        }
        // log: hprintf ("Player[%li].Image now (sabo) = %li", (LPCTSTR)qPlayer.ArabOpfer, (LPCTSTR)qOpfer.Image);

        // Für's Briefing vermerken:
        Sim.SabotageActs.ReSize(Sim.SabotageActs.AnzEntries() + 1);
        Sim.SabotageActs[Sim.SabotageActs.AnzEntries() - 1].Player = bFremdsabotage ? -2 : c;
        Sim.SabotageActs[Sim.SabotageActs.AnzEntries() - 1].ArabMode = qPlayer.ArabMode;
        Sim.SabotageActs[Sim.SabotageActs.AnzEntries() - 1].Opfer = qPlayer.ArabOpfer;

        Sim.Headlines.AddOverride(0, bprintf(StandardTexte.GetS(TOKEN_MISC, 2000 + qPlayer.ArabMode), (LPCTSTR)qOpfer.AirlineX), PictureId,
                                  static_cast<SLONG>(qPlayer.ArabOpfer == Sim.localPlayer) * 50 + qPlayer.ArabMode);
        Limit(SLONG(-1000), qOpfer.Image, SLONG(1000));

        // Araber meldet sich, oder Fax oder Brief sind da.
        if (c == Sim.localPlayer && (qLocalPlayer.IsOkayToCallThisPlayer() != 0)) {
            if (qLocalPlayer.GetRoom() == ROOM_SABOTAGE) {
                (qLocalPlayer.LocationWin)->StartDialog(TALKER_SABOTAGE, MEDIUM_AIR, 2000);
                bgWarp = FALSE;
                if (CheatTestGame == 0) {
                    qLocalPlayer.GameSpeed = 0;
                }
            } else {
                gUniversalFx.Stop();
                gUniversalFx.ReInit("phone.raw");
                gUniversalFx.Play(DSBPLAY_NOSTOP, Sim.Options.OptionEffekte * 100 / 7);

                bgWarp = FALSE;
                if (CheatTestGame == 0) {
                    qLocalPlayer.GameSpeed = 0;
                }

                delete qLocalPlayer.DialogWin;
                qLocalPlayer.DialogWin = new CSabotage(TRUE, Sim.localPlayer);
                (qLocalPlayer.LocationWin)->StartDialog(TALKER_SABOTAGE, MEDIUM_HANDY, 2000);
                (qLocalPlayer.LocationWin)->PayingForCall = FALSE;
            }
        } else if (qPlayer.ArabOpfer == Sim.localPlayer) {
            if (qOpfer.Owner == 0 && (qOpfer.IsOut == 0)) {
                qOpfer.Letters.AddLetter(FALSE,
                                         bprintf(StandardTexte.GetS(TOKEN_LETTER, 500 + qPlayer.ArabMode), (LPCTSTR)qOpfer.Planes[qPlayer.ArabPlane].Name), "",
                                         "", qPlayer.ArabMode);
            }

            if (qLocalPlayer.LocationWin != nullptr) {
                if (((qLocalPlayer.LocationWin)->IsDialogOpen() == 0) && ((qLocalPlayer.LocationWin)->MenuIsOpen() == 0) && (Sim.Options.OptionFax != 0) &&
                    (Sim.CallItADay == 0)) {
                    (qOpfer.LocationWin)->MenuStart(MENU_SABOTAGEFAX, qPlayer.ArabMode, qPlayer.ArabOpfer, qPlayer.ArabPlane);
                    (qOpfer.LocationWin)->MenuSetZoomStuff(XY(320, 220), 0.17, FALSE);

                    qOpfer.Messages.AddMessage(BERATERTYP_GIRL, StandardTexte.GetS(TOKEN_ADVICE, 2308));

                    bgWarp = FALSE;
                    if (CheatTestGame == 0) {
                        qLocalPlayer.GameSpeed = 0;
                    }
                } else if (Sim.CallItADay == 0) {
                    qOpfer.Messages.AddMessage(
                        BERATERTYP_GIRL, bprintf(StandardTexte.GetS(TOKEN_ADVICE, 2301 + qPlayer.ArabMode), (LPCTSTR)qOpfer.Planes[qPlayer.ArabPlane].Name));
                }
            }
        }

        qPlayer.ArabMode = 0;
    }
}

void GameMechanic::executeSabotageMode2(bool &outBAnyBombs) {
    outBAnyBombs = false;

    for (SLONG c = 0; c < Sim.Players.Players.AnzEntries(); c++) {
        PLAYER &qPlayer = Sim.Players.Players[c];
        if (qPlayer.IsOut != 0) {
            continue;
        }

        bool bFremdsabotage = false;
        if (qPlayer.ArabMode2 < 0) {
            qPlayer.ArabMode2 = -qPlayer.ArabMode2;
            bFremdsabotage = true;
        }

        if (qPlayer.ArabMode2 == 0) {
            continue;
        }

        PLAYER &qOpfer = Sim.Players.Players[qPlayer.ArabOpfer2];

        if (!bFremdsabotage) {
            qPlayer.Statistiken[STAT_SABOTIERT].AddAtPastDay(1);
        }

        switch (qPlayer.ArabMode2) {
        case 1: // Bakterien im Kaffee
            if (!bFremdsabotage) {
                qPlayer.ArabHints += 8;
            }
            qOpfer.Sympathie[c] -= 10;
            qOpfer.SickTokay = TRUE;
            PLAYER::NetSynchronizeFlags();
            break;

        case 2: // Virus im Notepad
            if ((qOpfer.HasItem(ITEM_LAPTOP) != 0) && qOpfer.LaptopVirus == 0) {
                qOpfer.LaptopVirus = 1;
            }
            break;

        case 3: // Bombe im Büro
            outBAnyBombs = true;
            if (!bFremdsabotage) {
                qPlayer.ArabHints += 25;
            }
            qOpfer.Sympathie[c] -= 50;
            qOpfer.OfficeState = 1;
            qOpfer.WalkToRoom(UBYTE(ROOM_BURO_A + qOpfer.PlayerNum * 10));
            break;

        case 4: // Streik provozieren
            if (!bFremdsabotage) {
                qPlayer.ArabHints += 40;
            }
            qOpfer.StrikePlanned = TRUE;
            break;
        default:
            hprintf("GameMechanic.cpp: Default case should not be reached.");
            DebugBreak();
        }

        // Für's nächste Briefing vermerken:
        Sim.SabotageActs.ReSize(Sim.SabotageActs.AnzEntries() + 1);
        Sim.SabotageActs[Sim.SabotageActs.AnzEntries() - 1].Player = bFremdsabotage ? -2 : c;
        Sim.SabotageActs[Sim.SabotageActs.AnzEntries() - 1].ArabMode = 2075 + qPlayer.ArabMode2 - 1;
        Sim.SabotageActs[Sim.SabotageActs.AnzEntries() - 1].Opfer = qPlayer.ArabOpfer2;

        qPlayer.ArabMode2 = 0;
    }
}

void GameMechanic::executeSabotageMode3() {
    for (SLONG c = 0; c < Sim.Players.Players.AnzEntries(); c++) {
        PLAYER &qPlayer = Sim.Players.Players[c];

        bool bFremdsabotage = false;
        if (qPlayer.ArabMode3 < 0) {
            qPlayer.ArabMode3 = -qPlayer.ArabMode3;
            bFremdsabotage = true;
        }

        if (qPlayer.ArabMode3 == 0) {
            continue;
        }

        PLAYER &qOpfer = Sim.Players.Players[qPlayer.ArabOpfer3];

        qPlayer.Statistiken[STAT_SABOTIERT].AddAtPastDay(1);

        switch (qPlayer.ArabMode3) {
        case 1: // Fremde Broschüren
            if (!bFremdsabotage) {
                qPlayer.ArabHints += 8;
            }
            qOpfer.WerbeBroschuere = qPlayer.PlayerNum;
            PLAYER::NetSynchronizeFlags();
            break;

        case 2: // Telefone sperren
            if (!bFremdsabotage) {
                qPlayer.ArabHints += 15;
            }
            qOpfer.TelephoneDown = 1;
            PLAYER::NetSynchronizeFlags();
            break;

        case 3: // Presseerklärung
            if (!bFremdsabotage) {
                qPlayer.ArabHints += 25;
            }
            qOpfer.Presseerklaerung = 1;
            PLAYER::NetSynchronizeFlags();
            Sim.Players.Players[Sim.localPlayer].Letters.AddLetter(
                FALSE, bprintf(StandardTexte.GetS(TOKEN_LETTER, 509), (LPCTSTR)qOpfer.AirlineX, (LPCTSTR)qOpfer.NameX, (LPCTSTR)qOpfer.AirlineX), "", "", 0);
            if (qOpfer.PlayerNum == Sim.localPlayer) {
                qOpfer.Messages.AddMessage(BERATERTYP_GIRL, StandardTexte.GetS(TOKEN_ADVICE, 2020));
            }

            {
                // Für alle Flugzeuge die er besitzt, die Passagierzahl aktualisieren:
                for (SLONG d = 0; d < qOpfer.Planes.AnzEntries(); d++) {
                    if (qOpfer.Planes.IsInAlbum(d) != 0) {
                        CPlane &qPlane = qOpfer.Planes[d];

                        for (SLONG e = 0; e < qPlane.Flugplan.Flug.AnzEntries(); e++) {
                            if (qPlane.Flugplan.Flug[e].ObjectType == 1) {
                                qPlane.Flugplan.Flug[e].CalcPassengers(qOpfer.PlayerNum, qPlane);
                            }
                        }
                        // qPlane.Flugplan.Flug[e].CalcPassengers (qPlane.TypeId, (LPCTSTR)qOpfer.PlayerNum, (LPCTSTR)qPlane);
                    }
                }
            }
            break;

        case 4: // Bankkonto hacken
            qOpfer.ChangeMoney(-1000000, 3502, "");
            if (!bFremdsabotage) {
                qPlayer.ChangeMoney(1000000, 3502, "");
            }
            if (!bFremdsabotage) {
                qPlayer.ArabHints += 30;
            }
            break;

        case 5: // Flugzeug festsetzen
            if (!bFremdsabotage) {
                qPlayer.ArabHints += 50;
            }
            if (qOpfer.Planes.IsInAlbum(qPlayer.ArabPlane) != 0) {
                qOpfer.Planes[qPlayer.ArabPlane].PseudoProblem = 15;
            }
            break;

        case 6: // Route klauen
            if (!bFremdsabotage) {
                qPlayer.ArabHints += 70;
            }
            qOpfer.RouteWegnehmen(Routen(qPlayer.ArabPlane), qPlayer.PlayerNum);
            {
                for (SLONG d = 0; d < Routen.AnzEntries(); d++) {
                    if ((Routen.IsInAlbum(d) != 0) && Routen[d].VonCity == Routen[qPlayer.ArabPlane].NachCity &&
                        Routen[d].NachCity == Routen[qPlayer.ArabPlane].VonCity) {
                        qOpfer.RouteWegnehmen(d, qPlayer.PlayerNum);
                        break;
                    }
                }
            }
            if (qOpfer.PlayerNum == Sim.localPlayer) {
                qOpfer.Messages.AddMessage(BERATERTYP_GIRL, StandardTexte.GetS(TOKEN_ADVICE, 2021));
            }
            break;
        default:
            hprintf("GameMechanic.cpp: Default case should not be reached.");
            DebugBreak();
        }

        // Für's nächste Briefing vermerken:
        Sim.SabotageActs.ReSize(Sim.SabotageActs.AnzEntries() + 1);
        Sim.SabotageActs[Sim.SabotageActs.AnzEntries() - 1].Player = bFremdsabotage ? -2 : c;
        Sim.SabotageActs[Sim.SabotageActs.AnzEntries() - 1].ArabMode = 2090 + qPlayer.ArabMode3;
        Sim.SabotageActs[Sim.SabotageActs.AnzEntries() - 1].Opfer = qPlayer.ArabOpfer3;

        qPlayer.ArabMode3 = 0;
    }
}