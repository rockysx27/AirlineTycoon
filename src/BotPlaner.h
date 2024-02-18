#ifndef BOT_PLANER_H_
#define BOT_PLANER_H_

#include <cassert>
#include <climits>
#include <deque>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

extern const int kAvailTimeExtra;
extern const int kDurationExtra;
extern const int kScheduleForNextDays;

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
    PlaneTime operator+(int delta) const {
        PlaneTime t = *this;
        t += delta;
        return t;
    }
    PlaneTime operator-(int delta) const {
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
        /* both algos: */
        PlaneTime startTime{};
        int cameFrom{-1};
        /* algo 1 only: */
        PlaneTime availTime{};
        bool visitedThisTour{false};
        int numVisited{0};
        int outIdx{0};
        /* algo 2 only: */
        int nextNode{-1};
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
    enum class JobOwner { Planned, PlannedFreight, Backlog, BacklogFreight, TravelAgency, LastMinute, Freight, International, InternationalFreight };
    struct JobScheduled {
        JobScheduled(int idx, PlaneTime a, PlaneTime b) : jobIdx(idx), start(a), end(b) {}
        int jobIdx{};
        PlaneTime start;
        PlaneTime end;
    };
    struct Solution {
        Solution() = default;
        Solution(int p) : totalPremium(p) {}
        std::deque<JobScheduled> jobs;
        int totalPremium{0};
    };

    BotPlaner() = default;
    BotPlaner(PLAYER &player, const CPlanes &planes, JobOwner jobOwner, std::vector<int> intJobSource);

    int planFlights(const std::vector<int> &planeIdsInput, bool bUseImprovedAlgo);
    bool applySolution();

  private:
    struct PlaneState {
        /* static data */
        int planeId{-1};
        int planeTypeId{-1};
        int currentCity{-1};
        /* both algos: */
        PlaneTime availTime{};
        Solution currentSolution{0};
        /* algo 1 only: */
        std::vector<int> bJobIdAssigned;
        /* algo 2 only: */
        PlaneTime currentTime{};
        int currentNode{-1};
        int numNodes{0};
        int currentPremium{0};
        int currentCost{0};
    };

    struct FlightJob {
        FlightJob(int i, int j, CAuftrag a, JobOwner o) : id(i), sourceId(j), auftrag(a), owner(o) { assert(i >= 0x1000000); }
        bool wasTaken() const {
            return owner == JobOwner::Planned || owner == JobOwner::PlannedFreight || owner == JobOwner::Backlog || owner == JobOwner::BacklogFreight;
        }
        bool isFreight() const {
            return owner == JobOwner::PlannedFreight || owner == JobOwner::Freight || owner == JobOwner::InternationalFreight ||
                   owner == JobOwner::BacklogFreight;
        }

        int id{};
        int sourceId{-1};
        CAuftrag auftrag;
        JobOwner owner;
        /* both algos: */
        int assignedtoPlaneIdx{-1};
        /* algo 1 only: */
        std::vector<int> bPlaneTypeEligible;
    };

    struct JobSource {
        JobSource(CAuftraege *ptr, JobOwner o) : jobs(ptr), owner(o) {}
        JobSource(CFrachten *ptr, JobOwner o) : freight(ptr), owner(o) {}
        CAuftraege *jobs{nullptr};
        CFrachten *freight{nullptr};
        int sourceId{-1};
        JobOwner owner{JobOwner::Backlog};
    };

    void printGraph(const std::vector<PlaneState> &planeStates, const std::vector<FlightJob> &list, const Graph &g);

    /* preparation */
    void findPlaneTypes();
    void collectAllFlightJobs(const std::vector<int> &planeIds);
    std::vector<Graph> prepareGraph();

    /* algo 1 */
    Solution algo1FindFlightPlan(Graph &g, int planeIdx, PlaneTime availTime, const std::vector<int> &eligibleJobIds);
    std::pair<int, int> algo1(std::vector<Graph> &graphs);

    /* algo 2 */
    void printNodeInfo(const Graph &g, int nodeIdx) const;
    bool algo2ShiftLeft(Graph &g, int nodeToShift, int shiftT, bool commit);
    bool algo2ShiftRight(Graph &g, int nodeToShift, int shiftT, bool commit);
    bool algo2MakeRoom(Graph &g, int nodeToMoveLeft, int nodeToMoveRight);
    void algo2GenSolutionsFromGraph(Graph &g, int p);
    void algo2ApplySolutionToGraph(std::vector<Graph> &graphs);
    int algo2FindNext(const Graph &g, PlaneState &planeState, int choice) const;
    void algo2InsertNode(Graph &g, int planeIdx, int nextNode);
    void algo2RemoveNode(Graph &g, int planeIdx, int currentNode);
    std::pair<int, int> algo2RunForPlane(Graph &g, int planeIdx, int temperature);
    std::pair<int, int> algo2(std::vector<Graph> &graphs);

    /* apply solution */
    bool applySolutionForPlane(int planeId, const BotPlaner::Solution &solution);

    /* randomness */
    inline int getRandInt(int min, int max) {
        std::uniform_int_distribution<int> dist(min, max);
        return dist(mMT);
    }

    PLAYER &qPlayer;
    JobOwner mJobOwner;
    std::vector<int> mIntJobSource;
    const CPlanes &qPlanes;
    PlaneTime mScheduleFromTime;

    /* state */
    std::vector<const CPlane *> mPlaneTypeToPlane;
    std::vector<FlightJob> mJobList;
    std::unordered_map<int, int> mExistingJobsById;
    std::vector<PlaneState> mPlaneStates;

    /* randomness source */
    std::random_device mRD;
    std::mt19937 mMT{mRD()};
};

#endif // BOT_PLANER_H_
