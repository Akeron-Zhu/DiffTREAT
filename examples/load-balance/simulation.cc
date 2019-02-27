#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"

#include "ns3/flow-monitor-module.h"
#include "ns3/ipv4-conga-routing-helper.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/ipv4-drb-routing-helper.h"
#include "ns3/ipv4-xpath-routing-helper.h"
#include "ns3/ipv4-tlb.h"
#include "ns3/ipv4-clove.h"
#include "ns3/ipv4-tlb-probing.h"
#include "ns3/link-monitor-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/tcp-resequence-buffer.h"
#include "ns3/ipv4-drill-routing-helper.h"
#include "ns3/ipv4-letflow-routing-helper.h"

#include <vector>
#include <map>
#include <utility>
#include <set>
#include <queue>

// The CDF in TrafficGenerator
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

#define PRESTO_RATIO 10

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("CongaSimulationLarge");

enum RunMode
{
    TLB,
    CONGA,
    CONGA_FLOW,
    CONGA_ECMP,
    PRESTO,
    WEIGHTED_PRESTO, // Distribute the packet according to the topology
    DRB,
    FlowBender,
    ECMP,
    Clove,
    DRILL,
    LetFlow
};

std::stringstream tlbBibleFilename;
std::stringstream tlbBibleFilename2;
std::stringstream rbTraceFilename;

/****************************************************************************/

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
bool m_isAsymLink1 = false;

DataRate m_cacheSpeed = DataRate("50Gbps");
uint32_t m_cacheBand = 2;
uint32_t m_uncacheFlowRto = 5; //ms
uint32_t m_cacheFlowRto = 10; 
uint32_t m_reTxThre = 1000;
double m_cacheThre = 0.5;
double m_uncacheThre = 0.4;
double m_markThre = 0.34;
uint32_t m_markCacheThre = 500;
uint32_t m_cachePor = 50; 

bool m_isNtcp = false; //modify retxthre and minrto for cacheable flow and control urge pkt with m_enableUrgePkt
uint32_t m_cdfType = 0;

