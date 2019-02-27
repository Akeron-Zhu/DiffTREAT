/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "ipv4-conga-routing.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/net-device.h"
#include "ns3/channel.h"
#include "ns3/node.h"
#include "ns3/flow-id-tag.h"
#include "ipv4-conga-tag.h"

#include <algorithm>

#define LOOPBACK_PORT 0

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("Ipv4CongaRouting");

NS_OBJECT_ENSURE_REGISTERED(Ipv4CongaRouting);

Ipv4CongaRouting::Ipv4CongaRouting() : // Parameters
                                       m_isLeaf(false),
                                       m_leafId(0),
                                       m_tdre(MicroSeconds(200)),
                                       m_alpha(0.2),
                                       m_C(DataRate("1Gbps")),
                                       m_Q(3),
                                       m_agingTime(MilliSeconds(10)),
                                       m_flowletTimeout(MicroSeconds(50)), // The default value of flowlet timeout is small for experimental purpose
                                       m_ecmpMode(false),
                                       // Variables
                                       m_feedbackIndex(0),
                                       m_dreEvent(),
                                       m_agingEvent(),
                                       m_ipv4(0)
{
  NS_LOG_FUNCTION(this);
}

Ipv4CongaRouting::~Ipv4CongaRouting()
{
  NS_LOG_FUNCTION(this);
}

TypeId
Ipv4CongaRouting::GetTypeId(void)
{
  static TypeId tid = TypeId("ns3::Ipv4CongaRouting")
                          .SetParent<Object>()
                          .SetGroupName("Internet")
                          .AddConstructor<Ipv4CongaRouting>();

  return tid;
}

//在设置leafId时将m_isLeaf设为true，并且设置Id号
void Ipv4CongaRouting::SetLeafId(uint32_t leafId)
{
  m_isLeaf = true;
  m_leafId = leafId;
}

//设置flowlet的timeout
void Ipv4CongaRouting::SetFlowletTimeout(Time timeout)
{
  m_flowletTimeout = timeout;
}

//设置DRE算法中参数
void Ipv4CongaRouting::SetAlpha(double alpha)
{
  m_alpha = alpha;
}

void Ipv4CongaRouting::SetTDre(Time time)
{
  m_tdre = time;
}

//设置链路速率
void Ipv4CongaRouting::SetLinkCapacity(DataRate dataRate)
{
  m_C = dataRate;
}

//设置某一个接口的链路速率
void Ipv4CongaRouting::SetLinkCapacity(uint32_t interface, DataRate dataRate)
{
  m_Cs[interface] = dataRate;
}

//设置一个Q值
void Ipv4CongaRouting::SetQ(uint32_t q)
{
  m_Q = q;
}

//添加到某个地址的叶结点
void Ipv4CongaRouting::AddAddressToLeafIdMap(Ipv4Address addr, uint32_t leafId)
{
  m_ipLeafIdMap[addr] = leafId; //表示可以从这个Leaf到达这个addr
}

//TODO
void Ipv4CongaRouting::EnableEcmpMode()
{
  m_ecmpMode = true;
}

// 产初始化对应交换机的指定端口的拥塞度量
void Ipv4CongaRouting::InitCongestion(uint32_t leafId, uint32_t port, uint32_t congestion)
{
  std::map<uint32_t, std::map<uint32_t, std::pair<Time, uint32_t> > >::iterator itr =
      m_congaToLeafTable.find(leafId);
  if (itr != m_congaToLeafTable.end())
  {
    (itr->second)[port] = std::make_pair(Simulator::Now(), congestion);
  }
  else
  {
    std::map<uint32_t, std::pair<Time, uint32_t> > newMap;
    newMap[port] = std::make_pair(Simulator::Now(), congestion);
    m_congaToLeafTable[leafId] = newMap;
  }
}

//添加一条路由条目到路由表
void Ipv4CongaRouting::AddRoute(Ipv4Address network, Ipv4Mask networkMask, uint32_t port)
{
  NS_LOG_LOGIC(this << " Add Conga routing entry: " << network << "/" << networkMask << " would go through port: " << port);
  CongaRouteEntry congaRouteEntry;
  congaRouteEntry.network = network;
  congaRouteEntry.networkMask = networkMask;
  congaRouteEntry.port = port;
  m_routeEntryList.push_back(congaRouteEntry);
}

