import React from 'react';
import { LineChart, Line, XAxis, YAxis, Tooltip, ResponsiveContainer, Legend } from 'recharts';

export const PriceSeriesChart = ({ history }) => {
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
          PRICE DYNAMICS (BBO & MID)
        </span>
        <span style={{ fontSize: '11px', color: 'var(--text-muted)' }}>
          Last {history.length} ticks
        </span>
      </div>

      <div style={{ flex: 1, width: '100%', minHeight: 0 }}>
        {history.length === 0 ? (
          <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', height: '100%', color: 'var(--text-muted)', fontSize: '13px' }}>
            Streaming price ticks...
          </div>
        ) : (
          <ResponsiveContainer width="100%" height="100%">
            <LineChart data={history} margin={{ top: 10, right: 10, left: 10, bottom: 0 }}>
              <XAxis
                dataKey="time"
                stroke="#64748b"
                tick={{ fontSize: 10, fill: '#64748b' }}
              />
              <YAxis
                stroke="#64748b"
                tick={{ fontSize: 10, fill: '#64748b' }}
                tickFormatter={(v) => `$${Number(v).toFixed(3)}`}
                domain={['auto', 'auto']}
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
                formatter={(val) => [`$${Number(val).toFixed(4)}`]}
              />
              <Legend wrapperStyle={{ fontSize: '11px', paddingTop: '6px' }} />
              <Line
                type="monotone"
                dataKey="bestBid"
                name="Best Bid"
                stroke="#10b981"
                strokeWidth={1.5}
                dot={false}
                isAnimationActive={false}
              />
              <Line
                type="monotone"
                dataKey="midPrice"
                name="Mid Price"
                stroke="#c084fc"
                strokeWidth={2}
                dot={false}
                isAnimationActive={false}
              />
              <Line
                type="monotone"
                dataKey="microPrice"
                name="Micro Price"
                stroke="#38bdf8"
                strokeWidth={1.5}
                strokeDasharray="4 2"
                dot={false}
                isAnimationActive={false}
              />
              <Line
                type="monotone"
                dataKey="bestAsk"
                name="Best Ask"
                stroke="#f43f5e"
                strokeWidth={1.5}
                dot={false}
                isAnimationActive={false}
              />
            </LineChart>
          </ResponsiveContainer>
        )}
      </div>
    </div>
  );
};
