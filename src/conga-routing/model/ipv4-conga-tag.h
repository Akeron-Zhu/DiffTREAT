#ifndef NS3_IPV4_CONGA_TAG
#define NS3_IPV4_CONGA_TAG

#include "ns3/tag.h"

namespace ns3 {

//Tag的作用：tag a set of bytes in a packet
class Ipv4CongaTag: public Tag
{
public:
    Ipv4CongaTag ();

    static TypeId GetTypeId (void);

    void SetLbTag (uint32_t lbTag);

    uint32_t GetLbTag (void) const;

    void SetCe (uint32_t ce);

    uint32_t GetCe (void) const;

    void SetFbLbTag (uint32_t fbLbTag);

    uint32_t GetFbLbTag (void) const;

    void SetFbMetric (uint32_t fbMetric);

    uint32_t GetFbMetric (void) const;

    virtual TypeId GetInstanceTypeId (void) const;

    virtual uint32_t GetSerializedSize (void) const;

    virtual void Serialize (TagBuffer i) const;

    virtual void Deserialize (TagBuffer i);

    virtual void Print (std::ostream &os) const;

private:
    uint32_t m_lbTag; //用于标记从源路由的哪个端口发出的包，使目的端记入表中时较为方便
    uint32_t m_ce;    //标记从源路由发出经过的路径的拥塞度量，congestion estiamte
    uint32_t m_fbLbTag;  //返回目的路由pigbacking的端口号 feedback
    uint32_t m_fbMetric; //目的路由pigbacking的端口号对应的拥塞度量
};

}

#endif
