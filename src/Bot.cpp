#include "Bot.h"

#include "BotHelper.h"
#include "BotPlaner.h"
#include "GameMechanic.h"
#include "TeakLibW.h"

#include <algorithm>
#include <iostream>

const bool kAlwaysReplan = true;
float kSchedulingMinScoreRatio = 140 * 1000.0f;
float kSchedulingMinScoreRatioLastMinute = 10 * 1000.0f;
SLONG kSwitchToRoutesNumPlanesMin = 2;
SLONG kSwitchToRoutesNumPlanesMax = 4;
const SLONG kSmallestAdCampaign = 4;
const SLONG kMinimumImage = -4;
const SLONG kRouteAvgDays = 3;
const SLONG kMinimumOwnRouteUtilization = 0.2;
SLONG kMaximumRouteUtilization = 90;
const SLONG kMaximumPlaneUtilization = 70;
DOUBLE kMaxTicketPriceFactor = 3.0;
const DOUBLE kDefaultTicketPriceFactor = 3.5;
const SLONG kTargetEmployeeHappiness = 90;
const SLONG kMinimumEmployeeSkill = 70;
SLONG kPlaneMinimumZustand = 90;
const SLONG kPlaneTargetZustand = 100;
const SLONG kUsedPlaneMinimumScore = 40;
DOUBLE kMaxKerosinQualiZiel = 1.2;
SLONG kNumRoutesStartBuyingTanks = 3;
SLONG kOwnStockPosessionRatio = 25;
const SLONG kStockEmissionMode = 2;
const bool kReduceDividend = false;
const SLONG kMaxSabotageHints = 99;

const SLONG kMoneyEmergencyFund = 100000;
const SLONG kMoneyReserveRepairs = 0;
const SLONG kMoneyReservePlaneUpgrades = 2500 * 1000;
const SLONG kMoneyReserveBuyTanks = 200 * 1000;
const SLONG kMoneyReserveIncreaseDividend = 100 * 1000;
const SLONG kMoneyReservePaybackCredit = 1500 * 1000;
const SLONG kMoneyReserveBuyOwnShares = 2000 * 1000;
const SLONG kMoneyReserveBuyNemesisShares = 80 * 1e6;
const SLONG kMoneyReserveBossOffice = 10 * 1000;
const SLONG kMoneyReserveExpandAirport = 1000 * 1000;
const SLONG kMoneyReserveSabotage = 200 * 1000;

SLONG kPlaneScoreMode = 0;
SLONG kPlaneScoreForceBest = -1;
SLONG kTestMode = 0;

inline const char *getPrioName(Bot::Prio prio) {
    switch (prio) {
    case Bot::Prio::Top:
        return "Top";
    case Bot::Prio::Higher:
        return "Higher";
    case Bot::Prio::High:
        return "High";
    case Bot::Prio::Medium:
        return "Medium";
    case Bot::Prio::Low:
        return "Low";
    case Bot::Prio::Lowest:
        return "Lowest";
    case Bot::Prio::None:
        return "None";
    default:
        redprintf("Bot.cpp: Default case should not be reached.");
        DebugBreak();
        return "INVALID";
    }
    return "INVALID";
}
inline const char *getPrioName(SLONG prio) { return getPrioName(static_cast<Bot::Prio>(prio)); }

Bot::Bot(PLAYER &player) : qPlayer(player) {}

