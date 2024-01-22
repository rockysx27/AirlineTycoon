#include "BotPlaner.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <unordered_map>

#include "BotHelper.h"

static const int kNumCities = 100;
static const int kNumJobsPerCity = 10;
static const int kNumPlanes = 2;

int CalcDistance(int startCity, int destCity) { return 200 * std::abs(startCity - destCity); }

int CalcDuration(const CPlane *plane, int startCity, int destCity) {
    int dist = CalcDistance(startCity, destCity);
    return (dist + plane->ptGeschwindigkeit - 1) / plane->ptGeschwindigkeit;
}

int CalcCost(const CPlane *plane, int startCity, int destCity) { return plane->ptVerbrauch * CalcDistance(startCity, destCity); }

// Test set

CAuftraege createTestJobs(int count) {
    CAuftraege list;
    list.ReSize(count);
    for (int i = 0; i < list.AnzEntries(); i++) {
        CAuftrag auftrag;
        auftrag.VonCity = (i % kNumCities);
        auftrag.NachCity = (auftrag.VonCity + (17 * i)) % kNumCities;
        if (auftrag.NachCity == auftrag.VonCity) {
            auftrag.NachCity = (auftrag.NachCity + 1) % kNumCities;
        }
        auftrag.Praemie *= (1 + i % 3);
        if (i % 2 == 0) {
            auftrag.Date = i % 7;
            auftrag.BisDate = i % 7;
        } else {
            auftrag.Date = 0;
            auftrag.BisDate = i % 7;
        }

        auftrag.Personen = 300;
        auftrag.Strafe = auftrag.Praemie;

        list += auftrag;
    }
    return list;
}

/////////////////////
// BEGIN
/////////////////////

// #define PRINT_DETAIL 1
// #define PRINT_OVERALL 1

static const int kAvailTimeExtra = 2;
static const int kDurationExtra = 1;
static const int kMinPremium = 1000;
static const int kMaxTimesVisited = 4;
static const bool kCanDropJobs = false;

static SLONG getPremiumEmptyFlight(const CPlane *qPlane, SLONG VonCity, SLONG NachCity) {
    return (qPlane->ptPassagiere * Cities.CalcDistance(VonCity, NachCity) / 1000 / 40);
}

BotPlaner::BotPlaner(PLAYER &player, const CPlanes &planes, JobOwner jobOwner, std::vector<int> intJobSource)
    : qPlayer(player), mJobOwner(jobOwner), mIntJobSource(std::move(intJobSource)), qPlanes(planes) {}

void BotPlaner::printSolution(const Solution &solution, const std::vector<FlightJob> &list) {
    std::cout << "Solution has premium = " << solution.totalPremium << std::endl;
    for (const auto &i : solution.jobs) {
        std::cout << i.start.getDate() << ":" << i.start.getHour() << "  ";
        const auto &job = list[i.jobIdx];
        Helper::printJob(job.auftrag);
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
            std::cout << "label=\"" << (LPCTSTR)Cities[job.VonCity].Kuerzel << " => " << (LPCTSTR)Cities[job.NachCity].Kuerzel << "\\n";
            // std::cout << "(" << job.Personen  << ", " << CalcDistance(job.VonCity, job.NachCity);
            // std::cout << ", P: " << job.Praemie << " $, F: " << job.Strafe << " $)\\n";
            std::cout << "earning " << curInfo.premium << " $, " << curInfo.duration << " hours\\n";
            std::cout << "earliest: " << Helper::getWeekday(curInfo.earliest) << ", latest " << Helper::getWeekday(curInfo.latest) << "\\n";
            std::cout << "\"";
        } else {
            const auto &planeState = planeStates[i];
            const auto &qPlane = qPlanes[planeState.planeId];
            std::cout << "label=\"start for " << qPlane.Name << "\",shape=Mdiamond";
        }
        std::cout << "];" << std::endl;
    }
    for (int i = 0; i < g.nNodes; i++) {
        auto &curInfo = g.nodeInfo[i];
        for (int j = 0; j < g.nNodes; j++) {
            if (g.adjMatrix[i][j].cost < 0) {
                continue;
            }
            std::cout << "node" << i << " -> "
                      << "node" << j << " [";
            std::cout << "label=\"" << g.adjMatrix[i][j].cost << " $, " << g.adjMatrix[i][j].duration << " h\"";
            for (int n = 0; n < 3 && n < curInfo.bestNeighbors.size(); n++) {
                if (curInfo.bestNeighbors[n] == j) {
                    std::cout << ", penwidth=" << (8 - 2 * n);
                    break;
                }
            }
            std::cout << "];" << std::endl;
        }
    }
    std::cout << "}" << std::endl;
}

