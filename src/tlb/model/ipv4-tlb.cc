/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include "ipv4-tlb.h"

#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/simulator.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/boolean.h"

#include <cstdio>
#include <algorithm>

#define RANDOM_BASE 100
#define SMOOTH_BASE 100

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("Ipv4TLB");

NS_OBJECT_ENSURE_REGISTERED (Ipv4TLB);

//初始化
Ipv4TLB::Ipv4TLB ():
    m_runMode (0),
    m_rerouteEnable (false),
    m_S (64000),
    m_T (MicroSeconds (1500)),
    m_K (10000),
    m_T1 (MicroSeconds (320)), // 100 200 300
    m_T2 (MicroSeconds (100000000)),
    m_agingCheckTime (MicroSeconds (25)),
    m_dreTime (MicroSeconds (30)),
    m_dreAlpha (0.2),
    m_dreDataRate (DataRate ("1Gbps")), //注意这里在初始化时属性中没有这个变量，因此要查看是否在Tcp实现中将这个数据更改，否则DRE值不准确
    m_dreQ (3),
    m_dreMultiply (5),
    m_minRtt (MicroSeconds (60)), // 50 70 100
    m_highRtt (MicroSeconds (80)),
    m_ecnSampleMin (14000),
    m_ecnPortionLow (0.1), // 0.3 0.1
    m_ecnPortionHigh (1.1), //本来1.1
    m_respondToFailure (false),
    m_flowRetransHigh (140000000),
    m_flowRetransVeryHigh (140000000),
    m_flowTimeoutCount (10000),
    m_betterPathEcnThresh (0),
    m_betterPathRttThresh (MicroSeconds (1)), // 100 200 300
    m_pathChangePoss (50),
    m_flowDieTime (MicroSeconds (1000)),
    m_isSmooth (false),
    m_smoothAlpha (50),
    m_smoothDesired (150),
    m_smoothBeta1 (101),
    m_smoothBeta2 (99),
    m_quantifyRttBase (MicroSeconds (10)),
    m_ackletTimeout (MicroSeconds (300)),
    // Added at Jan 11st
    /*
    m_epDefaultEcnPortion (0.0),
    m_epAlpha (0.5),
    m_epCheckTime (MicroSeconds (10000)),
    m_epAgingTime (MicroSeconds (10000)),
    */
    // Added at Jan 12nd
    m_flowletTimeout (MicroSeconds (5000000)),
    m_rttAlpha(1.0),
    m_ecnBeta(0.0)
{
    NS_LOG_FUNCTION (this);
}

Ipv4TLB::Ipv4TLB (const Ipv4TLB &other):
    m_runMode (other.m_runMode),
    m_rerouteEnable (other.m_rerouteEnable),
    m_S (other.m_S),
    m_T (other.m_T),
    m_K (other.m_K),
    m_T1 (other.m_T1),
    m_T2 (other.m_T2),
    m_agingCheckTime (other.m_agingCheckTime),
    m_dreTime (other.m_dreTime),
    m_dreAlpha (other.m_dreAlpha),
    m_dreDataRate (other.m_dreDataRate),
    m_dreQ (other.m_dreQ),
    m_dreMultiply (other.m_dreMultiply),
    m_minRtt (other.m_minRtt),
    m_highRtt (other.m_highRtt),
    m_ecnSampleMin (other.m_ecnSampleMin),
    m_ecnPortionLow (other.m_ecnPortionLow),
    m_ecnPortionHigh (other.m_ecnPortionHigh),
    m_respondToFailure (other.m_respondToFailure),
    m_flowRetransHigh (other.m_flowRetransHigh),
    m_flowRetransVeryHigh (other.m_flowRetransVeryHigh),
    m_flowTimeoutCount (other.m_flowTimeoutCount),
    m_betterPathEcnThresh (other.m_betterPathEcnThresh),
    m_betterPathRttThresh (other.m_betterPathRttThresh),
    m_pathChangePoss (other.m_pathChangePoss),
    m_flowDieTime (other.m_flowDieTime),
    m_isSmooth (other.m_isSmooth),
    m_smoothAlpha (other.m_smoothAlpha),
    m_smoothDesired (other.m_smoothDesired),
    m_smoothBeta1 (other.m_smoothBeta1),
    m_smoothBeta2 (other.m_smoothBeta2),
    m_quantifyRttBase (other.m_quantifyRttBase),
    m_ackletTimeout (other.m_ackletTimeout),
    /*
    m_epDefaultEcnPortion (other.m_epDefaultEcnPortion),
    m_epAlpha (other.m_epAlpha),
    m_epCheckTime (other.m_epCheckTime),
    m_epAgingTime (other.m_epAgingTime),
    */
    m_flowletTimeout (other.m_flowletTimeout),
    m_rttAlpha (other.m_rttAlpha),
    m_ecnBeta (other.m_ecnBeta)
{
    NS_LOG_FUNCTION (this);
}

