#include "bot.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <unordered_map>

// Game interface

static const int kNumCities = 100;
static const int kNumJobsPerCity = 10;
static const int kNumPlanes = 2;

int CalcDistance(int startCity, int destCity) { return 200 * std::abs(startCity - destCity); }

int CalcDuration(const CPlane *plane, int startCity, int destCity) {
    int dist = CalcDistance(startCity, destCity);
    return (dist + plane->mSpeed - 1) / plane->mSpeed;
}

int CalcCost(const CPlane *plane, int startCity, int destCity) { return plane->mFuelCostPerKM * CalcDistance(startCity, destCity); }

// Test set

CAuftraege createTestJobs(int count) {
    CAuftraege list(count);
    for (int i = 0; i < list.size(); i++) {
        list[i].VonCity = (i % kNumCities);
        list[i].NachCity = (list[i].VonCity + (17 * i)) % kNumCities;
        if (list[i].NachCity == list[i].VonCity) {
            list[i].NachCity = (list[i].NachCity + 1) % kNumCities;
        }
        list[i].Praemie *= (1 + i % 3);
        if (i % 2 == 0) {
            list[i].Date = i % 7;
            list[i].BisDate = i % 7;
        } else {
            list[i].Date = 0;
            list[i].BisDate = i % 7;
        }
    }
    return list;
}

/////////////////////
// BEGIN
/////////////////////

// #define PRINT_DETAIL 1
#define PRINT_OVERALL 1

static const int kMinPremium = 1000;
static const int kMaxTimesVisited = 3;

BotPlaner::BotPlaner(const CPlanes &planes, JobOwner jobOwner, int intJobSource) : mJobOwner(jobOwner), mIntJobSource(intJobSource), qPlanes(planes) {}

void BotPlaner::printJob(const CAuftrag &qAuftrag) {
    std::cout << qAuftrag.VonCity << " => " << qAuftrag.NachCity;
    std::cout << " (" << qAuftrag.Personen << ", " << CalcDistance(qAuftrag.VonCity, qAuftrag.NachCity);
    std::cout << ", P: " << qAuftrag.Praemie << " $, F: " << qAuftrag.Strafe << " $)";
    std::cout << std::endl;
}

void BotPlaner::printJobShort(const CAuftrag &qAuftrag) { std::cout << qAuftrag.VonCity << " => " << qAuftrag.NachCity; }

void BotPlaner::printSolution(const Solution &solution, const std::vector<FlightJob> &list) {
    std::cout << "Solution has premium = " << solution.totalPremium << std::endl;
    for (const auto &i : solution.jobs) {
        std::cout << i.second.getDate() << ":" << i.second.getHour() << "  ";
        const auto &job = list[i.first];
        printJob(job.auftrag);
    }
    std::cout << std::endl;
}

void BotPlaner::printGraph(const std::vector<PlaneState> &planeStates, const std::vector<FlightJob> &list, const Graph &g) {
    std::cout << "digraph G {" << std::endl;
    for (int i = 0; i < g.nNodes; i++) {
        auto &curInfo = g.nodeInfo[i];

        std::cout << "node" << i << " [";
        if (i >= g.nPlanes) {
            auto &job = list[i - g.nPlanes].auftrag;
            std::cout << "label=\"" << job.VonCity << " => " << job.NachCity << "\\n";
            // std::cout << "(" << job.Personen  << ", " << CalcDistance(job.VonCity, job.NachCity);
            // std::cout << ", P: " << job.Praemie << " $, F: " << job.Strafe << " $)\\n";
            std::cout << "earning " << curInfo.premium << " $, " << curInfo.duration << " hours\\n";
            std::cout << "earliest: " << curInfo.earliest << ", latest " << curInfo.latest << "\"";
        } else {
            const auto &planeState = planeStates[i];
            const auto &qPlane = qPlanes[planeState.planeId];
            std::cout << "label=\"start for " << qPlane.Name << "\",shape=Mdiamond";
        }
        std::cout << "];" << std::endl;
    }
    for (int i = 0; i < g.nNodes; i++) {
        for (int j = 0; j < g.nNodes; j++) {
            std::cout << "node" << i << " -> "
                      << "node" << j << " [";
            std::cout << "label=\"" << g.adjMatrix[i][j].cost << " $, " << g.adjMatrix[i][j].duration << " h\"";
            std::cout << "];" << std::endl;
        }
    }
    std::cout << "}" << std::endl;
}

