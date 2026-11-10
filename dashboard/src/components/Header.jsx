import React from 'react';
import { Activity, ShieldAlert, CheckCircle, RefreshCw, Radio } from 'lucide-react';

export const Header = ({
  markets,
  selectedAssetId,
  onSelectAsset,
  engineConnected,
  bookState,
  staleReason,
  totalUpdates,
  onTriggerFault
}) => {
  const getBadgeConfig = () => {
    if (!engineConnected) {
      return {
        label: 'ENGINE OFFLINE',
        bg: '#451a1a',
        border: '#b91c1c',
        text: '#f87171',
        icon: ShieldAlert,
        pulsing: false
      };
    }
    switch (bookState) {
      case 'LIVE':
        return {
          label: 'FEED LIVE',
          bg: '#064e3b',
          border: '#059669',
          text: '#34d399',
          icon: CheckCircle,
          pulsing: true
        };
      case 'STALE':
        return {
          label: 'FEED STALE',
          bg: '#78350f',
          border: '#d97706',
          text: '#fbbf24',
          icon: ShieldAlert,
          pulsing: true
        };
      case 'REBUILDING':
        return {
          label: 'REBUILDING STATE',
          bg: '#312e81',
          border: '#6366f1',
          text: '#a5b4fc',
          icon: RefreshCw,
          pulsing: true
        };
      default:
        return {
          label: 'CONNECTING',
          bg: '#1e293b',
          border: '#475569',
          text: '#94a3b8',
          icon: Activity,
          pulsing: false
        };
    }
  };

  const badge = getBadgeConfig();
  const Icon = badge.icon;

  return (
    <header style={{
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'space-between',
      padding: '16px 24px',
      backgroundColor: 'var(--bg-card)',
      borderBottom: '1px solid var(--border)',
      flexWrap: 'wrap',
      gap: '16px'
    }}>
      {/* Brand & Subtitle */}
      <div style={{ display: 'flex', alignItems: 'center', gap: '14px' }}>
        <div style={{
          width: '38px',
          height: '38px',
          borderRadius: '10px',
          background: 'linear-gradient(135deg, #38bdf8 0%, #0284c7 100%)',
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          boxShadow: '0 0 16px rgba(56, 189, 248, 0.3)'
        }}>
          <Radio size={22} color="#ffffff" />
        </div>
        <div>
          <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
            <h1 style={{ fontSize: '18px', fontWeight: 800, letterSpacing: '-0.02em', color: '#ffffff' }}>
              POLYMARKET CLOB ENGINE
            </h1>
            <span style={{
              fontSize: '10px',
              fontWeight: 700,
              padding: '2px 6px',
              borderRadius: '4px',
              backgroundColor: '#1e293b',
              color: '#38bdf8',
              letterSpacing: '0.05em'
            }}>
              C++17 + L2 IPC
            </span>
          </div>
          <p style={{ fontSize: '12px', color: 'var(--text-muted)' }}>
            Real-Time Deterministic Orderbook & Self-Healing Pipeline
          </p>
        </div>
      </div>

      {/* Middle: Market Selector */}
      <div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
        <label style={{ fontSize: '12px', fontWeight: 600, color: 'var(--text-muted)' }}>
          MARKET:
        </label>
        <select
          value={selectedAssetId}
          onChange={(e) => onSelectAsset(e.target.value)}
          style={{
            backgroundColor: 'var(--bg-panel)',
            color: '#ffffff',
            border: '1px solid var(--border)',
            borderRadius: '8px',
            padding: '8px 14px',
            fontSize: '13px',
            fontWeight: 600,
            cursor: 'pointer',
            minWidth: '320px',
            outline: 'none'
          }}
        >
          {markets.map((m) => (
            <option key={m.asset_id} value={m.asset_id}>
              {m.metadata?.candidate || 'Token'} ({m.metadata?.outcome || 'Outcome'}) — {m.metadata?.marketName || 'Polymarket'}
            </option>
          ))}
        </select>
      </div>

      {/* Right Controls: State Badge & Fault Trigger */}
      <div style={{ display: 'flex', alignItems: 'center', gap: '14px' }}>
        {/* State Badge */}
        <div style={{
          display: 'flex',
          alignItems: 'center',
          gap: '8px',
          padding: '6px 12px',
          borderRadius: '8px',
          backgroundColor: badge.bg,
          border: `1px solid ${badge.border}`,
          color: badge.text,
          fontSize: '12px',
          fontWeight: 700,
          letterSpacing: '0.04em'
        }}>
          <Icon size={14} className={badge.pulsing ? 'animate-pulse' : ''} />
          <span>{badge.label}</span>
        </div>

        {/* Total Updates Counter */}
        <div className="mono" style={{
          fontSize: '11px',
          color: 'var(--text-muted)',
          backgroundColor: 'var(--bg-panel)',
          padding: '6px 10px',
          borderRadius: '6px',
          border: '1px solid var(--border)'
        }}>
          {totalUpdates.toLocaleString()} updates
        </div>

        {/* Interactive Self-Healing Demo Trigger */}
        <button
          onClick={() => onTriggerFault('cross')}
          title="Simulate crossed book desync to showcase C++ engine invariant detection and self-healing recovery"
          style={{
            backgroundColor: '#271c38',
            color: '#c084fc',
            border: '1px solid #7e22ce',
            padding: '7px 13px',
            borderRadius: '8px',
            fontSize: '11px',
            fontWeight: 700,
            cursor: 'pointer',
            transition: 'all 0.15s ease'
          }}
          onMouseOver={(e) => { e.currentTarget.style.backgroundColor = '#3b1f5c'; }}
          onMouseOut={(e) => { e.currentTarget.style.backgroundColor = '#271c38'; }}
        >
          ⚡ Inject Desync Fault
        </button>
      </div>

      {/* Stale reason alert bar if applicable */}
      {bookState === 'STALE' && staleReason && (
        <div style={{
          width: '100%',
          backgroundColor: '#451a03',
          border: '1px solid #b45309',
          color: '#fef3c7',
          padding: '8px 14px',
          borderRadius: '6px',
          fontSize: '12px',
          fontWeight: 500,
          display: 'flex',
          alignItems: 'center',
          gap: '8px'
        }}>
          <ShieldAlert size={16} color="#fbbf24" />
          <span><strong>Desync Detected:</strong> {staleReason}. Engine entering <code>STALE</code> state; awaiting REST recovery snapshot...</span>
        </div>
      )}
    </header>
  );
};