struct FlowInfo
{
    double time;
    int flowsize;
    FlowInfo(double t, int f) : time(t), flowsize(f)
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
/****************************************************************************/

// 只在TLB模式下调用
void TLBPathSelectTrace(uint32_t flowId, uint32_t fromTor, uint32_t destTor, uint32_t path, bool isRandom, PathInfo pathInfo, std::vector<PathInfo> parallelPaths)
{
    NS_LOG_UNCOND("Flow: " << flowId << " (" << fromTor << " -> " << destTor << ") selects path: " << path << " at: " << Simulator::Now());
    NS_LOG_UNCOND("\t Is random select: " << isRandom);
    NS_LOG_UNCOND("\t Path info: type -> " << Ipv4TLB::GetPathType(pathInfo.pathType) << ", min RTT -> " << pathInfo.rttMin
                                           << ", ECN Portion -> " << pathInfo.ecnPortion << ", Flow counter -> " << pathInfo.counter
                                           << ", Quantified DRE -> " << pathInfo.quantifiedDre);

    NS_LOG_UNCOND("\t Parallel path info: ");
    std::vector<PathInfo>::iterator itr = parallelPaths.begin();
    for (; itr != parallelPaths.end(); ++itr)
    {
        struct PathInfo path = *itr;
        NS_LOG_UNCOND("\t\t Path info: " << path.pathId << ", type -> " << Ipv4TLB::GetPathType(path.pathType) << ", min RTT -> " << path.rttMin
                                         << ", Path size -> " << path.size << ", ECN Portion -> " << path.ecnPortion << ", Flow counter -> " << path.counter
                                         << ", Quantified DRE -> " << path.quantifiedDre);
    }
    NS_LOG_UNCOND("\n");

    std::ofstream out(tlbBibleFilename.str().c_str(), std::ios::out | std::ios::app);
    out << "Flow: " << flowId << " (" << fromTor << " -> " << destTor << ") selects path: " << path << " at: " << Simulator::Now() << std::endl;
    out << "\t Is random select: " << isRandom << std::endl;
    out << "\t Path info: type -> " << Ipv4TLB::GetPathType(pathInfo.pathType) << ", min RTT -> " << pathInfo.rttMin
        << ", ECN Portion -> " << pathInfo.ecnPortion << ", Flow counter -> " << pathInfo.counter
        << ", Quantified DRE -> " << pathInfo.quantifiedDre << std::endl;

    out << "\t Parallel path info: " << std::endl;
    itr = parallelPaths.begin();
    for (; itr != parallelPaths.end(); ++itr)
    {
        struct PathInfo path = *itr;
        out << "\t\t Path info: " << path.pathId << ", type -> " << Ipv4TLB::GetPathType(path.pathType) << ", min RTT -> " << path.rttMin
            << ", Path size -> " << path.size << ", ECN Portion -> " << path.ecnPortion << ", Flow counter -> " << path.counter
            << ", Quantified DRE -> " << path.quantifiedDre << std::endl;
    }
    out << std::endl;
}

// 只在TLB模式下调用
void TLBPathChangeTrace(uint32_t flowId, uint32_t fromTor, uint32_t destTor, uint32_t newPath, uint32_t oldPath, bool isRandom, std::vector<PathInfo> parallelPaths)
{
    NS_LOG_UNCOND("Flow: " << flowId << " (" << fromTor << " -> " << destTor << ") changes path from: " << oldPath << " to " << newPath << " at: " << Simulator::Now());
    NS_LOG_UNCOND("\t Is random select: " << isRandom);
    NS_LOG_UNCOND("\t Parallel path info: ");
    std::vector<PathInfo>::iterator itr = parallelPaths.begin();
    for (; itr != parallelPaths.end(); ++itr)
    {
        struct PathInfo path = *itr;
        NS_LOG_UNCOND("\t\t Path info: " << path.pathId << ", type -> " << Ipv4TLB::GetPathType(path.pathType) << ", min RTT -> " << path.rttMin
                                         << ", Path size -> " << path.size << ", ECN Portion -> " << path.ecnPortion << ", Flow counter -> " << path.counter
                                         << ", Quantified DRE -> " << path.quantifiedDre);
    }
    NS_LOG_UNCOND("\n");

    std::ofstream out(tlbBibleFilename2.str().c_str(), std::ios::out | std::ios::app);
    out << "Flow: " << flowId << " (" << fromTor << " -> " << destTor << ") changes path from: " << oldPath << " to " << newPath << " at: " << Simulator::Now();
    out << "\t Is random select: " << isRandom << std::endl;
    out << "\t Parallel path info: " << std::endl;
    itr = parallelPaths.begin();
    for (; itr != parallelPaths.end(); ++itr)
    {
        struct PathInfo path = *itr;
        out << "\t\t Path info: " << path.pathId << ", type -> " << Ipv4TLB::GetPathType(path.pathType) << ", min RTT -> " << path.rttMin
            << ", Path size -> " << path.size << ", ECN Portion -> " << path.ecnPortion << ", Flow counter -> " << path.counter
            << ", Quantified DRE -> " << path.quantifiedDre << std::endl;
    }
    out << std::endl;
}
// 只在RBTrace中调用
void RBTraceBuffer(uint32_t flowId, Time time, SequenceNumber32 revSeq, SequenceNumber32 expectSeq)
{
    NS_LOG_UNCOND("Flow: " << flowId << " (at time: " << time << "), receives: " << revSeq << ", while expecting: " << expectSeq);
    std::ofstream out(rbTraceFilename.str().c_str(), std::ios::out | std::ios::app);
    out << "Flow: " << flowId << " (at time: " << time << "), receives: " << revSeq << ", while expecting: " << expectSeq << std::endl;
}
// 只在RBTrace中调用
void RBTraceFlush(uint32_t flowId, Time time, SequenceNumber32 popSeq, uint32_t inOrderQueueLength, uint32_t outOrderQueueLength, TcpRBPopReason reason)
{
    NS_LOG_UNCOND("Flow: " << flowId << " (at time: " << time << "), pops: " << popSeq << ", with in order queue: " << inOrderQueueLength
                           << ", out order queue: " << outOrderQueueLength << ", reason: " << reason);
    std::ofstream out(rbTraceFilename.str().c_str(), std::ios::out | std::ios::app);
    out << "Flow: " << flowId << " (at time: " << time << "), pops: " << popSeq << ", with in order queue: " << inOrderQueueLength
        << ", out order queue: " << outOrderQueueLength << ", reason: " << reason << std::endl;
}
// RBTrace调用RBTraceFlush与RBTraceBuffer且这两个函数不在其它地方调用，也只在resequenceBuffer为true时调用RBTrace函数
void RBTrace(void)
{
    Config::ConnectWithoutContext("/NodeList/*/$ns3::TcpL4Protocol/SocketList/*/ResequenceBufferPointer/Buffer", MakeCallback(&RBTraceBuffer));
    Config::ConnectWithoutContext("/NodeList/*/$ns3::TcpL4Protocol/SocketList/*/ResequenceBufferPointer/Flush", MakeCallback(&RBTraceFlush));
}

// Port from Traffic Generator
// Acknowledged to https://github.com/HKUST-SING/TrafficGenerator/blob/master/src/common/common.c
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

//给一个TOR下的每个服务器设定发送的流和发送的时间
void install_applications(int srcLeafId, NodeContainer servers, double requestRate, struct cdf_table *cdfTable,
                          long &flowCount, long &totalFlowSize, long &totalCacheableSize, int PER_LEAF_SERVER_COUNT, int LEAF_COUNT, double START_TIME,
                          double END_TIME, double FLOW_LAUNCH_END_TIME, uint32_t applicationPauseThresh, uint32_t applicationPauseTime)
{
    NS_LOG_INFO("Install applications:");
    //在每个叶结点的服务器上安装应用
    for (int i = 0; i < PER_LEAF_SERVER_COUNT; i++)
    {
        //NS_LOG_INFO("Install Begin");
        //得到服务器的坐标
        int srcServerIndex = srcLeafId * PER_LEAF_SERVER_COUNT + i;
        //得到这个服务器开始发送流的时间
        double startTime = START_TIME + poission_gen_interval(requestRate);
        //只要开始时间startTime还小于FLOW_LAUNCH_END_TIME，就可以发送
        while (startTime < FLOW_LAUNCH_END_TIME)
        {

            //std::cout << "startTime=" << startTime << ' ' << "FLOW_LAUNCH_END_TIME=" << FLOW_LAUNCH_END_TIME << '\n';
            //增加流的数量
            flowCount++;
            uint16_t port = rand_range(PORT_START, PORT_END); //生成一个随机端口.

            //生成一个不在这个叶结点下的服务器地址做为目的地址
            int destServerIndex = srcServerIndex;
            while (destServerIndex >= srcLeafId * PER_LEAF_SERVER_COUNT && destServerIndex < srcLeafId * PER_LEAF_SERVER_COUNT + PER_LEAF_SERVER_COUNT)
            {
                destServerIndex = rand_range(0, PER_LEAF_SERVER_COUNT * LEAF_COUNT);
            }

            Ptr<Node> destServer = servers.Get(destServerIndex);
            Ptr<Ipv4> ipv4 = destServer->GetObject<Ipv4>();
            Ipv4InterfaceAddress destInterface = ipv4->GetAddress(1, 0); //得到第一个网络接口的所有IP地址，地址有主次之分
            Ipv4Address destAddress = destInterface.GetLocal();          //得到IPV4地址，因为Ipv4InterfaceAddress中有本地地址、了网掩码和广播地址等。
            //用于在一系列节点上创建BulkSendApplication，其是尽可能填充带宽的应用，但只支持SOCK_STREAM 和 SOCK_SEQPACKET。
            BulkSendHelper source("ns3::TcpSocketFactory", InetSocketAddress(destAddress, port));
            uint32_t flowSize = gen_random_cdf(cdfTable); //基于CDF产生一个value值，随机产生一个介于0-1的cdf值，然后找到其对应的value值。

            m_flows.push(FlowInfo(startTime, flowSize));
            uint8_t rtoRank = (rand() % 100 + 1) > m_cachePor? 1:2; // sizeRank = rand() % 8;
            if(flowSize > 10e6) rtoRank = 2;
            //记录总的流大小
            totalFlowSize += flowSize;
            if (rtoRank > 1)
                totalCacheableSize += flowSize;
            //设置一系列发送的属性
            source.SetAttribute("SendSize", UintegerValue(PACKET_SIZE));                     //每次发送的量
            source.SetAttribute("MaxBytes", UintegerValue(flowSize));                        //总共发送的数量，0表示无限制
            source.SetAttribute("DelayThresh", UintegerValue(applicationPauseThresh));       //多少包过去后发生Delay
            source.SetAttribute("DelayTime", TimeValue(MicroSeconds(applicationPauseTime))); //Delay的时间
            source.SetAttribute("NTcp", BooleanValue(m_isNtcp));
            source.SetAttribute("RtoRank", UintegerValue(rtoRank));                          //每次发送的量
            source.SetAttribute("EnableRTORank", BooleanValue(m_enableRtoRank));             //每次发送的量
            source.SetAttribute("EnableSizeRank", BooleanValue(m_enableSizeRank));           //每次发送的量
            source.SetAttribute("CacheBand", UintegerValue(m_cacheBand));
            source.SetAttribute("RTO", TimeValue(MilliSeconds(m_uncacheFlowRto))); //
            source.SetAttribute("ReTxThre", UintegerValue(m_reTxThre));
            source.SetAttribute("CDFType", UintegerValue(m_cdfType));

            // Install apps
            ApplicationContainer sourceApp = source.Install(servers.Get(srcServerIndex));
            sourceApp.Start(Seconds(startTime));
            sourceApp.Stop(Seconds(END_TIME));

            // Install packet sinks
            //A helper to make it easier to instantiate an ns3::PacketSinkApplication on a set of nodes.
            // PacketSinkHelper用于在一系列结点上实例上PacketSinkApplication
            //第一个参数是用来接受traffic的协议名称，第二个是sink的地址。
            //如果使用GetAny()也可以，但是为了查找Sink这里必须使用地址。
            PacketSinkHelper sink("ns3::TcpSocketFactory",
                                  InetSocketAddress(destAddress,port));//Ipv4Address::GetAny(), port));
            //给节点安装应用
            ApplicationContainer sinkApp = sink.Install(servers.Get(destServerIndex));
            sinkApp.Start(Seconds(START_TIME));
            sinkApp.Stop(Seconds(END_TIME));

            /*NS_LOG_INFO ("\tFlow from server: " << (((servers.Get(srcServerIndex))->GetObject<Ipv4>())->GetAddress(1,0)).GetLocal() << " to server: "
                    << (((servers.Get(destServerIndex))->GetObject<Ipv4>())->GetAddress(1,0)).GetLocal()<< " on port: " << port << " with flow size: "
                    << flowSize << " [start time: " << startTime <<"]");
            //*/
            if (requestRate == 0)
            {
                std::cout << "requestRate = 0, trap in the while loop!!!" << '\n';
            }
            startTime += poission_gen_interval(requestRate);
        }
        // NS_LOG_INFO("Install OK");
    }
}

int main(int argc, char *argv[])
{
#if 1
    // LogComponentEnable("CongaSimulationLarge", LOG_LEVEL_INFO);
    // LogComponentEnable ("Queue",LOG_LEVEL_FUNCTION);
    //LogComponentEnable("Cache", LOG_LEVEL_FUNCTION);
    //LogComponentEnable("TcpSocketBase", LOG_LEVEL_DEBUG);

    //LogComponentEnable ("TcpResequenceBuffer",LOG_LEVEL_DEBUG);
    //LogComponentEnable("PacketSink", LOG_LEVEL_INFO);
    //LogComponentEnable("PrioQueueDisc", LOG_LEVEL_FUNCTION);
    //LogComponentEnable("PrioSubqueueDisc", LOG_LEVEL_DEBUG);
#endif

    // Command line parameters parsing
    std::string id = "0";
    std::string runModeStr = "Conga";
    unsigned randomSeed = 0;
    std::string cdfFileName = "DCTCP_CDF.txt";
    double load = 0.5;
    std::string transportProt = "Tcp";

    int SPINE_LEAF_COUNT = 8;
    int PER_LEAF_SERVER_COUNT = 16;
    int SPINE_COUNT = SPINE_LEAF_COUNT;
    int LEAF_COUNT = SPINE_LEAF_COUNT;
    int LINK_COUNT = 1;

    uint64_t spineLeafCapacity = 10;
    uint64_t leafServerCapacity = 10;
    uint32_t linkLatency = 10; //链路时延

    // The simulation starting and ending time
    double START_TIME = 0.0;
    double END_TIME = 0.1;
    double FLOW_LAUNCH_END_TIME = 0.1;

    bool asymCapacity = false;      //是否有链路不对称
    uint32_t asymCapacityPoss = 40; // 40 % 链路不对称的概率，指定了asymCapacity后这个参数才有效
    bool congaAwareAsym = true;
    bool asymCapacity2 = m_isAsymLink1;

    uint32_t congaFlowletTimeout = 500;
    uint32_t letFlowFlowletTimeout = 500;

    bool resequenceBuffer = false;           //是否有buffer用来防止乱序，这个参数有效下面三个才有效
    uint32_t resequenceInOrderTimer = 5;    // MicroSeconds
    uint32_t resequenceInOrderSize = 100;   // 100 Packets
    uint32_t resequenceOutOrderTimer = 100; // MicroSeconds
    bool resequenceBufferLog = false;

    double flowBenderT = 0.05;
    uint32_t flowBenderN = 3;

    uint32_t TLBMinRTT = 40;
    uint32_t TLBHighRTT = 180;
    uint32_t TLBPoss = 50;
    uint32_t TLBBetterPathRTT = 5;
    uint32_t TLBT1 = 100;
    double TLBECNPortionLow = 0.1;
    uint32_t TLBRunMode = 12;
    bool TLBProbingEnable = true;
    uint32_t TLBProbingInterval = 50;
    bool TLBSmooth = true;
    bool TLBRerouting = true;
    uint32_t TLBDREMultiply = 3;
    uint32_t TLBS = 64000;
    bool TLBReverseACK = false;
    uint32_t TLBFlowletTimeout = 500;

    uint32_t applicationPauseThresh = 0;
    uint32_t applicationPauseTime = 1000;

    uint32_t cloveFlowletTimeout = 500;
    uint32_t cloveRunMode = 1;
    uint32_t cloveHalfRTT = 40;
    bool cloveDisToUncongestedPath = true;

    bool enableLargeDupAck = false;
    bool enableRandomDrop = false; //开启任意丢包，这个有效下面参数才有效
    double randomDropRate = 0.005; // 0.5%

    uint32_t blackHoleMode = 0; // When the black hole is enabled, the
    std::string blackHoleSrcAddrStr = "10.1.1.1";
    std::string blackHoleSrcMaskStr = "255.255.255.0";
    std::string blackHoleDestAddrStr = "10.1.2.0";
    std::string blackHoleDestMaskStr = "255.255.255.0";

    bool enableLargeSynRetries = false; //Whether the SYN packet would retry thousands of times [default value: false]

    bool enableLargeDataRetries = false; //Whether the SYN gap will be very small when reconnecting [default value: false]

    bool enableFastReConnection = false; //Whether the SYN gap will be very small when reconnecting [default value: false]

    bool enableMFQ = false;

    bool tcpPause = false;
    uint32_t quantifyRTTBase = 10;

    CommandLine cmd;
    cmd.AddValue("ID", " Running ID", id);
    cmd.AddValue("StartTime", "Start time of the simulation", START_TIME);
    cmd.AddValue("EndTime", "End time of the simulation", END_TIME);
    cmd.AddValue("FlowLaunchEndTime", "End time of the flow launch period", FLOW_LAUNCH_END_TIME);
    cmd.AddValue("runMode", "Running mode of this simulation: Conga, Conga-flow, Presto, Weighted-Presto, DRB, FlowBender, ECMP, Clove, DRILL, LetFlow", runModeStr);
    cmd.AddValue("randomSeed", "Random seed, 0 for random generated", randomSeed);
    cmd.AddValue("cdfFileName", "File name for flow distribution", cdfFileName);
    cmd.AddValue("load", "Load of the network, 0.0 - 1.0", load);
    cmd.AddValue("transportProt", "Transport protocol to use: Tcp, DcTcp", transportProt);
    cmd.AddValue("linkLatency", "Link latency, should be in MicroSeconds", linkLatency);

    cmd.AddValue("serverCount", "The Server count", PER_LEAF_SERVER_COUNT);
    cmd.AddValue("spineCount", "The Spine count", SPINE_COUNT);
    cmd.AddValue("leafCount", "The Leaf count", LEAF_COUNT);
    cmd.AddValue("linkCount", "The Link count", LINK_COUNT);
    cmd.AddValue("spineLeafCapacity", "Spine <-> Leaf capacity in Gbps", spineLeafCapacity);
    cmd.AddValue("leafServerCapacity", "Leaf <-> Server capacity in Gbps", leafServerCapacity);

    cmd.AddValue("asymCapacity", "Whether the capacity is asym, which means some link will have only 1/10 the capacity of others", asymCapacity);
    cmd.AddValue("asymCapacityPoss", "The possibility that a path will have only 1/10 capacity", asymCapacityPoss);
    cmd.AddValue("asymCapacity2", "Whether the Spine0-Leaf0's capacity is asymmetric", asymCapacity2);

    cmd.AddValue("congaFlowletTimeout", "Flowlet timeout in Conga", congaFlowletTimeout);
    cmd.AddValue("congaAwareAsym", "Whether Conga is aware of the capacity of asymmetric path capacity", congaAwareAsym);

    cmd.AddValue("resequenceBuffer", "Whether enabling the resequence buffer", resequenceBuffer);
    cmd.AddValue("resequenceInOrderTimer", "In order queue timeout in resequence buffer", resequenceInOrderTimer);
    cmd.AddValue("resequenceOutOrderTimer", "Out order queue timeout in resequence buffer", resequenceOutOrderTimer);
    cmd.AddValue("resequenceInOrderSize", "In order queue size in resequence buffer", resequenceInOrderSize);
    cmd.AddValue("resequenceBufferLog", "Whether enabling the resequence buffer logging system", resequenceBufferLog);

    cmd.AddValue("flowBenderT", "The T in flowBender", flowBenderT);
    cmd.AddValue("flowBenderN", "The N in flowBender", flowBenderN);

    cmd.AddValue("TLBMinRTT", "Min RTT used to judge a good path in TLB", TLBMinRTT);
    cmd.AddValue("TLBHighRTT", "High RTT used to judge a bad path in TLB", TLBHighRTT);
    cmd.AddValue("TLBPoss", "Possibility to change the path in TLB", TLBPoss);
    cmd.AddValue("TLBBetterPathRTT", "RTT Threshold used to judge one path is better than another in TLB", TLBBetterPathRTT);
    cmd.AddValue("TLBT1", "The path aging time interval in TLB", TLBT1);
    cmd.AddValue("TLBECNPortionLow", "The ECN portion used in judging a good path in TLB", TLBECNPortionLow);
    cmd.AddValue("TLBRunMode", "The running mode of TLB, 0 for minimize counter, 1 for minimize RTT, 2 for random, 11 for RTT counter, 12 for RTT DRE", TLBRunMode);
    cmd.AddValue("TLBProbingEnable", "Whether the TLB probing is enable", TLBProbingEnable);
    cmd.AddValue("TLBProbingInterval", "Probing interval for TLB probing", TLBProbingInterval);
    cmd.AddValue("TLBSmooth", "Whether the RTT calculation is smooth", TLBSmooth);
    cmd.AddValue("TLBRerouting", "Whether the rerouting is enabled in TLB", TLBRerouting);
    cmd.AddValue("TLBDREMultiply", "DRE multiply factor in TLB", TLBDREMultiply);
    cmd.AddValue("TLBS", "The S used to judge a whether a flow should change path in TLB", TLBS);
    cmd.AddValue("TLBReverseACK", "Whether to enable the TLB reverse ACK path selection", TLBReverseACK);
    cmd.AddValue("quantifyRTTBase", "The quantify RTT base in TLB", quantifyRTTBase);
    cmd.AddValue("TLBFlowletTimeout", "The TLB flowlet timeout", TLBFlowletTimeout);

    cmd.AddValue("TcpPause", "Whether TCP will pause in TLB & FlowBender", tcpPause);

    cmd.AddValue("applicationPauseThresh", "How many packets can pass before we have delay, 0 for disable", applicationPauseThresh);
    cmd.AddValue("applicationPauseTime", "The time for a delay, in MicroSeconds", applicationPauseTime);

    cmd.AddValue("cloveFlowletTimeout", "Flowlet timeout for Clove", cloveFlowletTimeout);
    cmd.AddValue("cloveRunMode", "Clove run mode, 0 for edge flowlet, 1 for ECN, 2 for INT (not yet implemented)", cloveRunMode);
    cmd.AddValue("cloveHalfRTT", "Half RTT used in Clove ECN", cloveHalfRTT);
    cmd.AddValue("cloveDisToUncongestedPath", "Whether Clove will distribute the weight to uncongested path (no ECN) or all paths", cloveDisToUncongestedPath);

    cmd.AddValue("letFlowFlowletTimeout", "Flowlet timeout in LetFlow", letFlowFlowletTimeout);

    cmd.AddValue("enableRandomDrop", "Whether the Spine-0 to other leaves has the random drop problem", enableRandomDrop);
    cmd.AddValue("randomDropRate", "The random drop rate when the random drop is enabled", randomDropRate);
    cmd.AddValue("enableLargeDupAck", "Whether to set the ReTxThreshold to a very large value to mask reordering", enableLargeDupAck);

    cmd.AddValue("blackHoleMode", "The packet black hole mode, 0 to disable, 1 src, 2 dest, 3 src/dest pair", blackHoleMode);
    cmd.AddValue("blackHoleSrcAddr", "The packet black hole source address", blackHoleSrcAddrStr);
    cmd.AddValue("blackHoleSrcMask", "The packet black hole source mask", blackHoleSrcMaskStr);
    cmd.AddValue("blackHoleDestAddr", "The packet black hole destination address", blackHoleDestAddrStr);
    cmd.AddValue("blackHoleDestMask", "The packet black hole destination mask", blackHoleDestMaskStr);

    cmd.AddValue("enableLargeSynRetries", "Whether the SYN packet would retry thousands of times", enableLargeSynRetries);
    cmd.AddValue("enableFastReConnection", "Whether the SYN gap will be very small when reconnecting", enableFastReConnection);
    cmd.AddValue("enableLargeDataRetries", "Whether the data retransmission will be more than 6 times", enableLargeDataRetries);

    cmd.AddValue("enableMFQ", "Whether enable the large cache in the spine switch", enableMFQ);

    cmd.Parse(argc, argv);

    uint64_t SPINE_LEAF_CAPACITY = spineLeafCapacity * LINK_CAPACITY_BASE;
    uint64_t LEAF_SERVER_CAPACITY = leafServerCapacity * LINK_CAPACITY_BASE;
    Time LINK_LATENCY = MicroSeconds(linkLatency);

    Ipv4Address blackHoleSrcAddr = Ipv4Address(blackHoleSrcAddrStr.c_str());
    Ipv4Mask blackHoleSrcMask = Ipv4Mask(blackHoleSrcMaskStr.c_str());
    Ipv4Address blackHoleDestAddr = Ipv4Address(blackHoleDestAddrStr.c_str());
    Ipv4Mask blackHoleDestMask = Ipv4Mask(blackHoleDestMaskStr.c_str());

    RunMode runMode;
    if (runModeStr.compare("Conga") == 0)
    {
        runMode = CONGA;
    }
    else if (runModeStr.compare("Conga-flow") == 0)
    {
        runMode = CONGA_FLOW;
    }
    else if (runModeStr.compare("Conga-ECMP") == 0)
    {
        runMode = CONGA_ECMP;
    }
    else if (runModeStr.compare("Presto") == 0)
    {
        //Presto不支持链路数量大于1
        if (LINK_COUNT != 1)
        {
            NS_LOG_ERROR("Presto currently does not support link count more than 1");
            return 0;
        }
        runMode = PRESTO;
    }
    else if (runModeStr.compare("Weighted-Presto") == 0)
    {
        //必须在不对称环境中使用，否则使用Presto
        if (asymCapacity == false && asymCapacity2 == false)
        {
            NS_LOG_ERROR("The Weighted-Presto has to work with asymmetric topology. For a symmetric topology, please use Presto instead");
            return 0;
        }
        runMode = WEIGHTED_PRESTO;
    }
    else if (runModeStr.compare("DRB") == 0)
    {
        if (LINK_COUNT != 1)
        {
            NS_LOG_ERROR("DRB currently does not support link count more than 1");
            return 0;
        }
        runMode = DRB;
    }
    else if (runModeStr.compare("FlowBender") == 0)
    {
        runMode = FlowBender;
    }
    else if (runModeStr.compare("ECMP") == 0) //使用ECMP来进行选路
    {
        runMode = ECMP;
    }
    else if (runModeStr.compare("TLB") == 0)
    {
        std::cout << Ipv4TLB::GetLogo() << std::endl;
        if (LINK_COUNT != 1)
        {
            NS_LOG_ERROR("TLB currently does not support link count more than 1");
            return 0;
        }
        runMode = TLB;
    }
    else if (runModeStr.compare("Clove") == 0)
    {
        runMode = Clove;
    }
    else if (runModeStr.compare("DRILL") == 0)
    {
        runMode = DRILL;
    }
    else if (runModeStr.compare("LetFlow") == 0)
    {
        runMode = LetFlow;
    }
    else
    {
        NS_LOG_ERROR("The running mode should be TLB, Conga, Conga-flow, Conga-ECMP, Presto, FlowBender, DRB and ECMP");
        return 0;
    }

    if (load < 0.0 || load >= 1.0)
    {
        NS_LOG_ERROR("The network load should within 0.0 and 1.0");
        return 0;
    }

    /*************************************************************************************************************************************/
    if (transportProt.compare("NTcp") != 0 || runMode != ECMP)
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
        if(m_enableCache)
        {
            m_isNtcp = true;
            resequenceBuffer = true;
        }
        else m_cacheFlowRto = 5;
    }


