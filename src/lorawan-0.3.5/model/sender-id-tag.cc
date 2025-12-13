#include "sender-id-tag.h"

namespace ns3::lorawan {

SenderIdTag::SenderIdTag()
    : m_senderId(0), m_sendTime(0)
{
}

SenderIdTag::SenderIdTag(uint32_t senderId)
    : m_senderId(senderId), m_sendTime(0)
{
}

SenderIdTag::SenderIdTag(uint32_t senderId, double sendTime)
    : m_senderId(senderId), m_sendTime(sendTime)
{
}

TypeId
SenderIdTag::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::SenderIdTag")
            .SetParent<Tag>()
            .AddConstructor<SenderIdTag>();
    return tid;
}

TypeId
SenderIdTag::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
SenderIdTag::GetSerializedSize() const
{
    return sizeof(uint32_t) + sizeof(double);
}

void
SenderIdTag::Serialize(TagBuffer buffer) const
{
    buffer.WriteU32(m_senderId);
    buffer.WriteDouble(m_sendTime);
}

void
SenderIdTag::Deserialize(TagBuffer buffer)
{
    m_senderId = buffer.ReadU32();
    m_sendTime = buffer.ReadDouble();
}

void
SenderIdTag::Print(std::ostream &os) const
{
    os << "SenderId=" << m_senderId << ",";
    os << "SendTime=" << m_sendTime;
}

void
SenderIdTag::SetSenderId(uint32_t senderId)
{
    m_senderId = senderId;
}

uint32_t
SenderIdTag::GetSenderId() const
{
    return m_senderId;
}

void
SenderIdTag::SetSendTime(double sendTime)
{
    m_sendTime = sendTime;
}

double
SenderIdTag::GetSendTime() const
{
    return m_sendTime;
}

} // namespace ns3
