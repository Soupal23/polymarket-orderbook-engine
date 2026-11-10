import React, { useMemo } from 'react';
import { AreaChart, Area, XAxis, YAxis, Tooltip, ResponsiveContainer, ReferenceLine } from 'recharts';

export const DepthChart = ({ state }) => {
  const bids = state?.bids || [];
  const asks = state?.asks || [];
  const midPrice = state?.mid_price ?? null;

  const chartData = useMemo(() => {
    if (bids.length === 0 && asks.length === 0) return [];

    // Cumulative sum for bids (descending from best bid down)
    let bidSum = 0;
    const bidPoints = [];
    for (const [price, size] of bids) {
      bidSum += size;
      bidPoints.push({
        price: Number(price.toFixed(4)),
        bidVolume: bidSum,
        askVolume: null,
      });
    }
    // Reverse bids so prices are ascending on the x-axis
    bidPoints.reverse();

    // Cumulative sum for asks (ascending from best ask up)
    let askSum = 0;
    const askPoints = [];
    for (const [price, size] of asks) {
      askSum += size;
      askPoints.push({
        price: Number(price.toFixed(4)),
        bidVolume: null,
        askVolume: askSum,
      });
    }

    // Mid point anchor
    const midPoint = midPrice !== null ? [{
      price: Number(midPrice.toFixed(4)),
      bidVolume: bidPoints.length > 0 ? bidPoints[bidPoints.length - 1].bidVolume : 0,
      askVolume: 0
    }] : [];

    return [...bidPoints, ...midPoint, ...askPoints];
  }, [bids, asks, midPrice]);

  return (
    <div style={{
      backgroundColor: 'var(--bg-card)',
      border: '1px solid var(--border)',
      borderRadius: '10px',
      padding: '16px',
      height: '320px',
      display: 'flex',
      flexDirection: 'column'
    }}>
      <div style={{
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between',
        marginBottom: '12px'
      }}>
        <span style={{ fontSize: '13px', fontWeight: 700, letterSpacing: '0.04em', color: '#ffffff' }}>
          CUMULATIVE MARKET DEPTH
        </span>
        <div style={{ display: 'flex', alignItems: 'center', gap: '14px', fontSize: '11px', fontWeight: 600 }}>
          <span style={{ color: 'var(--bid-green)' }}>● Bids Cumulative</span>
          <span style={{ color: 'var(--ask-red)' }}>● Asks Cumulative</span>
        </div>
      </div>

      <div style={{ flex: 1, width: '100%', minHeight: 0 }}>
        {chartData.length === 0 ? (
          <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', height: '100%', color: 'var(--text-muted)', fontSize: '13px' }}>
            Awaiting orderbook depth updates...
          </div>
        ) : (
          <ResponsiveContainer width="100%" height="100%">
            <AreaChart data={chartData} margin={{ top: 10, right: 10, left: 10, bottom: 0 }}>
              <defs>
                <linearGradient id="bidGrad" x1="0" y1="0" x2="0" y2="1">
                  <stop offset="5%" stopColor="#10b981" stopOpacity={0.4} />
                  <stop offset="95%" stopColor="#10b981" stopOpacity={0.0} />
                </linearGradient>
                <linearGradient id="askGrad" x1="0" y1="0" x2="0" y2="1">
                  <stop offset="5%" stopColor="#f43f5e" stopOpacity={0.4} />
                  <stop offset="95%" stopColor="#f43f5e" stopOpacity={0.0} />
                </linearGradient>
              </defs>
              <XAxis
                dataKey="price"
                stroke="#64748b"
                tick={{ fontSize: 10, fill: '#64748b' }}
                tickFormatter={(p) => `$${p.toFixed(3)}`}
                domain={['dataMin', 'dataMax']}
                type="number"
              />
              <YAxis
                stroke="#64748b"
                tick={{ fontSize: 10, fill: '#64748b' }}
                tickFormatter={(v) => v >= 1000 ? `${(v / 1000).toFixed(0)}k` : v}
                orientation="right"
              />
              <Tooltip
                contentStyle={{
                  backgroundColor: '#181c28',
                  border: '1px solid #323852',
                  borderRadius: '6px',
                  fontSize: '11px',
                  color: '#ffffff'
                }}
                formatter={(val, name) => [val?.toLocaleString(), name === 'bidVolume' ? 'Bid Depth' : 'Ask Depth']}
                labelFormatter={(p) => `Price: $${Number(p).toFixed(4)}`}
              />
              {midPrice !== null && (
                <ReferenceLine
                  x={Number(midPrice.toFixed(4))}
                  stroke="#38bdf8"
                  strokeDasharray="3 3"
                  label={{ value: 'MID', fill: '#38bdf8', fontSize: 10, position: 'top' }}
                />
              )}
              <Area
                type="stepAfter"
                dataKey="bidVolume"
                stroke="#10b981"
                strokeWidth={2}
                fillOpacity={1}
                fill="url(#bidGrad)"
                isAnimationActive={false}
              />
              <Area
                type="stepBefore"
                dataKey="askVolume"
                stroke="#f43f5e"
                strokeWidth={2}
                fillOpacity={1}
                fill="url(#askGrad)"
                isAnimationActive={false}
              />
            </AreaChart>
          </ResponsiveContainer>
        )}
      </div>
    </div>
  );
};