//查找路由表
std::vector<CongaRouteEntry>
Ipv4CongaRouting::LookupCongaRouteEntries(Ipv4Address dest)
{
  std::vector<CongaRouteEntry> congaRouteEntries;
  std::vector<CongaRouteEntry>::iterator itr = m_routeEntryList.begin();
  for (; itr != m_routeEntryList.end(); ++itr)
  {
    if ((*itr).networkMask.IsMatch(dest, (*itr).network))
    {
      congaRouteEntries.push_back(*itr);
    }
  }
  return congaRouteEntries;
}

//得到下一跳，创建并返回一个路由对象
Ptr<Ipv4Route>
Ipv4CongaRouting::ConstructIpv4Route(uint32_t port, Ipv4Address destAddress)
{
  //得到这个端口对应的NetDevice
  Ptr<NetDevice> dev = m_ipv4->GetNetDevice(port);
  //得到这个端口对应的channel
  Ptr<Channel> channel = dev->GetChannel();
  //检查index 0是否对应这个NetDevice，如果是另一端则对应在index 1
  uint32_t otherEnd = (channel->GetDevice(0) == dev) ? 1 : 0;
  //得到下一跳的节点和接口下标
  Ptr<Node> nextHop = channel->GetDevice(otherEnd)->GetNode();
  uint32_t nextIf = channel->GetDevice(otherEnd)->GetIfIndex();
  //得到下一跳的IP地址
  Ipv4Address nextHopAddr = nextHop->GetObject<Ipv4>()->GetAddress(nextIf, 0).GetLocal();
  //创建并返回一个路由对象
  Ptr<Ipv4Route> route = Create<Ipv4Route>();
  route->SetOutputDevice(m_ipv4->GetNetDevice(port));
  route->SetGateway(nextHopAddr);
  route->SetSource(m_ipv4->GetAddress(port, 0).GetLocal());
  route->SetDestination(destAddress);
  return route;
}

//主动发包时调用
Ptr<Ipv4Route>
Ipv4CongaRouting::RouteOutput(Ptr<Packet> packet, const Ipv4Header &header, Ptr<NetDevice> oif, Socket::SocketErrno &sockerr)
{
  NS_LOG_ERROR(this << " Conga routing is not support for local routing output");
  return 0;
}


