#ifndef COMPAT_TYPES_H_
#define COMPAT_TYPES_H_

#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <locale>
#include <string>

#define FALSE 0
#define TRUE 1

using SLONG = int32_t;
using ULONG = uint32_t;
using UWORD = uint16_t;
using BOOL = SLONG;
using UBYTE = unsigned char;
using SBYTE = signed char;
using LPCTSTR = const char *;

using std::strlen;
class CString : public std::string {
  public:
    CString(const char *str) : std::string(str) {}
    operator const char *() const { return c_str(); }
};

static char *bprintf(char const *format, ...) {
    static char buffer[8192];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    return buffer;
}

static CString Insert1000erDots(SLONG Value) {
    short c = 0;
    short d = 0; // Position in neuen bzw. alten String
    short l = 0; // Stringlänge
    short CharsUntilPoint = 0;
    char String[80];
    char Tmp[80];

    static char Dot = 0;

    if (Dot == 0) {
        Dot = std::use_facet<std::moneypunct<char, true>>(std::locale("")).thousands_sep();
        if (Dot != '.' && Dot != ',' && Dot != ':' && Dot != '/' && Dot != '\'' && Dot != '`') {
            Dot = '.';
        }
    }

    sprintf(Tmp, "%i", Value);

    l = short(strlen(Tmp));

    String[0] = Tmp[0];
    c = d = 1;

    CharsUntilPoint = short((l - c) % 3);

    if (Value < 0) {
        CharsUntilPoint = short((l - 1 - c) % 3 + 1);
    }

    for (; d < l; c++, d++) {
        if (CharsUntilPoint == 0 && d < l - 1) {
            String[c] = Dot;
            c++;
            CharsUntilPoint = 3;
        }

        String[c] = Tmp[d];
        CharsUntilPoint--;
    }

    String[c] = '\0';

    return (String);
}

class CEinheit {
  public:
    char *bString(SLONG Value) const { return (bprintf(Name.c_str(), (LPCTSTR)Insert1000erDots(Value))); }

    std::string Name{"%ld km"};
};

class RES {
  public:
    CString GetS(SLONG /*token*/, SLONG idx) const {
        switch (idx) {
        case 3010:
            return "Montag";
        case 3011:
            return "Dienstag";
        case 3012:
            return "Mittwoch";
        case 3013:
            return "Donnerstag";
        case 3014:
            return "Freitag";
        case 3015:
            return "Samstag";
        case 3016:
            return "Sonntag";
        default:
            return "Fehler";
        }
    }
};

#endif // COMPAT_TYPES_H_
