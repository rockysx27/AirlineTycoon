#include "compat.h"

#include <iostream>

namespace Helper {

CString getWeekday(UWORD date) { return StandardTexte.GetS(TOKEN_SCHED, 3010 + (date + Sim.StartWeekday) % 7); }

void printJob(const CAuftrag &qAuftrag) {
    CString strDate = (qAuftrag.Date == qAuftrag.BisDate) ? getWeekday(qAuftrag.Date) : getWeekday(qAuftrag.BisDate);
    CString strDist(Einheiten[EINH_KM].bString(Cities.CalcDistance(qAuftrag.VonCity, qAuftrag.NachCity) / 1000));
    CString strPraemie(Insert1000erDots(qAuftrag.Strafe));
    CString strStrafe(Insert1000erDots(qAuftrag.Praemie));
    hprintf("%s -> %s (%u, %s, %s, P: %s $, S: %s $)", (LPCTSTR)Cities[qAuftrag.VonCity].Kuerzel, (LPCTSTR)Cities[qAuftrag.NachCity].Kuerzel, qAuftrag.Personen,
            (LPCTSTR)strDist, (LPCTSTR)strDate, (LPCTSTR)strPraemie, (LPCTSTR)strStrafe);
}

void printFPE(const CFlugplanEintrag &qFPE) {
    CString strInfo = bprintf("%s -> %s (%s %ld -> %s %ld)", (LPCTSTR)Cities[qFPE.VonCity].Kuerzel, (LPCTSTR)Cities[qFPE.NachCity].Kuerzel,
                              (LPCTSTR)getWeekday(qFPE.Startdate), qFPE.Startzeit, (LPCTSTR)getWeekday(qFPE.Landedate), qFPE.Landezeit);

    CString strType = "Auto   ";
    if (qFPE.ObjectType == 1) {
        strType = "Route  ";
    } else if (qFPE.ObjectType == 2) {
        strType = "Auftrag";
    } else if (qFPE.ObjectType == 4) {
        strType = "Fracht ";
    }
    CString strDist(Einheiten[EINH_KM].bString(Cities.CalcDistance(qFPE.VonCity, qFPE.NachCity) / 1000));
    hprintf("%s: %s (%u, %s)", (LPCTSTR)strType, (LPCTSTR)strInfo, qFPE.Passagiere, (LPCTSTR)strDist);
}

std::string getJobName(const CAuftrag &qAuftrag) {
    return {bprintf("%s -> %s", (LPCTSTR)Cities[qAuftrag.VonCity].Kuerzel, (LPCTSTR)Cities[qAuftrag.NachCity].Kuerzel)};
}

SLONG checkFlightJobs(const PLAYER &qPlayer) {
    auto &qAuftraege = qPlayer.Auftraege;
    SLONG nIncorrect = 0;
    for (SLONG c = 0; c < qPlayer.Planes.AnzEntries(); c++) {
        if (qPlayer.Planes.IsInAlbum(c) == 0) {
            continue;
        }

        auto &qPlane = qPlayer.Planes[c];
        auto &qFluege = qPlane.Flugplan.Flug;

        hprintf("\n=== Check schedule for plane %s ===", (LPCTSTR)qPlane.Name);

        printFlightJobs(qPlayer, c);

        for (SLONG d = 0; d < qFluege.AnzEntries(); d++) {
            if (qFluege[d].ObjectType == 0) {
                continue;
            }

            printf("%c> ", 'A' + (d % 26));
            printFPE(qFluege[d]);

            if (d > 0) {
                if (qFluege[d].Startdate < qFluege[d - 1].Landedate ||
                    (qFluege[d].Startdate == qFluege[d - 1].Landedate && qFluege[d].Startzeit < qFluege[d - 1].Landezeit)) {
                    redprintf("Bot::checkFlightJobs(): Job (%s -> %s) overlaps with previous job (%s -> %s)", (LPCTSTR)Cities[qFluege[d].VonCity].Kuerzel,
                              (LPCTSTR)Cities[qFluege[d].NachCity].Kuerzel, (LPCTSTR)Cities[qFluege[d - 1].VonCity].Kuerzel,
                              (LPCTSTR)Cities[qFluege[d - 1].NachCity].Kuerzel);
                }
                if (qFluege[d].VonCity != qFluege[d - 1].NachCity) {
                    redprintf("Bot::checkFlightJobs(): Start location of job (%s -> %s) does not matching landing location of previous job (%s -> %s)",
                              (LPCTSTR)Cities[qFluege[d].VonCity].Kuerzel, (LPCTSTR)Cities[qFluege[d].NachCity].Kuerzel,
                              (LPCTSTR)Cities[qFluege[d - 1].VonCity].Kuerzel, (LPCTSTR)Cities[qFluege[d - 1].NachCity].Kuerzel);
                }
            }

            if (qFluege[d].ObjectType == 3) {
                continue; /* we are done with automatic flights */
            }

            auto &qAuftrag = qAuftraege[qFluege[d].ObjectId];
            printf("%c>          ", 'A' + (d % 26));
            printJob(qAuftrag);

            if (qFluege[d].VonCity != qAuftrag.VonCity || qFluege[d].NachCity != qAuftrag.NachCity || qFluege[d].Passagiere != qAuftrag.Personen) {
                redprintf("Bot::checkFlightJobs(): CFlugplanEintrag does not match CAuftrag for job (%s)", getJobName(qAuftrag).c_str());
                printJob(qAuftrag);
                printFPE(qFluege[d]);
            }

            if (qFluege[d].Okay != 0) {
                nIncorrect++;
                redprintf("Bot::checkFlightJobs(): Job (%s) for plane %s is not scheduled correctly", getJobName(qAuftrag).c_str(), (LPCTSTR)qPlane.Name);
            }

            if (qPlane.ptReichweite * 1000 < Cities.CalcDistance(qAuftrag.VonCity, qAuftrag.NachCity)) {
                redprintf("Bot::checkFlightJobs(): Job (%s) exceeds range for plane %s", getJobName(qAuftrag).c_str(), (LPCTSTR)qPlane.Name);
            }

            if (qFluege[d].ObjectType == 1) {
                continue; /* we are done with route flights */
            }

            if (qFluege[d].Startdate < qAuftrag.Date) {
                redprintf("Bot::checkFlightJobs(): Job (%s) starts too early (%s instead of %s) for plane %s", getJobName(qAuftrag).c_str(),
                          (LPCTSTR)getWeekday(qFluege[d].Startdate), (LPCTSTR)getWeekday(qAuftrag.Date), (LPCTSTR)qPlane.Name);
            }

            if (qFluege[d].Startdate > qAuftrag.BisDate) {
                redprintf("Bot::checkFlightJobs(): Job (%s) starts too late (%s instead of %s) for plane %s", getJobName(qAuftrag).c_str(),
                          (LPCTSTR)getWeekday(qFluege[d].Startdate), (LPCTSTR)getWeekday(qAuftrag.BisDate), (LPCTSTR)qPlane.Name);
            }

            if (qFluege[d].ObjectType == 4) {
                continue; /* we are done with freight flights */
            }

            if (qPlane.ptPassagiere < SLONG(qAuftrag.Personen)) {
                redprintf("Bot::checkFlightJobs(): Job (%s) exceeds number of seats for plane %s", getJobName(qAuftrag).c_str(), (LPCTSTR)qPlane.Name);
            }
        }
    }
    return nIncorrect;
}

void printFlightJobs(const PLAYER &qPlayer, SLONG planeId) {
    if (qPlayer.Planes.IsInAlbum(planeId) == 0) {
        return;
    }

    SLONG idx = 0;
    auto &qPlane = qPlayer.Planes[planeId];
    auto &qFluege = qPlane.Flugplan.Flug;
    for (SLONG day = Sim.Date; day < Sim.Date + 7; day++) {
        for (SLONG i = 0; i < 24; i++) {
            while ((idx < qFluege.AnzEntries()) &&
                   (qFluege[idx].ObjectType == 0 || qFluege[idx].Landedate < day || (qFluege[idx].Landedate == day && qFluege[idx].Landezeit < i))) {
                idx++;
            }

            if (idx == qFluege.AnzEntries()) {
                std::cout << ".";
                continue;
            }

            auto id = qFluege[idx].ObjectId;
            if (id < 0) {
                continue;
            }

            if (qFluege[idx].Startdate < day || (qFluege[idx].Startdate == day && qFluege[idx].Startzeit <= i)) {
                std::cout << "\e[1;";
                if (qFluege[idx].ObjectType == 1) {
                    std::cout << "34";
                } else if (qFluege[idx].ObjectType == 2) {
                    std::cout << "32";
                } else if (qFluege[idx].ObjectType == 4) {
                    std::cout << "33";
                } else {
                    std::cout << "35";
                }
                std::cout << "m";

                std::cout << static_cast<char>('A' + (idx % 26));

                std::cout << "\e[m";
            } else {
                std::cout << ".";
            }
        }
        std::cout << std::endl;
    }
}

SLONG calculateScheduleGain(const PLAYER &qPlayer, SLONG planeId) {
    if (qPlayer.Planes.IsInAlbum(planeId) == 0) {
        return 0;
    }

    SLONG gain = 0;
    auto &qPlane = qPlayer.Planes[planeId];
    auto &qFlightPlan = qPlane.Flugplan.Flug;
    for (SLONG c = qFlightPlan.AnzEntries() - 1; c >= 0; c--) {
        if (qFlightPlan[c].ObjectType <= 0) {
            continue;
        }
        gain += qFlightPlan[c].GetEinnahmen(qPlayer.PlayerNum, qPlane);
        gain -= CalculateFlightCostNoTank(qFlightPlan[c].VonCity, qFlightPlan[c].NachCity, qPlane.ptVerbrauch, qPlane.ptGeschwindigkeit);
    }
    return gain;
}

bool checkRoomOpen(SLONG roomId) {
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
} // namespace Helper
