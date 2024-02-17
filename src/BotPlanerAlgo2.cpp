#include "BotPlaner.h"

#include "BotHelper.h"
#include "TeakLibW.h"

#include <chrono>
#include <iostream>

// #define PRINT_DETAIL 1
// #define PRINT_OVERALL 1

extern const int kScheduleForNextDays;

void BotPlaner::printNodeInfo(const Graph &g, int nodeIdx) const {
    (void)g;
    (void)nodeIdx;
#ifdef PRINT_DETAIL
    const auto &curInfo = g.nodeInfo[nodeIdx];
    const auto &curState = g.nodeState[nodeIdx];
    if (nodeIdx < g.nPlanes) {
        auto &qPlaneState = mPlaneStates[nodeIdx];
        const auto &qPlane = qPlanes[qPlaneState.planeId];
        hprintf("Node %d is start for plane %s, available %s %d", nodeIdx, (LPCTSTR)qPlane.Name, (LPCTSTR)Helper::getWeekday(qPlaneState.availTime.getDate()),
                qPlaneState.availTime.getHour());
    } else {
        const auto &qJob = mJobList[curInfo.jobIdx];
        if (qJob.assignedtoPlaneId != -1) {
            const auto &qPlane = qPlanes[qJob.assignedtoPlaneId];
            hprintf("Node %d is job %s scheduled for plane %s (starting %s %d)", nodeIdx, Helper::getJobName(qJob.auftrag).c_str(), (LPCTSTR)qPlane.Name,
                    (LPCTSTR)Helper::getWeekday(curState.startTime.getDate()), curState.startTime.getHour());
        } else {
            hprintf("Node %d is job %s (unscheduled)", nodeIdx, Helper::getJobName(qJob.auftrag).c_str());
        }
    }
#endif
}

bool BotPlaner::algo2ShiftLeft(Graph &g, int nodeToShift, int shiftT, bool commit) {
    if (shiftT <= 0) {
        return false;
    }
    int currentNode = nodeToShift;
    auto currentTime = g.nodeState[currentNode].startTime - shiftT;

    while (true) {
        auto &curState = g.nodeState[currentNode];
        const auto &curInfo = g.nodeInfo[currentNode];

        /* check if we can shift this node */
        if (currentNode < g.nPlanes) {
            assert(!commit);
            return false;
        }
        if (currentTime.getDate() < curInfo.earliest) {
            assert(!commit);
            return false;
        }

        /* shift node */
        if (commit) {
            curState.startTime = currentTime;
        }

        int prevNode = curState.cameFrom;
        if (prevNode == -1) {
            break;
        }

        /* calculate new start time of previous node */
        currentTime -= g.adjMatrix[prevNode][currentNode].duration;
        currentTime -= g.nodeInfo[prevNode].duration;

        /* check if we have to move previous node as well */
        if (currentTime >= g.nodeState[prevNode].startTime) {
            break;
        }

        currentNode = prevNode;
    }
    return true;
}

bool BotPlaner::algo2ShiftRight(Graph &g, int nodeToShift, int shiftT, bool commit) {
    if (shiftT <= 0) {
        return false;
    }
    int currentNode = nodeToShift;
    auto currentTime = g.nodeState[currentNode].startTime + shiftT;

    while (true) {
        auto &curState = g.nodeState[currentNode];
        const auto &curInfo = g.nodeInfo[currentNode];

        /* check if we can shift this node */
        if (currentTime.getDate() > Sim.Date + kScheduleForNextDays) {
            assert(!commit);
            return false;
        }
        if (currentTime.getDate() > curInfo.latest) {
            assert(!commit);
            return false;
        }

        /* shift node */
        if (commit) {
            curState.startTime = currentTime;
        }

        int nextNode = curState.nextNode;
        if (nextNode == -1) {
            break;
        }
        auto &nextState = g.nodeState[nextNode];

        /* calculate new start time of next node */
        currentTime += curInfo.duration;
        currentTime += g.adjMatrix[currentNode][nextNode].duration;
        assert(g.adjMatrix[currentNode][nextNode].duration >= 0);

        /* check if we have to move next node as well */
        if (currentTime <= nextState.startTime) {
            break;
        }

        currentNode = nextNode;
    }
    return true;
}

