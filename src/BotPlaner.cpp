#include "BotPlaner.h"

#include "BotHelper.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <unordered_map>

// #define PRINT_DETAIL 1
#define PRINT_OVERALL 1

const int kAvailTimeExtra = 2;
const int kDurationExtra = 1;
const int kScheduleForNextDays = 4;
const int64_t timeBudgetMS = 100;
static const bool kCanDropJobs = false;

static bool canFlyThisJob(const CAuftrag &job, const CPlane &plane) { return (job.Personen <= plane.ptPassagiere && job.FitsInPlane(plane)); }

static SLONG getPremiumEmptyFlight(const CPlane *qPlane, SLONG VonCity, SLONG NachCity) {
    return (qPlane->ptPassagiere * Cities.CalcDistance(VonCity, NachCity) / 1000 / 40);
}

BotPlaner::BotPlaner(PLAYER &player, const CPlanes &planes, JobOwner jobOwner, std::vector<int> intJobSource)
    : qPlayer(player), mJobOwner(jobOwner), mIntJobSource(std::move(intJobSource)), qPlanes(planes) {
    if (jobOwner == JobOwner::International) {
        for (const auto &i : mIntJobSource) {
            if (i < 0 || i >= AuslandsAuftraege.size()) {
                redprintf("BotPlaner::BotPlaner(): Invalid intJobSource given: %d", i);
                mJobOwner = JobOwner::Backlog;
            }
        }
        if (mIntJobSource.empty()) {
            redprintf("BotPlaner::BotPlaner(): No intJobSource given.");
            mJobOwner = JobOwner::Backlog;
        }
    } else if (jobOwner == JobOwner::InternationalFreight) {
        for (const auto &i : mIntJobSource) {
            if (i < 0 || i >= AuslandsFrachten.size()) {
                redprintf("BotPlaner::BotPlaner(): Invalid intJobSource given.");
                mJobOwner = JobOwner::BacklogFreight;
            }
        }
        if (mIntJobSource.empty()) {
            redprintf("BotPlaner::BotPlaner(): No intJobSource given.");
            mJobOwner = JobOwner::BacklogFreight;
        }
    } else {
        if (!mIntJobSource.empty()) {
            redprintf("BotPlaner::BotPlaner(): intJobSource does not need to be given for this job source.");
        }
    }
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

void BotPlaner::collectAllFlightJobs(const std::vector<int> &planeIds) {
    mJobList.clear();
    std::vector<JobSource> sources;
    if (mJobOwner == JobOwner::TravelAgency) {
        sources.emplace_back(&ReisebueroAuftraege, mJobOwner);
    } else if (mJobOwner == JobOwner::LastMinute) {
        sources.emplace_back(&LastMinuteAuftraege, mJobOwner);
    } else if (mJobOwner == JobOwner::Freight) {
        sources.emplace_back(&gFrachten, mJobOwner);
    } else if (mJobOwner == JobOwner::International) {
        for (const auto &i : mIntJobSource) {
            sources.emplace_back(&AuslandsAuftraege[i], mJobOwner);
            sources.back().sourceId = i;
        }
    } else if (mJobOwner == JobOwner::InternationalFreight) {
        for (const auto &i : mIntJobSource) {
            sources.emplace_back(&AuslandsFrachten[i], mJobOwner);
            sources.back().sourceId = i;
        }
    }

    /* jobs already in planer */
    sources.emplace_back(&qPlayer.Auftraege, JobOwner::Backlog);

    /* freight jobs already in planer */
    sources.emplace_back(&qPlayer.Frachten, JobOwner::BacklogFreight);

    /* create list of open jobs */
    for (const auto &source : sources) {
        for (int i = 0; source.jobs && i < source.jobs->AnzEntries(); i++) {
            if (source.jobs->IsInAlbum(i) == 0) {
                continue;
            }
            const auto &job = source.jobs->at(i);
            if (job.InPlan != 0 || job.Praemie < 0) {
                continue;
            }
            if (job.Date <= Sim.Date + kScheduleForNextDays) {
                mJobList.emplace_back(source.jobs->GetIdFromIndex(i), source.sourceId, job, source.owner);
                mJobList.back().score = job.Praemie;
                mJobList.back().score += mDistanceFactor * Cities.CalcDistance(job.VonCity, job.NachCity);
                mJobList.back().score += mPassengerFactor * job.Personen;
                mJobList.back().score += mUhrigBonus * job.bUhrigFlight;
                mJobList.back().score += mConstBonus;
            }
        }
        /* job source B: Freight */
        for (int i = 0; source.freight && i < source.freight->AnzEntries(); i++) {
            if (source.freight->IsInAlbum(i) == 0) {
                continue;
            }
            const auto &job = source.freight->at(i);
            if (job.InPlan != 0 || job.Praemie < 0) {
                continue;
            }
            if (job.Date <= Sim.Date + kScheduleForNextDays) {
                // jobList.emplace_back(source.jobs->GetIdFromIndex(i), source.sourceId, job, source.owner);
            }
        }
    }

    /* add jobs that will be re-planned */
    for (auto i : planeIds) {
        const auto &qFlightPlan = qPlanes[i].Flugplan.Flug;
        for (int c = qFlightPlan.AnzEntries() - 1; c >= 0; c--) {
            const auto &qFPE = qFlightPlan[c];
            if (PlaneTime{qFPE.Startdate, qFPE.Startzeit} < mScheduleFromTime) {
                continue;
            }
            if (qFPE.ObjectType == 2) {
                const auto &job = qPlayer.Auftraege[qFPE.ObjectId];
                mJobList.emplace_back(qFPE.ObjectId, -1, job, JobOwner::Planned);
                mJobList.back().score = job.Praemie;
            }
            // TODO: JobOwner::PlannedFreight
        }
    }

    /* sort list of jobs */
    std::sort(mJobList.begin(), mJobList.end(), [](const FlightJob &a, const FlightJob &b) {
        if (kCanDropJobs) {
            return (a.score + (a.wasTaken() ? a.auftrag.Strafe : 0)) > (b.score + (b.wasTaken() ? b.auftrag.Strafe : 0));
        }
        if (a.wasTaken() != b.wasTaken()) {
            return a.wasTaken();
        }
        return a.score > b.score;
    });

    for (int i = 0; i < mJobList.size(); i++) {
        if (mJobList[i].owner == JobOwner::Planned || mJobList[i].owner == JobOwner::PlannedFreight) {
            mExistingJobsById[mJobList[i].id] = i;
        }
    }
}

void BotPlaner::findPlaneTypes() {
    int nPlaneTypes = 0;
    std::unordered_map<int, int> mapOldType2NewType;
    for (auto &planeState : mPlaneStates) {
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

#ifdef PRINT_DETAIL
    std::cout << "There are " << mPlaneStates.size() << " planes of " << mPlaneTypeToPlane.size() << " types" << std::endl;
#endif
}

std::vector<Graph> BotPlaner::prepareGraph() {
    int nPlanes = mPlaneStates.size();
    int nPlaneTypes = mPlaneTypeToPlane.size();

    /* prepare graph */
    std::vector<Graph> graphs(nPlaneTypes, Graph(nPlanes, mJobList.size()));
    for (int pt = 0; pt < nPlaneTypes; pt++) {
        auto &g = graphs[pt];
        const auto *plane = mPlaneTypeToPlane[pt];

        /* nodes */
        for (int i = 0; i < g.nNodes; i++) {
            auto &qNodeInfo = g.nodeInfo[i];

            if (i >= nPlanes && canFlyThisJob(mJobList[i - nPlanes].auftrag, *plane)) {
                auto score = mJobList[i - nPlanes].score;
                const auto &job = mJobList[i - nPlanes].auftrag;
                const auto *plane = mPlaneTypeToPlane[pt];
                qNodeInfo.jobIdx = i - nPlanes;
                qNodeInfo.premium = score - CalculateFlightCostNoTank(job.VonCity, job.NachCity, plane->ptVerbrauch, plane->ptGeschwindigkeit);
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

                int startCity = (i >= nPlanes) ? mJobList[i - nPlanes].auftrag.NachCity : mPlaneStates[i].startCity;
                int destCity = mJobList[j - nPlanes].auftrag.VonCity;
                startCity = Cities.find(startCity);
                destCity = Cities.find(destCity);
                assert(startCity >= 0 && startCity < Cities.AnzEntries());
                assert(destCity >= 0 && destCity < Cities.AnzEntries());
                if (startCity != destCity) {
                    g.adjMatrix[i][j].cost = CalculateFlightCostNoTank(startCity, destCity, plane->ptVerbrauch, plane->ptGeschwindigkeit);
                    g.adjMatrix[i][j].cost -= getPremiumEmptyFlight(plane, startCity, destCity);
                    g.adjMatrix[i][j].duration = kDurationExtra + Cities.CalcFlugdauer(startCity, destCity, plane->ptGeschwindigkeit);
                } else {
                    g.adjMatrix[i][j].cost = 0;
                    g.adjMatrix[i][j].duration = 0;
                }

                /* we skip some edges for the 'best' edges */
                if (g.nodeInfo[j].premium <= 0) {
                    continue; /* job has no premium (after deducting flight cost) */
                }
                if (g.nodeInfo[i].earliest > g.nodeInfo[j].latest) {
                    continue; /* not possible because date constraints */
                }
                neighborList.emplace_back(j, g.nodeInfo[j].premium - g.adjMatrix[i][j].cost);
            }

            std::sort(neighborList.begin(), neighborList.end(), [](const std::pair<int, int> &a, const std::pair<int, int> &b) { return a.second > b.second; });
            for (const auto &n : neighborList) {
                qNodeInfo.bestNeighbors.push_back(n.first);
            }
        }
#ifdef PRINT_DETAIL
        printGraph(mPlaneStates, mJobList, graphs[pt]);
#endif
    }
    return graphs;
}

bool BotPlaner::takeJobs(PlaneState &planeState) {
    bool ok = true;
    for (auto &jobScheduled : planeState.currentSolution.jobs) {
        auto &job = mJobList[jobScheduled.jobIdx];
        if (job.assignedtoPlaneIdx == -1) {
            continue;
        }

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
                redprintf("BotPlaner::takeJobs(): Unknown job source: %ld", job.owner);
                return false;
            }

            if (outAuftragsId < 0) {
                redprintf("BotPlaner::takeJobs(): GameMechanic returned error when trying to take job!");
                ok = false;
                continue;
            }
            job.id = outAuftragsId;
            if (job.isFreight()) {
                job.owner = JobOwner::BacklogFreight;
            } else {
                job.owner = JobOwner::Backlog;
            }
        }
        jobScheduled.objectId = job.id;
    }
    return ok;
}

