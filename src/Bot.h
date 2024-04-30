#ifndef BOT_H_
#define BOT_H_

#include "BotPlaner.h"

#include <deque>
#include <unordered_map>
#include <vector>

class CRoute;
class CRentRoute;
class PLAYER;

extern const bool kAlwaysReplan;
extern const float kSchedulingMinScoreRatio;
extern const float kSchedulingMinScoreRatioLastMinute;
extern const SLONG kSwitchToRoutesNumPlanesMin;
extern const SLONG kSwitchToRoutesNumPlanesMax;
extern const SLONG kSmallestAdCampaign;
extern const SLONG kMinimumImage;
extern const SLONG kMaximumRouteUtilizationValue;
inline SLONG kMaximumRouteUtilization() {
    /* we only need to hold the route, move on to the next one fast! */
    return (Sim.Difficulty == DIFF_NORMAL) ? 20 : kMaximumRouteUtilizationValue;
}
extern const SLONG kMaximumPlaneUtilization;
extern const DOUBLE kMaxTicketPriceFactor;
extern const SLONG kTargetEmployeeHappiness;
extern const SLONG kMinimumEmployeeSkill;
extern const SLONG kPlaneMinimumZustand;
extern const SLONG kPlaneTargetZustand;
extern const SLONG kUsedPlaneMinimumScore;
extern const DOUBLE kMaxKerosinQualiZiel;
extern const SLONG kStockEmissionMode;
extern const bool kReduceDividend;
extern const SLONG kMaxSabotageHints;

extern const SLONG kMoneyEmergencyFund;
extern const SLONG kMoneyReserveRepairs;
extern const SLONG kMoneyReservePlaneUpgrades;
extern const SLONG kMoneyReserveBuyTanks;
extern const SLONG kMoneyReserveIncreaseDividend;
extern const SLONG kMoneyReservePaybackCredit;
extern const SLONG kMoneyReserveBuyOwnShares;
extern const SLONG kMoneyReserveBuyNemesisShares;
extern const SLONG kMoneyReserveBossOffice;
extern const SLONG kMoneyReserveExpandAirport;

extern SLONG kPlaneScoreMode;
extern SLONG kPlaneScoreForceBest;
extern SLONG kTestMode;

class Bot {
  public:
    Bot(PLAYER &player);

    void RobotInit();
    void RobotPlan();
    void RobotExecuteAction();

    void setNoticedSickness() { mIsSickToday = true; }

    // private:
    enum class Prio {
        None,   /* conditions not met: Forbidden to perform this action! */
        Lowest, /* unimportant actions (mostly wasting time at bar, telescope, shops, ...) */
        Low,    /* not completely unimportant (for example collect items) */
        Medium, /* most main tasks that do not need higher priority */
        High,   /* important to do soon (grab flights, important investments) */
        Higher, /* important to do very soon (grab flights from travel agency) */
        Top     /* survial at stake */
    };
    enum class AreWeBroke {
        No,
        Somewhat, /* we can use some more, but don't generate cost (loan interest) or problems (sell all stock)*/
        Yes,      /* we need money, but do not go scorched earth (e.g. sell stock, but not that enemy can take over) */
        Desperate /* sell everything (TODO: Not fully implemented) */
    };
    enum class HowToGetMoney {
        None,
        SellShares,       /* sell shares from other airlines */
        SellOwnShares,    /* sell own shares if > 50% */
        SellAllOwnShares, /* sell all own shares */
        IncreaseCredit,
        EmitShares
    };
    enum class FinalPhase {
        No,
        SaveMoney, /* stop investing towards other targets */
        TargetRun  /* invest towards target, liquidize all other assets */
    };
    struct RouteInfo {
        RouteInfo() = default;
        RouteInfo(SLONG id, SLONG id2, SLONG typeId) : routeId(id), routeReverseId(id2), planeTypeId(typeId) {}
        SLONG routeId{-1};
        SLONG routeReverseId{-1};
        SLONG planeTypeId{-1};
        SLONG routeUtilization{};
        SLONG image{};
        SLONG planeUtilization{};
        SLONG planeUtilizationFC{};
        DOUBLE ticketCostFactor{2}; /* 0.5: discount / 1: flight cost / 2: normal price / 4: deluxe price */
        std::vector<SLONG> planeIds;
    };