bool BotPlaner::algo2MakeRoom(Graph &g, int nodeToMoveLeft, int nodeToMoveRight) {
    assert(nodeToMoveLeft >= g.nPlanes || nodeToMoveRight >= g.nPlanes);

    int shiftLeft = 1;
    int shiftRight = 1;
    if (nodeToMoveLeft >= g.nPlanes) {
        while (shiftLeft < 24 && algo2ShiftLeft(g, nodeToMoveLeft, shiftLeft, false)) {
            shiftLeft++;
        }
    }
    if (nodeToMoveRight >= g.nPlanes) {
        while (shiftRight < 24 && algo2ShiftRight(g, nodeToMoveRight, shiftRight, false)) {
            shiftRight++;
        }
    }
    if (shiftLeft == 1 && shiftRight == 1) {
        return false;
    }

#ifdef PRINT_DETAIL
    if (nodeToMoveLeft >= g.nPlanes && nodeToMoveRight >= g.nPlanes) {
        const auto &prevJob = mJobList[g.nodeInfo[nodeToMoveLeft].jobIdx].auftrag;
        const auto &job = mJobList[g.nodeInfo[nodeToMoveRight].jobIdx].auftrag;
        std::cout << "Make room between " << Helper::getJobName(prevJob).c_str() << " and " << Helper::getJobName(job).c_str() << std::endl;
    } else if (nodeToMoveLeft >= g.nPlanes) {
        const auto &prevJob = mJobList[g.nodeInfo[nodeToMoveLeft].jobIdx].auftrag;
        std::cout << "Make room after " << Helper::getJobName(prevJob).c_str() << std::endl;
    } else {
        const auto &job = mJobList[g.nodeInfo[nodeToMoveRight].jobIdx].auftrag;
        std::cout << "Make room before " << Helper::getJobName(job).c_str() << std::endl;
    }
    std::cout << "Maximum shift: " << shiftLeft - 1 << ", " << shiftRight - 1 << std::endl;
#endif

    algo2ShiftLeft(g, nodeToMoveLeft, shiftLeft - 1, true);
    algo2ShiftRight(g, nodeToMoveRight, shiftRight - 1, true);
    return true;
}

void BotPlaner::algo2ApplySolutionToGraph(std::vector<Graph> &graphs) {
    for (int p = 0; p < mPlaneStates.size(); p++) {
        auto &qPlaneState = mPlaneStates[p];
        const auto &qPlane = qPlanes[qPlaneState.planeId];
        auto pt = qPlaneState.planeTypeId;
        auto &g = graphs[pt];
        const auto &qFlightPlan = qPlane.Flugplan.Flug;
        int autoFlightDuration = -1;

        qPlaneState.currentNode = p;

        for (int d = 0; d < qFlightPlan.AnzEntries(); d++) {
            const auto &qFPE = qFlightPlan[d];
            if (qFPE.ObjectType <= 0) {
                break;
            }

            auto planeTime = PlaneTime{qFPE.Startdate, qFPE.Startzeit};
            if (planeTime < qPlaneState.availTime) {
                continue;
            }

            if (qFPE.ObjectType == 2) {
                auto it = mExistingJobsById.find(qFPE.ObjectId);
                if (it == mExistingJobsById.end()) {
                    redprintf("BotPlaner::planFlights(): Unknown job in flight plan: %ld", qFPE.ObjectId);
                    continue;
                }
                int currentNode = qPlaneState.currentNode;
                int nextNode = it->second + g.nPlanes;

                /* check duration of any previous automatic flight */
                if (autoFlightDuration != -1 && (autoFlightDuration + kDurationExtra) != g.adjMatrix[currentNode][nextNode].duration) {
                    redprintf("BotPlaner::planFlights(): Duration of automatic flight does not match before FPE: %ld", qFPE.ObjectId);
                }

                qPlaneState.currentTime = planeTime;
                if (autoFlightDuration != -1) {
                    qPlaneState.currentTime -= (autoFlightDuration + kDurationExtra);
                }
                algo2AdvanceToNode(g, p, nextNode);

                autoFlightDuration = -1;
            } else if (qFPE.ObjectType == 3) {
                assert(autoFlightDuration == -1);
                autoFlightDuration = 24 * (qFPE.Landedate - qFPE.Startdate) + (qFPE.Landezeit - qFPE.Startzeit);
            } /* TODO: Fracht */
        }
    }
}

