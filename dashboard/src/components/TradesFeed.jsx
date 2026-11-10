import React from 'react';

export const TradesFeed = ({ trades }) => {
  return (
    <div style={{
      backgroundColor: 'var(--bg-card)',
      border: '1px solid var(--border)',
      borderRadius: '10px',
      overflow: 'hidden',
      display: 'flex',
      flexDirection: 'column',
      height: '100%'
    }}>
      <div style={{
        padding: '12px 16px',
        borderBottom: '1px solid var(--border)',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between'
      }}>
        <span style={{ fontSize: '13px', fontWeight: 700, letterSpacing: '0.04em', color: '#ffffff' }}>
          TRADE EXECUTIONS
        </span>
        <span style={{ fontSize: '11px', color: 'var(--text-muted)' }}>
          Real-Time Tape
        </span>
      </div>

      <div className="mono" style={{
        display: 'grid',
        gridTemplateColumns: '1fr 1fr 1fr 1fr',
        padding: '8px 16px',
        fontSize: '11px',
        color: 'var(--text-muted)',
        borderBottom: '1px solid var(--border)',
        textAlign: 'right'
      }}>
        <span style={{ textAlign: 'left' }}>TIME</span>
        <span style={{ textAlign: 'center' }}>SIDE</span>
        <span>PRICE</span>
        <span>SIZE</span>
      </div>

      <div style={{
        flex: 1,
        overflowY: 'auto',
        display: 'flex',
        flexDirection: 'column',
        gap: '2px',
        padding: '6px 0'
      }}>
        {trades.length === 0 ? (
          <div style={{ padding: '24px', textAlign: 'center', color: 'var(--text-muted)', fontSize: '12px' }}>
            Awaiting market executions...
          </div>
        ) : (
          trades.map((t, idx) => {
            const isBuy = t.side === 'BUY';
            const timeStr = t.timestamp_ms
              ? new Date(t.timestamp_ms).toLocaleTimeString()
              : new Date().toLocaleTimeString();

            return (
              <div
                key={`trade-${idx}`}
                className="mono"
                style={{
                  display: 'grid',
                  gridTemplateColumns: '1fr 1fr 1fr 1fr',
                  padding: '4px 16px',
                  fontSize: '12px',
                  textAlign: 'right',
                  alignItems: 'center'
                }}
              >
                <span style={{ textAlign: 'left', color: 'var(--text-muted)', fontSize: '11px' }}>
                  {timeStr}
                </span>
                <span style={{
                  textAlign: 'center',
                  fontWeight: 700,
                  fontSize: '10px',
                  padding: '2px 6px',
                  borderRadius: '4px',
                  backgroundColor: isBuy ? 'rgba(16, 185, 129, 0.15)' : 'rgba(244, 63, 94, 0.15)',
                  color: isBuy ? 'var(--bid-green)' : 'var(--ask-red)',
                  justifySelf: 'center'
                }}>
                  {t.side}
                </span>
                <span style={{ color: isBuy ? 'var(--bid-green)' : 'var(--ask-red)', fontWeight: 600 }}>
                  ${Number(t.price).toFixed(4)}
                </span>
                <span style={{ color: '#e2e8f0' }}>
                  {Number(t.size).toLocaleString(undefined, { maximumFractionDigits: 1 })}
                </span>
              </div>
            );
          })
        )}
      </div>
    </div>
  );
};
