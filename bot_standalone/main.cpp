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
    for (int i = 0; i < 10; i++) {
        planeIds.push_back(qPlayer.BuyPlane(106, nullptr));
        planeIds.push_back(qPlayer.BuyPlane(111, nullptr));
        planeIds.push_back(qPlayer.BuyPlane(111, nullptr));
    }

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
        auto solutions = planer.planFlights(planeIds, useImproved);
        planer.applySolution(qPlayer, solutions);
        int nIncorrect = Helper::checkFlightJobs(qPlayer, true);
        if (nIncorrect > 0) {
            break;
        }
    }

    int totalGain = 0;
    for (auto planeId : planeIds) {
        totalGain += Helper::calculateScheduleGain(qPlayer, planeId);
    }
    std::cout << "main.cpp:" << totalGain << std::endl;
    return totalGain;
}

static const int numAveraged = 5;

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

#ifdef FIRST
    std::vector<int> params;
    for (int param = 0; param < 100; param++) {
        params.push_back(param);
    }
    for (auto i : params) {
        std::cout << i << ", ";
    }
    std::cout << std::endl;

    for (auto param : params) {
        kNumToAdd = param;
        kNumToRemove = 0;
        kTempStart = 1000;
        kTempStep = kTempStart / 10;
        int64_t result = 0;
        for (int i = 0; i < numAveraged; i++) {
            result += run(useImproved, numPasses);
        }
        result /= numAveraged;
        std::cout << result << ", ";
    }
    std::cout << std::endl;

    for (auto param : params) {
        kNumToAdd = param;
        kNumToRemove = 1;
        kTempStart = 1000;
        kTempStep = kTempStart / 10;
        int64_t result = 0;
        for (int i = 0; i < numAveraged; i++) {
            result += run(useImproved, numPasses);
        }
        result /= numAveraged;
        std::cout << result << ", ";
    }
    std::cout << std::endl;

    for (auto param : params) {
        kNumToAdd = param;
        kNumToRemove = 2;
        kTempStart = 1000;
        kTempStep = kTempStart / 10;
        int64_t result = 0;
        for (int i = 0; i < numAveraged; i++) {
            result += run(useImproved, numPasses);
        }
        result /= numAveraged;
        std::cout << result << ", ";
    }
    std::cout << std::endl;

    for (auto param : params) {
        kNumToAdd = param;
        kNumToRemove = param / 10 + 1;
        kTempStart = 1000;
        kTempStep = kTempStart / 10;
        int64_t result = 0;
        for (int i = 0; i < numAveraged; i++) {
            result += run(useImproved, numPasses);
        }
        result /= numAveraged;
        std::cout << result << ", ";
    }
    std::cout << std::endl;

    for (auto param : params) {
        kNumToAdd = param;
        kNumToRemove = param / 5 + 1;
        kTempStart = 1000;
        kTempStep = kTempStart / 10;
        int64_t result = 0;
        for (int i = 0; i < numAveraged; i++) {
            result += run(useImproved, numPasses);
        }
        result /= numAveraged;
        std::cout << result << ", ";
    }
    std::cout << std::endl;

    for (auto param : params) {
        kNumToAdd = param;
        kNumToRemove = param / 2 + 1;
        kTempStart = 1000;
        kTempStep = kTempStart / 10;
        int64_t result = 0;
        for (int i = 0; i < numAveraged; i++) {
            result += run(useImproved, numPasses);
        }
        result /= numAveraged;
        std::cout << result << ", ";
    }
    std::cout << std::endl;

    for (auto param : params) {
        kNumToAdd = param;
        kNumToRemove = param - 2;
        kTempStart = 1000;
        kTempStep = kTempStart / 10;
        int64_t result = 0;
        for (int i = 0; i < numAveraged; i++) {
            result += run(useImproved, numPasses);
        }
        result /= numAveraged;
        std::cout << result << ", ";
    }
    std::cout << std::endl;

    for (auto param : params) {
        kNumToAdd = param;
        kNumToRemove = param;
        kTempStart = 1000;
        kTempStep = kTempStart / 10;
        int64_t result = 0;
        for (int i = 0; i < numAveraged; i++) {
            result += run(useImproved, numPasses);
        }
        result /= numAveraged;
        std::cout << result << ", ";
    }
    std::cout << std::endl;
#endif

#if 0
    for (auto param : params) {
        kNumToAdd = 4;
        kNumToRemove = 1;
        kTempStart = 50 * param;
        kTempStep = kTempStart / 10;
        int64_t result = 0;
        for (int i = 0; i < numAveraged; i++) {
            result += run(useImproved, numPasses);
        }
        result /= numAveraged;
        std::cout << result << ", ";
    }
    std::cout << std::endl;

    for (auto param : params) {
        kNumToAdd = 4;
        kNumToRemove = 2;
        kTempStart = 50 * param;
        kTempStep = kTempStart / 10;
        int64_t result = 0;
        for (int i = 0; i < numAveraged; i++) {
            result += run(useImproved, numPasses);
        }
        result /= numAveraged;
        std::cout << result << ", ";
    }
    std::cout << std::endl;

    for (auto param : params) {
        kNumToAdd = 4;
        kNumToRemove = 3;
        kTempStart = 50 * param;
        kTempStep = kTempStart / 10;
        int64_t result = 0;
        for (int i = 0; i < numAveraged; i++) {
            result += run(useImproved, numPasses);
        }
        result /= numAveraged;
        std::cout << result << ", ";
    }
    std::cout << std::endl;

    for (auto param : params) {
        kNumToAdd = 4;
        kNumToRemove = 4;
        kTempStart = 50 * param;
        kTempStep = kTempStart / 10;
        int64_t result = 0;
        for (int i = 0; i < numAveraged; i++) {
            result += run(useImproved, numPasses);
        }
        result /= numAveraged;
        std::cout << result << ", ";
    }
    std::cout << std::endl;
#endif

    std::cout << "kNumToAdd = " << kNumToAdd << std::endl;
    std::cout << "kNumToRemove = " << kNumToRemove << std::endl;
    std::cout << "kTempStart = " << kTempStart << std::endl;
    std::cout << "kTempStep = " << kTempStep << std::endl;
    run(useImproved, numPasses);

    redprintf("END");

    return 0;
}
