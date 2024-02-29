#include "BotHelper.h"

#include "BotPlaner.h"
#include "compat_global.h"
#include "compat_misc.h"

#include <iostream>
#include <unordered_map>

// Öffnungszeiten:
extern SLONG timeDutyOpen;
extern SLONG timeDutyClose;
extern SLONG timeArabOpen;
extern SLONG timeLastClose;
extern SLONG timeMuseOpen;
extern SLONG timeReisClose;
extern SLONG timeMaklClose;
extern SLONG timeWerbOpen;

// #define PRINT_OVERALL 1

static void setColorForFlightJob(SLONG objectType) {
    std::cout << "\e[1;";
    if (objectType == 1) {
        std::cout << "34";
    } else if (objectType == 2) {
        std::cout << "32";
    } else if (objectType == 4) {
        std::cout << "33";
    } else {
        std::cout << "35";
    }
    std::cout << "m";
}

static void resetColor() { std::cout << "\e[m"; }

namespace Helper {

CString getWeekday(UWORD date) { return StandardTexte.GetS(TOKEN_SCHED, 3010 + (date + Sim.StartWeekday) % 7); }

void printJob(const CAuftrag &qAuftrag) {
    CString strDate = (qAuftrag.Date == qAuftrag.BisDate) ? getWeekday(qAuftrag.Date) : getWeekday(qAuftrag.BisDate);
    CString strDist(Einheiten[EINH_KM].bString(Cities.CalcDistance(qAuftrag.VonCity, qAuftrag.NachCity) / 1000));
    CString strPraemie(Insert1000erDots(qAuftrag.Strafe));
    CString strStrafe(Insert1000erDots(qAuftrag.Praemie));
    hprintf("%s -> %s (%u) (%s, %s, P: %s $, S: %s $)", (LPCTSTR)Cities[qAuftrag.VonCity].Name, (LPCTSTR)Cities[qAuftrag.NachCity].Name, qAuftrag.Personen,
            (LPCTSTR)strDist, (LPCTSTR)strDate, (LPCTSTR)strPraemie, (LPCTSTR)strStrafe);
}

void printRoute(const CRoute &qRoute) {
    CString strDist(Einheiten[EINH_KM].bString(Cities.CalcDistance(qRoute.VonCity, qRoute.NachCity) / 1000));
    hprintf("%s -> %s (Bedarf: %ld, Faktor: %f, %s)", (LPCTSTR)Cities[qRoute.VonCity].Name, (LPCTSTR)Cities[qRoute.NachCity].Name, qRoute.Bedarf, qRoute.Faktor,
            (LPCTSTR)strDist);
}

void printFreight(const CFracht &qAuftrag) {
    CString strDate = (qAuftrag.Date == qAuftrag.BisDate) ? getWeekday(qAuftrag.Date) : getWeekday(qAuftrag.BisDate);
    CString strDist(Einheiten[EINH_KM].bString(Cities.CalcDistance(qAuftrag.VonCity, qAuftrag.NachCity) / 1000));
    CString strPraemie(Insert1000erDots(qAuftrag.Strafe));
    CString strStrafe(Insert1000erDots(qAuftrag.Praemie));
    hprintf("%s -> %s (%ld tons total, %ld left, %ld open) (%s, %s, P: %s $, S: %s $)", (LPCTSTR)Cities[qAuftrag.VonCity].Name,
            (LPCTSTR)Cities[qAuftrag.NachCity].Name, qAuftrag.Tons, qAuftrag.TonsLeft, qAuftrag.TonsOpen, (LPCTSTR)strDist, (LPCTSTR)strDate,
            (LPCTSTR)strPraemie, (LPCTSTR)strStrafe);
}

std::string getRouteName(const CRoute &qRoute) { return {bprintf("%s -> %s", (LPCTSTR)Cities[qRoute.VonCity].Name, (LPCTSTR)Cities[qRoute.NachCity].Name)}; }

std::string getJobName(const CAuftrag &qAuftrag) {
    return {bprintf("%s -> %s", (LPCTSTR)Cities[qAuftrag.VonCity].Name, (LPCTSTR)Cities[qAuftrag.NachCity].Name)};
}

std::string getFreightName(const CFracht &qAuftrag) {
    return {bprintf("%s -> %s", (LPCTSTR)Cities[qAuftrag.VonCity].Name, (LPCTSTR)Cities[qAuftrag.NachCity].Name)};
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

const CFlugplanEintrag *getLastFlight(const CPlane &qPlane) {
    const auto &qFlightPlan = qPlane.Flugplan.Flug;
    for (int c = qFlightPlan.AnzEntries() - 1; c >= 0; c--) {
        auto *qFPE = &qFlightPlan[c];
        if (qFPE->ObjectType <= 0) {
            continue;
        }
        return qFPE;
    }
    return nullptr;
}

const CFlugplanEintrag *getLastFlightNotAfter(const CPlane &qPlane, PlaneTime ignoreFrom) {
    const auto &qFlightPlan = qPlane.Flugplan.Flug;
    for (int c = qFlightPlan.AnzEntries() - 1; c >= 0; c--) {
        auto *qFPE = &qFlightPlan[c];
        if (qFPE->ObjectType <= 0) {
            continue;
        }
        if (PlaneTime{qFPE->Startdate, qFPE->Startzeit} >= ignoreFrom) {
            continue;
        }
        return qFPE;
    }
    return nullptr;
}

std::pair<PlaneTime, int> getPlaneAvailableTimeLoc(const CPlane &qPlane, std::optional<PlaneTime> ignoreFrom, std::optional<PlaneTime> earliest) {
    std::pair<PlaneTime, int> res{};
    res.second = qPlane.Flugplan.StartCity;

    const CFlugplanEintrag *qFPE = ignoreFrom.has_value() ? getLastFlightNotAfter(qPlane, ignoreFrom.value()) : getLastFlight(qPlane);
    if (qFPE != nullptr) {
        res.first = {qFPE->Landedate, qFPE->Landezeit + kDurationExtra};
        res.second = qFPE->NachCity;
    }
    PlaneTime currentTime{Sim.Date, Sim.GetHour() + kAvailTimeExtra};
    if (earliest.has_value() && earliest.value() > currentTime) {
        currentTime = earliest.value();
    }
    if (currentTime > res.first) {
        res.first = currentTime;
    }
    return res;
}

SLONG checkPlaneSchedule(const PLAYER &qPlayer, SLONG planeId, bool printOnErrorOnly) {
    if (qPlayer.Planes.IsInAlbum(planeId) == 0) {
        return 0;
    }
    auto &qPlane = qPlayer.Planes[planeId];
    std::unordered_map<int, CString> assignedJobs;
    return _checkPlaneSchedule(qPlayer, qPlane, assignedJobs, printOnErrorOnly);
}

SLONG checkPlaneSchedule(const PLAYER &qPlayer, const CPlane &qPlane, bool printOnErrorOnly) {
    std::unordered_map<int, CString> assignedJobs;
    return _checkPlaneSchedule(qPlayer, qPlane, assignedJobs, printOnErrorOnly);
}

SLONG _checkPlaneSchedule(const PLAYER &qPlayer, const CPlane &qPlane, std::unordered_map<int, CString> &assignedJobs, bool printOnErrorOnly) {
    SLONG nIncorrect = 0;
    auto nIncorredOld = nIncorrect;

    auto &qFluege = qPlane.Flugplan.Flug;
    for (SLONG d = 0; d < qFluege.AnzEntries(); d++) {
        if (qFluege[d].ObjectType == 0) {
            continue;
        }

        auto id = qFluege[d].ObjectId;

        /* check duration */
        PlaneTime time{qFluege[d].Startdate, qFluege[d].Startzeit};
        time += Cities.CalcFlugdauer(qFluege[d].VonCity, qFluege[d].NachCity, qPlane.ptGeschwindigkeit);
        if ((qFluege[d].Landedate != time.getDate()) || (qFluege[d].Landezeit != time.getHour())) {
            redprintf("Helper::checkPlaneSchedule(): CFlugplanEintrag has invalid landing time: %s %ld, should be %s %ld",
                      (LPCTSTR)getWeekday(qFluege[d].Landedate), qFluege[d].Landezeit, (LPCTSTR)getWeekday(time.getDate()), time.getHour());
            printFPE(qFluege[d]);
        }

        /* check overlap */
        if (d > 0) {
            if (qFluege[d].Startdate < qFluege[d - 1].Landedate ||
                (qFluege[d].Startdate == qFluege[d - 1].Landedate && qFluege[d].Startzeit < qFluege[d - 1].Landezeit)) {
                nIncorrect++;
                redprintf("Helper::checkPlaneSchedule(): Job (%s -> %s) on plane %s overlaps with previous job (%s -> %s)",
                          (LPCTSTR)Cities[qFluege[d].VonCity].Kuerzel, (LPCTSTR)Cities[qFluege[d].NachCity].Kuerzel, (LPCTSTR)qPlane.Name,
                          (LPCTSTR)Cities[qFluege[d - 1].VonCity].Kuerzel, (LPCTSTR)Cities[qFluege[d - 1].NachCity].Kuerzel);
            }
            if (qFluege[d].VonCity != qFluege[d - 1].NachCity) {
                nIncorrect++;
                redprintf(
                    "Helper::checkPlaneSchedule(): Start location of job (%s -> %s) on plane %s does not matching landing location of previous job (%s -> %s)",
                    (LPCTSTR)Cities[qFluege[d].VonCity].Kuerzel, (LPCTSTR)Cities[qFluege[d].NachCity].Kuerzel, (LPCTSTR)qPlane.Name,
                    (LPCTSTR)Cities[qFluege[d - 1].VonCity].Kuerzel, (LPCTSTR)Cities[qFluege[d - 1].NachCity].Kuerzel);
            }
        }

        /* check route jobs */
        if (qFluege[d].ObjectType == 1) {
            auto &qAuftrag = Routen[id];

            if (qFluege[d].VonCity != qAuftrag.VonCity || qFluege[d].NachCity != qAuftrag.NachCity) {
                nIncorrect++;
                redprintf("Helper::checkPlaneSchedule(): CFlugplanEintrag does not match CRoute for job (%s)", getRouteName(qAuftrag).c_str());
                printRoute(qAuftrag);
                printFPE(qFluege[d]);
            }

            if (qFluege[d].Okay != 0) {
                nIncorrect++;
                redprintf("Helper::checkPlaneSchedule(): Route (%s) for plane %s is not scheduled correctly", getRouteName(qAuftrag).c_str(),
                          (LPCTSTR)qPlane.Name);
            }

            if (qPlane.ptReichweite * 1000 < Cities.CalcDistance(qAuftrag.VonCity, qAuftrag.NachCity)) {
                nIncorrect++;
                redprintf("Helper::checkPlaneSchedule(): Route (%s) exceeds range for plane %s", getRouteName(qAuftrag).c_str(), (LPCTSTR)qPlane.Name);
            }
        }

        /* check flight jobs */
        if (qFluege[d].ObjectType == 2) {
            auto &qAuftrag = qPlayer.Auftraege[id];

            if (assignedJobs.find(id) != assignedJobs.end()) {
                nIncorrect++;
                redprintf("Helper::checkPlaneSchedule(): Job (%s) for plane %s is also scheduled for plane %s", getJobName(qAuftrag).c_str(),
                          (LPCTSTR)qPlane.Name, (LPCTSTR)assignedJobs[id]);
                printJob(qAuftrag);
                printFPE(qFluege[d]);
            }
            assignedJobs[id] = qPlane.Name;

            if (qFluege[d].VonCity != qAuftrag.VonCity || qFluege[d].NachCity != qAuftrag.NachCity) {
                nIncorrect++;
                redprintf("Helper::checkPlaneSchedule(): CFlugplanEintrag start/destination does not match CAuftrag for job (%s)",
                          getJobName(qAuftrag).c_str());
                printJob(qAuftrag);
                printFPE(qFluege[d]);
            }

            if ((qFluege[d].Passagiere + qFluege[d].PassagiereFC) != qAuftrag.Personen) {
                nIncorrect++;
                redprintf("Helper::checkPlaneSchedule(): CFlugplanEintrag passenger count does not match CAuftrag for job (%s)", getJobName(qAuftrag).c_str());
                printJob(qAuftrag);
                printFPE(qFluege[d]);
            }

            if (qFluege[d].Okay != 0) {
                nIncorrect++;
                redprintf("Helper::checkPlaneSchedule(): Job (%s) for plane %s is not scheduled correctly", getJobName(qAuftrag).c_str(), (LPCTSTR)qPlane.Name);
            }

            if (qPlane.ptReichweite * 1000 < Cities.CalcDistance(qAuftrag.VonCity, qAuftrag.NachCity)) {
                nIncorrect++;
                redprintf("Helper::checkPlaneSchedule(): Job (%s) exceeds range for plane %s", getJobName(qAuftrag).c_str(), (LPCTSTR)qPlane.Name);
            }

            if (qFluege[d].Startdate < qAuftrag.Date) {
                nIncorrect++;
                redprintf("Helper::checkPlaneSchedule(): Job (%s) starts too early (%s instead of %s) for plane %s", getJobName(qAuftrag).c_str(),
                          (LPCTSTR)getWeekday(qFluege[d].Startdate), (LPCTSTR)getWeekday(qAuftrag.Date), (LPCTSTR)qPlane.Name);
            }

            if (qFluege[d].Startdate > qAuftrag.BisDate) {
                nIncorrect++;
                redprintf("Helper::checkPlaneSchedule(): Job (%s) starts too late (%s instead of %s) for plane %s", getJobName(qAuftrag).c_str(),
                          (LPCTSTR)getWeekday(qFluege[d].Startdate), (LPCTSTR)getWeekday(qAuftrag.BisDate), (LPCTSTR)qPlane.Name);
            }

            if (qPlane.ptPassagiere < SLONG(qAuftrag.Personen)) {
                nIncorrect++;
                redprintf("Helper::checkPlaneSchedule(): Job (%s) exceeds number of seats for plane %s", getJobName(qAuftrag).c_str(), (LPCTSTR)qPlane.Name);
            }
        }

        /* check freight jobs */
        if (qFluege[d].ObjectType == 4) {
            auto &qAuftrag = qPlayer.Frachten[id];

            if (assignedJobs.find(id) != assignedJobs.end()) {
                nIncorrect++;
                redprintf("Helper::checkPlaneSchedule(): Job (%s) for plane %s is also scheduled for plane %s", getFreightName(qAuftrag).c_str(),
                          (LPCTSTR)qPlane.Name, (LPCTSTR)assignedJobs[id]);
                printFreight(qAuftrag);
                printFPE(qFluege[d]);
            }
            assignedJobs[id] = qPlane.Name;

            if (qFluege[d].VonCity != qAuftrag.VonCity || qFluege[d].NachCity != qAuftrag.NachCity) {
                nIncorrect++;
                redprintf("Helper::checkPlaneSchedule(): CFlugplanEintrag does not match CFracht for job (%s)", getFreightName(qAuftrag).c_str());
                printFreight(qAuftrag);
                printFPE(qFluege[d]);
            }

            if (qFluege[d].Okay != 0) {
                nIncorrect++;
                redprintf("Helper::checkPlaneSchedule(): Freight job (%s) for plane %s is not scheduled correctly", getFreightName(qAuftrag).c_str(),
                          (LPCTSTR)qPlane.Name);
            }

            if (qPlane.ptReichweite * 1000 < Cities.CalcDistance(qAuftrag.VonCity, qAuftrag.NachCity)) {
                nIncorrect++;
                redprintf("Helper::checkPlaneSchedule(): Freight job (%s) exceeds range for plane %s", getFreightName(qAuftrag).c_str(), (LPCTSTR)qPlane.Name);
            }

            if (qFluege[d].Startdate < qAuftrag.Date) {
                nIncorrect++;
                redprintf("Helper::checkPlaneSchedule(): Freight job (%s) starts too early (%s instead of %s) for plane %s", getFreightName(qAuftrag).c_str(),
                          (LPCTSTR)getWeekday(qFluege[d].Startdate), (LPCTSTR)getWeekday(qAuftrag.Date), (LPCTSTR)qPlane.Name);
            }

            if (qFluege[d].Startdate > qAuftrag.BisDate) {
                nIncorrect++;
                redprintf("Helper::checkPlaneSchedule(): Freight job (%s) starts too late (%s instead of %s) for plane %s", getFreightName(qAuftrag).c_str(),
                          (LPCTSTR)getWeekday(qFluege[d].Startdate), (LPCTSTR)getWeekday(qAuftrag.BisDate), (LPCTSTR)qPlane.Name);
            }
        }
    }
    if ((nIncorrect > nIncorredOld) || !printOnErrorOnly) {
        printFlightJobs(qPlayer, qPlane);
    }
    assert(nIncorrect == 0);
    return nIncorrect;
}

SLONG checkFlightJobs(const PLAYER &qPlayer, bool printOnErrorOnly) {
    SLONG nIncorrect = 0;
    SLONG nPlanes = 0;
    std::unordered_map<int, CString> assignedJobs;
    for (SLONG c = 0; c < qPlayer.Planes.AnzEntries(); c++) {
        if (qPlayer.Planes.IsInAlbum(c) == 0) {
            continue;
        }
        nIncorrect += _checkPlaneSchedule(qPlayer, qPlayer.Planes[c], assignedJobs, printOnErrorOnly);
        nPlanes++;
    }
    if (nIncorrect > 0) {
        hprintf("Helper::checkFlightJobs(): Found %ld problems for %ld planes.", nIncorrect, nPlanes);
    }
    return nIncorrect;
}

void printFlightJobs(const PLAYER &qPlayer, SLONG planeId) {
    if (qPlayer.Planes.IsInAlbum(planeId) == 0) {
        return;
    }
    auto &qPlane = qPlayer.Planes[planeId];
    printFlightJobs(qPlayer, qPlane);
}

void printFlightJobs(const PLAYER &qPlayer, const CPlane &qPlane) {
    auto &qFluege = qPlane.Flugplan.Flug;

    hprintf("Helper::printFlightJobs(): Schedule for plane %s:", (LPCTSTR)qPlane.Name);

    /* print job list */
    for (SLONG d = 0; d < qFluege.AnzEntries(); d++) {
        auto objectType = qFluege[d].ObjectType;
        if (objectType == 0) {
            continue;
        }
        setColorForFlightJob(objectType);
        printf("%c> ", 'A' + (d % 26));
        printFPE(qFluege[d]);
        if (objectType == 1) {
            printf("%c>          ", 'A' + (d % 26));
            printRoute(Routen[qFluege[d].ObjectId]);
        } else if (objectType == 2) {
            printf("%c>          ", 'A' + (d % 26));
            printJob(qPlayer.Auftraege[qFluege[d].ObjectId]);
        } else if (objectType == 4) {
            printf("%c>          ", 'A' + (d % 26));
            printFreight(qPlayer.Frachten[qFluege[d].ObjectId]);
        }
        resetColor();
    }

    /* render schedule to ASCII */
    SLONG idx = 0;
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
                setColorForFlightJob(qFluege[idx].ObjectType);
                std::cout << static_cast<char>('A' + (idx % 26));
                resetColor();
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

const char *getItemName(SLONG item) {
    switch (item) {
    case ITEM_LAPTOP:
        return "Laptop";
    case ITEM_HANDY:
        return "Handy";
    case ITEM_NOTEBOOK:
        return "Filofax";
    case ITEM_MG:
        return "MG";
    case ITEM_BIER:
        return "Beer";
    case ITEM_ZIGARRE:
        return "Cigarette";
    case ITEM_OEL:
        return "Oilcan";
    case ITEM_POSTKARTE:
        return "Postcard";
    case ITEM_TABLETTEN:
        return "Pills";
    case ITEM_SPINNE:
        return "Tarantula";
    case ITEM_DART:
        return "Dart";
    case ITEM_DISKETTE:
        return "Floppy disk";
    case ITEM_BH:
        return "BH";
    case ITEM_HUFEISEN:
        return "Horseshoe";
    case ITEM_PRALINEN:
        return "Pralines";
    case ITEM_PRALINEN_A:
        return "Pralines (spiked)";
    case ITEM_PAPERCLIP:
        return "Paperclip";
    case ITEM_GLUE:
        return "Glue";
    case ITEM_GLOVE:
        return "Gloves";
    case ITEM_REDBULL:
        return "Redbull";
    case ITEM_STINKBOMBE:
        return "Stinkbomb";
    case ITEM_GLKOHLE:
        return "Ember";
    case ITEM_KOHLE:
        return "Ember (cold)";
    case ITEM_ZANGE:
        return "Pliers";
    case ITEM_PARFUEM:
        return "Perfume";
    case ITEM_XPARFUEM:
        return "Stinking perfume";
    default:
        redprintf("BotHelper.cpp: Default case should not be reached.");
        return "INVALID";
    }
    return "INVALID";
}

} // namespace Helper