void Bot::printStatisticsLine(CString prefix, bool printHeader) {
    if (printHeader) {
        printf("%s: Tag, Geld, Kredit, Available, ", (LPCTSTR)prefix);
        printf("SaldoGesamt, Saldo, Gewinn, Verlust, ");
        printf("Routen, Auftraege, Fracht, ");
        printf("KerosinFlug, KerosinVorrat, Essen, ");
        printf("Strafe, Wartung, Umbau, ");
        printf("Personal, Gatemiete, Citymiete, Routenmiete, ");
        printf("HabenZinsen, HabenRendite, KreditNeu, ");
        printf("SollZinsen, SollRendite, KreditTilgung, Steuer, ");
        printf("Aktienverkauf, AktienEmission, AktienEmissionKompErh, ");
        printf("AktienEmissionKompGez, Aktienkauf, AktienEmissionFee, ");
        printf("FlugzeugVerkauf, Takeovers, FlugzeugKauf, FlugzeugUpgrades, ");
        printf("ExpansionCity, ExpansionRouten, ExpansionGates, ExpansionTanks, ");
        printf("SabotageGeklaut, SabotageKomp, ");
        printf("Sabotage, SabotageStrafe, SabotageSchaden, ");
        printf("BodyguardRabatt, GeldErhalten, SonstigeEinnahmen, ");
        printf("PanneSchaden, SecurityKosten, WerbeKosten, ");
        printf("GeldGeschickt, SonstigeAusgaben, KerosinGespart, ");
        printf("Image, ");

        printf("Flugzeuge, Passagiere, PassagiereHome, Aktienkurs, ");
        printf("Flüge, Aufträge, LastMinute, ");
        printf("Firmenwert, PassZufrieden, PassUnzufrieden, PersonalZufrieden, ");
        printf("Verspätung, Unfälle, Sabotiert, ");
        printf("Mitarbeiter, Niederlassungen, AnzRouten, Gehalt, ");
        printf("AktienGesamt, AktienSA, AktienFL, AktienPT, AktienHA, ");
        printf("Frachtaufträge, Frachttonnen, ");
        printf("ZielSA, ZielFL, ZielPT, ZielHA, ");
        hprintf("Planetype");
    }
    std::cout << (LPCTSTR)prefix << ": ";

    std::vector<__int64> values;
    auto balanceAvg = qPlayer.BilanzWoche.Hole();
    auto balance = qPlayer.BilanzGesamt;
    values.insert(values.end(), {Sim.Date, qPlayer.Money, qPlayer.Credit, getMoneyAvailable()});
    values.insert(values.end(), {balance.GetOpSaldo(), balanceAvg.GetOpSaldo(), balanceAvg.GetOpGewinn(), balanceAvg.GetOpVerlust()});
    values.insert(values.end(), {balance.Tickets, balance.Auftraege, balance.FrachtAuftraege});
    values.insert(values.end(), {balance.KerosinFlug, balance.KerosinVorrat, balance.Essen});
    values.insert(values.end(), {balance.Vertragsstrafen, balance.Wartung, balance.FlugzeugUmbau});
    values.insert(values.end(), {balance.Personal, balance.Gatemiete, balance.Citymiete, balance.Routenmiete});
    values.insert(values.end(), {balance.HabenZinsen, balance.HabenRendite, balance.KreditNeu});
    values.insert(values.end(), {balance.SollZinsen, balance.SollRendite, balance.KreditTilgung, balance.Steuer});
    values.insert(values.end(), {balance.Aktienverkauf, balance.AktienEmission, balance.AktienEmissionKompErh});
    values.insert(values.end(), {balance.AktienEmissionKompGez, balance.Aktienkauf, balance.AktienEmissionFee});
    values.insert(values.end(), {balance.FlugzeugVerkauf, balance.Takeovers, balance.FlugzeugKauf, balance.FlugzeugUpgrades});
    values.insert(values.end(), {balance.ExpansionCity, balance.ExpansionRouten, balance.ExpansionGates, balance.ExpansionTanks});
    values.insert(values.end(), {balance.SabotageGeklaut, balance.SabotageKomp});
    values.insert(values.end(), {balance.Sabotage, balance.SabotageStrafe, balance.SabotageSchaden});
    values.insert(values.end(), {balance.BodyguardRabatt, balance.GeldErhalten, balance.SonstigeEinnahmen});
    values.insert(values.end(), {balance.PanneSchaden, balance.SecurityKosten, balance.WerbeKosten});
    values.insert(values.end(), {balance.GeldGeschickt, balance.SonstigeAusgaben, balance.KerosinGespart});
    values.insert(values.end(), {qPlayer.Image});

    std::vector<SLONG> valuesStat;
    valuesStat.insert(valuesStat.end(), {STAT_FLUGZEUGE, STAT_PASSAGIERE, STAT_PASSAGIERE_HOME, STAT_AKTIENKURS});
    valuesStat.insert(valuesStat.end(), {STAT_FLUEGE, STAT_AUFTRAEGE, STAT_LMAUFTRAEGE});
    valuesStat.insert(valuesStat.end(), {STAT_FIRMENWERT, STAT_ZUFR_PASSAGIERE, STAT_UNZUFR_PASSAGIERE, STAT_ZUFR_PERSONAL});
    valuesStat.insert(valuesStat.end(), {STAT_VERSPAETUNG, STAT_UNFAELLE, STAT_SABOTIERT});
    valuesStat.insert(valuesStat.end(), {STAT_MITARBEITER, STAT_NIEDERLASSUNGEN, STAT_ROUTEN, STAT_GEHALT});
    valuesStat.insert(valuesStat.end(), {STAT_AKTIEN_ANZAHL, STAT_AKTIEN_SA, STAT_AKTIEN_FL, STAT_AKTIEN_PT, STAT_AKTIEN_HA});
    valuesStat.insert(valuesStat.end(), {STAT_FRACHTEN, STAT_TONS});

    for (auto i : values) {
        std::cout << i << ", ";
    }
    for (auto i : valuesStat) {
        std::cout << qPlayer.Statistiken[i].GetAtPastDay(1) << ", ";
    }

    for (SLONG i = 0; i < 4; i++) {
        std::cout << Sim.Players.Players[i].Statistiken[STAT_MISSIONSZIEL].GetAtPastDay(1) << ", ";
    }

    SLONG count = 0;
    for (SLONG i = 0; i < qPlayer.Planes.AnzEntries(); i++) {
        if (qPlayer.Planes.IsInAlbum(i)) {
            if (kPlaneScoreForceBest + 0x10000000 == qPlayer.Planes[i].TypeId) {
                count++;
            }
        }
    }
    std::cout << count << std::endl;
}