//返回TypeId
TypeId
Ipv4TLB::GetTypeId (void)
{
    static TypeId tid = TypeId ("ns3::Ipv4TLB")
        .SetParent<Object> ()
        .SetGroupName ("TLB")
        .AddConstructor<Ipv4TLB> ()
        .AddAttribute ("RunMode", "The running mode of TLB (i.e., how to choose path from candidate paths), 0 for minimize counter, 1 for minimize RTT, 2 for random, 11 for RTT counter, 12 for RTT DRE",
                      UintegerValue (0),
                      MakeUintegerAccessor (&Ipv4TLB::m_runMode),
                      MakeUintegerChecker<uint32_t> ())
        .AddAttribute ("Rerouting", "Whether rerouting is enabled",
                      BooleanValue (false),
                      MakeBooleanAccessor (&Ipv4TLB::m_rerouteEnable),
                      MakeBooleanChecker ())
        .AddAttribute ("MinRTT", "Min RTT threshold used to judge a good path",
                      TimeValue (MicroSeconds(50)),
                      MakeTimeAccessor (&Ipv4TLB::m_minRtt),
                      MakeTimeChecker ())
        .AddAttribute ("HighRTT", "High RTT threshold used to judge a bad path",
                      TimeValue (MicroSeconds(200)),
                      MakeTimeAccessor (&Ipv4TLB::m_highRtt),
                      MakeTimeChecker ())
        .AddAttribute ("DREMultiply", "DRE multiply factor (refer to CONGA)",
                      UintegerValue (5),
                      MakeUintegerAccessor (&Ipv4TLB::m_dreMultiply),
                      MakeUintegerChecker<uint32_t> ())
        .AddAttribute ("S", "The sent size used to judge whether a flow should change path",
                      UintegerValue (64000),
                      MakeUintegerAccessor (&Ipv4TLB::m_S),
                      MakeUintegerChecker<uint32_t> ())
        .AddAttribute ("BetterPathRTTThresh", "RTT Threshold used to judge whether one path is better than another",
                      TimeValue (MicroSeconds (300)),
                      MakeTimeAccessor (&Ipv4TLB::m_betterPathRttThresh),
                      MakeTimeChecker ())
        .AddAttribute ("ChangePathPoss", "Possibility to change the path (to avoid herd behavior)",
                      UintegerValue (50),
                      MakeUintegerAccessor (&Ipv4TLB::m_pathChangePoss),
                      MakeUintegerChecker<uint32_t> ())
        .AddAttribute ("T1", "The path aging time interval (i.e., the frequency to update path condition)",
                      TimeValue (MicroSeconds (320)),
                      MakeTimeAccessor (&Ipv4TLB::m_T1),
                      MakeTimeChecker ())
        .AddAttribute ("ECNPortionLow", "The ECN portion threshold used in judging a good path",
                      DoubleValue (0.3),
                      MakeDoubleAccessor (&Ipv4TLB::m_ecnPortionLow),
                      MakeDoubleChecker<double> (0.0))
        .AddAttribute ("IsSmooth", "Whether the RTT calculation is smoothed (moving average)",
                      BooleanValue (false),
                      MakeBooleanAccessor (&Ipv4TLB::m_isSmooth),
                      MakeBooleanChecker ())
        .AddAttribute ("QuantifyRttBase", "The quantify RTT base  (granularity to quantify paths to differernt catagories according to RTT)",
                      TimeValue (MicroSeconds (10)),
                      MakeTimeAccessor (&Ipv4TLB::m_quantifyRttBase),
                      MakeTimeChecker ())
        .AddAttribute ("AckletTimeout", "The ACK flowlet timeout",
                      TimeValue (MicroSeconds (300)),
                      MakeTimeAccessor (&Ipv4TLB::m_ackletTimeout),
                      MakeTimeChecker ())
        .AddAttribute ("FlowletTimeout", "The flowlet timeout",
                      TimeValue (MicroSeconds (500)),
                      MakeTimeAccessor (&Ipv4TLB::m_flowletTimeout),
                      MakeTimeChecker ())
      .AddAttribute ("RespondToFailure", "Whether TLB reacts to failure",
                     BooleanValue (false),
                     MakeBooleanAccessor (&Ipv4TLB::m_respondToFailure),
                     MakeBooleanChecker ())
      .AddAttribute ("FlowRetransHigh",
                     "Threshold to determine whether the flow has experienced a serve retransmission",
                     UintegerValue (140000000),
                     MakeIntegerAccessor (&Ipv4TLB::m_flowRetransHigh),
                     MakeIntegerChecker<uint32_t> ())
      .AddAttribute ("FlowRetransVeryHigh",
                     "Threshold to determine whether the flow has experienced a very serve retransmission",
                     UintegerValue (140000000),
                     MakeIntegerAccessor (&Ipv4TLB::m_flowRetransVeryHigh),
                     MakeIntegerChecker<uint32_t> ())
      .AddAttribute ("FlowTimeoutCount",
                     "Threshold to determine whether the flow has expierienced a serve timeout",
                     UintegerValue (10000),
                     MakeIntegerAccessor (&Ipv4TLB::m_flowTimeoutCount),
                     MakeIntegerChecker<uint32_t> ())
      .AddAttribute ("RTTAlpha",
                     "The weight of RTT in characterizing paths into good, congested and gray",
                     DoubleValue (1.0),
                     MakeDoubleAccessor (&Ipv4TLB::m_rttAlpha),
                     MakeDoubleChecker<double> ())
      .AddAttribute ("ECNBeta",
                     "They weight of ECN in characterizing paths into good, congested and gray",
                     DoubleValue (0.0),
                     MakeDoubleAccessor (&Ipv4TLB::m_ecnBeta),
                     MakeDoubleChecker<double> ())
        // we set RTTAlpha = 1 and ECNBeta = 0 in our simulation as the RTT measurement is accurate. 
        // And we set RTTAlpha = 0 and ECNBeta = 1 in our testbed because the RTT measurement in our testbed is not accurate.
        // In general, Alpha and Beta can be flexibely tuned to reflect which value are more trusted.
        .AddTraceSource ("SelectPath",
                         "When the new flow is assigned the path",
                         MakeTraceSourceAccessor (&Ipv4TLB::m_pathSelectTrace),
                         "ns3::Ipv4TLB::TLBPathCallback")
        .AddTraceSource ("ChangePath",
                         "When the flow changes the path",
                         MakeTraceSourceAccessor (&Ipv4TLB::m_pathChangeTrace),
                         "ns3::Ipv4TLB::TLBPathChangeCallback")
    ;

    return tid;
}

//添加目的地址对应的TorId,即叶结点id
void
Ipv4TLB::AddAddressWithTor (Ipv4Address address, uint32_t torId)
{
    m_ipTorMap[address] = torId;
}

//存储一个可以到destTor地址的路径
void
Ipv4TLB::AddAvailPath (uint32_t destTor, uint32_t path)
{
    std::map<uint32_t, std::vector<uint32_t> >::iterator itr = m_availablePath.find (destTor);
    if (itr == m_availablePath.end ())
    {
        std::vector<uint32_t> paths;
        paths.push_back(path);
        m_availablePath[destTor] = paths;
    }
    else 
    {
        (itr->second).push_back (path);
    }
    
}

//得到去某个地址的路径集合
std::vector<uint32_t>
Ipv4TLB::GetAvailPath (Ipv4Address daddr)
{
    std::vector<uint32_t> emptyVector;
    uint32_t destTor = 0;
    if (!Ipv4TLB::FindTorId (daddr, destTor))
    {
        NS_LOG_ERROR ("Cannot find dest tor id based on the given dest address");
        return emptyVector;
    }

    std::map<uint32_t, std::vector<uint32_t> >::iterator itr = m_availablePath.find (destTor);
    if (itr == m_availablePath.end ())
    {
        return emptyVector;
    }
    return itr->second;
}

//得到ack的路径 //TODO什么时候调用
uint32_t
Ipv4TLB::GetAckPath (uint32_t flowId, Ipv4Address saddr, Ipv4Address daddr)
{
    //通过flowId找到acklet
    struct TLBAcklet acklet;
    std::map<uint32_t, TLBAcklet>::iterator ackletItr = m_acklets.find (flowId);
    //如果找到了
    if (ackletItr != m_acklets.end ())
    {
        // Existing flow
        acklet = ackletItr->second; //得到acklet
        //如果当前时间减去上次的时间 未超时
        if (Simulator::Now () - acklet.activeTime <= m_ackletTimeout) // Timeout
        {
            //更新时间
            acklet.activeTime = Simulator::Now ();
            //并且存储到m_acklets中 返回
            m_acklets[flowId] = acklet;
            return acklet.pathId;
        }
        //TODO？

        // Bug Fix for bad small flow FCT in black hole case
        // 如果时间已经超过1ms,则认为是包黑洞
        if (Simulator:: Now () - acklet.activeTime >= MilliSeconds (1))
        {
            //找到这个目的地址对应的TorId
            uint32_t destTor = 0;
            if (!Ipv4TLB::FindTorId (daddr, destTor))
            {
                NS_LOG_ERROR ("Cannot find dest tor id based on the given dest address");
                return 0;
            }
            //记录现在走的路径
            uint32_t oldPath = acklet.pathId;

            // Ipv4TLB::TimeoutPath (destTor, oldPath, false, true);
            //构建一个新路径
            struct PathInfo newPath;
            while (1)
            {
                //随机选择一条到这个目的Tor的新路径，然后返回 
                newPath = Ipv4TLB::SelectRandomPath (destTor);
                if (newPath.pathId != oldPath)
                {
                    break;
                }
            }
            //新路径赋值，更新时间，然后返回
            acklet.pathId = newPath.pathId;
            acklet.activeTime = Simulator::Now ();

            m_acklets[flowId] = acklet;

            return newPath.pathId;
        }
    }

    // New flow or expired flowlet
    // 如果是新的流，则先找到TorId
    uint32_t destTor = 0;
    if (!Ipv4TLB::FindTorId (daddr, destTor))
    {
        NS_LOG_ERROR ("Cannot find dest tor id based on the given dest address");
        return 0;
    }
    //得到一条新路径，如果无候选路径，则直接随机选择一条
    struct PathInfo newPath;
    if (!Ipv4TLB::WhereToChange (destTor, newPath, false, 0))
    {
        newPath = Ipv4TLB::SelectRandomPath (destTor);
    }
    //更新acklet信息 
    acklet.pathId = newPath.pathId;
    acklet.activeTime = Simulator::Now ();

    m_acklets[flowId] = acklet;
    //然后返回
    return newPath.pathId;
}

//从路径上移除一条流,更新相应的TLBPathInfo中的flowCounter
void
Ipv4TLB::RemoveFlowFromPath (uint32_t flowId, uint32_t destTor, uint32_t path)
{
    std::pair<uint32_t, uint32_t> key = std::make_pair(destTor, path);
    std::map<std::pair<uint32_t, uint32_t>, TLBPathInfo>::iterator itr = m_pathInfo.find (key);
    if (itr == m_pathInfo.end ())
    {
        NS_LOG_ERROR ("Cannot remove flow from a non-existing path");
        return;
    }
    if ((itr->second).flowCounter == 0)
    {
        NS_LOG_ERROR ("Cannot decrease from counter while it has reached 0");
        return;
    }
    //将flowCounter减一
    (itr->second).flowCounter --;

}

