#ifndef BOT_PLANER_H_
#define BOT_PLANER_H_

#include <cassert>
#include <climits>
#include <deque>
#include <string>
#include <vector>

extern const int kAvailTimeExtra;
extern const int kDurationExtra;

class PlaneTime {
  public:
    PlaneTime() = default;
    PlaneTime(int date, int time) : mDate(date), mTime(time) { normalize(); }

    int getDate() const { return mDate; }
    int getHour() const { return mTime; }

    PlaneTime &operator+=(int delta) {
        mTime += delta;
        normalize();
        return *this;
    }

    PlaneTime &operator-=(int delta) {
        mTime -= delta;
        normalize();
        return *this;
    }
    PlaneTime operator+(int delta) {
        PlaneTime t = *this;
        t += delta;
        return t;
    }
    PlaneTime operator-(int delta) {
        PlaneTime t = *this;
        t -= delta;
        return t;
    }
    bool operator==(const PlaneTime &other) const { return (mDate == other.mDate && mTime == other.mTime); }
    bool operator!=(const PlaneTime &other) const { return (mDate != other.mDate || mTime != other.mTime); }
    bool operator>(const PlaneTime &other) const {
        if (mDate == other.mDate) {
            return (mTime > other.mTime);
        }
        return (mDate > other.mDate);
    }
    bool operator>=(const PlaneTime &other) const {
        if (mDate == other.mDate) {
            return (mTime >= other.mTime);
        }
        return (mDate > other.mDate);
    }
    bool operator<(const PlaneTime &other) const { return other > *this; }
    bool operator<=(const PlaneTime &other) const { return other >= *this; }
    void setDate(int date) {
        mDate = date;
        mTime = 0;
    }

  private:
    void normalize() {
        while (mTime >= 24) {
            mTime -= 24;
            mDate++;
        }
        while (mTime < 0) {
            mTime += 24;
            mDate--;
        }
    }
    int mDate{0};
    int mTime{0};
};

class Graph {
  public:
    struct Node {
        int jobIdx{-1};
        int premium{0};
        int duration{0};
        int earliest{0};
        int latest{INT_MAX};
        std::vector<int> bestNeighbors;
    };

    struct Edge {
        int cost{-1};
        int duration{-1};
    };

    struct NodeState {
        bool visitedThisTour{false};
        int numVisited{0};
        PlaneTime availTime{};
        PlaneTime startTime{};
        int outIdx{0};
        int cameFrom{-1};
    };

    Graph(int numPlanes, int numJobs) : nPlanes(numPlanes), nNodes(numPlanes + numJobs), nodeInfo(nNodes), adjMatrix(nNodes), nodeState(nNodes) {
        for (int i = 0; i < nNodes; i++) {
            adjMatrix[i].resize(nNodes);
        }
    }

    void resetNodes() {
        for (int i = 0; i < nNodes; i++) {
            nodeState[i] = {};
        }
    }

    int nPlanes;
    int nNodes;
    std::vector<Node> nodeInfo;
    std::vector<std::vector<Edge>> adjMatrix;
    std::vector<NodeState> nodeState;
};

class BotPlaner {
  public:
    enum class JobOwner { Player, PlayerFreight, TravelAgency, LastMinute, Freight, International, InternationalFreight };
    struct JobScheduled {
        JobScheduled(int idx, PlaneTime a, PlaneTime b) : jobIdx(idx), start(a), end(b) {}
        int jobIdx{};
        PlaneTime start;
        PlaneTime end;
    };
    struct Solution {
        Solution(int p) : totalPremium(p) {}
        std::deque<JobScheduled> jobs;
        int totalPremium{0};
    };

    BotPlaner() = default;
    BotPlaner(PLAYER &player, const CPlanes &planes, JobOwner jobOwner, std::vector<int> intJobSource);

    int planFlights(const std::vector<int> &planeIdsInput);
    bool applySolution();

  private:
    struct PlaneState {
        int planeId{-1};
        int planeTypeId{-1};
        PlaneTime availTime{};
        int availCity{-1};
        std::vector<int> bJobIdAssigned;
        Solution currentSolution{0};
    };

    struct FlightJob {
        FlightJob(int i, int j, CAuftrag a, JobOwner o) : id(i), sourceId(j), auftrag(a), owner(o) { assert(i >= 0x1000000); }
        bool wasTaken() const { return owner == JobOwner::Player || owner == JobOwner::PlayerFreight; }
        bool isFreight() const { return owner == JobOwner::Freight || owner == JobOwner::InternationalFreight || owner == JobOwner::PlayerFreight; }

        int id{};
        int sourceId{-1};
        CAuftrag auftrag;
        JobOwner owner;
        std::vector<int> bPlaneTypeEligible;
        int assignedtoPlaneId{-1};
    };

    struct JobSource {
        JobSource(CAuftraege *ptr, JobOwner o) : jobs(ptr), owner(o) {}
        JobSource(CFrachten *ptr, JobOwner o) : freight(ptr), owner(o) {}
        CAuftraege *jobs{nullptr};
        CFrachten *freight{nullptr};
        int sourceId{-1};
        JobOwner owner{JobOwner::Player};
    };

    void printSolution(const Solution &solution, const std::vector<FlightJob> &list);
    void printGraph(const std::vector<PlaneState> &planeStates, const std::vector<FlightJob> &list, const Graph &g);

    /* preparation */
    void findPlaneTypes();
    void collectAllFlightJobs(const std::vector<int> &planeIds);
    std::vector<Graph> prepareGraph();

    /* algo 1 */
    Solution findFlightPlan(Graph &g, int planeIdx, PlaneTime availTime, const std::vector<int> &eligibleJobIds);
    std::pair<int, int> gatherAndPlanJobs(std::vector<Graph> &graphs);

    /* apply solution */
    bool applySolutionForPlane(int planeId, const BotPlaner::Solution &solution);

    PLAYER &qPlayer;
    JobOwner mJobOwner;
    std::vector<int> mIntJobSource;
    const CPlanes &qPlanes;
    PlaneTime mScheduleFromTime;

    /* state */
    std::vector<const CPlane *> mPlaneTypeToPlane;
    std::vector<FlightJob> mJobList;
    std::vector<PlaneState> mPlaneStates;
};

#endif // BOT_PLANER_H_