    if (cdfFileName[0] == 'D')
        m_cdfType = 0;
    else if (cdfFileName[0] == 'V')
        m_cdfType = 1;
    else if (cdfFileName[0] == 'L')
        m_cdfType = 2;
    else
    {
        std::cout << "CDF File ERROR!\n";
        return 0;
    }

    if (resequenceBuffer)
    {
        NS_LOG_INFO("Enabling Resequence Buffer");
        Config::SetDefault("ns3::TcpSocketBase::ResequenceBuffer", BooleanValue(true));
        Config::SetDefault("ns3::TcpResequenceBuffer::InOrderQueueTimerLimit", TimeValue(MicroSeconds(resequenceInOrderTimer)));
        Config::SetDefault("ns3::TcpResequenceBuffer::SizeLimit", UintegerValue(resequenceInOrderSize));
        Config::SetDefault("ns3::TcpResequenceBuffer::OutOrderQueueTimerLimit", TimeValue(MicroSeconds(resequenceOutOrderTimer)));
    }


    if (tcpPause)
    {
        NS_LOG_INFO("Enabling TCP pause");
        Config::SetDefault("ns3::TcpSocketBase::Pause", BooleanValue(true));
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
    if (enableFastReConnection)
    {
        Config::SetDefault("ns3::TcpSocket::ConnTimeout", TimeValue(MicroSeconds(40)));
    }
    else
    {
        Config::SetDefault("ns3::TcpSocket::ConnTimeout", TimeValue(MilliSeconds(5)));
    }
    //设置快重传的阀值为一个较大的值以掩盖乱序
    if (enableLargeDupAck)
    {
        Config::SetDefault("ns3::TcpSocketBase::ReTxThreshold", UintegerValue(1000));
        //The attribute which manages the number of dup ACKs necessary to start the fast retransmit algorithm is named "ReTxThreshold", and by default is 3.
    }
    //允许重试连接即发送SYN包很多次
    if (enableLargeSynRetries)
    {
        //Number of connection attempts (SYN retransmissions) before returning failure
        Config::SetDefault("ns3::TcpSocket::ConnCount", UintegerValue(10000));
    }
    //允许多次重传的尝试
    if (enableLargeDataRetries)
    {
        // Number of data retransmission attempts
        Config::SetDefault("ns3::TcpSocket::DataRetries", UintegerValue(10000));
    }

    if (runMode == TLB)
    {
        NS_LOG_INFO("Enabling TLB");
        Config::SetDefault("ns3::TcpSocketBase::TLB", BooleanValue(true));
        Config::SetDefault("ns3::TcpSocketBase::TLBReverseACK", BooleanValue(TLBReverseACK));
        Config::SetDefault("ns3::Ipv4TLB::MinRTT", TimeValue(MicroSeconds(TLBMinRTT)));
        Config::SetDefault("ns3::Ipv4TLB::HighRTT", TimeValue(MicroSeconds(TLBHighRTT)));
        Config::SetDefault("ns3::Ipv4TLB::BetterPathRTTThresh", TimeValue(MicroSeconds(TLBBetterPathRTT)));
        Config::SetDefault("ns3::Ipv4TLB::ChangePathPoss", UintegerValue(TLBPoss));
        Config::SetDefault("ns3::Ipv4TLB::T1", TimeValue(MicroSeconds(TLBT1)));
        Config::SetDefault("ns3::Ipv4TLB::ECNPortionLow", DoubleValue(TLBECNPortionLow));
        Config::SetDefault("ns3::Ipv4TLB::RunMode", UintegerValue(TLBRunMode));
        Config::SetDefault("ns3::Ipv4TLBProbing::ProbeInterval", TimeValue(MicroSeconds(TLBProbingInterval)));
        Config::SetDefault("ns3::Ipv4TLB::IsSmooth", BooleanValue(TLBSmooth));
        Config::SetDefault("ns3::Ipv4TLB::Rerouting", BooleanValue(TLBRerouting));
        Config::SetDefault("ns3::Ipv4TLB::DREMultiply", UintegerValue(TLBDREMultiply));
        Config::SetDefault("ns3::Ipv4TLB::S", UintegerValue(TLBS));
        Config::SetDefault("ns3::Ipv4TLB::QuantifyRttBase", TimeValue(MicroSeconds(quantifyRTTBase)));
        Config::SetDefault("ns3::Ipv4TLB::FlowletTimeout", TimeValue(MicroSeconds(TLBFlowletTimeout)));
    }

    if (runMode == Clove)
    {
        NS_LOG_INFO("Enabling Clove");
        Config::SetDefault("ns3::TcpSocketBase::Clove", BooleanValue(true));
        Config::SetDefault("ns3::Ipv4Clove::FlowletTimeout", TimeValue(MicroSeconds(cloveFlowletTimeout)));
        Config::SetDefault("ns3::Ipv4Clove::RunMode", UintegerValue(cloveRunMode));
        Config::SetDefault("ns3::Ipv4Clove::HalfRTT", TimeValue(MicroSeconds(cloveHalfRTT)));
        Config::SetDefault("ns3::Ipv4Clove::DisToUncongestedPath", BooleanValue(cloveDisToUncongestedPath));
    }

    /*************************************************************************************************************************************/
    
    //创建节点
    NodeContainer spines;
    spines.Create(SPINE_COUNT);
    NodeContainer leaves;
    leaves.Create(LEAF_COUNT);
    NodeContainer servers;
    servers.Create(PER_LEAF_SERVER_COUNT * LEAF_COUNT);
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
        InstallCache(spines, std::string("spine"));
        InstallCache(leaves, std::string("leave"));
        InstallCache(servers, std::string("server"));
    }