//分配流到路径，得到TLBPahtInfo，然后增加其上的流数，即flowCounter值
void
Ipv4TLB::AssignFlowToPath (uint32_t flowId, uint32_t destTor, uint32_t path)
{
    std::pair<uint32_t, uint32_t> key = std::make_pair(destTor, path);
    std::map<std::pair<uint32_t, uint32_t>, TLBPathInfo>::iterator itr = m_pathInfo.find (key);

    TLBPathInfo pathInfo;
    if (itr == m_pathInfo.end ())
    {
        pathInfo = Ipv4TLB::GetInitPathInfo (path);
    }
    else
    {
        pathInfo = itr->second;
    }

    pathInfo.flowCounter ++;
    m_pathInfo[key] = pathInfo;
}

//根据flowId还有源地址与目的地址得到一个路径，返回路径ID值
uint32_t
Ipv4TLB::GetPath (uint32_t flowId, Ipv4Address saddr, Ipv4Address daddr)
{
    //如果路径老化事件没有运行，则运行
    if (!m_agingEvent.IsRunning ())
    {
        m_agingEvent = Simulator::Schedule (m_agingCheckTime, &Ipv4TLB::PathAging, this);
    }
    //如果DRE事件未运行，则运行
    if (!m_dreEvent.IsRunning ())
    {
        m_dreEvent = Simulator::Schedule (m_dreTime, &Ipv4TLB::DreAging, this);
    }
    //找到目的地址对应的TorId
    uint32_t destTor = 0;
    if (!Ipv4TLB::FindTorId (daddr, destTor))
    {
        NS_LOG_ERROR ("Cannot find dest tor id based on the given dest address");
        return 0;
    }
    //找到源地址对应的TorId
    uint32_t sourceTor = 0;
    if (!Ipv4TLB::FindTorId (saddr, sourceTor))
    {
        NS_LOG_ERROR ("Cannot find source tor id based on the given source address");
    }
    //得到这个流对应的TLBFlowInfo
    std::map<uint32_t, TLBFlowInfo>::iterator flowItr = m_flowInfo.find (flowId);

    // First check if the flow is a new flow
    // 如果是个新流
    if (flowItr == m_flowInfo.end ())
    {
        // New flow
        // 如果可以走的通则选一条好的路径
        struct PathInfo newPath;
        if (Ipv4TLB::WhereToChange (destTor, newPath, false, 0))
        {
            m_pathSelectTrace (flowId, sourceTor, destTor, newPath.pathId, false, newPath, Ipv4TLB::GatherParallelPaths (destTor));
        }
        else //如果不行，则随机选择一条路径
        {
            newPath = Ipv4TLB::SelectRandomPath (destTor);
            m_pathSelectTrace (flowId, sourceTor, destTor, newPath.pathId, true, newPath, Ipv4TLB::GatherParallelPaths (destTor));
        }
        //初始化一个TLBFlowInfo对象并且将其添加到m_flowInfo中
        Ipv4TLB::UpdateFlowPath (flowId, newPath.pathId, destTor);
        //将这条流分配到pathId这条路径上
        Ipv4TLB::AssignFlowToPath (flowId, destTor, newPath.pathId);
        return newPath.pathId;
    }
    else if (m_rerouteEnable) //如果不是新流，并且重路由开启了
    {
        //得到TLBFlowInfo中的activeTime,并且更新时间
        Time flowActiveTime = (flowItr->second).activeTime;
        (flowItr->second).activeTime = Simulator::Now ();

        // Old flow
        //得到现在的路径及类型
        uint32_t oldPath = (flowItr->second).path;
        struct PathInfo oldPathInfo = Ipv4TLB::JudgePath (destTor, oldPath);
        //如果对失败处理并且重传的量超过了阀值或已经有过超时
        if (m_respondToFailure
                && ((flowItr->second).retransmissionSize > m_flowRetransVeryHigh
                || (flowItr->second).timeoutCount >= 1))
        {
            //则重新选择路径时行路由
            struct PathInfo newPath;
            if (Ipv4TLB::WhereToChange (destTor, newPath, true, oldPath))
            {
                if (newPath.pathId != oldPath)
                {
                    m_pathChangeTrace (flowId, sourceTor, destTor, newPath.pathId, oldPath, false, Ipv4TLB::GatherParallelPaths (destTor));
                }
            }
            else
            {
                newPath = Ipv4TLB::SelectRandomPath (destTor);
                if (newPath.pathId != oldPath)
                {
                    m_pathChangeTrace (flowId, sourceTor, destTor, newPath.pathId, oldPath, true, Ipv4TLB::GatherParallelPaths (destTor));
                }
            }

            //如果还是老路径寻到就返回
            if (newPath.pathId == oldPath)
            {
                return oldPath;
            }
            // Change path
            // 更新流信息
            Ipv4TLB::UpdateFlowPath (flowId, newPath.pathId, destTor);
            //将流从老路径移下来并分配到新路径
            Ipv4TLB::RemoveFlowFromPath (flowId, destTor, oldPath);
            Ipv4TLB::AssignFlowToPath (flowId, destTor, newPath.pathId);
            return newPath.pathId;
        }
        else if ((oldPathInfo.pathType == BadPath || Simulator::Now () - flowActiveTime > m_flowletTimeout) // Trigger for rerouting
                && oldPathInfo.quantifiedDre <= m_dreMultiply // DRE值可以代表最近一段的速率，如果速率小于设定的阀值，且发送量大于m_s，则重传
                && (flowItr->second).size >= m_S
                /*&& ((static_cast<double> ((flowItr->second).ecnSize) / (flowItr->second).size > m_ecnPortionHigh && Simulator::Now () - (flowItr->second).timeStamp >= m_T) || (flowItr->second).retransmissionSize > m_flowRetransHigh)*/
                && Simulator::Now() - (flowItr->second).tryChangePath > MicroSeconds (100))
        {
            //以m_pathChangePoss的概率重路由
            if (rand () % RANDOM_BASE < static_cast<int> (RANDOM_BASE - m_pathChangePoss))
            {
                (flowItr->second).tryChangePath = Simulator::Now ();
                return oldPath;
            }
            //选一条新的路径
            struct PathInfo newPath;
            if (Ipv4TLB::WhereToChange (destTor, newPath, true, oldPath))
            {
                if (newPath.pathId == oldPath)
                {
                    return oldPath;
                }

                m_pathChangeTrace (flowId, sourceTor, destTor, newPath.pathId, oldPath, false, Ipv4TLB::GatherParallelPaths (destTor));

                // Calculate the pause time
                Time pauseTime = oldPathInfo.rttMin - newPath.rttMin;
                m_pauseTime[flowId] = std::max (pauseTime, MicroSeconds (1));

                // Change path
                // 选择新路径后更新信息
                Ipv4TLB::UpdateFlowPath (flowId, newPath.pathId, destTor);
                Ipv4TLB::RemoveFlowFromPath (flowId, destTor, oldPath);
                Ipv4TLB::AssignFlowToPath (flowId, destTor, newPath.pathId);
                return newPath.pathId;
            }
            else
            {
                // Do not change path
                return oldPath;
            }
        }
        else
        {
            return oldPath;
        }
    }
    else
    {
        //如果是正常的流，则更新时间即可
        (flowItr->second).activeTime = Simulator::Now ();

        uint32_t oldPath = (flowItr->second).path;
        return oldPath;
    }
}

//得到暂停的时间
Time
Ipv4TLB::GetPauseTime (uint32_t flowId)
{
   std::map<uint32_t, Time>::iterator itr = m_pauseTime.find (flowId);
   if (itr == m_pauseTime.end ())
   {
        return MicroSeconds (0);
   }
   return itr->second;
}


//当收到流时调用，主要更新TLBFlowInfo中的信息
void
Ipv4TLB::FlowRecv (uint32_t flowId, uint32_t path, Ipv4Address daddr, uint32_t size, bool withECN, Time rtt)
{
    // NS_LOG_FUNCTION (flowId << path << daddr << size << withECN << rtt);
    uint32_t destTor = 0;
    if (!Ipv4TLB::FindTorId (daddr, destTor))
    {
        NS_LOG_ERROR ("Cannot find dest tor id based on the given dest address");
        return;
    }
    //更新收到的量和ECN比例还有livetime等 
    Ipv4TLB::PacketReceive (flowId, path, destTor, size, withECN, rtt, false);
}


