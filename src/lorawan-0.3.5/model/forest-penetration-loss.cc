/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * A simple foliage penetration loss model for forests.
 */

#include "forest-penetration-loss.h"

#include "ns3/double.h"
#include "ns3/log.h"
#include "ns3/mobility-model.h"

#include <algorithm>

namespace ns3
{
namespace lorawan
{

NS_LOG_COMPONENT_DEFINE("ForestPenetrationLoss");
NS_OBJECT_ENSURE_REGISTERED(ForestPenetrationLoss);

TypeId
ForestPenetrationLoss::GetTypeId()
{
    static TypeId tid = TypeId("ns3::ForestPenetrationLoss")
                            .SetParent<PropagationLossModel>()
                            .SetGroupName("Lora")
                            .AddConstructor<ForestPenetrationLoss>()
                            .AddAttribute("LightFoliageAttenuationPerMeter",
                                          "dB loss per meter for light foliage.",
                                          DoubleValue(0.08),
                                          MakeDoubleAccessor(&ForestPenetrationLoss::m_lightPerMeter),
                                          MakeDoubleChecker<double>(0.0))
                            .AddAttribute("HeavyFoliageAttenuationPerMeter",
                                          "dB loss per meter for heavy foliage.",
                                          DoubleValue(0.18),
                                          MakeDoubleAccessor(&ForestPenetrationLoss::m_heavyPerMeter),
                                          MakeDoubleChecker<double>(0.0))
                            .AddAttribute("ShadowingStdDev",
                                          "Shadowing standard deviation in dB (log-normal in linear domain).",
                                          DoubleValue(2.0),
                                          MakeDoubleAccessor(&ForestPenetrationLoss::m_shadowStdDev),
                                          MakeDoubleChecker<double>(0.0))
                            .AddAttribute("MinCanopyFraction",
                                          "Minimum fraction of link distance assumed under canopy.",
                                          DoubleValue(0.25),
                                          MakeDoubleAccessor(&ForestPenetrationLoss::m_minCanopyFrac),
                                          MakeDoubleChecker<double>(0.0, 1.0))
                            .AddAttribute("MaxCanopyFraction",
                                          "Maximum fraction of link distance assumed under canopy.",
                                          DoubleValue(0.6),
                                          MakeDoubleAccessor(&ForestPenetrationLoss::m_maxCanopyFrac),
                                          MakeDoubleChecker<double>(0.0, 1.0));
    return tid;
}

ForestPenetrationLoss::ForestPenetrationLoss()
{
    m_uniform = CreateObject<UniformRandomVariable>();
    m_normal = CreateObject<NormalRandomVariable>();
}

ForestPenetrationLoss::~ForestPenetrationLoss() = default;

double
ForestPenetrationLoss::DoCalcRxPower(double txPowerDbm,
                                     Ptr<MobilityModel> a,
                                     Ptr<MobilityModel> b) const
{
    NS_LOG_FUNCTION(this << txPowerDbm << a << b);

    double distance = a->GetDistanceFrom(b);
    if (distance <= 0.0)
    {
        return txPowerDbm;
    }

    // Randomly decide canopy depth along the path
    double canopyFrac = m_uniform->GetValue(m_minCanopyFrac, m_maxCanopyFrac);
    canopyFrac = std::min(std::max(canopyFrac, 0.0), 1.0);
    double foliageDepth = canopyFrac * distance;

    // Blend between light and heavy foliage slopes
    double alpha = (m_uniform->GetValue(0.0, 1.0) < 0.5) ? m_lightPerMeter : m_heavyPerMeter;

    double meanLoss = alpha * foliageDepth;
    double shadow = m_normal->GetValue(0.0, m_shadowStdDev);

    double extraLoss = std::max(0.0, meanLoss + shadow);

    NS_LOG_DEBUG("Forest loss: distance=" << distance << " m, foliageDepth=" << foliageDepth
                                          << " m, alpha=" << alpha << " dB/m, shadow=" << shadow
                                          << " dB, extraLoss=" << extraLoss << " dB");

    return txPowerDbm - extraLoss;
}

int64_t
ForestPenetrationLoss::DoAssignStreams(int64_t stream)
{
    m_uniform->SetStream(stream);
    m_normal->SetStream(stream + 1);
    return 2;
}

} // namespace lorawan
} // namespace ns3
