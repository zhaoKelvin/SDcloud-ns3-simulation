/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * A simple foliage penetration loss model for forests.
 * Implements a per-meter attenuation plus log-normal shadowing term along
 * the fraction of the path assumed to be under canopy.
 */

#ifndef FOREST_PENETRATION_LOSS_H
#define FOREST_PENETRATION_LOSS_H

#include "ns3/propagation-loss-model.h"
#include "ns3/random-variable-stream.h"

namespace ns3
{
namespace lorawan
{

/**
 * @ingroup lorawan
 *
 * A lightweight forest penetration loss model.
 * The loss is computed as: L = alpha * d_foliage + N(0, sigma),
 * where d_foliage is a random fraction of the link distance representing
 * canopy depth, alpha is drawn between light and heavy foliage slopes,
 * and sigma is a log-normal shadowing stddev in dB.
 */
class ForestPenetrationLoss : public PropagationLossModel
{
  public:
    static TypeId GetTypeId();

    ForestPenetrationLoss();
    ~ForestPenetrationLoss() override;

  private:
    double DoCalcRxPower(double txPowerDbm, Ptr<MobilityModel> a, Ptr<MobilityModel> b) const override;
    int64_t DoAssignStreams(int64_t stream) override;

    Ptr<UniformRandomVariable> m_uniform; //!< Uniform(0,1) RNG
    Ptr<NormalRandomVariable> m_normal;   //!< Normal(0,1) RNG

    double m_lightPerMeter;   //!< dB per meter for light foliage
    double m_heavyPerMeter;   //!< dB per meter for heavy foliage
    double m_shadowStdDev;    //!< Shadowing stddev (dB)
    double m_minCanopyFrac;   //!< Minimum fraction of link under canopy
    double m_maxCanopyFrac;   //!< Maximum fraction of link under canopy
};

} // namespace lorawan
} // namespace ns3

#endif /* FOREST_PENETRATION_LOSS_H */
