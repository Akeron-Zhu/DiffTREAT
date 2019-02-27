/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
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
 */

#include <fstream>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/netanim-module.h"

#include "ns3/flow-monitor-module.h"

#include <queue>
#include <fstream>

extern "C"
{
#include "cdf.h"
}

#define LINK_CAPACITY_BASE 1000000000 // 1Gbps
#define BUFFER_SIZE 600               // 250 packets

#define RED_QUEUE_MARKING 65 // 65 Packets (available only in DcTcp)

// The flow port range, each flow will be assigned a random port number within this range
#define PORT_START 10000
#define PORT_END 50000

// Adopted from the simulation from WANG PENG
// Acknowledged to https://williamcityu@bitbucket.org/williamcityu/2016-socc-simulation.git
#define PACKET_SIZE 1400

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TestPrio");

//

struct FlowInfo
{
    double time;
    double ddl;
    int flowsize;
    FlowInfo(double t, double d, int f) : time(t), ddl(d), flowsize(f)
    {
    }
};

class CMP //定义比较方法，先比较dist,dist小的在前面，如果dist相等，再比较fre，fre大的在前面
{
  public:
    bool operator()(FlowInfo left, FlowInfo right) const
    {
        return left.time >= right.time;
    }
};

std::priority_queue<FlowInfo, std::vector<FlowInfo>, CMP> m_flows;

void InstallCache(Ptr<Node> node, std::string name)
{
    Ptr<Cache> t_cache = CreateObject<Cache>();
    t_cache->m_name = name;
    node->SetCache(t_cache);
}

void InstallCache(NodeContainer c, std::string name)
{
    int count = 0;
    for (NodeContainer::Iterator i = c.Begin(); i != c.End(); ++i, count++)
    {
        char buffer[20];
        sprintf(buffer, "%d", count);
        InstallCache(*i, std::string(name + buffer));
    }
} //*/

double poission_gen_interval(double avg_rate)
{
    if (avg_rate > 0)
        return -logf(1.0 - (double)rand() / RAND_MAX) / avg_rate;
    else
        return 0;
}

template <typename T>
T rand_range(T min, T max)
{
    return min + ((double)max - min) * rand() / RAND_MAX;
}