void BotPlaner::findPlaneTypes(std::vector<PlaneState> &planeStates, std::vector<const CPlane *> &planeTypeToPlane) {
    int nPlaneTypes = 0;
    std::unordered_map<int, int> mapOldType2NewType;
    for (auto &planeState : planeStates) {
        const auto &qPlane = qPlanes[planeState.planeId];
        if (qPlane.TypeId == -1) {
            planeState.planeTypeId = nPlaneTypes++;
            planeTypeToPlane.push_back(&qPlane);
            continue;
        }
        if (mapOldType2NewType.end() == mapOldType2NewType.find(qPlane.TypeId)) {
            mapOldType2NewType[qPlane.TypeId] = nPlaneTypes++;
            planeTypeToPlane.push_back(&qPlane);
        }
        planeState.planeTypeId = mapOldType2NewType[qPlane.TypeId];
    }
    assert(planeTypeToPlane.size() == nPlaneTypes);

#ifdef PRINT_OVERALL
    std::cout << "There are " << planeStates.size() << " planes of " << planeTypeToPlane.size() << " types" << std::endl;
#endif
}

BotPlaner::Solution BotPlaner::findFlightPlan(Graph &g, int planeId, PlaneTime availTime, const std::vector<int> &eligibleJobIds) {
    int numJobs = 0;
    for (const auto &i : eligibleJobIds) {
        if (i) {
            numJobs++;
        }
    }
#ifdef PRINT_DETAIL
    std::cout << ">>> Scheduling plane " << planeId << " for " << numJobs << " jobs." << std::endl;
    auto t_begin = std::chrono::steady_clock::now();
#endif

    /* simple backtracing algo */
    int nVisited = 0;
    PlaneTime currentTime{availTime};
    int currentPremium = 0;
    int currentCost = 0;
    int currentNode = planeId;
    g.resetNodes();

    std::vector<Solution> solutions;
    int bestPremium = INT_MIN;
    int bestPremiumIdx = -1;
    g.nodeState[currentNode].visitedThisTour = true;
    while (currentNode >= 0) {
        const auto &curInfo = g.nodeInfo[currentNode];
        auto &curState = g.nodeState[currentNode];
        curState.numVisited++;
        if (curState.visitedThisTour == false) {
            curState.visitedThisTour = true;
            nVisited++;

            currentPremium += curInfo.premium;

            curState.availTime = currentTime;
            if (currentTime.getDate() < curInfo.earliest) {
                currentTime.setDate(curInfo.earliest);
            }
            curState.startTime = currentTime;

            currentTime += curInfo.duration;
        }

#ifdef PRINT_DETAIL
        std::cout << ">>> Now examining node " << currentNode << ", visited " << nVisited << std::endl;
        std::cout << ">>> currentPremium: " << currentPremium << std::endl;
        std::cout << ">>> currentCost: " << currentCost << std::endl;
        std::cout << ">>> currentTime: " << currentTime.getDate() << ":" << currentTime.getHour() << std::endl;
        std::cout << std::endl;
#endif

        /* found solution? */
        if (nVisited == numJobs) {
            Solution solution;
            solution.totalPremium = currentPremium - currentCost;
            int n = currentNode;
            while (g.nodeState[n].cameFrom != -1) {
                int jobID = g.nodeToJob(n);
                assert(jobID >= 0);
                solution.jobs.emplace_front(jobID, g.nodeState[n].startTime);
                n = g.nodeState[n].cameFrom;
            }
            solutions.emplace_back(std::move(solution));

            if (solutions.back().totalPremium > bestPremium) {
                bestPremium = solutions.back().totalPremium;
                bestPremiumIdx = solutions.size() - 1;
            }
        }

        /* determine next node */
        if (curState.numVisited > kMaxTimesVisited) {
            curState.outIdx = curInfo.closestNeighbors.size();
        }
        while (curState.outIdx < curInfo.closestNeighbors.size()) {
            int n = curInfo.closestNeighbors[curState.outIdx];
            assert(n >= g.nPlanes && n < g.nNodes);
            if (eligibleJobIds[g.nodeToJob(n)] == 0) {
                curState.outIdx++;
                continue;
            }
            if (g.nodeState[n].visitedThisTour) {
                curState.outIdx++;
                continue;
            }
            auto arrivalTime = currentTime + g.adjMatrix[currentNode][n].duration;
            if (arrivalTime.getDate() > g.nodeInfo[n].latest) {
                curState.outIdx++;
                continue;
            }
            break;
        }

        if (curState.outIdx < curInfo.closestNeighbors.size()) {
            /* advance to next node */
            int nextNode = curInfo.closestNeighbors[curState.outIdx];
            curState.outIdx++;

            g.nodeState[nextNode].cameFrom = currentNode;
            currentCost += g.adjMatrix[currentNode][nextNode].cost;
            currentTime += g.adjMatrix[currentNode][nextNode].duration;

            currentNode = nextNode;
        } else {
            /* no nodes left to visit, back-track */

            if (currentNode < g.nPlanes) {
                /* we are back at the start, we are done here */
                break;
            }
            int prevNode = curState.cameFrom;

            assert(curState.visitedThisTour == true);
            curState.visitedThisTour = false;
            nVisited--;
            currentPremium -= curInfo.premium;
            currentTime = curState.availTime;

            currentCost -= g.adjMatrix[prevNode][currentNode].cost;
            currentTime -= g.adjMatrix[prevNode][currentNode].duration;

            curState.outIdx = 0;

            currentNode = prevNode;
        }
    }

#ifdef PRINT_DETAIL
    if (bestPremiumIdx == -1) {
        std::cout << ">>> Could not find any solution!" << std::endl;
    } else {
        std::cout << ">>> Found solution with premium = " << solutions[bestPremiumIdx].totalPremium << std::endl;
    }

    {
        auto t_end = std::chrono::steady_clock::now();
        auto delta = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_begin).count();
        std::cout << ">>> Elapsed time findFlightPlan(): " << delta << " microseconds" << std::endl;
    }