//转发时调用
bool Ipv4CongaRouting::RouteInput(Ptr<const Packet> p, const Ipv4Header &header, Ptr<const NetDevice> idev,
                                  UnicastForwardCallback ucb, MulticastForwardCallback mcb,
                                  LocalDeliverCallback lcb, ErrorCallback ecb)
{
  NS_LOG_LOGIC(this << " RouteInput: " << p << "Ip header: " << header);

  NS_ASSERT(m_ipv4->GetInterfaceForDevice(idev) >= 0);
  //得到包指针
  Ptr<Packet> packet = ConstCast<Packet>(p);
  //从头部得到地址
  Ipv4Address destAddress = header.GetDestination();

  //CONGA只支持单播
  // Conga routing only supports unicast
  if (destAddress.IsMulticast() || destAddress.IsBroadcast())
  {
    NS_LOG_ERROR(this << " Conga routing only supports unicast");
    ecb(packet, header, Socket::ERROR_NOROUTETOHOST); //Error callback
    return false;
  }

  // Check if input device supports IP forwarding
  uint32_t iif = m_ipv4->GetInterfaceForDevice(idev);
  //IsForwarding对输入的包如果支持路由则返回true
  if (m_ipv4->IsForwarding(iif) == false)
  {
    NS_LOG_ERROR(this << " Forwarding disabled for this interface");
    ecb(packet, header, Socket::ERROR_NOROUTETOHOST);
    return false;
  }

  // P.acket arrival time
  Time now = Simulator::Now();

  // Extract the flow id
  uint32_t flowId = 0;
  FlowIdTag flowIdTag;
  //Search a matching tag and call Tag::Deserialize if it is found.
  //如果找到FlowIdTag返回true，否则返回false
  bool flowIdFound = packet->PeekPacketTag(flowIdTag);
  //如果找不到对应的flowID则返回不能路由
  if (!flowIdFound)
  {
    NS_LOG_ERROR(this << " Conga routing cannot extract the flow id");
    ecb(packet, header, Socket::ERROR_NOROUTETOHOST);
    return false;
  }
  //否则得到flowId
  flowId = flowIdTag.GetFlowId();
  //查询路由表得到可以到达目的地的一系列的routerEntry
  std::vector<CongaRouteEntry> routeEntries = Ipv4CongaRouting::LookupCongaRouteEntries(destAddress);
  //如果查表没得到记录，则不能路由
  if (routeEntries.empty())
  {
    NS_LOG_ERROR(this << " Conga routing cannot find routing entry");
    ecb(packet, header, Socket::ERROR_NOROUTETOHOST);
    return false;
  }

  // Dev use
  //如果使用ECMP形式，则在查询到的条目中用如下算法选择
  if (m_ecmpMode)
  {
    uint32_t selectedPort = routeEntries[flowId % routeEntries.size()].port;
    //通过这个端口路由，建立一个route对象
    Ptr<Ipv4Route> route = Ipv4CongaRouting::ConstructIpv4Route(selectedPort, destAddress);
    ucb(route, packet, header); //UnicastForwardCallback
  }

  // Turn on DRE event scheduler if it is not running
  //如果DRE算法没有运行，则开启DRE算法
  if (!m_dreEvent.IsRunning())
  {
    NS_LOG_LOGIC(this << " Conga routing restarts dre event scheduling");
    //调度DRE算法
    m_dreEvent = Simulator::Schedule(m_tdre, &Ipv4CongaRouting::DreEvent, this);
  }

  // Turn on aging event scheduler if it is not running
  //调度老化事件开始运行
  if (!m_agingEvent.IsRunning())
  {
    NS_LOG_LOGIC(this << "Conga routing restarts aging event scheduling");
    m_agingEvent = Simulator::Schedule(m_agingTime / 4, &Ipv4CongaRouting::AgingEvent, this);
  }

  // First, check if this switch if leaf switch
  if (m_isLeaf)
  {
    // If the switch is leaf switch, two possible situations
    // 1. The sender is connected to this leaf switch
    // 2. The receiver is connected to this leaf switch
    // We can distinguish it by checking whether the packet has CongaTag
    //检查是否有ipv4CongaTag，因为一个包在发送端处加上CongaTag，如果没有的话表明是发送端
    Ipv4CongaTag ipv4CongaTag;
    bool found = packet->PeekPacketTag(ipv4CongaTag);

    //如果没有证明是发送过去的包
    if (!found)
    {
      uint32_t selectedPort;

      // When sending a new packet
      // Build an empty Conga header (as the packet tag)
      // Determine the port and fill the header fields

      Ipv4CongaRouting::PrintDreTable();
      Ipv4CongaRouting::PrintCongaToLeafTable();
      Ipv4CongaRouting::PrintFlowletTable();

      // Determine the dest switch leaf id
      //得到目的地址的叶结点ID
      std::map<Ipv4Address, uint32_t>::iterator itr = m_ipLeafIdMap.find(destAddress);
      if (itr == m_ipLeafIdMap.end())
      {
        NS_LOG_ERROR(this << " Conga routing cannot find leaf switch id");
        ecb(packet, header, Socket::ERROR_NOROUTETOHOST);
        return false;
      }
      uint32_t destLeafId = itr->second;

      // Check piggyback information
      //fbItr是一个二维表，分别是交换机，端口与对应的拥塞度量
      std::map<uint32_t, std::map<uint32_t, FeedbackInfo> >::iterator fbItr =
          m_congaFromLeafTable.find(destLeafId);

      uint32_t fbLbTag = LOOPBACK_PORT;
      uint32_t fbMetric = 0;

      // Piggyback according to round robin and favoring those that has been changed
      //按round robin的规则带回信息，并且更偏向那些改变的值。
      if (fbItr != m_congaFromLeafTable.end())
      {
        //innerFbItr是一个交换机节点是所有端口对应的congestion metric
        std::map<uint32_t, FeedbackInfo>::iterator innerFbItr = (fbItr->second).begin();
        std::advance(innerFbItr, m_feedbackIndex++ % (fbItr->second).size()); // round robin
        //innerFbitr->second是一个FeedbackInfo对象
        if ((innerFbItr->second).change == false) // prefer the changed ones
        {
          for (unsigned loopIndex = 0; loopIndex < (fbItr->second).size(); loopIndex++) // prevent infinite looping
          {
            //相当于遍历一遍所有的端口，因为有可能不是从头开始的，所以到最后要变到第一个
            if (++innerFbItr == (fbItr->second).end())
            {
              innerFbItr = (fbItr->second).begin(); // start from the beginning
            }
            //如果这其中有改变了的，就break，并返回，如果都没有改变，就顺序返回。
            if ((innerFbItr->second).change == true)
            {
              break;
            }
          }
        }

        fbLbTag = innerFbItr->first;
        fbMetric = (innerFbItr->second).ce;
        (innerFbItr->second).change = false;
      }

      // Port determination logic:
      // Firstly, check the flowlet table to see whether there is existing flowlet
      // If not hit, determine the port based on the congestion degree of the link

      // Flowlet table look up
      struct Flowlet *flowlet = NULL;

      // If the flowlet table entry is valid, return the port
      //查询flowlet表
      std::map<uint32_t, struct Flowlet *>::iterator flowletItr = m_flowletTable.find(flowId);
      if (flowletItr != m_flowletTable.end())
      {
        //如果hit，得到flowlet的结构体，其实即端口和时间，也即flowlet表中元素
        flowlet = flowletItr->second;
        //如果表项还有效
        if (flowlet != NULL && // Impossible in normal cases
            now - flowlet->activeTime <= m_flowletTimeout)
        {
          //更新时间
          // Do not forget to update the flowlet active time
          flowlet->activeTime = now;

          //返回选择的端口信息
          // Return the port information used for routing routine to select the port
          selectedPort = flowlet->port;

          // Construct Conga Header for the packet
          //LbTag表示了选择的port，CE表示了CE位
          ipv4CongaTag.SetLbTag(selectedPort);
          ipv4CongaTag.SetCe(0);

          // Piggyback the feedback information
          //要带给另一端路由的信息
          ipv4CongaTag.SetFbLbTag(fbLbTag);
          ipv4CongaTag.SetFbMetric(fbMetric);
          packet->AddPacketTag(ipv4CongaTag); //加入packet的tag

          // Update local dre
          //更新这个端口的DRE信息
          Ipv4CongaRouting::UpdateLocalDre(header, packet, selectedPort);
          //构建一个路由条目
          Ptr<Ipv4Route> route = Ipv4CongaRouting::ConstructIpv4Route(selectedPort, destAddress);
          ucb(route, packet, header);//单播Callback

          NS_LOG_LOGIC(this << " Sending Conga on leaf switch (flowlet hit): " << m_leafId << " - LbTag: " << selectedPort << ", CE: " << 0 << ", FbLbTag: " << fbLbTag << ", FbMetric: " << fbMetric);

          return true;
        }
      }
      //如果没有hit到flowlet表中的条目
      NS_LOG_LOGIC(this << " Flowlet expires, calculate the new port");
      // Not hit. Determine the port

      // 1. Select port congestion information based on dest leaf switch id
      //得到目的叶结点的拥塞表项的向量指针。
      std::map<uint32_t, std::map<uint32_t, std::pair<Time, uint32_t> > >::iterator
          congaToLeafItr = m_congaToLeafTable.find(destLeafId);

      // 2. Prepare the candidate port
      // For a new flowlet, we pick the uplink port that minimizes the maximum of the local metric (from the local DREs)
      // and the remote metric (from the Congestion-To-Leaf Table).
      //得到一个最大值
      uint32_t minPortCongestion = (std::numeric_limits<uint32_t>::max)();

      //候选发送的端口
      std::vector<uint32_t> portCandidates;
      //遍历能达到目的结点的路由条目
      std::vector<CongaRouteEntry>::iterator routeEntryItr = routeEntries.begin();
      for (; routeEntryItr != routeEntries.end(); ++routeEntryItr)
      {
        //得到每个路由对应的端口，初始化两个变量，一个本地拥塞度量和一个远程拥塞度量，用于取最小值
        uint32_t port = (*routeEntryItr).port;
        uint32_t localCongestion = 0;
        uint32_t remoteCongestion = 0;
        //得到这个端口本地量化后的拥塞度量
        std::map<uint32_t, uint32_t>::iterator localCongestionItr = m_XMap.find(port);
        if (localCongestionItr != m_XMap.end())
        {
          localCongestion = Ipv4CongaRouting::QuantizingX(port, localCongestionItr->second);
        }

        //得到远端的拥塞度量
        std::map<uint32_t, std::pair<Time, uint32_t> >::iterator remoteCongestionItr =
            (congaToLeafItr->second).find(port);
        if (remoteCongestionItr != (congaToLeafItr->second).end())
        {
          remoteCongestion = (remoteCongestionItr->second).second;
        }
        //取两者中的较大值
        uint32_t congestionDegree = std::max(localCongestion, remoteCongestion);

        //如果比当前最小值小就替换最小值并且将其放入数组
        if (congestionDegree < minPortCongestion)
        {
          // Strictly better port
          minPortCongestion = congestionDegree;
          portCandidates.clear();
          portCandidates.push_back(port);
        }
        //如果与当前最小相等也push
        if (congestionDegree == minPortCongestion)
        {
          // Equally good port
          portCandidates.push_back(port);
        }
      }
      //从其中选择一个端口并且倾向于已缓存的端口
      // 3. Select one port from all those candidate ports
      if (flowlet != NULL &&
          std::find(portCandidates.begin(), portCandidates.end(), flowlet->port) != portCandidates.end())
      {
        // Prefer the port cached in flowlet table
        selectedPort = flowlet->port;
        // Activate the flowlet entry again
        flowlet->activeTime = now;
      }
      else //如果已缓存的端口不在候选中，则随机选择一个端口
      {
        // If there are no cached ports, we randomly choose a good port
        selectedPort = portCandidates[rand() % portCandidates.size()];
        //如果flowlet表中没有表项则新建立一个
        if (flowlet == NULL)
        {
          struct Flowlet *newFlowlet = new Flowlet;
          newFlowlet->port = selectedPort;
          newFlowlet->activeTime = now;
          m_flowletTable[flowId] = newFlowlet;
        }
        else //否则只更改端口与时间即可
        {
          flowlet->port = selectedPort;
          flowlet->activeTime = now;
        }
      }
      //构建Conga的头部
      // 4. Construct Conga Header for the packet
      ipv4CongaTag.SetLbTag(selectedPort);
      ipv4CongaTag.SetCe(0);

      //设置要反馈的信息
      // Piggyback the feedback information
      ipv4CongaTag.SetFbLbTag(fbLbTag);
      ipv4CongaTag.SetFbMetric(fbMetric);
      //然后将tag加入到包中
      packet->AddPacketTag(ipv4CongaTag);

      // Update local dre
      //更新本地DRE
      Ipv4CongaRouting::UpdateLocalDre(header, packet, selectedPort);

      //构建一个route对象，调用单播的CallBack
      Ptr<Ipv4Route> route = Ipv4CongaRouting::ConstructIpv4Route(selectedPort, destAddress);
      ucb(route, packet, header);

      NS_LOG_LOGIC(this << " Sending Conga on leaf switch: " << m_leafId << " - LbTag: " << selectedPort << ", CE: " << 0 << ", FbLbTag: " << fbLbTag << ", FbMetric: " << fbMetric);

      return true;
    }
    else //如果有的话表明是接收端
    {
      NS_LOG_LOGIC(this << " Receiving Conga - LbTag: " << ipv4CongaTag.GetLbTag()
                        << ", CE: " << ipv4CongaTag.GetCe()
                        << ", FbLbTag: " << ipv4CongaTag.GetFbLbTag()
                        << ", FbMetric: " << ipv4CongaTag.GetFbMetric());

      // Forwarding the packet to destination

      // Determine the source switch leaf id
      //得到源路由的id用于将带来的信息存储到CongaFromLeafTable
      std::map<Ipv4Address, uint32_t>::iterator itr = m_ipLeafIdMap.find(header.GetSource());
      if (itr == m_ipLeafIdMap.end())
      {
        NS_LOG_ERROR(this << " Conga routing cannot find leaf switch id");
        ecb(packet, header, Socket::ERROR_NOROUTETOHOST);
        return false;
      }
      uint32_t sourceLeafId = itr->second;

      // 1. Update the CongaFromLeafTable
      //更新CongaFromLeafTable表，并标记更新标识
      std::map<uint32_t, std::map<uint32_t, FeedbackInfo> >::iterator fromLeafItr = m_congaFromLeafTable.find(sourceLeafId);
      //如果表中本来不存在则新加入
      if (fromLeafItr == m_congaFromLeafTable.end())
      {
        std::map<uint32_t, FeedbackInfo> newMap;
        FeedbackInfo feedbackInfo;
        feedbackInfo.ce = ipv4CongaTag.GetCe();
        feedbackInfo.change = true;
        feedbackInfo.updateTime = Simulator::Now();
        newMap[ipv4CongaTag.GetLbTag()] = feedbackInfo;
        m_congaFromLeafTable[sourceLeafId] = newMap;
      }
      else
      {
        //如果存在这个表项，但是不存在这个端口，新建一个存入
        std::map<uint32_t, FeedbackInfo>::iterator innerItr = (fromLeafItr->second).find(ipv4CongaTag.GetLbTag());
        if (innerItr == (fromLeafItr->second).end())
        {
          FeedbackInfo feedbackInfo;
          feedbackInfo.ce = ipv4CongaTag.GetCe();
          feedbackInfo.change = true;
          feedbackInfo.updateTime = Simulator::Now();
          (fromLeafItr->second)[ipv4CongaTag.GetLbTag()] = feedbackInfo;
        }
        else//都存在的话直接更新
        {
          (innerItr->second).ce = ipv4CongaTag.GetCe();
          (innerItr->second).change = true;
          (innerItr->second).updateTime = Simulator::Now();
        }
      }

      // 2. Update the CongaToLeafTable
      if (ipv4CongaTag.GetFbLbTag() != LOOPBACK_PORT)
      {
        //查找源结点的id并用于更新CongaToLeafTable
        std::map<uint32_t, std::map<uint32_t, std::pair<Time, uint32_t> > >::iterator toLeafItr =
            m_congaToLeafTable.find(sourceLeafId);
        if (toLeafItr != m_congaToLeafTable.end())
        {
          (toLeafItr->second)[ipv4CongaTag.GetFbLbTag()] =
              std::make_pair(Simulator::Now(), ipv4CongaTag.GetFbMetric());
        }
        else //否则新建立一个条目
        {
          std::map<uint32_t, std::pair<Time, uint32_t> > newMap;
          newMap[ipv4CongaTag.GetFbLbTag()] =
              std::make_pair(Simulator::Now(), ipv4CongaTag.GetFbMetric());
          m_congaToLeafTable[sourceLeafId] = newMap;
        }
      }

      // Not necessary
      // Remove the Conga Header
      //移除CongaTag的头部
      packet->RemovePacketTag(ipv4CongaTag);

      // Pick port using standard ECMP
      //使用ECMP选择一个端口
      uint32_t selectedPort = routeEntries[flowId % routeEntries.size()].port;
      //更新本地的DRE之后
      Ipv4CongaRouting::UpdateLocalDre(header, packet, selectedPort);
      //建立一个路由对象并调用单播Callback
      Ptr<Ipv4Route> route = Ipv4CongaRouting::ConstructIpv4Route(selectedPort, destAddress);
      ucb(route, packet, header);

      Ipv4CongaRouting::PrintDreTable();
      Ipv4CongaRouting::PrintCongaToLeafTable();
      Ipv4CongaRouting::PrintCongaFromLeafTable();

      return true;
    }
  }
  else //如果不是叶结点路由器，而是骨干路由器
  {
    // If the switch is spine switch
    // Extract Conga Header
    Ipv4CongaTag ipv4CongaTag;
    bool found = packet->PeekPacketTag(ipv4CongaTag);
    //如果找不到CONGA的Tag,则报错
    if (!found)
    {
      NS_LOG_ERROR(this << "Conga routing cannot extract Conga Header in spine switch");
      ecb(p, header, Socket::ERROR_NOROUTETOHOST);
      return false;
    }

    // Determine the port using standard ECMP
    //如果找得到就在可选的端口中使用ECMP
    uint32_t selectedPort = routeEntries[flowId % routeEntries.size()].port;

    // Update local dre
    //更新本地的DRE
    uint32_t X = Ipv4CongaRouting::UpdateLocalDre(header, packet, selectedPort);

    NS_LOG_LOGIC(this << " Forwarding Conga packet, Quantized X on port: " << selectedPort
                      << " is: " << Ipv4CongaRouting::QuantizingX(selectedPort, X)
                      << ", LbTag in Conga header is: " << ipv4CongaTag.GetLbTag()
                      << ", CE in Conga header is: " << ipv4CongaTag.GetCe()
                      << ", packet size is: " << packet->GetSize());
    //将拥塞根据提供的bit数量化
    uint32_t quantizingX = Ipv4CongaRouting::QuantizingX(selectedPort, X);

    // Compare the X with that in the Conga Header
    //与头部中的拥塞度量比较，如果大就更新 
    if (quantizingX > ipv4CongaTag.GetCe())
    {
      ipv4CongaTag.SetCe(quantizingX);
      packet->ReplacePacketTag(ipv4CongaTag);
    }

    //建立路由对象，并且调用callback
    Ptr<Ipv4Route> route = Ipv4CongaRouting::ConstructIpv4Route(selectedPort, destAddress);
    ucb(route, packet, header);

    return true;
  }
}

