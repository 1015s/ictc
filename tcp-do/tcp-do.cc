#include "tcp-do.h"
#include "ns3/simulator.h"
#include "ns3/log.h"
#include <algorithm> // std::max 사용

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
                      DoubleValue(0.0005), // 임계값을 설정하여 민감도 조정
                      MakeDoubleAccessor(&TcpDo::m_congestionThreshold),
                      MakeDoubleChecker<double>());
    return tid;
}

TcpDo::TcpDo()
    : m_congestionThreshold(0.0005),  // 초기화
      m_lastOscillationFrequency(0.0) // 초기화
{
}

TcpDo::TcpDo(const TcpDo& sock)
    : TcpVegas(sock),
      m_congestionThreshold(sock.m_congestionThreshold),
      m_lastOscillationFrequency(sock.m_lastOscillationFrequency)
{
}

TcpDo::~TcpDo() {}

std::string TcpDo::GetName() const
{
    return "TcpDo";
}

void TcpDo::PktsAcked(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time& rtt)
{
    TcpVegas::PktsAcked(tcb, segmentsAcked, rtt);
    CalculateOscillationFrequency(rtt);
}

void TcpDo::IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked)
{
    bool vegasDetectedCongestion = (tcb->m_cWnd.Get() > tcb->m_ssThresh);
    double currentOscillationFrequency = m_lastOscillationFrequency;
    bool frequencyDetectedCongestion = (currentOscillationFrequency > m_congestionThreshold);

    if (vegasDetectedCongestion || frequencyDetectedCongestion)
    {
        NS_LOG_INFO("Congestion detected by Vegas or Oscillation Frequency: Reducing cwnd");

        uint32_t newCwnd;
        if (frequencyDetectedCongestion)
        {
            double severity = currentOscillationFrequency / m_congestionThreshold;
            NS_LOG_UNCOND("Calculated severity of congestion: " << severity);

            // 혼잡이 심각할수록 더 많이 줄이기 위해 감소 인자를 조정
            double reductionFactor;
            if (severity > 20) {
                reductionFactor = 0.001;  // 매우 심각한 혼잡 - 거의 모든 대역폭을 차단
            } else if (severity > 10) {
                reductionFactor = 0.01;  // 심각한 혼잡 - 99% 감소
            } else {
                reductionFactor = 0.05;  // 중간 정도의 혼잡 - 95% 감소
            }

            // 새로운 cwnd를 계산, 최소 cwnd를 설정하여 과도한 감소 방지
            newCwnd = std::max(static_cast<uint32_t>(tcb->m_cWnd.Get() * reductionFactor), tcb->m_segmentSize * 10);
            NS_LOG_INFO("High oscillation frequency detected: Reducing cwnd by factor " << reductionFactor);
        }
        else
        {
            // Vegas에 의해 감지된 혼잡에 대해서는 기본적인 감소 처리
            newCwnd = std::max(tcb->m_cWnd.Get() / 2, tcb->m_segmentSize * 10);
            NS_LOG_INFO("Vegas detected congestion: Decreasing cwnd");
        }

        tcb->m_ssThresh = newCwnd;  // 혼잡 후 바로 선형 증가 모드로 진입
        tcb->m_cWnd = newCwnd;
        NS_LOG_INFO("Congestion detected, reducing cwnd to " << newCwnd << " and avoiding slow start");
    }
    else
    {
        NS_LOG_INFO("No congestion detected: Increasing cwnd");
        TcpVegas::IncreaseWindow(tcb, segmentsAcked);
    }
}

void TcpDo::CalculateOscillationFrequency(const Time& rtt)
{
    static Time lastRtt = Time(0);
    Time currentRtt = rtt;

    if (lastRtt != Time(0))
    {
        double rttChange = (currentRtt - lastRtt).GetSeconds();
        // 매우 작은 변동은 무시하고, 큰 변동만 반영
        if (std::abs(rttChange) > 0.001) 
        {
            m_lastOscillationFrequency = std::abs(rttChange);
        }
    }

    lastRtt = currentRtt;

    m_rttHistory.push_back(rtt);

    if (m_rttHistory.size() > 20) // 더 많은 샘플을 고려하여 진동 주파수 계산
    {
        m_rttHistory.pop_front();
    }

    if (m_rttHistory.size() < 2) return;

    double sum = 0.0;
    for (size_t i = 1; i < m_rttHistory.size(); ++i)
    {
        sum += std::abs(m_rttHistory[i].GetSeconds() - m_rttHistory[i - 1].GetSeconds());
    }

    m_lastOscillationFrequency = sum / (m_rttHistory.size() - 1);
}

} // namespace ns3
