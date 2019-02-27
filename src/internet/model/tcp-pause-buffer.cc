#include "tcp-pause-buffer.h"

#include "ns3/log.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE ("TcpPauseBuffer");

NS_OBJECT_ENSURE_REGISTERED (TcpPauseBuffer);

//返回TypeId
TypeId
TcpPauseBuffer::GetTypeId (void)
{
    static TypeId tid = TypeId ("ns3::TcpPauseBuffer")
        .SetParent<Object> ()
        .SetGroupName ("Internet")
        .AddConstructor<TcpPauseBuffer> ();

    return tid;

}

TcpPauseBuffer::TcpPauseBuffer ()
{
    NS_LOG_FUNCTION (this);
}

TcpPauseBuffer::~TcpPauseBuffer ()
{
    NS_LOG_FUNCTION (this);
}

//得到PauseBuffer中的条目
struct TcpPauseItem
TcpPauseBuffer::GetBufferedItem (void)
{
    struct TcpPauseItem item = m_pauseItems.front ();
    m_pauseItems.pop_front ();
    return item;
}

//检查PauseBuffer是否为空
bool
TcpPauseBuffer::HasBufferedItem (void)
{
    return m_pauseItems.empty ();
}

//构建条目并入入m_pauseItems中
void
TcpPauseBuffer::BufferItem (Ptr<Packet> p, TcpHeader header)
{
    struct TcpPauseItem pauseItem;
    pauseItem.packet = p;
    pauseItem.header = header;
    m_pauseItems.push_back (pauseItem);
}

}
