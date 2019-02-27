/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2008 INRIA
 *
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
 *
 * Author: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 */
#include "rto-pri-tag.h"
#include "ns3/log.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("RtoPriTag");

NS_OBJECT_ENSURE_REGISTERED (RtoPriTag);

TypeId 
RtoPriTag::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::RtoPriTag")
    .SetParent<Tag> ()
    .SetGroupName("Network")
    .AddConstructor<RtoPriTag> ()
  ;
  return tid;
}
TypeId 
RtoPriTag::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}
uint32_t 
RtoPriTag::GetSerializedSize (void) const
{
  NS_LOG_FUNCTION (this);
  return 2;
}
void 
RtoPriTag::Serialize (TagBuffer buf) const
{
  NS_LOG_FUNCTION (this << &buf);
  buf.WriteU8 (m_RtoRank);
  buf.WriteU8 (m_SizeRank);
}
void 
RtoPriTag::Deserialize (TagBuffer buf)
{
  NS_LOG_FUNCTION (this << &buf);
  m_RtoRank = buf.ReadU8 ();
  m_SizeRank= buf.ReadU8 ();
}
void 
RtoPriTag::Print (std::ostream &os) const
{
  NS_LOG_FUNCTION (this << &os);
  os << "RtoRank=" << m_RtoRank;
  os << "SizeRank=" << m_SizeRank;
}

RtoPriTag::RtoPriTag ()
  : Tag () 
{
  NS_LOG_FUNCTION (this);
  m_RtoRank=-1;
  m_SizeRank=-1;
}

RtoPriTag::RtoPriTag (uint8_t rtoRank, uint8_t sizeRank)
  : Tag (),
    m_RtoRank (rtoRank),
    m_SizeRank(sizeRank)
{
  NS_LOG_FUNCTION (this << rtoRank<<sizeRank);
}

void
RtoPriTag::SetRtoRank (uint8_t rtoRank)
{
  NS_LOG_FUNCTION (this << rtoRank);
  m_RtoRank = rtoRank;
}

uint8_t
RtoPriTag::GetRtoRank (void) const
{
  NS_LOG_FUNCTION (this);
  return m_RtoRank;
}

void
RtoPriTag::SetSizeRank (uint8_t sizeRank)
{
  NS_LOG_FUNCTION (this << sizeRank);
  m_SizeRank = sizeRank;
}

uint8_t
RtoPriTag::GetSizeRank (void) const
{
  NS_LOG_FUNCTION (this);
  return m_SizeRank;
}


} // namespace ns3

