#include "BotPlaner.h"

#include "BotHelper.h"
#include "TeakLibW.h"

#include <chrono>
#include <iostream>

// #define PRINT_DETAIL 1
// #define PRINT_OVERALL 1

static const int kMaxTimesVisited = 4;
static const int kMinPremium = 1000;

BotPlaner::Solution BotPlaner::algo1FindFlightPlan(Graph &g, int planeIdx, PlaneTime availTime, const std::vector<int> &eligibleJobIds) {
    int numJobs = 0;
    for (const auto &i : eligibleJobIds) {
        if (i) {
            numJobs++;
        }
    }
#ifdef PRINT_DETAIL
    std::cout << ">>> Scheduling plane " << planeIdx << " for " << numJobs << " jobs." << std::endl;
    auto t_begin = std::chrono::steady_clock::now();
#endif

    /* simple backtracing algo */
    int nVisited = 0;
    PlaneTime currentTime{availTime};
    int currentPremium = 0;
    int currentCost = 0;
    int currentNode = planeIdx;
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

        /* advance or backtrack? */
        bool advance = true;
        if (currentTime.getDate() >= Sim.Date + kScheduleForNextDays) {
            advance = false;
        }
        if (curState.numVisited > kMaxTimesVisited) {
            advance = false;
        }

        /* found solution? */
        if (nVisited == numJobs || !advance) {
            advance = false;

            solutions.emplace_back(currentPremium - currentCost);

            int n = currentNode;
            while (g.nodeState[n].cameFrom != -1) {
                int jobIdx = g.nodeInfo[n].jobIdx;
                assert(jobIdx >= 0);
                solutions.back().jobs.emplace_front(jobIdx, g.nodeState[n].startTime, g.nodeState[n].startTime + g.nodeInfo[n].duration);
                n = g.nodeState[n].cameFrom;
            }

            if (solutions.back().totalPremium > bestPremium) {
                bestPremium = solutions.back().totalPremium;
                bestPremiumIdx = solutions.size() - 1;
            }
        }

        /* determine next node */
        if (!advance) {
            curState.outIdx = curInfo.bestNeighbors.size();
        }
        while (curState.outIdx < curInfo.bestNeighbors.size()) {
            int n = curInfo.bestNeighbors[curState.outIdx];
            assert(n >= g.nPlanes && n < g.nNodes);

            int jobIdx = g.nodeInfo[n].jobIdx;
            if (eligibleJobIds[jobIdx] == 0) {
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

        if (curState.outIdx < curInfo.bestNeighbors.size()) {
            /* advance to next node */
            int nextNode = curInfo.bestNeighbors[curState.outIdx];
            curState.outIdx++;

            g.nodeState[nextNode].cameFrom = currentNode;
            currentCost += g.adjMatrix[currentNode][nextNode].cost;
            currentTime += g.adjMatrix[currentNode][nextNode].duration;
            assert(g.adjMatrix[currentNode][nextNode].duration >= 0);

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
            curState.outIdx = 0;
            nVisited--;

            currentPremium -= curInfo.premium;
            currentTime = curState.availTime;

            currentCost -= g.adjMatrix[prevNode][currentNode].cost;
            currentTime -= g.adjMatrix[prevNode][currentNode].duration;
            assert(g.adjMatrix[prevNode][currentNode].duration >= 0);

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
        std::cout << ">>> Elapsed time algo1FindFlightPlan(): " << delta << " microseconds" << std::endl;
    }
#endif

    if (bestPremiumIdx == -1) {
        return {0};
    }

    return solutions[bestPremiumIdx];
}

bool BotPlaner::algo1() {
    int nPlanes = mPlaneStates.size();
    int nPlaneTypes = mPlaneTypeToPlane.size();
    int totalDelta = 0;
    for (int i = 0; i < mJobList.size(); i++) {
        auto &jobState = mJobList[i];

#ifdef PRINT_OVERALL
        std::cout << "Evaluating job " << Helper::getJobName(jobState.auftrag) << std::endl;
#endif

        int bestDelta = INT_MIN;
        int bestPlane = -1;
        Solution bestSolution{0};
        for (int p = 0; p < nPlanes; p++) {
            auto &qPlaneState = mPlaneStates[p];
            auto pt = qPlaneState.planeTypeId;
            auto &g = mGraphs[pt];
            if (g.adjMatrix[p][nPlaneTypes + i].cost == -1) {
                continue;
            }

            qPlaneState.bJobIdAssigned[i] = 1;

            auto newSolution = algo1FindFlightPlan(g, p, qPlaneState.availTime, qPlaneState.bJobIdAssigned);
            int delta = newSolution.totalPremium - qPlaneState.currentSolution.totalPremium;
            if (delta > bestDelta) {
                bestDelta = delta;
                bestPlane = p;
                bestSolution = newSolution;
            }
            qPlaneState.bJobIdAssigned[i] = 0;
        }

        if (bestPlane != -1) {
            auto &planeState = mPlaneStates[bestPlane];
            if (bestDelta >= kMinPremium) {
                planeState.bJobIdAssigned[i] = 1;

                planeState.currentSolution = std::move(bestSolution);
                planeState.currentSolution.planeId = planeState.planeId;
                planeState.currentSolution.scheduleFromTime = mScheduleFromTime;

                jobState.numStillNeeded = bestPlane;
                totalDelta += bestDelta;

#ifdef PRINT_OVERALL
                const auto &qPlane = qPlanes[planeState.planeId];
                std::cout << "Job assigned to plane " << qPlane.Name << " (best delta: " << bestDelta << ")" << std::endl;
            } else {
                std::cout << "Job is not worth it (best delta: " << bestDelta << ")" << std::endl;
#endif
            }
        } else {
#ifdef PRINT_OVERALL
            std::cout << "Cannot find plane for job " << Helper::getJobName(mJobList[i].auftrag) << std::endl;
#endif
        }
    }

#ifdef PRINT_OVERALL
    hprintf("Improved gain in total by %d", totalDelta);
#endif

    return (totalDelta > 0);
}
