import React from 'react';
import { TrendingUp, ArrowDown, ArrowUp, BarChart2, Scale, Percent } from 'lucide-react';

export const MetricsBar = ({ state }) => {
  const bestBid = state?.best_bid ?? null;
  const bestAsk = state?.best_ask ?? null;
  const spread = state?.spread ?? null;
  const midPrice = state?.mid_price ?? null;
  const microPrice = state?.micro_price ?? null;
  const imbalance = state?.imbalance ?? 0;
  const totalBidQty = state?.total_bid_qty ?? 0;
  const totalAskQty = state?.total_ask_qty ?? 0;

  // Compute spread in basis points (bps)
  const spreadBps = (spread !== null && midPrice && midPrice > 0)
    ? Math.round((spread / midPrice) * 10000)
    : null;

  // Imbalance percentage: value between -1.0 (100% sell pressure) and +1.0 (100% buy pressure)
  const imbalancePct = Math.round(imbalance * 100);
  const buyPct = Math.max(0, Math.min(100, Math.round(((imbalance + 1) / 2) * 100)));

  return (
    <div style={{
      display: 'grid',
      gridTemplateColumns: 'repeat(auto-fit, minmax(180px, 1fr))',
      gap: '12px',
      padding: '16px 24px',
      backgroundColor: 'var(--bg-primary)'
    }}>
      {/* Best Bid Card */}
      <div style={{
        backgroundColor: 'var(--bg-card)',
        border: '1px solid var(--border)',
        borderRadius: '10px',
        padding: '14px',
        borderLeft: '4px solid var(--bid-green)'
      }}>
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', color: 'var(--text-muted)', fontSize: '11px', fontWeight: 600 }}>
          <span>BEST BID</span>
          <ArrowUp size={14} color="var(--bid-green)" />
        </div>
        <div className="mono" style={{ fontSize: '24px', fontWeight: 700, color: 'var(--bid-green)', marginTop: '4px' }}>
          {bestBid !== null ? `$${bestBid.toFixed(4)}` : '—'}
        </div>
        <div style={{ fontSize: '11px', color: 'var(--text-muted)', marginTop: '2px' }}>
          Depth: {totalBidQty.toLocaleString()} units
        </div>
      </div>

      {/* Best Ask Card */}
      <div style={{
        backgroundColor: 'var(--bg-card)',
        border: '1px solid var(--border)',
        borderRadius: '10px',
        padding: '14px',
        borderLeft: '4px solid var(--ask-red)'
      }}>
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', color: 'var(--text-muted)', fontSize: '11px', fontWeight: 600 }}>
          <span>BEST ASK</span>
          <ArrowDown size={14} color="var(--ask-red)" />
        </div>
        <div className="mono" style={{ fontSize: '24px', fontWeight: 700, color: 'var(--ask-red)', marginTop: '4px' }}>
          {bestAsk !== null ? `$${bestAsk.toFixed(4)}` : '—'}
        </div>
        <div style={{ fontSize: '11px', color: 'var(--text-muted)', marginTop: '2px' }}>
          Depth: {totalAskQty.toLocaleString()} units
        </div>
      </div>

      {/* Spread Card */}
      <div style={{
        backgroundColor: 'var(--bg-card)',
        border: '1px solid var(--border)',
        borderRadius: '10px',
        padding: '14px'
      }}>
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', color: 'var(--text-muted)', fontSize: '11px', fontWeight: 600 }}>
          <span>SPREAD</span>
          <Scale size={14} color="var(--accent)" />
        </div>
        <div className="mono" style={{ fontSize: '24px', fontWeight: 700, color: '#ffffff', marginTop: '4px' }}>
          {spread !== null ? `$${spread.toFixed(4)}` : '—'}
        </div>
        <div style={{ fontSize: '11px', color: spreadBps && spreadBps > 200 ? '#f59e0b' : 'var(--accent)', marginTop: '2px' }}>
          {spreadBps !== null ? `${spreadBps} bps` : '—'}
        </div>
      </div>

      {/* Mid Price Card */}
      <div style={{
        backgroundColor: 'var(--bg-card)',
        border: '1px solid var(--border)',
        borderRadius: '10px',
        padding: '14px'
      }}>
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', color: 'var(--text-muted)', fontSize: '11px', fontWeight: 600 }}>
          <span>MID PRICE</span>
          <TrendingUp size={14} color="#a855f7" />
        </div>
        <div className="mono" style={{ fontSize: '24px', fontWeight: 700, color: '#ffffff', marginTop: '4px' }}>
          {midPrice !== null ? `$${midPrice.toFixed(4)}` : '—'}
        </div>
        <div style={{ fontSize: '11px', color: 'var(--text-muted)', marginTop: '2px' }}>
          Arithmetic Mean
        </div>
      </div>

      {/* Micro Price Card */}
      <div style={{
        backgroundColor: 'var(--bg-card)',
        border: '1px solid var(--border)',
        borderRadius: '10px',
        padding: '14px'
      }}>
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', color: 'var(--text-muted)', fontSize: '11px', fontWeight: 600 }}>
          <span>MICRO PRICE</span>
          <Percent size={14} color="#38bdf8" />
        </div>
        <div className="mono" style={{ fontSize: '24px', fontWeight: 700, color: '#38bdf8', marginTop: '4px' }}>
          {microPrice !== null ? `$${microPrice.toFixed(4)}` : '—'}
        </div>
        <div style={{ fontSize: '11px', color: 'var(--text-muted)', marginTop: '2px' }}>
          Volume-Weighted Mid
        </div>
      </div>

      {/* Orderbook Imbalance Meter */}
      <div style={{
        backgroundColor: 'var(--bg-card)',
        border: '1px solid var(--border)',
        borderRadius: '10px',
        padding: '14px'
      }}>
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', color: 'var(--text-muted)', fontSize: '11px', fontWeight: 600 }}>
          <span>BOOK IMBALANCE</span>
          <BarChart2 size={14} color="#e2e8f0" />
        </div>
        <div className="mono" style={{ fontSize: '18px', fontWeight: 700, color: imbalancePct > 0 ? 'var(--bid-green)' : (imbalancePct < 0 ? 'var(--ask-red)' : '#ffffff'), marginTop: '4px' }}>
          {imbalancePct > 0 ? `+${imbalancePct}% Buy` : (imbalancePct < 0 ? `${Math.abs(imbalancePct)}% Sell` : 'Neutral')}
        </div>
        {/* Progress Ratio Bar */}
        <div style={{
          width: '100%',
          height: '6px',
          backgroundColor: 'var(--ask-red)',
          borderRadius: '3px',
          overflow: 'hidden',
          marginTop: '8px'
        }}>
          <div style={{
            width: `${buyPct}%`,
            height: '100%',
            backgroundColor: 'var(--bid-green)',
            transition: 'width 0.2s ease'
          }} />
        </div>
      </div>
    </div>
  );
};
