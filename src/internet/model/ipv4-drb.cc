#include "ns3/log.h"
#include "ipv4-drb.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("Ipv4Drb");

NS_OBJECT_ENSURE_REGISTERED (Ipv4Drb);

TypeId
Ipv4Drb::GetTypeId (void)
{
  static TypeId tid = TypeId("ns3::Ipv4Drb")
      .SetParent<Object>()
      .SetGroupName ("Internet")
      .AddConstructor<Ipv4Drb> ();

  return tid;
}

Ipv4Drb::Ipv4Drb ()
{
  NS_LOG_FUNCTION (this);
}

Ipv4Drb::~Ipv4Drb ()
{
  NS_LOG_FUNCTION (this);
}

//一个流的包向不同的core路由器轮询
Ipv4Address
Ipv4Drb::GetCoreSwitchAddress (uint32_t flowId)
{
  NS_LOG_FUNCTION (this);
  //得到m_coreSwitchAddressList的size
  uint32_t listSize = m_coreSwitchAddressList.size();
  //如果为0，则返回一个新地址
  if (listSize == 0)
  {
    return Ipv4Address ();
  }
  
  //得到一个随机数
  uint32_t index = rand () % listSize;
  //如果找得到就用这个，找不到就用随机数得到的
  std::map<uint32_t, uint32_t>::iterator itr = m_indexMap.find (flowId);

  if (itr != m_indexMap.end ())
  {
    index = itr->second;
  }
  //并且将下次使用的加1,一个流的包向不同的core路由器轮询
  m_indexMap[flowId] = ((index + 1) % listSize);

  //返回IP地址
  Ipv4Address addr = m_coreSwitchAddressList[index];

  NS_LOG_DEBUG (this << " The index for flow: " << flowId << " is : " << index);
  return addr;
}

void
Ipv4Drb::AddCoreSwitchAddress (Ipv4Address addr)
{
  NS_LOG_FUNCTION (this << addr);
  m_coreSwitchAddressList.push_back (addr);
}

void
Ipv4Drb::AddCoreSwitchAddress (uint32_t k, Ipv4Address addr)
{
  for (uint32_t i = 0; i < k; i++)
  {
    Ipv4Drb::AddCoreSwitchAddress(addr);
  }
}

}