void BotPlaner::algo2GenSolutionsFromGraph(std::vector<Graph> &graphs, int p) {
    auto &planeState = mPlaneStates[p];
    auto pt = planeState.planeTypeId;
    auto &g = graphs[pt];

    int node = g.nodeState[p].nextNode;
    if (node == -1) {
        return;
    }
    planeState.currentSolution = {planeState.currentPremium - planeState.currentCost};
    auto &qJobList = planeState.currentSolution.jobs;
    while (node != -1) {
        int jobIdx = g.nodeInfo[node].jobIdx;
        assert(p == mJobList[jobIdx].assignedtoPlaneId);

        qJobList.emplace_front(jobIdx, g.nodeState[node].startTime, g.nodeState[node].startTime + g.nodeInfo[node].duration);
        printNodeInfo(g, node);

        node = g.nodeState[node].nextNode;
    }
}

int BotPlaner::algo2FindNext(const Graph &g, PlaneState &planeState, int choice) const {
    auto currentNode = planeState.currentNode;
    const auto &curInfo = g.nodeInfo[currentNode];

#ifdef PRINT_DETAIL
    std::cout << "algo2FindNext(): planeState.currentNode = ";
    printNodeInfo(g, planeState.currentNode);
    std::cout << "algo2FindNext(): planeState.currentTime = " << Helper::getWeekday(planeState.currentTime.getDate()) << planeState.currentTime.getHour()
              << std::endl;
#endif

    /* determine next node for plane */
    int outIdx = 0;
    while (outIdx < curInfo.bestNeighbors.size()) {
        int nextNode = curInfo.bestNeighbors[outIdx];
        assert(nextNode >= g.nPlanes && nextNode < g.nNodes);
        const auto &nextInfo = g.nodeInfo[nextNode];

#ifdef PRINT_DETAIL
        std::cout << std::endl;
        std::cout << "??? Now examining node: ";
        printNodeInfo(g, nextNode);
#endif

        int jobIdx = nextInfo.jobIdx;
        if (mJobList[jobIdx].assignedtoPlaneId != -1) {
            outIdx++;
#ifdef PRINT_DETAIL
            std::cout << "??? Already assigned to plane " << mJobList[jobIdx].assignedtoPlaneId << std::endl;
#endif
            continue; /* job already assigned */
        }

        auto tEarliest = planeState.currentTime + g.adjMatrix[currentNode][nextNode].duration;
#ifdef PRINT_DETAIL
        std::cout << "??? tEarliest = " << Helper::getWeekday(tEarliest.getDate()) << tEarliest.getHour() << std::endl;
#endif
        if (tEarliest.getDate() > nextInfo.latest) {
            outIdx++;
#ifdef PRINT_DETAIL
            std::cout << "??? we are too late" << std::endl;
#endif
            continue; /* we are too late */
        }

        if (tEarliest.getDate() < nextInfo.earliest) {
            tEarliest.setDate(nextInfo.earliest);
        }

#ifdef PRINT_DETAIL
        std::cout << "??? tEarliest (job not before) = " << Helper::getWeekday(tEarliest.getDate()) << tEarliest.getHour() << std::endl;
#endif

        /* if the current node already has a successor, then we need to check
         * whether the new node can be inserted inbetween.
         * We must not shift it by even one 1 hour since the makeRoom function
         * already pushed everything as far back as possible. */
        int overnextNode = g.nodeState[currentNode].nextNode;
        if (overnextNode != -1) {
            auto tEarliestOvernext = tEarliest + nextInfo.duration;
            int duration = g.adjMatrix[nextNode][overnextNode].duration;
            tEarliestOvernext += duration;
#ifdef PRINT_DETAIL
            std::cout << "??? overnextNode = ";
            printNodeInfo(g, overnextNode);
            std::cout << "??? tEarliestOvernext = " << Helper::getWeekday(tEarliestOvernext.getDate()) << tEarliestOvernext.getHour() << std::endl;
            std::cout << "??? duration = " << duration << std::endl;
#endif
            if (duration < 0 || tEarliestOvernext > g.nodeState[overnextNode].startTime) {
                outIdx++;
#ifdef PRINT_DETAIL
                std::cout << "??? no room for auto flight to next node" << std::endl;
#endif
                continue; /* no room */
            }
        }

        if (choice-- == 0) {
            break;
        }
    }
    if (outIdx == curInfo.bestNeighbors.size()) {
        return -1;
    }

#ifdef PRINT_DETAIL
    std::cout << "??? Select node: " << curInfo.bestNeighbors[outIdx] << std::endl << std::endl;
    printNodeInfo(g, curInfo.bestNeighbors[outIdx]);
#endif
    return curInfo.bestNeighbors[outIdx];
}