#endif

    if (bestPremiumIdx == -1) {
        return {};
    }

    return solutions[bestPremiumIdx];
}

void BotPlaner::gatherAndPlanJobs(std::vector<FlightJob> &jobList, std::vector<PlaneState> &planeStates) {
    int nPlanes = planeStates.size();

#ifdef PRINT_OVERALL
    auto t_begin = std::chrono::steady_clock::now();
#endif

    /* find number of unique plane types */
    std::vector<const CPlane *> planeTypeToPlane;
    findPlaneTypes(planeStates, planeTypeToPlane);
    int nPlaneTypes = planeTypeToPlane.size();

    /* prepare job list */
    std::sort(jobList.begin(), jobList.end(), [](const FlightJob &a, const FlightJob &b) { return a.auftrag.Praemie > b.auftrag.Praemie; });
    for (int p = 0; p < nPlanes; p++) {
        auto &qPlaneState = planeStates[p];
        const auto &qPlane = qPlanes[qPlaneState.planeId];

        qPlaneState.assignedJobIds.resize(jobList.size());

        /* filter jobs that exceed range / seats */
        int eligibleJobs = 0;
        for (int j = 0; j < jobList.size(); j++) {
            const auto &job = jobList[j].auftrag;
            if (job.Personen <= qPlane.ptPassagiere && CalcDistance(job.VonCity, job.NachCity) <= qPlane.ptReichweite) {
                jobList[j].eligiblePlanes.push_back(p);
                eligibleJobs++;
            }
        }
#ifdef PRINT_OVERALL
        std::cout << "Plane " << qPlane.Name << " is able to fly " << eligibleJobs << " / " << jobList.size() << " jobs" << std::endl;
#endif
    }

    /* prepare graph */
    std::vector<Graph> graphs(nPlaneTypes, Graph(nPlanes, jobList.size()));
    for (int pt = 0; pt < nPlaneTypes; pt++) {
        auto &g = graphs[pt];
        for (int i = 0; i < g.nNodes; i++) {
            auto &qNodeInfo = g.nodeInfo[i];

            if (i >= nPlanes) {
                auto &job = jobList[i - nPlanes].auftrag;
                qNodeInfo.premium = job.Praemie - CalcCost(planeTypeToPlane[pt], job.VonCity, job.NachCity);
                qNodeInfo.duration = CalcDuration(planeTypeToPlane[pt], job.VonCity, job.NachCity);
                qNodeInfo.earliest = job.Date;
                qNodeInfo.latest = job.BisDate;
            } else {
                qNodeInfo.earliest = 0;
                qNodeInfo.latest = INT_MAX;
            }

            std::vector<std::pair<int, int>> neighborList;
            neighborList.reserve(g.nNodes);
            for (int j = 0; j < g.nNodes; j++) {
                int startCity = (i >= nPlanes) ? jobList[i - nPlanes].auftrag.NachCity : planeStates[i].availCity;
                int destCity = (j >= nPlanes) ? jobList[j - nPlanes].auftrag.VonCity : planeStates[j].availCity;
                if (startCity != destCity) {
                    g.adjMatrix[i][j].cost = CalcCost(planeTypeToPlane[pt], startCity, destCity);
                    g.adjMatrix[i][j].duration = CalcDuration(planeTypeToPlane[pt], startCity, destCity);
                } else {
                    g.adjMatrix[i][j].cost = 0;
                    g.adjMatrix[i][j].duration = 0;
                }

                if (j >= nPlanes) {
                    neighborList.emplace_back(j, g.adjMatrix[i][j].cost);
                }
            }

            std::sort(neighborList.begin(), neighborList.end(), [](const std::pair<int, int> &a, const std::pair<int, int> &b) { return a.second < b.second; });
            for (const auto &n : neighborList) {
                qNodeInfo.closestNeighbors.push_back(n.first);
            }
        }
    }

#ifdef PRINT_OVERALL
    {
        auto t_end = std::chrono::steady_clock::now();
        auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_begin).count();
        std::cout << "Elapsed time for preparation in gatherAndPlanJobs(): " << delta << " ms" << std::endl;
    }
