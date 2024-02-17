#include "BotHelper.h"
#include "BotPlaner.h"
#include "compat_global.h"

#include "compat_misc.h"

int main(int argc, char *argv[]) {

    Init();
    for (const auto &job : ReisebueroAuftraege) {
        Helper::printJob(job);
    }

    auto &qPlayer = Sim.Players.Players[0];

    std::vector<int> planeIds;
    planeIds.push_back(qPlayer.BuyPlane(106, nullptr));
    planeIds.push_back(qPlayer.BuyPlane(111, nullptr));
    planeIds.push_back(qPlayer.BuyPlane(111, nullptr));

    for (auto &plane : qPlayer.Planes) {
        plane.Flugplan.StartCity = plane.Ort = Sim.HomeAirportId;
        plane.Flugplan.UpdateNextFlight();
        plane.Flugplan.UpdateNextStart();
        plane.CheckFlugplaene(qPlayer.PlayerNum);
    }

    for (int i = 0; i < 5; i++) {
        hprintf("******************** planning pass %d ********************", i);
        ReisebueroAuftraege.FillForReisebuero();
        for (const auto &job : ReisebueroAuftraege) {
            Helper::printJob(job);
        }

        BotPlaner planer(qPlayer, qPlayer.Planes, BotPlaner::JobOwner::TravelAgency, {});
        planer.planFlights(planeIds, true);
        planer.applySolution();
        int nIncorrect = Helper::checkFlightJobs(qPlayer, true);
        if (nIncorrect > 0) {
            break;
        }
    }

    redprintf("END");

    return 0;
}