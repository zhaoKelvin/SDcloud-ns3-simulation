#ifndef SENDER_ID_TAG_H
#define SENDER_ID_TAG_H

#include "ns3/tag.h"
#include "ns3/uinteger.h"
#include "ns3/type-id.h"
#include <iostream>

namespace ns3 {
namespace lorawan {

class SenderIdTag : public Tag
{
public:
    SenderIdTag();
    SenderIdTag(uint32_t senderId);
    SenderIdTag(uint32_t senderId, double sendTime);

    static TypeId GetTypeId();
    virtual TypeId GetInstanceTypeId() const override;

    virtual uint32_t GetSerializedSize() const override;
    virtual void Serialize(TagBuffer buffer) const override;
    virtual void Deserialize(TagBuffer buffer) override;
    virtual void Print(std::ostream &os) const override;

    void SetSenderId(uint32_t senderId);
    uint32_t GetSenderId() const;

    void SetSendTime(double sendTime);
    double GetSendTime() const;

private:
    uint32_t m_senderId;
    double m_sendTime;
};

} // namespace ns3
}
#endif // SENDER_ID_TAG_H