    /*************************************************************************************************************************************/
    ///给节点安装协议栈

    NS_LOG_INFO("Install Internet stacks");
    InternetStackHelper internet;

    //RoutingHelper用于InternetStackHelper中的SetRoutingHelper()函数。
    //静态路由
    Ipv4StaticRoutingHelper staticRoutingHelper;
    //Performs pre-simulation static route computation on a layer-3 IPv4 topology
    //Ipv4GlobalRoutingHelper会在仿真之前对静态路由表进行计算
    Ipv4GlobalRoutingHelper globalRoutingHelper;
    //将多个RoutingHelper添加到一个列表，并赋以优先级，逐个咨询直到可以处理包后
    Ipv4ListRoutingHelper listRoutingHelper;

    Ipv4CongaRoutingHelper congaRoutingHelper;
    Ipv4XPathRoutingHelper xpathRoutingHelper;
    Ipv4DrbRoutingHelper drbRoutingHelper;
    Ipv4DrillRoutingHelper drillRoutingHelper;
    Ipv4LetFlowRoutingHelper letFlowRoutingHelper;

    if (runMode == CONGA || runMode == CONGA_FLOW || runMode == CONGA_ECMP)
    {
        //给服务器添加staticRoutingHelper,
        internet.SetRoutingHelper(staticRoutingHelper);
        internet.Install(servers);
        //给叶节点与骨干结点添加CongaRoutingHelper
        internet.SetRoutingHelper(congaRoutingHelper);
        internet.Install(spines);
        internet.Install(leaves);
    }
    else if (runMode == PRESTO || runMode == DRB || runMode == WEIGHTED_PRESTO)
    {
        if (runMode == DRB)
        {
            //DRB是Per dest模式
            Config::SetDefault("ns3::Ipv4DrbRouting::Mode", UintegerValue(0)); // Per dest
        }
        else
        {
            //Presto是Per flow模式
            Config::SetDefault("ns3::Ipv4DrbRouting::Mode", UintegerValue(1)); // Per flow
        }
        //数字是优先级，越高表示优先级越高
        //服务器优先使用DrbRouting，再使用global
        //在实现中DrbRouting并不进行路由，而是依赖global与xpath进行路由
        listRoutingHelper.Add(drbRoutingHelper, 1);
        listRoutingHelper.Add(globalRoutingHelper, 0);
        internet.SetRoutingHelper(listRoutingHelper);
        internet.Install(servers);

        //骨干结点与叶结点优先使用xpathRouting，然后使用global
        listRoutingHelper.Clear();
        listRoutingHelper.Add(xpathRoutingHelper, 1);
        listRoutingHelper.Add(globalRoutingHelper, 0);
        internet.SetRoutingHelper(listRoutingHelper);
        internet.Install(spines);
        internet.Install(leaves);
    }
    else if (runMode == Clove || runMode == TLB)
    {
        if (runMode == Clove)
        {
            //设置Clove模式
            internet.SetClove(true);
            internet.Install(servers);
            internet.SetClove(false);
        }
        else
        {
            //设置TLB模式
            internet.SetTLB(true);
            internet.Install(servers);
            internet.SetTLB(false);
        }

        //骨干结点与叶结点优先使用xpathRouting，然后使用global
        listRoutingHelper.Add(xpathRoutingHelper, 1);
        listRoutingHelper.Add(globalRoutingHelper, 0);
        internet.SetRoutingHelper(listRoutingHelper);
        Config::SetDefault("ns3::Ipv4GlobalRouting::PerflowEcmpRouting", BooleanValue(true));

        internet.Install(spines);
        internet.Install(leaves);
    }
    else if (runMode == DRILL || runMode == LetFlow)
    {
        //给服务器添加staticRoutingHelper,
        internet.SetRoutingHelper(staticRoutingHelper);
        internet.Install(servers);
        //给叶结点与骨干结点添加drillRoutingHelper,
        if (runMode == DRILL)
        {
            internet.SetRoutingHelper(drillRoutingHelper);
        }
        else
        {
            internet.SetRoutingHelper(letFlowRoutingHelper);
        }
        internet.Install(spines);
        internet.Install(leaves);
    }
    else if (runMode == ECMP || runMode == FlowBender)
    {
        if (runMode == FlowBender)
        {
            NS_LOG_INFO("Enabling Flow Bender");
            //FlowBender只运行DCTCP协议
            if (transportProt.compare("DcTcp") != 0)
            {
                NS_LOG_ERROR("FlowBender has to be working with DCTCP");
                return 0;
            }
            //开启FlowBender并且设置ECN的比例与FlowBender重路由前的忍耐次数
            Config::SetDefault("ns3::TcpSocketBase::FlowBender", BooleanValue(true));
            Config::SetDefault("ns3::TcpFlowBender::T", DoubleValue(flowBenderT));
            Config::SetDefault("ns3::TcpFlowBender::N", UintegerValue(flowBenderN));
        }
        //在ECMP模式下给所有的节点都添加全局路由
        Config::SetDefault("ns3::Ipv4GlobalRouting::PerflowEcmpRouting", BooleanValue(true));
        internet.SetRoutingHelper(globalRoutingHelper);
         /***********************************************Added***************************************/
        if (enableMFQ)
        {
            Config::SetDefault("ns3::Ipv4GlobalRouting::MFQ", BooleanValue(true));
        }
        /***********************************************Added***************************************/

        internet.Install(servers);
        internet.Install(spines);
        internet.Install(leaves);
    }

    /*************************************************************************************************************************************/
    //设置链路

    NS_LOG_INFO("Install channels and assign addresses");

    //用来创建一系列的QueueDis对象并将它们映射到对应的设备上。这种映射存储在Traffic Control层。
    //TC层处于NetDevices与协议层之间，负责对包的一些处理，如调度，丢包。上层遵循QueueDis规则填充队列下层，
    //下层遵循QueueDis Dequeue发包。
    TrafficControlHelper tc;
    TrafficControlHelper tc2;
    //RED机制仅在DCTCP中起作用

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
        tc2.SetRootQueueDisc("ns3::RedQueueDisc", "MinTh", DoubleValue(250),
                            "MaxTh", DoubleValue(250));
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

    NS_LOG_INFO("Configuring servers");
    // 设置服务器与叶结点间的链路速率与时延
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", DataRateValue(DataRate(LEAF_SERVER_CAPACITY)));
    p2p.SetChannelAttribute("Delay", TimeValue(LINK_LATENCY));

    if (transportProt.compare("Tcp") == 0)
    {
        // 这里是设置NetDevice层的队列，
        //每个点对点结点必须有一个队列来传包，这里是为Link设置一个队列，使得当这个Link连接到节点时自动创建
        p2p.SetQueue("ns3::DropTailQueue", "MaxPackets", UintegerValue(BUFFER_SIZE + 10));
    }
    else
    {
        p2p.SetQueue("ns3::DropTailQueue", "MaxPackets", UintegerValue(10));
    }

    /*************************************************************************************************************************************/
    //设置服务器与叶结点之间的IP地址并且配置路由表

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.0.0", "255.255.255.0");