//得到要发送的流的TLBFlowInfo更改发送量，然后返回
bool
Ipv4TLB::SendFlow (uint32_t flowId, uint32_t path, uint32_t size)
{
    std::map<uint32_t, TLBFlowInfo>::iterator itr = m_flowInfo.find (flowId);
    if (itr == m_flowInfo.end ())
    {
        NS_LOG_ERROR ("Cannot retransmit a non-existing flow");
        return false;
    }
    if ((itr->second).path != path)
    {
        return false;
    }
    (itr->second).sendSize += size;
    return true;
}

//得到将要发送的流的路径TLBPathInfo信息，更改发送量，即DRE值，然后返回
void
Ipv4TLB::SendPath (uint32_t destTor, uint32_t path, uint32_t size)
{
    std::pair<uint32_t, uint32_t> key = std::make_pair(destTor, path);
    std::map<std::pair<uint32_t, uint32_t>, TLBPathInfo>::iterator itr = m_pathInfo.find (key);

    if (itr == m_pathInfo.end ())
    {
        NS_LOG_ERROR ("Cannot send a non-existing path");
        return;
    }

    (itr->second).dreValue += size;
}


//更改TLBFlowInfo中retransmissionSize值
bool
Ipv4TLB::RetransFlow (uint32_t flowId, uint32_t path, uint32_t size, bool &needRetranPath, bool &needHighRetransPath)
{
    needRetranPath = false;
    needHighRetransPath = false;
    std::map<uint32_t, TLBFlowInfo>::iterator itr = m_flowInfo.find (flowId);
    if (itr == m_flowInfo.end ())
    {
        NS_LOG_ERROR ("Cannot retransmit a non-existing flow");
        return false;
    }
    if ((itr->second).path != path)
    {
        return false;
    }
    if (Simulator::Now () - (itr->second).timeStamp < MicroSeconds (1000))
    {
        return false;
    }
    (itr->second).retransmissionSize += size;
    if ((itr->second).retransmissionSize > m_flowRetransHigh)
    {
        needRetranPath = true;
    }
    if ((itr->second).retransmissionSize > m_flowRetransVeryHigh)
    {
        needHighRetransPath = true;
    }
    return true;
}

//更改TLBPathInfo中的isRetransmission与isHighRetransmission的信息
void
Ipv4TLB::RetransPath (uint32_t destTor, uint32_t path, bool needHighRetransPath)
{
    std::pair<uint32_t, uint32_t> key = std::make_pair(destTor, path);
    std::map<std::pair<uint32_t, uint32_t>, TLBPathInfo>::iterator itr = m_pathInfo.find (key);
    if (itr == m_pathInfo.end ())
    {
        NS_LOG_ERROR ("Cannot timeout a non-existing path");
        return;
    }
    (itr->second).isRetransmission = true;
    if (needHighRetransPath)
    {
        (itr->second).isHighRetransmission = true;
    }
}

//流发送时调用，主要更新流信息等，还有路径信息，如果是重传还有更新相应的变量
void
Ipv4TLB::FlowSend (uint32_t flowId, Ipv4Address daddr, uint32_t path, uint32_t size, bool isRetransmission)
{
    // NS_LOG_FUNCTION (flowId << daddr << path << size << isRetransmission);
    uint32_t destTor = 0;
    if (!Ipv4TLB::FindTorId (daddr, destTor))
    {
        NS_LOG_ERROR ("Cannot find dest tor id based on the given dest address");
        return;
    }
    //没有更改路径就为true
    bool notChangePath = Ipv4TLB::SendFlow (flowId, path, size);

    if (!notChangePath)
    {
        NS_LOG_ERROR ("Cannot send flow on the expired path");
        return;
    }
    //更改路径DRE即发送量的值
    Ipv4TLB::SendPath (destTor, path, size);

    if (isRetransmission)
    {
        bool needRetransPath = false;
        bool needHighRetransPath = false;
        bool notChangePath = Ipv4TLB::RetransFlow (flowId, path, size, needRetransPath, needHighRetransPath);
        if (!notChangePath)
        {
            NS_LOG_LOGIC ("Cannot send flow on the expired path");
            return;
        }
        if (needRetransPath)
        {
            Ipv4TLB::RetransPath (destTor, path, needHighRetransPath);
        }
    }
}


//超时后对流的处理，更改TLBFlowInfo中timeoutCount次数，并有返回是否超过阀值
bool
Ipv4TLB::TimeoutFlow (uint32_t flowId, uint32_t path, bool &isVeryTimeout)
{
    isVeryTimeout = false;
    std::map<uint32_t, TLBFlowInfo>::iterator itr = m_flowInfo.find (flowId);
    if (itr == m_flowInfo.end ())
    {
        NS_LOG_ERROR ("Cannot timeout a non-existing flow");
        return false;
    }
    if ((itr->second).path != path)
    {
        return false;
    }
    (itr->second).timeoutCount ++;
    if ((itr->second).timeoutCount >= m_flowTimeoutCount)
    {
        isVeryTimeout = true;
    }
    return true;
}

//超时后对路径的操作，新TLBPathInfo中的信息，包括isTimeout与isVeryTimeout，其中isProbing表示是否是探测
void
Ipv4TLB::TimeoutPath (uint32_t destTor, uint32_t path, bool isProbing, bool isVeryTimeout)
{
    std::pair<uint32_t, uint32_t> key = std::make_pair(destTor, path);
    std::map<std::pair<uint32_t, uint32_t>, TLBPathInfo>::iterator itr = m_pathInfo.find (key);
    if (itr == m_pathInfo.end ())
    {
        NS_LOG_ERROR ("Cannot timeout a non-existing path");
        return;
    }
    if (!isProbing)
    {
        (itr->second).isTimeout = true;
        if (isVeryTimeout)
        {
            (itr->second).isVeryTimeout = true;
        }
    }
    else
    {
        (itr->second).isProbingTimeout = true;
    }
}

//流超时后调用，主要更新相应的TLBPathInfo与TLBFlowInfo
void
Ipv4TLB::FlowTimeout (uint32_t flowId, Ipv4Address daddr, uint32_t path)
{
    uint32_t destTor = 0;
    if (!Ipv4TLB::FindTorId (daddr, destTor))
    {
        NS_LOG_ERROR ("Cannot find dest tor id based on the given dest address");
        return;
    }

    bool isVeryTimeout = false;
    bool notChangePath = Ipv4TLB::TimeoutFlow (flowId, path, isVeryTimeout);
    if (!notChangePath)
    {
        NS_LOG_LOGIC ("The flow has changed the path");
    }
    Ipv4TLB::TimeoutPath (destTor, path, false, isVeryTimeout);
}

//流完成后调用,更新路径上的流数量即TLBPathInfo中的flowCounter
void
Ipv4TLB::FlowFinish (uint32_t flowId, Ipv4Address daddr)
{
    uint32_t destTor = 0;
    if (!Ipv4TLB::FindTorId (daddr, destTor))
    {
        NS_LOG_ERROR ("Cannot find dest tor id based on the given dest address");
        return;
    }
    std::map<uint32_t, TLBFlowInfo>::iterator itr = m_flowInfo.find (flowId);
    if (itr == m_flowInfo.end ())
    {
        NS_LOG_ERROR ("Cannot finish a non-existing flow");
        return;
    }

    Ipv4TLB::RemoveFlowFromPath (flowId, destTor, (itr->second).path);

}

