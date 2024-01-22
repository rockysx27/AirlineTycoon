#ifndef COMPAT_H_
#define COMPAT_H_

#include "compat_album.h"
#include "compat_types.h"

#include <vector>

// Die Schwierigkeitsgrade fürs Spiel:
#define DIFF_TUTORIAL 0 // Tutorium: 3 Aufträge
#define DIFF_FIRST 1    // 5000 Passagiere befördern; keine
#define DIFF_EASY 2
#define DIFF_NORMAL 3
#define DIFF_HARD 4
#define DIFF_FINAL 5 // Rakete zusammenbauen

#define DIFF_FREEGAME -1
#define DIFF_FREEGAMEMAP 7

#define DIFF_ADDON 10   // Keine Mission, sondern nur ein Vergleichsmarker
#define DIFF_ADDON01 11 // Fracht: x Tonnen
#define DIFF_ADDON02 12 // Sanierung einer bankrotten Fluglinie
#define DIFF_ADDON03 13 // Fracht: Hilfsflüge x Tonnen
#define DIFF_ADDON04 14 // Flugkilometer
#define DIFF_ADDON05 15 // Kein Reisebüro
#define DIFF_ADDON06 16 // Alles modernisieren
#define DIFF_ADDON07 17 // Aktienkurs
#define DIFF_ADDON08 18 // Viele Aufträge
#define DIFF_ADDON09 19 // Service & Luxus
#define DIFF_ADDON10 20 // Weltraumstation

#define DIFF_ATFS 40   // Keine Mission, sondern nur ein Vergleichsmarker
#define DIFF_ATFS01 41 // Back again
#define DIFF_ATFS02 42 // Sicher ist sicher
#define DIFF_ATFS03 43 // Schnell und viel
#define DIFF_ATFS04 44 // Safety first
#define DIFF_ATFS05 45 // Schnell und viel mehr
#define DIFF_ATFS06 46 // Krisenmanagement
#define DIFF_ATFS07 47 // Schwarzer Freitag
#define DIFF_ATFS08 48 // Das 3 Liter Flugzeug
#define DIFF_ATFS09 49 // Die Kerosin Krise
#define DIFF_ATFS10 50 // Der Tycoon

// Dinge für den Roboter (Computerspieler):
#define ACTION_NONE 0   // Leereintrag
#define ACTION_WAIT 100 // Warten - kein Leereintrag!
#define ACTION_RAISEMONEY 200
#define ACTION_DROPMONEY 201
#define ACTION_VISITBANK 202
#define ACTION_EMITSHARES 203
#define ACTION_CHECKAGENT1 210 // Last-Minute
#define ACTION_CHECKAGENT2 211 // Reisebüro
#define ACTION_CHECKAGENT3 212 // Frachtraum
#define ACTION_STARTDAY 221    // Geht ins Büro
#define ACTION_BUERO 221       // Geht ins Büro
#define ACTION_PERSONAL 222    // Geht ins Personalbüro
#define ACTION_VISITARAB 230
#define ACTION_VISITKIOSK 231
#define ACTION_VISITMECH 232
#define ACTION_VISITMUSEUM 233
#define ACTION_VISITDUTYFREE 234
#define ACTION_VISITAUFSICHT 235
#define ACTION_VISITNASA 236
#define ACTION_VISITTELESCOPE 237
#define ACTION_VISITMAKLER 238
#define ACTION_VISITRICK 239
#define ACTION_VISITROUTEBOX 240
#define ACTION_VISITSECURITY 241
#define ACTION_VISITDESIGNER 242
#define ACTION_VISITSECURITY2 243

#define ACTION_BUYUSEDPLANE 300
#define ACTION_BUYNEWPLANE 301
#define ACTION_WERBUNG 400
#define ACTION_SABOTAGE 500

#define ACTION_UPGRADE_PLANES 600
#define ACTION_BUY_KEROSIN 601
#define ACTION_BUY_KEROSIN_TANKS 602
#define ACTION_SET_DIVIDEND 603
#define ACTION_BUYSHARES 604
#define ACTION_SELLSHARES 605
#define ACTION_WERBUNG_ROUTES 606
#define ACTION_CALL_INTERNATIONAL 607

// Öfnungszeiten:
SLONG timeDutyOpen = 10 * 60000;
SLONG timeDutyClose = 16 * 60000; // Nur Sa, So
SLONG timeArabOpen = 10 * 60000;
SLONG timeLastClose = 16 * 60000;
SLONG timeMuseOpen = 11 * 60000;
SLONG timeReisClose = 17 * 60000;
SLONG timeMaklClose = 16 * 60000;
SLONG timeWerbOpen = 12 * 60000;

void HercPrintf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    printf(format, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}
#define hprintf HercPrintf

