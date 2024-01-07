#include <climits>
#include <deque>

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
    };

    struct Edge {
        int cost{0};
        int duration{0};
    };

    struct NodeState {
        bool visited{false};
        PlaneTime availTime{};
        PlaneTime startTime{};
        int outIdx{0};
        int cameFrom{-1};
    };

    Graph(int numPlanes, int numJobs, int numNodeTypes)
        : nPlanes(numPlanes), nNodes(numPlanes + numJobs), nNodesTypes(numNodeTypes), nodeInfo(numNodeTypes), adjMatrix(numNodeTypes), nodeState(nNodes) {
        for (int p = 0; p < numNodeTypes; p++) {
            nodeInfo[p].resize(nNodes);
            adjMatrix[p].resize(nNodes);
            for (int i = 0; i < nNodes; i++) {
                adjMatrix[p][i].resize(nNodes);
            }
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
    int nNodesTypes;
    std::vector<std::vector<Node>> nodeInfo;
    std::vector<std::vector<std::vector<Edge>>> adjMatrix;
    std::vector<NodeState> nodeState;
};

class BotPlaner {
  public:
    enum class JobOwner { Player, PlayerFreight, TravelAgency, LastMinute, Freight, International, InternationalFreight };
    struct Solution {
        std::deque<std::pair<int, PlaneTime>> jobs;
        int totalPremium{0};
    };

    BotPlaner(PLAYER &player, const CPlanes &planes, JobOwner jobOwner, int intJobSource = -1);

    bool planFlights(int planeId);

  private:
    struct PlaneState {
        PlaneTime availTime{};
        int availCity{};
        std::vector<int> assignedJobIds;
        Solution currentSolution{};
        bool enabled{false};
    };

    struct FlightJob {
        FlightJob(int i, CAuftrag a, JobOwner o) : id(i), auftrag(a), owner(o) {}
        int id{};
        CAuftrag auftrag;
        JobOwner owner;
        std::vector<int> eligiblePlanes;
        int assignedtoPlaneId{-1};
    };

    void printJob(const CAuftrag &qAuftrag);
    void printJobShort(const CAuftrag &qAuftrag);
    void printSolution(const Solution &solution, const std::vector<FlightJob> &list);
    void printGraph(const std::vector<FlightJob> &list, const Graph &g, int pt);

    void findPlaneTypes(std::vector<int> &planeIdToType, std::vector<const CPlane *> &planeTypeToPlane);
    Solution findFlightPlan(Graph &g, int p, int planeId, PlaneTime availTime, const std::vector<int> &eligibleJobIds);
    void gatherAndPlanJobs(std::vector<FlightJob> &jobList, std::vector<PlaneState> &planeStates);
    bool applySolution(int planeId, const BotPlaner::Solution &solution, std::vector<FlightJob> &jobList);

    PLAYER &qPlayer;
    JobOwner mJobOwner;
    int mIntJobSource;
    const CPlanes &qPlanes;
};