    // 用于存储叶结点的IPV4地址的向量
    std::vector<Ipv4Address> leafNetworks(LEAF_COUNT);
    // 用于存储每个服务器IP地址的向量
    std::vector<Ipv4Address> serverAddresses(PER_LEAF_SERVER_COUNT * LEAF_COUNT);
    // 用于存储路径的向量。<A,B>表示从A->B要到哪个口。
    std::map<std::pair<int, int>, uint32_t> leafToSpinePath;
    std::map<std::pair<int, int>, uint32_t> spineToLeafPath;

    //用于TLB算法中，//TODO
    std::vector<Ptr<Ipv4TLBProbing>> probings(PER_LEAF_SERVER_COUNT * LEAF_COUNT);

    //配置所有叶结点与服务器的地址
    for (int i = 0; i < LEAF_COUNT; i++)
    {
        // 增加网络号，并且将网络地址设置为SetBase中提供的最初的地址。比如10.0.1.0->10.0.2.0，
        //SetBase的第三个参数是基地址，如0.0.0.1则每次都会从1分配，增加网络号后会再次从1开始分配
        Ipv4Address network = ipv4.NewNetwork();
        // 将得到的网络号存储到叶结点网络中
        leafNetworks[i] = network;

        //配置好叶结点i与其所有服务器的路由表
        for (int j = 0; j < PER_LEAF_SERVER_COUNT; j++)
        {
            int serverIndex = i * PER_LEAF_SERVER_COUNT + j;
            //创建一个NodeContainer并且以叶结点，服务器的顺序放置。即从上向下。
            NodeContainer nodeContainer = NodeContainer(leaves.Get(i), servers.Get(serverIndex));
            //给叶结点与服务器间安装NetDevice,即链路。
            NetDeviceContainer netDeviceContainer = p2p.Install(nodeContainer);

            if (transportProt.compare("Tcp") != 0)
            {
                //如果协议是DCTCP，则在NetDevice上再次安装TC。
                NS_LOG_INFO("Install RED Queue for leaf: " << i << " and server: " << j);
                tc.Install(netDeviceContainer);
            }
            //给这一个<叶结点，服务器>对分配网卡及IP地址
            Ipv4InterfaceContainer interfaceContainer = ipv4.Assign(netDeviceContainer);
            //GetIfIndex用于得到网卡接口的index。这里也理解为端口号。
            /*NS_LOG_INFO("Leaf - " << i << " is connected to Server - " << j << " with address "
                                  << interfaceContainer.GetAddress(0) << " <-> " << interfaceContainer.GetAddress(1)
                                  << " with port " << netDeviceContainer.Get(0)->GetIfIndex() << " <-> " << netDeviceContainer.Get(1)->GetIfIndex());//*/

            //存储这个服务器得到的地址
            serverAddresses[serverIndex] = interfaceContainer.GetAddress(1);
            if (transportProt.compare("Tcp") == 0)
            {
                tc.Uninstall(netDeviceContainer);
            }

            if (runMode == CONGA || runMode == CONGA_FLOW || runMode == CONGA_ECMP)
            {
                // All servers just forward the packet to leaf switch
                //GetStaticRouting返回Ipv4StaticRouting，AddNetworkRouteTo向静态路由表加入一条Network路由，即服务器所有的数据都由这个拉端口发出
                staticRoutingHelper.GetStaticRouting(servers.Get(serverIndex)->GetObject<Ipv4>())->AddNetworkRouteTo(Ipv4Address("0.0.0.0"), Ipv4Mask("0.0.0.0"), netDeviceContainer.Get(1)->GetIfIndex());
                // Conga leaf switches forward the packet to the correct servers
                // 向叶结点中的路由表中加入一条路由，即将这个服务器的IP地址都由叶结点的固定端口转发
                congaRoutingHelper.GetCongaRouting(leaves.Get(i)->GetObject<Ipv4>())->AddRoute(interfaceContainer.GetAddress(1), //Add routing table entry if it doesn't yet exist in routing table.
                                                                                               Ipv4Mask("255.255.255.255"), netDeviceContainer.Get(0)->GetIfIndex());
                for (int k = 0; k < LEAF_COUNT; k++)
                {
                    // 对CongaRouting对象添加叶结点的地址到ID的映射
                    congaRoutingHelper.GetCongaRouting(leaves.Get(k)->GetObject<Ipv4>())->AddAddressToLeafIdMap(interfaceContainer.GetAddress(1), i);
                }
            }

            if (runMode == DRILL)
            {
                // All servers just forward the packet to leaf switch
                staticRoutingHelper.GetStaticRouting(servers.Get(serverIndex)->GetObject<Ipv4>())->AddNetworkRouteTo(Ipv4Address("0.0.0.0"), Ipv4Mask("0.0.0.0"), netDeviceContainer.Get(1)->GetIfIndex());

                // DRILL leaf switches forward the packet to the correct servers
                drillRoutingHelper.GetDrillRouting(leaves.Get(i)->GetObject<Ipv4>())->AddRoute(interfaceContainer.GetAddress(1), Ipv4Mask("255.255.255.255"), netDeviceContainer.Get(0)->GetIfIndex());
            }

            if (runMode == LetFlow)
            {
                // All servers just forward the packet to leaf switch
                //得到这个服务器上的静态路由表并添加路由
                staticRoutingHelper.GetStaticRouting(servers.Get(serverIndex)->GetObject<Ipv4>())->AddNetworkRouteTo(Ipv4Address("0.0.0.0"), Ipv4Mask("0.0.0.0"), netDeviceContainer.Get(1)->GetIfIndex());

                Ptr<Ipv4LetFlowRouting> letFlowLeaf = letFlowRoutingHelper.GetLetFlowRouting(leaves.Get(i)->GetObject<Ipv4>());

                // LetFlow leaf switches forward the packet to the correct servers
                letFlowLeaf->AddRoute(interfaceContainer.GetAddress(1),
                                      Ipv4Mask("255.255.255.255"),
                                      netDeviceContainer.Get(0)->GetIfIndex());
                letFlowLeaf->SetFlowletTimeout(MicroSeconds(letFlowFlowletTimeout));
            }

            if (runMode == TLB)
            {
                for (int k = 0; k < PER_LEAF_SERVER_COUNT * LEAF_COUNT; k++)
                {
                    //对所有服务器的添加这个服务器地址到叶结点id的映射，即此服务器与哪个叶结点相连。
                    Ptr<Ipv4TLB> tlb = servers.Get(k)->GetObject<Ipv4TLB>();
                    tlb->AddAddressWithTor(interfaceContainer.GetAddress(1), i);
                    // NS_LOG_INFO ("Configuring TLB with " << k << "'s server, inserting server: " << j << " under leaf: " << i);
                }
            }

            if (runMode == Clove)
            {
                for (int k = 0; k < PER_LEAF_SERVER_COUNT * LEAF_COUNT; k++)
                {
                    //对所有服务器的添加这个服务器地址到叶结点id的映射，即此服务器与哪个叶结点相连。
                    Ptr<Ipv4Clove> clove = servers.Get(k)->GetObject<Ipv4Clove>();
                    clove->AddAddressWithTor(interfaceContainer.GetAddress(1), i);
                }
            }
        }
    }

    /*************************************************************************************************************************************/
    //设置叶结点与骨干节点之间的IP地址

    NS_LOG_INFO("Configuring switches");
    // 设置骨干结点与节点间的速率
    p2p.SetDeviceAttribute("DataRate", DataRateValue(DataRate(SPINE_LEAF_CAPACITY)));