void Bot::RobotInit() {
    if (gQuickTestRun > 0) {
        printStatisticsLine("BotStatistics", (Sim.Date == 0));
    } else {
        auto balance = qPlayer.BilanzWoche.Hole();
        greenprintf("Bot.cpp: Enter RobotInit(): Current day: %d, money: %s $ (op saldo %s = %s %s)", Sim.Date, Insert1000erDots64(qPlayer.Money).c_str(),
                    Insert1000erDots64(balance.GetOpSaldo()).c_str(), Insert1000erDots64(balance.GetOpGewinn()).c_str(),
                    Insert1000erDots64(balance.GetOpVerlust()).c_str());
    }

    /* print inventory */
    printf("Inventory: ");
    for (SLONG d = 0; d < 6; d++) {
        if (qPlayer.Items[d] != 0xff) {
            printf("%s, ", Helper::getItemName(qPlayer.Items[d]));
        }
    }
    printf("\n");

    if (mFirstRun) {
        hprintf("Bot::RobotInit(): First run.");

        /* random source */
        LocalRandom.SRand(qPlayer.WaitWorkTill);

        /* starting planes */
        for (SLONG i = 0; i < qPlayer.Planes.AnzEntries(); i++) {
            if (qPlayer.Planes.IsInAlbum(i)) {
                mPlanesForJobsUnassigned.push_back(qPlayer.Planes.GetIdFromIndex(i));
            }
        }

        /* mission specialization */
        if (Sim.Difficulty == DIFF_TUTORIAL) {
            mItemPills = -1;      /* item not available */
            mItemAntiStrike = -1; /* item not available */
        }
        if (Sim.Difficulty <= DIFF_FIRST) {
            mItemAntiVirus = -1; /* item not available */
        }

        if (Sim.Difficulty == DIFF_NORMAL) {
            kMaximumRouteUtilization = 20;
        }

        /* is this a mission that is usually very fast? */
        if (Sim.Difficulty != DIFF_FREEGAME && Sim.Difficulty != DIFF_FREEGAMEMAP) {
            if (Sim.Difficulty <= DIFF_EASY) {
                // TODO
                // mLongTermStrategy = false;
            }
        }

        if (qPlayer.RobotUse(ROBOT_USE_FORCEROUTES)) {
            std::swap(mPlanesForJobsUnassigned, mPlanesForRoutesUnassigned);
        }

        if (qPlayer.RobotUse(ROBOT_USE_GROSSESKONTO)) {
            /* immediately start saving money */
            mRunToFinalObjective = FinalPhase::SaveMoney;
        }

        if (qPlayer.RobotUse(ROBOT_USE_DESIGNER_BUY)) {
            if (Sim.Difficulty == DIFF_ATFS05) {
                kSwitchToRoutesNumPlanesMin = std::max(3, kSwitchToRoutesNumPlanesMin);
                kSwitchToRoutesNumPlanesMax = std::max(3, kSwitchToRoutesNumPlanesMax);
                setHardcodedDesignerPlaneLarge();
                mDesignerPlaneFile = FullFilename("botplane_atfs05_1.plane", MyPlanePath);
            } else if (Sim.Difficulty == DIFF_ATFS08) {
                kSwitchToRoutesNumPlanesMin = std::max(5, kSwitchToRoutesNumPlanesMin);
                kSwitchToRoutesNumPlanesMax = std::max(5, kSwitchToRoutesNumPlanesMax);
                setHardcodedDesignerPlaneEco();
                mDesignerPlaneFile = FullFilename("botplane_atfs08_1.plane", MyPlanePath);
            }
            if (!mDesignerPlaneFile.empty()) {
                if (DoesFileExist(mDesignerPlaneFile) == 1) {
                    mDesignerPlane.Load(mDesignerPlaneFile);
                } else {
                    mDesignerPlane.Save(mDesignerPlaneFile);
                }
            }
        }

        if (qPlayer.RobotUse(ROBOT_USE_MAX20PERCENT)) {
            kOwnStockPosessionRatio = 20;
        }

        /* bot level */
        hprintf("Bot::RobotInit(): We are %s with bot level = %s.", (LPCTSTR)qPlayer.AirlineX, StandardTexte.GetS(TOKEN_NEWGAME, 5001 + qPlayer.BotLevel));
        if (qPlayer.BotLevel <= 1) {
            kMaxTicketPriceFactor = std::min(1.0, kMaxTicketPriceFactor);
            kSchedulingMinScoreRatio = std::min(3.0f, kSchedulingMinScoreRatio);
            kSchedulingMinScoreRatioLastMinute = std::min(3.0f, kSchedulingMinScoreRatioLastMinute);
            kPlaneMinimumZustand = std::min(80, kPlaneMinimumZustand);
            kNumRoutesStartBuyingTanks = 99;
        } else if (qPlayer.BotLevel <= 2) {
            kMaxTicketPriceFactor = std::min(2.0, kMaxTicketPriceFactor);
            kSchedulingMinScoreRatio = std::min(5.0f, kSchedulingMinScoreRatio);
            kSchedulingMinScoreRatioLastMinute = std::min(5.0f, kSchedulingMinScoreRatioLastMinute);
            kMaxKerosinQualiZiel = std::min(1.0, kMaxKerosinQualiZiel);
        }

        mFirstRun = false;
    }

    for (auto &i : qPlayer.RobotActions) {
        i = {};
    }

    mLastTimeInRoom.clear();
    mNumActionsToday = 0;

    /* strategy state */
    mBestUsedPlaneIdx = -1;
    mDayStarted = false;
    mNeedToShutdownSecurity = false;

    /* status boss office */
    mBossNumCitiesAvailable = -1;
    mBossGateAvailable = false;

    /* items */
    mIsSickToday = false;

    RobotPlan();
    hprintf("Bot.cpp: Leaving RobotInit()");
}

