#include "StdAfx.h"

#include <climits>
#include <iostream>
#include <vector>

class PlaneTime {
  public:
    PlaneTime() = default;
    PlaneTime(int date, int time) : mDate(date), mTime(time) {}

    int GetDate() const { return mDate; }
    int GetHour() const { return mTime; }

    PlaneTime &operator+=(int delta) {
        mTime += delta;
        while (mTime >= 24) {
            mDate += 1;
            mTime -= 24;
        }
        return *this;
    }

    PlaneTime &operator-=(int delta) {
        mTime -= delta;
        while (mTime < 0) {
            mDate -= 1;
            mTime += 24;
        }
        return *this;
    }

  private:
    int mDate{0};
    int mTime{0};
};

struct State {
    bool visited{false};
    int outIdx{0};
    int cameFrom{-1};
};
struct Solution {
    int totalPremium{0};
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

std::vector<std::pair<int, CAuftrag>> Bot::gatherFlightsForPlane(PLAYER &qPlayer, int planeId, bool killFlightPlan) {
    if (killFlightPlan) {
        GameMechanic::killFlightPlan(qPlayer, planeId);
    }

    /* gather current open flights and print to log */
    auto &qPlane = qPlayer.Planes[planeId];
    std::vector<std::pair<int, CAuftrag>> openJobs;
    for (int i = 0; i < qPlayer.Auftraege.AnzEntries(); i++) {
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
    return openJobs;
}

bool Bot::planFlightsForPlane(PLAYER &qPlayer, int planeId) {
    hprintf("Bot::planFlightsForPlane: Start.");
    if (qPlayer.Planes.IsInAlbum(planeId) == 0) {
        hprintf("Bot::planFlightsForPlane: Invalid plane ID.");
        return false;
    }

    auto &qPlane = qPlayer.Planes[planeId];
    CFlugplan &qPlan = qPlane.Flugplan;

    hprintf("Bot::planFlightsForPlane: Planning for plane %s (%li)", (LPCTSTR)qPlane.Name, planeId);

    /* determine when and where the plane will be available */
    PlaneTime availTime{Sim.Date, Sim.GetHour() + 2};
    int availPlace = qPlane.Ort;
    for (int c = qPlan.Flug.AnzEntries() - 1; c >= 0; c--) {
        if (qPlan.Flug[c].ObjectType == 0) {
            continue;
        }
        availTime = {qPlan.Flug[c].Landedate, qPlan.Flug[c].Landezeit + 1};
        availPlace = qPlan.Flug[c].NachCity;
        break;
    }
    hprintf("Bot::planFlightsForPlane: Plane is in %s @ %li %li", (LPCTSTR)Cities[availPlace].Kuerzel, availTime.GetDate(), availTime.GetHour());

    auto openJobs = gatherFlightsForPlane(qPlayer, planeId, true);
    if (openJobs.size() < 2) {
        return false;
    }

    /* prepare graph */
    int nJobs = openJobs.size();
    int nNodes = nJobs + 1;
    std::vector<std::pair<int, int>> nodeWeight(nNodes);
    std::vector<std::vector<int>> adjMatrixCost(nNodes);
    std::vector<std::vector<int>> adjMatrixDuration(nNodes);
    for (int i = 0; i < nNodes; i++) {
        if (i != 0) {
            const auto &qAuftrag = openJobs[i - 1].second;
            nodeWeight[i] = {qAuftrag.Praemie, Cities.CalcFlugdauer(qAuftrag.VonCity, qAuftrag.NachCity, qPlane.ptGeschwindigkeit) + 1};
        }

        adjMatrixCost[i].resize(nNodes);
        adjMatrixDuration[i].resize(nNodes);
        for (int j = 0; j < nNodes; j++) {
            auto from = (i == 0) ? availPlace : openJobs[i - 1].second.NachCity;
            auto to = (j == 0) ? availPlace : openJobs[j - 1].second.VonCity;
            if (from != to) {
                adjMatrixCost[i][j] = CalculateFlightCostNoTank(from, to, qPlane.ptVerbrauch, qPlane.ptGeschwindigkeit);
                adjMatrixDuration[i][j] = Cities.CalcFlugdauer(from, to, qPlane.ptGeschwindigkeit) + 1;
            } else {
                adjMatrixCost[i][j] = 0;
                adjMatrixDuration[i][j] = 0;
            }
        }
    }

    /* simple backtracing algo */
    int currentNode = 0;
    int nVisited = 0;
    int currentPremium = 0;
    int currentCost = 0;
    PlaneTime currentTime{availTime};
    std::vector<State> nodeState(nNodes);
    std::vector<Solution> solutions;
    int bestCost = INT_MAX;
    int bestSolutionIdx = -1;
    while (currentNode >= 0) {
        auto &state = nodeState[currentNode];
        if (state.visited == 0) {
            state.visited = 1;
            nVisited++;

            currentPremium += nodeWeight[currentNode].first;
            currentTime += nodeWeight[currentNode].second;
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
            currentCost += adjMatrixCost[currentNode][nextNode];
            currentTime += adjMatrixDuration[currentNode][nextNode];
            nodeState[nextNode].cameFrom = currentNode;

            currentNode = nextNode;
        } else {
            /* no nodes left to visit, back-track */

            if (currentNode == 0) {
                /* we are back at the start, we are done here */
                break;
            }

            /* solution found */
            if (nVisited == nNodes) {
                Solution solution;
                solution.totalPremium = currentPremium;
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

            /* backtrack */
            currentCost -= adjMatrixCost[state.cameFrom][currentNode];
            currentTime -= adjMatrixDuration[state.cameFrom][currentNode];

            assert(state.visited == 1);
            state.visited = 0;
            state.outIdx = 0;
            nVisited--;

            currentPremium -= nodeWeight[currentNode].first;
            currentTime -= nodeWeight[currentNode].second;

            currentNode = state.cameFrom;
        }
    }
    if (bestSolutionIdx == -1) {
        hprintf("Could not find any solution!");
        return false;
    }

    /* apply best solution */
    const auto &bestSolution = solutions[bestSolutionIdx];
    int prevNode = -1;
    for (int i = bestSolution.nodeIDs.size() - 1; i >= 0; i--) {
        int nID = bestSolution.nodeIDs[i];
        if (nID == 0) {
            prevNode = nID;
            continue;
        }

        availTime += adjMatrixDuration[prevNode][nID];

        const auto &qAuftrag = openJobs[nID - 1];
        if (!GameMechanic::planFlightJob(qPlayer, planeId, qAuftrag.first, availTime.GetDate(), availTime.GetHour())) {
            hprintf("GameMechanic::planFlightJob returned error!");
            return false;
        }
        availTime += nodeWeight[nID].second;
        availPlace = qAuftrag.second.NachCity;

        prevNode = nID;
    }

    hprintf("Plane scheduled until %s @ %li %li", (LPCTSTR)Cities[availPlace].Kuerzel, availTime.GetDate(), availTime.GetHour());

    return true;
}