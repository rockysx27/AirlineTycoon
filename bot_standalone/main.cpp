#include "BotHelper.h"
#include "BotPlaner.h"
#include "compat_global.h"
#include "compat_misc.h"
#include "GameMechanic.h"

#include <iostream>

int run(bool useImproved, int numPasses);

int run(bool useImproved, int numPasses) {
    Init();

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

    for (int i = 0; i < numPasses; i++) {
        // hprintf("******************** planning pass %d ********************", i);
        auto source = BotPlaner::JobOwner::International;
        std::vector<int> cities;
        if (source == BotPlaner::JobOwner::TravelAgency) {
            ReisebueroAuftraege.FillForReisebuero();
        } else if (source == BotPlaner::JobOwner::International) {
            for (int cityNum = 0; cityNum < AuslandsAuftraege.size(); cityNum++) {
                GameMechanic::refillFlightJobs(cityNum);
                cities.push_back(cityNum);
            }
        }

        BotPlaner planer(qPlayer, qPlayer.Planes, source, cities);
        planer.planFlights(planeIds, useImproved);
        planer.applySolution();
        int nIncorrect = Helper::checkFlightJobs(qPlayer, true);
        if (nIncorrect > 0) {
            break;
        }
    }

    int totalGain = 0;
    for (auto planeId : planeIds) {
        totalGain += Helper::calculateScheduleGain(qPlayer, planeId);
    }
    return totalGain;
}

static const int numAveraged = 10;

int main(int argc, char *argv[]) {

    bool useImproved = false;
    if (argc > 1) {
        useImproved = atoi(argv[1]) > 0;
    }
    int numPasses = 1;
    if (argc > 2) {
        numPasses = atoi(argv[2]);
    }
    if (argc > 3) {
        kNumToAdd = atoi(argv[3]);
    }
    if (argc > 4) {
        kNumToRemove = atoi(argv[4]);
    }
    if (argc > 5) {
        kTempStart = atoi(argv[5]);
    }
    if (argc > 6) {
        kTempStep = atoi(argv[6]);
    }

    std::vector<int> params;
    std::vector<int64_t> results;
    for (int param = 0; param < 50; param++) {
        params.push_back(param);
        results.push_back(0);
        for (int i = 0; i < numAveraged; i++) {
            results.back() += run(useImproved, numPasses);
        }
        results.back() /= numAveraged;
    }
    for (auto i : params) {
        std::cout << i << ", ";
    }
    std::cout << std::endl;
    for (auto i : results) {
        std::cout << i << ", ";
    }
    std::cout << std::endl;

    /*
    std::cout << "kNumToAdd = " << kNumToAdd << std::endl;
    std::cout << "kNumToRemove = " << kNumToRemove << std::endl;
    std::cout << "kTempStart = " << kTempStart << std::endl;
    std::cout << "kTempStep = " << kTempStep << std::endl;
    run(useImproved, numPasses);
    */

    redprintf("END");

    return 0;
}
