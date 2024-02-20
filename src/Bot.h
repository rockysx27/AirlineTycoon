

#ifndef BOT_H_
#define BOT_H_

#include <deque>
#include <unordered_map>
#include <vector>

class BotPlaner;
class CPlaneType;
class CRoute;
class CRentRoute;
class PLAYER;

typedef long long __int64;

extern const SLONG kMoneyEmergencyFund;
extern const SLONG kSmallestAdCampaign;
extern const SLONG kMaximumRouteUtilization;
extern const SLONG kMaximumPlaneUtilization;
extern const DOUBLE kMaxTicketPriceFactor;
extern const SLONG kTargetEmployeeHappiness;

class Bot {
  public:
    Bot(PLAYER &player);

    void RobotInit();
    void RobotPlan();
    void RobotExecuteAction();

    void setNoticedSickness() { mIsSickToday = true; }

    // private:
    enum class Prio { None, Low, Medium, High, Top };
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
    __int64 getMoneyAvailable() const;
    bool hoursPassed(SLONG room, SLONG hours);
    bool haveDiscount() const;
    enum class HowToPlan { None, Laptop, Office };
    HowToPlan canWePlanFlights();
    Prio condAll(SLONG actionId);
    Prio condStartDay();
    Prio condBuero();
    Prio condCallInternational();
    Prio condCheckLastMinute();
    Prio condCheckTravelAgency();
    Prio condCheckFreight();
    Prio condUpgradePlanes(__int64 &moneyAvailable);
    Prio condBuyNewPlane(__int64 &moneyAvailable);
    Prio condVisitHR();
    Prio condBuyKerosine(__int64 &moneyAvailable);
    Prio condBuyKerosineTank(__int64 &moneyAvailable);
    Prio condSabotage(__int64 &moneyAvailable);
    Prio condIncreaseDividend(__int64 &moneyAvailable);
    Prio condRaiseMoney(__int64 &moneyAvailable);
    Prio condDropMoney(__int64 &moneyAvailable);
    Prio condEmitShares();
    Prio condSellShares(__int64 &moneyAvailable);
    Prio condBuyShares(__int64 &moneyAvailable, SLONG dislike);
    Prio condBuyOwnShares(__int64 &moneyAvailable);
    Prio condBuyNemesisShares(__int64 &moneyAvailable, SLONG dislike);
    Prio condVisitMech(__int64 &moneyAvailable);
    Prio condVisitNasa(__int64 &moneyAvailable);
    Prio condVisitMisc();
    Prio condVisitMakler();
    Prio condVisitRick();
    Prio condBuyUsedPlane(__int64 &moneyAvailable);
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

    /* in BotActions.cpp */
    void actionStartDay(__int64 moneyAvailable);
    void actionBuero();
    void actionCallInternational();
    void actionCheckLastMinute();
    void actionCheckTravelAgency();
    void grabFlights(BotPlaner &planer);
    void requestPlanFlights();
    void planFlights();
    void actionUpgradePlanes(__int64 moneyAvailable);
    void actionBuyNewPlane(__int64 moneyAvailable);
    void actionVisitHR();
    std::pair<SLONG, SLONG> kerosineQualiOptimization(__int64 moneyAvailable, DOUBLE targetFillRatio) const;
    void actionBuyKerosine(__int64 moneyAvailable);
    void actionBuyKerosineTank(__int64 moneyAvailable);
    SLONG calcNumberOfShares(__int64 moneyAvailable, DOUBLE kurs) const;
    SLONG calcNumOfFreeShares(SLONG playerId) const;
    void actionEmitShares();
    void actionBuyShares(__int64 moneyAvailable);
    void actionVisitDutyFree(__int64 moneyAvailable);
    void actionVisitBoss(__int64 moneyAvailable);
    std::pair<SLONG, SLONG> actionFindBestRoute(TEAKRAND &rnd) const;
    void actionRentRoute(SLONG routeA, SLONG planeTypeId);
    void actionBuyAdsForRoutes(__int64 moneyAvailable);
    void actionBuyAds(__int64 moneyAvailable);

    /* routes */
    SLONG getRouteTurnAroundDuration(const CRoute &qRoute, SLONG planeTypeId) const;
    void checkLostRoutes();
    void updateRouteInfo();
    void requestPlanRoutes();
    void planRoutes();

    /* misc */
    SLONG numPlanes() const { return mPlanesForJobs.size() + mPlanesForRoutes.size() + mPlanesForRoutesUnassigned.size(); }
    std::vector<SLONG> findBestAvailablePlaneType() const;
    SLONG calcCurrentGainFromJobs() const;
    bool findPlanesWithoutCrew(std::vector<SLONG> &listAvailable, std::deque<SLONG> &listUnassigned);
    bool findPlanesWithCrew(std::deque<SLONG> &listUnassigned, std::vector<SLONG> &listAvailable);
    bool checkPlaneAvailable(SLONG planeId, bool printIfAvailable) const;
    const CRentRoute &getRentRoute(const RouteInfo &routeInfo) const;
    const CRoute &getRoute(const RouteInfo &routeInfo) const;
    SLONG getDailyOpSaldo() const;
    SLONG getWeeklyOpSaldo() const;

    TEAKRAND LocalRandom;
    PLAYER &qPlayer;

    /* to not keep doing the same thing */
    std::unordered_map<SLONG, SLONG> mLastTimeInRoom;

    /* planes used for what? */
    std::vector<SLONG> mPlanesForJobs;
    std::deque<SLONG> mPlanesForJobsUnassigned;
    std::vector<SLONG> mPlanesForRoutes;
    std::deque<SLONG> mPlanesForRoutesUnassigned;

    /* strategy state */
    SLONG mDislike{-1};
    SLONG mBestPlaneTypeId{-1};
    SLONG mBuyPlaneForRouteId{-1};
    SLONG mWantToRentRouteId{-1};
    bool mFirstRun{true};
    bool mDoRoutes{false};
    bool mOutOfGates{false};
    bool mNeedToPlanJobs{false};
    bool mNeedToPlanRoutes{false};

    /* visit boss office */
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
};

TEAKFILE &operator<<(TEAKFILE &File, const Bot &bot);
TEAKFILE &operator>>(TEAKFILE &File, Bot &bot);

#endif // BOT_H_