bool BotPlaner::applySolutionForPlane(PLAYER &qPlayer, int planeId, const BotPlaner::Solution &solution) {
    const auto &qPlanes = qPlayer.Planes;
    if (planeId < 0x1000000 || !qPlanes.IsInAlbum(planeId)) {
        redprintf("BotPlaner::applySolutionForPlane(): Skipping invalid plane: %d", planeId);
        return false;
    }

    std::unordered_map<int, JobScheduled> jobHash;
    for (auto &iter : solution.jobs) {
        if (!qPlayer.Auftraege.IsInAlbum(iter.objectId)) {
            redprintf("BotPlaner::applySolutionForPlane(): Skipping invalid job: %d", iter.objectId);
            continue;
        }
        jobHash[iter.objectId] = iter;
        const auto &auftrag = qPlayer.Auftraege[iter.objectId];
        const auto startTime = iter.start;

        /* plan taken jobs */
        if (!iter.bIsFreight) {
            if (!GameMechanic::planFlightJob(qPlayer, planeId, iter.objectId, startTime.getDate(), startTime.getHour())) {
                redprintf("BotPlaner::applySolutionForPlane(): GameMechanic::planFlightJob returned error!");
                redprintf("Tried to schedule %s at %s %ld", Helper::getJobName(auftrag).c_str(), Helper::getWeekday(startTime.getDate()).c_str(),
                          startTime.getHour());
            }
        } else {
            // TODO
        }
    }

    /* check flight time */
    const auto &qFlightPlan = qPlanes[planeId].Flugplan.Flug;
    for (SLONG d = 0; d < qFlightPlan.AnzEntries(); d++) {
        const auto &flug = qFlightPlan[d];
        if (flug.ObjectType != 2) {
            continue; /* TODO: freight */
        }
        if (jobHash.find(flug.ObjectId) == jobHash.end()) {
            continue;
        }

        auto iter = jobHash[flug.ObjectId];
        jobHash.erase(flug.ObjectId);
        const auto &auftrag = qPlayer.Auftraege[iter.objectId];
        const auto startTime = iter.start;
        const auto endTime = iter.end - kDurationExtra;

        if (PlaneTime(flug.Startdate, flug.Startzeit) != startTime) {
            redprintf("BotPlaner::applySolutionForPlane(): Plane %s, schedule entry %ld: GameMechanic scheduled job (%s) at different start time (%s %d "
                      "instead of "
                      "%s %d)!",
                      (LPCTSTR)qPlanes[planeId].Name, d, Helper::getJobName(auftrag).c_str(), (LPCTSTR)Helper::getWeekday(flug.Startdate), flug.Startzeit,
                      (LPCTSTR)Helper::getWeekday(startTime.getDate()), startTime.getHour());
        }
        if (PlaneTime(flug.Landedate, flug.Landezeit) != endTime) {
            redprintf("BotPlaner::applySolutionForPlane(): Plane %s, schedule entry %ld: GameMechanic scheduled job (%s) with different landing time (%s %d "
                      "instead of %s %d)!",
                      (LPCTSTR)qPlanes[planeId].Name, d, Helper::getJobName(auftrag).c_str(), (LPCTSTR)Helper::getWeekday(flug.Landedate), flug.Landezeit,
                      (LPCTSTR)Helper::getWeekday(endTime.getDate()), endTime.getHour());
        }
    }
    for (const auto &iter : jobHash) {
        const auto &auftrag = qPlayer.Auftraege[iter.second.objectId];
        redprintf("BotPlaner::applySolutionForPlane(): Did not find job %s in flight plan!", Helper::getJobName(auftrag).c_str());
    }

    return true;
}