    /* in BotConditions.cpp */
    bool isOfficeUsable() const;
    bool hoursPassed(SLONG room, SLONG hours) const;
    void grabNewFlights();
    bool haveDiscount() const;
    enum class HowToPlan { None, Laptop, Office };
    HowToPlan canWePlanFlights();
    __int64 getMoneyAvailable() const;
    AreWeBroke areWeBroke() const;
    HowToGetMoney howToGetMoney();
    __int64 howMuchMoneyCanWeGet(bool extremMeasures);
    bool canWeCallInternational();
    Prio condAll(SLONG actionId);
    Prio condStartDay();
    Prio condStartDayLaptop();
    Prio condBuero();
    Prio condCallInternational();
    Prio condCallInternationalHandy();
    Prio condCheckLastMinute();
    Prio condCheckTravelAgency();
    Prio condCheckFreight();
    Prio condUpgradePlanes();
    Prio condBuyNewPlane(__int64 &moneyAvailable);
    Prio condBuyUsedPlane(__int64 &moneyAvailable);
    Prio condVisitMuseum();
    Prio condVisitHR();
    Prio condBuyKerosine(__int64 &moneyAvailable);
    Prio condBuyKerosineTank(__int64 &moneyAvailable);
    Prio condSabotage(__int64 &moneyAvailable);
    Prio condIncreaseDividend(__int64 &moneyAvailable);
    Prio condTakeOutLoan();
    Prio condDropMoney(__int64 &moneyAvailable);
    Prio condEmitShares();
    Prio condSellShares(__int64 &moneyAvailable);
    Prio condBuyShares(__int64 &moneyAvailable, SLONG dislike);
    Prio condBuyOwnShares(__int64 &moneyAvailable);
    Prio condBuyNemesisShares(__int64 &moneyAvailable, SLONG dislike);
    Prio condVisitMech();
    Prio condVisitNasa(__int64 &moneyAvailable);
    Prio condVisitMisc();
    Prio condVisitMakler();
    Prio condVisitArab();
    Prio condVisitRick();
    Prio condVisitDutyFree(__int64 &moneyAvailable);
    Prio condVisitBoss(__int64 &moneyAvailable);
    Prio condExpandAirport(__int64 &moneyAvailable);
    Prio condVisitRouteBoxPlanning();
    Prio condVisitRouteBoxRenting(__int64 &moneyAvailable);
    Prio condVisitSecurity(__int64 &moneyAvailable);
    Prio condSabotageSecurity();
    Prio condVisitDesigner(__int64 &moneyAvailable);
    Prio condBuyAdsForRoutes(__int64 &moneyAvailable);
    Prio condBuyAds(__int64 &moneyAvailable);
    Prio condVisitAds();

    /* in BotActions.cpp */
    void actionStartDay(__int64 moneyAvailable);
    void actionStartDayLaptop(__int64 moneyAvailable);
    void actionBuero();
    void actionCallInternational(bool areWeInOffice);
    void actionCheckLastMinute();
    void actionCheckTravelAgency();
    void actionCheckFreightDepot();
    void actionUpgradePlanes();
    void actionBuyNewPlane(__int64 moneyAvailable);
    void actionBuyUsedPlane(__int64 moneyAvailable);
    void actionVisitHR();
    void actionBuyKerosine(__int64 moneyAvailable);
    void actionBuyKerosineTank(__int64 moneyAvailable);
    void actionSabotage(__int64 moneyAvailable);
    SLONG calcBuyShares(__int64 moneyAvailable, DOUBLE kurs) const;
    SLONG calcSellShares(__int64 moneyToGet, DOUBLE kurs) const;
    SLONG calcNumOfFreeShares(SLONG playerId) const;
    void actionEmitShares();
    void actionBuyShares(__int64 moneyAvailable);
    void actionSellShares(__int64 moneyAvailable);
    void actionVisitMech();
    void actionVisitDutyFree(__int64 moneyAvailable);
    void actionVisitBoss();
    void actionFindBestRoute();
    void actionRentRoute();
    void actionBuyAdsForRoutes(__int64 moneyAvailable);
    void actionBuyAds(__int64 moneyAvailable);

    /* in BotFunctions.cpp */
    void determineNemesis();
    void switchToFinalTarget();
    std::vector<SLONG> findBestAvailablePlaneType(bool forRoutes) const;
    SLONG findBestAvailableUsedPlane() const;
    SLONG findBestDesignerPlane() const;
    void grabFlights(BotPlaner &planer, bool areWeInOffice);
    void requestPlanFlights(bool areWeInOffice);
    void planFlights();
    std::pair<SLONG, SLONG> kerosineQualiOptimization(__int64 moneyAvailable, DOUBLE targetFillRatio) const;
    /* routes */
    SLONG getRouteTurnAroundDuration(const CRoute &qRoute, SLONG planeTypeId) const;
    void checkLostRoutes();
    void updateRouteInfo();
    void requestPlanRoutes(bool areWeInOffice);
    void planRoutes();

