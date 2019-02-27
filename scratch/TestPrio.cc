/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <fstream>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/netanim-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TestPrio");

// ===========================================================================
//
//         node 0                 node 1
//   +----------------+    +----------------+
//   |    ns-3 TCP    |    |    ns-3 TCP    |
//   +----------------+    +----------------+
//   |    10.1.1.1    |    |    10.1.1.2    |
//   +----------------+    +----------------+
//   | point-to-point |    | point-to-point |
//   +----------------+    +----------------+
//           |                     |
//           +---------------------+
//                5 Mbps, 2 ms
//
//
// We want to look at changes in the ns-3 TCP congestion window.  We need
// to crank up a flow and hook the CongestionWindow attribute on the socket
// of the sender.  Normally one would use an on-off application to generate a
// flow, but this has a couple of problems.  First, the socket of the on-off
// application is not created until Application Start time, so we wouldn't be
// able to hook the socket (now) at configuration time.  Second, even if we
// could arrange a call after start time, the socket is not public so we
// couldn't get at it.
//
// So, we can cook up a simple version of the on-off application that does what
// we want.  On the plus side we don't need all of the complexity of the on-off
// application.  On the minus side, we don't have a helper, so we have to get
// a little more involved in the details, but this is trivial.
//
// So first, we create a socket and do the trace connect on it; then we pass
// this socket into the constructor of our simple application which we then
// install in the source node.
// ===========================================================================
//

void InstallCache(Ptr<Node> node)
{
  Ptr<Cache> t_cache = CreateObject<Cache>();
  node->SetCache(t_cache);
}

void InstallCache(NodeContainer c)
{
  for (NodeContainer::Iterator i = c.Begin(); i != c.End(); ++i)
  {
    InstallCache(*i);
  }
} //*/

int main(int argc, char *argv[])
{
#if 1
  LogComponentEnable("TestPrio", LOG_LEVEL_DEBUG);
  LogComponentEnable("PointToPointNetDevice", LOG_LEVEL_DEBUG);
  LogComponentEnable("Cache", LOG_LEVEL_DEBUG);
  LogComponentEnable("PrioQueueDisc", LOG_LEVEL_DEBUG);
  //LogComponentEnable("BulkSendApplication", LOG_LEVEL_DEBUG);
#endif
  double START_TIME = 0.0, END_TIME = 20.0;
  int ServerNum = 5, SwitchNum = 1;
  Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1400));
  std::cout << "g" << std::endl;
  Ptr<Node> Switches = CreateObject<Node>();
  NodeContainer serverNode;
  serverNode.Create(ServerNum);
  InstallCache(serverNode);
  InstallCache(Switches);
  // Switches->InitCache();
  InternetStackHelper stack;
  Ipv4GlobalRoutingHelper globalRoutingHelper;
  stack.SetRoutingHelper(globalRoutingHelper);
  stack.Install(serverNode);
  stack.Install(Switches);

  PointToPointHelper pointToPoint;
  pointToPoint.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
  pointToPoint.SetChannelAttribute("Delay", StringValue("0ms"));
  pointToPoint.SetQueue("ns3::DropTailQueue", "MaxPackets", UintegerValue(600));

  TrafficControlHelper tc;
  //tc.SetRootQueueDisc("ns3::FifoQueueDisc");
  tc.SetRootQueueDisc("ns3::PrioQueueDisc");
  tc.AddPacketFilter(0, "ns3::PrioQueueDiscFilter");

  Ipv4AddressHelper address;
  address.SetBase("10.1.1.0", "255.255.255.0");

  Ipv4InterfaceContainer interface;
  for (int i = 0; i < SwitchNum; i++)
  {
    for (int j = 0; j < ServerNum; j++)
    {
      NodeContainer part = NodeContainer(serverNode.Get(j), Switches);
      if (ServerNum - 1 == j)
      {
        pointToPoint.SetDeviceAttribute("DataRate", StringValue("10kbps"));
      }
      NetDeviceContainer netDevice = pointToPoint.Install(part);
      tc.Install(netDevice);
      interface = address.Assign(netDevice);
      address.NewNetwork();
    }
  }
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  Address destAddress(InetSocketAddress(interface.GetAddress(0), 8080));
  BulkSendHelper source("ns3::TcpSocketFactory", destAddress);
  uint8_t rtoRank = 0, sizeRank = 6;
  source.SetAttribute("SendSize", UintegerValue(1400));           //每次发送的量
  source.SetAttribute("MaxBytes", UintegerValue(14000000));       //总共发送的数量，0表示无限制
  source.SetAttribute("DelayThresh", UintegerValue(0));           //多少包过去后发生Delay
  source.SetAttribute("DelayTime", TimeValue(MicroSeconds(100))); //Delay的时间
  source.SetAttribute("RtoRank", UintegerValue(rtoRank));         //
  source.SetAttribute("SizeRank", UintegerValue(sizeRank));
  for (int i = 0; i < ServerNum - 1; i++)
  {
    ApplicationContainer sourceApp = source.Install(serverNode.Get(i));
    sourceApp.Start(Seconds(START_TIME));
    sourceApp.Stop(Seconds(END_TIME));
  }

  PacketSinkHelper sink("ns3::TcpSocketFactory",
                        InetSocketAddress(Ipv4Address::GetAny(), 8080));
  ApplicationContainer sinkApp = sink.Install(serverNode.Get(ServerNum - 1));
  sinkApp.Start(Seconds(START_TIME));
  sinkApp.Stop(Seconds(END_TIME));

  Simulator::Stop(Seconds(20));
  Simulator::Run();
  Simulator::Destroy();

  return 0;
}
