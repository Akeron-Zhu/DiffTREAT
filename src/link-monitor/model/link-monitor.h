/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef LINK_MONITOR_H
#define LINK_MONITOR_H

#include "ns3/object.h"
#include "ns3/nstime.h"
#include "link-probe.h"

#include <vector>
#include <string>

namespace ns3 {

//继承Object用于监视多个链路的状态，也用于输出Ipv4LinkProbe中各链路的状态
class LinkMonitor : public Object
{
public:

  static TypeId GetTypeId (void);

  static std::string DefaultFormat (struct LinkProbe::LinkStats stat);

  LinkMonitor ();

  void AddLinkProbe (Ptr<LinkProbe> probe);

  void Start (Time startTime);

  void Stop (Time stopTime);

  void OutputToFile (std::string filename, std::string (*formatFunc)(struct LinkProbe::LinkStats));

private:

  void DoStart (void);

  void DoStop (void);

  //一个存储LinkProbe的向量m_linkProbes用于存储各个链路。而每个LinkProbe中又都有一个Map
  //MAP用于存储不同接口对应的不同时间的链路信息，类似于一个三维数组，分别是<链路，接口，不同时间的链路信息>
  std::vector<Ptr<LinkProbe> > m_linkProbes;

};

}

#endif /* LINK_MONITOR_H */

