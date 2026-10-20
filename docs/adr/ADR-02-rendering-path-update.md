# ADR-02 갱신 — D3D11 + DirectComposition 렌더링 경로 (잠정 확정)

- 상태: **잠정 확정** (2026-10-13). 기준선 ADR-02 "기술 검증 필요" → P0 단축 검증 통과
- 관련: 기준선 ADR-02, 계획서 §3.2, D7, D14

## 결정

D3D11(`BGRA_SUPPORT`) + DComp composition swapchain(premultiplied alpha, flip sequential)을 기본 렌더링 경로로 유지한다. D2D/DirectWrite는 같은 Device에서 interop한다. 타이머는 hires(애니메이션 중에만 고해상도 Waitable Timer one-shot), 정지·숨김·잠금 시 이벤트 대기.

## 근거 (P0)

- 투명 합성·스프라이트·한글 텍스트·3D 큐브(깊이 버퍼)가 같은 경로에서 정상 출력(Desktop Duplication 캡처 확인)
- 1마리: 정지 CPU 0.003%, 30fps CPU 0.111%, Working Set 약 40MB (목표 ≤0.2% / ≤1% / ≤60MB)
- 정지 구간 Present 0, 숨김·잠금 시 Present 0·wake-up은 측정 샘플만
- hit-test는 ADR-09(방식 B)로 해결 가능

## 미확정

- 실제 Device Lost(TDR) 복구 — 보류(`dxcap -forcetdr` 미승인)
- 다른 GPU 벤더·하이브리드 GPU·Windows 10
- 정식(10분 × 3회) 측정
