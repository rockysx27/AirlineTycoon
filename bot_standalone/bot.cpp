#include "bot.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <unordered_map>

// Game interface

int CalcDistance(int startCity, int destCity) { return 5000 * std::abs(startCity - destCity); }

int CalcDuration(const CPlane *plane, int startCity, int destCity) {
    int dist = CalcDistance(startCity, destCity);
    return (dist + plane->mSpeed - 1) / plane->mSpeed;
}

int CalcCost(const CPlane *plane, int startCity, int destCity) { return plane->mFuelCostPerKM * CalcDistance(startCity, destCity); }

// Test set

CAuftraege createTestJobs(int count) {
    CAuftraege list(count);
    for (int i = 0; i < list.size(); i++) {
        list[i].VonCity = (i % 4);
        list[i].NachCity = (list[i].VonCity + 1 + (i % 3)) % 4;
        list[i].Praemie *= (1 + i % 3);
        if (i % 2 == 0) {
            list[i].Date = i % 7;
            list[i].BisDate = i % 7;
        }
    }
    return list;
}

static const int kMinPremium = 1000;

Bot::Bot(const CPlanes &planes, JobOwner jobOwner, int intJobSource) : mJobOwner(jobOwner), mIntJobSource(intJobSource), qPlanes(planes) {}

void Bot::printJob(const CAuftrag &qAuftrag) {
    std::cout << qAuftrag.VonCity << " => " << qAuftrag.NachCity;
    std::cout << " (" << qAuftrag.Personen << ", " << CalcDistance(qAuftrag.VonCity, qAuftrag.NachCity);
    std::cout << ", P: " << qAuftrag.Praemie << " $, F: " << qAuftrag.Strafe << " $)";
    std::cout << std::endl;
}

void Bot::printJobShort(const CAuftrag &qAuftrag) { std::cout << qAuftrag.VonCity << " => " << qAuftrag.NachCity; }

void Bot::printSolution(const Solution &solution, const std::vector<FlightJob> &list) {
    std::cout << "Solution has premium = " << solution.totalPremium << std::endl;
    for (const auto &i : solution.jobs) {
        std::cout << i.second.getDate() << ":" << i.second.getHour() << "  ";
        const auto &job = list[i.first];
        printJob(job.auftrag);
    }
    std::cout << std::endl;
}

void Bot::printGraph(const std::vector<FlightJob> &list, const Graph &g, int pt) {
    std::cout << "digraph G {" << std::endl;
    for (int i = 0; i < g.nNodes; i++) {
        auto &curInfo = g.nodeInfo[pt][i];

        std::cout << "node" << i << " [";
        if (i >= g.nPlanes) {
            auto &job = list[i - g.nPlanes].auftrag;
            std::cout << "label=\"" << job.VonCity << " => " << job.NachCity << "\\n";
            // std::cout << "(" << job.Personen  << ", " << CalcDistance(job.VonCity, job.NachCity);
            // std::cout << ", P: " << job.Praemie << " $, F: " << job.Strafe << " $)\\n";
            std::cout << "earning " << curInfo.premium << " $, " << curInfo.duration << " hours\\n";
            std::cout << "earliest: " << curInfo.earliest << ", latest " << curInfo.latest << "\"";
        } else {
            std::cout << "label=\"start for " << qPlanes[i].Name << "\",shape=Mdiamond";
        }
        std::cout << "];" << std::endl;
    }
    for (int i = 0; i < g.nNodes; i++) {
        for (int j = 0; j < g.nNodes; j++) {
            std::cout << "node" << i << " -> "
                      << "node" << j << " [";
            std::cout << "label=\"" << g.adjMatrix[pt][i][j].cost << " $, " << g.adjMatrix[pt][i][j].duration << " h\"";
            std::cout << "];" << std::endl;
        }
    }
    std::cout << "}" << std::endl;
}

void Bot::findPlaneTypes(std::vector<int> &planeIdToType, std::vector<const CPlane *> &planeTypeToPlane) {
    int nPlaneTypes = 0;
    std::unordered_map<int, int> mapOldType2NewType;
    for (int i = 0; i < qPlanes.size(); i++) {
        if (qPlanes[i].TypeId == -1) {
            planeIdToType[i] = nPlaneTypes++;
            planeTypeToPlane.push_back(&qPlanes[i]);
            continue;
        }
        if (mapOldType2NewType.end() == mapOldType2NewType.find(qPlanes[i].TypeId)) {
            mapOldType2NewType[qPlanes[i].TypeId] = nPlaneTypes++;
            planeTypeToPlane.push_back(&qPlanes[i]);
        }
        planeIdToType[i] = mapOldType2NewType[qPlanes[i].TypeId];
    }
    assert(planeIdToType.size() == qPlanes.size());
    assert(planeTypeToPlane.size() == nPlaneTypes);
    std::cout << "There are " << qPlanes.size() << " planes of " << planeTypeToPlane.size() << " types" << std::endl;
}