    /*************************************Added***********************************/
    /*  if (enableMFQ && runMode == ECMP)
    {
        for (int j = 0; j < SPINE_COUNT; j++)
        {
            Ptr<Ipv4GlobalRouting> globalSpine = globalRoutingHelper.GetGlobalRouting(spines.Get(j)->GetObject<Ipv4>());
            globalSpine->SetSpineId(j);
            globalSpine->SetTDre(MicroSeconds(30));
            globalSpine->SetAlpha(0.2);
            globalSpine->SetLinkCapacity(DataRate(SPINE_LEAF_CAPACITY));
        }
    }
    /*************************************Added***********************************/
    // 存储非对称路径对
    std::set<std::pair<uint32_t, uint32_t>> asymLink; // set< (A, B) > Leaf A -> Spine B is asymmetric
    //设置骨干结点与叶结点间的连接，不对称，随意丢包和包黑洞都在这里设置，也即都发生在core->leave之间
    for (int i = 0; i < LEAF_COUNT; i++)
    {
        if (runMode == CONGA || runMode == CONGA_FLOW || runMode == CONGA_ECMP)
        {
            // 得到Ipv4CongaRouting类，设置叶结点的id、DRE、Alpha与LinkCapacity.下面有配置core节点的相似过程
            Ptr<Ipv4CongaRouting> congaLeaf = congaRoutingHelper.GetCongaRouting(leaves.Get(i)->GetObject<Ipv4>());
            congaLeaf->SetLeafId(i);
            congaLeaf->SetTDre(MicroSeconds(30));
            congaLeaf->SetAlpha(0.2);
            congaLeaf->SetLinkCapacity(DataRate(SPINE_LEAF_CAPACITY));
            if (runMode == CONGA)
            {
                //设置flowlet中timeout的大小
                congaLeaf->SetFlowletTimeout(MicroSeconds(congaFlowletTimeout));
            }
            if (runMode == CONGA_FLOW)
            {
                //当flowlet时间的间隔很大时，则永远不会超时，变成流粒度。
                congaLeaf->SetFlowletTimeout(MilliSeconds(13));
            }
            if (runMode == CONGA_ECMP)
            {
                // TODO
                congaLeaf->EnableEcmpMode();
            }
        }

        for (int j = 0; j < SPINE_COUNT; j++)
        {

            for (int l = 0; l < LINK_COUNT; l++)
            {
                bool isAsymCapacity = false;

                //如果指定了会不对称，即只有1/10的容量，则按指定的不对称概率来确定是否不对称
                if (asymCapacity && static_cast<uint32_t>(rand() % 100) < asymCapacityPoss)
                {
                    isAsymCapacity = true;
                }
                //asymCapacity2表示是否让<spine0-leave0)之间的link变为不对称
                if (asymCapacity2 && i == 0 && j == 0)
                {
                    isAsymCapacity = true;
                }

                // TODO
                uint64_t spineLeafCapacity_tmp = SPINE_LEAF_CAPACITY;

                // 如果设置了不对称则让它的容量变为原来的1/5，且将不对称的路径插入记录向量中。
                if (isAsymCapacity)
                {
                    spineLeafCapacity_tmp = SPINE_LEAF_CAPACITY / 5;
                    asymLink.insert(std::make_pair(i, j));
                    asymLink.insert(std::make_pair(j, i));
                }

                //设置路径的速率，并增加网络号
                p2p.SetDeviceAttribute("DataRate", DataRateValue(DataRate(spineLeafCapacity_tmp)));
                ipv4.NewNetwork();
                // 创建NodeContainer并按照从下到上的顺序，总之get(0)都是叶结点。
                NodeContainer nodeContainer = NodeContainer(leaves.Get(i), spines.Get(j));
                // 安装链路
                NetDeviceContainer netDeviceContainer = p2p.Install(nodeContainer);

                if (transportProt.compare("Tcp") != 0)
                {
                    if (transportProt.compare("DcTcp") == 0)
                    {
                        NS_LOG_INFO("Install RED Queue for leaf: " << i << " and spine: " << j);
                        if (enableRandomDrop)
                        {
                            //如果开启了任意丢包，则在第0个core结点到叶节点的链路丢包
                            if (j == 0)
                            {
                                Config::SetDefault("ns3::RedQueueDisc::DropRate", DoubleValue(0.0));
                                tc.Install(netDeviceContainer.Get(0)); // Leaf to Spine Queue 叶结点到core节点的队列
                                Config::SetDefault("ns3::RedQueueDisc::DropRate", DoubleValue(randomDropRate));
                                tc.Install(netDeviceContainer.Get(1)); // Spine to Leaf Queue core节点到叶结点的队列
                            }
                            else
                            {
                                //不是第0个，则直接统一设置不丢包即可
                                Config::SetDefault("ns3::RedQueueDisc::DropRate", DoubleValue(0.0));
                                tc.Install(netDeviceContainer);
                            }
                        }
                        else if (blackHoleMode != 0) //如果开启了包黑洞，则在core节点到叶结点的队列处丢包
                        {
                            if (j == 0)
                            {
                                Config::SetDefault("ns3::RedQueueDisc::BlackHole", UintegerValue(0));
                                tc.Install(netDeviceContainer.Get(0)); // Leaf to Spine Queue
                                Config::SetDefault("ns3::RedQueueDisc::BlackHole", UintegerValue(blackHoleMode));
                                tc.Install(netDeviceContainer.Get(1)); // Spine to Leaf Queue
                                Ptr<TrafficControlLayer> tcl = netDeviceContainer.Get(1)->GetNode()->GetObject<TrafficControlLayer>();
                                Ptr<QueueDisc> queueDisc = tcl->GetRootQueueDiscOnDevice(netDeviceContainer.Get(1));
                                Ptr<RedQueueDisc> redQueueDisc = DynamicCast<RedQueueDisc>(queueDisc);
                                //自己添加的函数，设置BlackHole的目的地址或源地址
                                redQueueDisc->SetBlackHoleSrc(blackHoleSrcAddr, blackHoleSrcMask);
                                redQueueDisc->SetBlackHoleDest(blackHoleDestAddr, blackHoleDestMask);
                            }
                            else
                            {
                                //不是第0个core结点时设置不丢包
                                Config::SetDefault("ns3::RedQueueDisc::BlackHole", UintegerValue(0));
                                tc.Install(netDeviceContainer);
                            }
                        }
                        else
                        {
                            //否则直接安装
                            if(spineLeafCapacity == 40) tc2.Install(netDeviceContainer);
                            else tc.Install(netDeviceContainer);
                        }
                    }
                    else
                    {
                        //否则直接安装
                        tc.Install(netDeviceContainer);
                    }
                }

                /***********************************进行地址分配*****************************/
                //对<leave，core>结点对进行地址分配
                Ipv4InterfaceContainer ipv4InterfaceContainer = ipv4.Assign(netDeviceContainer);
                /*NS_LOG_INFO("Leaf - " << i << " is connected to Spine - " << j << " with address "
                                      << ipv4InterfaceContainer.GetAddress(0) << " <-> " << ipv4InterfaceContainer.GetAddress(1)
                                      << " with port " << netDeviceContainer.Get(0)->GetIfIndex() << " <-> " << netDeviceContainer.Get(1)->GetIfIndex()
                                      << " with data rate " << spineLeafCapacity);//*/

                if (runMode == TLB || runMode == DRB || runMode == PRESTO || runMode == WEIGHTED_PRESTO || runMode == Clove)
                {
                    //从叶结点i到骨干节点j，走叶结点i的第netDeviceContainer.Get(0)->GetIfIndex()的端口
                    std::pair<int, int> leafToSpine = std::make_pair(i, j);
                    leafToSpinePath[leafToSpine] = netDeviceContainer.Get(0)->GetIfIndex();
                    //从骨干结点j到叶节点i，走骨干节点j的第netDeviceContainer.Get(1)->GetIfIndex()的端口
                    std::pair<int, int> spineToLeaf = std::make_pair(j, i);
                    spineToLeafPath[spineToLeaf] = netDeviceContainer.Get(1)->GetIfIndex();
                }

                if (transportProt.compare("Tcp") == 0)
                {
                    tc.Uninstall(netDeviceContainer);
                }

                if (runMode == CONGA || runMode == CONGA_FLOW || runMode == CONGA_ECMP)
                {
                    // For each conga leaf switch, routing entry to route the packet to OTHER leaves should be added
                    //配置叶结点的路由表
                    for (int k = 0; k < LEAF_COUNT; k++)
                    {
                        //配置路由表，意思是叶结点i到其它叶结点都可以通过现在这个骨干结点到达，所以可以将包发送这个core节点的端口
                        if (k != i)
                        {
                            congaRoutingHelper.GetCongaRouting(leaves.Get(i)->GetObject<Ipv4>())->AddRoute(leafNetworks[k], Ipv4Mask("255.255.255.0"), netDeviceContainer.Get(0)->GetIfIndex());
                        }
                    }

                    // For each conga spine switch, routing entry to THIS leaf switch should be added
                    // 得到Ipv4CongaRouting类，设置core结点的DRE、Alpha与LinkCapacity. 上面有相似配置leaf结点的过程
                    Ptr<Ipv4CongaRouting> congaSpine = congaRoutingHelper.GetCongaRouting(spines.Get(j)->GetObject<Ipv4>());
                    congaSpine->SetTDre(MicroSeconds(30));
                    congaSpine->SetAlpha(0.2);
                    congaSpine->SetLinkCapacity(DataRate(SPINE_LEAF_CAPACITY));

                    //因为core节点只负责转发，所以不用配置Flowlet
                    if (runMode == CONGA_ECMP)
                    {
                        congaSpine->EnableEcmpMode();
                    }
                    //配置到叶结点i的路由表，即从core节点的这个端口转发即可。
                    congaSpine->AddRoute(leafNetworks[i],
                                         Ipv4Mask("255.255.255.0"),
                                         netDeviceContainer.Get(1)->GetIfIndex());

                    //congaAwareCapacity表示CONGA是否会注意不对称情况
                    if (isAsymCapacity)
                    {
                        //SPINE_LEAF_CAPACITY表示原来设定的链路速率，而spineLeafCapacity表示不对称时的速率
                        //如果CONGA濂注意不对称，则会将总体链路的速率设为spineLeafCapacity。
                        uint64_t congaAwareCapacity = congaAwareAsym ? spineLeafCapacity : SPINE_LEAF_CAPACITY;
                        Ptr<Ipv4CongaRouting> congaLeaf = congaRoutingHelper.GetCongaRouting(leaves.Get(i)->GetObject<Ipv4>());
                        congaLeaf->SetLinkCapacity(netDeviceContainer.Get(0)->GetIfIndex(), DataRate(congaAwareCapacity));
                        NS_LOG_INFO("Reducing Link Capacity of Conga Leaf: " << i << " with port: " << netDeviceContainer.Get(0)->GetIfIndex());
                        congaSpine->SetLinkCapacity(netDeviceContainer.Get(1)->GetIfIndex(), DataRate(congaAwareCapacity));
                        NS_LOG_INFO("Reducing Link Capacity of Conga Spine: " << j << " with port: " << netDeviceContainer.Get(1)->GetIfIndex());
                    }
                }

                if (runMode == DRILL)
                {
                    // For each drill leaf switch, routing entry to route the packet to OTHER leaves should be added
                    ////配置叶结点的路由表
                    for (int k = 0; k < LEAF_COUNT; k++)
                    {
                        if (k != i)
                        {
                            //配置路由表，意思是叶结点i到其它叶结点都可以通过现在这个骨干结点到达，所以可以将包发送这个core节点的端口
                            drillRoutingHelper.GetDrillRouting(leaves.Get(i)->GetObject<Ipv4>())->AddRoute(leafNetworks[k], Ipv4Mask("255.255.255.0"), netDeviceContainer.Get(0)->GetIfIndex());
                        }
                    }

                    // For each drill spine switch, routing entry to THIS leaf switch should be added
                    // 配置到叶结点i的路由表，即从core节点的这个端口转发即可。
                    Ptr<Ipv4DrillRouting> drillSpine = drillRoutingHelper.GetDrillRouting(spines.Get(j)->GetObject<Ipv4>());
                    drillSpine->AddRoute(leafNetworks[i],
                                         Ipv4Mask("255.255.255.0"),
                                         netDeviceContainer.Get(1)->GetIfIndex());
                }

                if (runMode == LetFlow)
                {
                    // For each LetFlow leaf switch, routing entry to route the packet to OTHER leaves should be added
                    for (int k = 0; k < LEAF_COUNT; k++)
                    {
                        if (k != i)
                        {
                            letFlowRoutingHelper.GetLetFlowRouting(leaves.Get(i)->GetObject<Ipv4>())->AddRoute(leafNetworks[k], Ipv4Mask("255.255.255.0"), netDeviceContainer.Get(0)->GetIfIndex());
                        }
                    }

                    // For each LetFlow spine switch, routing entry to THIS leaf switch should be added
                    Ptr<Ipv4LetFlowRouting> letFlowSpine = letFlowRoutingHelper.GetLetFlowRouting(spines.Get(j)->GetObject<Ipv4>());
                    letFlowSpine->AddRoute(leafNetworks[i],
                                           Ipv4Mask("255.255.255.0"),
                                           netDeviceContainer.Get(1)->GetIfIndex());
                    letFlowSpine->SetFlowletTimeout(MicroSeconds(letFlowFlowletTimeout));
                }
            }
        }
    }

    /*************************************************************************************************************************************/
    //构建路由表
    if (runMode == ECMP || runMode == PRESTO || runMode == WEIGHTED_PRESTO || runMode == DRB || runMode == FlowBender || runMode == TLB || runMode == Clove)
    {
        NS_LOG_INFO("Populate global routing tables");
        //Build a routing database and initialize the routing tables of the nodes in the simulation.Makes all nodes in the simulation into routers.
        //构建路由数据库并且在仿真时初始化每个结点的路由表
        Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    }

