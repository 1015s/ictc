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
                      DoubleValue(0.001), // 기본 임계값 설정
                      MakeDoubleAccessor(&TcpDo::m_congestionThreshold),
                      MakeDoubleChecker<double>());
    return tid;
}

TcpDo::TcpDo()
    : m_congestionThreshold(0.001),  // 초기화
      m_lastOscillationFrequency(0.0), // 초기화
      m_maxRttHistorySize(20), // 초기화
      m_timeWindow(Seconds(0.01)), // 초기화
      m_baseRtt(Time(0.0)),
      m_lastCalculationTime(Time(0.0)) // 최소 RTT 및 마지막 계산 시간 초기화
{
}

TcpDo::TcpDo(const TcpDo& sock)
    : TcpVegas(sock),
      m_congestionThreshold(sock.m_congestionThreshold),
      m_lastOscillationFrequency(sock.m_lastOscillationFrequency),
      m_oscillationCount(sock.m_oscillationCount),
      m_timeWindow(sock.m_timeWindow),
      m_baseRtt(sock.m_baseRtt),
      m_lastCalculationTime(sock.m_lastCalculationTime) // 복사 생성자에서 최소 RTT와 마지막 계산 시간 초기화
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

    // 최소 RTT 갱신
    if (m_baseRtt == Time(0.0) || rtt < m_baseRtt)
    {
        m_baseRtt = rtt;
    }

    CalculateOscillationFrequency(rtt);
}

void TcpDo::IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked)
{
    static double lastOscillationFrequency = m_lastOscillationFrequency; // 마지막 계산된 진동수를 캐시

    // 별도의 타이머를 사용하여 비교
    static Time lastIncreaseTime = Time(0);

    // 진동수 계산 주기가 지나면 값을 갱신
    Time now = Simulator::Now();
    if (now - lastIncreaseTime >= m_timeWindow)
    {
        lastOscillationFrequency = m_lastOscillationFrequency;
        lastIncreaseTime = now; // 마지막 계산 시간 갱신

        // NS_LOG_UNCOND("Oscillation Frequency updated in IncreaseWindow: " << lastOscillationFrequency);
    }


    bool vegasDetectedCongestion = (tcb->m_cWnd.Get() > tcb->m_ssThresh);
    double currentOscillationFrequency = lastOscillationFrequency; // 이전 계산된 진동수 사용
    bool frequencyDetectedCongestion = (currentOscillationFrequency > m_congestionThreshold);
    double currentRtt = tcb->m_lastRtt.Get().GetSeconds(); // 현재 RTT 가져오기
    double maxRttThreshold = m_baseRtt.GetSeconds() * 1.2; // 최소 RTT에 기반한 동적 임계값

    if (vegasDetectedCongestion || frequencyDetectedCongestion || currentRtt > maxRttThreshold)
    {
        NS_LOG_INFO("Congestion detected by Vegas, Oscillation Frequency, or High RTT: Reducing cwnd");

        uint32_t newCwnd;
        double severity = currentOscillationFrequency / m_congestionThreshold;

        if (vegasDetectedCongestion || currentRtt > maxRttThreshold) 
        {
            // Vegas나 RTT 기반 혼잡 감지의 경우 약간 덜 공격적으로 감소
            double reductionFactor = std::max(0.8, 1.0 - severity * 0.05);  // 혼잡이 심할수록 줄임 (최소 70%)
            newCwnd = std::max(static_cast<uint32_t>(tcb->m_cWnd.Get() * reductionFactor), tcb->m_segmentSize * 10);
        } 
        else if (frequencyDetectedCongestion) 
        {
            // 진동수 기반 혼잡 감지의 경우 더 강하게 감소
            double reductionFactor = std::max(0.7, 1.0 - severity * 0.15);  // 혼잡이 심할수록 더 줄임 (최소 50%)
            newCwnd = std::max(static_cast<uint32_t>(tcb->m_cWnd.Get() * reductionFactor), tcb->m_segmentSize * 10);
        }

        // 혼잡 후 빠르게 회복하기 위해 임계값을 일시적으로 증가
        m_congestionThreshold *= std::min(2.0, 1.0 + severity * 0.2);

        tcb->m_ssThresh = newCwnd;  // 혼잡 후 바로 선형 증가 모드로 진입
        tcb->m_cWnd = newCwnd;
    }
    else
    {
        NS_LOG_INFO("No congestion detected: Increasing cwnd cautiously");

        if (currentOscillationFrequency == 0.0)
        {
            tcb->m_cWnd += tcb->m_segmentSize * 15;  // 더 공격적인 변동 유도
            m_congestionThreshold *= 0.9;
            NS_LOG_INFO("Reducing congestion threshold temporarily to induce change");
        }

        double alpha = 1.0; 
        double beta = 3.0;  

        double diff = (tcb->m_cWnd.Get() - tcb->m_ssThresh.Get()) / tcb->m_segmentSize;

        if (diff < alpha)
        {
            tcb->m_cWnd += tcb->m_segmentSize * 7;  
            NS_LOG_INFO("Minimal congestion detected: Slowly increasing cwnd by four segments");
        }
        else if (diff > beta)
        {
            tcb->m_cWnd -= tcb->m_segmentSize; 
            NS_LOG_INFO("Heavy congestion detected: Decreasing cwnd by one segment");
        }
        else
        {
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

    // RTT 변화 감지
    if (lastRtt != Time(0))
    {
        double rttChange = (currentRtt - lastRtt).GetSeconds();
        if (std::abs(rttChange) > 0.0001) 
        {
            // 변화가 발생할 때마다 진동 수를 증가
            m_oscillationCount++;
        }
    }

    lastRtt = currentRtt;

    // 일정 시간 창(window) 동안의 진동 수를 계산
    Time now = Simulator::Now();
    if (now - m_lastCalculationTime >= m_timeWindow)
    {
        // 가중치를 적용한 진동수 계산
        double weightedOscillationSum = 0.0;
        double weightTotal = 0.0;
        double weight = 1.0;
        double weightIncrement = 0.1;

        for (auto it = m_rttHistory.rbegin(); it != m_rttHistory.rend(); ++it)
        {
            double rttChange = std::abs((currentRtt - *it).GetSeconds());
            if (rttChange > 0.0001) 
            {
                weightedOscillationSum += rttChange * weight;
                weightTotal += weight;
                weight += weightIncrement;
            }
        }

        m_lastOscillationFrequency = weightTotal > 0 ? weightedOscillationSum / weightTotal : 0;

        // 다음 계산을 위해 초기화
        m_oscillationCount = 0;
        m_lastCalculationTime = now;
    }

    m_rttHistory.push_back(rtt);
    if (m_rttHistory.size() > m_maxRttHistorySize) 
    {
        m_rttHistory.pop_front();
    }

    // 혼잡 감지 시 샘플 크기 증가, 안정적인 경우 샘플 크기 감소
    if (m_lastOscillationFrequency > m_congestionThreshold)
    {
        m_maxRttHistorySize = std::min(static_cast<size_t>(50), m_maxRttHistorySize + 1); 
    }
    else
    {
        m_maxRttHistorySize = std::max(static_cast<size_t>(10), m_maxRttHistorySize - 1); 
    }
}


} // namespace ns3
