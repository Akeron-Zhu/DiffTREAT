/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include "ipv4-drb-routing.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/net-device.h"
#include "ns3/channel.h"
#include "ns3/node.h"
#include "ns3/flow-id-tag.h"
#include "ns3/ipv4-xpath-tag.h"
#include "ns3/uinteger.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("Ipv4DrbRouting");

NS_OBJECT_ENSURE_REGISTERED (Ipv4DrbRouting);

//返回TypeID
TypeId
Ipv4DrbRouting::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::Ipv4DrbRouting")
      .SetParent<Object> ()
      .SetGroupName ("DRBRouting")
      .AddConstructor<Ipv4DrbRouting> ()
      .AddAttribute ("Mode", "DRB Mode, 0 for PER DEST and 1 for PER FLOW",
                    UintegerValue (1),
                    MakeUintegerAccessor (&Ipv4DrbRouting::m_mode),
                    MakeUintegerChecker<uint32_t> ())
  ;

  return tid;
}

Ipv4DrbRouting::Ipv4DrbRouting () :
    m_mode (PER_FLOW)
{
  NS_LOG_FUNCTION (this);
}

Ipv4DrbRouting::~Ipv4DrbRouting ()
{
  NS_LOG_FUNCTION (this);
}

//添加路径
bool
Ipv4DrbRouting::AddPath (uint32_t path)
{
  //非权重路径就是权重都为1
  return this->AddPath (1, path);
}

//添加有权重路径
bool
Ipv4DrbRouting::AddPath (uint32_t weight, uint32_t path)
{
  //权重路径必须与PER_FLOW模式一起用
  if (weight != 1 && m_mode != PER_FLOW)
  {
    NS_LOG_ERROR ("You have to use the PER_FLOW mode when the weight != 1");
    return false;
  }
  //将路径添加到m_paths中
  for (uint32_t i = 0; i < weight; i++)
  {
    m_paths.push_back (path);
  }
  return true;
}

/* Sorry for those very ugly code */
bool
Ipv4DrbRouting::AddWeightedPath (uint32_t weight, uint32_t path,
        const std::set<Ipv4Address>& exclusiveIPs)
{
  /**/
  // First we should add rules to the default paths table
  // 先向m_paths里添加
  Ipv4DrbRouting::AddPath (weight, path);

  // Add rules to all other tables
  // 给这张表里除了ExclusiveIP外的所有地址添加这个权重的路径
  // 如果在ExclusiveIP里面则不再添加权重，否则不加入，注意这个函数中m_extraPaths不会创建新项
  ////画一个3X3并且per_leaf_ser=1按流程走一遍，即可明白这里与ipv4-drb-routing.cc中的函数
  std::map<Ipv4Address, std::vector<uint32_t> >::iterator itr = m_extraPaths.begin ();
  for (; itr != m_extraPaths.end (); ++itr)
  {
    Ipv4Address key = itr->first;
    if (exclusiveIPs.find (key) != exclusiveIPs.end ())
    {
      continue;
    }
    std::vector<uint32_t> paths = itr->second;
    for (uint32_t i = 0; i < weight; i++)
    {
      paths.push_back (path);
    }
    m_extraPaths[key] = paths;
  }
  return true;
}


//给这个目的地址添加这个权重的路径
//画一个3X3并且per_leaf_ser=1按流程走一遍，即可明白这里与ipv4-drb-routing.cc中的函数
bool
Ipv4DrbRouting::AddWeightedPath (Ipv4Address destAddr, uint32_t weight, uint32_t path)
{
  //寻找这个目的地址的路径，如果有就用，如果没有就用m_paths，注意在这个函数里m_extraPaths才会建立新项
  std::vector<uint32_t> paths;
  std::map<Ipv4Address, std::vector<uint32_t> >::iterator itr = m_extraPaths.find (destAddr);
  if (itr != m_extraPaths.end ())
  {
    paths = itr->second;
  }
  else
  {
    paths = m_paths;//？
  }

  for (uint32_t i = 0; i < weight; i++)
  {
    paths.push_back (path);
  }

  m_extraPaths[destAddr] = paths;
  return true;
}