    //配置显式的路径
    if (runMode == DRB || runMode == PRESTO || runMode == WEIGHTED_PRESTO)
    {
        NS_LOG_INFO("Configuring DRB / PRESTO paths");
        for (int i = 0; i < LEAF_COUNT; i++)
        {
            for (int j = 0; j < PER_LEAF_SERVER_COUNT; j++)
            {
                //得到服务器的下标和服务器上drbRouting
                int serverIndex = i * PER_LEAF_SERVER_COUNT + j;
                Ptr<Ipv4DrbRouting> drbRouting = drbRoutingHelper.GetDrbRouting(servers.Get(serverIndex)->GetObject<Ipv4>());
                //对下标为serverIndex的服务器的drbRouting中添加了从叶结点i到所有骨干结点的路径
                for (int k = 0; k < SPINE_COUNT; k++)
                {
                    //得到这个服务器的Ipv4DrbRouting然后进行操作
                    if (runMode == DRB)
                    {
                        //添加路径，权重为1，per dest模式下权重都为1
                        drbRouting->AddPath(leafToSpinePath[std::make_pair(i, k)]);
                    }
                    else if (runMode == WEIGHTED_PRESTO) //画一个3X3并且per_leaf_ser=1按流程走一遍，即可明白这里与ipv4-drb-routing.cc中的函数
                    {
                        // If the capacity of a uplink is reduced, the weight should be reduced either
                        //检查从这个叶结点i到骨干结点k是否为不对称路径，如果是则添加到服务器serverIndex时添加权重乘0.2
                        if (asymLink.find(std::make_pair(i, k)) != asymLink.end())
                        {
                            drbRouting->AddWeightedPath(PRESTO_RATIO * 0.2, leafToSpinePath[std::make_pair(i, k)]);
                        }
                        else //如果是对称路径
                        {
                            // Check whether the spine down to the leaf is reduced and add the exception
                            // 检查从骨干结点k到叶结点l是否为不对称路径。
                            std::set<Ipv4Address> exclusiveIPs;
                            for (int l = 0; l < LEAF_COUNT; l++)
                            {
                                //如果从骨干结点k到叶结点l为不对称路径
                                if (asymLink.find(std::make_pair(k, l)) != asymLink.end())
                                {
                                    //对于叶结点l下的所有服务器，得到其IP地址，然后将从叶结点i到骨干结点k并且目的地址为叶结点l下的服务器的路径设为不对称路径
                                    //即从叶结点i到叶结点l经过骨干结点k，由于k到l为不对称路径，所以为了保持不拥塞，从i去l且经过k的路径都必须为不对称的
                                    for (int m = l * PER_LEAF_SERVER_COUNT; m < l * PER_LEAF_SERVER_COUNT + PER_LEAF_SERVER_COUNT; m++)
                                    {
                                        Ptr<Node> destServer = servers.Get(m);
                                        Ptr<Ipv4> ipv4 = destServer->GetObject<Ipv4>();
                                        Ipv4InterfaceAddress destInterface = ipv4->GetAddress(1, 0);
                                        Ipv4Address destAddress = destInterface.GetLocal();
                                        drbRouting->AddWeightedPath(destAddress, PRESTO_RATIO * 0.2, leafToSpinePath[std::make_pair(i, k)]);
                                        exclusiveIPs.insert(destAddress);
                                    }
                                }
                            }
                            //了上
                            drbRouting->AddWeightedPath(PRESTO_RATIO, leafToSpinePath[std::make_pair(i, k)], exclusiveIPs);
                        }
                    }
                    else
                    {
                        drbRouting->AddPath(PRESTO_RATIO, leafToSpinePath[std::make_pair(i, k)]);
                    }
                }
            }
        }
    }

    if (runMode == Clove)
    {
        //配置路径即从叶结点i经过骨干结点k到其它叶结点的路径 方式为每两位一个端口组成一个数字
        NS_LOG_INFO("Configuring Clove available paths");
        for (int i = 0; i < LEAF_COUNT; i++)
        {
            for (int j = 0; j < PER_LEAF_SERVER_COUNT; j++)
            {
                int serverIndex = i * PER_LEAF_SERVER_COUNT + j;
                Ptr<Ipv4Clove> clove = servers.Get(serverIndex)->GetObject<Ipv4Clove>();
                for (int k = 0; k < SPINE_COUNT; k++)
                {
                    //从i到k的端口号
                    int path = 0;
                    int pathBase = 1;
                    path += leafToSpinePath[std::make_pair(i, k)] * pathBase;
                    pathBase *= 100;
                    //配置从骨干结点k到其它叶结点的路径
                    for (int l = 0; l < LEAF_COUNT; l++)
                    {
                        if (i == l)
                        {
                            continue;
                        }
                        int newPath = spineToLeafPath[std::make_pair(k, l)] * pathBase + path;
                        clove->AddAvailPath(l, newPath);
                    }
                }
            }
        }
    }

    if (runMode == TLB)
    {
        NS_LOG_INFO("Configuring TLB available paths");
        for (int i = 0; i < LEAF_COUNT; i++)
        {
            for (int j = 0; j < PER_LEAF_SERVER_COUNT; j++)
            {
                int serverIndex = i * PER_LEAF_SERVER_COUNT + j;
                for (int k = 0; k < SPINE_COUNT; k++)
                {
                    int path = 0;
                    int pathBase = 1;
                    path += leafToSpinePath[std::make_pair(i, k)] * pathBase;
                    pathBase *= 100;
                    for (int l = 0; l < LEAF_COUNT; l++)
                    {
                        if (i == l)
                        {
                            continue;
                        }
                        int newPath = spineToLeafPath[std::make_pair(k, l)] * pathBase + path;
                        Ptr<Ipv4TLB> tlb = servers.Get(serverIndex)->GetObject<Ipv4TLB>();
                        tlb->AddAvailPath(l, newPath);
                        //NS_LOG_INFO ("Configuring server: " << serverIndex << " to leaf: " << l << " with path: " << newPath);
                    }
                }
            }
        }

        if (runMode == TLB && TLBProbingEnable)
        {
            NS_LOG_INFO("Configuring TLB Probing");
            for (int i = 0; i < PER_LEAF_SERVER_COUNT * LEAF_COUNT; i++)
            {
                // The i th server under one leaf is used to probe the leaf i by contacting the i th server under that leaf
                Ptr<Ipv4TLBProbing> probing = CreateObject<Ipv4TLBProbing>();
                probings[i] = probing;
                probing->SetNode(servers.Get(i));
                probing->SetSourceAddress(serverAddresses[i]);
                probing->Init();

                int serverIndexUnderLeaf = i % PER_LEAF_SERVER_COUNT;

                if (serverIndexUnderLeaf < LEAF_COUNT)
                {
                    int serverBeingProbed = PER_LEAF_SERVER_COUNT * serverIndexUnderLeaf + serverIndexUnderLeaf;
                    if (serverBeingProbed == i)
                    {
                        continue;
                    }
                    probing->SetProbeAddress(serverAddresses[serverBeingProbed]);
                    //NS_LOG_INFO ("Server: " << i << " is going to probe server: " << serverBeingProbed);
                    int leafIndex = i / PER_LEAF_SERVER_COUNT;
                    for (int j = leafIndex * PER_LEAF_SERVER_COUNT; j < leafIndex * PER_LEAF_SERVER_COUNT + PER_LEAF_SERVER_COUNT; j++)
                    {
                        if (i == j)
                        {
                            continue;
                        }
                        probing->AddBroadCastAddress(serverAddresses[j]);
                        //NS_LOG_INFO ("Server:" << i << " is going to broadcast to server: " << j);
                    }
                    probing->StartProbe();
                    probing->StopProbe(Seconds(END_TIME));
                }
            }
        }
    }

    /*************************************************************************************************************************************/

    //求得oversubRatio之
    double oversubRatio = static_cast<double>(PER_LEAF_SERVER_COUNT * LEAF_SERVER_CAPACITY) / (SPINE_LEAF_CAPACITY * SPINE_COUNT * LINK_COUNT);
    NS_LOG_INFO("Over-subscription ratio: " << oversubRatio);

    NS_LOG_INFO("Initialize CDF table");
    struct cdf_table *cdfTable = new cdf_table();
    init_cdf(cdfTable);
    load_cdf(cdfTable, cdfFileName.c_str());

    NS_LOG_INFO("Calculating request rate");
    double requestRate = load * LEAF_SERVER_CAPACITY * PER_LEAF_SERVER_COUNT / oversubRatio / (8 * avg_cdf(cdfTable)) / PER_LEAF_SERVER_COUNT;
    NS_LOG_INFO("Average request rate: " << requestRate << "Byte per second");

    NS_LOG_INFO("Initialize random seed: " << randomSeed);
    if (randomSeed == 0)
    {
        srand((unsigned)time(NULL));
    }
    else
    {
        srand(randomSeed);
    }

    NS_LOG_INFO("Create applications");

    long flowCount = 0;
    long totalFlowSize = 0;
    long totalCacheableSize = 0;

    for (int srcLeafId = 0; srcLeafId < LEAF_COUNT; srcLeafId++)
    {
        install_applications(srcLeafId, servers, requestRate, cdfTable, flowCount, totalFlowSize, totalCacheableSize, PER_LEAF_SERVER_COUNT, LEAF_COUNT, START_TIME, END_TIME, FLOW_LAUNCH_END_TIME, applicationPauseThresh, applicationPauseTime);
    }

    std::cout << "Total flow: " << flowCount << std::endl;
    if (flowCount)
    {
        std::cout << "Actual average flow size: " << (double)(totalFlowSize) / flowCount / 1e6 << "MB\n";
        std::cout << "\nTotal flow byte is " << totalFlowSize / 1e6 << "MB\nTotal cacheable count is "
                  << totalCacheableSize / 1e6 << "\nCacheable is " << totalCacheableSize * 100 / totalFlowSize << "%\n";
        while(!m_flows.empty())
        {
            FlowInfo fi=m_flows.top();
            m_flows.pop();
            int packetNum=ceil(fi.flowsize/1400.0)+3;
            printf("Flow size %8d %8d %8.4lf\n",fi.flowsize,packetNum,fi.time);
        }//*/
    }

    NS_LOG_INFO("Enabling flow monitor");

    //FlowMonitor用于在仿真期间监视流
    Ptr<FlowMonitor> flowMonitor;
    FlowMonitorHelper flowHelper;
    // 在所有节点上开启流监控
    flowMonitor = flowHelper.InstallAll();

    NS_LOG_INFO("Enabling link monitor");

    //创建一个LinkMonitor的指针，用于传入Ipv4LinkProbe中，输出信息
    Ptr<LinkMonitor> linkMonitor = Create<LinkMonitor>();
    //在每一个骨干交换机上加入Monitor
    for (int i = 0; i < SPINE_COUNT; i++)
    {
        std::stringstream name;
        name << "Spine " << i;
        Ptr<Ipv4LinkProbe> spineLinkProbe = Create<Ipv4LinkProbe>(spines.Get(i), linkMonitor);
        spineLinkProbe->SetProbeName(name.str());                      //设置探测的名称
        spineLinkProbe->SetCheckTime(Seconds(0.01));                   //设置多长时间监测一次链路利用率
        spineLinkProbe->SetDataRateAll(DataRate(SPINE_LEAF_CAPACITY)); //设置容量，用于计算链路利用率
    }
    //基本同上，这次是在叶结点上加入Monitor
    for (int i = 0; i < LEAF_COUNT; i++)
    {
        std::stringstream name;
        name << "Leaf " << i;
        Ptr<Ipv4LinkProbe> leafLinkProbe = Create<Ipv4LinkProbe>(leaves.Get(i), linkMonitor);
        leafLinkProbe->SetProbeName(name.str());
        leafLinkProbe->SetCheckTime(Seconds(0.01));
        leafLinkProbe->SetDataRateAll(DataRate(SPINE_LEAF_CAPACITY));
    }

