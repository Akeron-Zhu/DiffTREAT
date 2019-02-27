/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2009 University of Washington
 *
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
 *
 */

#include "ns3/log.h"
#include "ns3/ipv4.h"
#include "ns3/ipv4-route.h"
#include "ns3/flow-id-tag.h"
#include "ns3/node.h"
#include "ns3/ipv4-static-routing.h"
#include "ipv4-list-routing.h"
#include "ipv4-drb-tag.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("Ipv4ListRouting");

NS_OBJECT_ENSURE_REGISTERED (Ipv4ListRouting);

//返回TypeId
TypeId
Ipv4ListRouting::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::Ipv4ListRouting")
    .SetParent<Ipv4RoutingProtocol> ()
    .SetGroupName ("Internet")
    .AddConstructor<Ipv4ListRouting> ()
  ;
  return tid;
}

//初始化
Ipv4ListRouting::Ipv4ListRouting ()
  : m_ipv4 (0), m_drb (0)
{
  NS_LOG_FUNCTION (this);
}

Ipv4ListRouting::~Ipv4ListRouting ()
{
  NS_LOG_FUNCTION (this);
}

//清理 
void
Ipv4ListRouting::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  for (Ipv4RoutingProtocolList::iterator rprotoIter = m_routingProtocols.begin ();
       rprotoIter != m_routingProtocols.end (); rprotoIter++)
    {
      // Note:  Calling dispose on these protocols causes memory leak
      //        The routing protocols should not maintain a pointer to
      //        this object, so Dispose() shouldn't be necessary.
      (*rprotoIter).second = 0;
    }
  m_routingProtocols.clear ();
  m_ipv4 = 0;
  m_drb = 0;
}

//输出ListRouting的路由表
void
Ipv4ListRouting::PrintRoutingTable (Ptr<OutputStreamWrapper> stream) const
{
  NS_LOG_FUNCTION (this << stream);
  *stream->GetStream () << "Node: " << m_ipv4->GetObject<Node> ()->GetId ()
                        << ", Time: " << Now().As (Time::S)
                        << ", Local time: " << GetObject<Node> ()->GetLocalTime ().As (Time::S)
                        << ", Ipv4ListRouting table" << std::endl;
  for (Ipv4RoutingProtocolList::const_iterator i = m_routingProtocols.begin ();
       i != m_routingProtocols.end (); i++)
    {
      *stream->GetStream () << "  Priority: " << (*i).first << " Protocol: " << (*i).second->GetInstanceTypeId () << std::endl;
      (*i).second->PrintRoutingTable (stream);
    }
}

//初始化添加进去的路由协议
void
Ipv4ListRouting::DoInitialize (void)
{
  NS_LOG_FUNCTION (this);
  for (Ipv4RoutingProtocolList::iterator rprotoIter = m_routingProtocols.begin ();
       rprotoIter != m_routingProtocols.end (); rprotoIter++)
    {
      Ptr<Ipv4RoutingProtocol> protocol = (*rprotoIter).second;
      protocol->Initialize ();
    }
  Ipv4RoutingProtocol::DoInitialize ();
}

//路由输出
Ptr<Ipv4Route>
Ipv4ListRouting::RouteOutput (Ptr<Packet> p, const Ipv4Header &header, Ptr<NetDevice> oif, enum Socket::SocketErrno &sockerr)
{
  NS_LOG_FUNCTION (this << p << header.GetDestination () << header.GetSource () << oif << sockerr);
  Ptr<Ipv4Route> route;
  //根据列表依次调用其它转文的RouteOutput函数，
  for (Ipv4RoutingProtocolList::const_iterator i = m_routingProtocols.begin ();
       i != m_routingProtocols.end (); i++)
    {
      NS_LOG_LOGIC ("Checking protocol " << (*i).second->GetInstanceTypeId () << " with priority " << (*i).first);
      NS_LOG_LOGIC ("Requesting source address for destination " << header.GetDestination ());
      route = (*i).second->RouteOutput (p, header, oif, sockerr);
      if (route)
        {
          NS_LOG_LOGIC ("Found route " << route);
          sockerr = Socket::ERROR_NOTERROR;
          return route; //如果返回了路由对象，则直接返回
        }
    }
  NS_LOG_LOGIC ("Done checking " << GetTypeId ());
  NS_LOG_LOGIC ("");
  sockerr = Socket::ERROR_NOROUTETOHOST; //否则无法路由，返回错误
  return 0;
}

