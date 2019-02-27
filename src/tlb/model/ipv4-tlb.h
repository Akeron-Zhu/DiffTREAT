/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef IPV4_TLB_H
#define IPV4_TLB_H


#include "ns3/object.h"
#include "ns3/callback.h"
#include "ns3/traced-value.h"
#include "ns3/ipv4-address.h"
#include "ns3/data-rate.h"
#include "ns3/event-id.h"
#include "tlb-flow-info.h"
#include "tlb-path-info.h"

#include <vector>
#include <map>
#include <utility>
#include <string>

#define TLB_RUNMODE_COUNTER 0
#define TLB_RUNMODE_MINRTT 1
#define TLB_RUNMODE_RANDOM 2
#define TLB_RUNMODE_RTT_COUNTER 11
#define TLB_RUNMODE_RTT_DRE 12

namespace ns3 {

//路径分类
enum PathType {
    GoodPath,
    GreyPath,
    BadPath,
    FailPath,
};
//路径信息
struct PathInfo {
    uint32_t pathId; //路径ID
    PathType pathType; //路径类型
    Time rttMin; //这条路径上最小的RTT
    uint32_t size; //是
    double ecnPortion;
    uint32_t counter;
    uint32_t quantifiedDre;
};

//记录pathId与激活时间
struct TLBAcklet {
    uint32_t pathId; //路径ID
    Time activeTime; //上次活动的时间 
};

class Node;

class Ipv4TLB : public Object
{

public:

    Ipv4TLB ();

    Ipv4TLB (const Ipv4TLB&);

    static TypeId GetTypeId (void);

    void AddAddressWithTor (Ipv4Address address, uint32_t torId);

    void AddAvailPath (uint32_t destTor, uint32_t path);

    std::vector<uint32_t> GetAvailPath (Ipv4Address daddr);

    // These methods are used for TCP flows
    uint32_t GetPath (uint32_t flowId, Ipv4Address saddr, Ipv4Address daddr);

    uint32_t GetAckPath (uint32_t flowId, Ipv4Address saddr, Ipv4Address daddr);

    Time GetPauseTime (uint32_t flowId);

    void FlowRecv (uint32_t flowId, uint32_t path, Ipv4Address daddr, uint32_t size, bool withECN, Time rtt);

    void FlowSend (uint32_t flowId, Ipv4Address daddr, uint32_t path, uint32_t size, bool isRetrasmission);

    void FlowTimeout (uint32_t flowId, Ipv4Address daddr, uint32_t path);

    void FlowFinish (uint32_t flowId, Ipv4Address daddr);

    // These methods are used in probing
    void ProbeSend (Ipv4Address daddr, uint32_t path);

    void ProbeRecv (uint32_t path, Ipv4Address daddr, uint32_t size, bool withECN, Time rtt);

    void ProbeTimeout (uint32_t path, Ipv4Address daddr);

    // Node
    void SetNode (Ptr<Node> node);

    static std::string GetPathType (PathType type);

    static std::string GetLogo (void);

private:

    void PacketReceive (uint32_t flowId, uint32_t path, uint32_t destTorId,
                        uint32_t size, bool withECN, Time rtt, bool isProbing);

    bool UpdateFlowInfo (uint32_t flowId, uint32_t path, uint32_t size, bool withECN, Time rtt);

    TLBPathInfo GetInitPathInfo (uint32_t path);

    void UpdatePathInfo (uint32_t destTor, uint32_t path, uint32_t size, bool withECN, Time rtt);

    bool TimeoutFlow (uint32_t flowId, uint32_t path, bool &isVeryTimeout);

    void TimeoutPath (uint32_t destTor, uint32_t path, bool isProbing, bool isVeryTimeout);

    bool SendFlow (uint32_t flowId, uint32_t path, uint32_t size);

    void SendPath (uint32_t destTor, uint32_t path, uint32_t size);

    bool RetransFlow (uint32_t flowId, uint32_t path, uint32_t size, bool &needRetranPath, bool &needHighRetransPath);

    void RetransPath (uint32_t destTor, uint32_t path, bool needHighRetransPath);

    void UpdateFlowPath (uint32_t flowId, uint32_t path, uint32_t destTor);

    void AssignFlowToPath (uint32_t flowId, uint32_t destTor, uint32_t path);

    void RemoveFlowFromPath (uint32_t flowId, uint32_t destTor, uint32_t path);

    bool WhereToChange (uint32_t destTor, struct PathInfo &newPath, bool hasOldPath, uint32_t oldPath);

    struct PathInfo SelectRandomPath (uint32_t destTor);

    struct PathInfo JudgePath (uint32_t destTor, uint32_t path);

    bool PathLIsBetterR (struct PathInfo pathL, struct PathInfo pathR);

    bool FindTorId (Ipv4Address daddr, uint32_t &destTorId);

    void PathAging (void);

