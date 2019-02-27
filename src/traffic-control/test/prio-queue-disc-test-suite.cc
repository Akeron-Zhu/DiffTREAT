/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2017 Universita' degli Studi di Napoli Federico II
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
 * Authors: Stefano Avallone <stavallo@unina.it>
 *
 */

#include "ns3/test.h"
#include "ns3/prio-queue-disc.h"
#include "ns3/prio-subqueue-disc.h"
#include "ns3/packet-filter.h"
#include "ns3/packet.h"
#include "ns3/socket.h"
#include "ns3/string.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include <array>
#include <queue>
#include<iostream>

using namespace ns3;

/**
 * \ingroup traffic-control-test
 * \ingroup tests
 *
 * \brief Prio Queue Disc Test Item
 */
class PrioQueueDiscTestItem : public QueueDiscItem
{
public:
  /**
   * Constructor
   *
   * \param p the packet
   * \param addr the address
   * \param priority the packet priority
   */
  PrioQueueDiscTestItem (Ptr<Packet> p, const Address & addr, uint8_t rtoRank,uint8_t sizeRank);
  virtual ~PrioQueueDiscTestItem ();
  virtual void AddHeader (void);
  virtual bool Mark (void);
  uint8_t m_RtoRank;
  uint8_t m_SizeRank;
};

PrioQueueDiscTestItem::PrioQueueDiscTestItem (Ptr<Packet> p, const Address & addr, uint8_t rtoRank,uint8_t sizeRank)
  : QueueDiscItem (p, addr, 0)
{
  m_RtoRank=rtoRank;
  m_SizeRank=sizeRank;
}

PrioQueueDiscTestItem::~PrioQueueDiscTestItem ()
{
}

void
PrioQueueDiscTestItem::AddHeader (void)
{
}

bool
PrioQueueDiscTestItem::Mark (void)
{
  return false;
}


/**
 * \ingroup traffic-control-test
 * \ingroup tests
 *
 * \brief Prio Queue Disc Test Packet Filter
 */
class PrioQueueDiscTestFilter : public PacketFilter
{
public:
  /**
   * Constructor
   *
   * \param cls whether this filter is able to classify a PrioQueueDiscTestItem
   */
  PrioQueueDiscTestFilter (bool cls);
  virtual ~PrioQueueDiscTestFilter ();
  /**
   * \brief Set the value returned by DoClassify
   *
   * \param ret the value that DoClassify returns
   */
  void SetReturnValue (int32_t ret);
  void SetCls(bool flag);
private:
  virtual bool CheckProtocol (Ptr<QueueDiscItem> item) const;
  virtual int32_t DoClassify (Ptr<QueueDiscItem> item) const;

  bool m_cls;     //!< whether this filter is able to classify a PrioQueueDiscTestItem
  bool canCheck;
  int32_t m_ret;  //!< the value that DoClassify returns if m_cls is true
};

PrioQueueDiscTestFilter::PrioQueueDiscTestFilter (bool cls)
  : m_cls (cls),
    m_ret (0)
{
  canCheck=true;
}

PrioQueueDiscTestFilter::~PrioQueueDiscTestFilter ()
{
}

void
PrioQueueDiscTestFilter::SetReturnValue (int32_t ret)
{
  m_ret = ret;
}

void
PrioQueueDiscTestFilter::SetCls (bool flag)
{
  canCheck = flag;
}

bool
PrioQueueDiscTestFilter::CheckProtocol (Ptr<QueueDiscItem> item) const
{
  return canCheck;
}

int32_t
PrioQueueDiscTestFilter::DoClassify (Ptr<QueueDiscItem> item) const
{
  if(m_cls) return m_ret;
  else return 7-m_ret;
}



/**
 * \ingroup traffic-control-test
 * \ingroup tests
 *
 * \brief Prio Queue Disc Test Packet Filter
 */
class PrioSubqueueDiscTestFilter : public PacketFilter
{
public:
  /**
   * Constructor
   *
   * \param cls whether this filter is able to classify a PrioQueueDiscTestItem
   */
  PrioSubqueueDiscTestFilter (bool cls);
  virtual ~PrioSubqueueDiscTestFilter ();
  /**
   * \brief Set the value returned by DoClassify
   *
   * \param ret the value that DoClassify returns
   */
  void SetSizeRank (int32_t ret);

private:
  virtual bool CheckProtocol (Ptr<QueueDiscItem> item) const;
  virtual int32_t DoClassify (Ptr<QueueDiscItem> item) const;

  bool m_cls;     //!< whether this filter is able to classify a PrioQueueDiscTestItem
  int32_t m_SizeRank;  //!< the value that DoClassify returns if m_cls is true
};

PrioSubqueueDiscTestFilter::PrioSubqueueDiscTestFilter (bool cls)
  : m_cls (cls),
    m_SizeRank (0)
{
}

PrioSubqueueDiscTestFilter::~PrioSubqueueDiscTestFilter ()
{
}