//初始化一个TLBPathInfo并返回
TLBPathInfo
Ipv4TLB::GetInitPathInfo (uint32_t path)
{
    TLBPathInfo pathInfo;
    pathInfo.pathId = path;
    pathInfo.size = 3;
    pathInfo.ecnSize = 1;
    /*pathInfo.minRtt = m_betterPathRttThresh + MicroSeconds (100);*/
    pathInfo.minRtt = m_minRtt;
    pathInfo.isRetransmission = false;
    pathInfo.isHighRetransmission = false;
    pathInfo.isTimeout = false;
    pathInfo.isVeryTimeout = false;
    pathInfo.isProbingTimeout = false;
    pathInfo.flowCounter = 0; // XXX Notice the flow count will be update using Add/Remove Flow To/From Path method
    pathInfo.timeStamp1 = Simulator::Now ();
    pathInfo.timeStamp2 = Simulator::Now ();
    pathInfo.timeStamp3 = Simulator::Now ();
    pathInfo.dreValue = 0;

    // Added Jan 11st
    // Path ECN portion default value
    /*
    pathInfo.epAckSize = 1;
    pathInfo.epEcnSize = 0;
    pathInfo.epEcnPortion = m_epDefaultEcnPortion;
    pathInfo.epTimeStamp = Simulator::Now ();
    */

    return pathInfo;
}

//探测包发送时调用,主要是初始化路径的TLBPathInfo
void
Ipv4TLB::ProbeSend (Ipv4Address daddr, uint32_t path)
{
    uint32_t destTor = 0;
    if (!Ipv4TLB::FindTorId (daddr, destTor))
    {
        NS_LOG_ERROR ("Cannot find dest tor id based on the given dest address");
        return;
    }
    std::pair<uint32_t, uint32_t> key = std::make_pair(destTor, path);
    std::map<std::pair<uint32_t, uint32_t>, TLBPathInfo>::iterator itr = m_pathInfo.find (key);

    //如果没有找到就初始化一个并加入m_pathInfo
    if (itr == m_pathInfo.end ())
    {
        m_pathInfo[key] = Ipv4TLB::GetInitPathInfo (path);
    }

}

//收到探测包时调用，更新相应的信息，探测包则更新路径信息
void
Ipv4TLB::ProbeRecv (uint32_t path, Ipv4Address daddr, uint32_t size, bool withECN, Time rtt)
{
    NS_LOG_FUNCTION (path << daddr << size << withECN << rtt);
    uint32_t destTor = 0;
    if (!Ipv4TLB::FindTorId (daddr, destTor))
    {
        NS_LOG_ERROR ("Cannot find dest tor id based on the given dest address");
        return;
    }
    Ipv4TLB::PacketReceive (0, path, destTor, size, withECN, rtt, true);
}

//在探测超时时调用，更新路径的超时信息
void
Ipv4TLB::ProbeTimeout (uint32_t path, Ipv4Address daddr)
{   
    uint32_t destTor = 0;
    if (!Ipv4TLB::FindTorId (daddr, destTor))
    {
        NS_LOG_ERROR ("Cannot find dest tor id based on the given dest address");
        return;
    }
    //更新路径的超时信息
    Ipv4TLB::TimeoutPath (path, destTor, true, false);
}

//设置节点
void
Ipv4TLB::SetNode (Ptr<Node> node)
{
    m_node = node;
}


//根据是否是探测更新相应信息
void
Ipv4TLB::PacketReceive (uint32_t flowId, uint32_t path, uint32_t destTorId,
                        uint32_t size, bool withECN, Time rtt, bool isProbing)
{
    // If the packet acks the current path the flow goes, update the flow table and path table
    // If not or the packet is a probing, update the path table
    //如果不是探测包则更新流信息
    if (!isProbing)
    {
        bool notChangePath = Ipv4TLB::UpdateFlowInfo (flowId, path, size, withECN, rtt);
        if (!notChangePath)
        {
            NS_LOG_LOGIC ("The flow has changed the path");
        }
    }
    //如果是探测则更新路径信息
    Ipv4TLB::UpdatePathInfo (destTorId, path, size, withECN, rtt);
}

//更新流信息，更新收到的量和ECN量和时间等
bool
Ipv4TLB::UpdateFlowInfo (uint32_t flowId, uint32_t path, uint32_t size, bool withECN, Time rtt)
{
    std::map<uint32_t, TLBFlowInfo>::iterator itr = m_flowInfo.find (flowId);
    if (itr == m_flowInfo.end ())
    {
        NS_LOG_ERROR ("Cannot update info for a non-existing flow");
        return false;
    }
    if ((itr->second).path != path)
    {
        return false;
    }
    (itr->second).size += size;
    if (withECN)
    {
        (itr->second).ecnSize += size;
    }
    (itr->second).liveTime = Simulator::Now ();

    // Added Dec 23rd
    /*
    if (m_isSmooth)
    {
        (itr->second).rtt = (SMOOTH_BASE - m_smoothAlpha) * (itr->second).rtt / SMOOTH_BASE + m_smoothAlpha * rtt / SMOOTH_BASE;
    }
    else
    {
        if (rtt < (itr->second).rtt)
        {
            (itr->second).rtt = rtt;
        }
    }
    */
    // ---

    // Added Jan 11st
    /*
    (itr->second).epAckSize += size;
    if (withECN)
    {
        (itr->second).epEcnSize += size;
    }
    if (Simulator::Now () - (itr->second).epTimeStamp > m_epCheckTime)
    {
        double originalEcnPortion = (itr->second).epEcnPortion;
        double newEcnPortition = static_cast<double> ((itr->second).epEcnSize) / (itr->second).epAckSize;
        (itr->second).epAckSize = 1;
        (itr->second).epEcnSize = 0;
        (itr->second).epEcnPortion = m_epAlpha * originalEcnPortion + (1.0 - m_epAlpha) * newEcnPortition;
        (itr->second).epTimeStamp = Simulator::Now ();
    }
    */
    // --

    return true;
}

//更新路径信息即对应路径的TLBPathInfo信息
void
Ipv4TLB::UpdatePathInfo (uint32_t destTor, uint32_t path, uint32_t size, bool withECN, Time rtt)
{
    std::pair<uint32_t, uint32_t> key = std::make_pair(destTor, path);
    std::map<std::pair<uint32_t, uint32_t>, TLBPathInfo>::iterator itr = m_pathInfo.find (key);
    //如果找不到就初始化一个
    TLBPathInfo pathInfo;
    if (itr == m_pathInfo.end ())
    {
        pathInfo = Ipv4TLB::GetInitPathInfo (path);
    }
    else
    {
        pathInfo = itr->second;
    }
    //更新收到的量和ECN量还有时间
    pathInfo.size += size;
    if (withECN)
    {
        pathInfo.ecnSize += size;
    }
    if (m_isSmooth) //是否平滑RTT的计算
    { 
        //m_smoothAlpha表示计入多少最新的采样值
        pathInfo.minRtt = (SMOOTH_BASE - m_smoothAlpha) * pathInfo.minRtt / SMOOTH_BASE + m_smoothAlpha * rtt / SMOOTH_BASE;
    }
    else
    {
        if (rtt < pathInfo.minRtt)
        {
            pathInfo.minRtt = rtt;
        }
    }
    //更新路径信息时会亲更新timeStamp3
    pathInfo.timeStamp3 = Simulator::Now ();

    // Added Jan 11st
    /*
    pathInfo.epAckSize += size;
    if (withECN)
    {
        pathInfo.epEcnSize += size;
    }
    if (Simulator::Now () - pathInfo.epTimeStamp > m_epCheckTime)
    {
        double originalEcnPortion = pathInfo.epEcnPortion;
        double newEcnPortition = static_cast<double> (pathInfo.epEcnSize) / pathInfo.epAckSize;
        pathInfo.epAckSize = 1;
        pathInfo.epEcnSize = 0;
        pathInfo.epEcnPortion = m_epAlpha * originalEcnPortion + (1.0 - m_epAlpha) * newEcnPortition;
        pathInfo.epTimeStamp = Simulator::Now ();
    }
    */
    // --

    m_pathInfo[key] = pathInfo;
}




