#include "StdAfx.h"
#include "AtNet.h"

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

bool GameMechanic::buyKerosinTank(PLAYER &qPlayer, SLONG typ, SLONG anzahl) {
    if (typ < 0 || typ >= sizeof(TankSize) || typ >= sizeof(TankPrice)) {
        hprintf("GameMechanic::buyKerosinTank: Invalid tank type.");
        return false;
    }
    if (anzahl < 0) {
        hprintf("GameMechanic::calcKerosinPrice: Negative amount.");
        return false;
    }

    SLONG Preis = TankPrice[typ];
    SLONG Size = TankSize[typ];

    if (qPlayer.Money - Preis * anzahl < DEBT_LIMIT) {
        return false;
    }

    qPlayer.Tank += Size / 1000 * anzahl;
    qPlayer.NetUpdateKerosin();

    qPlayer.ChangeMoney(-Preis * anzahl, 2091, CString(bitoa(Size)), const_cast<char *>((LPCTSTR)CString(bitoa(anzahl))));
    SIM::SendSimpleMessage(ATNET_CHANGEMONEY, 0, Sim.localPlayer, -Preis * anzahl, -1);

    qPlayer.DoBodyguardRabatt(Preis * anzahl);
    return true;
}

bool GameMechanic::toggleKerosinTank(PLAYER &qPlayer) {
    qPlayer.TankOpen ^= 1;
    qPlayer.NetUpdateKerosin();
    return true;
}

bool GameMechanic::buyKerosin(PLAYER &qPlayer, SLONG typ, SLONG menge) {
    if (typ < 0 || typ >= 3) {
        hprintf("GameMechanic::calcKerosinPrice: Invalid kerosine type.");
        return false;
    }
    if (menge < 0) {
        hprintf("GameMechanic::buyKerosin: Negative amount.");
        return false;
    }

    __int64 preis = Sim.HoleKerosinPreis(typ);
    if ((preis != 0) && qPlayer.Money - preis < DEBT_LIMIT) {
        return false;
    }
    if (preis != 0) {
        SLONG OldInhalt = qPlayer.TankInhalt;
        qPlayer.TankInhalt += menge;

        qPlayer.TankPreis = (OldInhalt * qPlayer.TankPreis + preis) / qPlayer.TankInhalt;
        qPlayer.KerosinQuali = (OldInhalt * qPlayer.KerosinQuali + menge * qPlayer.KerosinKind) / qPlayer.TankInhalt;
        qPlayer.NetUpdateKerosin();

        qPlayer.ChangeMoney(-preis, 2020, "");
        SIM::SendSimpleMessage(ATNET_CHANGEMONEY, 0, Sim.localPlayer, -preis, -1);

        qPlayer.DoBodyguardRabatt(preis);
    }
    return true;
}