// Patterned after Linux ip_route_input and ip_route_input_slow
bool
Ipv4ListRouting::RouteInput (Ptr<const Packet> p, const Ipv4Header &header, Ptr<const NetDevice> idev,
                             UnicastForwardCallback ucb, MulticastForwardCallback mcb,
                             LocalDeliverCallback lcb, ErrorCallback ecb)
{
  NS_LOG_FUNCTION (this << p << header << idev << &ucb << &mcb << &lcb << &ecb);
  bool retVal = false;
  NS_LOG_LOGIC ("RouteInput logic for node: " << m_ipv4->GetObject<Node> ()->GetId ());

  NS_ASSERT (m_ipv4 != 0);
  // Check if input device supports IP
  NS_ASSERT (m_ipv4->GetInterfaceForDevice (idev) >= 0);
  uint32_t iif = m_ipv4->GetInterfaceForDevice (idev);

  Ptr<Packet> packet = ConstCast<Packet> (p);
  Ipv4Header ipHeader = header;

  // XXX DRB support
  // We need to query the DRB index here to simulate the IP-in-IP encapsulation

  //尝试得到包的Ipv4DrbTag和FlowIdTag
  Ipv4DrbTag ipv4DrbTag;
  bool found = packet->PeekPacketTag(ipv4DrbTag);

  FlowIdTag flowIdTag;
  bool foundFlowId = packet->PeekPacketTag(flowIdTag);
  
  //如果m_drb不为0,但是没有发现DRB标签，证明在服务器上
  //在叶结点与core上，m_drb为0,所以不会进入
  //此时添加标签 并且将IP包头部的目的地址设为core路由器地址，然后将真正地址放在ipv4DrbTag
  if (m_drb != 0 && !found)
  {
    NS_LOG_DEBUG ("DRB is enabled");
    uint32_t flowId = 0;
    if (foundFlowId)
    {
      flowId = flowIdTag.GetFlowId ();
    }
    else
    {
      NS_LOG_ERROR ("Cannot find flow id in DRB");
    }
    //添加DRB标签并路由
    Ipv4Address address = m_drb->GetCoreSwitchAddress (flowId);
    if (address != Ipv4Address()) // NULL check
    {
      Ipv4DrbTag ipv4DrbTag;
      ipv4DrbTag.SetOriginalDestAddr(header.GetDestination ()); //然后将真正地址放在ipv4DrbTag
      ipHeader.SetDestination(address); //将IP头部地址设为核心路由器地址
      packet->AddPacketTag(ipv4DrbTag);
      NS_LOG_DEBUG ("Forwarding the packet to core switch: " << address);
    }
    else
    {
      NS_LOG_ERROR ("Core switch address is missing");
    }
  }

  //根据这个包到来的端口和地址判断是否可以发送到本地，根据头部地址判断，这里地址是核心路由器地址
  retVal = m_ipv4->IsDestinationAddress (header.GetDestination (), iif);
  //retVal为true表示到达了核心路由器或着到达了目的地址
  if (retVal == true)
    {
      // XXX DRB support, extract the original address
      //如果找到了DRB标签，但是不是最终的目的地址，此时在core处
      if (found && !m_ipv4->IsDestinationAddress (ipv4DrbTag.GetOriginalDestAddr(), iif))
        {
          //将头部设为最终目的地址，此时包头部与Tag中的地址是一样的
          Ipv4Address originalDestAddr = ipv4DrbTag.GetOriginalDestAddr();
          ipHeader.SetDestination(originalDestAddr);
          NS_LOG_DEBUG ("Receive DRB packet, bouncing packet to: " << originalDestAddr);
        }
      else //如果没找标签或者是最终目的地址，此时是在目的地址处
        {
          NS_LOG_LOGIC ("Address "<< header.GetDestination () << " is a match for local delivery");
          if (header.GetDestination ().IsMulticast ())
            {
              Ptr<Packet> packetCopy = packet->Copy ();
              lcb (packetCopy, header, iif);
              retVal = true;
              // Fall through
            }
          else
            {
              lcb (packet, header, iif);
              return true;
            }
        }
    }
  // Check if input device supports IP forwarding
  //检查这个端口是否支持IP转发
  if (m_ipv4->IsForwarding (iif) == false)
    {
      NS_LOG_LOGIC ("Forwarding disabled for this interface");
      ecb (packet, header, Socket::ERROR_NOROUTETOHOST);
      return false;
    }
  // Next, try to find a route
  // If we have already delivered a packet locally (e.g. multicast)
  // we suppress further downstream local delivery by nulling the callback
  //并且试图在协议列表中找到一个并进行转发，这时可心是在叶结点上，或其它方案中
  LocalDeliverCallback downstreamLcb = lcb;
  if (retVal == true)
    {
      downstreamLcb = MakeNullCallback<void, Ptr<const Packet>, const Ipv4Header &, uint32_t > ();
    }
  for (Ipv4RoutingProtocolList::const_iterator rprotoIter =
         m_routingProtocols.begin ();
       rprotoIter != m_routingProtocols.end ();
       rprotoIter++)
    {
      if ((*rprotoIter).second->RouteInput (packet, ipHeader, idev, ucb, mcb, downstreamLcb, ecb))
        {
          NS_LOG_LOGIC ("Route found to forward packet in protocol " << (*rprotoIter).second->GetInstanceTypeId ().GetName ());
          return true;
        }
    }
  // No routing protocol has found a route.
  return retVal;
}