//初始化一个TLBFlowInfo并返回
void
Ipv4TLB::UpdateFlowPath (uint32_t flowId, uint32_t path, uint32_t destTor)
{
    TLBFlowInfo flowInfo;
    flowInfo.path = path;
    flowInfo.destTor = destTor;
    flowInfo.size = 0;
    flowInfo.ecnSize = 0;
    flowInfo.sendSize = 0;
    flowInfo.retransmissionSize = 0;
    flowInfo.timeoutCount = 0;
    flowInfo.timeStamp = Simulator::Now ();
    flowInfo.tryChangePath = Simulator::Now (); //更改路径后更新
    flowInfo.liveTime = Simulator::Now ();

    // Added Dec 23rd
    // Flow RTT default value
    // flowInfo.rtt = m_minRtt;

    // Added Jan 11st
    // Flow ECN portion default value
    /*
    flowInfo.epAckSize = 1;
    flowInfo.epEcnSize = 0;
    flowInfo.epEcnPortion = m_epDefaultEcnPortion;
    flowInfo.epTimeStamp = Simulator::Now ();
    */

    // Added Jan 12nd
    flowInfo.activeTime = Simulator::Now ();

    m_flowInfo[flowId] = flowInfo;
}


//选择路径路由的主要算法，先选择good，再选grey，注意这里PathInfo是引用
bool
Ipv4TLB::WhereToChange (uint32_t destTor, PathInfo &newPath, bool hasOldPath, uint32_t oldPath)
{
    //找到到达目的Tor的路径
    std::map<uint32_t, std::vector<uint32_t> >::iterator itr = m_availablePath.find (destTor);
    //如果找不到则返回错误
    if (itr == m_availablePath.end ())
    {
        NS_LOG_ERROR ("Cannot find available paths");
        return false;
    }

    std::vector<uint32_t>::iterator vectorItr = (itr->second).begin ();

    // Firstly, checking good path
    uint32_t minCounter = std::numeric_limits<uint32_t>::max ();
    Time minRTT = Seconds (666);
    uint32_t minRTTLevel = 5;
    uint32_t minDre = std::pow (2, m_dreQ);
    std::vector<PathInfo> candidatePaths;
    //遍历所有可以到destTor的路径
    for ( ; vectorItr != (itr->second).end (); ++vectorItr)
    {
        uint32_t pathId = *vectorItr;
        //判断路径得到路径的具体信息
        struct PathInfo pathInfo = JudgePath (destTor, pathId);
        if (pathInfo.pathType == GoodPath)
        {
            if (m_runMode == TLB_RUNMODE_COUNTER) //选择counter值最小的路径
            {
                //如果等于则直接加入候选，如果小于则清空候选，然后加入
                if (pathInfo.counter <= minCounter)
                {
                    if (pathInfo.counter < minCounter)
                    {
                        candidatePaths.clear ();
                        minCounter = pathInfo.counter;
                    }
                    candidatePaths.push_back (pathInfo);
                }
            }
            else if (m_runMode == TLB_RUNMODE_MINRTT) //选择minRTT最小的路径
            {
                 //如果等于则直接加入候选，如果小于则清空候选，然后加入
                if (pathInfo.rttMin <= minRTT)
                {
                    if (pathInfo.rttMin < minRTT)
                    {
                        candidatePaths.clear ();
                        minRTT = pathInfo.rttMin;
                    }
                    candidatePaths.push_back (pathInfo);
                }
            }
            else if (m_runMode == TLB_RUNMODE_RTT_COUNTER || m_runMode == TLB_RUNMODE_RTT_DRE)
            {
                //将这条路径的pathInfo中的rttMin量化分级
                uint32_t RTTLevel = Ipv4TLB::QuantifyRtt (pathInfo.rttMin);
                //下面的代码是寻找RTTLevel和counter都小于当前，如果等于则直接push进去，而不clear，优先比较RTT
                //如果RTTLevel就比较大则直接不比较了，如果RTTLevel小则会直接push,也即优先考虑RTT
                if (RTTLevel < minRTTLevel)
                {
                    minRTTLevel = RTTLevel;
                    minCounter = std::numeric_limits<uint32_t>::max ();
                    candidatePaths.clear (); //TODO
                }
                if (RTTLevel == minRTTLevel)
                {
                    if (m_runMode == TLB_RUNMODE_RTT_COUNTER)
                    {
                        if (pathInfo.counter < minCounter)
                        {
                            minCounter = pathInfo.counter;
                            candidatePaths.clear ();
                        }
                        if (pathInfo.counter == minCounter)
                        {
                            candidatePaths.push_back (pathInfo);
                        }
                    }
                    else if (m_runMode == TLB_RUNMODE_RTT_DRE)
                    {
                        if (pathInfo.quantifiedDre < minDre)
                        {
                            minDre = pathInfo.quantifiedDre;
                            candidatePaths.clear ();
                        }
                        if (pathInfo.quantifiedDre == minDre)
                        {
                            candidatePaths.push_back (pathInfo);
                        }
                    }
                }
            }
            else//其它RunMode直接返回
            {
                candidatePaths.push_back (pathInfo);
            }
        }
    }

    //如果候选路径非空， 即good路径非空
    if (!candidatePaths.empty ())
    {
        if (m_runMode == TLB_RUNMODE_COUNTER)
        {
            //如是数量小于等于m_K,则随机选择一个
            if (minCounter <= m_K) //TODO
            {
                newPath = candidatePaths[rand () % candidatePaths.size ()];
            }
        }
        else if (m_runMode == TLB_RUNMODE_MINRTT)
        {
            newPath = candidatePaths[rand () % candidatePaths.size ()];
        }
        else if (m_runMode == TLB_RUNMODE_RTT_COUNTER || m_runMode == TLB_RUNMODE_RTT_DRE)
        {
            newPath = candidatePaths[rand () % candidatePaths.size ()];
        }
        else
        {
            newPath = candidatePaths[rand () % candidatePaths.size ()];
        }
        NS_LOG_LOGIC ("Find Good Path: " << newPath.pathId);
        return true;
    }

    // Secondly, checking grey path
    //如果good路径为空
    struct PathInfo originalPath;
    //如果有老路径
    if (hasOldPath)
    {
        //判断原来路径类型
        originalPath = JudgePath (destTor, oldPath);
    }
    else
    {
        //如果本来就没有路径，初始化一条grey路径
        originalPath.pathType = GreyPath;
        originalPath.rttMin = Seconds (666);
        originalPath.ecnPortion = 1;
        originalPath.counter = std::numeric_limits<uint32_t>::max ();
        originalPath.quantifiedDre = std::pow (2, m_dreQ);
    }

    minCounter = std::numeric_limits<uint32_t>::max ();
    minRTT = Seconds (666);
    minDre = std::pow (2, m_dreQ);
    candidatePaths.clear ();
    vectorItr = (itr->second).begin ();
    //然后同上面一样遍历所有路径，选择一条最好的grey路径，其它同上
    for ( ; vectorItr != (itr->second).end (); ++vectorItr)
    {
        uint32_t pathId = *vectorItr;
        struct PathInfo pathInfo = JudgePath (destTor, pathId);
        if (pathInfo.pathType == GreyPath
            && Ipv4TLB::PathLIsBetterR (pathInfo, originalPath))
        {
            if (m_runMode == TLB_RUNMODE_COUNTER)
            {
                if (pathInfo.counter <= minCounter)
                {
                    if (pathInfo.counter < minCounter)
                    {
                        candidatePaths.clear ();
                        minCounter = pathInfo.counter;
                    }
                    candidatePaths.push_back (pathInfo);
                }
            }
            else if (m_runMode == TLB_RUNMODE_MINRTT)
            {
                if (pathInfo.rttMin <= minRTT)
                {
                    if (pathInfo.rttMin < minRTT)
                    {
                        candidatePaths.clear ();
                        minRTT = pathInfo.rttMin;
                    }
                    candidatePaths.push_back (pathInfo);
                }
            }
            else if (m_runMode == TLB_RUNMODE_RTT_COUNTER || m_runMode == TLB_RUNMODE_RTT_DRE)
            {
                uint32_t RTTLevel = Ipv4TLB::QuantifyRtt (pathInfo.rttMin);
                if (RTTLevel < minRTTLevel)
                {
                    minRTTLevel = RTTLevel;
                    minCounter = std::numeric_limits<uint32_t>::max ();
                    candidatePaths.clear ();
                }
                if (RTTLevel == minRTTLevel)
                {

                    if (m_runMode == TLB_RUNMODE_RTT_COUNTER)
                    {
                        if (pathInfo.counter < minCounter)
                        {
                            minCounter = pathInfo.counter;
                            candidatePaths.clear ();
                        }
                        if (pathInfo.counter == minCounter)
                        {
                            candidatePaths.push_back (pathInfo);
                        }
                    }
                    else if (m_runMode == TLB_RUNMODE_RTT_DRE)
                    {
                        if (pathInfo.quantifiedDre < minDre)
                        {
                            minDre = pathInfo.quantifiedDre;
                            candidatePaths.clear ();
                        }
                        if (pathInfo.quantifiedDre == minDre)
                        {
                            candidatePaths.push_back (pathInfo);
                        }
                    }
                }
            }
            else
            {
                candidatePaths.push_back (pathInfo);
            }
        }
    }

    if (!candidatePaths.empty ()) //注释同上
    {
        if (m_runMode == TLB_RUNMODE_COUNTER)
        {
            if (minCounter <= m_K)
            {
                newPath = candidatePaths[rand () % candidatePaths.size ()];
            }
        }
        else if (m_runMode == TLB_RUNMODE_MINRTT)
        {
            newPath = candidatePaths[rand () % candidatePaths.size ()];
        }
        else if (m_runMode == TLB_RUNMODE_RTT_COUNTER || m_runMode == TLB_RUNMODE_RTT_DRE)
        {
            newPath = candidatePaths[rand () % candidatePaths.size ()];
        }

        else
        {
            newPath = candidatePaths[rand () % candidatePaths.size ()];
        }
        NS_LOG_LOGIC ("Find Grey Path: " << newPath.pathId);
        return true;
    }

   // Thirdly, checking bad path
   //如果连grey路径都没有，选择bad路径中比原来好的，选到即返回
    vectorItr = (itr->second).begin ();
    for ( ; vectorItr != (itr->second).end (); ++vectorItr)
    {
        uint32_t pathId = *vectorItr;
        struct PathInfo pathInfo = JudgePath (destTor, pathId);
        if (pathInfo.pathType == BadPath
            && Ipv4TLB::PathLIsBetterR (pathInfo, originalPath))
        {
            newPath = pathInfo;
            NS_LOG_LOGIC ("Find Bad Path: " << newPath.pathId);
            return true;
        }
    }

    // Thirdly, indicating no paths available
    NS_LOG_LOGIC ("No Path Returned");
    return false;
}