void BotPlaner::findPlaneTypes(std::vector<PlaneState> &planeStates) {
    int nPlaneTypes = 0;
    std::unordered_map<int, int> mapOldType2NewType;
    for (auto &planeState : planeStates) {
        const auto &qPlane = qPlanes[planeState.planeId];
        if (qPlane.TypeId == -1) {
            planeState.planeTypeId = nPlaneTypes++;
            mPlaneTypeToPlane.push_back(&qPlane);
            continue;
        }
        if (mapOldType2NewType.end() == mapOldType2NewType.find(qPlane.TypeId)) {
            mapOldType2NewType[qPlane.TypeId] = nPlaneTypes++;
            mPlaneTypeToPlane.push_back(&qPlane);
        }
        planeState.planeTypeId = mapOldType2NewType[qPlane.TypeId];
    }
    assert(mPlaneTypeToPlane.size() == nPlaneTypes);

#ifdef PRINT_OVERALL
    std::cout << "There are " << planeStates.size() << " planes of " << mPlaneTypeToPlane.size() << " types" << std::endl;
#endif
}

BotPlaner::Solution BotPlaner::findFlightPlan(Graph &g, int planeIdx, PlaneTime availTime, const std::vector<int> &eligibleJobIds) {
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
        if (currentTime.getDate() >= Sim.Date + 6) {
            advance = false;
        }
        if (curState.numVisited > kMaxTimesVisited) {
            advance = false;
        }

        /* found solution? */
        if (nVisited == numJobs || !advance) {
            advance = false;

            Solution solution;
            solution.totalPremium = currentPremium - currentCost;
            int n = currentNode;
            while (g.nodeState[n].cameFrom != -1) {
                int jobIdx = g.nodeInfo[n].jobIdx;
                assert(jobIdx >= 0);
                solution.jobs.emplace_front(jobIdx, g.nodeState[n].startTime, g.nodeState[n].startTime + g.nodeInfo[n].duration);
                n = g.nodeState[n].cameFrom;
            }
            solutions.emplace_back(std::move(solution));

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
            int jobIdx = g.nodeInfo[n].jobIdx;
            assert(n >= g.nPlanes && n < g.nNodes);
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
            nVisited--;
            currentPremium -= curInfo.premium;
            currentTime = curState.availTime;

            currentCost -= g.adjMatrix[prevNode][currentNode].cost;
            currentTime -= g.adjMatrix[prevNode][currentNode].duration;
            assert(g.adjMatrix[prevNode][currentNode].duration >= 0);

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

std::pair<int, int> BotPlaner::gatherAndPlanJobs(std::vector<FlightJob> &jobList, std::vector<PlaneState> &planeStates) {
    int nPlanes = planeStates.size();
    int nPlaneTypes = mPlaneTypeToPlane.size();

#ifdef PRINT_OVERALL
    auto t_begin = std::chrono::steady_clock::now();
#endif

    /* prepare graph */
    std::vector<Graph> graphs(nPlaneTypes, Graph(nPlanes, jobList.size()));
    for (int pt = 0; pt < nPlaneTypes; pt++) {
        auto &g = graphs[pt];

        /* nodes */
        for (int i = 0; i < g.nNodes; i++) {
            auto &qNodeInfo = g.nodeInfo[i];

            if (i >= nPlanes && (jobList[i - nPlanes].bPlaneTypeEligible[pt] == 1)) {
                auto &job = jobList[i - nPlanes].auftrag;
                const auto *plane = mPlaneTypeToPlane[pt];
                qNodeInfo.jobIdx = i - nPlanes;
                qNodeInfo.premium = job.Praemie - CalculateFlightCostNoTank(job.VonCity, job.NachCity, plane->ptVerbrauch, plane->ptGeschwindigkeit);
                qNodeInfo.duration = kDurationExtra + Cities.CalcFlugdauer(job.VonCity, job.NachCity, plane->ptGeschwindigkeit);
                qNodeInfo.earliest = job.Date;
                qNodeInfo.latest = job.BisDate;
            }
        }

        /* edges */
        for (int i = 0; i < g.nNodes; i++) {
            auto &qNodeInfo = g.nodeInfo[i];
            std::vector<std::pair<int, int>> neighborList;
            neighborList.reserve(g.nNodes);
            for (int j = nPlanes; j < g.nNodes; j++) {
                if (i == j) {
                    continue; /* self edge not allowed */
                }
                if (g.nodeInfo[j].premium <= 0) {
                    continue; /* job has no premium / not eligible for plane */
                }
                if (g.nodeInfo[i].earliest > g.nodeInfo[j].latest) {
                    continue; /* not possible because date constraints */
                }

                int startCity = (i >= nPlanes) ? jobList[i - nPlanes].auftrag.NachCity : planeStates[i].availCity;
                int destCity = jobList[j - nPlanes].auftrag.VonCity;
                if (startCity != destCity) {
                    const auto *plane = mPlaneTypeToPlane[pt];
                    g.adjMatrix[i][j].cost = CalculateFlightCostNoTank(startCity, destCity, plane->ptVerbrauch, plane->ptGeschwindigkeit);
                    g.adjMatrix[i][j].cost -= getPremiumEmptyFlight(plane, startCity, destCity);
                    g.adjMatrix[i][j].duration = kDurationExtra + Cities.CalcFlugdauer(startCity, destCity, plane->ptGeschwindigkeit);
                } else {
                    g.adjMatrix[i][j].cost = 0;
                    g.adjMatrix[i][j].duration = 0;
                }
                neighborList.emplace_back(j, g.nodeInfo[j].premium - g.adjMatrix[i][j].cost);
            }

            std::sort(neighborList.begin(), neighborList.end(), [](const std::pair<int, int> &a, const std::pair<int, int> &b) { return a.second > b.second; });
            for (const auto &n : neighborList) {
                qNodeInfo.bestNeighbors.push_back(n.first);
            }
        }
#ifdef PRINT_OVERALL
        printGraph(planeStates, jobList, graphs[pt]);
#endif
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
        std::cout << "Evaluating job " << Helper::getJobName(jobState.auftrag) << std::endl;
#endif

        int bestDelta = INT_MIN;
        int bestPlane = -1;
        Solution bestSolution;
        for (int p = 0; p < nPlanes; p++) {
            auto &qPlaneState = planeStates[p];
            auto pt = qPlaneState.planeTypeId;
            if (jobState.bPlaneTypeEligible[pt] == 0) {
                continue;
            }

            qPlaneState.bJobIdAssigned[i] = 1;

            auto newSolution = findFlightPlan(graphs[pt], p, qPlaneState.availTime, qPlaneState.bJobIdAssigned);
            int delta = newSolution.totalPremium - qPlaneState.currentSolution.totalPremium;
            if (delta > bestDelta) {
                bestDelta = delta;
                bestPlane = p;
                bestSolution = newSolution;
            }
            qPlaneState.bJobIdAssigned[i] = 0;
        }

        if (bestPlane != -1) {
            auto &planeState = planeStates[bestPlane];
            if (bestDelta >= kMinPremium) {
                planeState.bJobIdAssigned[i] = 1;
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
            std::cout << "Cannot find plane for job " << Helper::getJobName(jobList[i].auftrag) << std::endl;
#endif
        }
    }

#ifdef PRINT_OVERALL
    {
        auto t_end = std::chrono::steady_clock::now();
        auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_begin).count();
        std::cout << "Elapsed time in total in gatherAndPlanJobs(): " << delta << " ms" << std::endl;
    }
#endif

    std::cout << "Improved gain in total by " << totalDelta << std::endl;

    for (int p = 0; p < nPlanes; p++) {
        int planeId = planeStates[p].planeId;
        auto &bestSolution = planeStates[p].currentSolution;
        if (bestSolution.totalPremium < kMinPremium) {
            continue;
        }

        SLONG oldGain = Helper::calculateScheduleGain(qPlayer, planeId);
        applySolution(planeId, bestSolution, jobList);
        SLONG newGain = Helper::calculateScheduleGain(qPlayer, planeId);
        std::cout << qPlanes[planeId].Name << ": Improved gain " << oldGain << " => " << newGain << std::endl;

#ifdef PRINT_DETAIL
        const auto &qPlane = qPlanes[planeState.planeId];
        std::cout << "Schedule of plane " << qPlane.Name << std::endl;
        printSolution(bestSolution, jobList);
#endif
    }

    return {nJobsScheduled, totalDelta};
}

int BotPlaner::planFlights(const std::vector<int> &planeIdsInput) {
    hprintf("BotPlaner::planFlights(): Current time: %d", Sim.GetHour());

    /* kill existing flight plans */
    std::vector<int> planeIds;
    for (auto i : planeIdsInput) {
        if (qPlanes.IsInAlbum(i) != 0) {
            planeIds.push_back(i);
        }
    }

    /* prepare list of jobs */
    std::vector<FlightJob> jobList;
    auto jobSourceA = createTestJobs(kNumJobsPerCity * kNumCities);
    for (int i = 0; i < jobSourceA.size(); i++) {
        jobList.emplace_back(source.jobs->GetIdFromIndex(i), source.sourceId, source.jobs->at(i), source.owner);
    }
    std::sort(jobList.begin(), jobList.end(), [](const FlightJob &a, const FlightJob &b) {
        if (kCanDropJobs) {
            return (a.auftrag.Praemie + (a.wasTaken() ? a.auftrag.Strafe : 0)) > (b.auftrag.Praemie + (b.wasTaken() ? b.auftrag.Strafe : 0));
        }
        if (a.wasTaken() != b.wasTaken()) {
            return a.wasTaken();
        }
        return a.auftrag.Praemie > b.auftrag.Praemie;
    });

    /* statistics of existing jobs */
    int nPreviouslyOwned = 0;
    int nNewJobs = 0;
    for (const auto &job : jobList) {
        if (job.owner == JobOwner::Player || job.owner == JobOwner::PlayerFreight) {
            nPreviouslyOwned++;
        } else {
            nNewJobs++;
        }
    }

    /* prepare list of planes */
    std::vector<PlaneState> planeStates;
    planeStates.reserve(planeIds.size());
    for (auto i : planeIds) {
        assert(i >= 0x1000000 && qPlanes.IsInAlbum(i));

        planeStates.push_back({});
        auto &planeState = planeStates.back();
        planeState.planeId = i;
        planeState.bJobIdAssigned.resize(jobList.size());

        /* determine when and where the plane will be available */
        planeState.availTime = {};
        planeState.availCity = i % 4;

        assert(planeState.availCity >= 0);
#ifdef PRINT_OVERALL
        hprintf("BotPlaner::planFlights(): Plane %s is in %s @ %s %d", (LPCTSTR)qPlanes[i].Name, (LPCTSTR)Cities[planeState.availCity].Kuerzel,
                (LPCTSTR)Helper::getWeekday(planeState.availTime.getDate()), planeState.availTime.getHour());
#endif
    }

    /* find number of unique plane types */
    findPlaneTypes(planeStates);
    int nPlaneTypes = mPlaneTypeToPlane.size();
    for (auto &job : jobList) {
        job.bPlaneTypeEligible.resize(nPlaneTypes);
    }

    /* figure out which jobs are eligible for which plane */
    for (auto &planeState : planeStates) {
        const auto &qPlane = qPlanes[planeState.planeId];

        /* filter jobs that exceed range / seats */
        int eligibleJobs = 0;
        for (auto &job : jobList) {
            if (job.auftrag.Personen <= qPlane.ptPassagiere && job.auftrag.FitsInPlane(qPlane)) {
                job.bPlaneTypeEligible[planeState.planeTypeId] = 1;
                eligibleJobs++;
            }
        }
#ifdef PRINT_OVERALL
        std::cout << "Plane " << qPlane.Name << " is able to fly " << eligibleJobs << " / " << jobList.size() << " jobs" << std::endl;
#endif
    }

    /* start algo */
    int nJobsScheduled, totalGain;
    std::tie(nJobsScheduled, totalGain) = gatherAndPlanJobs(jobList, planeStates);

    /* check statistics */
    int nPreviouslyOwnedScheduled = 0;
    int nNewJobsScheduled = 0;
    for (const auto &job : jobList) {
        if (job.assignedtoPlaneId < 0) {
            continue;
        }
        if (job.owner == JobOwner::Player || job.owner == JobOwner::PlayerFreight) {
            nPreviouslyOwnedScheduled++;
        } else {
            nNewJobsScheduled++;
        }
    }
    assert(nJobsScheduled == nPreviouslyOwnedScheduled + nNewJobsScheduled);
    std::cout << "Scheduled " << nPreviouslyOwnedScheduled << " out of " << nPreviouslyOwned << " existing jobs." << std::endl;
    std::cout << "Scheduled " << nNewJobsScheduled << " out of " << nNewJobs << " of new jobs." << std::endl;

    return totalGain;
}

int main() {
    std::vector<int> planeIds;
    std::vector<CPlane> planes(kNumPlanes);
    for (int i = 0; i < planes.size(); i++) {
        planeIds.push_back(i);

        char buf[100];
        snprintf(buf, sizeof(buf), "plane%d", i);
        planes[i].Name += buf;
        planes[i].ptPassagiere = 300;
        planes[i].ptReichweite = 10000;
        planes[i].ptGeschwindigkeit = 1000;
        planes[i].ptVerbrauch = 1;
        if (i % 2 == 0) {
            planes[i].ptReichweite *= 2;
            planes[i].TypeId++;
        }
    }

    BotPlaner bot(planes, BotPlaner::JobOwner::Player);
    bot.planFlights(planeIds);

    return 0;
}