std::pair<__int64, __int64> GameMechanic::calcKerosinPrice(PLAYER &qPlayer, __int64 typ, __int64 amount) {
    if (typ < 0 || typ >= 3) {
        hprintf("GameMechanic::calcKerosinPrice: Invalid kerosine type.");
        return {};
    }
    if (amount < 0) {
        hprintf("GameMechanic::calcKerosinPrice: Negative amount.");
        return {};
    }

    __int64 Kerosinpreis = Sim.HoleKerosinPreis(typ);
    __int64 Kosten = Kerosinpreis * typ;
    __int64 Rabatt = 0;

    if (amount >= 50000) {
        Rabatt = Kosten / 10;
    } else if (amount >= 10000) {
        Rabatt = Kosten / 20;
    }

    // Geldmenge begrenzen, damit man sich nichts in den Bankrott schießt
    if (Kosten - Rabatt > qPlayer.Money - (DEBT_LIMIT)) {
        amount = (qPlayer.Money - (DEBT_LIMIT)) / Kerosinpreis;
        if (amount < 0) {
            amount = 0;
        }
        Kosten = Kerosinpreis * typ;

        if (amount >= 50000) {
            Rabatt = Kosten / 10;
        } else if (amount >= 10000) {
            Rabatt = Kosten / 20;
        } else {
            Rabatt = 0;
        }
    }
    return {Kosten, Rabatt};
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
        return {false, 0};
    }

    auto victimID = qPlayer.ArabOpferSelection;
    if (type == 0) {
        if (number < 1 || number >= 6) {
            hprintf("GameMechanic::checkPrerequisitesForSaboteurJob: Invalid job number.");
            return {false, 0};
        }
        if (number == 1 && ((Sim.Players.Players[victimID].SecurityFlags & (1 << 6)) != 0U)) {
            return {false, 2096, Sim.Players.Players[victimID].AirlineX};
        } else if (number == 2 && ((Sim.Players.Players[victimID].SecurityFlags & (1 << 6)) != 0U)) {
            return {false, 2096, Sim.Players.Players[victimID].AirlineX};
        } else if (number == 3 && ((Sim.Players.Players[victimID].SecurityFlags & (1 << 7)) != 0U)) {
            return {false, 2097, Sim.Players.Players[victimID].AirlineX};
        } else if (number == 4 && ((Sim.Players.Players[victimID].SecurityFlags & (1 << 7)) != 0U)) {
            return {false, 2097, Sim.Players.Players[victimID].AirlineX};
        } else if (number == 5 && ((Sim.Players.Players[victimID].SecurityFlags & (1 << 6)) != 0U)) {
            return {false, 2096, Sim.Players.Players[victimID].AirlineX};
        } else if (qPlayer.Money - SabotagePrice[number - 1] < DEBT_LIMIT) {
            return {false, 6000};
        } else {
            qPlayer.ArabModeSelection = {type, number};
            return {true, 1060};
        }
    } else if (type == 1) {
        if (number < 1 || number >= 5) {
            hprintf("GameMechanic::checkPrerequisitesForSaboteurJob: Invalid job number.");
            return {false, 0};
        }
        if (number == 1 && ((Sim.Players.Players[victimID].SecurityFlags & (1 << 0)) != 0U)) {
            return {false, 2090, Sim.Players.Players[victimID].AirlineX};
        } else if (number == 2 && ((Sim.Players.Players[victimID].SecurityFlags & (1 << 1)) != 0U)) {
            return {false, 2091, Sim.Players.Players[victimID].AirlineX};
        } else if (number == 3 && ((Sim.Players.Players[victimID].SecurityFlags & (1 << 0)) != 0U)) {
            return {false, 2090, Sim.Players.Players[victimID].AirlineX};
        } else if (number == 4 && ((Sim.Players.Players[victimID].SecurityFlags & (1 << 2)) != 0U)) {
            return {false, 2092, Sim.Players.Players[victimID].AirlineX};
        } else if (qPlayer.Money - SabotagePrice2[number - 1] < DEBT_LIMIT) {
            return {false, 6000};
        } else if (number == 2 && (Sim.Players.Players[victimID].HasItem(ITEM_LAPTOP) == 0)) {
            return {false, 1300};
        } else {
            qPlayer.ArabModeSelection = {type, number};
            return {true, 1160};
        }
    } else if (type == 2) {
        if (number < 1 || number >= 7) {
            hprintf("GameMechanic::checkPrerequisitesForSaboteurJob: Invalid job number.");
            return {false, 0};
        }
        if (number == 1 && ((Sim.Players.Players[victimID].SecurityFlags & (1 << 8)) != 0U)) {
            return {false, 2098, Sim.Players.Players[victimID].AirlineX};
        } else if (number == 2 && ((Sim.Players.Players[victimID].SecurityFlags & (1 << 5)) != 0U)) {
            return {false, 2095, Sim.Players.Players[victimID].AirlineX};
        } else if (number == 3 && ((Sim.Players.Players[victimID].SecurityFlags & (1 << 5)) != 0U)) {
            return {false, 2095, Sim.Players.Players[victimID].AirlineX};
        } else if (number == 4 && ((Sim.Players.Players[victimID].SecurityFlags & (1 << 3)) != 0U)) {
            return {false, 2093, Sim.Players.Players[victimID].AirlineX};
        } else if (number == 5 && ((Sim.Players.Players[victimID].SecurityFlags & (1 << 8)) != 0U)) {
            return {false, 2098, Sim.Players.Players[victimID].AirlineX};
        } else if (number == 6 && ((Sim.Players.Players[victimID].SecurityFlags & (1 << 4)) != 0U)) {
            return {false, 2094, Sim.Players.Players[victimID].AirlineX};
        } else if (qPlayer.Money - SabotagePrice3[number - 1] < DEBT_LIMIT) {
            return {false, 6000};
        } else {
            qPlayer.ArabModeSelection = {type, number};
            return {true, 1160};
        }
    }
    hprintf("GameMechanic::checkPrerequisitesForSaboteurJob: Invalid type.");
    return {false, 0};
}