/* Inherit From Ipv4RoutingProtocol */
/* NOTE In DRB, the RouteOutput will not actually route the packets out but assign the path ID on it */
/* DRB relies the list routing & static routing to do the real routing */
Ptr<Ipv4Route>
Ipv4DrbRouting::RouteOutput (Ptr<Packet> p, const Ipv4Header &header, Ptr<NetDevice> oif, Socket::SocketErrno &sockerr)
{
  //如果包为空，则返回
  if (p == 0)
  {
    return 0;
  }
  //
  uint32_t flowIndentify = 0;
  //如果是PER_FLOW模式，则得到flowId
  if (m_mode == PER_FLOW)
  {
    FlowIdTag flowIdTag;
    bool found = p->PeekPacketTag (flowIdTag);
    if (!found)
    {
      sockerr = Socket::ERROR_NOROUTETOHOST;
      return 0;
    }
    flowIndentify = flowIdTag.GetFlowId ();
    NS_LOG_LOGIC ("For flow with flow id: " << flowIndentify);
  }
  else if (m_mode == PER_DEST)//如果是PER_DEST模式则返回地址
  {
    flowIndentify = header.GetDestination ().Get ();
    NS_LOG_LOGIC ("For flow with dest: " << flowIndentify);
  }
  else
  {
    sockerr = Socket::ERROR_NOROUTETOHOST;
    return 0;
  }
  
  //如果是有不对称路径，则走extraPath中的，否则走对称路径中的即可

  /* Ugly code, patch to support Weighted Presto */
  std::vector<uint32_t> paths;
  std::map<Ipv4Address, std::vector<uint32_t> >::iterator extraItr = m_extraPaths.find (header.GetDestination ());
  if (extraItr == m_extraPaths.end ())
  {
    paths = m_paths;
  }
  else
  {
    paths = extraItr->second;
  }
  /* Breathe a fresh air to celebrate the end of ugly code */


  uint32_t index = rand () % paths.size ();
  std::map<uint32_t, uint32_t>::iterator itr = m_indexMap.find (flowIndentify);
  if (itr != m_indexMap.end ())
  {
    index = itr->second;
  }

  uint32_t path = paths[index];
  m_indexMap[flowIndentify] = (index + 1) % paths.size ();

  Ipv4XPathTag ipv4XPathTag;
  ipv4XPathTag.SetPathId (path);
  p->AddPacketTag (ipv4XPathTag);

  NS_LOG_LOGIC ("\tDRB Routing has assigned path: " << path);

  sockerr = Socket::ERROR_NOTERROR;

  return 0;
}
//以上代码，第一次在可选路径中随机选择一个端口，第二次选择端口加1，即轮询
//DRB中路径也是查询得知，而不是通过XpathTag得到，但其依赖了Xpath去路由
bool
Ipv4DrbRouting::RouteInput (Ptr<const Packet> p, const Ipv4Header &header, Ptr<const NetDevice> idev,
                            UnicastForwardCallback ucb, MulticastForwardCallback mcb,
                            LocalDeliverCallback lcb, ErrorCallback ecb)
{
  NS_LOG_ERROR ("DRB can only supports end host routing");
  ecb (p, header, Socket::ERROR_NOROUTETOHOST);
  return false;
}

void
Ipv4DrbRouting::NotifyInterfaceUp (uint32_t interface)
{
}

void
Ipv4DrbRouting::NotifyInterfaceDown (uint32_t interface)
{
}

void
Ipv4DrbRouting::NotifyAddAddress (uint32_t interface, Ipv4InterfaceAddress address)
{
}

void
Ipv4DrbRouting::NotifyRemoveAddress (uint32_t interface, Ipv4InterfaceAddress address)
{
}

void
Ipv4DrbRouting::SetIpv4 (Ptr<Ipv4> ipv4)
{
  NS_LOG_LOGIC (this << "Setting up Ipv4: " << ipv4);
  NS_ASSERT (m_ipv4 == 0 && ipv4 != 0);
  m_ipv4 = ipv4;
}

void
Ipv4DrbRouting::PrintRoutingTable (Ptr<OutputStreamWrapper> stream) const
{
}

void
Ipv4DrbRouting::DoDispose (void)
{
}

}