void HercPrintfRed(const char *format, ...) {
    printf("\e[1;31m");
    va_list args;
    va_start(args, format);
    printf(format, args);
    va_end(args);
    printf("\e[m\n");
    fflush(stdout);
}
#define redprintf HercPrintfRed

void HercPrintfGreen(const char *format, ...) {
    printf("\e[1;32m");
    va_list args;
    va_start(args, format);
    printf(format, args);
    va_end(args);
    printf("\e[m\n");
    fflush(stdout);
}
#define greenprintf HercPrintfGreen

#define EINH_KM 0
static std::array<CEinheit, 1> Einheiten;

#define TOKEN_SCHED 0
static const RES StandardTexte{};

struct SIM {
    SLONG Date{};                    // Tage seit Spielbeginn
    ULONG Time{};                    // Die Simulationszeit
    SLONG StartWeekday{};            // An diesem Wochentag haben wir das Spiel begonnen
    SLONG Weekday{};                 // 0-6 für Mo-So
    SLONG Kerosin{500};              // Preis pro Liter
    SLONG nSecOutDays{};             // 0 oder n=So viele Tage fällt das Security Office noch aus
    SBYTE Difficulty{DIFF_FREEGAME}; // Schwierigkeitsgrad
};
static const SIM Sim{};

class CITY {
  public:
    CITY(CString n, CString k) : Name(n), Kuerzel(k) {}
    CString Name;    // z.B. "Rio de Janeiro"
    CString Kuerzel; // aktuelles kuerzel
};
class CITIES : public std::vector<CITY> {
  public:
    CITIES() {
        for (SLONG i = 0; i < 100; i++) {
            emplace_back(CString(bprintf("City_%ld", i)), CString(bprintf("C%ld", i)));
        }
    }

    SLONG CalcDistance(SLONG CityId1, SLONG CityId2) { return 200 * std::abs(CityId1 - CityId2); }
};
static CITIES Cities{};

class CAuftrag {
  public:
    ULONG VonCity{};     // bezeichnet eine Stadt
    ULONG NachCity{};    // bezeichnet eine Stadt
    ULONG Personen{};    // So viele müssen in die Maschine passen
    UWORD Date{};        // Vertragsbeginn
    UWORD BisDate{};     // Vertragsende
    SBYTE InPlan{};      // 0=Nix, -1=Durchgeführt, 1=1x im Plan
    SBYTE Okay{};        // 0=Nix, -1=Durchgeführt, 1=1x im Plan
    SLONG Praemie{};     // Prämie bei Erfüllung
    SLONG Strafe{};      // Strafe bei Versagen
    BOOL bUhrigFlight{}; // Von Uhrig in Auftrag gegeben?
};
class CAuftraege : public ALBUM_V<CAuftrag> {
  public:
    CAuftraege() : ALBUM_V<CAuftrag>("Auftraege") {}
};

class CFracht {
  public:
    ULONG VonCity{};  // bezeichnet eine Stadt
    ULONG NachCity{}; // bezeichnet eine Stadt
    SLONG Tons{};     // So viele Tonnen müssen insgesamt geflogen werden
    SLONG TonsOpen{}; // So viele Tonnen sind weder geflogen, noch im Flugplan verplant
    SLONG TonsLeft{}; // So viele Tonnen müssen noch geflogen werden
    UWORD Date{};     // Vertragsbeginn
    UWORD BisDate{};  // Vertragsende
    SBYTE InPlan{};   // 0=Nix, -1=Durchgeführt, 1=1x im Plan
    SBYTE Okay{};     // 0=Nix, -1=Durchgeführt, 1=1x im Plan
    SLONG Praemie{};  // Prämie bei Erfüllung
    SLONG Strafe{};   // Strafe bei Versagen
};
class CFrachten : public ALBUM_V<CFracht> {
  public:
    CFrachten() : ALBUM_V<CFracht>("Fracht") {}
};

class CFlugplanEintrag {
  public:
    SLONG GetEinnahmen(SLONG PlayerNum, const CPlane &qPlane) const {
        switch (ObjectType) {
        // Route:
        case 1:
            return (Ticketpreis * Passagiere + TicketpreisFC * PassagiereFC);
            break;

            // Auftrag:
        case 2:
            return (Sim.Players.Players[PlayerNum].Auftraege[ObjectId].Praemie);
            break;

            // Leerflug:
        case 3:
            return (qPlane.ptPassagiere * Cities.CalcDistance(VonCity, NachCity) / 1000 / 40);
            break;

            // Frachtauftrag:
        case 4:
            return (Sim.Players.Players[PlayerNum].Frachten[ObjectId].Praemie);
            break;

        default: // Eigentlich unmöglich:
            return (0);
        }

        return (0);
    }

