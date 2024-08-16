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
                      DoubleValue(0.01), // 임계값을 설정하여 민감도 조정
                      MakeDoubleAccessor(&TcpDo::m_congestionThreshold),
                      MakeDoubleChecker<double>());
    return tid;
}

TcpDo::TcpDo()
    : m_congestionThreshold(0.01),  // 초기화
      m_lastOscillationFrequency(0.0), // 초기화
      m_maxRttHistorySize(20) // 초기화
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
    double currentRtt = tcb->m_lastRtt.Get().GetSeconds(); // 현재 RTT 가져오기
    double maxRttThreshold = 0.15; // 최대 RTT 임계값 설정 

    if (vegasDetectedCongestion || frequencyDetectedCongestion || currentRtt > maxRttThreshold)
    {
        NS_LOG_INFO("Congestion detected by Vegas, Oscillation Frequency, or High RTT: Reducing cwnd");

        uint32_t newCwnd;
        double severity = currentOscillationFrequency / m_congestionThreshold;
        NS_LOG_UNCOND("Calculated severity of congestion: " << severity);

        // 혼잡 상황에서의 윈도우 크기 감소 및 복구 전략
        if (frequencyDetectedCongestion || vegasDetectedCongestion)
        {
            double reductionFactor = std::max(0.5, 1.0 - severity * 0.05);  // 혼잡이 심할수록 더 줄임 (최소 50%)
            double recoveryFactor = std::min(3.0, 1.0 + severity * 0.1);   // 혼잡 해소 후 빠르게 회복

            newCwnd = std::max(static_cast<uint32_t>(tcb->m_cWnd.Get() * reductionFactor), tcb->m_segmentSize * 10);
            NS_LOG_INFO("Adjusted cwnd by factor " << reductionFactor << " due to congestion");

            // 혼잡 후 빠르게 회복하기 위해 임계값을 일시적으로 증가
            m_congestionThreshold *= recoveryFactor;
            NS_LOG_INFO("Temporarily increasing congestion threshold for faster recovery");
        }
        else
        {
            // Vegas에 의해 감지된 혼잡 또는 높은 RTT에 대한 기본적인 감소 처리
            // 감소폭을 줄여서 혼잡 감지 후에도 적절한 대역폭 사용 유지
            newCwnd = std::max(static_cast<uint32_t>(tcb->m_cWnd.Get() * 0.8), tcb->m_segmentSize * 30);
            NS_LOG_UNCOND("Vegas detected congestion or high RTT detected: Decreasing cwnd less aggressively");

            // 혼잡 후 회복 속도를 증가시킴
            m_congestionThreshold *= 1.2;
            NS_LOG_INFO("Increasing congestion threshold for faster recovery");
        }

        tcb->m_ssThresh = newCwnd;  // 혼잡 후 바로 선형 증가 모드로 진입
        tcb->m_cWnd = newCwnd;
        NS_LOG_UNCOND("Congestion detected, reducing cwnd to " << newCwnd << " and avoiding slow start");
    }
    else
    {
        NS_LOG_INFO("No congestion detected: Increasing cwnd cautiously");

        // 혼잡이 없는 경우에도 RTT가 변동하지 않으면 적극적으로 증가를 유도
        if (currentOscillationFrequency == 0.0) 
        {
            tcb->m_cWnd += tcb->m_segmentSize * 6;  // 더 공격적인 변동 유도
            NS_LOG_UNCOND("No oscillation detected: Aggressively increasing cwnd to induce change");

            // 혼잡 감지 임계값을 일시적으로 낮추어 변화를 유도
            m_congestionThreshold *= 0.95;
            NS_LOG_INFO("Reducing congestion threshold temporarily to induce change");
        }

        // Vegas의 혼잡 회피 방법을 유지하면서, RTT에 민감하게 반응
        double alpha = 1.0; // 혼잡 회피를 위한 최소 RTT 차이
        double beta = 3.0;  // 혼잡 감지를 위한 최대 RTT 차이

        double diff = (tcb->m_cWnd.Get() - tcb->m_ssThresh.Get()) / tcb->m_segmentSize;

        if (diff < alpha)
        {
            // 혼잡이 없다고 판단되면 더 천천히 증가
            tcb->m_cWnd += tcb->m_segmentSize * 4;  // 증가폭 완화
            NS_LOG_INFO("Minimal congestion detected: Slowly increasing cwnd by four segments");
        }
        else if (diff > beta)
        {
            // 혼잡이 감지되면 cwnd를 더욱 줄임
            tcb->m_cWnd -= tcb->m_segmentSize; 
            NS_LOG_INFO("Heavy congestion detected: Decreasing cwnd by one segment");
        }
        else
        {
            // 신중하게 윈도우를 증가
            uint32_t maxIncrease = std::max(1U, tcb->m_cWnd.Get() / 2);
            tcb->m_cWnd = std::min(tcb->m_cWnd.Get() + maxIncrease, tcb->m_ssThresh.Get());
            NS_LOG_INFO("Moderate congestion detected: Increasing cwnd moderately");
        }
    }
}



void TcpDo::CalculateOscillationFrequency(const Time& rtt)
{
    static Time lastRtt = Time(0);
    Time currentRtt = rtt;

    if (lastRtt != Time(0))
    {
        double rttChange = (currentRtt - lastRtt).GetSeconds();
        if (std::abs(rttChange) > 0.001) 
        {
            m_lastOscillationFrequency = std::abs(rttChange);
        }
    }

    lastRtt = currentRtt;

    // 혼잡 감지 시 샘플 크기 증가, 안정적인 경우 샘플 크기 감소
    if (m_lastOscillationFrequency > m_congestionThreshold)
    {
        m_maxRttHistorySize = std::min(static_cast<size_t>(50), m_maxRttHistorySize + 1); // 혼잡 시 샘플 크기를 50까지 증가
    }
    else
    {
        m_maxRttHistorySize = std::max(static_cast<size_t>(10), m_maxRttHistorySize - 1); // 안정 시 샘플 크기를 10까지 감소
    }

    m_rttHistory.push_back(rtt);
    
    if (m_rttHistory.size() > m_maxRttHistorySize) 
    {
        m_rttHistory.pop_front();
    }

    if (m_rttHistory.size() < 2) return;

    double weightedSum = 0.0;
    double weightTotal = 0.0;
    double weight = 1.0;
    double weightIncrement = 0.1;

    for (auto it = m_rttHistory.rbegin(); it != m_rttHistory.rend(); ++it)
    {
        weightedSum += it->GetSeconds() * weight;
        weightTotal += weight;
        weight += weightIncrement;
    }

    double weightedAverageRtt = weightedSum / weightTotal;
    m_lastOscillationFrequency = std::abs(currentRtt.GetSeconds() - weightedAverageRtt);
}



} // namespace ns3
