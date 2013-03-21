/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

/** An experiment of network failure scenarios during disasters.  Randomly fails the given links/nodes
    within the given region, running a RonClient on each node, who contacts the chosen RonServer
    within the disaster region.  Can run this class multiple times, once for each set of parameters.  **/

#ifndef GEOCRON_EXPERIMENT_H
#define GEOCRON_EXPERIMENT_H

#include "ns3/core-module.h"
//#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/internet-module.h"
#include "ns3/nix-vector-routing-module.h"
#include "ns3/topology-read-module.h"
#include "ns3/ipv4-address-generator.h" //necessary?
#include "ns3/point-to-point-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"

#include "ron-header.h"
#include "ron-helper.h"
#include "ron-client.h"
#include "ron-server.h"
#include "region.h"

#include <iostream>
#include <sstream>
#include <map>

// prefer boost's map when not iterating as it's a hash map
#include "boost/unordered_map.hpp" 
#include "boost/filesystem.hpp"
#include "boost/lexical_cast.hpp"

namespace ns3 {

// Some useful HELPER FUNCTIONS
Ipv4Address GetNodeAddress(Ptr<Node> node);
uint32_t GetNodeDegree(Ptr<Node> node);

//forward declaration to allow helper functions' use
class RonPeerTable;

class GeocronExperiment {
public:
  GeocronExperiment ();

  void ReadTopology (std::string topologyFile);
  void ReadLatencyFile (std::string latencyFile);
  void ReadLocationFile (std::string locationFile);
  void RunAllScenarios ();
  void Run ();
  /*void SetHeuristic (int newHeuristic);
  void SetDisasterLocation (std::string newLocation);
  void SetFailureProbability (double newProb);*/

  void SetTimeout (Time newTimeout);
  void SetTraceFile (std::string newTraceFile);
  void ConnectAppTraces ();
  
  void SetDisasterLocation (Location newDisasterLocation);
  void SetFailureProbability (double newFailureProbability);
  //void SetHeuristic (newHeuristic);
  void NextHeuristic ();
  void NextDisasterLocation ();
  void NextFailureProbability ();

  //these rely on the Vector following
  Vector GetLocation (Ptr<Node> node);
  bool HasLocation (Ptr<Node> node);
  Vector NO_LOCATION_VECTOR;

  std::vector<ObjectFactory*> * heuristics;
  std::vector<Location> * disasterLocations;
  std::vector<double> * failureProbabilities;
  //TODO: timeouts, max retries, etc.

  // These are public to allow a command line parser to set them
  uint32_t maxNDevs;
  uint32_t contactAttempts;
  uint32_t nruns;
  uint32_t start_run_number;

private:
  /** Sets the next server for the simulation. */
  void SetNextServers ();
  void ApplyFailureModel ();
  void UnapplyFailureModel ();
  bool IsDisasterNode (Ptr<Node> node);
  void AutoSetTraceFile ();

  /** A factory to generate the current heuristic */
  ObjectFactory* currHeuristic;
  Location currLocation;
  double currFprob;
  uint32_t currRun; //keep at 32 as it's used as a string later
  Time timeout;
  Time simulationLength;

  NodeContainer nodes;
  ApplicationContainer clientApps;
  Ptr<RonPeerTable> overlayPeers;
  Ptr<RonPeerTable> serverPeers;
  std::string topologyFile;
  boost::unordered_map<Location,Vector> locations; //to actual position mapping

  RocketfuelTopologyReader::LatenciesMap latencies;

  std::string traceFile;
  Time appStopTime;

  // Random variable for determining if links fail during the disaster
  UniformVariable random;

  // These maps, indexed by disaster location name, hold nodes and ifaces of interest for the associated disaster region
  boost::unordered_map<Location, Ipv4InterfaceContainer> potentialIfacesToKill; //are the nested containers created automatically?
  std::map<Location, std::map<uint32_t, Ptr <Node> > > disasterNodes; //both random access AND iteration hmmm...
  std::map<Location, NodeContainer> serverNodeCandidates; //want to just find these once and randomly pick from later (1 entry per disaster location)
  /** Number of nodes to collect as potential server choices. */
  uint32_t nServerChoices;

  // Keep track of failed nodes/links to unfail them in between runs
  Ipv4InterfaceContainer ifacesToKill;
  NodeContainer failNodes;
};
} //namespace ns3
#endif //GEOCRON_EXPERIMENT_H