void
Ipv4ListRouting::NotifyInterfaceUp (uint32_t interface)
{
  NS_LOG_FUNCTION (this << interface);
  for (Ipv4RoutingProtocolList::const_iterator rprotoIter =
         m_routingProtocols.begin ();
       rprotoIter != m_routingProtocols.end ();
       rprotoIter++)
    {
      (*rprotoIter).second->NotifyInterfaceUp (interface);
    }
}

void
Ipv4ListRouting::NotifyInterfaceDown (uint32_t interface)
{
  NS_LOG_FUNCTION (this << interface);
  for (Ipv4RoutingProtocolList::const_iterator rprotoIter =
         m_routingProtocols.begin ();
       rprotoIter != m_routingProtocols.end ();
       rprotoIter++)
    {
      (*rprotoIter).second->NotifyInterfaceDown (interface);
    }
}

void
Ipv4ListRouting::NotifyAddAddress (uint32_t interface, Ipv4InterfaceAddress address)
{
  NS_LOG_FUNCTION (this << interface << address);
  for (Ipv4RoutingProtocolList::const_iterator rprotoIter =
         m_routingProtocols.begin ();
       rprotoIter != m_routingProtocols.end ();
       rprotoIter++)
    {
      (*rprotoIter).second->NotifyAddAddress (interface, address);
    }
}

void
Ipv4ListRouting::NotifyRemoveAddress (uint32_t interface, Ipv4InterfaceAddress address)
{
  NS_LOG_FUNCTION (this << interface << address);
  for (Ipv4RoutingProtocolList::const_iterator rprotoIter =
         m_routingProtocols.begin ();
       rprotoIter != m_routingProtocols.end ();
       rprotoIter++)
    {
      (*rprotoIter).second->NotifyRemoveAddress (interface, address);
    }
}

//设置ipv4
void
Ipv4ListRouting::SetIpv4 (Ptr<Ipv4> ipv4)
{
  NS_LOG_FUNCTION (this << ipv4);
  NS_ASSERT (m_ipv4 == 0);
  for (Ipv4RoutingProtocolList::const_iterator rprotoIter =
         m_routingProtocols.begin ();
       rprotoIter != m_routingProtocols.end ();
       rprotoIter++)
    {
      (*rprotoIter).second->SetIpv4 (ipv4);
    }
  m_ipv4 = ipv4;
}

//添加不同的协议及优先级 在添加协议时会按优先级排序
void
Ipv4ListRouting::AddRoutingProtocol (Ptr<Ipv4RoutingProtocol> routingProtocol, int16_t priority)
{
  NS_LOG_FUNCTION (this << routingProtocol->GetInstanceTypeId () << priority);
  m_routingProtocols.push_back (std::make_pair (priority, routingProtocol));
  m_routingProtocols.sort ( Compare );
  if (m_ipv4 != 0)
    {
      routingProtocol->SetIpv4 (m_ipv4);
    }
}

//返回有多少个协议
uint32_t
Ipv4ListRouting::GetNRoutingProtocols (void) const
{
  NS_LOG_FUNCTION (this);
  return m_routingProtocols.size ();
}

//得到第index个协议
Ptr<Ipv4RoutingProtocol>
Ipv4ListRouting::GetRoutingProtocol (uint32_t index, int16_t& priority) const
{
  NS_LOG_FUNCTION (this << index << priority);
  if (index > m_routingProtocols.size ())
    {
      NS_FATAL_ERROR ("Ipv4ListRouting::GetRoutingProtocol():  index " << index << " out of range");
    }
  uint32_t i = 0;
  for (Ipv4RoutingProtocolList::const_iterator rprotoIter = m_routingProtocols.begin ();
       rprotoIter != m_routingProtocols.end (); rprotoIter++, i++)
    {
      if (i == index)
        {
          priority = (*rprotoIter).first;
          return (*rprotoIter).second;
        }
    }
  return 0;
}

//优先级比较函数，用于排序
bool
Ipv4ListRouting::Compare (const Ipv4RoutingProtocolEntry& a, const Ipv4RoutingProtocolEntry& b)
{
  NS_LOG_FUNCTION_NOARGS ();
  return a.first > b.first;
}

//设置DRB
void
Ipv4ListRouting::SetDrb(Ptr<Ipv4Drb> drb)
{
  m_drb = drb;
}

//得到DRB
Ptr<Ipv4Drb>
Ipv4ListRouting::GetDrb ()
{
  return m_drb;
}

} // namespace ns3