void Bot::RobotPlan() {
    // hprintf("Bot.cpp: Enter RobotPlan()");

    if (mFirstRun) {
        redprintf("Bot::RobotPlan(): Bot was not initialized!");
        RobotInit();
        hprintf("Bot.cpp: Leaving RobotPlan() (not initialized)\n");
        return;
    }

    if (mIsSickToday && qPlayer.HasItem(ITEM_TABLETTEN)) {
        GameMechanic::useItem(qPlayer, ITEM_TABLETTEN);
        hprintf("Bot::RobotPlan(): Cured sickness using item pills");
        mIsSickToday = false;
    }

    auto &qRobotActions = qPlayer.RobotActions;

    SLONG actions[] = {ACTION_STARTDAY, ACTION_STARTDAY_LAPTOP,
                       /* repeated actions */
                       ACTION_BUERO, ACTION_CALL_INTERNATIONAL, ACTION_CALL_INTER_HANDY, ACTION_CHECKAGENT1, ACTION_CHECKAGENT2, ACTION_CHECKAGENT3,
                       ACTION_UPGRADE_PLANES, ACTION_BUYNEWPLANE, ACTION_BUYUSEDPLANE, ACTION_VISITMUSEUM, ACTION_PERSONAL, ACTION_BUY_KEROSIN,
                       ACTION_BUY_KEROSIN_TANKS, ACTION_SABOTAGE, ACTION_SET_DIVIDEND, ACTION_RAISEMONEY, ACTION_DROPMONEY, ACTION_EMITSHARES,
                       ACTION_SELLSHARES, ACTION_BUYSHARES, ACTION_VISITMECH, ACTION_VISITNASA, ACTION_VISITTELESCOPE, ACTION_VISITMAKLER, ACTION_VISITARAB,
                       ACTION_VISITRICK, ACTION_VISITKIOSK, ACTION_VISITDUTYFREE, ACTION_VISITAUFSICHT, ACTION_EXPANDAIRPORT, ACTION_VISITROUTEBOX,
                       ACTION_VISITROUTEBOX2, ACTION_VISITSECURITY, ACTION_VISITSECURITY2, ACTION_VISITDESIGNER, ACTION_WERBUNG_ROUTES, ACTION_WERBUNG,
                       ACTION_VISITADS, ACTION_OVERTAKE_AIRLINE};

    if (qRobotActions[0].ActionId != ACTION_NONE || qRobotActions[1].ActionId != ACTION_NONE) {
        hprintf("Bot.cpp: Leaving RobotPlan() (actions already planned)\n");
        return;
    }
    qRobotActions[1].ActionId = ACTION_NONE;
    qRobotActions[2].ActionId = ACTION_NONE;

    /* populate prio list. Will be sorted by priority (1st order) and then by score (2nd order)-
     * score depends on walking distance. */
    struct PrioListItem {
        SLONG actionId{-1};
        Prio prio{Prio::None};
        SLONG secondaryScore{0};
        SLONG walkingDistance{0};
    };
    std::vector<PrioListItem> prioList;
    for (auto &action : actions) {
        if (!Helper::checkRoomOpen(action)) {
            continue;
        }
        auto prio = condAll(action);
        if (prio == Prio::None) {
            continue;
        }

        SLONG p = qPlayer.PlayerNum;
        SLONG room = Helper::getRoomFromAction(p, action);
        SLONG score = qPlayer.PlayerWalkRandom.Rand(0, 100);
        prioList.emplace_back(PrioListItem{action, prio, score});

        if (prio >= Prio::Medium && room > 0 && (Sim.Time > 540000)) {
            /* factor in walking distance for more important actions */
            prioList.back().walkingDistance = Helper::getWalkDistance(p, room);
        }
    }

    if (prioList.size() < 2) {
        prioList.emplace_back(PrioListItem{ACTION_CHECKAGENT2, Prio::Medium, 10000});
    }
    if (prioList.size() < 2) {
        prioList.emplace_back(PrioListItem{ACTION_CHECKAGENT1, Prio::Medium, 10000});
    }

    /* sort by priority */
    std::sort(prioList.begin(), prioList.end(), [](const PrioListItem &a, const PrioListItem &b) {
        if (a.prio == b.prio) {
            return ((a.secondaryScore + a.walkingDistance) < (b.secondaryScore + b.walkingDistance));
        }
        return (a.prio > b.prio);
    });

    /*for (const auto &qAction : prioList) {
        hprintf("Bot::RobotPlan(): %s with prio %s (%d+%d)", getRobotActionName(qAction.actionId), getPrioName(qAction.prio), qAction.secondaryScore,
                qAction.walkingDistance);
    }*/

    auto condNoRun = (qPlayer.BotLevel >= 3 ? Prio::Low : (qPlayer.BotLevel >= 2 ? Prio::Medium : Prio::Top));

    qRobotActions[1].ActionId = prioList[0].actionId;
    qRobotActions[1].Running = (prioList[0].prio > condNoRun);
    qRobotActions[1].Prio = static_cast<SLONG>(prioList[0].prio);
    qRobotActions[2].ActionId = prioList[1].actionId;
    qRobotActions[2].Running = (prioList[1].prio > condNoRun);
    qRobotActions[2].Prio = static_cast<SLONG>(prioList[1].prio);

    // hprintf("Bot::RobotPlan(): Action 0: %s", getRobotActionName(qRobotActions[0].ActionId));
    // greenprintf("Bot::RobotPlan(): Action 1: %s with prio %s", getRobotActionName(qRobotActions[1].ActionId), getPrioName(prioList[0].prio));
    // greenprintf("Bot::RobotPlan(): Action 2: %s with prio %s", getRobotActionName(qRobotActions[2].ActionId), getPrioName(prioList[1].prio));

    if (qRobotActions[1].ActionId == ACTION_NONE) {
        redprintf("Did not plan action for slot #1");
    }
    if (qRobotActions[2].ActionId == ACTION_NONE) {
        redprintf("Did not plan action for slot #2");
    }

    // hprintf("Bot.cpp: Leaving RobotPlan()\n");
}