void
PrioSubqueueDiscTestFilter::SetSizeRank (int32_t ret)
{
  m_SizeRank = ret;
}

bool
PrioSubqueueDiscTestFilter::CheckProtocol (Ptr<QueueDiscItem> item) const
{
  return m_cls;
}

int32_t
PrioSubqueueDiscTestFilter::DoClassify (Ptr<QueueDiscItem> item) const
{
  Ptr<PrioQueueDiscTestItem> itmp=DynamicCast<PrioQueueDiscTestItem> (item);
  return itmp->m_SizeRank;
}


/**
 * \ingroup traffic-control-test
 * \ingroup tests
 *
 * \brief Prio Queue Disc Test Case
 */
class PrioQueueDiscTestCase : public TestCase
{
public:
  PrioQueueDiscTestCase ();
  virtual void DoRun (void);
};

PrioQueueDiscTestCase::PrioQueueDiscTestCase ()
  : TestCase ("Sanity check on the prio queue disc implementation")
{
}

void
PrioQueueDiscTestCase::DoRun (void)
{
  Ptr<PrioQueueDisc> qdisc;
  Ptr<QueueDiscItem> item;
  Address dest;
  std::array<std::queue<uint64_t>,8> uids;
  /*
   * Test 1: set priomap
   */
  qdisc = CreateObject<PrioQueueDisc> ();
  // add 4 child fifo queue discs
  for (uint8_t i = 0; i < 8; i++)
    {
      Ptr<PrioSubqueueDisc> child = CreateObject<PrioSubqueueDisc> ();
      Ptr<PrioSubqueueDiscTestFilter> pf = CreateObject<PrioSubqueueDiscTestFilter> (true);
      child->AddPacketFilter (pf);


      child->Initialize ();
      Ptr<QueueDiscClass> c = CreateObject<QueueDiscClass> ();
      c->SetQueueDisc (child);
      qdisc->AddQueueDiscClass (c);
    }

  qdisc->Initialize ();
  NS_TEST_EXPECT_MSG_EQ (qdisc->GetNQueueDiscClasses (), 8, "Verify that the queue disc has 4 child queue discs");

  /*
   * Test 2: classify packets based on priomap because no packet filter is installed
   */

  // create packets with priorities from 0 to 3
 /* for (uint16_t i = 0; i < 8; i++)
    {
      NS_TEST_EXPECT_MSG_EQ (qdisc->GetQueueDiscClass (i)->GetQueueDisc ()->GetNPackets (),
                             0, "There should be no packets in the child queue disc " << i);

      for(int j=0;j<8;j++)
      {
          item = Create<PrioQueueDiscTestItem> (Create<Packet> (100), dest, i,j);
          qdisc->Enqueue (item);

        // packet is assigned band 1 //没有filter,默认为1
        uids[1].push (item->GetPacket ()->GetUid ());
      }


      NS_TEST_EXPECT_MSG_EQ (qdisc->GetQueueDiscClass (1)->GetQueueDisc ()->GetNPackets (),
                             8*i, "There should be one packet in the child queue disc " << i);
    }
for(int j=0;j<8;j++) 
      std::cout<<"RtoRank["<<j<<"] has "<<qdisc->GetQueueDiscClass (j)->GetQueueDisc ()->GetNPackets ()<<" Packets!"<<std::endl;
      std::cout<<"\n";//*/
  /*
   * Test 3: classify packets based on priomap because no packet filter able
   * to classify packets is installed
   */

  Ptr<PrioQueueDiscTestFilter> pf1 = CreateObject<PrioQueueDiscTestFilter> (false);
  qdisc->AddPacketFilter (pf1);

  // create packets with priorities from 4 to 7
  for (uint16_t i = 0; i < 4; i++)
    {
      pf1->SetReturnValue (i);
      //NS_TEST_EXPECT_MSG_EQ (qdisc->GetQueueDiscClass (i)->GetQueueDisc ()->GetNPackets (),
       //                      1, "There should be one packet in the child queue disc " << i);
      for(int j=0;j<8;j++)
      {
        item = Create<PrioQueueDiscTestItem> (Create<Packet> (110), dest, i,j);
        qdisc->Enqueue (item);
        // packet is assigned band i
        uids[7-i].push (item->GetPacket ()->GetUid ());
      }
      //std::cout<<"RtoRank["<<i<<"] has "<<qdisc->GetQueueDiscClass (i)->GetQueueDisc ()->GetNPackets ()<<" Packets!"<<std::endl;
     // NS_TEST_EXPECT_MSG_EQ (qdisc->GetQueueDiscClass (i)->GetQueueDisc ()->GetNPackets (),
       //                      2, "There should be two packets in the child queue disc " << i);
    }
    for(int j=0;j<8;j++) 
      std::cout<<"RtoRank["<<j<<"] has "<<qdisc->GetQueueDiscClass (j)->GetQueueDisc ()->GetNPackets ()<<" Packets!"<<std::endl;
      std::cout<<"\n";
    pf1->SetCls(false);
  /*
   * Test 4: classify packets based on the value returned by the installed packet filter
   */

  Ptr<PrioQueueDiscTestFilter> pf2 = CreateObject<PrioQueueDiscTestFilter> (true);
  qdisc->AddPacketFilter (pf2);

  // create packets with priority 0 (which is neglected by the prio queue disc)
  for (uint16_t i = 0; i < 4; i++)
    {
      pf2->SetReturnValue (i);
      //NS_TEST_EXPECT_MSG_EQ (qdisc->GetQueueDiscClass (i)->GetQueueDisc ()->GetNPackets (),
     //                        2, "There should be two packets in the child queue disc " << i);
      for(int j=0;j<8;j++)
      {
        item = Create<PrioQueueDiscTestItem> (Create<Packet> (120), dest, i,j);
        qdisc->Enqueue (item);
        // packet is assigned band i
        uids[i].push (item->GetPacket ()->GetUid ());
      }
    
     //std::cout<<"RtoRank["<<i<<"] has "<<qdisc->GetQueueDiscClass (i)->GetQueueDisc ()->GetNPackets ()<<" Packets!"<<std::endl;
   
      //NS_TEST_EXPECT_MSG_EQ (qdisc->GetQueueDiscClass (i)->GetQueueDisc ()->GetNPackets (),
        //                     3, "There should be three packets in the child queue disc " << i);
    }
for(int j=0;j<8;j++) 
      std::cout<<"RtoRank["<<j<<"] has "<<qdisc->GetQueueDiscClass (j)->GetQueueDisc ()->GetNPackets ()<<" Packets!"<<std::endl;
      std::cout<<"\n";
  /*
   * Test 5: classify packets into the band specified by the first element of the
   * priomap array because the value returned by the installed packet filter is
   * not less than the number of bands
   */

  // create packets with priority 1 (which is neglected by the prio queue disc)
  for (uint16_t i = 0; i < 8; i++)
    {
      pf2->SetReturnValue (1);
      NS_TEST_EXPECT_MSG_EQ (qdisc->GetBandForPriority (0), 0, "The band for priority 0 must be band 0");
      NS_TEST_EXPECT_MSG_EQ (qdisc->GetQueueDiscClass (0)->GetQueueDisc ()->GetNPackets (), i+3u,
                             "There should be " << i+3 << " packets in the child queue disc "
                             << qdisc->GetBandForPriority (0));
      
        item = Create<PrioQueueDiscTestItem> (Create<Packet> (130), dest, 1,i);
        qdisc->Enqueue (item);
        // packet is assigned band 0
        uids[1].push (item->GetPacket ()->GetUid ());
      // std::cout<<"RtoRank["<<i<<"] has "<<qdisc->GetQueueDiscClass (i)->GetQueueDisc ()->GetNPackets ()<<" Packets!"<<std::endl;
   

      //NS_TEST_EXPECT_MSG_EQ (qdisc->GetQueueDiscClass (0)->GetQueueDisc ()->GetNPackets (), i+4u,
      //                       "There should be " << i+4 << " packets in the child queue disc "
        //                     << qdisc->GetBandForPriority (0));
    }
for(int j=0;j<8;j++) 
      std::cout<<"RtoRank["<<j<<"] has "<<qdisc->GetQueueDiscClass (j)->GetQueueDisc ()->GetNPackets ()<<" Packets!"<<std::endl;
      std::cout<<"\n";
  /*
   * Test 6: dequeue packets starting from the highest priority band (band 0)
   */
  int ok=0,nok=0;
  while ((item = qdisc->Dequeue ()))
    {
      for (uint16_t i = 0; i < 8; i++)
        {
          if (uids[i].empty ())
            {
              NS_TEST_EXPECT_MSG_EQ (qdisc->GetQueueDiscClass (i)->GetQueueDisc ()->GetNPackets (),
                                     0, "Band " << i << " should be empty");
              continue;
            }
            if(uids[i].front ()== item->GetPacket ()->GetUid ()) ok++;
            else nok++;
          NS_TEST_EXPECT_MSG_EQ (uids[i].front (), item->GetPacket ()->GetUid (),
                                 "The dequeued packet is not the one we expected");
          uids[i].pop ();
          break;
        }
    }
  std::cout<<ok<<' '<<nok<<std::endl;
  Simulator::Destroy ();
}

/**
 * \ingroup traffic-control-test
 * \ingroup tests
 *
 * \brief Prio Queue Disc Test Suite
 */
static class PrioQueueDiscTestSuite : public TestSuite
{
public:
  PrioQueueDiscTestSuite ()
    : TestSuite ("prio-queue-disc", UNIT)
  {
    AddTestCase (new PrioQueueDiscTestCase (), TestCase::QUICK);
  }
} g_prioQueueTestSuite; ///< the test suite
