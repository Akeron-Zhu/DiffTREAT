/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "ipv4-xpath-routing.h"
#include "ns3/ipv4-xpath-tag.h"
#include "ns3/net-device.h"
#include "ns3/channel.h"
#include "ns3/node.h"
#include "ns3/log.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("Ipv4XPathRouting");

NS_OBJECT_ENSURE_REGISTERED (Ipv4XPathRouting);

Ipv4XPathRouting::Ipv4XPathRouting ()
{
  NS_LOG_FUNCTION (this);
}

Ipv4XPathRouting::~Ipv4XPathRouting ()
{
  NS_LOG_FUNCTION (this);
}

//得到TypeId
TypeId
Ipv4XPathRouting::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::Ipv4XPathRouting")
      .SetParent<Ipv4RoutingProtocol> ()
      .SetGroupName ("Internet")
      .AddConstructor<Ipv4XPathRouting> ();

  return tid;
}

//不支持RouteOutput
Ptr<Ipv4Route>
Ipv4XPathRouting::RouteOutput (Ptr<Packet> p, const Ipv4Header &header, Ptr<NetDevice> oif,
        Socket::SocketErrno &sockerr)
{
  NS_LOG_ERROR (this << " XPath routing is not support for local routing output");
  return 0;
}

//Route an input packet (to be forwarded or locally delivered)
bool
Ipv4XPathRouting::RouteInput (Ptr<const Packet> p, const Ipv4Header &header, Ptr<const NetDevice> idev,
                           UnicastForwardCallback ucb, MulticastForwardCallback mcb,
                           LocalDeliverCallback lcb, ErrorCallback ecb)
{
  NS_ASSERT (m_ipv4->GetInterfaceForDevice (idev) >= 0);

  Ptr<Packet> packet = ConstCast<Packet> (p);

  //得到Ipv4Address
  Ipv4Address destAddress = header.GetDestination();

  // XPath routing only supports unicast
  //XPath只支持单播
  if (destAddress.IsMulticast() || destAddress.IsBroadcast()) {
    NS_LOG_ERROR (this << " XPath routing only supports unicast");
    ecb (packet, header, Socket::ERROR_NOROUTETOHOST);
    return false;
  }

  // Check if input device supports IP forwarding
  // 检查输入设备是否支持IP协议
  uint32_t iif = m_ipv4->GetInterfaceForDevice (idev);
  if (m_ipv4->IsForwarding (iif) == false) {
    NS_LOG_ERROR (this << " Forwarding disabled for this interface");
    ecb (packet, header, Socket::ERROR_NOROUTETOHOST);
    return false;
  }

  // 得到Ipv4XPathTag，同时去除了包中XpathTag也。
  Ipv4XPathTag ipv4XPathTag;
  bool found = packet->RemovePacketTag (ipv4XPathTag);
  if (!found)
  {
    NS_LOG_ERROR (this << " Cannot perform XPath routing without knowing the Path ID");
    ecb (packet, header, Socket::ERROR_NOROUTETOHOST);
    return false;
  }
  
  //得到pathId
  uint32_t pathId = ipv4XPathTag.GetPathId ();
  
  //如果是最后一跳，则不处理
  if (pathId == 0)
  {
    NS_LOG_LOGIC (this << " Reaching final hop, XPath will not handle the final hop");
    ecb (packet, header, Socket::ERROR_NOROUTETOHOST);
    return false;
  }
  
  //取pathId的百位与个位当做端口号，如pathId=988则端口为88
  uint32_t currentPort = pathId - (pathId / 100) * 100;

  // NS_LOG_LOGIC (this << " Current port is: " << currentPort);

  // std::cout << "Path: " << pathId << ", Current Port: " << currentPort << std::endl;

  //如果端口号大于这个地址有的总端口，则报错
  if (currentPort > m_ipv4->GetNInterfaces ())
  {
    NS_LOG_ERROR (this << " Port number error");
    ecb (packet, header, Socket::ERROR_NOROUTETOHOST);
    return false;
  }

  NS_LOG_LOGIC (this << " Forwarding packet: " << packet << " to port: " << currentPort);
  //将端口号组成了一个数字比如102030表示先从30端口再从20端口最后10端口，就是这样
  ipv4XPathTag.SetPathId (pathId / 100);
  //在这里再将Tag加回
  packet->AddPacketTag (ipv4XPathTag);

  //创建路由对象并设置下一跳地址
  Ptr<NetDevice> dev = m_ipv4->GetNetDevice (currentPort);
  Ptr<Channel> channel = dev->GetChannel ();
  uint32_t otherEnd = (channel->GetDevice (0) == dev) ? 1 : 0;
  Ptr<Node> nextHop = channel->GetDevice (otherEnd)->GetNode ();
  uint32_t nextIf = channel->GetDevice (otherEnd)->GetIfIndex ();
  Ipv4Address nextHopAddr = nextHop->GetObject<Ipv4>()->GetAddress (nextIf, 0).GetLocal ();
  Ptr<Ipv4Route> route = Create<Ipv4Route> ();
  route->SetOutputDevice (m_ipv4->GetNetDevice (currentPort));
  route->SetGateway (nextHopAddr);
  route->SetSource (m_ipv4->GetAddress (currentPort, 0).GetLocal ());
  route->SetDestination (destAddress);

  ucb (route, packet, header);

  return true;
}

void
Ipv4XPathRouting::NotifyInterfaceUp (uint32_t interface)
{

}

void
Ipv4XPathRouting::NotifyInterfaceDown (uint32_t interface)
{

}

void
Ipv4XPathRouting::NotifyAddAddress (uint32_t interface, Ipv4InterfaceAddress address)
{

}

void
Ipv4XPathRouting::NotifyRemoveAddress (uint32_t interface, Ipv4InterfaceAddress address)
{

}

void
Ipv4XPathRouting::SetIpv4 (Ptr<Ipv4> ipv4)
{
  NS_LOG_LOGIC (this << "Setting up Ipv4: " << ipv4);
  NS_ASSERT (m_ipv4 == 0 && ipv4 != 0);
  m_ipv4 = ipv4;
}

void
Ipv4XPathRouting::PrintRoutingTable (Ptr<OutputStreamWrapper> stream) const
{

}

void
Ipv4XPathRouting::DoDispose (void)
{
  m_ipv4 = 0;
  Ipv4RoutingProtocol::DoDispose ();
}

}

