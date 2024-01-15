#ifndef BOT_H_
#define BOT_H_

class CPlaneType;
class PLAYER;

typedef long long __int64;

class Bot {
  public:
    Bot(PLAYER &player);

    void RobotInit();
    void RobotPlan();
    void RobotExecuteAction();

    // private:
    enum class Prio { None, Low, Medium, High, Top };
    bool hoursPassed(SLONG room, SLONG hours);
    bool checkRoomOpen(SLONG roomId);

    Prio condAll(SLONG actionId, __int64 moneyAvailable, SLONG dislike, SLONG bestPlaneTypeId);
    Prio condStartDay();
    Prio condCallInternational();
    Prio condCheckLastMinute();
    Prio condCheckTravelAgency();
    Prio condCheckFreight();
    Prio condUpgradePlanes(__int64 &moneyAvailable);
    Prio condBuyNewPlane(__int64 &moneyAvailable, SLONG bestPlaneTypeId);
    Prio condVisitHR();
    Prio condBuyKerosine(__int64 &moneyAvailable);
    Prio condBuyKerosineTank(__int64 &moneyAvailable);
    Prio condSabotage(__int64 &moneyAvailable);
    Prio condIncreaseDividend(__int64 &moneyAvailable);
    Prio condRaiseMoney(__int64 &moneyAvailable);
    Prio condDropMoney(__int64 &moneyAvailable);
    Prio condEmitShares(__int64 &moneyAvailable);
    Prio condSellShares(__int64 &moneyAvailable);
    Prio condBuyOwnShares(__int64 &moneyAvailable);
    Prio condBuyNemesisShares(__int64 &moneyAvailable, SLONG dislike);
    Prio condVisitMech(__int64 &moneyAvailable);
    Prio condVisitNasa(__int64 &moneyAvailable);
    Prio condVisitMisc();
    Prio condBuyUsedPlane(__int64 &moneyAvailable);
    Prio condVisitDutyFree(__int64 &moneyAvailable);
    Prio condVisitBoss(__int64 &moneyAvailable);
    Prio condVisitRouteBox(__int64 &moneyAvailable);
    Prio condVisitSecurity(__int64 &moneyAvailable);
    Prio condSabotageSecurity();
    Prio condVisitDesigner(__int64 &moneyAvailable);
    Prio condBuyAdsForRoutes(__int64 &moneyAvailable);
    Prio condBuyAds(__int64 &moneyAvailable);

    std::pair<SLONG, SLONG> kerosineQualiOptimization(__int64 moneyAvailable, DOUBLE targetFillRatio);
    SLONG findBestAvailablePlaneType();
    SLONG calcNumberOfShares(__int64 moneyAvailable, DOUBLE kurs);
    SLONG calcNumOfFreeShares(SLONG playerId);
    SLONG checkFlightJobs();

    PLAYER &qPlayer;

    /* to not keep doing the same thing */
    std::unordered_map<SLONG, SLONG> mLastTimeInRoom;

    /* planes used for what? */
    std::vector<SLONG> mPlanesForJobs;
    std::vector<SLONG> mPlanesForRoutes;

    /* strategy state */
    SLONG mDislike{-1};
    SLONG mBestPlaneTypeId{-1};
    bool mFirstRun{true};
    bool mDoRoutes{false};
    bool mWantToBuyNewRoute{false};
    bool mSavingForPlane{false};
    bool mOutOfGates{false};

    /* visit boss office */
    SLONG mBossNumCitiesAvailable{-1};
    bool mBossGateAvailable{false};

    /* detect tanks being too small */
    DOUBLE mTanksFilledYesterday{0.0};
    DOUBLE mTanksFilledToday{0.0};
    bool mTankWasEmpty{false};

    /* jobs*/
    SLONG mCurrentGain{0};

    /* routes */
    struct RouteInfo {
        RouteInfo() = default;
        RouteInfo(SLONG id, SLONG util) : routeId(id), utilization(util) {}
        SLONG routeId{-1};
        SLONG utilization{0};
    };
    std::vector<RouteInfo> mRoutesSortedByUtilization;
};

TEAKFILE &operator<<(TEAKFILE &File, const Bot &bot);
TEAKFILE &operator>>(TEAKFILE &File, Bot &bot);

#endif // BOT_H_