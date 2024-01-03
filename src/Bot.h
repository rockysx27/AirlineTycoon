
class Bot {
  public:
    static bool planFlightsForPlane(PLAYER &qPlayer, int planeId);

  private:
    static std::vector<std::pair<int, CAuftrag>> gatherFlightsForPlane(PLAYER &qPlayer, int planeId, bool killFlightPlan);
};