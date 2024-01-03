#include "StdAfx.h"

#include <climits>
#include <iostream>
#include <vector>

struct State {
    bool visited{false};
    int outIdx{0};
    int cameFrom{-1};
};
struct Solution {
    int totalCost{0};
    std::vector<int> nodeIDs;
};

void printAuftrag(CString str, const CAuftrag &qAuftrag) {
    const char *buf = (qAuftrag.Date == qAuftrag.BisDate ? StandardTexte.GetS(TOKEN_SCHED, 3010 + (qAuftrag.Date + Sim.StartWeekday) % 7)
                                                         : StandardTexte.GetS(TOKEN_SCHED, 3010 + (qAuftrag.BisDate + Sim.StartWeekday) % 7));
    CString strDist(Einheiten[EINH_KM].bString(Cities.CalcDistance(qAuftrag.VonCity, qAuftrag.NachCity) / 1000));
    CString strPraemie(Insert1000erDots(qAuftrag.Strafe));
    CString strStrafe(Insert1000erDots(qAuftrag.Praemie));
    hprintf("%s%s - %s (%li, %s, %s, P: %s $, S: %s $)", (LPCTSTR)str, (LPCTSTR)Cities[qAuftrag.VonCity].Kuerzel, (LPCTSTR)Cities[qAuftrag.NachCity].Kuerzel,
            qAuftrag.Personen, (LPCTSTR)strDist, buf, (LPCTSTR)strPraemie, (LPCTSTR)strStrafe);
}