BotPlaner::SolutionList BotPlaner::planFlights(const std::vector<int> &planeIdsInput, bool bUseImprovedAlgo, int extraBufferTime) {
    auto t_begin = std::chrono::steady_clock::now();

    if (mDistanceFactor > 0) {
        hprintf("BotPlaner::planFlights(): Using mDistanceFactor = %ld", mDistanceFactor);
    }
    if (mPassengerFactor > 0) {
        hprintf("BotPlaner::planFlights(): Using mPassengerFactor = %ld", mPassengerFactor);
    }
    if (mUhrigBonus > 0) {
        hprintf("BotPlaner::planFlights(): Using mUhrigBonus = %ld", mUhrigBonus);
    }
    if (mConstBonus > 0) {
        hprintf("BotPlaner::planFlights(): Using mConstBonus = %ld", mConstBonus);
    }

#ifdef PRINT_DETAIL
    hprintf("BotPlaner::planFlights(): Current time: %d", Sim.GetHour());
#endif
    mScheduleFromTime = {Sim.Date, Sim.GetHour() + extraBufferTime};

    /* find valid plane IDs */
    std::vector<int> planeIds;
    for (auto i : planeIdsInput) {
        if (qPlanes.IsInAlbum(i) != 0) {
            planeIds.push_back(i);
        }
    }

    /* prepare list of jobs */
    collectAllFlightJobs(planeIds);

    /* statistics of existing jobs */
    int nPreviouslyOwned = 0;
    int nNewJobs = 0;
    for (const auto &job : mJobList) {
        if (job.wasTaken()) {
            nPreviouslyOwned++;
        } else {
            nNewJobs++;
        }
    }

    /* prepare list of planes */
    mPlaneStates.clear();
    mPlaneStates.reserve(planeIds.size());
    for (auto i : planeIds) {
        assert(i >= 0x1000000 && qPlanes.IsInAlbum(i));

        mPlaneStates.push_back({});
        auto &planeState = mPlaneStates.back();
        planeState.planeId = i;
        planeState.bJobIdAssigned.resize(mJobList.size());

        /* determine when and where the plane will be available */
        std::tie(planeState.availTime, planeState.startCity) = Helper::getPlaneAvailableTimeLoc(qPlanes[i], mScheduleFromTime, mScheduleFromTime);
        planeState.startCity = Cities.find(planeState.startCity);
        assert(planeState.startCity >= 0 && planeState.startCity < Cities.AnzEntries());
#ifdef PRINT_DETAIL
        Helper::checkPlaneSchedule(qPlayer, i, false);
        hprintf("BotPlaner::planFlights(): After %s %ld: Plane %s is in %s @ %s %d", (LPCTSTR)Helper::getWeekday(mScheduleFromTime.getDate()),
                mScheduleFromTime.getHour(), (LPCTSTR)qPlanes[i].Name, (LPCTSTR)Cities[planeState.startCity].Kuerzel,
                (LPCTSTR)Helper::getWeekday(planeState.availTime.getDate()), planeState.availTime.getHour());
#endif
    }

    /* find number of unique plane types */
    findPlaneTypes();

    /* prepare graph */
    mGraphs = prepareGraph();

    /* start algo */
    bool improved = false;
    if (bUseImprovedAlgo) {
        auto t_current = std::chrono::steady_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(t_current - t_begin).count();
        improved = algo2(timeBudgetMS - diff);
    } else {
        improved = algo1();
    }

    /* check statistics */
    int nPreviouslyOwnedScheduled = 0;
    int nNewJobsScheduled = 0;
    for (const auto &job : mJobList) {
        if (job.assignedtoPlaneIdx < 0) {
            continue;
        }
        if (job.wasTaken()) {
            nPreviouslyOwnedScheduled++;
        } else {
            nNewJobsScheduled++;
        }
    }

#ifdef PRINT_OVERALL
    hprintf("Scheduled %d out of %d existing jobs.", nPreviouslyOwnedScheduled, nPreviouslyOwned);
    hprintf("Scheduled %d out of %d new jobs.", nNewJobsScheduled, nNewJobs);
#endif

#ifdef PRINT_OVERALL
    auto t_end = std::chrono::steady_clock::now();
    auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_begin).count();
    if (bUseImprovedAlgo) {
        hprintf("Elapsed time in total using algo2(): %lld ms", delta);
    } else {
        hprintf("Elapsed time in total using algo1(): %lld ms", delta);
    }
