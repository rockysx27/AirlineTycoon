extern SLONG TankSize[4];
extern SLONG TankPrice[4];

extern SLONG SabotagePrice[5];
extern SLONG SabotagePrice2[4];
extern SLONG SabotagePrice3[6];

extern SLONG RocketPrices[10];

extern SLONG StationPrices[10];

class GameMechanic {
  public:
    static void bankruptPlayer(PLAYER &qPlayer);

    /* Kerosin */
    static bool buyKerosinTank(PLAYER &qPlayer, SLONG type, SLONG amount);
    static bool toggleKerosinTank(PLAYER &qPlayer);
    static bool buyKerosin(PLAYER &qPlayer, SLONG type, SLONG amount);
    struct KerosinTransaction {
        __int64 Kosten{};
        __int64 Rabatt{};
        __int64 Menge{};
    };
    static KerosinTransaction calcKerosinPrice(PLAYER &qPlayer, __int64 typ, __int64 menge);

    /* Sabotage */
    static SLONG setSaboteurTarget(PLAYER &qPlayer, SLONG target);
    static bool checkSaboteurBusy(PLAYER &qPlayer) { return (qPlayer.ArabMode != 0) || (qPlayer.ArabMode2 != 0) || (qPlayer.ArabMode3 != 0); }
    struct CheckSabotage {
        bool OK;
        int dialogID;
        CString dialogParam{};
    };
    static CheckSabotage checkPrerequisitesForSaboteurJob(PLAYER &qPlayer, SLONG type, SLONG number);
    static bool activateSaboteurJob(PLAYER &qPlayer);
    static void paySaboteurFine(SLONG player, SLONG opfer);

    /* Kredite */
    static bool takeOutCredit(PLAYER &qPlayer, SLONG target);
    static bool payBackCredit(PLAYER &qPlayer, SLONG target);

    /* Flugzeuge */
    static void setPlaneTargetZustand(PLAYER &qPlayer, SLONG idx, SLONG zustand);

    /* Security */
    static bool toggleSecurity(PLAYER &qPlayer, SLONG securityType);

    /* Makler */
    static bool buyPlane(PLAYER &qPlayer, SLONG planeType, SLONG amount);

    /* Museum */
    static bool buyUsedPlane(PLAYER &qPlayer, SLONG planeID);
    static bool sellPlane(PLAYER &qPlayer, SLONG planeID);

    /* Designer */
    static bool buyXPlane(PLAYER &qPlayer, const CString &filename, SLONG amount);

    /* Bank */
    enum class OvertakeAirlineResult { Ok, DeniedInvalidParam, DeniedYourAirline, DeniedAlreadyGone, DeniedNoStock, DeniedNotEnoughStock, DeniedEnemyStock };
    static OvertakeAirlineResult canOvertakeAirline(PLAYER &qPlayer, SLONG targetAirline);
    static bool overtakeAirline(PLAYER &qPlayer, SLONG targetAirline, bool liquidate);

    enum class EmitStockResult { Ok, DeniedTooMuch, DeniedValueTooLow };
    static EmitStockResult canEmitStock(PLAYER &qPlayer);
    static bool emitStock(PLAYER &qPlayer, SLONG neueAktien, SLONG mode);

    /* Boss */
    enum class ExpandAirportResult { Ok, DeniedFreeGates, DeniedLimitReached, DeniedTooEarly, DeniedAlreadyExpanded, DeniedExpandingRightNow };
    static ExpandAirportResult canExpandAirport(PLAYER &qPlayer);
    static bool expandAirport(PLAYER &qPlayer);

    /* Mechanic */
    static SLONG setMechMode(PLAYER &qPlayer, SLONG mode);

    /* HR */
    static void increaseAllSalaries(PLAYER &qPlayer);
    static void decreaseAllSalaries(PLAYER &qPlayer);
    enum class EndStrikeMode { Salary, Threat, Drunk };
    static void endStrike(PLAYER &qPlayer, EndStrikeMode mode);

    /* Ads */
    static bool buyAdvertisement(PLAYER &qPlayer, SLONG adCampaignType, SLONG adCampaignSize, SLONG routeID = -1);

    /* Flights */
    static bool takeFlightJob(PLAYER &qPlayer, SLONG par1, SLONG par2, SLONG &outObjectId);
    static bool takeFreightJob(PLAYER &qPlayer, SLONG par1, SLONG par2, SLONG &outObjectId);
    static bool refillFlightJobs(SLONG cityNum);

    /* Crew */
    static bool hireWorker(PLAYER &qPlayer, CWorker &qWorker);
    static bool fireWorker(PLAYER &qPlayer, CWorker &qWorker);

    /* Execution routines */
    static void executeAirlineOvertake();
    static void executeSabotage();
};