#include "BotPlaner.h"

#include "BotHelper.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <unordered_map>

// #define PRINT_DETAIL 1
// #define PRINT_OVERALL 1

const int kAvailTimeExtra = 2;
const int kDurationExtra = 1;
static const int kMinPremium = 1000;
static const int kScheduleForNextDays = 4;
static const int kMaxTimesVisited = 4;
static const bool kCanDropJobs = false;

static SLONG getPremiumEmptyFlight(const CPlane *qPlane, SLONG VonCity, SLONG NachCity) {
    return (qPlane->ptPassagiere * Cities.CalcDistance(VonCity, NachCity) / 1000 / 40);
}

BotPlaner::BotPlaner(PLAYER &player, const CPlanes &planes, JobOwner jobOwner, std::vector<int> intJobSource)
    : qPlayer(player), mJobOwner(jobOwner), mIntJobSource(std::move(intJobSource)), qPlanes(planes) {
    if (jobOwner == JobOwner::International) {
        for (const auto &i : mIntJobSource) {
            if (i < 0 || i >= AuslandsAuftraege.size()) {
                redprintf("BotPlaner::BotPlaner(): Invalid intJobSource given: %d", i);
                mJobOwner = JobOwner::Player;
            }
        }
        if (mIntJobSource.empty()) {
            redprintf("BotPlaner::BotPlaner(): No intJobSource given.");
            mJobOwner = JobOwner::Player;
        }
    } else if (jobOwner == JobOwner::InternationalFreight) {
        for (const auto &i : mIntJobSource) {
            if (i < 0 || i >= AuslandsFrachten.size()) {
                redprintf("BotPlaner::BotPlaner(): Invalid intJobSource given.");
                mJobOwner = JobOwner::PlayerFreight;
            }
        }
        if (mIntJobSource.empty()) {
            redprintf("BotPlaner::BotPlaner(): No intJobSource given.");
            mJobOwner = JobOwner::Player;
        }
    } else {
        if (!mIntJobSource.empty()) {
            redprintf("BotPlaner::BotPlaner(): intJobSource does not need to be given for this job source.");
        }
    }
}

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
        std::cout << ">>> Elapsed time findFlightPlan(): " << delta << " microseconds" << std::endl;
    }