#endif

    if (!improved) {
        hprintf("Scheduling algo did not improve anything, returning empty solution.");
        return {};
    }

    /* now take all jobs */
    for (auto &planeState : mPlaneStates) {
        takeJobs(planeState);
    }

    SolutionList list;
    for (const auto &plane : mPlaneStates) {
        list.emplace_back(std::move(plane.currentSolution));
    }
    return list;
}

bool BotPlaner::applySolution(PLAYER &qPlayer, const SolutionList &solutions) {
    /* kill existing flight plans */
    for (const auto &solution : solutions) {
        int planeId = solution.planeId;
        auto time = solution.scheduleFromTime;
        if (!GameMechanic::killFlightPlanFrom(qPlayer, planeId, time.getDate(), time.getHour())) {
            return false;
        }
    }

    /* apply solution */
    Helper::ScheduleInfo overallInfo;
    SLONG totalDiff = 0;
    for (const auto &solution : solutions) {
        int planeId = solution.planeId;

        auto oldInfo = Helper::calculateScheduleInfo(qPlayer, planeId);
        applySolutionForPlane(qPlayer, planeId, solution);

        auto newInfo = Helper::calculateScheduleInfo(qPlayer, planeId);
        overallInfo += newInfo;
        SLONG diff = newInfo.gain - oldInfo.gain;
        totalDiff += diff;

#ifdef PRINT_DETAIL
        Helper::checkPlaneSchedule(qPlayer, planeId, false);
        if (diff > 0) {
            hprintf("%s: Improved gain: %d => %d (+%d)", (LPCTSTR)qPlayer.Planes[planeId].Name, oldInfo.gain, newInfo.gain, diff);
        } else {
            hprintf("%s: Gain did not improve: %d => %d (%d)", (LPCTSTR)qPlayer.Planes[planeId].Name, oldInfo.gain, newInfo.gain, diff);
        }
#endif
    }

#ifdef PRINT_DETAIL
    if (totalDiff > 0) {
        hprintf("Total gain improved: %s $ (+%s $)", Insert1000erDots(overallInfo.gain).c_str(), Insert1000erDots(totalDiff).c_str());
    } else if (totalDiff == 0) {
        hprintf("Total gain did not change: %s $", Insert1000erDots(overallInfo.gain).c_str());
    } else {
        hprintf("Total gain got worse: %s $ (%s $)", Insert1000erDots(overallInfo.gain).c_str(), Insert1000erDots(totalDiff).c_str());
    }
#endif

#ifdef PRINT_OVERALL
    overallInfo.printDetails();
#endif

    return true;
}
