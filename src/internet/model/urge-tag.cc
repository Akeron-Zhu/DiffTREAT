/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include "ns3/urge-tag.h"

namespace ns3
{

TypeId
UrgeTag::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::UrgeTag")
                            .SetParent<Tag>()
                            .SetGroupName("Internet")
                            .AddConstructor<UrgeTag>();

    return tid;
}

UrgeTag::UrgeTag() : m_urgeNum(10)
{
}

uint32_t
UrgeTag::GetUrgeNum(void) const
{
    return m_urgeNum;
}

void
UrgeTag::SetUrgeNum(int urgeNum)
{
    m_urgeNum = urgeNum;
}

TypeId
UrgeTag::GetInstanceTypeId(void) const
{
    return UrgeTag::GetTypeId();
}

uint32_t
UrgeTag::GetSerializedSize(void) const
{
    return sizeof(uint32_t);
}

void UrgeTag::Serialize(TagBuffer i) const
{
    i.WriteU32(m_urgeNum);
}

void UrgeTag::Deserialize(TagBuffer i)
{
    m_urgeNum = i.ReadU32();
}

void UrgeTag::Print(std::ostream &os) const
{
    os << " path: " << m_urgeNum;
}

} // namespace ns3
