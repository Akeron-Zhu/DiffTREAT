/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef URGE_TAG_H
#define URGE_TAG_H

#include "ns3/tag.h"

namespace ns3 {

class UrgeTag : public Tag
{
public:

    static TypeId GetTypeId (void);

    UrgeTag ();

    uint32_t GetUrgeNum (void) const;

    void SetUrgeNum(int urgeNum);

    virtual TypeId GetInstanceTypeId (void) const;

    virtual uint32_t GetSerializedSize (void) const;

    virtual void Serialize (TagBuffer i) const;

    virtual void Deserialize (TagBuffer i);

    virtual void Print (std::ostream &os) const;

private:

    uint32_t m_urgeNum;

};

}

#endif
