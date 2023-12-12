#include "StdAfx.h"
#include "AtNet.h"

SLONG TankSize[4] = {100000, 1000000, 10000000, 100000000};
SLONG TankPrice[4] = {100000, 800000, 7000000, 60000000};

SLONG SabotagePrice[5] = {1000, 5000, 10000, 50000, 100000};
SLONG SabotagePrice2[4] = {10000, 25000, 50000, 250000};
SLONG SabotagePrice3[6] = {100000, 500000, 1000000, 2000000, 5000000, 8000000};

SLONG RocketPrices[10] = {200000, 400000, 600000, 5000000, 8000000, 10000000, 20000000, 25000000, 50000000, 85000000};

SLONG StationPrices[10] = {1000000, 2000000, 3000000, 2000000, 10000000, 20000000, 35000000, 50000000, 35000000, 80000000};

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

void GameMechanic::setSaboteurTarget(PLAYER &qPlayer, SLONG target) {
    if (target < 0 || target >= 4) {
        hprintf("GameMechanic::setSaboteurTarget: Invalid target.");
        return;
    }

    qPlayer.ArabOpfer = target;
    qPlayer.ArabOpfer2 = target;
    qPlayer.ArabOpfer3 = target;
}

GameMechanic::CheckSabotage GameMechanic::checkPrerequisitesForSaboteurJob(PLAYER &qPlayer, SLONG type, SLONG number) {
    if (checkSaboteurBusy(qPlayer)) {
        return {false, 0};
    }

    if (type == 0) {
        if (number < 1 || number >= 6) {
            hprintf("GameMechanic::checkPrerequisitesForSaboteurJob: Invalid job number.");
            return {false, 0};
        }
        if (number == 1 && ((Sim.Players.Players[qPlayer.ArabOpfer].SecurityFlags & (1 << 6)) != 0U)) {
            return {false, 2096, Sim.Players.Players[qPlayer.ArabOpfer].AirlineX};
        } else if (number == 2 && ((Sim.Players.Players[qPlayer.ArabOpfer].SecurityFlags & (1 << 6)) != 0U)) {
            return {false, 2096, Sim.Players.Players[qPlayer.ArabOpfer].AirlineX};
        } else if (number == 3 && ((Sim.Players.Players[qPlayer.ArabOpfer].SecurityFlags & (1 << 7)) != 0U)) {
            return {false, 2097, Sim.Players.Players[qPlayer.ArabOpfer].AirlineX};
        } else if (number == 4 && ((Sim.Players.Players[qPlayer.ArabOpfer].SecurityFlags & (1 << 7)) != 0U)) {
            return {false, 2097, Sim.Players.Players[qPlayer.ArabOpfer].AirlineX};
        } else if (number == 5 && ((Sim.Players.Players[qPlayer.ArabOpfer].SecurityFlags & (1 << 6)) != 0U)) {
            return {false, 2096, Sim.Players.Players[qPlayer.ArabOpfer].AirlineX};
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
        if (number == 1 && ((Sim.Players.Players[qPlayer.ArabOpfer2].SecurityFlags & (1 << 0)) != 0U)) {
            return {false, 2090, Sim.Players.Players[qPlayer.ArabOpfer].AirlineX};
        } else if (number == 2 && ((Sim.Players.Players[qPlayer.ArabOpfer2].SecurityFlags & (1 << 1)) != 0U)) {
            return {false, 2091, Sim.Players.Players[qPlayer.ArabOpfer].AirlineX};
        } else if (number == 3 && ((Sim.Players.Players[qPlayer.ArabOpfer2].SecurityFlags & (1 << 0)) != 0U)) {
            return {false, 2090, Sim.Players.Players[qPlayer.ArabOpfer].AirlineX};
        } else if (number == 4 && ((Sim.Players.Players[qPlayer.ArabOpfer2].SecurityFlags & (1 << 2)) != 0U)) {
            return {false, 2092, Sim.Players.Players[qPlayer.ArabOpfer].AirlineX};
        } else if (qPlayer.Money - SabotagePrice2[number - 1] < DEBT_LIMIT) {
            return {false, 6000};
        } else if (number == 2 && (Sim.Players.Players[qPlayer.ArabOpfer2].HasItem(ITEM_LAPTOP) == 0)) {
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
        if (number == 1 && ((Sim.Players.Players[qPlayer.ArabOpfer3].SecurityFlags & (1 << 8)) != 0U)) {
            return {false, 2098, Sim.Players.Players[qPlayer.ArabOpfer].AirlineX};
        } else if (number == 2 && ((Sim.Players.Players[qPlayer.ArabOpfer3].SecurityFlags & (1 << 5)) != 0U)) {
            return {false, 2095, Sim.Players.Players[qPlayer.ArabOpfer].AirlineX};
        } else if (number == 3 && ((Sim.Players.Players[qPlayer.ArabOpfer3].SecurityFlags & (1 << 5)) != 0U)) {
            return {false, 2095, Sim.Players.Players[qPlayer.ArabOpfer].AirlineX};
        } else if (number == 4 && ((Sim.Players.Players[qPlayer.ArabOpfer3].SecurityFlags & (1 << 3)) != 0U)) {
            return {false, 2093, Sim.Players.Players[qPlayer.ArabOpfer].AirlineX};
        } else if (number == 5 && ((Sim.Players.Players[qPlayer.ArabOpfer3].SecurityFlags & (1 << 8)) != 0U)) {
            return {false, 2098, Sim.Players.Players[qPlayer.ArabOpfer].AirlineX};
        } else if (number == 6 && ((Sim.Players.Players[qPlayer.ArabOpfer3].SecurityFlags & (1 << 4)) != 0U)) {
            return {false, 2094, Sim.Players.Players[qPlayer.ArabOpfer].AirlineX};
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

    if (type == 0) {
        if (number < 1 || number >= 6) {
            hprintf("GameMechanic::activateSaboteurJob: Invalid job number.");
            return false;
        }

        if (number == 1) {
            Sim.Players.Players[qPlayer.ArabOpfer].SecurityNeeded |= (1 << 6);
        } else if (number == 2) {
            Sim.Players.Players[qPlayer.ArabOpfer].SecurityNeeded |= (1 << 6);
        } else if (number == 3) {
            Sim.Players.Players[qPlayer.ArabOpfer].SecurityNeeded |= (1 << 7);
        } else if (number == 4) {
            Sim.Players.Players[qPlayer.ArabOpfer].SecurityNeeded |= (1 << 7);
        } else if (number == 5) {
            Sim.Players.Players[qPlayer.ArabOpfer].SecurityNeeded |= (1 << 6);
        }

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
            Sim.Players.Players[qPlayer.ArabOpfer2].SecurityNeeded |= (1 << 0);
        } else if (number == 2) {
            Sim.Players.Players[qPlayer.ArabOpfer2].SecurityNeeded |= (1 << 1);
        } else if (number == 3) {
            Sim.Players.Players[qPlayer.ArabOpfer2].SecurityNeeded |= (1 << 0);
        } else if (number == 4) {
            Sim.Players.Players[qPlayer.ArabOpfer2].SecurityNeeded |= (1 << 2);
        }

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
            Sim.Players.Players[qPlayer.ArabOpfer3].SecurityNeeded |= (1 << 8);
        } else if (number == 2) {
            Sim.Players.Players[qPlayer.ArabOpfer3].SecurityNeeded |= (1 << 5);
        } else if (number == 3) {
            Sim.Players.Players[qPlayer.ArabOpfer3].SecurityNeeded |= (1 << 5);
        } else if (number == 4) {
            Sim.Players.Players[qPlayer.ArabOpfer3].SecurityNeeded |= (1 << 3);
        }

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