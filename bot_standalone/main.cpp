#include "BotHelper.h"
#include "BotPlaner.h"
#include "compat_global.h"
#include "compat_misc.h"
#include "GameMechanic.h"

#include <iostream>

static const int numAveraged = 5;

void findBestPlaneTypes();
int64_t run(bool useImproved, int numPasses);
int64_t _run(bool useImproved, int numPasses, const std::vector<int> &planeIds);
int64_t benchmark(bool useImproved, int numPasses);

void findBestPlaneTypes() {
    std::vector<int> bestPlaneTypes;
    for (int i = 0; i < 10; i++) {
        int64_t bestGain = 0;
        int bestType = -1;
        for (int newType = 100; newType <= 126; newType++) {
            Init();
            if (!PlaneTypes.IsInAlbum(newType + 0x10000000)) {
                continue;
            }
            std::cout << "Checking plane type " << PlaneTypes[newType + 0x10000000].Name << std::endl;

            auto &qPlayer = Sim.Players.Players[0];

            std::vector<int> planeIds;
            for (const auto &type : bestPlaneTypes) {
                planeIds.push_back(qPlayer.BuyPlane(type, nullptr));
            }
            planeIds.push_back(qPlayer.BuyPlane(newType, nullptr));

            auto gain = _run(true, 1, planeIds);
            if (gain > bestGain) {
                bestGain = gain;
                bestType = newType;
            }
        }

        if (-1 == bestType) {
            break;
        }
        std::cout << "Best next plane type is " << PlaneTypes[bestType + 0x10000000].Name << std::endl;
        bestPlaneTypes.push_back(bestType);
    }

    for (const auto &i : bestPlaneTypes) {
        std::cout << "Final plane list: " << PlaneTypes[i + 0x10000000].Name << std::endl;
    }
}

int64_t run(bool useImproved, int numPasses) {
    Init();
    auto &qPlayer = Sim.Players.Players[0];
    std::vector<int> planeIds;
    for (int i = 0; i < 10; i++) {
        planeIds.push_back(qPlayer.BuyPlane(106, nullptr));
        planeIds.push_back(qPlayer.BuyPlane(111, nullptr));
        planeIds.push_back(qPlayer.BuyPlane(111, nullptr));
    }
    return _run(useImproved, numPasses, planeIds);
}

int64_t _run(bool useImproved, int numPasses, const std::vector<int> &planeIds) {
    auto &qPlayer = Sim.Players.Players[0];
    for (auto &plane : qPlayer.Planes) {
        plane.Flugplan.StartCity = plane.Ort = Sim.HomeAirportId;
        plane.Flugplan.UpdateNextFlight();
        plane.Flugplan.UpdateNextStart();
        plane.CheckFlugplaene(qPlayer.PlayerNum);
    }

    for (int i = 0; i < numPasses; i++) {
        auto source = BotPlaner::JobOwner::International;
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

        // std::cout << "====================" << std::endl;
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

    findBestPlaneTypes();
    // benchmark(useImproved, numPasses);

    redprintf("END");

    return 0;
}
