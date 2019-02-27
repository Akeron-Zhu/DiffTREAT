/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "link-probe.h"

#include "link-monitor.h"
#include "ns3/log.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("LinkProbe");

NS_OBJECT_ENSURE_REGISTERED (LinkProbe);


//返回一个TypedId
TypeId
LinkProbe::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::LinkProbe")
        .SetParent<Object> ()
        .SetGroupName ("LinkProbe");

  return tid;
}

//初始化LinkProbe将其自身加入到一个LinkMointor中，也即LinkMonitor是LinkProbe的外壳
LinkProbe::LinkProbe (Ptr<LinkMonitor> linkMonitor)
{
  linkMonitor->AddLinkProbe (this);
}

//返回存储链路状态的Map供查询
std::map<uint32_t, std::vector<struct LinkProbe::LinkStats> >
LinkProbe::GetLinkStats (void)
{
  return m_stats;
}

//设置ProbeName
void
LinkProbe::SetProbeName (std::string name)
{
  m_probeName = name;
}

//得到ProbeName字
std::string
LinkProbe::GetProbeName (void)
{
  return m_probeName;
}

}