    void DreAging (void);

    std::vector<PathInfo> GatherParallelPaths (uint32_t destTor);

    uint32_t QuantifyRtt (Time rtt);
    uint32_t QuantifyDre (uint32_t dre);

    // Parameters
    // Running Mode 0 for minimize counter, 1 for minimize RTT, 2 for random
    uint32_t m_runMode; 
    //重路由是否开启
    bool m_rerouteEnable;
    //一个流已经发送的量，用于判断是否要重路由
    uint32_t m_S;

    Time m_T;

    uint32_t m_K;
    //路径老化时间，即更新路径信息的频率
    Time m_T1;
    //重传超时等老化的时间
    Time m_T2;
    //老化路径事件运行的周期
    Time m_agingCheckTime;
    //评估DRE事件运行的周期
    Time m_dreTime;
    //算法DRE中alph参数
    double m_dreAlpha;
    //数据速率，注意与链路容量不同
    DataRate m_dreDataRate;
    //用于量化DRE的bit数
    uint32_t m_dreQ;

    uint32_t m_dreMultiply;
    //最小的RTT用于判断好的路径
    Time m_minRtt;
    //最大的RTT阀值用于判断坏路径
    Time m_highRtt;
    //路径的数量量必须大于这个量才会进行ECN采样，否则不进行，也就不考虑ECN
    uint32_t m_ecnSampleMin;
    //ECN比例被认为是好路径
    double m_ecnPortionLow;
    //ECN比例被认为是坏路径
    double m_ecnPortionHigh;

    // Failure Related Configurations
    //TLB是否对失败响应
    bool m_respondToFailure;
    //被认为重传多次的阀值
    uint32_t m_flowRetransHigh;
    //被认为很多次重传的阀值
    uint32_t m_flowRetransVeryHigh;
    //被认为经过多次timemout的次数
    uint32_t m_flowTimeoutCount;
    // End of Failure Related Configurations

    double m_betterPathEcnThresh;
    //RTT阀值，用于判断一条路径是否比其它路径好
    Time m_betterPathRttThresh;
    //改变路径即重路由的概率用于防止同时选择一条路
    uint32_t m_pathChangePoss;
    
    //多长时间没收到包则认为这条流已经结束
    Time m_flowDieTime;
    //是否平滑RTT计算
    bool m_isSmooth;
    //计入多少最新的采样值
    uint32_t m_smoothAlpha;
    uint32_t m_smoothDesired;
    uint32_t m_smoothBeta1;
    uint32_t m_smoothBeta2;
    //量化RTT到不同量级的基准
    Time m_quantifyRttBase;
    //Ack flowlet timeout
    Time m_ackletTimeout;

    // Added at Jan 11st
    /*
    double m_epDefaultEcnPortion;
    double m_epAlpha;
    Time m_epCheckTime;
    Time m_epAgingTime;
    */
    // --

    // Added at Jan 12nd
    //flowlet timeout
    Time m_flowletTimeout;
    // --
    //rtt的权重
    double m_rttAlpha;
    //ecn的权重
    double m_ecnBeta;

    // Variables
    std::map<uint32_t, TLBFlowInfo> m_flowInfo; /* <FlowId, TLBFlowInfo> */
    std::map<std::pair<uint32_t, uint32_t>, TLBPathInfo> m_pathInfo; /* <DestTorId, PathId>, TLBPathInfo> */

    std::map<uint32_t, TLBAcklet> m_acklets; /* <FlowId, TLBAcklet> */
    // 服务器地址到与其相连的ToRId的映射
    std::map<Ipv4Address, uint32_t> m_ipTorMap; /* <DestAddress, DestTorId> */
    // 可以到达某个地址的路径
    std::map<uint32_t, std::vector<uint32_t> > m_availablePath; /* <DestTorId, List<PathId>> */

    std::map<uint32_t, Ipv4Address> m_probingAgent; /* <DestTorId, ProbingAgentAddress>*/

    EventId m_agingEvent;

    EventId m_dreEvent;

    Ptr<Node> m_node;

    std::map<uint32_t, Time> m_pauseTime; // Used in the TCP pause, not mandatory

    typedef void (* TLBPathCallback) (uint32_t flowId, uint32_t fromTor,
            uint32_t toTor, uint32_t path, bool isRandom, PathInfo info, std::vector<PathInfo> parallelPaths);

    typedef void (* TLBPathChangeCallback) (uint32_t flowId, uint32_t fromTor, uint32_t toTor,
            uint32_t newPath, uint32_t oldPath, bool isRandom, std::vector<PathInfo> parallelPaths);

    TracedCallback <uint32_t, uint32_t, uint32_t, uint32_t, bool, PathInfo, std::vector<PathInfo> > m_pathSelectTrace;

    TracedCallback <uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, bool, std::vector<PathInfo> > m_pathChangeTrace;


};

}

#endif /* TLB_H */

