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

std::pair<PlaneTime, int> getPlaneAvailableTimeLoc(const CPlane &qPlane);
SLONG checkPlaneSchedule(const PLAYER &qPlayer, SLONG planeId, bool printOnErrorOnly);
SLONG checkPlaneSchedule(const PLAYER &qPlayer, const CPlane &qPlane, bool printOnErrorOnly);
SLONG checkFlightJobs(const PLAYER &qPlayer);
void printFlightJobs(const PLAYER &qPlayer, SLONG planeId);
void printFlightJobs(const PLAYER &qPlayer, const CPlane &qPlane);
SLONG calculateScheduleGain(const PLAYER &qPlayer, SLONG planeId);

bool checkRoomOpen(SLONG roomId);

} // namespace Helper