    linkMonitor->Start(Seconds(START_TIME));
    linkMonitor->Stop(Seconds(END_TIME));

    flowMonitor->CheckForLostPackets();

    /*************************************************************************************************************************************/
    //设置输出的文件名称，第一个是runningID,

    std::stringstream flowMonitorFilename;
    std::stringstream linkMonitorFilename;

    flowMonitorFilename << id << "-1-large-load-" << LEAF_COUNT << "X" << SPINE_COUNT << "-" << load << "-" << transportProt << "-";
    linkMonitorFilename << id << "-1-large-load-" << LEAF_COUNT << "X" << SPINE_COUNT << "-" << load << "-" << transportProt << "-";
    tlbBibleFilename << id << "-1-large-load-" << LEAF_COUNT << "X" << SPINE_COUNT << "-" << load << "-" << transportProt << "-";
    tlbBibleFilename2 << id << "-1-large-load-" << LEAF_COUNT << "X" << SPINE_COUNT << "-" << load << "-" << transportProt << "-";
    rbTraceFilename << id << "-1-large-load-" << LEAF_COUNT << "X" << SPINE_COUNT << "-" << load << "-" << transportProt << "-";

    if (runMode == CONGA)
    {
        flowMonitorFilename << "conga-simulation-" << congaFlowletTimeout << "-";
        linkMonitorFilename << "conga-simulation-" << congaFlowletTimeout << "-";
    }
    else if (runMode == CONGA_FLOW)
    {
        flowMonitorFilename << "conga-flow-simulation-";
        linkMonitorFilename << "conga-flow-simulation-";
    }
    else if (runMode == CONGA_ECMP)
    {
        flowMonitorFilename << "conga-ecmp-simulation-" << congaFlowletTimeout << "-";
        linkMonitorFilename << "conga-ecmp-simulation-" << congaFlowletTimeout << "-";
    }
    else if (runMode == PRESTO)
    {
        flowMonitorFilename << "presto-simulation-";
        linkMonitorFilename << "presto-simulation-";
        rbTraceFilename << "presto-simulation-";
    }
    else if (runMode == WEIGHTED_PRESTO)
    {
        flowMonitorFilename << "weighted-presto-simulation-";
        linkMonitorFilename << "weighted-presto-simulation-";
        rbTraceFilename << "weighted-presto-simulation-";
    }
    else if (runMode == DRB)
    {
        flowMonitorFilename << "drb-simulation-";
        linkMonitorFilename << "drb-simulation-";
        rbTraceFilename << "drb-simulation-";
    }
    else if (runMode == ECMP)
    {
        flowMonitorFilename << "ecmp-simulation-";
        linkMonitorFilename << "ecmp-simulation-";
    }
    else if (runMode == FlowBender)
    {
        flowMonitorFilename << "flow-bender-" << flowBenderT << "-" << flowBenderN << "-simulation-";
        linkMonitorFilename << "flow-bender-" << flowBenderT << "-" << flowBenderN << "-simulation-";
    }
    else if (runMode == TLB)
    {
        flowMonitorFilename << "tlb-" << TLBHighRTT << "-" << TLBMinRTT << "-" << TLBBetterPathRTT << "-" << TLBPoss << "-" << TLBS << "-" << TLBT1 << "-" << TLBProbingInterval << "-" << TLBSmooth << "-" << TLBRerouting << "-" << quantifyRTTBase << "-";
        linkMonitorFilename << "tlb-" << TLBHighRTT << "-" << TLBMinRTT << "-" << TLBBetterPathRTT << "-" << TLBPoss << "-" << TLBS << "-" << TLBT1 << "-" << TLBProbingInterval << "-" << TLBSmooth << "-" << TLBRerouting << "-" << quantifyRTTBase << "-";
        tlbBibleFilename << "tlb-" << TLBHighRTT << "-" << TLBMinRTT << "-" << TLBBetterPathRTT << "-" << TLBPoss << "-" << TLBS << "-" << TLBT1 << "-" << TLBProbingInterval << "-" << TLBSmooth << "-" << TLBRerouting << "-" << quantifyRTTBase << "-";
        tlbBibleFilename2 << "tlb-" << TLBHighRTT << "-" << TLBMinRTT << "-" << TLBBetterPathRTT << "-" << TLBPoss << "-" << TLBS << "-" << TLBT1 << "-" << TLBProbingInterval << "-" << TLBSmooth << "-" << TLBRerouting << "-" << quantifyRTTBase << "-";
    }
    else if (runMode == Clove)
    {
        flowMonitorFilename << "clove-" << cloveRunMode << "-" << cloveFlowletTimeout << "-" << cloveHalfRTT << "-" << cloveDisToUncongestedPath << "-";
        linkMonitorFilename << "clove-" << cloveRunMode << "-" << cloveFlowletTimeout << "-" << cloveHalfRTT << "-" << cloveDisToUncongestedPath << "-";
    }
    else if (runMode == DRILL)
    {
        flowMonitorFilename << "drill-simulation-";
        linkMonitorFilename << "drill-simulation-";
    }
    else if (runMode == LetFlow)
    {
        flowMonitorFilename << "letflow-simulation-" << letFlowFlowletTimeout << "-";
        linkMonitorFilename << "letflow-simulation-" << letFlowFlowletTimeout << "-";
    }

    flowMonitorFilename << randomSeed << "-";
    linkMonitorFilename << randomSeed << "-";
    tlbBibleFilename << randomSeed << "-";
    tlbBibleFilename2 << randomSeed << "-";
    rbTraceFilename << randomSeed << "-";

    if (asymCapacity)
    {
        flowMonitorFilename << "capacity-asym-";
        linkMonitorFilename << "capacity-asym-";
        tlbBibleFilename << "capacity-asym-";
        tlbBibleFilename2 << "capacity-asym-";
    }

    if (asymCapacity2)
    {
        flowMonitorFilename << "capacity-asym2-";
        linkMonitorFilename << "capacity-asym2-";
        tlbBibleFilename << "capacity-asym2-";
        tlbBibleFilename2 << "capacity-asym2-";
    }

    if (resequenceBuffer)
    {
        flowMonitorFilename << "rb-" << resequenceInOrderSize << "-" << resequenceInOrderTimer << "-" << resequenceOutOrderTimer;
        linkMonitorFilename << "rb-" << resequenceInOrderSize << "-" << resequenceInOrderTimer << "-" << resequenceOutOrderTimer;
        rbTraceFilename << "rb-";
    }

    if (applicationPauseThresh > 0)
    {
        flowMonitorFilename << "p" << applicationPauseThresh << "-" << applicationPauseTime << "-";
        linkMonitorFilename << "p" << applicationPauseThresh << "-" << applicationPauseTime << "-";
        tlbBibleFilename << "p" << applicationPauseThresh << "-" << applicationPauseTime << "-";
        tlbBibleFilename2 << "p" << applicationPauseThresh << "-" << applicationPauseTime << "-";
    }

    if (enableRandomDrop)
    {
        flowMonitorFilename << "random-drop-" << randomDropRate << "-";
        linkMonitorFilename << "random-drop-" << randomDropRate << "-";
        tlbBibleFilename << "random-drop-" << randomDropRate << "-";
        tlbBibleFilename2 << "random-drop-" << randomDropRate << "-";
        rbTraceFilename << "random-drop-" << randomDropRate << "-";
    }

    if (blackHoleMode != 0)
    {
        flowMonitorFilename << "black-hole-" << blackHoleMode << "-";
        linkMonitorFilename << "black-hole-" << blackHoleMode << "-";
        tlbBibleFilename << "black-hole-" << blackHoleMode << "-";
        tlbBibleFilename2 << "black-hole-" << blackHoleMode << "-";
        rbTraceFilename << "black-hole-" << blackHoleMode << "-";
    }

    flowMonitorFilename << "b" << BUFFER_SIZE << ".xml";
    linkMonitorFilename << "b" << BUFFER_SIZE << "-link-utility.out";
    tlbBibleFilename << "b" << BUFFER_SIZE << "-bible.txt";
    tlbBibleFilename2 << "b" << BUFFER_SIZE << "-piple.txt";
    rbTraceFilename << "b" << BUFFER_SIZE << "-RBTrace.txt";

    if (runMode == TLB)
    {
        NS_LOG_INFO("Enabling TLB tracing");
        remove(tlbBibleFilename.str().c_str());
        remove(tlbBibleFilename2.str().c_str());

        Config::ConnectWithoutContext("/NodeList/*/$ns3::Ipv4TLB/SelectPath",
                                      MakeCallback(&TLBPathSelectTrace));

        Config::ConnectWithoutContext("/NodeList/*/$ns3::Ipv4TLB/ChangePath",
                                      MakeCallback(&TLBPathChangeTrace));
        std::ofstream out(tlbBibleFilename.str().c_str(), std::ios::out | std::ios::app);
        out << Ipv4TLB::GetLogo();
        std::ofstream out2(tlbBibleFilename2.str().c_str(), std::ios::out | std::ios::app);
        out2 << Ipv4TLB::GetLogo();
    }
    // resequenceBuffer默认是false
    if (resequenceBuffer && resequenceBufferLog)
    {
        remove(rbTraceFilename.str().c_str());
        Simulator::Schedule(Seconds(START_TIME) + MicroSeconds(1), &RBTrace);
    }

    NS_LOG_INFO("Start simulation");
    Simulator::Stop(Seconds(END_TIME));
    Simulator::Run();

    //输出内容至设定好文件名称中
    flowMonitor->SerializeToXmlFile(flowMonitorFilename.str(), true, true);
    linkMonitor->OutputToFile(linkMonitorFilename.str(), &LinkMonitor::DefaultFormat);
    int flowIdSize = (flowMonitor->GetFlowStats()).size();
    if (flowCount != flowIdSize)
    {
        printf("统计数与设置数不符\n");
    }
    std::stringstream doubleToStr;
    doubleToStr << "./xml " << flowMonitorFilename.str().c_str() << " " << load << " " << id << " ";
    if (transportProt == "Tcp")
        doubleToStr << 0;
    else
        doubleToStr << 1;
    system(doubleToStr.str().c_str());

    Simulator::Destroy();
    free_cdf(cdfTable);
    NS_LOG_INFO("Stop simulation");
}
