import React from 'react';

export const OrderBookLadder = ({ state }) => {
  const bids = state?.bids || []; // [[price, size], ...]
  const asks = state?.asks || []; // [[price, size], ...]
  const spread = state?.spread ?? null;
  const midPrice = state?.mid_price ?? null;

  // Compute max cumulative size to scale depth bars
  let bidCumSum = 0;
  const bidRows = bids.slice(0, 10).map(([p, s]) => {
    bidCumSum += s;
    return { price: p, size: s, total: bidCumSum };
  });

  let askCumSum = 0;
  const askRows = asks.slice(0, 10).map(([p, s]) => {
    askCumSum += s;
    return { price: p, size: s, total: askCumSum };
  });

  const maxTotal = Math.max(bidCumSum, askCumSum, 1);

  // We display Asks reversed so Best Ask is at the bottom of the asks section
  const displayedAsks = [...askRows].reverse();

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
      {/* Title Header */}
      <div style={{
        padding: '12px 16px',
        borderBottom: '1px solid var(--border)',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between'
      }}>
        <span style={{ fontSize: '13px', fontWeight: 700, letterSpacing: '0.04em', color: '#ffffff' }}>
          ORDERBOOK L2 DEPTH
        </span>
        <span style={{ fontSize: '11px', color: 'var(--text-muted)' }}>
          Top 10 Levels
        </span>
      </div>

      {/* Column Headers */}
      <div className="mono" style={{
        display: 'grid',
        gridTemplateColumns: '1fr 1fr 1fr',
        padding: '8px 16px',
        fontSize: '11px',
        color: 'var(--text-muted)',
        borderBottom: '1px solid var(--border)',
        textAlign: 'right'
      }}>
        <span style={{ textAlign: 'left' }}>PRICE ($)</span>
        <span>SIZE</span>
        <span>TOTAL</span>
      </div>

      {/* Asks Ladder (Red) */}
      <div style={{ display: 'flex', flexDirection: 'column', gap: '1px', flex: 1, justifyContent: 'flex-end' }}>
        {displayedAsks.map((row, idx) => {
          const depthPct = Math.min(100, Math.round((row.total / maxTotal) * 100));
          return (
            <div
              key={`ask-${idx}`}
              className="mono"
              style={{
                position: 'relative',
                display: 'grid',
                gridTemplateColumns: '1fr 1fr 1fr',
                padding: '4px 16px',
                fontSize: '12px',
                textAlign: 'right'
              }}
            >
              {/* Depth background bar */}
              <div style={{
                position: 'absolute',
                top: 0,
                right: 0,
                bottom: 0,
                width: `${depthPct}%`,
                backgroundColor: 'rgba(244, 63, 94, 0.12)',
                zIndex: 0
              }} />

              <span style={{ textAlign: 'left', color: 'var(--ask-red)', fontWeight: 600, zIndex: 1 }}>
                ${row.price.toFixed(4)}
              </span>
              <span style={{ color: '#e2e8f0', zIndex: 1 }}>
                {row.size.toLocaleString(undefined, { maximumFractionDigits: 1 })}
              </span>
              <span style={{ color: 'var(--text-muted)', zIndex: 1 }}>
                {row.total.toLocaleString(undefined, { maximumFractionDigits: 1 })}
              </span>
            </div>
          );
        })}
      </div>

      {/* Spread Indicator Bar */}
      <div style={{
        padding: '8px 16px',
        backgroundColor: 'var(--bg-panel)',
        borderTop: '1px solid var(--border)',
        borderBottom: '1px solid var(--border)',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between',
        fontSize: '11px',
        fontWeight: 600
      }}>
        <div style={{ color: 'var(--text-muted)' }}>
          SPREAD: <strong className="mono" style={{ color: '#ffffff' }}>{spread !== null ? `$${spread.toFixed(4)}` : '—'}</strong>
        </div>
        <div style={{ color: 'var(--accent)' }}>
          MID: <strong className="mono">{midPrice !== null ? `$${midPrice.toFixed(4)}` : '—'}</strong>
        </div>
      </div>

      {/* Bids Ladder (Green) */}
      <div style={{ display: 'flex', flexDirection: 'column', gap: '1px', flex: 1 }}>
        {bidRows.map((row, idx) => {
          const depthPct = Math.min(100, Math.round((row.total / maxTotal) * 100));
          return (
            <div
              key={`bid-${idx}`}
              className="mono"
              style={{
                position: 'relative',
                display: 'grid',
                gridTemplateColumns: '1fr 1fr 1fr',
                padding: '4px 16px',
                fontSize: '12px',
                textAlign: 'right'
              }}
            >
              {/* Depth background bar */}
              <div style={{
                position: 'absolute',
                top: 0,
                right: 0,
                bottom: 0,
                width: `${depthPct}%`,
                backgroundColor: 'rgba(16, 185, 129, 0.12)',
                zIndex: 0
              }} />

              <span style={{ textAlign: 'left', color: 'var(--bid-green)', fontWeight: 600, zIndex: 1 }}>
                ${row.price.toFixed(4)}
              </span>
              <span style={{ color: '#e2e8f0', zIndex: 1 }}>
                {row.size.toLocaleString(undefined, { maximumFractionDigits: 1 })}
              </span>
              <span style={{ color: 'var(--text-muted)', zIndex: 1 }}>
                {row.total.toLocaleString(undefined, { maximumFractionDigits: 1 })}
              </span>
            </div>
          );
        })}
      </div>
    </div>
  );
};