//随机选择一条路径
struct PathInfo
Ipv4TLB::SelectRandomPath (uint32_t destTor)
{
    //查询可行路径
    std::map<uint32_t, std::vector<uint32_t> >::iterator itr = m_availablePath.find (destTor);
    //如果没有则直接返回其中pathId=0
    if (itr == m_availablePath.end ())
    {
        NS_LOG_ERROR ("Cannot find available paths");
        PathInfo pathInfo;
        pathInfo.pathId = 0;
        return pathInfo;
    }

    //否则遍历所有数组
    std::vector<uint32_t>::iterator vectorItr = (itr->second).begin ();
    std::vector<PathInfo> availablePaths;
    for ( ; vectorItr != (itr->second).end (); ++vectorItr)
    {
        //判断路径
        uint32_t pathId = *vectorItr;
        struct PathInfo pathInfo = JudgePath (destTor, pathId);
        if (pathInfo.pathType == GoodPath || pathInfo.pathType == GreyPath || pathInfo.pathType == BadPath)
        {
            //合全部加入
            availablePaths.push_back (pathInfo);
        }
    }
    
    //如果候选非空，则在候选随机选择一条路径
    struct PathInfo newPath;
    if (!availablePaths.empty ())
    {
        newPath = availablePaths[rand() % availablePaths.size ()];
    }
    else
    {
        //否则随机选一个候选路径
        uint32_t pathId = (itr->second)[rand() % (itr->second).size ()];
        newPath = Ipv4TLB::JudgePath (destTor, pathId);
    }
    NS_LOG_LOGIC ("Random selection return path: " << newPath.pathId);
    return newPath;
}

//判断路径是什么类型的
struct PathInfo
Ipv4TLB::JudgePath (uint32_t destTor, uint32_t pathId)
{
    //查询TLBPathInfo
    std::pair<uint32_t, uint32_t> key = std::make_pair (destTor, pathId);
    std::map<std::pair<uint32_t, uint32_t>, TLBPathInfo>::iterator itr = m_pathInfo.find (key);

    struct PathInfo path;
    path.pathId = pathId;
    //如果没找到，则直接返回，返回类型为greyPath
    if (itr == m_pathInfo.end ())
    {
        path.pathType = GreyPath;
        /*path.pathType = GoodPath;*/
        /*path.rttMin = m_betterPathRttThresh + MicroSeconds (100);*/
        path.rttMin = m_minRtt;
        path.size = 0;
        path.ecnPortion = 0.3;
        path.counter = 0;
        path.quantifiedDre = 0;
        return path;
    }
    TLBPathInfo pathInfo = itr->second;
    path.rttMin = pathInfo.minRtt;
    path.size = pathInfo.size;
    //ECN的比例
    path.ecnPortion = static_cast<double>(pathInfo.ecnSize) / pathInfo.size;
    path.counter = pathInfo.flowCounter;
    path.quantifiedDre = Ipv4TLB::QuantifyDre (pathInfo.dreValue);
    //如果超守ECN采样的最小值才考虑ECN，否则不考虑
    int consideECN = (pathInfo.size > m_ecnSampleMin) ? 1 : 0;
    //m_rttAlpah是RTT在判断路径时所占的比重 m_ecnBeta是ECN在判断路径时据点的比重
    if ((m_rttAlpha * pathInfo.minRtt + m_ecnBeta * consideECN * static_cast<double>(pathInfo.ecnSize) / pathInfo.size
                < m_rttAlpha * m_minRtt + m_ecnBeta * consideECN * m_ecnPortionLow)
            && (pathInfo.isRetransmission) == false
            /*&& (pathInfo.isHighRetransmission) == false*/ //只要isRetransmission为false，则isHighRetransmission一定为false，下面同理。
            && (pathInfo.isTimeout) == false
            /*&& (pathInfo.isVeryTimeout) == false*/
            && (pathInfo.isProbingTimeout == false))
    {
        path.pathType = GoodPath;
        return path;
    }

    if (pathInfo.isHighRetransmission
            || pathInfo.isVeryTimeout
            || pathInfo.isProbingTimeout)
    {
        path.pathType = FailPath;
        return path;
    }

    if ((m_rttAlpha * pathInfo.minRtt + m_ecnBeta * consideECN * static_cast<double>(pathInfo.ecnSize) / pathInfo.size
                >= m_rttAlpha * m_highRtt + m_ecnBeta * consideECN * m_ecnPortionLow) //这里本来是小于号，m_ecnPortionHigh改为m_ecnPortionLow
            || pathInfo.isTimeout == true
            || pathInfo.isRetransmission == true)
    {
        path.pathType = BadPath;
        return path;
    }

    path.pathType = GreyPath;
    return path;
}

//对比两个路径，第一个路径是否比第二个路径好
bool
Ipv4TLB::PathLIsBetterR (struct PathInfo pathL, struct PathInfo pathR)
{
    if (/*pathR.ecnPortion - pathL.ecnPortion >= m_betterPathEcnThresh // TODO Comment ECN
        &&*/ pathR.rttMin - pathL.rttMin >= m_betterPathRttThresh)
    {
        return true;
    }
    else
    {
        return false;
    }
}

//根据目的地址返回TorId
bool
Ipv4TLB::FindTorId (Ipv4Address daddr, uint32_t &destTorId)
{
    std::map<Ipv4Address, uint32_t>::iterator torItr = m_ipTorMap.find (daddr);

    if (torItr == m_ipTorMap.end ())
    {
        return false;
    }
    destTorId = torItr->second;
    return true;
}