#endif

    /* main algo */
    int nJobsScheduled = 0;
    int totalDelta = 0;
    for (int i = 0; i < jobList.size(); i++) {
        auto &jobState = jobList[i];

#ifdef PRINT_OVERALL
        std::cout << "Evaluating job ";
        printJobShort(jobState.auftrag);
        std::cout << std::endl;
#endif

        int bestDelta = INT_MIN;
        int bestPlane = -1;
        Solution bestSolution;
        for (int p : jobState.eligiblePlanes) {
            auto &planeState = planeStates[p];
            auto pt = planeState.planeTypeId;

            planeState.assignedJobIds[i] = 1;

            auto newSolution = findFlightPlan(graphs[pt], planeState.planeTypeId, planeState.availTime, planeState.assignedJobIds);
            int delta = newSolution.totalPremium - planeState.currentSolution.totalPremium;
            if (delta > bestDelta) {
                bestDelta = delta;
                bestPlane = p;
                bestSolution = newSolution;
            }
            planeState.assignedJobIds[i] = 0;
        }

        if (bestPlane != -1) {
            auto &planeState = planeStates[bestPlane];
            if (bestDelta >= kMinPremium) {
                planeState.assignedJobIds[i] = 1;
                planeState.currentSolution = std::move(bestSolution);
                jobState.assignedtoPlaneId = bestPlane;
                totalDelta += bestDelta;
                nJobsScheduled++;

#ifdef PRINT_OVERALL
                const auto &qPlane = qPlanes[planeState.planeId];
                std::cout << "Job assigned to plane " << qPlane.Name << " (best delta: " << bestDelta << ")" << std::endl;
            } else {
                std::cout << "Job is not worth it (best delta: " << bestDelta << ")" << std::endl;
#endif
            }
        } else {
#ifdef PRINT_OVERALL
            std::cout << "Cannot find plane for job ";
            printJobShort(jobList[i].auftrag);
            std::cout << std::endl;
#endif
        }
    }

#ifdef PRINT_OVERALL
    {
        auto t_end = std::chrono::steady_clock::now();
        auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_begin).count();
        std::cout << "Elapsed time in total in gatherAndPlanJobs(): " << delta << " ms" << std::endl;
    }

    std::cout << "Scheduled " << nJobsScheduled << " out of " << jobList.size() << " jobs." << std::endl;
    std::cout << "Improved gain by " << totalDelta << std::endl;
#endif

    for (int p = 0; p < nPlanes; p++) {
        auto &planeState = planeStates[p];

        auto &bestSolution = planeState.currentSolution;
        if (bestSolution.totalPremium < kMinPremium) {
            continue;
        }

#ifdef PRINT_DETAIL
        const auto &qPlane = qPlanes[planeState.planeId];
        std::cout << "Schedule of plane " << qPlane.Name << std::endl;
        printSolution(bestSolution, jobList);
#endif
    }
    // printGraph(planeStates, jobList, graphs[0]);
}

bool BotPlaner::planFlights(const std::vector<int> &planeIds) {
    /* prepare list of planes */
    std::vector<PlaneState> planeStates;
    planeStates.reserve(planeIds.size());
    for (auto i : planeIds) {
        planeStates.push_back({});
        auto &planeState = planeStates.back();
        planeState.planeId = i;

        /* determine when and where the plane will be available */
        planeState.availTime = {};
        planeState.availCity = i % 4;
    }

    /* prepare list of jobs */
    std::vector<FlightJob> openJobs;
    auto jobSourceA = createTestJobs(kNumJobsPerCity * kNumCities);

    /* job source A: Passenger flights */
    for (int i = 0; i < jobSourceA.size(); i++) {
        openJobs.emplace_back(i, jobSourceA.at(i), mJobOwner);
    }

    /* start algo */
    gatherAndPlanJobs(openJobs, planeStates);

    return true;
}

int main() {
    std::vector<int> planeIds;
    std::vector<CPlane> planes(kNumPlanes);
    for (int i = 0; i < planes.size(); i++) {
        planeIds.push_back(i);

        char buf[100];
        snprintf(buf, sizeof(buf), "plane%d", i);
        planes[i].Name += buf;
        if (i % 2 == 0) {
            planes[i].ptReichweite *= 2;
            planes[i].TypeId++;
        }
    }

    BotPlaner bot(planes, BotPlaner::JobOwner::Player);
    bot.planFlights(planeIds);

    return 0;
}