    /* misc (in Bot.cpp) */
    SLONG numPlanes() const { return mPlanesForJobs.size() + mPlanesForJobsUnassigned.size() + mPlanesForRoutes.size() + mPlanesForRoutesUnassigned.size(); }
    std::vector<SLONG> getAllPlanes() const {
        std::vector<SLONG> planes = mPlanesForRoutes;
        for (auto &i : mPlanesForRoutesUnassigned) {
            planes.push_back(i);
        }
        for (auto &i : mPlanesForJobs) {
            planes.push_back(i);
        }
        for (auto &i : mPlanesForJobsUnassigned) {
            planes.push_back(i);
        }
        return planes;
    }
    SLONG calcCurrentGainFromJobs() const;
    bool checkPlaneLists();
    bool findPlanesNotAvailableForService(std::vector<SLONG> &listAvailable, std::deque<SLONG> &listUnassigned);
    bool findPlanesAvailableForService(std::deque<SLONG> &listUnassigned, std::vector<SLONG> &listAvailable);
    bool checkPlaneAvailable(SLONG planeId, bool printIfAvailable) const;
    const CRentRoute &getRentRoute(const RouteInfo &routeInfo) const;
    const CRoute &getRoute(const RouteInfo &routeInfo) const;
    __int64 getDailyOpSaldo() const;
    __int64 getWeeklyOpSaldo() const;
    void forceReplanning();

    /* anim state */
    bool getOnThePhone() const { return mOnThePhone > 0; }
    void decOnThePhone() { mOnThePhone--; }

    TEAKRAND LocalRandom;
    PLAYER &qPlayer;

    /* action economy */
    std::unordered_map<SLONG, SLONG> mLastTimeInRoom;
    SLONG mNumActionsToday{0};

    /* planes used for what? */
    std::vector<SLONG> mPlanesForJobs;
    std::deque<SLONG> mPlanesForJobsUnassigned;
    std::vector<SLONG> mPlanesForRoutes;
    std::deque<SLONG> mPlanesForRoutesUnassigned;

    /* strategy state */
    bool mLongTermStrategy{true};
    SLONG mBestPlaneTypeId{-1};
    SLONG mBestUsedPlaneIdx{-1};
    SLONG mBuyPlaneForRouteId{-1};
    SLONG mUsePlaneForRouteId{-1};
    SLONG mWantToRentRouteId{-1};
    bool mFirstRun{true};
    bool mDayStarted{false};
    bool mDoRoutes{false};
    FinalPhase mRunToFinalObjective{FinalPhase::No};
    __int64 mMoneyForFinalObjective{0};
    bool mOutOfGates{false};
    bool mNeedToPlanJobs{false};
    bool mNeedToPlanRoutes{false};
    __int64 mMoneyReservedForRepairs{0};
    __int64 mMoneyReservedForUpgrades{0};
    __int64 mMoneyReservedForAuctions{0};
    SLONG mNemesis{-1};
    __int64 mNemesisScore{0};

    /* status boss office */
    SLONG mBossNumCitiesAvailable{-1};
    bool mBossGateAvailable{false};

    /* detect tanks being too small */
    DOUBLE mTankRatioEmptiedYesterday{0};
    SLONG mKerosineUsedTodaySoFar{0};
    SLONG mKerosineLevelLastChecked{0};

    /* routes */
    std::vector<RouteInfo> mRoutes;
    std::vector<SLONG> mRoutesSortedByUtilization;
    std::vector<SLONG> mRoutesSortedByImage;

    /* crew */
    SLONG mNumEmployees{0};

    /* items */
    SLONG mItemPills{0};      /* 1: card taken, 2: card given */
    SLONG mItemAntiVirus{0};  /* 1: spider taken, 2: spider given, 3: darts taken, 4: darts given */
    SLONG mItemAntiStrike{0}; /* 1: BH taken, 2: BH given, 3: horseshoe taken, 4: horseshoe given */
    SLONG mItemArabTrust{0};  /* 1: MG bought, 2: MG given */
    bool mIsSickToday{false};

    /* current solution (not planned yet) */
    BotPlaner::SolutionList mPlanerSolution;

    /* anim state */
    int mOnThePhone{0};
};

TEAKFILE &operator<<(TEAKFILE &File, const PlaneTime &planeTime);
TEAKFILE &operator>>(TEAKFILE &File, PlaneTime &planeTime);

TEAKFILE &operator<<(TEAKFILE &File, const Bot &bot);
TEAKFILE &operator>>(TEAKFILE &File, Bot &bot);

#endif // BOT_H_
