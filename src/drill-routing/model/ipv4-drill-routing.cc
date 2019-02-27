/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include "ipv4-drill-routing.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/net-device.h"
#include "ns3/channel.h"
#include "ns3/node.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/traffic-control-layer.h"
#include "ns3/point-to-point-net-device.h"

#include <algorithm>
#include <limits>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("Ipv4DrillRouting");

NS_OBJECT_ENSURE_REGISTERED (Ipv4DrillRouting);


//得到TypeId
TypeId
Ipv4DrillRouting::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::Ipv4DrillRouting")
      .SetParent<Object> ()
      .SetGroupName ("DrillRouting")
      .AddConstructor<Ipv4DrillRouting> ()
      .AddAttribute ("d", "Sample d random outputs queue",
                     UintegerValue (2),
                     MakeUintegerAccessor (&Ipv4DrillRouting::m_d),
                     MakeUintegerChecker<uint32_t> ())
  ;

  return tid;
}

//初始化设置m_d为2地
Ipv4DrillRouting::Ipv4DrillRouting ()
    : m_d (2)
{
  NS_LOG_FUNCTION (this);
}

Ipv4DrillRouting::~Ipv4DrillRouting ()
{
  NS_LOG_FUNCTION (this);
}

//添加路由表
void
Ipv4DrillRouting::AddRoute (Ipv4Address network, Ipv4Mask networkMask, uint32_t port)
{
  NS_LOG_LOGIC (this << " Add Drill routing entry: " << network << "/" << networkMask << " would go through port: " << port);
  //新建一个drill路由表条目
  DrillRouteEntry drillRouteEntry;
  drillRouteEntry.network = network;
  drillRouteEntry.networkMask = networkMask;
  drillRouteEntry.port = port;
  m_routeEntryList.push_back (drillRouteEntry);
}

//查询路由表得到路径
std::vector<DrillRouteEntry>
Ipv4DrillRouting::LookupDrillRouteEntries (Ipv4Address dest)
{
  std::vector<DrillRouteEntry> drillRouteEntries;
  std::vector<DrillRouteEntry>::iterator itr = m_routeEntryList.begin ();
  for ( ; itr != m_routeEntryList.end (); ++itr)
  {
    if((*itr).networkMask.IsMatch(dest, (*itr).network))
    {
      drillRouteEntries.push_back (*itr);
    }
  }
  return drillRouteEntries;
}

//得到队列长度
uint32_t
Ipv4DrillRouting::CalculateQueueLength (uint32_t interface)
{
  //临到Ipv4L3Protocol
  Ptr<Ipv4L3Protocol> ipv4L3Protocol = DynamicCast<Ipv4L3Protocol> (m_ipv4);
  if (!ipv4L3Protocol)
  {
    NS_LOG_ERROR (this << " Drill routing cannot work other than Ipv4L3Protocol");
    return 0;
  }

  //队列总长度变量
  uint32_t totalLength = 0;
  //得到这个接口的NetDevice
  const Ptr<NetDevice> netDevice = this->m_ipv4->GetNetDevice (interface);

  //如果是P2p设备
  if (netDevice->IsPointToPoint ())
  {
    //得到它的队列长度
    Ptr<PointToPointNetDevice> p2pNetDevice = DynamicCast<PointToPointNetDevice> (netDevice);
    if (p2pNetDevice)
    {
      totalLength += p2pNetDevice->GetQueue ()->GetNBytes ();
    }
  }
  //得到trafficControlLayer层
  Ptr<TrafficControlLayer> tc = ipv4L3Protocol->GetNode ()->GetObject<TrafficControlLayer> ();
  //如果没有tc则直接返回
  if (!tc)
  {
    return totalLength;
  }
  //如果有则得到QueueDisc，然后加上这个里面的数据返回
  Ptr<QueueDisc> queueDisc = tc->GetRootQueueDiscOnDevice (netDevice);
  if (queueDisc)
  {
    totalLength += queueDisc->GetNBytes ();
  }

  return totalLength;
}

//构建一个Ipv4ROute对象，并且
Ptr<Ipv4Route>
Ipv4DrillRouting::ConstructIpv4Route (uint32_t port, Ipv4Address destAddress)
{
  Ptr<NetDevice> dev = m_ipv4->GetNetDevice (port);
  Ptr<Channel> channel = dev->GetChannel ();
  //得到另一端
  uint32_t otherEnd = (channel->GetDevice (0) == dev) ? 1 : 0;
  //得到另一端的节点
  Ptr<Node> nextHop = channel->GetDevice (otherEnd)->GetNode ();
  //得到另一端的端口号
  uint32_t nextIf = channel->GetDevice (otherEnd)->GetIfIndex ();
  //得到另一端的IP地址
  Ipv4Address nextHopAddr = nextHop->GetObject<Ipv4>()->GetAddress(nextIf,0).GetLocal();
  //构建一个路由对象
  Ptr<Ipv4Route> route = Create<Ipv4Route> ();
  route->SetOutputDevice (m_ipv4->GetNetDevice (port));
  route->SetGateway (nextHopAddr);
  route->SetSource (m_ipv4->GetAddress (port, 0).GetLocal ());
  route->SetDestination (destAddress);
  return route;
}


