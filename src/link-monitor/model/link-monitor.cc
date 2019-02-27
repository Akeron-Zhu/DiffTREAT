/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "link-monitor.h"

#include "ns3/log.h"
#include "ns3/simulator.h"

#include <fstream>
#include <sstream>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("LinkMonitor");

NS_OBJECT_ENSURE_REGISTERED (LinkMonitor);


//返回TypeId
TypeId
LinkMonitor::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::LinkMonitor")
            .SetParent<Object> ()
            .SetGroupName ("LinkMonitor")
            .AddConstructor<LinkMonitor> ();

  return tid;
}

//LinkMonitor的初始化是开启FUNCTION级别的LOG输出
LinkMonitor::LinkMonitor ()
{ 
  NS_LOG_FUNCTION (this);
}


//添加一条链路于检测
void
LinkMonitor::AddLinkProbe (Ptr<LinkProbe> probe)
{
  m_linkProbes.push_back (probe);
}

//开始监视链路
void
LinkMonitor::Start (Time startTime)
{
  //第一个参数是开始的时间，第二个是要调用的函数，第三个是调用函数的对象
  Simulator::Schedule (startTime, &LinkMonitor::DoStart, this);
}

//停止监视链路
void
LinkMonitor::Stop (Time stopTime)
{
  Simulator::Schedule (stopTime, &LinkMonitor::DoStop, this);
}


//开始监控链路时要做的事情
//对存储于m_linkProbes中的所有要监视的路径（LinkProbe）调用Start函数
void
LinkMonitor::DoStart (void)
{
  //LinkProbe的Start函数是一个虚函数，没有LinkProbe中实现，会在继承它的类中实现
  std::vector<Ptr<LinkProbe> >::iterator itr = m_linkProbes.begin ();
  for ( ; itr != m_linkProbes.end (); ++itr)
  {
    (*itr)->Start ();
  }
}

//停止监控链路时要做的事情
//对存储于m_linkProbes中的所有要监视的路径（LinkProbe）调用Stop函数
//LinkProbe的STOP函数是一个虚函数，没有LinkProbe中实现，会在继承它的类中实现
void
LinkMonitor::DoStop (void)
{
  std::vector<Ptr<LinkProbe> >::iterator itr = m_linkProbes.begin ();
  for ( ; itr != m_linkProbes.end (); ++itr)
  {
    (*itr)->Stop ();
  }
}


//输出链路信息
void
LinkMonitor::OutputToFile (std::string filename, std::string (*formatFunc)(struct LinkProbe::LinkStats))
{
  //文本输出流
  std::ofstream os (filename.c_str (), std::ios::out|std::ios::binary);
  //遍历所有要监视的链路
  std::vector<Ptr<LinkProbe> >::iterator itr = m_linkProbes.begin ();
  //对链路信息进行输出
  for ( ; itr != m_linkProbes.end (); ++itr)
  {
    //得到这个链路
    Ptr<LinkProbe> linkProbe = *itr;
    //得到这个链路的m_stat映射，里面存储了这个链路的接口以对应的不同时间的链路探测信息
    std::map<uint32_t, std::vector<struct LinkProbe::LinkStats> > stats = linkProbe->GetLinkStats ();
    //输出信息
    os << linkProbe->GetProbeName () << ": (contain: " << stats.size () << " ports)" << std::endl;
    //遍历数组stats即输出<端口，用Default函数格式化的信息对>。
    std::map<uint32_t, std::vector<struct LinkProbe::LinkStats> >::iterator portItr = stats.begin ();
    for ( ; portItr != stats.end (); ++portItr)
    {
      os << "\tPort: " << portItr->first << " (contain " << (portItr->second).size ()  << " entries)"<< std::endl;
      os << "\t\t";
      std::vector<struct LinkProbe::LinkStats>::iterator timeItr = (portItr->second).begin ();
      for ( ; timeItr != (portItr->second).end (); ++timeItr)
      {
        os << formatFunc (*timeItr) << "\t";
      }
      os << std::endl;
    }
    os << std::endl;
  }

  os.close ();
}

//得到默认的格式，即将链路的状态按照一定格式输出
std::string
LinkMonitor::DefaultFormat (struct LinkProbe::LinkStats stat)
{
  std::ostringstream oss;
  oss << stat.txLinkUtility << "/"
      << stat.packetsInQueue << "/"
      << stat.bytesInQueue << "/"
      << stat.packetsInQueueDisc << "/"
      << stat.bytesInQueueDisc;
  return oss.str ();
}

}