void Ipv4CongaRouting::NotifyInterfaceUp(uint32_t interface)
{
}

void Ipv4CongaRouting::NotifyInterfaceDown(uint32_t interface)
{
}

void Ipv4CongaRouting::NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address)
{
}

void Ipv4CongaRouting::NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address)
{
}

//如果设置IPV4
void Ipv4CongaRouting::SetIpv4(Ptr<Ipv4> ipv4)
{
  NS_LOG_LOGIC(this << "Setting up Ipv4: " << ipv4);
  NS_ASSERT(m_ipv4 == 0 && ipv4 != 0);
  m_ipv4 = ipv4;
}

void Ipv4CongaRouting::PrintRoutingTable(Ptr<OutputStreamWrapper> stream) const
{
}

//删除flowlet表并取消各种事件
void Ipv4CongaRouting::DoDispose(void)
{
  std::map<uint32_t, Flowlet *>::iterator itr = m_flowletTable.begin();
  for (; itr != m_flowletTable.end(); ++itr)
  {
    delete (itr->second);
  }
  m_dreEvent.Cancel();
  m_agingEvent.Cancel();
  m_ipv4 = 0;
  Ipv4RoutingProtocol::DoDispose();
}

//更新本地DRE信息
uint32_t
Ipv4CongaRouting::UpdateLocalDre(const Ipv4Header &header, Ptr<Packet> packet, uint32_t port)
{
  //得到原来的信息
  uint32_t X = 0;
  std::map<uint32_t, uint32_t>::iterator XItr = m_XMap.find(port);
  if (XItr != m_XMap.end())
  {
    X = XItr->second;
  }
  //更新信息
  uint32_t newX = X + packet->GetSize() + header.GetSerializedSize();
  NS_LOG_LOGIC(this << " Update local dre, new X: " << newX);
  m_XMap[port] = newX;
  return newX;
}

