#include "tcp-do-v1.h"
#include "ns3/simulator.h"
#include "ns3/log.h"
#include <algorithm> // std::max 사용
#include <numeric>   // std::accumulate 사용
#include <cmath>     // std::abs, std::sqrt 사용

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("TcpDo");

NS_OBJECT_ENSURE_REGISTERED(TcpDo);

TypeId TcpDo::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::TcpDo")
        .SetParent<TcpVegas>()
        .SetGroupName("Internet")
        .AddConstructor<TcpDo>()
        .AddAttribute("CongestionThreshold", "The threshold for oscillation frequency to detect congestion",
                      DoubleValue(0.000001),
                      MakeDoubleAccessor(&TcpDo::m_congestionThreshold),
                      MakeDoubleChecker<double>());
    return tid;
}

TcpDo::TcpDo()
    : m_congestionThreshold(0.000001),
      m_lastOscillationFrequency(0.0),
      m_maxRttHistorySize(30),
      m_timeWindow(Seconds(0.1)),
      m_retransmitDetected(false)
{
}

TcpDo::TcpDo(const TcpDo& sock)
    : TcpVegas(sock),
      m_congestionThreshold(sock.m_congestionThreshold),
      m_lastOscillationFrequency(sock.m_lastOscillationFrequency),
      m_oscillationCount(sock.m_oscillationCount),
      m_timeWindow(sock.m_timeWindow),
      m_retransmitDetected(sock.m_retransmitDetected)
{
}

TcpDo::~TcpDo() {}

std::string TcpDo::GetName() const
{
    return "TcpDo";
}

void TcpDo::PktsAcked(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time& rtt)
{
    if (!m_retransmitDetected) {
        TcpVegas::PktsAcked(tcb, segmentsAcked, rtt);
        CalculateOscillationFrequency(rtt);
    } else {
        NS_LOG_INFO("Retransmission detected: Ignoring RTT update to prevent oscillation");
        m_retransmitDetected = false;
    }
}

void TcpDo::IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked)
{
    if (m_retransmitDetected) {
        HandleRetransmission(tcb);
        m_retransmitDetected = false;
        return;
    }

    bool vegasDetectedCongestion = (tcb->m_cWnd.Get() > tcb->m_ssThresh);
    double currentOscillationFrequency = m_lastOscillationFrequency;
    bool frequencyDetectedCongestion = (currentOscillationFrequency > m_congestionThreshold);
    double currentRtt = tcb->m_lastRtt.Get().GetSeconds();

    double rttAverage = std::accumulate(m_rttHistory.begin(), m_rttHistory.end(), 0.0,
                                        [](double sum, const Time& rtt) { return sum + rtt.GetSeconds(); }) / m_rttHistory.size();
    double rttStdDev = std::sqrt(std::accumulate(m_rttHistory.begin(), m_rttHistory.end(), 0.0,
                      [rttAverage](double acc, const Time& val) {
                          return acc + std::pow(val.GetSeconds() - rttAverage, 2);
                      }) / m_rttHistory.size());

    double dynamicRttThreshold = rttAverage + 2 * rttStdDev;

    if (vegasDetectedCongestion || frequencyDetectedCongestion || currentRtt > dynamicRttThreshold) {
        NS_LOG_INFO("Congestion detected: Reducing cwnd based on Vegas and oscillation frequency");

        uint32_t newCwnd;
        double severity = currentOscillationFrequency / m_congestionThreshold;

        double reductionFactor = std::max(0.5, 1.0 - severity * 0.1);
        double recoveryFactor = std::min(1.8, 1.0 + severity * 0.1);

        newCwnd = std::max(static_cast<uint32_t>(tcb->m_cWnd.Get() * reductionFactor), tcb->m_segmentSize * 10);
        m_congestionThreshold *= recoveryFactor;

        tcb->m_ssThresh = newCwnd;
        tcb->m_cWnd = newCwnd;
    } else {
        NS_LOG_INFO("No congestion detected: Increasing cwnd cautiously");

        double lowOscillationThreshold = 0.00001;
        if (currentOscillationFrequency <= lowOscillationThreshold) {
            tcb->m_cWnd += tcb->m_segmentSize * 8;
            m_congestionThreshold *= 0.98;
        }

        double alpha = 0.5;
        double beta = 2.0;

        double diff = (tcb->m_cWnd.Get() - tcb->m_ssThresh.Get()) / tcb->m_segmentSize;

        if (diff < alpha) {
            tcb->m_cWnd += tcb->m_segmentSize * 6;
        } else if (diff > beta) {
            tcb->m_cWnd -= tcb->m_segmentSize * 3;
        } else {
            uint32_t maxIncrease = std::max(1U, tcb->m_cWnd.Get() / 2);
            tcb->m_cWnd = std::min(tcb->m_cWnd.Get() + maxIncrease, tcb->m_ssThresh.Get());
        }
    }
}

