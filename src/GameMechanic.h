extern SLONG TankSize[4];
extern SLONG TankPrice[4];

extern SLONG SabotagePrice[5];
extern SLONG SabotagePrice2[4];
extern SLONG SabotagePrice3[6];

extern SLONG RocketPrices[10];

extern SLONG StationPrices[10];

class GameMechanic {
  public:
    /* Kerosin */
    static bool buyKerosinTank(PLAYER &qPlayer, SLONG typ, SLONG anzahl);
    static bool toggleKerosinTank(PLAYER &qPlayer);
    static bool buyKerosin(PLAYER &qPlayer, SLONG preis, SLONG menge);
    static std::pair<__int64, __int64> calcKerosinPrice(PLAYER &qPlayer, __int64 typ, __int64 menge);

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

    /* Kredite */
    static bool takeOutCredit(PLAYER &qPlayer, SLONG target);
    static bool payBackCredit(PLAYER &qPlayer, SLONG target);

    /* Flugzeuge */
    static void setPlaneTargetZustand(PLAYER &qPlayer, SLONG idx, SLONG zustand);

    /* Security */
    static bool toggleSecurity(PLAYER &qPlayer, SLONG securityType);

    /* Designer */
    static bool buyXPlane(PLAYER &qPlayer, const CString &filename, SLONG amount);

    /* Bank */
    enum class OvertakeAirlineResult { Ok, DeniedInvalidParam, DeniedYourAirline, DeniedAlreadyGone, DeniedNoStock, DeniedNotEnoughStock, DeniedEnemyStock };
    static OvertakeAirlineResult canOvertakeAirline(PLAYER &qPlayer, SLONG targetAirline);
    static bool overtakeAirline(PLAYER &qPlayer, SLONG targetAirline, bool liquidate);

    enum class EmitStockResult { Ok, DeniedTooMuch, DeniedValueTooLow };
    static EmitStockResult canEmitStock(PLAYER &qPlayer);
    static bool emitStock(PLAYER &qPlayer, SLONG neueAktien, SLONG mode);
};