void Ipv4CongaRouting::DreEvent()
{
  //表示是否进入Idle状态，因为当拥塞为0时，则不需要减小了
  bool moveToIdleStatus = true;

  std::map<uint32_t, uint32_t>::iterator itr = m_XMap.begin();
  for (; itr != m_XMap.end(); ++itr)
  {
    uint32_t newX = itr->second * (1 - m_alpha);
    itr->second = newX;
    if (newX != 0)
    {
      //如果不为0就表示还未进入Idle状态
      moveToIdleStatus = false;
    }
  }

  NS_LOG_LOGIC(this << " Dre event finished, the dre table is now: ");
  Ipv4CongaRouting::PrintDreTable();

  //如果没有进入IDLE状态则每隔一段时间调用一次
  if (!moveToIdleStatus)
  {
    m_dreEvent = Simulator::Schedule(m_tdre, &Ipv4CongaRouting::DreEvent, this);
  }
  else
  {
    //否则就不进行调用了
    NS_LOG_LOGIC(this << " Dre event goes into idle status");
  }
}

//老化机制
void Ipv4CongaRouting::AgingEvent()
{
  //标志这个老化事件是否进入idle阶段
  bool moveToIdleStatus = true;
  //老化CongestiontoLeaf表
  std::map<uint32_t, std::map<uint32_t, std::pair<Time, uint32_t> > >::iterator itr =
      m_congaToLeafTable.begin();
  for (; itr != m_congaToLeafTable.end(); ++itr)
  {
    std::map<uint32_t, std::pair<Time, uint32_t> >::iterator innerItr =
        (itr->second).begin();
    for (; innerItr != (itr->second).end(); ++innerItr)
    {
      //如果设置的时间与现在相比超过老化的时间，则进行老化，即将其拥塞变为0 
      if (Simulator::Now() - (innerItr->second).first > m_agingTime)
      {
        (innerItr->second).second = 0;
      }
      else
      {
        //不允许进入老化阶段
        moveToIdleStatus = false;
      }
    }
  }
  //老化congetsionFromLeaf表，feedbackinfo中是包含时间信息的
  std::map<uint32_t, std::map<uint32_t, FeedbackInfo> >::iterator itr2 =
      m_congaFromLeafTable.begin();
  for (; itr2 != m_congaFromLeafTable.end(); ++itr2)
  {
    std::map<uint32_t, FeedbackInfo>::iterator innerItr2 =
        (itr2->second).begin();
    for (; innerItr2 != (itr2->second).end(); ++innerItr2)
    {
      if (Simulator::Now() - (innerItr2->second).updateTime > m_agingTime)
      {
        (itr2->second).erase(innerItr2);
        if ((itr2->second).empty())
        {
          m_congaFromLeafTable.erase(itr2);
        }
      }
      else
      {
        moveToIdleStatus = false;
      }
    }
  }

  //如果还没到IDLE状态，继续调用函数
  if (!moveToIdleStatus)
  {
    m_agingEvent = Simulator::Schedule(m_agingTime / 4, &Ipv4CongaRouting::AgingEvent, this);
  }
  else
  {
    NS_LOG_LOGIC(this << " Aging event goes into idle status");
  }
}