/* Inherit From Ipv4RoutingProtocol */
Ptr<Ipv4Route>
Ipv4DrillRouting::RouteOutput (Ptr<Packet> p, const Ipv4Header &header, Ptr<NetDevice> oif, Socket::SocketErrno &sockerr)
{
  NS_LOG_ERROR (this << " Drill routing is not support for local routing output");
  return 0;
}

//设置
bool
Ipv4DrillRouting::RouteInput (Ptr<const Packet> p, const Ipv4Header &header, Ptr<const NetDevice> idev,
                            UnicastForwardCallback ucb, MulticastForwardCallback mcb,
                            LocalDeliverCallback lcb, ErrorCallback ecb)
{
  NS_LOG_LOGIC (this << " RouteInput: " << p << "Ip header: " << header);

  NS_ASSERT (m_ipv4->GetInterfaceForDevice (idev) >= 0);

  Ptr<Packet> packet = ConstCast<Packet> (p);

  Ipv4Address destAddress = header.GetDestination();

  // DRILL routing only supports unicast
  //DRILL不支持多播
  if (destAddress.IsMulticast() || destAddress.IsBroadcast()) {
    NS_LOG_ERROR (this << " Drill routing only supports unicast");
    ecb (packet, header, Socket::ERROR_NOROUTETOHOST);
    return false;
  }

  // Check if input device supports IP forwarding
  //检查input device是否支持IP转发
  //true if IP forwarding enabled for input datagrams on this device
  uint32_t iif = m_ipv4->GetInterfaceForDevice (idev);
  if (m_ipv4->IsForwarding (iif) == false) {
    NS_LOG_ERROR (this << " Forwarding disabled for this interface");
    ecb (packet, header, Socket::ERROR_NOROUTETOHOST);
    return false; //不支持IP转发的话则返回
  }
  //对这个目的地址进行查表，得到可以转发的条目
  std::vector<DrillRouteEntry> allPorts = Ipv4DrillRouting::LookupDrillRouteEntries (destAddress);
  //如果查表后为空，则无法转发
  if (allPorts.empty ())
  {
    NS_LOG_ERROR (this << " Drill routing cannot find routing entry");
    ecb (packet, header, Socket::ERROR_NOROUTETOHOST);
    return false;
  }

  //负载最小的端口，并将负载设为最大值
  uint32_t leastLoadInterface = 0;
  uint32_t leastLoad = std::numeric_limits<uint32_t>::max ();
  //得到一个随机端口
  std::random_shuffle (allPorts.begin (), allPorts.end ());
  //从上次最好中寻找是否有过这个destAddress
  std::map<Ipv4Address, uint32_t>::iterator itr = m_previousBestQueueMap.find (destAddress);
  //如果找到后则将端口和负载都设为这个端口
  if (itr != m_previousBestQueueMap.end ())
  {
    leastLoadInterface = itr->second;
    leastLoad = CalculateQueueLength (itr->second);
  }
  //允许的采样端口数
  uint32_t sampleNum = m_d < allPorts.size () ? m_d : allPorts.size ();
  //对每个端口探测，得到这个端口的负载，并得到最小的
  for (uint32_t samplePort = 0; samplePort < sampleNum; samplePort ++)
  {
    uint32_t sampleLoad = Ipv4DrillRouting::CalculateQueueLength (allPorts[samplePort].port);
    if (sampleLoad < leastLoad)
    {
      leastLoad = sampleLoad;
      leastLoadInterface = allPorts[samplePort].port;
    }
  }

  NS_LOG_INFO (this << " Drill routing chooses interface: " << leastLoadInterface << ", since its load is: " << leastLoad);
  //将最好的端口存储到m_previousBestQueueMap中
  m_previousBestQueueMap[destAddress] = leastLoadInterface;
  //构建Ipv4Route并且从这条路径路由
  Ptr<Ipv4Route> route = Ipv4DrillRouting::ConstructIpv4Route (leastLoadInterface, destAddress);
  ucb (route, packet, header);

  return true;
}

void
Ipv4DrillRouting::NotifyInterfaceUp (uint32_t interface)
{
}

void
Ipv4DrillRouting::NotifyInterfaceDown (uint32_t interface)
{
}

void
Ipv4DrillRouting::NotifyAddAddress (uint32_t interface, Ipv4InterfaceAddress address)
{
}

void
Ipv4DrillRouting::NotifyRemoveAddress (uint32_t interface, Ipv4InterfaceAddress address)
{
}

//设置IPV4
void
Ipv4DrillRouting::SetIpv4 (Ptr<Ipv4> ipv4)
{
  NS_LOG_LOGIC (this << "Setting up Ipv4: " << ipv4);
  NS_ASSERT (m_ipv4 == 0 && ipv4 != 0);
  m_ipv4 = ipv4;
}

void
Ipv4DrillRouting::PrintRoutingTable (Ptr<OutputStreamWrapper> stream) const
{
}

void
Ipv4DrillRouting::DoDispose (void)
{
}
}

