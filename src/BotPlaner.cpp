#include "StdAfx.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <unordered_map>

/////////////////////
// BEGIN
/////////////////////

// #define PRINT_DETAIL 1
// #define PRINT_OVERALL 1

static const int kMinPremium = 1000;
static const int kMaxTimesVisited = 3;
static const bool kCanDropJobs = false;

BotPlaner::BotPlaner(PLAYER &player, const CPlanes &planes, JobOwner jobOwner, std::vector<int> intJobSource)
    : qPlayer(player), mJobOwner(jobOwner), mIntJobSource(std::move(intJobSource)), qPlanes(planes) {
    if (jobOwner == JobOwner::International) {
        for (const auto &i : mIntJobSource) {
            if (i < 0 || i >= AuslandsAuftraege.size()) {
                hprintf("BotPlaner::BotPlaner(): Invalid intJobSource given: %d", i);
                mJobOwner = JobOwner::Player;
            }
        }
        if (mIntJobSource.empty()) {
            hprintf("BotPlaner::BotPlaner(): No intJobSource given.");
            mJobOwner = JobOwner::Player;
        }
    } else if (jobOwner == JobOwner::InternationalFreight) {
        for (const auto &i : mIntJobSource) {
            if (i < 0 || i >= AuslandsFrachten.size()) {
                hprintf("BotPlaner::BotPlaner(): Invalid intJobSource given.");
                mJobOwner = JobOwner::PlayerFreight;
            }
        }
        if (mIntJobSource.empty()) {
            hprintf("BotPlaner::BotPlaner(): No intJobSource given.");
            mJobOwner = JobOwner::Player;
        }
    } else {
        if (!mIntJobSource.empty()) {
            hprintf("BotPlaner::BotPlaner(): intJobSource does not need to be given for this job source.");
        }
    }
}

void BotPlaner::printJob(const CAuftrag &qAuftrag) {
    const char *buf = (qAuftrag.Date == qAuftrag.BisDate ? StandardTexte.GetS(TOKEN_SCHED, 3010 + (qAuftrag.Date + Sim.StartWeekday) % 7)
                                                         : StandardTexte.GetS(TOKEN_SCHED, 3010 + (qAuftrag.BisDate + Sim.StartWeekday) % 7));
    CString strDist(Einheiten[EINH_KM].bString(Cities.CalcDistance(qAuftrag.VonCity, qAuftrag.NachCity) / 1000));
    CString strPraemie(Insert1000erDots(qAuftrag.Strafe));
    CString strStrafe(Insert1000erDots(qAuftrag.Praemie));
    printf("%s - %s (%u, %s, %s, P: %s $, S: %s $)\n", (LPCTSTR)Cities[qAuftrag.VonCity].Kuerzel, (LPCTSTR)Cities[qAuftrag.NachCity].Kuerzel, qAuftrag.Personen,
           (LPCTSTR)strDist, buf, (LPCTSTR)strPraemie, (LPCTSTR)strStrafe);
}

void BotPlaner::printJobShort(const CAuftrag &qAuftrag) {
    printf("%s - %s", (LPCTSTR)Cities[qAuftrag.VonCity].Kuerzel, (LPCTSTR)Cities[qAuftrag.NachCity].Kuerzel);
}

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

