import React, { useState, useEffect, useRef } from 'react';
import { io } from 'socket.io-client';
import { Header } from './components/Header';
import { MetricsBar } from './components/MetricsBar';
import { OrderBookLadder } from './components/OrderBookLadder';
import { DepthChart } from './components/DepthChart';
import { PriceSeriesChart } from './components/PriceSeriesChart';
import { TradesFeed } from './components/TradesFeed';

const DEFAULT_ASSET_ID = "95574512423985392683838234327660995166649195744297839352932371900160867086053";

export function App() {
  const [markets, setMarkets] = useState([]);
  const [selectedAssetId, setSelectedAssetId] = useState(DEFAULT_ASSET_ID);
  const [bookState, setBookState] = useState(null);
  const [priceHistory, setPriceHistory] = useState([]);
  const [trades, setTrades] = useState([]);
  const [engineConnected, setEngineConnected] = useState(false);
  const [totalUpdates, setTotalUpdates] = useState(0);

  const socketRef = useRef(null);

  // 1. Initial Load: Fetch markets
  useEffect(() => {
    fetch('/api/markets')
      .then((res) => res.json())
      .then((data) => {
        if (Array.isArray(data) && data.length > 0) {
          setMarkets(data);
        }
      })
      .catch((err) => console.warn('Failed fetching initial markets:', err));

    // Fetch initial trades
    fetch(`/api/trades/${selectedAssetId}`)
      .then((res) => res.json())
      .then((data) => {
        if (Array.isArray(data)) setTrades(data);
      })
      .catch(() => {});
  }, []);

  // 2. Fetch specific state when selected asset changes
  useEffect(() => {
    fetch(`/api/state/${selectedAssetId}`)
      .then((res) => res.json())
      .then((state) => {
        if (state && !state.error) {
          setBookState(state);
        }
      })
      .catch(() => {});

    // Clear history on market change
    setPriceHistory([]);
  }, [selectedAssetId]);

  // 3. Socket.IO Connection & Event Handlers
  useEffect(() => {
    const socket = io('/', { transports: ['websocket', 'polling'] });
    socketRef.current = socket;

    socket.on('connect', () => {
      setEngineConnected(true);
      socket.emit('subscribe', selectedAssetId);
    });

    socket.on('disconnect', () => {
      setEngineConnected(false);
    });

    socket.on('initial_state', (data) => {
      if (data?.markets) {
        setMarkets(data.markets);
      }
    });

    socket.on('engine_status', (status) => {
      setEngineConnected(Boolean(status?.connected));
    });

    socket.on('book_update', (msg) => {
      setTotalUpdates((prev) => prev + 1);

      if (msg.asset_id === selectedAssetId) {
        setBookState(msg);

        // Append to rolling price history
        if (msg.mid_price !== null && msg.best_bid !== null && msg.best_ask !== null) {
          const nowStr = new Date().toLocaleTimeString('en-US', { hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit' });
          setPriceHistory((prev) => {
            const next = [
              ...prev,
              {
                time: nowStr,
                bestBid: msg.best_bid,
                bestAsk: msg.best_ask,
                midPrice: msg.mid_price,
                microPrice: msg.micro_price || msg.mid_price
              }
            ];
            return next.length > 40 ? next.slice(-40) : next;
          });
        }
      }
    });

    socket.on('market_bbo_update', (bbo) => {
      setMarkets((prev) =>
        prev.map((m) =>
          m.asset_id === bbo.asset_id
            ? { ...m, ...bbo }
            : m
        )
      );
    });

    socket.on('trade', (tradeMsg) => {
      if (!tradeMsg.asset_id || tradeMsg.asset_id === selectedAssetId) {
        setTrades((prev) => [tradeMsg, ...prev].slice(0, 50));
      }
    });

    return () => {
      socket.disconnect();
    };
  }, [selectedAssetId]);

  const handleSelectAsset = (assetId) => {
    if (socketRef.current) {
      socketRef.current.emit('unsubscribe', selectedAssetId);
      socketRef.current.emit('subscribe', assetId);
    }
    setSelectedAssetId(assetId);
  };

  const handleTriggerFault = (type = 'cross') => {
    fetch('/api/simulate/fault', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ asset_id: selectedAssetId, type })
    }).catch((err) => console.error('Failed triggering fault:', err));
  };

  return (
    <div style={{ minHeight: '100vh', display: 'flex', flexDirection: 'column' }}>
      {/* Header Bar */}
      <Header
        markets={markets}
        selectedAssetId={selectedAssetId}
        onSelectAsset={handleSelectAsset}
        engineConnected={engineConnected}
        bookState={bookState?.state || 'AWAITING_FEED'}
        staleReason={bookState?.stale_reason || ''}
        totalUpdates={totalUpdates}
        onTriggerFault={handleTriggerFault}
      />

      {/* Quantitative Signal Metric Tiles */}
      <MetricsBar state={bookState} />

      {/* Main Trading Terminal Grid */}
      <div style={{
        flex: 1,
        padding: '0 24px 24px 24px',
        display: 'grid',
        gridTemplateColumns: '360px 1fr 340px',
        gap: '16px',
        alignItems: 'stretch'
      }}>
        {/* Left Column: L2 Orderbook Depth Ladder */}
        <div style={{ height: '660px' }}>
          <OrderBookLadder state={bookState} />
        </div>

        {/* Center Column: Depth & Price Charts */}
        <div style={{ display: 'flex', flexDirection: 'column', gap: '16px', height: '660px' }}>
          <div style={{ flex: 1 }}>
            <DepthChart state={bookState} />
          </div>
          <div style={{ flex: 1 }}>
            <PriceSeriesChart history={priceHistory} />
          </div>
        </div>

        {/* Right Column: Recent Trades Tape */}
        <div style={{ height: '660px' }}>
          <TradesFeed trades={trades} />
        </div>
      </div>
    </div>
  );
}
export default App;