Bot::Solution Bot::findFlightPlan(Graph &g, int pt, int planeId, PlaneTime availTime, const std::vector<int> &eligibleJobIds) {
    // std::cout << ">>> Scheduling plane " << planeId << " for " << eligibleJobIds.size() << " jobs." << std::endl;

    auto t_begin = std::chrono::steady_clock::now();

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
    g.nodeState[currentNode].visited = true;
    while (currentNode >= 0) {
        const auto &curInfo = g.nodeInfo[pt][currentNode];
        auto &curState = g.nodeState[currentNode];
        if (curState.visited == false) {
            curState.visited = true;
            nVisited++;

            currentPremium += curInfo.premium;

            curState.availTime = currentTime;
            if (currentTime.getDate() < curInfo.earliest) {
                currentTime.setDate(curInfo.earliest);
            }
            curState.startTime = currentTime;

            currentTime += curInfo.duration;
        }

        /*std::cout << ">>> Now examining node " << currentNode << ", visited " << nVisited << std::endl;
        std::cout << ">>> currentPremium: " << currentPremium << std::endl;
        std::cout << ">>> currentCost: " << currentCost << std::endl;
        std::cout << ">>> currentTime: " << currentTime.getDate() << ":" << currentTime.getHour() << std::endl;
        std::cout << std::endl;*/

        /* found solution? */
        if (nVisited == eligibleJobIds.size()) {
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

            // printSolution(solutions.back(), list);
        }

        /* determine next node */
        while (curState.outIdx < eligibleJobIds.size()) {
            int n = g.jobToNode(eligibleJobIds[curState.outIdx]);
            assert(n >= g.nPlanes && n < g.nNodes);
            if (g.nodeState[n].visited) {
                curState.outIdx++;
                continue;
            }
            auto arrivalTime = currentTime + g.adjMatrix[pt][currentNode][n].duration;
            if (arrivalTime.getDate() > g.nodeInfo[pt][n].latest) {
                curState.outIdx++;
                continue;
            }
            break;
        }

        if (curState.outIdx < eligibleJobIds.size()) {
            /* advance to next node */
            int nextNode = g.jobToNode(eligibleJobIds[curState.outIdx]);
            curState.outIdx++;

            g.nodeState[nextNode].cameFrom = currentNode;
            currentCost += g.adjMatrix[pt][currentNode][nextNode].cost;
            currentTime += g.adjMatrix[pt][currentNode][nextNode].duration;

            currentNode = nextNode;
        } else {
            /* no nodes left to visit, back-track */

            if (currentNode < g.nPlanes) {
                /* we are back at the start, we are done here */
                break;
            }
            int prevNode = curState.cameFrom;

            assert(curState.visited == true);
            curState.visited = false;
            nVisited--;
            currentPremium -= curInfo.premium;
            currentTime = curState.availTime;

            currentCost -= g.adjMatrix[pt][prevNode][currentNode].cost;
            currentTime -= g.adjMatrix[pt][prevNode][currentNode].duration;

            curState.outIdx = 0;

            currentNode = prevNode;
        }
    }

    if (bestPremiumIdx == -1) {
        std::cout << ">>> Could not find any solution!" << std::endl;
        return {};
    }
    std::cout << ">>> Found solution with premium = " << solutions[bestPremiumIdx].totalPremium << std::endl;

    auto t_end = std::chrono::steady_clock::now();
    auto delta = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_begin).count();
    // std::cout << ">>> Elapsed time: " << delta << " microseconds" << std::endl;

    return solutions[bestPremiumIdx];
}