bool Bot::planFlightsForPlane(PLAYER &qPlayer, SLONG planeId) {
    hprintf("Bot::planFlightsForPlane: Start.");
    if (qPlayer.Planes.IsInAlbum(planeId) == 0) {
        hprintf("Bot::planFlightsForPlane: Invalid plane ID.");
        return false;
    }

    auto &qPlane = qPlayer.Planes[planeId];
    CFlugplan &qPlan = qPlane.Flugplan;

    hprintf("Bot::planFlightsForPlane: Planning for plane %s (%li)", (LPCTSTR)qPlane.Name, planeId);

    /* kill current flight plan and determine when and where the plane will be available */
    GameMechanic::killFlightPlan(qPlayer, planeId);
    SLONG availDate = Sim.Date;
    SLONG availTime = Sim.GetHour() + 2;
    SLONG availPlace = qPlane.Ort;
    for (SLONG c = qPlan.Flug.AnzEntries() - 1; c >= 0; c--) {
        if (qPlan.Flug[c].ObjectType == 0) {
            continue;
        }
        availDate = qPlan.Flug[c].Landedate;
        availTime = qPlan.Flug[c].Landezeit + 1;
        availPlace = qPlan.Flug[c].NachCity;
        break;
    }
    hprintf("Bot::planFlightsForPlane: Plane is in %s @ %li %li", (LPCTSTR)Cities[availPlace].Kuerzel, availDate, availTime);

    /* gather current open flights and print to log */
    std::vector<std::pair<int, CAuftrag>> openJobs;
    for (SLONG i = 0; i < qPlayer.Auftraege.AnzEntries(); i++) {
        if (!qPlayer.Auftraege.IsInAlbum(i)) {
            continue;
        }

        auto &qAuftrag = qPlayer.Auftraege[i];
        if (qAuftrag.Personen > qPlane.ptPassagiere) {
            continue;
        } else if (Cities.CalcDistance(qAuftrag.VonCity, qAuftrag.NachCity) > qPlane.ptReichweite * 1000) {
            continue;
        }

        openJobs.emplace_back(i, qAuftrag);

        printAuftrag(CString(), qAuftrag);
    }

    if (openJobs.size() < 2) {
        return false;
    }

    /* prepare graph */
    int nJobs = openJobs.size();
    int nNodes = nJobs + 1;
    std::vector<std::vector<int>> adjMatrix(nNodes);
    for (int i = 0; i < nNodes; i++) {
        adjMatrix[i].resize(nNodes);
        for (int j = 0; j < nNodes; j++) {
            auto from = (i == 0) ? availPlace : openJobs[i - 1].second.NachCity;
            auto to = (j == 0) ? availPlace : openJobs[j - 1].second.VonCity;
            adjMatrix[i][j] = CalculateFlightCostNoTank(from, to, qPlane.ptVerbrauch, qPlane.ptGeschwindigkeit);
        }
    }

    /* simple backtracing algo */
    int nVisited = 0;
    int currentCost = 0;
    int currentNode = 0;
    std::vector<State> nodeState(nNodes);
    std::vector<Solution> solutions;
    int bestCost = INT_MAX;
    int bestSolutionIdx = -1;
    while (currentNode >= 0) {
        auto &state = nodeState[currentNode];
        if (state.visited == 0) {
            state.visited = 1;
            nVisited++;
        }

        /* print current path */
        {
            int n = currentNode;
            while (n >= 0) {
                if (n == 0) {
                    std::cout << "STARTFLUGHFAFEN";
                } else {
                    std::cout << (LPCTSTR)Cities[openJobs[n - 1].second.VonCity].Kuerzel, (LPCTSTR)Cities[openJobs[n - 1].second.NachCity].Kuerzel;
                }
                std::cout << "   ";
                n = nodeState[n].cameFrom;
            }
            std::cout << std::endl;
        }

        /* find next node to visit */
        while (state.outIdx < nNodes) {
            if (state.outIdx != currentNode && !nodeState[state.outIdx].visited) {
                break;
            }
            ++state.outIdx;
        }

        if (state.outIdx < nNodes) {
            /* visit next node */
            int nextNode = state.outIdx++;
            currentCost += adjMatrix[currentNode][nextNode];
            nodeState[nextNode].cameFrom = currentNode;
            currentNode = nextNode;
        } else {
            /* no nodes left to visit, back-track */

            if (currentNode == 0) {
                /* we are back at the start, we are done here */
                break;
            }

            if (nVisited == nNodes) {
                Solution solution;
                solution.totalCost = currentCost;

                /* reconstruct path */
                int n = currentNode;
                while (n >= 0) {
                    solution.nodeIDs.push_back(n);
                    n = nodeState[n].cameFrom;
                }
                solutions.push_back(std::move(solution));

                if (solutions.back().totalCost < bestCost) {
                    bestSolutionIdx = solutions.size() - 1;
                    bestCost = solutions.back().totalCost;
                }

                hprintf("Found solution with cost %d", solutions.back().totalCost);
                for (int i = solutions.back().nodeIDs.size() - 1; i >= 0; i--) {
                    int nID = solutions.back().nodeIDs[i];
                    if (nID == 0) {
                        hprintf(">>> STARTFLUGHFAFEN");
                    } else {
                        printAuftrag(CString(">>> "), openJobs[nID - 1].second);
                    }
                }
            }

            currentCost -= adjMatrix[state.cameFrom][currentNode];
            assert(state.visited == 1);
            state.visited = 0;
            state.outIdx = 0;
            nVisited--;
            currentNode = state.cameFrom;
        }
    }
    if (bestSolutionIdx == -1) {
        hprintf("Could not find any solution!");
        return false;
    }

    /* apply best solution */
    const auto &bestSolution = solutions[bestSolutionIdx];
    for (int i = bestSolution.nodeIDs.size() - 1; i >= 0; i--) {
        int nID = bestSolution.nodeIDs[i];
        if (nID == 0) {
            continue;
        }

        const auto &qAuftrag = openJobs[nID - 1];
        if (availPlace != qAuftrag.second.VonCity) {
            /* auto flight */
            availTime += Cities.CalcFlugdauer(availPlace, qAuftrag.second.VonCity, qPlane.ptGeschwindigkeit) + 1;
            if (availTime >= 24) {
                availDate += 1;
                availTime -= 24;
            }
            availPlace = qAuftrag.second.VonCity;
        }

        if (!GameMechanic::planFlightJob(qPlayer, planeId, qAuftrag.first, availDate, availTime)) {
            hprintf("GameMechanic::planFlightJob returned error!");
            return false;
        }
        availTime += Cities.CalcFlugdauer(qAuftrag.second.VonCity, qAuftrag.second.NachCity, qPlane.ptGeschwindigkeit) + 1;
        if (availTime >= 24) {
            availDate += 1;
            availTime -= 24;
        }
        availPlace = qAuftrag.second.NachCity;
    }

    hprintf("Plane scheduled until %s @ %li %li", (LPCTSTR)Cities[availPlace].Kuerzel, availDate, availTime);

    return true;
}