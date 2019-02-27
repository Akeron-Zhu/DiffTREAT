/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "ipv4-link-probe.h"

#include "ns3/log.h"
#include "ns3/config.h"
#include "ns3/simulator.h"
#include "ns3/ipv4-header.h"

#include "link-monitor.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("Ipv4LinkProbe");

NS_OBJECT_ENSURE_REGISTERED(Ipv4LinkProbe);

//返回TypeId
TypeId
Ipv4LinkProbe::GetTypeId(void)
{
  static TypeId tid = TypeId("ns3::Ipv4LinkProbe")
                          .SetParent<LinkProbe>()
                          .SetGroupName("LinkMonitor");

  return tid;
}

//初始化，后跟初始化列表，对每个接口的各个属性进行监控
Ipv4LinkProbe::Ipv4LinkProbe(Ptr<Node> node, Ptr<LinkMonitor> linkMonitor)
    : LinkProbe(linkMonitor), //因为是继承的LinkProbe类，所以初始化时需要传一个linkMonitor进去
      m_checkTime(MicroSeconds(100))
{
  NS_LOG_FUNCTION(this);
  //Ipv4L3Protocol是实际执行IP的层，包括收发包和routing
  //这个类包括两个trace source即TX与RX，一个是在NetDeviced即将发包前执行，
  //一个是在NetDevice刚收到包执行
  //这里先得到节点的m_ipv4
  m_ipv4 = node->GetObject<Ipv4L3Protocol>();


  // Notice, the interface at 0 is loopback, we simply ignore it
  // GetNInterfaces得到用户添加了多少个interface，遍历时行trace每个接口
  for (uint32_t interface = 1; interface < m_ipv4->GetNInterfaces(); ++interface)
  {
    //初始化每个接口的计数
    m_accumulatedTxBytes[interface] = 0;
    m_accumulatedDequeueBytes[interface] = 0;
    m_NPacketsInQueue[interface] = 0;
    m_NBytesInQueue[interface] = 0;
    m_NPacketsInQueueDisc[interface] = 0;
    m_NBytesInQueueDisc[interface] = 0;
    //创造一个Ipv4QueueProbe指针并存储到m_queueProbe中，Creat函数返回对应类型的指针
    m_queueProbe[interface] = Create<Ipv4QueueProbe>();
    m_queueProbe[interface]->SetInterfaceId(interface);
    m_queueProbe[interface]->SetIpv4LinkProbe(this);

    std::ostringstream oss;
    oss << "/NodeList/" << node->GetId() << "/DeviceList/" << interface << "/TxQueue/Dequeue";
    //MakeCallback告诉调用哪个Object中的哪个函数，oss.str()表示哪个对象变动时调用函数 
    Config::ConnectWithoutContext(oss.str(),
                                  MakeCallback(&Ipv4QueueProbe::DequeueLogger, m_queueProbe[interface]));

    std::ostringstream oss2;
    oss2 << "/NodeList/" << node->GetId() << "/DeviceList/" << interface << "/TxQueue/PacketsInQueue";
    Config::ConnectWithoutContext(oss2.str(),
                                  MakeCallback(&Ipv4QueueProbe::PacketsInQueueLogger, m_queueProbe[interface]));

    std::ostringstream oss3;
    oss3 << "/NodeList/" << node->GetId() << "/DeviceList/" << interface << "/TxQueue/BytesInQueue";
    Config::ConnectWithoutContext(oss3.str(),
                                  MakeCallback(&Ipv4QueueProbe::BytesInQueueLogger, m_queueProbe[interface]));

    std::ostringstream oss4;
    oss4 << "/NodeList/" << node->GetId() << "/$ns3::TrafficControlLayer/RootQueueDiscList/" << interface << "/PacketsInQueue";
    Config::ConnectWithoutContext(oss4.str(),
                                  MakeCallback(&Ipv4QueueProbe::PacketsInQueueDiscLogger, m_queueProbe[interface]));

    std::ostringstream oss5;
    oss5 << "/NodeList/" << node->GetId() << "/$ns3::TrafficControlLayer/RootQueueDiscList/" << interface << "/BytesInQueue";
    Config::ConnectWithoutContext(oss5.str(),
                                  MakeCallback(&Ipv4QueueProbe::BytesInQueueDiscLogger, m_queueProbe[interface]));
  }
  //TraceConnectWithoutContext用于连接一个TraceSource，并返回bool值表示是否成功
  if (!m_ipv4->TraceConnectWithoutContext("Tx",
                                          MakeCallback(&Ipv4LinkProbe::TxLogger, this)))
  {
    NS_FATAL_ERROR("trace fail");
  }
}

//用于设置速率
void Ipv4LinkProbe::SetDataRateAll(DataRate dataRate)
{
  //对所有的接口设置相同的速率
  for (uint32_t interface = 1; interface < m_ipv4->GetNInterfaces(); ++interface)
  {
    m_dataRate[interface] = dataRate;
  }
}

