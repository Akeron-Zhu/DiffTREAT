/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef IPV4_LETFLOW_ROUTING_H
#define IPV4_LETFLOW_ROUTING_H

#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv4-route.h"
#include "ns3/object.h"
#include "ns3/packet.h"
#include "ns3/ipv4-header.h"
#include "ns3/data-rate.h"
#include "ns3/nstime.h"
#include "ns3/event-id.h"

namespace ns3 {

//构建flowlet结构体
struct LetFlowFlowlet {
  uint32_t port;
  Time activeTime;
};

//flowlet路由条目
struct LetFlowRouteEntry {
  Ipv4Address network;
  Ipv4Mask networkMask;
  uint32_t port;
};

class Ipv4LetFlowRouting : public Ipv4RoutingProtocol
{
public:
  Ipv4LetFlowRouting ();
  ~Ipv4LetFlowRouting ();

  static TypeId GetTypeId (void);

  void AddRoute (Ipv4Address network, Ipv4Mask networkMask, uint32_t port);

  /* Inherit From Ipv4RoutingProtocol */
  virtual Ptr<Ipv4Route> RouteOutput (Ptr<Packet> p, const Ipv4Header &header, Ptr<NetDevice> oif, Socket::SocketErrno &sockerr);
  virtual bool RouteInput (Ptr<const Packet> p, const Ipv4Header &header, Ptr<const NetDevice> idev,
                           UnicastForwardCallback ucb, MulticastForwardCallback mcb,
                           LocalDeliverCallback lcb, ErrorCallback ecb);
  virtual void NotifyInterfaceUp (uint32_t interface);
  virtual void NotifyInterfaceDown (uint32_t interface);
  virtual void NotifyAddAddress (uint32_t interface, Ipv4InterfaceAddress address);
  virtual void NotifyRemoveAddress (uint32_t interface, Ipv4InterfaceAddress address);
  virtual void SetIpv4 (Ptr<Ipv4> ipv4);
  virtual void PrintRoutingTable (Ptr<OutputStreamWrapper> stream) const;

  virtual void DoDispose (void);
  //查询路由表
  std::vector<LetFlowRouteEntry> LookupLetFlowRouteEntries (Ipv4Address dest);
  //构建路由项
  Ptr<Ipv4Route> ConstructIpv4Route (uint32_t port, Ipv4Address destAddress);
  //设置flowletTimeout的时间
  void SetFlowletTimeout (Time timeout);

private:
  // Flowlet Timeout
  // flowlet超时时间
  Time m_flowletTimeout;

  // Ipv4 associated with this router
  // 路由器的Ipv4变量
  Ptr<Ipv4> m_ipv4;

  // Flowlet Table
  // flowlet表
  std::map<uint32_t, LetFlowFlowlet> m_flowletTable;

  // Route table
  // 路由表
  std::vector<LetFlowRouteEntry> m_routeEntryList;
};

}

#endif /* LETFLOW_ROUTING_H */