    UBYTE Okay{};             // 0=Auftrag Okay 1=falscher Tag, 2=schon durchgeführt, 3=Passagiere passen nicht
    UBYTE HoursBefore{};      // So viele Stunden vor dem Start wurde der Flug festgelegt
    UWORD Passagiere{};       // Zahl der belegten Sitzplätze (Normal)
    UWORD PassagiereFC{};     // Zahl der belegten Sitzplätze (in der ersten Klasse)
    UWORD PArrived{};         // Zahl Passagiere, die schon im Flughafen sind
    SLONG Gate{-1};           //-1 = kein Gate frei ==> Flugfeld; -2=externer Hafen
    UBYTE GateWarning{FALSE}; // Warnung, daß ein anderer Flug zu dieser Zeit Probleme macht
    ULONG VonCity{};          // bezeichnet eine Stadt
    ULONG NachCity{};         // bezeichnet eine Stadt
    SLONG Startzeit{};        // Zu diesem Zeitpunkt (0-24h) beginnt dieser Eintrag
    SLONG Landezeit{};        // Zu diesem Zeitpunkt (0-24h) landet das Flugzeug
    SLONG Startdate{};        // Referenz auf Sim.Date
    SLONG Landedate{};        // Referenz auf Sim.Date
    SLONG ObjectType{};       // 0=Nix 1=Route 2=Auftrag 3=Automatik 4=Fracht
    SLONG ObjectId{-1};       // Bezeichnet Auftrag oder -1
    SLONG Ticketpreis{};      // Ticketpreis für Routen
    SLONG TicketpreisFC{};    // Ticketpreis für Routen (Erste Klasse)
};

class CFlugplan {
  public:
    SLONG StartCity{};               // Hier beginnt der Flugplan
    BUFFER_V<CFlugplanEintrag> Flug; // Eine Zeile des Flugplans
    SLONG NextFlight{-1};            // Aktueller; sonst nächster Flug
    SLONG NextStart{-1};             // Verweis auf den nächsten, startenden Flug

  public:
    CFlugplan() {
        Flug.ReSize(10 * 6); // Maximal 10 Flüge pro Tag
        for (SLONG c = Flug.AnzEntries() - 1; c >= 0; c--) {
            Flug[c].ObjectType = 0;
        }
    }
};

class CPlane {
  public:
    CString Name{"noname"};    // Der Name des Flugzeuges
    ULONG TypeId{};            // referenziert CPlaneType oder ist -1
    CFlugplan Flugplan;        // Der Flugplan
    SLONG ptPassagiere{};      // Maximale Zahl der Passagiere (ein erste Klasse Passagier verbraucht 2 Plätze)
    SLONG ptReichweite{};      // Reichweite in km
    SLONG ptGeschwindigkeit{}; // in km/h
    SLONG ptVerbrauch{};       // Kerosin in l/h
};
class CPlanes : public ALBUM_V<CPlane> {
  public:
    CPlanes() : ALBUM_V<CPlane>("Planes") {}
};

class PLAYER {
  public:
    SLONG PlayerNum{}; // Seine Nummer
    BOOL IsOut;        // Ist der Spieler aus dem Spiel?
    UBYTE Owner{};     // Spieler=0, Computergegner=1, Netzwerkgegner=2

    CPlanes Planes;       // Flugzeuge, die der Spieler besitzt
    CAuftraege Auftraege; // Verträge die er für Flüge abgeschlossen hat
};

//--------------------------------------------------------------------------------------------
// Berechnet, wieviel ein Flug kostet:
//--------------------------------------------------------------------------------------------
SLONG CalculateFlightKerosin(SLONG VonCity, SLONG NachCity, SLONG Verbrauch, SLONG Geschwindigkeit) {
    SLONG Kerosin = Cities.CalcDistance(VonCity, NachCity) / 1000 // weil Distanz in m übergeben wird
                    * Verbrauch / 160                             // Liter pro Barrel
                    / Geschwindigkeit;

    return (Kerosin);
}

//--------------------------------------------------------------------------------------------
// Berechnet, wieviel ein Flug kostet (ignoriere Kerosin-Tanks)
//--------------------------------------------------------------------------------------------
SLONG CalculateFlightCostNoTank(SLONG VonCity, SLONG NachCity, SLONG Verbrauch, SLONG Geschwindigkeit) {
    SLONG Kerosin = CalculateFlightKerosin(VonCity, NachCity, Verbrauch, Geschwindigkeit);
    SLONG Kosten = Kerosin * Sim.Kerosin;

    if (Kosten < 1000) {
        Kosten = 1000;
    }

    return (Kosten);
}

#endif // COMPAT_H_
