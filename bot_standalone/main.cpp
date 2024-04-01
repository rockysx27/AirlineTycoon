#include "BotHelper.h"
#include "BotPlaner.h"
#include "compat_global.h"
#include "compat_misc.h"
#include "GameMechanic.h"

#include <iostream>

int64_t run(bool useImproved, int numPasses);

int64_t run(bool useImproved, int numPasses) {
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
        auto source = BotPlaner::JobOwner::InternationalFreight;
        std::vector<int> cities;
        if (source == BotPlaner::JobOwner::TravelAgency) {
            ReisebueroAuftraege.FillForReisebuero();
        } else if (source == BotPlaner::JobOwner::International) {
            for (int cityNum = 0; cityNum < AuslandsAuftraege.size(); cityNum++) {
                GameMechanic::refillFlightJobs(cityNum);
                cities.push_back(cityNum);
            }
        } else if (source == BotPlaner::JobOwner::InternationalFreight) {
            for (int cityNum = 0; cityNum < AuslandsFrachten.size(); cityNum++) {
                GameMechanic::refillFlightJobs(cityNum);
                cities.push_back(cityNum);
            }
        }

        BotPlaner planer(qPlayer, qPlayer.Planes, source, cities);
        auto solutions = planer.planFlights(planeIds, useImproved, 2);
        planer.applySolution(qPlayer, solutions);
        int nIncorrect = Helper::checkFlightJobs(qPlayer, true);
        if (nIncorrect > 0) {
            break;
        }
    }

    int64_t totalGain = 0;
    for (auto planeId : planeIds) {
        totalGain += Helper::calculateScheduleInfo(qPlayer, planeId).gain;
    }
    return totalGain;
}

static const int numAveraged = 5;

int64_t benchmark(bool useImproved, int numPasses);

int64_t benchmark(bool useImproved, int numPasses) {
    std::cout << "kNumBestToAdd = " << kNumBestToAdd << std::endl;
    std::cout << "kNumToAdd = " << kNumToAdd << std::endl;
    std::cout << "kNumToRemove = " << kNumToRemove << std::endl;
    std::cout << "kTempStart = " << kTempStart << std::endl;
    std::cout << "kTempStep = " << kTempStep << std::endl;

    std::vector<int64_t> r;
    for (int i = 0; i < numAveraged; i++) {
        r.push_back(run(useImproved, numPasses));
    }
    int64_t mean = std::accumulate(r.begin(), r.end(), 0) / numAveraged;
    int64_t stddev = std::accumulate(r.begin(), r.end(), 0LL, [mean](int64_t acc, int64_t val) { return acc + (val - mean) * (val - mean); });
    stddev = std::round(std::sqrt(stddev / numAveraged));

    std::cout << "averaged benchmark result: " << Insert1000erDots(mean).c_str() << " (stddev = " << Insert1000erDots(stddev).c_str() << ")" << std::endl;
    return mean;
}

int main(int argc, char *argv[]) {

    int numPasses = 2;
    bool useImproved = false;
    if (argc > 1) {
        useImproved = atoi(argv[1]) > 0;
    }
    if (argc > 2) {
        kNumBestToAdd = atoi(argv[2]);
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

    benchmark(useImproved, numPasses);

    redprintf("END");

    return 0;
}