//根据提供的比特位数来量化拥塞度量
uint32_t
Ipv4CongaRouting::QuantizingX(uint32_t interface, uint32_t X)
{
  DataRate c = m_C;
  std::map<uint32_t, DataRate>::iterator itr = m_Cs.find(interface);
  if (itr != m_Cs.end())
  {
    c = itr->second;
  }
  double ratio = static_cast<double>(X * 8) / (c.GetBitRate() * m_tdre.GetSeconds() / m_alpha);
  NS_LOG_LOGIC("ratio: " << ratio);
  return static_cast<uint32_t>(ratio * std::pow(2, m_Q));
}

void Ipv4CongaRouting::PrintCongaToLeafTable()
{
  /*
  std::ostringstream oss;
  oss << "===== CongaToLeafTable For Leaf: " << m_leafId <<"=====" << std::endl;
  std::map<uint32_t, std::map<uint32_t, uint32_t> >::iterator itr = m_congaToLeafTable.begin ();
  for ( ; itr != m_congaToLeafTable.end (); ++itr )
  {
    oss << "Leaf ID: " << itr->first << std::endl<<"\t";
    std::map<uint32_t, uint32_t>::iterator innerItr = (itr->second).begin ();
    for ( ; innerItr != (itr->second).end (); ++innerItr)
    {
      oss << "{ port: "
          << innerItr->first << ", ce: "  << (innerItr->second)
          << " } ";
    }
    oss << std::endl;
  }
  oss << "============================";
  NS_LOG_LOGIC (oss.str ());
*/
}

