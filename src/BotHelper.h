#include "StdAfx.h"

namespace Helper {

CString getWeekday(UWORD date);

void printJob(const CAuftrag &qAuftrag);
void printFPE(const CFlugplanEintrag &qFPE);
std::string getJobName(const CAuftrag &qAuftrag);
SLONG checkFlightJobs(const PLAYER &qPlayer);
void printFlightJobs(const PLAYER &qPlayer, SLONG planeId);

bool checkRoomOpen(SLONG roomId);

} // namespace Helper