//路径老化事件
void
Ipv4TLB::PathAging (void)
{
    NS_LOG_LOGIC (this << " Path Info: " << (Simulator::Now ()));
    //第一个是destTor，第二个是pathId
    std::map<std::pair<uint32_t, uint32_t>, TLBPathInfo>::iterator itr = m_pathInfo.begin ();
    for ( ; itr != m_pathInfo.end (); ++itr)
    {
        NS_LOG_LOGIC ("<" << (itr->first).first << "," << (itr->first).second << ">");
        NS_LOG_LOGIC ("\t" << " Size: " << (itr->second).size
                           << " ECN Size: " << (itr->second).ecnSize
                           << " Min RTT: " << (itr->second).minRtt
                           << " Is Retransmission: " << (itr->second).isRetransmission
                           << " Is HRetransmission: " << (itr->second).isHighRetransmission
                           << " Is Timeout: " << (itr->second).isTimeout
                           << " Is VTimeout: " << (itr->second).isVeryTimeout
                           << " Is ProbingTimeout: " << (itr->second).isProbingTimeout
                           << " Flow Counter: " << (itr->second).flowCounter);
        //如果当前时间距离上次超过m_T1,则会老化超时等变量
        if (Simulator::Now() - (itr->second).timeStamp1 > m_T1)
        {
            //gj 更新一些参数
            (itr->second).size = 1;
            (itr->second).ecnSize = 0;
            (itr->second).isTimeout = false;
            (itr->second).timeStamp1 = Simulator::Now ();
        }
        //如果超过m_T2则老化重传等变量
        if (Simulator::Now () - (itr->second).timeStamp2 > m_T2)
        {
            (itr->second).isRetransmission = false;
            (itr->second).isHighRetransmission = false;
            (itr->second).isVeryTimeout = false;
            (itr->second).isProbingTimeout = false;
            (itr->second).timeStamp2 = Simulator::Now ();
        }//如果超过则更新 TODO
        if (Simulator::Now () - (itr->second).timeStamp3 > m_T1)
        {
            if (m_isSmooth)
            {
                Time desiredRtt = m_minRtt * m_smoothDesired / SMOOTH_BASE;
                if ((itr->second).minRtt < desiredRtt)
                {
                    (itr->second).minRtt = std::min (desiredRtt, (itr->second).minRtt * m_smoothBeta1 / SMOOTH_BASE);
                }
                else
                {
                    (itr->second).minRtt = std::max (desiredRtt, (itr->second).minRtt * m_smoothBeta2 / SMOOTH_BASE);
                }
            }
            else
            {
                (itr->second).minRtt = Seconds (666);
            }
            (itr->second).timeStamp3 = Simulator::Now ();
        }

        /*
        if (Simulator::Now () - (itr->second).epTimeStamp > m_epAgingTime)
        {
            (itr->second).epAckSize = 1;
            (itr->second).epEcnSize = 0;
            (itr->second).epEcnPortion = m_epDefaultEcnPortion;
            (itr->second).epTimeStamp = Simulator::Now ();
        }
        */
    }

    //如果一个流的时间超过m_flowDieTime,则从路径上移除
    std::map<uint32_t, TLBFlowInfo>::iterator itr2 = m_flowInfo.begin ();
    for ( ; itr2 != m_flowInfo.end (); ++itr2)
    {
        if (Simulator::Now () - (itr2->second).liveTime >= m_flowDieTime)
        {
            Ipv4TLB::RemoveFlowFromPath ((itr2->second).flowId, (itr2->second).destTor, (itr2->second).path);
            m_flowInfo.erase (itr2);
        }

        /*
        else if (Simulator::Now () - (itr2->second).epTimeStamp > m_epAgingTime)
        {
            (itr2->second).epAckSize = 1;
            (itr2->second).epEcnSize = 0;
            (itr2->second).epEcnPortion = m_epDefaultEcnPortion;
            (itr2->second).epTimeStamp = Simulator::Now ();
        }
        */
    }

    m_agingEvent = Simulator::Schedule (m_agingCheckTime, &Ipv4TLB::PathAging, this);
}

//得到到这个目的地址所有路径的路径信息
std::vector<PathInfo>
Ipv4TLB::GatherParallelPaths (uint32_t destTor)
{
    std::vector<PathInfo> paths;

    std::map<uint32_t, std::vector<uint32_t> >::iterator itr = m_availablePath.find (destTor);
    if (itr == m_availablePath.end ())
    {
        return paths;
    }

    std::vector<uint32_t>::iterator innerItr = (itr->second).begin ();
    for ( ; innerItr != (itr->second).end (); ++innerItr )
    {
        paths.push_back(Ipv4TLB::JudgePath ((itr->first), *innerItr));
    }

    return paths;
}

//DRE老化事件
void
Ipv4TLB::DreAging (void)
{
    std::map<std::pair<uint32_t, uint32_t>, TLBPathInfo>::iterator itr = m_pathInfo.begin ();
    for ( ; itr != m_pathInfo.end (); ++itr)
    {
        NS_LOG_LOGIC ("<" << (itr->first).first << "," << (itr->first).second << ">");
        (itr->second).dreValue *= (1 - m_dreAlpha);
        NS_LOG_LOGIC ("\tDre value :" << Ipv4TLB::QuantifyDre ((itr->second).dreValue));
    }

    m_dreEvent = Simulator::Schedule (m_dreTime, &Ipv4TLB::DreAging, this);
}

//量化RTT
uint32_t
Ipv4TLB::QuantifyRtt (Time rtt)
{
    if (rtt <= m_minRtt + m_quantifyRttBase)
    {
        return 0;
    }
    else if (rtt <= m_minRtt + 2 * m_quantifyRttBase)
    {
        return 1;
    }
    else if (rtt <= m_minRtt + 3 * m_quantifyRttBase)
    {
        return 2;
    }
    else if (rtt <= m_minRtt + 4 * m_quantifyRttBase)
    {
        return 3;
    }
    else
    {
        return 4;
    }
}

//量化DRE
uint32_t
Ipv4TLB::QuantifyDre (uint32_t dre)
{
    double ratio = static_cast<double> (dre * 8) / (m_dreDataRate.GetBitRate () * m_dreTime.GetSeconds () / m_dreAlpha);
    return static_cast<uint32_t> (ratio * std::pow (2, m_dreQ));
}
//得到路径类型
std::string
Ipv4TLB::GetPathType (PathType type)
{
    if (type == GoodPath)
    {
        return "GoodPath";
    }
    else if (type == GreyPath)
    {
        return "GreyPath";
    }
    else if (type == BadPath)
    {
        return "BadPath";
    }
    else if (type == FailPath)
    {
        return "FailPath";
    }
    else
    {
        return "Unknown";
    }
}

//输出Logo
std::string
Ipv4TLB::GetLogo (void)
{
    std::stringstream oss;
    oss << " .-') _           .-. .-')           ('-.       .-') _    ('-.    .-. .-')               ('-.  _ .-') _   " << std::endl;
    oss << "(  OO) )          \\  ( OO )        _(  OO)     ( OO ) )  ( OO ).-.\\  ( OO )            _(  OO)( (  OO) )  " << std::endl;
    oss << "/     '._ ,--.     ;-----.\\       (,------.,--./ ,--,'   / . --. / ;-----.\\  ,--.     (,------.\\     .'_  " << std::endl;
    oss << "|'--...__)|  |.-') | .-.  |        |  .---'|   \\ |  |\\   | \\-.  \\  | .-.  |  |  |.-')  |  .---',`'--..._) " << std::endl;
    oss << "'--.  .--'|  | OO )| '-' /_)       |  |    |    \\|  | ).-'-'  |  | | '-' /_) |  | OO ) |  |    |  |  \\  ' " << std::endl;
    oss << "   |  |   |  |`-' || .-. `.       (|  '--. |  .     |/  \\| |_.'  | | .-. `.  |  |`-' |(|  '--. |  |   ' | " << std::endl;
    oss << "   |  |  (|  '---.'| |  \\  |       |  .--' |  |\\    |    |  .-.  | | |  \\  |(|  '---.' |  .--' |  |   / : " << std::endl;
    oss << "   |  |   |      | | '--'  /       |  `---.|  | \\   |    |  | |  | | '--'  / |      |  |  `---.|  '--'  / " << std::endl;
    oss << "   `--'   `------' `------'        `------'`--'  `--'    `--' `--' `------'  `------'  `------'`-------'  " << std::endl;
    return oss.str ();
}

}