void Ipv4CongaRouting::PrintCongaFromLeafTable()
{
  /*
  std::ostringstream oss;
  oss << "===== CongaFromLeafTable For Leaf: " << m_leafId << "=====" <<std::endl;
  std::map<uint32_t, std::map<uint32_t, FeedbackInfo> >::iterator itr = m_congaFromLeafTable.begin ();
  for ( ; itr != m_congaFromLeafTable.end (); ++itr )
  {
    oss << "Leaf ID: " << itr->first << std::endl << "\t";
    std::map<uint32_t, FeedbackInfo>::iterator innerItr = (itr->second).begin ();
    for ( ; innerItr != (itr->second).end (); ++innerItr)
    {
      oss << "{ port: "
          << innerItr->first << ", ce: "  << (innerItr->second).ce
          << ", change: " << (innerItr->second).change
          << " } ";
    }
    oss << std::endl;
  }
  oss << "==============================";
  NS_LOG_LOGIC (oss.str ());
*/
}

void Ipv4CongaRouting::PrintFlowletTable()
{
  /*
  std::ostringstream oss;
  oss << "===== Flowlet For Leaf: " << m_leafId << "=====" << std::endl;
  std::map<uint32_t, Flowlet*>::iterator itr = m_flowletTable.begin ();
  for ( ; itr != m_flowletTable.end(); ++itr )
  {
    oss << "flowId: " << itr->first << std::endl << "\t"
        << "port: " << (itr->second)->port << "\t"
        << "activeTime" << (itr->second)->activeTime << std::endl;
  }
  oss << "===================";
  NS_LOG_LOGIC (oss.str ());
*/
}

void Ipv4CongaRouting::PrintDreTable()
{
  /*
  std::ostringstream oss;
  std::string switchType = m_isLeaf == true ? "leaf switch" : "spine switch";
  oss << "==== Local Dre for " << switchType << " ====" <<std::endl;
  std::map<uint32_t, uint32_t>::iterator itr = m_XMap.begin ();
  for ( ; itr != m_XMap.end (); ++itr)
  {
    oss << "port: " << itr->first <<
      ", X: " << itr->second <<
      ", Quantized X: " << Ipv4CongaRouting::QuantizingX (itr->second) <<std::endl;
  }
  oss << "=================================";
  NS_LOG_LOGIC (oss.str ());
*/
}

} // namespace ns3