#endif

    if (bestPremiumIdx == -1) {
        return {0};
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
                startCity = Cities.find(startCity);
                destCity = Cities.find(destCity);
                assert(startCity >= 0 && startCity < Cities.AnzEntries());
                assert(destCity >= 0 && destCity < Cities.AnzEntries());
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
        Solution bestSolution{0};
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

bool BotPlaner::applySolution(int planeId, const BotPlaner::Solution &solution, std::vector<FlightJob> &jobList) {
    assert(planeId >= 0x1000000 && qPlanes.IsInAlbum(planeId));

    for (auto &iter : solution.jobs) {
        auto &job = jobList[iter.jobIdx];
        const auto &startTime = iter.start;
        const auto &endTime = iter.end;

        /* take jobs that have not been taken yet */
        if (!job.wasTaken()) {
            int outAuftragsId = -1;
            switch (job.owner) {
            case JobOwner::TravelAgency:
                GameMechanic::takeFlightJob(qPlayer, job.id, outAuftragsId);
                break;
            case JobOwner::LastMinute:
                GameMechanic::takeLastMinuteJob(qPlayer, job.id, outAuftragsId);
                break;
            case JobOwner::Freight:
                GameMechanic::takeFreightJob(qPlayer, job.id, outAuftragsId);
                break;
            case JobOwner::International:
                assert(job.sourceId != -1);
                GameMechanic::takeInternationalFlightJob(qPlayer, job.sourceId, job.id, outAuftragsId);
                break;
            case JobOwner::InternationalFreight:
                assert(job.sourceId != -1);
                GameMechanic::takeInternationalFreightJob(qPlayer, job.sourceId, job.id, outAuftragsId);
                break;
            default:
                redprintf("BotPlaner::applySolution(): Unknown job source!");
                return false;
            }

            if (outAuftragsId < 0) {
                redprintf("BotPlaner::applySolution(): GameMechanic returned error when trying to take job!");
                return false;
            }
            job.id = outAuftragsId;
        }

        /* plan taken jobs */
#ifdef PRINT_OVERALL
        hprintf("BotPlaner::applySolution(): Plane %s: Scheduling job (%s) at %s %d, landing %s %d", (LPCTSTR)qPlanes[planeId].Name,
                Helper::getJobName(job.auftrag).c_str(), (LPCTSTR)Helper::getWeekday(startTime.getDate()), startTime.getHour(),
                (LPCTSTR)Helper::getWeekday(endTime.getDate()), endTime.getHour());
#endif
        if (job.isFreight()) {
            if (!GameMechanic::planFreightJob(qPlayer, planeId, job.id, startTime.getDate(), startTime.getHour())) {
                redprintf("BotPlaner::applySolution(): GameMechanic::planFreightJob returned error!");
                return false;
            }
        } else {
            if (!GameMechanic::planFlightJob(qPlayer, planeId, job.id, startTime.getDate(), startTime.getHour())) {
                redprintf("BotPlaner::applySolution(): GameMechanic::planFlightJob returned error!");
                return false;
            }
        }

        /* check flight time */
        bool found = false;
        const auto &qFlightPlan = qPlanes[planeId].Flugplan.Flug;
        for (SLONG d = 0; d < qFlightPlan.AnzEntries(); d++) {
            const auto &flug = qFlightPlan[d];
            if (flug.ObjectType != 2) {
                continue;
            }
            if (flug.ObjectId != job.id) {
                continue;
            }

            found = true;

            if (PlaneTime(flug.Startdate, flug.Startzeit) != startTime) {
                redprintf("BotPlaner::applySolution(): Plane %s, schedule entry %ld: GameMechanic scheduled job (%s) at different start time (%s %d instead of "
                          "%s %d)!",
                          (LPCTSTR)qPlanes[planeId].Name, d, Helper::getJobName(job.auftrag).c_str(), (LPCTSTR)Helper::getWeekday(flug.Startdate),
                          flug.Startzeit, (LPCTSTR)Helper::getWeekday(startTime.getDate()), startTime.getHour());
            }
            if ((PlaneTime(flug.Landedate, flug.Landezeit) + kDurationExtra) != endTime) {
                redprintf("BotPlaner::applySolution(): Plane %s, schedule entry %ld: GameMechanic scheduled job (%s) with different landing time (%s %d "
                          "instead of %s %d)!",
                          (LPCTSTR)qPlanes[planeId].Name, d, Helper::getJobName(job.auftrag).c_str(), (LPCTSTR)Helper::getWeekday(flug.Landedate),
                          flug.Landezeit, (LPCTSTR)Helper::getWeekday(endTime.getDate()), endTime.getHour());
            }
        }
        if (!found) {
            redprintf("BotPlaner::applySolution(): Did not find job %s in flight plan!", Helper::getJobName(job.auftrag).c_str());
        }
    }
    return true;
}

int BotPlaner::planFlights(const std::vector<int> &planeIdsInput) {
    hprintf("BotPlaner::planFlights(): Current time: %d", Sim.GetHour());

    /* kill existing flight plans */
    std::vector<int> planeIds;
    for (auto i : planeIdsInput) {
        if (qPlanes.IsInAlbum(i) != 0) {
            GameMechanic::killFlightPlanFrom(qPlayer, i, Sim.Date, Sim.GetHour() + kAvailTimeExtra);
            planeIds.push_back(i);
        }
    }

    /* prepare list of jobs */
    std::vector<FlightJob> jobList;
    std::vector<JobSource> sources;
    if (mJobOwner == JobOwner::TravelAgency) {
        sources.emplace_back(&ReisebueroAuftraege, JobOwner::TravelAgency);
    } else if (mJobOwner == JobOwner::LastMinute) {
        sources.emplace_back(&LastMinuteAuftraege, JobOwner::LastMinute);
    } else if (mJobOwner == JobOwner::Freight) {
        sources.emplace_back(&gFrachten, JobOwner::Freight);
    } else if (mJobOwner == JobOwner::International) {
        for (const auto &i : mIntJobSource) {
            sources.emplace_back(&AuslandsAuftraege[i], JobOwner::International);
            sources.back().sourceId = i;
        }
    } else if (mJobOwner == JobOwner::InternationalFreight) {
        for (const auto &i : mIntJobSource) {
            sources.emplace_back(&AuslandsFrachten[i], JobOwner::InternationalFreight);
            sources.back().sourceId = i;
        }
    }

    /* jobs already in planer */
    sources.emplace_back(&qPlayer.Auftraege, JobOwner::Player);

    /* freight jobs already in planer */
    sources.emplace_back(&qPlayer.Frachten, JobOwner::PlayerFreight);

    /* create list of open jobs */
    for (const auto &source : sources) {
        for (int i = 0; source.jobs && i < source.jobs->AnzEntries(); i++) {
            if (source.jobs->IsInAlbum(i) == 0) {
                continue;
            }
            if (source.jobs->at(i).InPlan != 0) {
                continue;
            }
            if (source.jobs->at(i).Date >= Sim.Date + kScheduleForNextDays) {
                continue;
            }
            jobList.emplace_back(source.jobs->GetIdFromIndex(i), source.sourceId, source.jobs->at(i), source.owner);
        }
        /* job source B: Freight */
        for (int i = 0; source.freight && i < source.freight->AnzEntries(); i++) {
            if (source.freight->IsInAlbum(i) == 0) {
                continue;
            }
            if (source.freight->at(i).InPlan != 0) {
                continue;
            }
            if (source.freight->at(i).Date >= Sim.Date + kScheduleForNextDays) {
                continue;
            }
            // jobList.emplace_back(source.jobs->GetIdFromIndex(i), source.sourceId, source.freight->at(i), source.owner);
        }
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
        std::tie(planeState.availTime, planeState.availCity) = Helper::getPlaneAvailableTimeLoc(qPlanes[i]);
        planeState.availCity = Cities.find(planeState.availCity);
        assert(planeState.availCity >= 0 && planeState.availCity < Cities.AnzEntries());
        // #ifdef PRINT_OVERALL
        hprintf("BotPlaner::planFlights(): Plane %s is in %s @ %s %d", (LPCTSTR)qPlanes[i].Name, (LPCTSTR)Cities[planeState.availCity].Kuerzel,
                (LPCTSTR)Helper::getWeekday(planeState.availTime.getDate()), planeState.availTime.getHour());
        // #endif
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