bool GameMechanic::activateSaboteurJob(PLAYER &qPlayer) {
    SLONG type = qPlayer.ArabModeSelection.first;
    SLONG number = qPlayer.ArabModeSelection.second;
    auto res = checkPrerequisitesForSaboteurJob(qPlayer, type, number);
    if (!res.OK) {
        return false;
    }

    auto victimID = qPlayer.ArabOpferSelection;
    if (type == 0) {
        if (number < 1 || number >= 6) {
            hprintf("GameMechanic::activateSaboteurJob: Invalid job number.");
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

        qPlayer.ArabPlane = qPlayer.ArabPlaneSelection;
        qPlayer.ArabPlaneSelection = -1;

        qPlayer.ArabTrust = min(6, qPlayer.ArabMode + 1);

        qPlayer.ChangeMoney(-SabotagePrice[qPlayer.ArabMode - 1], 2080, "");
        qPlayer.DoBodyguardRabatt(SabotagePrice[qPlayer.ArabMode - 1]);
        SIM::SendSimpleMessage(ATNET_CHANGEMONEY, 0, Sim.localPlayer, -SabotagePrice[qPlayer.ArabMode - 1], -1);

        qPlayer.NetSynchronizeSabotage();

        return true;
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

        qPlayer.ArabPlane = qPlayer.ArabPlaneSelection;
        qPlayer.ArabPlaneSelection = -1;

        qPlayer.ArabTrust = min(6, qPlayer.ArabMode2 + 1);

        qPlayer.ChangeMoney(-SabotagePrice2[number - 1], 2080, "");
        qPlayer.DoBodyguardRabatt(SabotagePrice2[number - 1]);
        SIM::SendSimpleMessage(ATNET_CHANGEMONEY, 0, Sim.localPlayer, -SabotagePrice2[number - 1], -1);

        qPlayer.NetSynchronizeSabotage();

        return true;
    } else if (type == 2) {
        if (number < 1 || number >= 5) {
            hprintf("GameMechanic::activateSaboteurJob: Invalid job number.");
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

        qPlayer.ArabPlane = qPlayer.ArabPlaneSelection;
        qPlayer.ArabPlaneSelection = -1;

        qPlayer.ArabTrust = min(6, qPlayer.ArabMode3 + 1);

        qPlayer.ChangeMoney(-SabotagePrice3[number - 1], 2080, "");
        qPlayer.DoBodyguardRabatt(SabotagePrice3[number - 1]);
        SIM::SendSimpleMessage(ATNET_CHANGEMONEY, 0, Sim.localPlayer, -SabotagePrice3[number - 1], -1);

        qPlayer.NetSynchronizeSabotage();

        return true;
    }
    hprintf("GameMechanic::activateSaboteurJob: Invalid type.");
    return false;
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

    SLONG cost = -Sim.UsedPlanes[planeID].CalculatePrice();
    qPlayer.ChangeMoney(cost, 2010, Sim.UsedPlanes[planeID].Name);
    SIM::SendSimpleMessage(ATNET_CHANGEMONEY, 0, Sim.localPlayer, cost, STAT_A_SONSTIGES);

    qPlayer.DoBodyguardRabatt(Sim.UsedPlanes[planeID].CalculatePrice());

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