void Bot::RobotExecuteAction() {
    if (mFirstRun) {
        redprintf("Bot::RobotExecuteAction(): Bot was not initialized!");
        RobotInit();
        hprintf("Bot.cpp: Leaving RobotExecuteAction() (not initialized)\n");
        return;
    }

    /* refuse to work outside working hours (game sometimes calls this too early) */
    if (Sim.Time <= 540000) { /* check if it is precisely 09:00 or earlier */
        hprintf("Bot.cpp: Leaving RobotExecuteAction() (too early)\n");
        forceReplanning();
        return;
    }
    if (Sim.GetHour() >= 18) {
        hprintf("Bot.cpp: Leaving RobotExecuteAction() (too late)\n");
        forceReplanning();
        return;
    }

    if (kAlwaysReplan) {
        forceReplanning();
    }

    /* handy references to player data (rw) */
    auto &qAction = qPlayer.RobotActions[0];
    auto &qWorkCountdown = qPlayer.WorkCountdown;

    LocalRandom.Rand(2); // Sicherheitshalber, damit wir immer genau ein Random ausführen

    mNumActionsToday += 1;
    greenprintf("Bot::RobotExecuteAction(): Executing %s (#%d, %s), current time: %02ld:%02ld, money: %s $ (available: %s $)",
                getRobotActionName(qAction.ActionId), mNumActionsToday, getPrioName(qAction.Prio), Sim.GetHour(), Sim.GetMinute(),
                (LPCTSTR)Insert1000erDots64(qPlayer.Money), (LPCTSTR)Insert1000erDots64(getMoneyAvailable()));

    mOnThePhone = 0;

    __int64 moneyAvailable = getMoneyAvailable();
    switch (qAction.ActionId) {
    case 0:
        qWorkCountdown = 2;
        break;

    case ACTION_STARTDAY:
        if (condStartDay() != Prio::None) {
            actionStartDay(moneyAvailable);
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_STARTDAY_LAPTOP:
        if (condStartDayLaptop() != Prio::None) {
            actionStartDayLaptop(moneyAvailable);
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_BUERO:
        if (condBuero() != Prio::None) {
            actionBuero();
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_CALL_INTERNATIONAL:
        if (condCallInternational() != Prio::None) {
            hprintf("Bot::RobotExecuteAction(): Calling international using office phone.");
            actionCallInternational(true);
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_CALL_INTER_HANDY:
        if (condCallInternationalHandy() != Prio::None) {
            hprintf("Bot::RobotExecuteAction(): Calling international using mobile phone.");
            actionCallInternational(false);
            mOnThePhone = 30;
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

        // Last-Minute
    case ACTION_CHECKAGENT1:
        if (condCheckLastMinute() != Prio::None) {
            actionCheckLastMinute();
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

        // Reisebüro:
    case ACTION_CHECKAGENT2:
        if (condCheckTravelAgency() != Prio::None) {
            actionCheckTravelAgency();
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

        // Frachtbüro:
    case ACTION_CHECKAGENT3:
        if (condCheckFreight() != Prio::None) {
            actionCheckFreightDepot();
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

    case ACTION_UPGRADE_PLANES:
        if (condUpgradePlanes() != Prio::None) {
            actionUpgradePlanes();
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }

        qWorkCountdown = 20 * 5;
        break;

    case ACTION_BUYNEWPLANE:
        if (condBuyNewPlane(moneyAvailable) != Prio::None) {
            actionBuyNewPlane(moneyAvailable);
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_BUYUSEDPLANE:
        if (condBuyUsedPlane(moneyAvailable) != Prio::None) {
            actionBuyUsedPlane(moneyAvailable);
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        break;

    case ACTION_VISITMUSEUM:
        if (condVisitMuseum() != Prio::None) {
            mBestUsedPlaneIdx = findBestAvailableUsedPlane();
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        break;

    case ACTION_PERSONAL:
        if (condVisitHR() != Prio::None) {
            actionVisitHR();
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }

        qWorkCountdown = 20 * 5;
        break;

    case ACTION_BUY_KEROSIN:
        if (condBuyKerosine(moneyAvailable) != Prio::None) {
            actionBuyKerosine(moneyAvailable);
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_BUY_KEROSIN_TANKS:
        if (condBuyKerosineTank(moneyAvailable) != Prio::None) {
            actionBuyKerosineTank(moneyAvailable);
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_SABOTAGE:
        if (condSabotage(moneyAvailable) != Prio::None) {
            actionSabotage(moneyAvailable);
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_SET_DIVIDEND:
        if (condIncreaseDividend(moneyAvailable) != Prio::None) {
            SLONG _dividende = qPlayer.Dividende;
            SLONG maxToEmit = (2500000 - qPlayer.MaxAktien) / 100 * 100;
            if (kReduceDividend && maxToEmit < 10000) {
                /* we cannot emit any shares anymore. We do not care about stock prices now. */
                _dividende = 0;
            } else if (qPlayer.RobotUse(ROBOT_USE_HIGHSHAREPRICE)) {
                _dividende = 25;
            } else if (LocalRandom.Rand(10) == 0) {
                _dividende++;
                Limit(5, _dividende, 25);
            }

            if (_dividende != qPlayer.Dividende) {
                hprintf("Bot::RobotExecuteAction(): Setting dividend to %d", _dividende);
                GameMechanic::setDividend(qPlayer, _dividende);
            }
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_RAISEMONEY:
        if (condTakeOutLoan() != Prio::None) {
            __int64 limit = qPlayer.CalcCreditLimit();
            __int64 moneyRequired = -getMoneyAvailable();
            __int64 m = std::min(limit, moneyRequired);
            m = std::max(m, 1000LL);
            if (mRunToFinalObjective == FinalPhase::TargetRun) {
                m = limit;
            }
            if (m > 0) {
                hprintf("Bot::RobotExecuteAction(): Taking loan: %s $", (LPCTSTR)Insert1000erDots64(m));
                GameMechanic::takeOutCredit(qPlayer, m);
                moneyAvailable = getMoneyAvailable();
            }
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_DROPMONEY:
        if (condDropMoney(moneyAvailable) != Prio::None) {
            __int64 m = std::min({qPlayer.Credit, moneyAvailable, getWeeklyOpSaldo()});
            m = std::max(m, 1000LL);
            hprintf("Bot::RobotExecuteAction(): Paying back loan: %s $", (LPCTSTR)Insert1000erDots64(m));
            GameMechanic::payBackCredit(qPlayer, m);
            moneyAvailable = getMoneyAvailable();
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_EMITSHARES:
        if (condEmitShares() != Prio::None) {
            actionEmitShares();
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 6;
        break;

    case ACTION_SELLSHARES:
        if (condSellShares(moneyAvailable) != Prio::None) {
            actionSellShares(moneyAvailable);
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 6;
        break;

    case ACTION_BUYSHARES: {
        bool performedAction = false;
        if (condBuyNemesisShares(moneyAvailable) != Prio::None) {
            actionBuyNemesisShares(moneyAvailable);
            performedAction = true;
        }
        if (condBuyOwnShares(moneyAvailable) != Prio::None) {
            actionBuyOwnShares(moneyAvailable);
            performedAction = true;
        }
        if (!performedAction) {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
    }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_OVERTAKE_AIRLINE:
        if (condOvertakeAirline() != Prio::None) {
            actionOvertakeAirline();
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 6;
        break;

    case ACTION_VISITMECH:
        if (condVisitMech() != Prio::None) {
            actionVisitMech();
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_VISITNASA:
        if (condVisitNasa(moneyAvailable) != Prio::None) {
            const auto &qPrices = (Sim.Difficulty == DIFF_FINAL) ? RocketPrices : StationPrices;
            auto nParts = sizeof(qPrices) / sizeof(qPrices[0]);
            for (SLONG i = 0; i < nParts; i++) {
                if ((qPlayer.RocketFlags & (1 << i)) == 0 && moneyAvailable >= qPrices[i]) {
                    qPlayer.ChangeMoney(-qPrices[i], 3400, "");
                    PlayFanfare();
                    qPlayer.RocketFlags |= (1 << i);
                }
                moneyAvailable = getMoneyAvailable();
            }
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_VISITTELESCOPE:
    case ACTION_VISITKIOSK:
        if (condVisitMisc() != Prio::None) {
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_VISITMAKLER:
        if (condVisitMakler() != Prio::None) {
            mBestPlaneTypeId = findBestAvailablePlaneType(false, true)[0];

            if (mItemAntiStrike == 0) {
                if (GameMechanic::PickUpItemResult::PickedUp == GameMechanic::pickUpItem(qPlayer, ITEM_BH)) {
                    hprintf("Bot::RobotExecuteAction(): Picked up item BH");
                    mItemAntiStrike = 1;
                }
            }
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_VISITARAB:
        if (condVisitArab() != Prio::None) {
            if (mItemArabTrust == 1) {
                if (GameMechanic::useItem(qPlayer, ITEM_MG)) {
                    hprintf("Bot::RobotExecuteAction(): Used item MG");
                    mItemArabTrust = 2;
                }
            }
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_VISITRICK:
        if (condVisitRick() != Prio::None) {
            if (mItemAntiStrike == 3) {
                if (GameMechanic::useItem(qPlayer, ITEM_HUFEISEN)) {
                    hprintf("Bot::RobotExecuteAction(): Used item horse shoe");
                    mItemAntiStrike = 4;
                }
            }
            if (qPlayer.StrikeHours > 0 && qPlayer.StrikeEndType == 0 && mItemAntiStrike == 4) {
                hprintf("Bot::RobotExecuteAction(): Ended strike using drunk guy");
                GameMechanic::endStrike(qPlayer, GameMechanic::EndStrikeMode::Drunk);
            }
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_VISITDUTYFREE:
        if (condVisitDutyFree(moneyAvailable) != Prio::None) {
            actionVisitDutyFree(moneyAvailable);
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 5;
        break;

    case ACTION_VISITAUFSICHT:
        if (condVisitBoss(moneyAvailable) != Prio::None) {
            actionVisitBoss();
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

    case ACTION_EXPANDAIRPORT:
        if (condExpandAirport(moneyAvailable) != Prio::None) {
            hprintf("Bot::RobotExecuteAction(): Expanding Airport");
            GameMechanic::expandAirport(qPlayer);
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

    case ACTION_VISITROUTEBOX:
        if (condVisitRouteBoxPlanning() != Prio::None) {
            actionVisitRouteBox();
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

    case ACTION_VISITROUTEBOX2:
        if (condVisitRouteBoxRenting(moneyAvailable) != Prio::None) {
            actionRentRoute();
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

    case ACTION_VISITSECURITY:
        if (condVisitSecurity(moneyAvailable) != Prio::None) {
            actionVisitSecurity(moneyAvailable);
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

    case ACTION_VISITSECURITY2:
        if (condSabotageSecurity() != Prio::None) {
            if (GameMechanic::sabotageSecurityOffice(qPlayer)) {
                hprintf("Bot::RobotExecuteAction(): Successfully sabotaged security office.");
            } else {
                redprintf("Bot::RobotExecuteAction(): Failed to sabotage security office!");
            }
            mNeedToShutdownSecurity = false;
            mLastTimeInRoom.erase(ACTION_SABOTAGE); /* allow sabotage again */
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 1;
        break;

    case ACTION_VISITDESIGNER:
        if (condVisitDesigner(moneyAvailable) != Prio::None) {
            actionBuyDesignerPlane(moneyAvailable);
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

    case ACTION_WERBUNG_ROUTES:
        if (condBuyAdsForRoutes(moneyAvailable) != Prio::None) {
            actionBuyAdsForRoutes(moneyAvailable);
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

    case ACTION_WERBUNG:
        if (condBuyAds(moneyAvailable) != Prio::None) {
            actionBuyAds(moneyAvailable);
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

    case ACTION_VISITADS:
        if (condVisitAds() != Prio::None) {
            if (mItemAntiVirus == 3) {
                if (GameMechanic::useItem(qPlayer, ITEM_DART)) {
                    hprintf("Bot::RobotExecuteAction(): Used item darts");
                    mItemAntiVirus = 4;
                }
            }
            if (mItemAntiVirus == 4) {
                if (qPlayer.HasItem(ITEM_DISKETTE) == 0) {
                    GameMechanic::pickUpItem(qPlayer, ITEM_DISKETTE);
                    hprintf("Bot::RobotExecuteAction(): Picked up item floppy disk");
                }
            }
            mCurrentImage = qPlayer.Image;
            hprintf("Bot::RobotExecuteAction(): Checked company image: %d", mCurrentImage);
        } else {
            orangeprintf("Bot::RobotExecuteAction(): Conditions not met anymore.");
        }
        qWorkCountdown = 20 * 7;
        break;

    default:
        redprintf("Bot::RobotExecuteAction(): Trying to execute invalid action: %s", getRobotActionName(qAction.ActionId));
        // DebugBreak();
    }

    mLastTimeInRoom[qAction.ActionId] = Sim.Time;

    if (qPlayer.RobotUse(ROBOT_USE_WORKQUICK_2) && qWorkCountdown > 2) {
        qWorkCountdown /= 2;
    }

    if (qPlayer.RobotUse(ROBOT_USE_WORKVERYQUICK) && qWorkCountdown > 4) {
        qWorkCountdown /= 4;
    } else if (qPlayer.RobotUse(ROBOT_USE_WORKQUICK) && qWorkCountdown > 2) {
        qWorkCountdown /= 2;
    }

    // hprintf("Bot.cpp: Leaving RobotExecuteAction()\n");
    hprintf("");
}

TEAKFILE &operator<<(TEAKFILE &File, const PlaneTime &planeTime) {
    File << static_cast<SLONG>(planeTime.getDate());
    File << static_cast<SLONG>(planeTime.getHour());
    return (File);
}

TEAKFILE &operator<<(TEAKFILE &File, const Bot &bot) {
    File << static_cast<SLONG>(bot.mLastTimeInRoom.size());
    for (const auto &i : bot.mLastTimeInRoom) {
        File << i.first << i.second;
    }

    File << bot.mNumActionsToday;

    File << static_cast<SLONG>(bot.mPlanesForJobs.size());
    for (const auto &i : bot.mPlanesForJobs) {
        File << i;
    }

    File << static_cast<SLONG>(bot.mPlanesForJobsUnassigned.size());
    for (const auto &i : bot.mPlanesForJobsUnassigned) {
        File << i;
    }

    File << static_cast<SLONG>(bot.mPlanesForRoutes.size());
    for (const auto &i : bot.mPlanesForRoutes) {
        File << i;
    }

    File << static_cast<SLONG>(bot.mPlanesForRoutesUnassigned.size());
    for (const auto &i : bot.mPlanesForRoutesUnassigned) {
        File << i;
    }

    File << static_cast<SLONG>(bot.mKnownPlaneTypes.size());
    for (const auto &i : bot.mKnownPlaneTypes) {
        File << i;
    }

    File << bot.mLongTermStrategy;
    File << bot.mBestPlaneTypeId << bot.mBestUsedPlaneIdx;
    File << bot.mBuyPlaneForRouteId << bot.mPlaneTypeForNewRoute;

    File << static_cast<SLONG>(bot.mPlanesForNewRoute.size());
    for (const auto &i : bot.mPlanesForNewRoute) {
        File << i;
    }

    File << bot.mWantToRentRouteId;
    File << bot.mFirstRun << bot.mDayStarted << bot.mDoRoutes;
    File << static_cast<SLONG>(bot.mRunToFinalObjective) << bot.mMoneyForFinalObjective;
    File << bot.mOutOfGates;
    File << bot.mNeedToPlanJobs << bot.mNeedToPlanRoutes;
    File << bot.mMoneyReservedForRepairs << bot.mMoneyReservedForUpgrades;
    File << bot.mMoneyReservedForAuctions << bot.mMoneyReservedForFines;
    File << bot.mNemesis << bot.mNemesisScore << bot.mNeedToShutdownSecurity << bot.mUsingSecurity;
    File << bot.mNemesisSabotaged << bot.mArabHintsTracker << bot.mCurrentImage;

    File << bot.mBossNumCitiesAvailable;
    File << bot.mBossGateAvailable;

    File << bot.mTankRatioEmptiedYesterday;
    File << bot.mKerosineUsedTodaySoFar;
    File << bot.mKerosineLevelLastChecked;

    File << static_cast<SLONG>(bot.mRoutes.size());
    for (const auto &i : bot.mRoutes) {
        File << i.routeId << i.routeReverseId << i.planeTypeId;
        File << i.routeUtilization << i.routeOwnUtilization << i.image;
        File << i.planeUtilization << i.planeUtilizationFC;
        File << i.ticketCostFactor;

        File << static_cast<SLONG>(i.planeIds.size());
        for (const auto &j : i.planeIds) {
            File << j;
        }
        File << i.canUpgrade;
    }

    File << static_cast<SLONG>(bot.mRoutesSortedByOwnUtilization.size());
    for (const auto &i : bot.mRoutesSortedByOwnUtilization) {
        File << i;
    }

    File << bot.mRoutesUpdated << bot.mRoutesUtilizationUpdated;
    File << static_cast<SLONG>(bot.mRoutesNextStep) << bot.mImproveRouteId;

    File << bot.mNumEmployees << bot.mExtraPilots << bot.mExtraBegleiter;

    File << bot.mItemPills << bot.mItemAntiVirus << bot.mItemAntiStrike << bot.mItemArabTrust << bot.mIsSickToday;

    File << static_cast<SLONG>(bot.mPlanerSolution.size());
    for (const auto &solution : bot.mPlanerSolution) {
        File << static_cast<SLONG>(solution.jobs.size());
        for (const auto &i : solution.jobs) {
            File << i.jobIdx;
            File << i.start;
            File << i.end;
            File << i.objectId;
            File << i.bIsFreight;
        }

        File << solution.totalPremium;
        File << solution.planeId;
        File << solution.scheduleFromTime;
    }

    File << bot.mOnThePhone;

    File << bot.mDesignerPlane;
    File << bot.mDesignerPlaneFile;

    SLONG magicnumber = 0x42;
    File << magicnumber;

    return (File);
}

TEAKFILE &operator>>(TEAKFILE &File, PlaneTime &planeTime) {
    SLONG date;
    SLONG time;
    File >> date;
    File >> time;
    planeTime = {date, time};

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

    File >> bot.mNumActionsToday;

    File >> size;
    bot.mPlanesForJobs.resize(size);
    for (SLONG i = 0; i < size; i++) {
        File >> bot.mPlanesForJobs[i];
    }

    File >> size;
    bot.mPlanesForJobsUnassigned.resize(size);
    for (SLONG i = 0; i < size; i++) {
        File >> bot.mPlanesForJobsUnassigned[i];
    }

    File >> size;
    bot.mPlanesForRoutes.resize(size);
    for (SLONG i = 0; i < size; i++) {
        File >> bot.mPlanesForRoutes[i];
    }

    File >> size;
    bot.mPlanesForRoutesUnassigned.resize(size);
    for (SLONG i = 0; i < size; i++) {
        File >> bot.mPlanesForRoutesUnassigned[i];
    }

    File >> size;
    bot.mKnownPlaneTypes.resize(size);
    for (SLONG i = 0; i < size; i++) {
        File >> bot.mKnownPlaneTypes[i];
    }

    File >> bot.mLongTermStrategy;
    File >> bot.mBestPlaneTypeId >> bot.mBestUsedPlaneIdx;
    File >> bot.mBuyPlaneForRouteId >> bot.mPlaneTypeForNewRoute;

    File >> size;
    bot.mPlanesForNewRoute.resize(size);
    for (SLONG i = 0; i < size; i++) {
        File >> bot.mPlanesForNewRoute[i];
    }

    File >> bot.mWantToRentRouteId;
    File >> bot.mFirstRun >> bot.mDayStarted >> bot.mDoRoutes;
    SLONG runToFinalObjective = 0;
    File >> runToFinalObjective >> bot.mMoneyForFinalObjective;
    bot.mRunToFinalObjective = static_cast<Bot::FinalPhase>(runToFinalObjective);
    File >> bot.mOutOfGates;
    File >> bot.mNeedToPlanJobs >> bot.mNeedToPlanRoutes;
    File >> bot.mMoneyReservedForRepairs >> bot.mMoneyReservedForUpgrades;
    File >> bot.mMoneyReservedForAuctions >> bot.mMoneyReservedForFines;
    File >> bot.mNemesis >> bot.mNemesisScore >> bot.mNeedToShutdownSecurity >> bot.mUsingSecurity;
    File >> bot.mNemesisSabotaged >> bot.mArabHintsTracker >> bot.mCurrentImage;

    File >> bot.mBossNumCitiesAvailable;
    File >> bot.mBossGateAvailable;

    File >> bot.mTankRatioEmptiedYesterday;
    File >> bot.mKerosineUsedTodaySoFar;
    File >> bot.mKerosineLevelLastChecked;

    File >> size;
    bot.mRoutes.resize(size);
    for (SLONG i = 0; i < bot.mRoutes.size(); i++) {
        Bot::RouteInfo info;
        File >> info.routeId >> info.routeReverseId >> info.planeTypeId;
        File >> info.routeUtilization >> info.routeOwnUtilization >> info.image;
        File >> info.planeUtilization >> info.planeUtilizationFC;
        File >> info.ticketCostFactor;

        File >> size;
        info.planeIds.resize(size);
        for (SLONG i = 0; i < info.planeIds.size(); i++) {
            File >> info.planeIds[i];
        }
        File >> info.canUpgrade;

        bot.mRoutes[i] = info;
    }

    File >> size;
    bot.mRoutesSortedByOwnUtilization.resize(size);
    for (SLONG i = 0; i < size; i++) {
        File >> bot.mRoutesSortedByOwnUtilization[i];
    }

    File >> bot.mRoutesUpdated >> bot.mRoutesUtilizationUpdated;
    SLONG routesNextStep = 0;
    File >> routesNextStep >> bot.mImproveRouteId;
    bot.mRoutesNextStep = static_cast<Bot::RoutesNextStep>(routesNextStep);

    File >> bot.mNumEmployees >> bot.mExtraPilots >> bot.mExtraBegleiter;

    File >> bot.mItemPills >> bot.mItemAntiVirus >> bot.mItemAntiStrike >> bot.mItemArabTrust >> bot.mIsSickToday;

    File >> size;
    bot.mPlanerSolution.resize(size);
    for (SLONG i = 0; i < bot.mPlanerSolution.size(); i++) {
        auto &solution = bot.mPlanerSolution[i];

        File >> size;
        for (SLONG j = 0; j < size; j++) {
            SLONG jobId;
            PlaneTime start;
            PlaneTime end;
            File >> jobId;
            File >> start;
            File >> end;

            solution.jobs.emplace_back(jobId, start, end);
            File >> solution.jobs.back().objectId;
            File >> solution.jobs.back().bIsFreight;
        }

        File >> solution.totalPremium;
        File >> solution.planeId;
        File >> solution.scheduleFromTime;
    }

    File >> bot.mOnThePhone;

    File >> bot.mDesignerPlane;
    File >> bot.mDesignerPlaneFile;

    SLONG magicnumber = 0;
    File >> magicnumber;
    assert(magicnumber == 0x42);

    return (File);
}