void Ipv4LinkProbe::TxLogger(Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
{
  uint32_t size = packet->GetSize();
  NS_LOG_LOGIC("Trace " << size << " bytes TX on port: " << interface);
  m_accumulatedTxBytes[interface] = m_accumulatedTxBytes[interface] + size;
}

//如何得到的packet
void Ipv4LinkProbe::DequeueLogger(Ptr<const Packet> packet, uint32_t interface)
{
  uint32_t size = packet->GetSize();
  NS_LOG_LOGIC("Trace " << size << " bytes dequeued on port: " << interface);
  m_accumulatedDequeueBytes[interface] = m_accumulatedDequeueBytes[interface] + size;
}

void Ipv4LinkProbe::PacketsInQueueLogger(uint32_t NPackets, uint32_t interface)
{
  NS_LOG_LOGIC("Packets in queue are now: " << NPackets);
  m_NPacketsInQueue[interface] = NPackets;
}

void Ipv4LinkProbe::BytesInQueueLogger(uint32_t NBytes, uint32_t interface)
{
  NS_LOG_LOGIC("Bytes in queue are now: " << NBytes);
  m_NBytesInQueue[interface] = NBytes;
}

void Ipv4LinkProbe::PacketsInQueueDiscLogger(uint32_t NPackets, uint32_t interface)
{
  NS_LOG_LOGIC("Packets in queue are now: " << NPackets);
  m_NPacketsInQueueDisc[interface] = NPackets;
}

void Ipv4LinkProbe::BytesInQueueDiscLogger(uint32_t NBytes, uint32_t interface)
{
  NS_LOG_LOGIC("Bytes in queue are now: " << NBytes);
  m_NBytesInQueueDisc[interface] = NBytes;
}

//
void Ipv4LinkProbe::CheckCurrentStatus()
{
  for (uint32_t interface = 1; interface < m_ipv4->GetNInterfaces(); ++interface)
  {
    uint64_t lastTxBytes = 0;
    uint64_t lastDequeueBytes = 0;
    //Ipv4LinkProbe是继承于LinkProbe的
    //每条链路都有一个m_stat MAP用于存储不同时间的状态信息，这里先在这个MAP中找一下是否有这个接口
    std::map<uint32_t, std::vector<struct LinkProbe::LinkStats> >::iterator itr = m_stats.find(interface);
    //如果还有存储过，就新建立了一个Vector存入。
    if (itr == m_stats.end())
    {
      struct LinkProbe::LinkStats newStats;
      newStats.checkTime = Simulator::Now();
      newStats.accumulatedTxBytes = m_accumulatedTxBytes[interface];
      newStats.txLinkUtility =
          Ipv4LinkProbe::GetLinkUtility(interface, m_accumulatedTxBytes[interface] - lastTxBytes, m_checkTime);
      newStats.accumulatedDequeueBytes = m_accumulatedDequeueBytes[interface];
      newStats.dequeueLinkUtility =
          Ipv4LinkProbe::GetLinkUtility(interface, m_accumulatedDequeueBytes[interface] - lastDequeueBytes, m_checkTime);
      newStats.packetsInQueue = m_NPacketsInQueue[interface];
      newStats.bytesInQueue = m_NBytesInQueue[interface];
      newStats.packetsInQueueDisc = m_NPacketsInQueueDisc[interface];
      newStats.bytesInQueueDisc = m_NBytesInQueueDisc[interface];
      std::vector<struct LinkProbe::LinkStats> newVector;
      newVector.push_back(newStats);
      m_stats[interface] = newVector;
    }
    else //如果已经有了就直接push到m_stat中的vector数组
    {
      lastTxBytes = (itr->second).back().accumulatedTxBytes; //得到上一次push进去的元素的信息
      lastDequeueBytes = (itr->second).back().accumulatedDequeueBytes; //计算链路利用率会减去
      struct LinkProbe::LinkStats newStats;
      newStats.checkTime = Simulator::Now();
      newStats.accumulatedTxBytes = m_accumulatedTxBytes[interface];
      //这里要减去是因为m_accumulatedTxBytes是总共积累发了多少
      newStats.txLinkUtility = 
          Ipv4LinkProbe::GetLinkUtility(interface, m_accumulatedTxBytes[interface] - lastTxBytes, m_checkTime);
      newStats.accumulatedDequeueBytes = m_accumulatedDequeueBytes[interface];
      newStats.dequeueLinkUtility =
          Ipv4LinkProbe::GetLinkUtility(interface, m_accumulatedDequeueBytes[interface] - lastDequeueBytes, m_checkTime);
      newStats.packetsInQueue = m_NPacketsInQueue[interface];
      newStats.bytesInQueue = m_NBytesInQueue[interface];
      newStats.packetsInQueueDisc = m_NPacketsInQueueDisc[interface];
      newStats.bytesInQueueDisc = m_NPacketsInQueueDisc[interface];
      (itr->second).push_back(newStats);
    }
  }

  m_checkEvent = Simulator::Schedule(m_checkTime, &Ipv4LinkProbe::CheckCurrentStatus, this);
}

//开始之后m_checkTime后又会调用CheckCurrentStatus，这个函数中又会调用Schedule，达到每m_checkTime检测一次状态
void Ipv4LinkProbe::Start()
{
  m_checkEvent = Simulator::Schedule(m_checkTime, &Ipv4LinkProbe::CheckCurrentStatus, this);
}

//停止检测状态
void Ipv4LinkProbe::Stop()
{
  m_checkEvent.Cancel();
}

//计算链路利用率
double
Ipv4LinkProbe::GetLinkUtility(uint32_t interface, uint64_t bytes, Time time)
{
  std::map<uint32_t, DataRate>::iterator itr = m_dataRate.find(interface);
  if (itr == m_dataRate.end())
  {
    return 0.0f;
  }

  return static_cast<double>(bytes * 8) / ((itr->second).GetBitRate() * time.GetSeconds());
}

void Ipv4LinkProbe::SetCheckTime(Time checkTime)
{
  m_checkTime = checkTime;
}

} // namespace ns3
