/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "ipv4-queue-probe.h"

#include "ns3/log.h"

#include "ipv4-link-probe.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("Ipv4QueueProbe");

NS_OBJECT_ENSURE_REGISTERED (Ipv4QueueProbe);

//返回TypeId
TypeId
Ipv4QueueProbe::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::Ipv4QueueProbe")
            .SetParent<Object> ()
            .SetGroupName ("LinkMonitor")
            .AddConstructor<Ipv4QueueProbe> ();

  return tid;
}

//初始化时开启Log功能
Ipv4QueueProbe::Ipv4QueueProbe ()
{
  NS_LOG_FUNCTION (this);
}

//设置要监视的链路
void
Ipv4QueueProbe::SetIpv4LinkProbe (Ptr<Ipv4LinkProbe> linkProbe)
{
    m_ipv4LinkProbe = linkProbe;
}

//设置要监视的接口ID
void
Ipv4QueueProbe::SetInterfaceId (uint32_t interfaceId)
{
  m_interfaceId = interfaceId;
}

//调用Ipv4LinkProbe中的DequeueLogger，注意在调用时加入了m_interfaceId参数
//下面多个函数同理，这里列出的应试是标准的CallbackSignature，通过这里调用其它的实现传输不同的参数
void
Ipv4QueueProbe::DequeueLogger (Ptr<const Packet> packet)
{
  NS_LOG_FUNCTION (this);
  m_ipv4LinkProbe->DequeueLogger (packet, m_interfaceId);
}

//调用Ipv4LinkProbe中的PacketsInQueueLogger
void
Ipv4QueueProbe::PacketsInQueueLogger (uint32_t oldValue, uint32_t newValue)
{
  NS_LOG_FUNCTION (this);
  m_ipv4LinkProbe->PacketsInQueueLogger (newValue, m_interfaceId);
}

//调用Ipv4LinkProbe中的BytesInQueueLogger
void
Ipv4QueueProbe::BytesInQueueLogger (uint32_t oldValue, uint32_t newValue)
{
  NS_LOG_FUNCTION (this);
  m_ipv4LinkProbe->BytesInQueueLogger (newValue, m_interfaceId);
}

//调用Ipv4LinkProbe中的PacketsInQueueDiscLogger
void
Ipv4QueueProbe::PacketsInQueueDiscLogger (uint32_t oldValue, uint32_t newValue)
{
  NS_LOG_FUNCTION (this);
  m_ipv4LinkProbe->PacketsInQueueDiscLogger (newValue, m_interfaceId);
}

//调用Ipv4LinkProbe中的BytesInQueueDiscLogger
void
Ipv4QueueProbe::BytesInQueueDiscLogger (uint32_t oldValue, uint32_t newValue)
{
  NS_LOG_FUNCTION (this);
  m_ipv4LinkProbe->BytesInQueueDiscLogger (newValue, m_interfaceId);
}

}
