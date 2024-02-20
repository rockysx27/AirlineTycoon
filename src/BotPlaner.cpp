#include "BotPlaner.h"

#include "BotHelper.h"

#include <algorithm>
#include <iostream>
#include <unordered_map>

// #define PRINT_DETAIL 1
// #define PRINT_OVERALL 1

const int kAvailTimeExtra = 2;
const int kDurationExtra = 1;
const int kScheduleForNextDays = 4;
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
                mJobList.emplace_back(qFPE.ObjectId, -1, qPlayer.Auftraege[qFPE.ObjectId], JobOwner::Planned);
            }
            // TODO: JobOwner::PlannedFreight
        }
    }

    /* sort list of jobs */
    std::sort(mJobList.begin(), mJobList.end(), [](const FlightJob &a, const FlightJob &b) {
        if (kCanDropJobs) {
            return (a.auftrag.Praemie + (a.wasTaken() ? a.auftrag.Strafe : 0)) > (b.auftrag.Praemie + (b.wasTaken() ? b.auftrag.Strafe : 0));
        }
        if (a.wasTaken() != b.wasTaken()) {
            return a.wasTaken();
        }
        return a.auftrag.Praemie > b.auftrag.Praemie;
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

        /* nodes */
        for (int i = 0; i < g.nNodes; i++) {
            auto &qNodeInfo = g.nodeInfo[i];

            if (i >= nPlanes && (mJobList[i - nPlanes].bPlaneTypeEligible[pt] == 1)) {
                auto &job = mJobList[i - nPlanes].auftrag;
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

                int startCity = (i >= nPlanes) ? mJobList[i - nPlanes].auftrag.NachCity : mPlaneStates[i].currentCity;
                int destCity = mJobList[j - nPlanes].auftrag.VonCity;
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

bool BotPlaner::applySolutionForPlane(int planeId, const BotPlaner::Solution &solution) {
    assert(planeId >= 0x1000000 && qPlanes.IsInAlbum(planeId));

    for (auto &iter : solution.jobs) {
        if (iter.jobIdx < 0 || iter.jobIdx >= mJobList.size()) {
            redprintf("BotPlaner::applySolutionForPlane(): Skipping invalid job!");
            continue;
        }
        auto &job = mJobList[iter.jobIdx];
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
                redprintf("BotPlaner::applySolutionForPlane(): Unknown job source: %ld", job.owner);
                return false;
            }

            if (outAuftragsId < 0) {
                redprintf("BotPlaner::applySolutionForPlane(): GameMechanic returned error when trying to take job!");
                return false;
            }
            job.id = outAuftragsId;
            if (job.isFreight()) {
                job.owner = JobOwner::BacklogFreight;
            } else {
                job.owner = JobOwner::Backlog;
            }
        }

        /* plan taken jobs */
        if (job.isFreight()) {
            if (!GameMechanic::planFreightJob(qPlayer, planeId, job.id, startTime.getDate(), startTime.getHour())) {
                redprintf("BotPlaner::applySolutionForPlane(): GameMechanic::planFreightJob returned error!");
                redprintf("Tried to schedule %s at %s %ld", Helper::getJobName(job.auftrag).c_str(), Helper::getWeekday(startTime.getDate()).c_str(),
                          startTime.getHour());
                return false;
            }
        } else {
            if (!GameMechanic::planFlightJob(qPlayer, planeId, job.id, startTime.getDate(), startTime.getHour())) {
                redprintf("BotPlaner::applySolutionForPlane(): GameMechanic::planFlightJob returned error!");
                redprintf("Tried to schedule %s at %s %ld", Helper::getJobName(job.auftrag).c_str(), Helper::getWeekday(startTime.getDate()).c_str(),
                          startTime.getHour());
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
                redprintf("BotPlaner::applySolutionForPlane(): Plane %s, schedule entry %ld: GameMechanic scheduled job (%s) at different start time (%s %d "
                          "instead of "
                          "%s %d)!",
                          (LPCTSTR)qPlanes[planeId].Name, d, Helper::getJobName(job.auftrag).c_str(), (LPCTSTR)Helper::getWeekday(flug.Startdate),
                          flug.Startzeit, (LPCTSTR)Helper::getWeekday(startTime.getDate()), startTime.getHour());
            }
            if ((PlaneTime(flug.Landedate, flug.Landezeit) + kDurationExtra) != endTime) {
                redprintf(
                    "BotPlaner::applySolutionForPlane(): Plane %s, schedule entry %ld: GameMechanic scheduled job (%s) with different landing time (%s %d "
                    "instead of %s %d)!",
                    (LPCTSTR)qPlanes[planeId].Name, d, Helper::getJobName(job.auftrag).c_str(), (LPCTSTR)Helper::getWeekday(flug.Landedate), flug.Landezeit,
                    (LPCTSTR)Helper::getWeekday(endTime.getDate()), endTime.getHour());
            }
        }
        if (!found) {
            redprintf("BotPlaner::applySolutionForPlane(): Did not find job %s in flight plan!", Helper::getJobName(job.auftrag).c_str());
        }
    }
    return true;
}

int BotPlaner::planFlights(const std::vector<int> &planeIdsInput, bool bUseImprovedAlgo) {
#ifdef PRINT_OVERALL
    hprintf("BotPlaner::planFlights(): Current time: %d", Sim.GetHour());
#endif

    mScheduleFromTime = {Sim.Date, Sim.GetHour() + kAvailTimeExtra};

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
        std::tie(planeState.availTime, planeState.currentCity) = Helper::getPlaneAvailableTimeLoc(qPlanes[i], mScheduleFromTime);
        planeState.currentCity = Cities.find(planeState.currentCity);
        assert(planeState.currentCity >= 0 && planeState.currentCity < Cities.AnzEntries());
#ifdef PRINT_OVERALL
        Helper::checkPlaneSchedule(qPlayer, i, false);
        hprintf("BotPlaner::planFlights(): After %s %ld: Plane %s is in %s @ %s %d", (LPCTSTR)Helper::getWeekday(mScheduleFromTime.getDate()),
                mScheduleFromTime.getHour(), (LPCTSTR)qPlanes[i].Name, (LPCTSTR)Cities[planeState.currentCity].Kuerzel,
                (LPCTSTR)Helper::getWeekday(planeState.availTime.getDate()), planeState.availTime.getHour());
#endif
    }

    /* find number of unique plane types */
    findPlaneTypes();
    int nPlaneTypes = mPlaneTypeToPlane.size();
    for (auto &job : mJobList) {
        job.bPlaneTypeEligible.resize(nPlaneTypes);
    }

    /* figure out which jobs are eligible for which plane */
    for (auto &planeState : mPlaneStates) {
        const auto &qPlane = qPlanes[planeState.planeId];

        /* filter jobs that exceed range / seats */
        int eligibleJobs = 0;
        for (auto &job : mJobList) {
            if (job.auftrag.Personen <= qPlane.ptPassagiere && job.auftrag.FitsInPlane(qPlane)) {
                job.bPlaneTypeEligible[planeState.planeTypeId] = 1;
                eligibleJobs++;
            }
        }
#ifdef PRINT_DETAIL
        std::cout << "Plane " << qPlane.Name << " is able to fly " << eligibleJobs << " / " << mJobList.size() << " jobs" << std::endl;
#endif
    }

    /* prepare graph */
    mGraphs = prepareGraph();

    /* start algo */
    int nJobsScheduled{0};
    int totalGain{0};
    if (bUseImprovedAlgo) {
        std::tie(nJobsScheduled, totalGain) = algo2();
    } else {
        std::tie(nJobsScheduled, totalGain) = algo1();
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

    return totalGain;
}

bool BotPlaner::applySolution() {
    /* apply solution */
    int totalGain = 0;
    int totalDiff = 0;
    for (int p = 0; p < mPlaneStates.size(); p++) {
        int planeId = mPlaneStates[p].planeId;

        /* kill existing flight plans */
        SLONG oldGain = Helper::calculateScheduleGain(qPlayer, planeId);
        GameMechanic::killFlightPlanFrom(qPlayer, planeId, mScheduleFromTime.getDate(), mScheduleFromTime.getHour());

        auto &bestSolution = mPlaneStates[p].currentSolution;
        applySolutionForPlane(planeId, bestSolution);

        SLONG newGain = Helper::calculateScheduleGain(qPlayer, planeId);
        SLONG diff = newGain - oldGain;
        totalGain += newGain;
        totalDiff += diff;

#ifdef PRINT_OVERALL
        Helper::checkPlaneSchedule(qPlayer, p, false);
        if (diff > 0) {
            hprintf("%s: Improved gain: %d => %d (+%d)", (LPCTSTR)qPlanes[planeId].Name, oldGain, newGain, diff);
        } else {
            hprintf("%s: Gain did not improve: %d => %d (%d)", (LPCTSTR)qPlanes[planeId].Name, oldGain, newGain, diff);
        }
#endif
    }

#ifdef PRINT_OVERALL
    if (totalDiff > 0) {
        hprintf("Total gain improved: %d (+%d)", totalGain, totalDiff);
    } else {
        hprintf("Total gain did not improve: %d (%d)", totalGain, totalDiff);
    }
#endif

    return true;
}
