#ifndef TCP_DO_H
#define TCP_DO_H

#include "ns3/tcp-vegas.h"
#include "ns3/simulator.h"
#include "ns3/log.h"
#include <deque>

namespace ns3 {

/**
 * \brief TcpDo implements a custom TCP congestion control algorithm
 *        that combines elements of TCP Vegas with oscillation frequency-based
 *        congestion detection.
 */
class TcpDo : public TcpVegas
{
public:
    // Create TypeId for TcpDo
    static TypeId GetTypeId(void);

    TcpDo(); // Default constructor
    TcpDo(const TcpDo& sock); // Copy constructor
    virtual ~TcpDo(); // Destructor

    virtual std::string GetName() const override;

protected:
    // Override methods from TcpVegas
    virtual void PktsAcked(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time& rtt) override;
    virtual void IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked) override;

    // Method to adjust congestion threshold
    void AdjustCongestionThreshold(double newThreshold);

private:
    // Calculate oscillation frequency based on RTT history
    void CalculateOscillationFrequency(const Time& rtt);

    // Threshold for detecting congestion based on oscillation frequency
    double m_congestionThreshold;

    // Last calculated oscillation frequency
    double m_lastOscillationFrequency;

    // History of RTT samples for oscillation frequency calculation
    std::deque<Time> m_rttHistory;

    size_t m_maxRttHistorySize;
};

} // namespace ns3

#endif // TCP_DO_H
