#include <optional>

class PlaneTime;

namespace Helper {

CString getWeekday(UWORD date);

void printJob(const CAuftrag &qAuftrag);
void printRoute(const CRoute &qRoute);
void printFreight(const CFracht &qAuftrag);

std::string getRouteName(const CRoute &qRoute);
std::string getJobName(const CAuftrag &qAuftrag);
std::string getFreightName(const CFracht &qAuftrag);

void printFPE(const CFlugplanEintrag &qFPE);

const CFlugplanEintrag *getLastFlight(const CPlane &qPlane);
const CFlugplanEintrag *getLastFlightNotAfter(const CPlane &qPlane, PlaneTime ignoreFrom);
std::pair<PlaneTime, int> getPlaneAvailableTimeLoc(const CPlane &qPlane, std::optional<PlaneTime> ignoreFrom, std::optional<PlaneTime> earliest);

SLONG checkPlaneSchedule(const PLAYER &qPlayer, SLONG planeId, bool printOnErrorOnly);
SLONG checkPlaneSchedule(const PLAYER &qPlayer, const CPlane &qPlane, bool printOnErrorOnly);
SLONG _checkPlaneSchedule(const PLAYER &qPlayer, const CPlane &qPlane, std::unordered_map<int, CString> &assignedJobs, bool printOnErrorOnly);
SLONG checkFlightJobs(const PLAYER &qPlayer, bool printOnErrorOnly);
void printFlightJobs(const PLAYER &qPlayer, SLONG planeId);
void printFlightJobs(const PLAYER &qPlayer, const CPlane &qPlane);

struct ScheduleInfo {
    SLONG jobs{0};
    SLONG gain{0};
    SLONG passengers{0};
    SLONG tons{0};
    SLONG miles{0};
    SLONG uhrigFlights{0};
    SLONG hoursFlights{0};
    SLONG hoursAutoFlights{0};
    SLONG numPlanes{0};
    DOUBLE getRatioFlights() { return 100.0 * hoursFlights / (24 * 7 * numPlanes); }
    DOUBLE getRatioAutoFlights() { return 100.0 * hoursAutoFlights / (24 * 7 * numPlanes); }

    ScheduleInfo &operator+=(ScheduleInfo delta) {
        jobs += delta.jobs;
        gain += delta.gain;
        passengers += delta.passengers;
        tons += delta.tons;
        miles += delta.miles;
        uhrigFlights += delta.uhrigFlights;
        hoursFlights += delta.hoursFlights;
        hoursAutoFlights += delta.hoursAutoFlights;
        numPlanes += delta.numPlanes;
        return *this;
    }

    void printGain() { hprintf("Schedule gain with %ld planes is %s $.", numPlanes, Insert1000erDots(gain).c_str()); }
    void printDetails() {
        if (uhrigFlights > 0) {
            hprintf("Flying %ld passengers, %ld tons, %ld jobs (%ld from Uhrig) and %ld miles.", passengers, tons, jobs, uhrigFlights, miles);
        } else {
            hprintf("Flying %ld passengers, %ld tons, %ld jobs and %ld miles.", passengers, tons, jobs, miles);
        }
        hprintf("%.1f %% of plane schedule are regular flights, %.1f %% are automatic flights.", getRatioFlights(), getRatioAutoFlights());
    }
};
ScheduleInfo calculateScheduleInfo(const PLAYER &qPlayer, SLONG planeId);

void printAllSchedules(bool infoOnly);

bool checkRoomOpen(SLONG roomId);

const char *getItemName(SLONG item);

} // namespace Helper
