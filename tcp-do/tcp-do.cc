#include "tcp-do.h"
#include "ns3/log.h"
#include <algorithm>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TcpDo");
NS_OBJECT_ENSURE_REGISTERED (TcpDo);

TypeId
TcpDo::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TcpDo")
    .SetParent<TcpCongestionOps> ()
    .SetGroupName ("Internet")
    .AddConstructor<TcpDo> ();
  return tid;
}

TcpDo::TcpDo (void)
  : m_prevRtt(0.0),
    m_oscillationThreshold(0.05),
    m_congestionWindowDelta(0.0)
{
}

TcpDo::~TcpDo (void)
{
}

std::string
TcpDo::GetName () const
{
  return "TcpDo";
}

void
TcpDo::IncreaseWindow (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked)
{
  // 현재 RTT 측정
  double currentRtt = tcb->m_lastRtt.Get().GetSeconds(); // RTT 값을 가져오는 방법 수정

  // RTT 오실레이션 감지
  m_congestionWindowDelta = currentRtt - m_prevRtt;

  // 오실레이션이 임계값을 초과하면 혼잡으로 간주
  if (std::abs(m_congestionWindowDelta) > m_oscillationThreshold)
    {
      // 혼잡 발생 시 창 크기를 줄임
      tcb->m_cWnd = std::max (tcb->m_cWnd.Get() * 0.9, double(tcb->m_segmentSize * 2));
    }
  else
    {
      // 혼잡이 발생하지 않은 경우 창 크기를 증가
      tcb->m_cWnd = tcb->m_cWnd.Get() + tcb->m_segmentSize * segmentsAcked;
    }

  // 이전 RTT 값 갱신
  m_prevRtt = currentRtt;
}

uint32_t
TcpDo::GetSsThresh (Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight)
{
  // 혼잡 발생 시 ssthresh를 설정하는 방법 - 일단 절반으로 설정
  return std::max (2 * tcb->m_segmentSize, tcb->m_cWnd.Get() / 2);
}

Ptr<TcpCongestionOps>
TcpDo::Fork ()
{
  return CreateObject<TcpDo> ();
}

} // namespace ns3
