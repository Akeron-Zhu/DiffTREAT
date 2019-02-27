/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef LINK_PROBE_H
#define LINK_PROBE_H

#include "ns3/object.h"
#include "ns3/nstime.h"

#include <map>
#include <vector>
#include <string>

namespace ns3 {

class LinkMonitor; //这里包含了LinkMonitor

//继承Object，用于代表一条链路的状态与探测
class LinkProbe : public Object 
{
public:
  //构建一个描述链路状态的结构体LinkStats
  struct LinkStats
  {
    // The check time
    Time        checkTime;

    // The accumulated bytes transmitted on this link
    //这条链路上累积发送的字节量
    uint64_t    accumulatedTxBytes;

    // The utility of this link based on TX
    //这条链路的链路利用率
    double      txLinkUtility;

    // The accumulated bytes dequeued on this link
    // 这条链路累积的dequeued的字节量
    uint64_t    accumulatedDequeueBytes;

    // The utility of this link based on dequeue
    // 基于dequeue的字节量计算的链路利用率
    double      dequeueLinkUtility;

    //队列中的包
    uint32_t    packetsInQueue;

    //队列中的字节
    uint32_t    bytesInQueue;

    //QueueDisc中的包
    uint32_t    packetsInQueueDisc;
    //QueueDisc中的字节
    uint32_t    bytesInQueueDisc;
  };

  static TypeId GetTypeId (void);

  LinkProbe (Ptr<LinkMonitor> linkMonitor);

  std::map<uint32_t, std::vector<struct LinkStats> > GetLinkStats (void);

  void SetProbeName (std::string name);

  std::string GetProbeName (void);
  //虚函数，一定要在它的子类中实现Ipv4LinkProbe中实现了
  virtual void Start () = 0;

  virtual void Stop () = 0;

protected:
  // map <interface, list of link stats collected at different time point>
  // The later ones are inserted at the tail of the list
  //声明一个Map集合用于接口不同时间点的链路状态
  std::map<uint32_t, std::vector<struct LinkStats> > m_stats;

  // Used to help identifying the probe
  //用于确定帮助确定探测
  std::string m_probeName;
};

}

#endif /* LINK_PROBE_H */