int main(int argc, char *argv[])
{
#if 1
    LogComponentEnable("TestPrio", LOG_LEVEL_DEBUG);
    //LogComponentEnable("PointToPointNetDevice", LOG_LEVEL_DEBUG);
    //LogComponentEnable("Cache", LOG_LEVEL_DEBUG);
    //LogComponentEnable("PrioQueueDisc", LOG_LEVEL_DEBUG);
    // LogComponentEnable("BulkSendApplication", LOG_LEVEL_DEBUG);
#endif
    std::string loadCdf = "DCTCP_CDF.txt";
    bool m_enableCache = true;
    bool m_enableMakring = true;
    bool m_enableUrgePkt = true; //if false, disc not deal urge packet. and tcp also not send urge
    bool m_enableRtoRank = true; //if false no priority
    bool m_enableSizeRank = true;
    bool m_cacheFirst = true;
    bool m_cacheFIFO = false; //if ture m_enableUrgePkt must be false.
    bool m_enableCacheLog = false;
    bool m_contest = false;
    bool m_WRConcurrent = false;

    DataRate m_cacheSpeed = DataRate("50Gbps");
    double START_TIME = 0.0, END_TIME = 0.1, FLOW_LAUNCH_END_TIME = 0.10;
    uint32_t m_cacheBand = 2;
    uint32_t m_uncacheFlowRto = 5; //ms
    uint32_t m_cacheFlowRto = 10;
    uint32_t m_reTxThre = 1000;
    double m_cacheThre = 0.5;
    double m_uncacheThre = 0.4;
    double m_markThre = 0.34;
    uint32_t m_markCacheThre = 500;
    uint32_t m_cachePor = 50;

    double meanDdl = 0.1; //ms

    bool m_isNtcp = false; //modify retxthre and minrto for cacheable flow and control urge pkt with m_enableUrgePkt
    uint32_t m_cdfType = 0;

    double load = 0.6;
    double leafServerCapacity = 10;
    double linkLatency = 10;
    int ServerNum = 41, SwitchNum = 1;

    bool resequenceBuffer = false;          //是否有buffer用来防止乱序，这个参数有效下面三个才有效
    uint32_t resequenceInOrderTimer = 5;    // MicroSeconds
    uint32_t resequenceInOrderSize = 100;   // 100 Packets
    uint32_t resequenceOutOrderTimer = 100; // MicroSeconds
    bool resequenceBufferLog = false;

    unsigned randomSeed = 0;
    std::string transportProt = "Tcp";
    std::string id = "1";
    CommandLine cmd;
    cmd.AddValue("ID", " Running ID", id);
    cmd.AddValue("StartTime", "Start time of the simulation", START_TIME);
    cmd.AddValue("EndTime", "End time of the simulation", END_TIME);
    cmd.AddValue("FlowLaunchEndTime", "End time of the flow launch period", FLOW_LAUNCH_END_TIME);
    cmd.AddValue("randomSeed", "Random seed, 0 for random generated", randomSeed);
    cmd.AddValue("cdfFileName", "File name for flow distribution", loadCdf);
    cmd.AddValue("load", "Load of the network, 0.0 - 1.0", load);
    cmd.AddValue("transportProt", "Transport protocol to use: Tcp, DcTcp", transportProt);
    cmd.AddValue("linkLatency", "Link latency, should be in MicroSeconds", linkLatency);
    cmd.AddValue("DDL", "Link latency, should be in MicroSeconds", meanDdl);
    cmd.Parse(argc, argv);

    uint64_t LINK_CAPACITY = leafServerCapacity * LINK_CAPACITY_BASE;
    Time LINK_LATENCY = MicroSeconds(linkLatency);

    if (transportProt.compare("NTcp") != 0)
    {
        m_enableCache = false;
        m_enableMakring = false;
        m_enableUrgePkt = false; //if false, disc not deal urge packet.
        m_enableSizeRank = false;
        resequenceBuffer = false;
        m_isNtcp = false;
        m_cacheFlowRto = 5;
    } //*/
    else
    {
        m_isNtcp = true;
        resequenceBuffer = true;
    }

    if (loadCdf[0] == 'D')
        m_cdfType = 0;
    else if (loadCdf[0] == 'V')
        m_cdfType = 1;
    else if (loadCdf[0] == 'L')
        m_cdfType = 2;
    else
    {
        std::cout << "CDF File ERROR!\n";
        return 0;
    }

    NS_LOG_INFO("Initialize random seed: " << randomSeed);
    if (randomSeed == 0)
    {
        srand((unsigned)time(NULL));
    }
    else
    {
        srand(randomSeed);
    }

    if (resequenceBuffer)
    {
        NS_LOG_INFO("Enabling Resequence Buffer");
        Config::SetDefault("ns3::TcpSocketBase::ResequenceBuffer", BooleanValue(true));
        Config::SetDefault("ns3::TcpResequenceBuffer::InOrderQueueTimerLimit", TimeValue(MicroSeconds(resequenceInOrderTimer)));
        Config::SetDefault("ns3::TcpResequenceBuffer::SizeLimit", UintegerValue(resequenceInOrderSize));
        Config::SetDefault("ns3::TcpResequenceBuffer::OutOrderQueueTimerLimit", TimeValue(MicroSeconds(resequenceOutOrderTimer)));
    }

    NS_LOG_INFO("Config parameters");
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(PACKET_SIZE));
    Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(0));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(10));
    Config::SetDefault("ns3::TcpSocketBase::MinRto", TimeValue(MilliSeconds(m_cacheFlowRto)));
    Config::SetDefault("ns3::TcpSocketBase::UrgeSend", BooleanValue(m_enableUrgePkt)); //added
    Config::SetDefault("ns3::TcpSocketBase::ClockGranularity", TimeValue(MicroSeconds(100)));
    Config::SetDefault("ns3::RttEstimator::InitialEstimation", TimeValue(MicroSeconds(80)));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(160000000));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(160000000));
    Config::SetDefault("ns3::Ipv4GlobalRouting::PerflowEcmpRouting", BooleanValue(true));
    srand((unsigned)time(NULL));
    Ptr<Node> Switches = CreateObject<Node>();
    NodeContainer serverNode;
    serverNode.Create(ServerNum);

    if (transportProt.compare("NTcp") == 0 && m_enableCache)
    {

        Config::SetDefault("ns3::Cache::FIFO", BooleanValue(m_cacheFIFO));
        Config::SetDefault("ns3::Cache::DataRate", DataRateValue(m_cacheSpeed));
        Config::SetDefault("ns3::Cache::CacheLog", BooleanValue(m_enableCacheLog));
        Config::SetDefault("ns3::Cache::Contest", BooleanValue(m_contest));
        Config::SetDefault("ns3::Cache::WRConcurrent", BooleanValue(m_WRConcurrent));
        if (m_cacheFIFO && m_enableUrgePkt)
        {
            printf("Cache is FIFO, it can not use urge packet, auto colse UrgePkts!\n");
            m_enableUrgePkt = false;
        }
        if (m_cacheFIFO && !m_cacheFirst)
        {
            printf("Cache is FIFO, it can not use cache first, auto colse CacheFirst!\n");
            m_cacheFirst = true;
        }
        InstallCache(serverNode, std::string("serverNode"));
        InstallCache(Switches, std::string("Switches"));
    }
    // Switches->InitCache();
    InternetStackHelper stack;
    Ipv4GlobalRoutingHelper globalRoutingHelper;
    stack.SetRoutingHelper(globalRoutingHelper);
    stack.Install(serverNode);
    stack.Install(Switches);

    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", DataRateValue(DataRate(LINK_CAPACITY)));
    pointToPoint.SetChannelAttribute("Delay", TimeValue(LINK_LATENCY));

    //pointToPoint.SetDeviceAttribute("DataRate", StringValue("10Gbps"));
    //pointToPoint.SetChannelAttribute("Delay", StringValue("10us"));
    if (transportProt.compare("Tcp") == 0)
    {
        // 这里是设置NetDevice层的队列，
        //每个点对点结点必须有一个队列来传包，这里是为Link设置一个队列，使得当这个Link连接到节点时自动创建
        pointToPoint.SetQueue("ns3::DropTailQueue", "MaxPackets", UintegerValue(BUFFER_SIZE + 10));
    }
    else
    {
        pointToPoint.SetQueue("ns3::DropTailQueue", "MaxPackets", UintegerValue(10));
    }

    TrafficControlHelper tc;
    if (transportProt.compare("DcTcp") == 0)
    {
        //用于根据给定的属性设置根队列的Disc，超过这个阀值后丢包。这里是设置TC的队列。
        // 当device层队列满的时候会通知上面的TC层。这时TC层才会有效，TC层会停止发送包给NetDevice。
        // 否则，由于队列纪律的待办事项（backlog）是空的，所以不起作用。
        NS_LOG_INFO("Enabling DcTcp");
        Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TcpDCTCP::GetTypeId()));
        Config::SetDefault("ns3::RedQueueDisc::Mode", StringValue("QUEUE_MODE_PACKETS"));
        Config::SetDefault("ns3::RedQueueDisc::MeanPktSize", UintegerValue(PACKET_SIZE));
        Config::SetDefault("ns3::RedQueueDisc::QueueLimit", UintegerValue(BUFFER_SIZE));
        Config::SetDefault("ns3::RedQueueDisc::Gentle", BooleanValue(false));
        //Config::SetDefault ("ns3::QueueDisc::Quota", UintegerValue (BUFFER_SIZE));
        tc.SetRootQueueDisc("ns3::RedQueueDisc", "MinTh", DoubleValue(RED_QUEUE_MARKING),
                            "MaxTh", DoubleValue(RED_QUEUE_MARKING));
    }
    else if (transportProt.compare("NTcp") == 0)
    {
        Config::SetDefault("ns3::PrioQueueDisc::Mode", StringValue("QUEUE_MODE_PACKETS"));
        Config::SetDefault("ns3::PrioQueueDisc::EnableCache", BooleanValue(m_enableCache));
        Config::SetDefault("ns3::PrioQueueDisc::EnableMarking", BooleanValue(m_enableMakring));
        Config::SetDefault("ns3::PrioQueueDisc::EnableUrge", BooleanValue(m_enableUrgePkt));
        Config::SetDefault("ns3::PrioQueueDisc::CacheBand", UintegerValue(m_cacheBand));
        Config::SetDefault("ns3::PrioQueueDisc::EnCacheFirst", BooleanValue(m_cacheFirst));
        Config::SetDefault("ns3::PrioQueueDisc::CacheThre", DoubleValue(m_cacheThre));
        Config::SetDefault("ns3::PrioQueueDisc::UnCacheThre", DoubleValue(m_uncacheThre));
        Config::SetDefault("ns3::PrioQueueDisc::MarkThre", DoubleValue(m_markThre));
        Config::SetDefault("ns3::PrioQueueDisc::MarkCacheThre", UintegerValue(m_markCacheThre));
        if (!m_cacheFirst)
        {
            m_reTxThre = 3;
        }

        tc.SetRootQueueDisc("ns3::PrioQueueDisc");
        tc.AddPacketFilter(0, "ns3::PrioQueueDiscFilter");
    }
    else if (transportProt.compare("Tcpf") == 0)
    {
        Config::SetDefault("ns3::PrioQueueDisc::Mode", StringValue("QUEUE_MODE_PACKETS"));
        Config::SetDefault("ns3::PrioQueueDisc::EnableCache", BooleanValue(false));
        Config::SetDefault("ns3::PrioQueueDisc::EnableMarking", BooleanValue(false));
        Config::SetDefault("ns3::PrioQueueDisc::EnableUrge", BooleanValue(false));
        Config::SetDefault("ns3::PrioQueueDisc::CacheBand", UintegerValue(m_cacheBand));
        Config::SetDefault("ns3::PrioQueueDisc::EnCacheFirst", BooleanValue(m_cacheFirst));
        Config::SetDefault("ns3::PrioQueueDisc::CacheThre", DoubleValue(m_cacheThre));
        Config::SetDefault("ns3::PrioQueueDisc::UnCacheThre", DoubleValue(m_uncacheThre));
        Config::SetDefault("ns3::PrioQueueDisc::MarkThre", DoubleValue(m_markThre));
        Config::SetDefault("ns3::PrioQueueDisc::MarkCacheThre", UintegerValue(m_markCacheThre));
        m_reTxThre = 3;
        m_enableSizeRank = false;
        tc.SetRootQueueDisc("ns3::PrioQueueDisc");
        tc.AddPacketFilter(0, "ns3::PrioQueueDiscFilter");
    }

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interface;
    for (int i = 0; i < SwitchNum; i++)
    {
        for (int j = 0; j < ServerNum; j++)
        {

            NodeContainer part = NodeContainer(serverNode.Get(j), Switches);
            // if (ServerNum - 1 == j)
            //{
            //pointToPoint.SetDeviceAttribute("DataRate", StringValue("10kbps"));
            //}
            NetDeviceContainer netDevice = pointToPoint.Install(part);
            if (transportProt.compare("Tcp") != 0)
                tc.Install(netDevice);

            interface = address.Assign(netDevice);
            if (transportProt.compare("Tcp") == 0)
                tc.Uninstall(netDevice); //address.Assign()会自动分配tc.
            address.NewNetwork();
        }
    }

    struct cdf_table *cdfTable = new cdf_table();
    init_cdf(cdfTable);
    load_cdf(cdfTable, loadCdf.c_str());

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    double requestRate = load * LINK_CAPACITY / (8 * avg_cdf(cdfTable)) / (ServerNum - 1);

    /*Ptr<Ipv4> ipv4 = serverNode.Get(ServerNum - 1)->GetObject<Ipv4>();
  Ipv4InterfaceAddress destInterface = ipv4->GetAddress(1, 0); //得到第一个网络接口的所有IP地址，地址有主次之分
  Ipv4Address destAddress2 = destInterface.GetLocal();
  std::cout<<destAddress2<<'\n'; //*/
    int flowCount = 0;
    for (int i = 0; i < ServerNum - 1; i++)
    {
        double startTime = START_TIME + poission_gen_interval(requestRate);
        while (startTime < FLOW_LAUNCH_END_TIME)
        {
            flowCount++;
            uint16_t port = rand_range(PORT_START, PORT_END);
            uint32_t flowSize = gen_random_cdf(cdfTable);

            //std::cout << "FlowSize=" << flowSize << '\n';
            Address destAddress(InetSocketAddress(interface.GetAddress(0), port));
            BulkSendHelper source("ns3::TcpSocketFactory", destAddress);

            uint8_t rtoRank = (rand() % 100 + 1) > m_cachePor ? 1 : 2;
            if (flowSize > 10e6)
                rtoRank = 2;
            double ddl = -1;
            if (rtoRank == 1)
                ddl = std::max(poission_gen_interval(1.0 / meanDdl), 1.25 * 8 * flowSize / LINK_CAPACITY * 1000 + 2 * linkLatency / 1e3);
            m_flows.push(FlowInfo(startTime, ddl, flowSize));

            //        printf("%d\n",rtoRank);
            source.SetAttribute("SendSize", UintegerValue(PACKET_SIZE)); //每次发送的量
            source.SetAttribute("MaxBytes", UintegerValue(flowSize));    //总共发送的数量，0表示无限制
            //source.SetAttribute("DelayThresh", UintegerValue(applicationPauseThresh));       //多少包过去后发生Delay
            //source.SetAttribute("DelayTime", TimeValue(MicroSeconds(applicationPauseTime))); //Delay的时间
            source.SetAttribute("EnableRTORank", BooleanValue(m_enableRtoRank));   //每次发送的量
            source.SetAttribute("EnableSizeRank", BooleanValue(m_enableSizeRank)); //每次发送的量
            source.SetAttribute("NTcp", BooleanValue(m_isNtcp));
            source.SetAttribute("RtoRank", UintegerValue(rtoRank)); //每次发送的量
            source.SetAttribute("CacheBand", UintegerValue(m_cacheBand));
            source.SetAttribute("RTO", TimeValue(MilliSeconds(m_uncacheFlowRto))); //
            source.SetAttribute("ReTxThre", UintegerValue(m_reTxThre));
            source.SetAttribute("CDFType", UintegerValue(m_cdfType));
            ApplicationContainer sourceApp = source.Install(serverNode.Get(i));

            sourceApp.Start(Seconds(startTime));
            sourceApp.Stop(Seconds(END_TIME));

            PacketSinkHelper sink("ns3::TcpSocketFactory", destAddress);
            ApplicationContainer sinkApp = sink.Install(serverNode.Get(ServerNum - 1));
            sinkApp.Start(Seconds(START_TIME));
            sinkApp.Stop(Seconds(END_TIME));

            startTime += poission_gen_interval(requestRate);
        }
    }

    if (flowCount)
    {
        std::stringstream outputName;
        outputName << id << "-" << load << ".txt";
        std::ofstream outfile(outputName.str().c_str(), std::ios::out);
        while (!m_flows.empty())
        {
            FlowInfo fi = m_flows.top();
            m_flows.pop();
            //int packetNum = ceil(fi.flowsize / 1400.0) + 3;
            outfile << std::fixed << fi.flowsize << ' ' << fi.ddl << '\n';
            printf("Flow size %8d %8.4lf %8.4lf\n", fi.flowsize, fi.ddl, fi.time);
        } //*/
        outfile.close();
    }

    Ptr<FlowMonitor> flowMonitor;
    FlowMonitorHelper flowHelper;
    // 在所有节点上开启流监控
    flowMonitor = flowHelper.InstallAll();
    std::stringstream flowMonitorFilename;
    flowMonitorFilename << id << "-test-prio-" << load << '-' << ServerNum << "X" << SwitchNum << "-" << transportProt << "-";
    flowMonitorFilename << "b" << BUFFER_SIZE << ".xml";
    printf("Flow Number is :%d\n", flowCount);
    Simulator::Stop(Seconds(END_TIME));
    Simulator::Run();

    flowMonitor->SerializeToXmlFile(flowMonitorFilename.str(), true, true);
    std::stringstream doubleToStr;
    doubleToStr << "./xmdl " << flowMonitorFilename.str().c_str() << " " << load << " " << id << " ";
    if (transportProt == "Tcp")
        doubleToStr << 0;
    else
        doubleToStr << 1;
    system(doubleToStr.str().c_str());
    Simulator::Destroy();

    return 0;
}