int BotPlaner::algo2AdvanceToNode(Graph &g, int planeIdx, int nextNode) {
    assert(nextNode >= g.nPlanes && nextNode < g.nNodes);
    int gain = 0;

    auto &qPlaneState = mPlaneStates[planeIdx];
    auto currentNode = qPlaneState.currentNode;
    auto &curState = g.nodeState[currentNode];
    const auto &nextInfo = g.nodeInfo[nextNode];
    auto &nextState = g.nodeState[nextNode];

    /* edge */
    qPlaneState.currentCost += g.adjMatrix[currentNode][nextNode].cost;
    gain -= g.adjMatrix[currentNode][nextNode].cost;
    qPlaneState.currentTime += g.adjMatrix[currentNode][nextNode].duration;
    assert(g.adjMatrix[currentNode][nextNode].duration >= 0);

    /* job premium */
    qPlaneState.currentPremium += nextInfo.premium;
    gain += nextInfo.premium;

    /* job duration */
    if (qPlaneState.currentTime.getDate() < nextInfo.earliest) {
        qPlaneState.currentTime.setDate(nextInfo.earliest);
    }
    nextState.startTime = qPlaneState.currentTime;
    qPlaneState.currentTime += nextInfo.duration;

    /* assign job */
    assert(mJobList[nextInfo.jobIdx].assignedtoPlaneId == -1);
    mJobList[nextInfo.jobIdx].assignedtoPlaneId = planeIdx;

    /* did current already have a successor? This will now become the overnext node */
    int overnextNode = curState.nextNode;
    if (overnextNode != -1) {
        nextState.nextNode = overnextNode;
        g.nodeState[overnextNode].cameFrom = nextNode;
    }

#ifdef PRINT_DETAIL
    if (overnextNode != -1) {
        std::cout << ">>> Node " << nextNode << " is inserted after " << currentNode << " and before " << overnextNode << std::endl;
    } else {
        std::cout << ">>> Node " << nextNode << " is inserted after " << currentNode << " and is the last node" << std::endl;
    }
    printNodeInfo(g, currentNode);
    printNodeInfo(g, nextNode);
    if (overnextNode != -1) {
        printNodeInfo(g, overnextNode);
    }
#endif

    curState.nextNode = nextNode;
    nextState.cameFrom = currentNode;

    qPlaneState.currentNode = nextNode;
    qPlaneState.numNodes++;

#ifdef PRINT_DETAIL
    std::cout << ">>> Advanced to node " << nextNode << ": " << std::endl;
    printNodeInfo(g, nextNode);
    std::cout << ">>> currentPremium: " << qPlaneState.currentPremium << std::endl;
    std::cout << ">>> currentCost: " << qPlaneState.currentCost << std::endl;
    std::cout << ">>> currentTime: " << Helper::getWeekday(qPlaneState.currentTime.getDate()) << qPlaneState.currentTime.getHour() << std::endl;
    std::cout << std::endl;
#endif

    return gain; /* note: does not consider cost of potential automatic flights _after_ inserted node */
}

bool BotPlaner::algo2Backtrack(const Graph &g, PlaneState &qPlaneState) {
    auto currentNode = qPlaneState.currentNode;
    const auto &curInfo = g.nodeInfo[currentNode];
    auto &curState = g.nodeState[currentNode];

    /* go one node back */
    int prevNode = curState.cameFrom;
    if (prevNode < 0) {
        return false;
    }

    /* job premium */
    qPlaneState.currentPremium -= curInfo.premium;

    /* edge cost */
    qPlaneState.currentCost -= g.adjMatrix[prevNode][currentNode].cost;
    assert(g.adjMatrix[prevNode][currentNode].duration >= 0);

    /* when was plane available? */
    qPlaneState.currentTime = g.nodeState[prevNode].startTime;
    qPlaneState.currentTime += g.nodeInfo[prevNode].duration;

    /* unassign job */
    assert(mJobList[curInfo.jobIdx].assignedtoPlaneId != -1);
    mJobList[curInfo.jobIdx].assignedtoPlaneId = -1;

    qPlaneState.currentNode = prevNode;
    qPlaneState.numNodes--;
    assert(qPlaneState.numNodes >= 0);

    return true;
}

