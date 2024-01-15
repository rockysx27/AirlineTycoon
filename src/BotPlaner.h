#ifndef BOT_PLANER_H_
#define BOT_PLANER_H_

#include <climits>
#include <deque>
#include <string>

class PlaneTime {
  public:
    PlaneTime() = default;
    PlaneTime(int date, int time) : mDate(date), mTime(time) {}

    int getDate() const { return mDate; }
    int getHour() const { return mTime; }

    PlaneTime &operator+=(int delta) {
        mTime += delta;
        while (mTime >= 24) {
            mTime -= 24;
            mDate++;
        }
        return *this;
    }

    PlaneTime &operator-=(int delta) {
        mTime -= delta;
        while (mTime < 0) {
            mTime += 24;
            mDate--;
        }
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
    void setDate(int date) {
        mDate = date;
        mTime = 0;
    }

  private:
    int mDate{0};
    int mTime{0};
};

class Graph {
  public:
    struct Node {
        int premium{0};
        int duration{0};
        int earliest{0};
        int latest{0};
        std::vector<int> closestNeighbors;
    };

    struct Edge {
        int cost{0};
        int duration{0};
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

    inline constexpr int jobToNode(int i) { return i + nPlanes; }
    inline constexpr int nodeToJob(int i) { return i - nPlanes; }

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
        JobScheduled(int id, PlaneTime a, PlaneTime b) : jobId(id), start(a), end(b) {}
        int jobId{};
        PlaneTime start;
        PlaneTime end;
    };
    struct Solution {
        std::deque<JobScheduled> jobs;
        int totalPremium{0};
    };

    BotPlaner(PLAYER &player, const CPlanes &planes, JobOwner jobOwner, std::vector<int> intJobSource);

    int planFlights(const std::vector<int> &planeIds);

  private:
    struct PlaneState {
        int planeId{-1};
        int planeTypeId{-1};
        PlaneTime availTime{};
        int availCity{};
        std::vector<int> assignedJobIds;
        Solution currentSolution{};
    };

    struct FlightJob {
        FlightJob(int i, int j, CAuftrag a, JobOwner o) : id(i), sourceId(j), auftrag(a), owner(o) {}
        bool wasTaken() const { return owner == JobOwner::Player || owner == JobOwner::PlayerFreight; }
        bool isFreight() const { return owner == JobOwner::Freight || owner == JobOwner::InternationalFreight || owner == JobOwner::PlayerFreight; }

        int id{};
        int sourceId{-1};
        CAuftrag auftrag;
        JobOwner owner;
        std::vector<int> eligiblePlanes;
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

    void printJob(const CAuftrag &qAuftrag);
    std::string printJobShort(const CAuftrag &qAuftrag);
    void printSolution(const Solution &solution, const std::vector<FlightJob> &list);
    void printGraph(const std::vector<PlaneState> &planeStates, const std::vector<FlightJob> &list, const Graph &g);

    void findPlaneTypes(std::vector<PlaneState> &planeStates, std::vector<const CPlane *> &planeTypeToPlane);
    Solution findFlightPlan(Graph &g, int planeId, PlaneTime availTime, const std::vector<int> &eligibleJobIds);
    std::pair<int, int> gatherAndPlanJobs(std::vector<FlightJob> &jobList, std::vector<PlaneState> &planeStates);
    bool applySolution(int planeId, const BotPlaner::Solution &solution, std::vector<FlightJob> &jobList);

    PLAYER &qPlayer;
    JobOwner mJobOwner;
    std::vector<int> mIntJobSource;
    const CPlanes &qPlanes;
};

#endif // BOT_PLANER_H_