std::pair<int, int> BotPlaner::gatherAndPlanJobs(std::vector<FlightJob> &jobList, std::vector<PlaneState> &planeStates) {
    int nPlanes = planeStates.size();

#ifdef PRINT_OVERALL
    auto t_begin = std::chrono::steady_clock::now();
#endif

    /* find number of unique plane types */
    std::vector<const CPlane *> planeTypeToPlane;
    findPlaneTypes(planeStates, planeTypeToPlane);
    int nPlaneTypes = planeTypeToPlane.size();

    /* prepare job list */
    for (int p = 0; p < nPlanes; p++) {
        auto &qPlaneState = planeStates[p];
        const auto &qPlane = qPlanes[qPlaneState.planeId];

        qPlaneState.assignedJobIds.resize(jobList.size());

        /* filter jobs that exceed range / seats */
        int eligibleJobs = 0;
        for (int j = 0; j < jobList.size(); j++) {
            const auto &job = jobList[j].auftrag;
            if (job.Personen <= qPlane.ptPassagiere && Cities.CalcDistance(job.VonCity, job.NachCity) <= qPlane.ptReichweite * 1000) {
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
                qNodeInfo.premium = job.Praemie - CalculateFlightCostNoTank(job.VonCity, job.NachCity, planeTypeToPlane[pt]->ptVerbrauch,
                                                                            planeTypeToPlane[pt]->ptGeschwindigkeit);
                qNodeInfo.duration = Cities.CalcFlugdauer(job.VonCity, job.NachCity, planeTypeToPlane[pt]->ptGeschwindigkeit);
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
                    g.adjMatrix[i][j].cost =
                        CalculateFlightCostNoTank(startCity, destCity, planeTypeToPlane[pt]->ptVerbrauch, planeTypeToPlane[pt]->ptGeschwindigkeit);
                    g.adjMatrix[i][j].duration = Cities.CalcFlugdauer(startCity, destCity, planeTypeToPlane[pt]->ptGeschwindigkeit);
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
    std::cout << "Improved gain by " << totalDelta << std::endl;
#endif

    for (int p = 0; p < nPlanes; p++) {
        auto &planeState = planeStates[p];

        auto &bestSolution = planeState.currentSolution;
        if (bestSolution.totalPremium < kMinPremium) {
            continue;
        }

        applySolution(p, bestSolution, jobList);

#ifdef PRINT_DETAIL
        const auto &qPlane = qPlanes[planeState.planeId];
        std::cout << "Schedule of plane " << qPlane.Name << std::endl;
        printSolution(bestSolution, jobList);
#endif
    }
    // printGraph(planeStates, jobList, graphs[0]);

    return {nJobsScheduled, totalDelta};
}

bool BotPlaner::applySolution(int planeId, const BotPlaner::Solution &solution, std::vector<FlightJob> &jobList) {
    for (auto &i : solution.jobs) {
        auto &job = jobList[i.first];
        const auto &time = i.second;

        /* take jobs that have not been taken yet */
        if (job.owner != JobOwner::Player && job.owner != JobOwner::PlayerFreight) {
            int outAuftragsId = -1;
            if (job.owner == JobOwner::TravelAgency) {
                GameMechanic::takeFlightJob(qPlayer, job.id, outAuftragsId);
                job.owner = JobOwner::Player;
            } else if (job.owner == JobOwner::LastMinute) {
                GameMechanic::takeLastMinuteJob(qPlayer, job.id, outAuftragsId);
                job.owner = JobOwner::Player;
            } else if (job.owner == JobOwner::Freight) {
                GameMechanic::takeFreightJob(qPlayer, job.id, outAuftragsId);
                job.owner = JobOwner::PlayerFreight;
            } else if (job.owner == JobOwner::International) {
                assert(job.sourceId != -1);
                GameMechanic::takeInternationalFlightJob(qPlayer, job.sourceId, job.id, outAuftragsId);
                job.owner = JobOwner::Player;
            } else if (job.owner == JobOwner::InternationalFreight) {
                assert(job.sourceId != -1);
                GameMechanic::takeInternationalFreightJob(qPlayer, job.sourceId, job.id, outAuftragsId);
                job.owner = JobOwner::PlayerFreight;
            } else {
                hprintf("BotPlaner::applySolution(): Unknown job source!");
                return false;
            }
            if (outAuftragsId < 0) {
                hprintf("BotPlaner::applySolution(): GameMechanic returned error when trying to take job!");
                return false;
            }
            job.id = outAuftragsId;
        }

        /* plan taken jobs */
        if (job.owner == JobOwner::Player) {
            if (!GameMechanic::planFlightJob(qPlayer, planeId, job.id, time.getDate(), time.getHour())) {
                hprintf("BotPlaner::applySolution(): GameMechanic::planFlightJob returned error!");
                return false;
            }
        } else if (job.owner == JobOwner::PlayerFreight) {
            if (!GameMechanic::planFreightJob(qPlayer, planeId, job.id, time.getDate(), time.getHour())) {
                hprintf("BotPlaner::applySolution(): GameMechanic::planFreightJob returned error!");
                return false;
            }
        }
    }
    return true;
}

int BotPlaner::planFlights(const std::vector<int> &planeIds) {
    /* prepare list of planes */
    std::vector<PlaneState> planeStates;
    planeStates.reserve(planeIds.size());
    for (auto i : planeIds) {
        if (qPlanes.IsInAlbum(i) == 0) {
            continue;
        }

        planeStates.push_back({});
        auto &planeState = planeStates.back();
        planeState.planeId = i;

        GameMechanic::killFlightPlan(qPlayer, i);

        const auto qPlane = qPlanes[i];
        const CFlugplan &qPlan = qPlane.Flugplan;

        /* determine when and where the plane will be available */
        planeState.availTime = {Sim.Date, Sim.GetHour() + 2};
        planeState.availCity = qPlane.Ort;
        for (int c = qPlan.Flug.AnzEntries() - 1; c >= 0; c--) {
            if (qPlan.Flug[c].ObjectType == 0) {
                continue;
            }
            planeState.availTime = {qPlan.Flug[c].Landedate, qPlan.Flug[c].Landezeit + 1};
            planeState.availCity = qPlan.Flug[c].NachCity;
            break;
        }
        assert(planeState.availCity >= 0);
        hprintf("BotPlaner::planFlights(): Plane %s is in %s @ %li %li", (LPCTSTR)qPlane.Name, (LPCTSTR)Cities[planeState.availCity].Kuerzel,
                planeState.availTime.getDate(), planeState.availTime.getHour());
    }

    /* prepare list of jobs */
    std::vector<FlightJob> openJobs;
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
    sources.back().isNewJob = false;

    /* freight jobs already in planer */
    sources.emplace_back(&qPlayer.Frachten, JobOwner::PlayerFreight);
    sources.back().isNewJob = false;

    for (const auto &source : sources) {
        for (int i = 0; source.jobs && i < source.jobs->AnzEntries(); i++) {
            if (source.jobs->IsInAlbum(i) == 0) {
                continue;
            }
            if (source.isNewJob && source.jobs->at(i).InPlan != 0) {
                continue;
            }
            openJobs.emplace_back(i, source.sourceId, source.jobs->at(i), source.owner);
        }
        /* job source B: Freight */
        for (int i = 0; source.freight && i < source.freight->AnzEntries(); i++) {
            if (source.freight->IsInAlbum(i) == 0) {
                continue;
            }
            if (source.isNewJob && source.freight->at(i).InPlan != 0) {
                continue;
            }
            // openJobs.emplace_back(i, source.sourceId, source.freight->at(i), source.owner);
        }
    }

    std::sort(openJobs.begin(), openJobs.end(), [](const FlightJob &a, const FlightJob &b) {
        bool owningA = (a.owner == JobOwner::Player || a.owner == JobOwner::PlayerFreight);
        bool owningB = (b.owner == JobOwner::Player || b.owner == JobOwner::PlayerFreight);
        if (kCanDropJobs) {
            return (a.auftrag.Praemie + (owningA ? a.auftrag.Strafe : 0)) > (b.auftrag.Praemie + (owningB ? b.auftrag.Strafe : 0));
        }
        if (owningA && !owningB) {
            return true;
        }
        if (!owningA && owningB) {
            return false;
        }
        return a.auftrag.Praemie > b.auftrag.Praemie;
    });

    /* statistics of existing jobs */
    int nPreviouslyOwned = 0;
    int nNewJobs = 0;
    for (const auto &job : openJobs) {
        if (job.owner == JobOwner::Player || job.owner == JobOwner::PlayerFreight) {
            nPreviouslyOwned++;
        } else {
            nNewJobs++;
        }
    }

    /* start algo */
    int nJobsScheduled, totalGain;
    std::tie(nJobsScheduled, totalGain) = gatherAndPlanJobs(openJobs, planeStates);

    /* check statistics */
    int nPreviouslyOwnedScheduled = 0;
    int nNewJobsScheduled = 0;
    for (const auto &job : openJobs) {
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