void Bot::gatherAndPlanJobs(std::vector<FlightJob> &jobList, std::vector<PlaneState> &planeStates) {
    int nPlanes = qPlanes.size();

    auto t_begin = std::chrono::steady_clock::now();

    /* find number of unique plane types */
    std::vector<int> planeIdToType(nPlanes);
    std::vector<const CPlane *> planeTypeToPlane;
    findPlaneTypes(planeIdToType, planeTypeToPlane);
    int nPlaneTypes = planeTypeToPlane.size();

    /* prepare job list */
    std::sort(jobList.begin(), jobList.end(), [](const FlightJob &a, const FlightJob &b) { return a.auftrag.Praemie > b.auftrag.Praemie; });
    for (int i = 0; i < jobList.size(); i++) {
        printJob(jobList[i].auftrag);
    }
    for (int i = 0; i < nPlanes; i++) {
        /* filter jobs that exceed range / seats */
        if (!planeStates[i].enabled) {
            continue;
        }
        int eligibleJobs = 0;
        for (int j = 0; j < jobList.size(); j++) {
            auto &job = jobList[j].auftrag;
            if (job.Personen <= qPlanes[i].ptPassagiere && CalcDistance(job.VonCity, job.NachCity) <= qPlanes[i].ptReichweite) {
                jobList[j].eligiblePlanes.push_back(i);
                eligibleJobs++;
            }
        }
        std::cout << "Plane " << i << " is able to fly " << eligibleJobs << " / " << jobList.size() << " jobs" << std::endl;
    }

    /* prepare graph */
    Graph g(nPlanes, jobList.size(), nPlaneTypes);
    for (int pt = 0; pt < nPlaneTypes; pt++) {
        for (int i = 0; i < g.nNodes; i++) {
            if (i >= nPlanes) {
                auto &job = jobList[i - nPlanes].auftrag;
                g.nodeInfo[pt][i].premium = job.Praemie - CalcCost(planeTypeToPlane[pt], job.VonCity, job.NachCity);
                g.nodeInfo[pt][i].duration = CalcDuration(planeTypeToPlane[pt], job.VonCity, job.NachCity);
                g.nodeInfo[pt][i].earliest = job.Date;
                g.nodeInfo[pt][i].latest = job.BisDate;
            } else {
                g.nodeInfo[pt][i].earliest = 0;
                g.nodeInfo[pt][i].latest = INT_MAX;
            }

            for (int j = 0; j < g.nNodes; j++) {
                int startCity = (i >= nPlanes) ? jobList[i - nPlanes].auftrag.NachCity : planeStates[i].availCity;
                int destCity = (j >= nPlanes) ? jobList[j - nPlanes].auftrag.VonCity : planeStates[j].availCity;
                if (startCity != destCity) {
                    g.adjMatrix[pt][i][j].cost = CalcCost(planeTypeToPlane[pt], startCity, destCity);
                    g.adjMatrix[pt][i][j].duration = CalcDuration(planeTypeToPlane[pt], startCity, destCity);
                } else {
                    g.adjMatrix[pt][i][j].cost = 0;
                    g.adjMatrix[pt][i][j].duration = 0;
                }
            }
        }
    }

    int nJobsScheduled = 0;
    int totalDelta = 0;
    for (int i = 0; i < jobList.size(); i++) {
        auto &jobState = jobList[i];

        int bestDelta = INT_MIN;
        int bestPlane = -1;
        Solution bestSolution;
        for (int p : jobState.eligiblePlanes) {
            auto &planeState = planeStates[p];
            planeState.assignedJobIds.push_back(i);

            std::cout << "Trying to assign job ";
            printJobShort(jobList[i].auftrag);
            std::cout << " to plane " << qPlanes[p].Name << std::endl;

            auto newSolution = findFlightPlan(g, planeIdToType[p], p, planeState.availTime, planeState.assignedJobIds);
            int delta = newSolution.totalPremium - planeState.currentSolution.totalPremium;
            if (delta > bestDelta) {
                bestDelta = delta;
                bestPlane = p;
                bestSolution = newSolution;
            }
            planeState.assignedJobIds.pop_back();
        }

        if (bestPlane != -1) {
            if (bestDelta >= kMinPremium) {
                auto &planeState = planeStates[bestPlane];
                planeState.assignedJobIds.push_back(i);
                planeState.currentSolution = std::move(bestSolution);
                jobState.assignedtoPlaneId = bestPlane;
                totalDelta += bestDelta;
                nJobsScheduled++;
            } else {
                std::cout << "Job ";
                printJobShort(jobList[i].auftrag);
                std::cout << " is not worth it (best delta: " << bestDelta << ")" << std::endl;
            }
        } else {
            std::cout << "Cannot find plane for job ";
            printJobShort(jobList[i].auftrag);
            std::cout << std::endl;
        }
    }

    auto t_end = std::chrono::steady_clock::now();
    auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_begin).count();
    std::cout << "Elapsed time: " << delta << " ms" << std::endl;

    std::cout << "Scheduled " << nJobsScheduled << " out of " << jobList.size() << " jobs." << std::endl;
    std::cout << "Improved gain by " << totalDelta << std::endl;

    std::cout << "New best solution for all planes: " << std::endl;
    for (int p = 0; p < nPlanes; p++) {
        auto &bestSolution = planeStates[p].currentSolution;
        if (bestSolution.totalPremium < kMinPremium) {
            continue;
        }
        std::cout << "Schedule of plane " << qPlanes[p].Name << std::endl;
        printSolution(bestSolution, jobList);
    }
    printGraph(jobList, g, 0);
}

bool Bot::planFlights(CAuftraege qAuftraege, int planeId) {
    /* prepare list of planes */
    std::vector<PlaneState> planeStates(qPlanes.size());
    for (int i = 0; i < qPlanes.size(); i++) {
        auto &planeState = planeStates[i];
        planeState.enabled = (i == planeId) || (-1 == planeId);
        if (!planeState.enabled) {
            continue;
        }

        /* determine when and where the plane will be available */
        planeState.availTime = {};
        planeState.availCity = i % 4;
    }

    /* prepare list of jobs */
    std::vector<FlightJob> openJobs;
    for (int i = 0; i < qAuftraege.size(); i++) {
        openJobs.emplace_back(i, qAuftraege[i], JobOwner::Player);
    }

    /* start algo */
    gatherAndPlanJobs(openJobs, planeStates);

    return true;
}

int main() {
    auto jobList = createTestJobs(10);

    std::vector<CPlane> planes(2);
    planes[0].Name = "Aachen";
    planes[1].Name = "Braunschweig";
    planes[1].ptReichweite *= 2;
    planes[1].TypeId++;

    Bot bot(planes, Bot::JobOwner::Player);
    bot.planFlights(jobList, -1);

    return 0;
}