std::pair<int, int> BotPlaner::algo2(std::vector<Graph> &graphs) {
#ifdef PRINT_OVERALL
    auto t_begin = std::chrono::steady_clock::now();
#endif

    for (auto &g : graphs) {
        g.resetNodes();

        for (int n = 0; n < g.nPlanes; n++) {
            g.nodeState[n].startTime = mPlaneStates[n].availTime;
        }
    }

    /* apply existing solution to graph and plane state */
    algo2ApplySolutionToGraph(graphs);

    /* main algo */
    int nPlanes = mPlaneStates.size();
    int nJobsScheduled = 0;
    int totalDelta = 0;

    bool scheduledNewJob = true;
    while (scheduledNewJob) {
        scheduledNewJob = false;

        for (int planeIdx = 0; planeIdx < nPlanes; planeIdx++) {
            auto &planeState = mPlaneStates[planeIdx];
            auto pt = planeState.planeTypeId;
            auto &g = graphs[pt];

#ifdef PRINT_OVERALL
            std::cout << "****************************************" << std::endl;
            std::cout << "Plane:  " << qPlanes[planeState.planeId].Name.c_str() << std::endl;
            std::cout << "qPlaneState.numNodes " << planeState.numNodes << std::endl;
#endif

            for (int pass = 0; pass < 3; pass++) { // TODO
                /* make room after random node */
                if (planeState.numNodes > 0) {
                    int whereToInsert = getRandInt(0, planeState.numNodes);
                    int nodeToMoveLeft = planeIdx;
                    while (whereToInsert > 0) {
                        whereToInsert--;
                        nodeToMoveLeft = g.nodeState[nodeToMoveLeft].nextNode;
                    }

                    bool success = algo2MakeRoom(g, nodeToMoveLeft, g.nodeState[nodeToMoveLeft].nextNode);

#ifdef PRINT_OVERALL
                    if (!success) {
                        std::cout << "MakeRoom failed" << std::endl;
                    } else {
                        algo2GenSolutionsFromGraph(graphs, planeIdx);
                        GameMechanic::killFlightPlanFrom(qPlayer, planeState.planeId, mScheduleFromTime.getDate(), mScheduleFromTime.getHour());
                        applySolutionForPlane(planeState.planeId, planeState.currentSolution);
                        Helper::checkPlaneSchedule(qPlayer, qPlanes[planeState.planeId], false);
                    }
#endif

                    if (!success) {
                        continue;
                    }

                    planeState.currentNode = nodeToMoveLeft;
                }

                /* when is the plane available */
                int currentNode = planeState.currentNode;
#ifdef PRINT_DETAIL
                std::cout << "planeState.currentNode " << planeState.currentNode << std::endl;
                std::cout << "g.nodeState[currentNode].startTime " << Helper::getWeekday(g.nodeState[currentNode].startTime.getDate())
                          << g.nodeState[currentNode].startTime.getHour() << std::endl;
                std::cout << "g.nodeInfo[currentNode].duration " << g.nodeInfo[currentNode].duration << std::endl;
#endif
                planeState.currentTime = g.nodeState[currentNode].startTime;
                planeState.currentTime += g.nodeInfo[currentNode].duration;
                if (planeState.currentTime < planeState.availTime) {
                    planeState.currentTime = planeState.availTime;
                }
#ifdef PRINT_DETAIL
                std::cout << "planeState.currentTime " << Helper::getWeekday(planeState.currentTime.getDate()) << planeState.currentTime.getHour() << std::endl;
#endif

                int nextNode = algo2FindNext(g, planeState, 0); // TODO: Randomize choice
                if (nextNode != -1) {
                    totalDelta += algo2AdvanceToNode(g, planeIdx, nextNode);
                    scheduledNewJob = true;
                    nJobsScheduled++;
#ifdef PRINT_OVERALL
                    {
                        algo2GenSolutionsFromGraph(graphs, planeIdx);
                        GameMechanic::killFlightPlanFrom(qPlayer, planeState.planeId, mScheduleFromTime.getDate(), mScheduleFromTime.getHour());
                        applySolutionForPlane(planeState.planeId, planeState.currentSolution);
                        Helper::checkPlaneSchedule(qPlayer, qPlanes[planeState.planeId], false);
                    }
#endif
                }
            }
        }
    }

#ifdef PRINT_OVERALL
    {
        auto t_end = std::chrono::steady_clock::now();
        auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_begin).count();
        std::cout << "Elapsed time in total in algo2(): " << delta << " ms" << std::endl;
    }
#endif

    hprintf("Scheduled %d jobs and improved gain in total by %d", nJobsScheduled, totalDelta);

    /* generate solution */
    for (int planeIdx = 0; planeIdx < nPlanes; planeIdx++) {
        algo2GenSolutionsFromGraph(graphs, planeIdx);
    }

    return {nJobsScheduled, totalDelta};
}