void TcpDo::HandleRetransmission(Ptr<TcpSocketState> tcb)
{
    NS_LOG_INFO("Retransmission detected: Adjusting congestion control based on RTT");

    double currentRtt = tcb->m_lastRtt.Get().GetSeconds();
    double weightedRtt = std::accumulate(m_rttHistory.begin(), m_rttHistory.end(), 0.0,
                                         [](double sum, const Time& rtt) { return sum + rtt.GetSeconds(); }) / m_rttHistory.size();

    if (currentRtt > weightedRtt) {
        tcb->m_ssThresh = std::max(static_cast<uint32_t>(tcb->m_ssThresh.Get() / 1.5), 2 * tcb->m_segmentSize);
        tcb->m_cWnd = tcb->m_ssThresh;
    } else {
        tcb->m_cWnd = std::max(static_cast<uint32_t>(tcb->m_cWnd / 1.1), tcb->m_segmentSize * 10);
    }
}

void TcpDo::Retransmit(Ptr<TcpSocketState> tcb)
{
    NS_LOG_INFO("Retransmission detected, setting flag for special handling");
    m_retransmitDetected = true;
}

void TcpDo::CalculateOscillationFrequency(const Time& rtt)
{
    static Time lastRtt = Time(0);
    static Time firstRttTime = Simulator::Now();
    Time currentRtt = rtt;

    if (lastRtt != Time(0)) {
        double rttChange = (currentRtt - lastRtt).GetSeconds();
        if (std::abs(rttChange) > 0.000001) {
            m_oscillationCount++;
        }
    }

    lastRtt = currentRtt;

    Time now = Simulator::Now();
    if (now - firstRttTime >= m_timeWindow) {
        m_lastOscillationFrequency = static_cast<double>(m_oscillationCount) / m_timeWindow.GetSeconds();
        m_oscillationCount = 0;
        firstRttTime = now;
    }

    if (m_lastOscillationFrequency > m_congestionThreshold) {
        m_maxRttHistorySize = std::min(static_cast<size_t>(50), m_maxRttHistorySize + 1);
    } else {
        m_maxRttHistorySize = std::max(static_cast<size_t>(10), m_maxRttHistorySize - 1);
    }

    m_rttHistory.push_back(rtt);
    
    if (m_rttHistory.size() > m_maxRttHistorySize) {
        m_rttHistory.pop_front();
    }

    if (m_rttHistory.size() < 2) return;

    double weightedSum = 0.0;
    double weightTotal = 0.0;
    double weight = 1.0;
    double weightIncrement = 0.1;

    for (auto it = m_rttHistory.rbegin(); it != m_rttHistory.rend(); ++it) {
        weightedSum += it->GetSeconds() * weight;
        weightTotal += weight;
        weight += weightIncrement;
    }

    double weightedAverageRtt = weightedSum / weightTotal;
    m_lastOscillationFrequency = std::abs(currentRtt.GetSeconds() - weightedAverageRtt);
}

} // namespace ns3
