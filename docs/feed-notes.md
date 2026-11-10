# Polymarket Feed & Protocol Analysis

This document captures verified specifications, payload schemas, and behavioral nuances of the Polymarket Central Limit Order Book (CLOB) and Gamma Market discovery APIs.

---

## 1. Official Documentation Sources Cited

- **Polymarket CLOB API Documentation**: `https://docs.polymarket.com`
- **CLOB REST Endpoint Specification**: `https://clob.polymarket.com/book`
- **CLOB Market WebSocket Channel**: `wss://ws-subscriptions-clob.polymarket.com/ws/market`
- **Gamma Discovery REST API**: `https://gamma-api.polymarket.com/events` & `https://gamma-api.polymarket.com/markets`

---

## 2. WebSocket Connection & Subscription

- **Endpoint**: `wss://ws-subscriptions-clob.polymarket.com/ws/market`
- **Authentication**: None required for public market data streaming.
- **Heartbeat (Ping/Pong)**: The client should handle unsolicited `PING` frames or periodically send `PONG` / keepalive frames to maintain persistent socket connections across proxy timeouts (standard TCP keepalive / 30s interval).

### Subscription Payload
Clients subscribe to market feeds using specific outcome token IDs (**`assets_ids`**—a 77-digit string corresponding to the Conditional Token outcome):

```json
{
  "assets_ids": [
    "54533043819946592547517511176940999955633860128497669742211153063842200957669",
    "87854174148074652060467921081181402357467303721471806610111179101805869578687"
  ],
  "type": "market",
  "custom_feature_enabled": true
}
```
*Note*: Notice the key name is explicitly pluralized as `"assets_ids"`.

---

## 3. Streaming Message Types & Schemas

### Type A: Orderbook Snapshot (`event_type: "book"`)
Sent immediately upon channel subscription or triggered on full state resynchronization.

```json
{
  "event_type": "book",
  "asset_id": "54533043819946592547517511176940999955633860128497669742211153063842200957669",
  "market": "0x0f49db97f71c68b1e42a6d16e3de93d85dbf7d4148e3f018eb79e88554be9f75",
  "bids": [
    { "price": "0.089", "size": "13558.89" },
    { "price": "0.088", "size": "2437.5" }
  ],
  "asks": [
    { "price": "0.090", "size": "5620.47" },
    { "price": "0.091", "size": "3977.0" }
  ],
  "hash": "14d73940a8469eba898e27f0f08eec78a1d72a71",
  "timestamp": "1794289780044"
}
```

### Type B: Incremental Price Level Update (`event_type: "price_change"`)
Streams real-time updates whenever orders rest, get filled, or cancel.

```json
{
  "event_type": "price_change",
  "market": "0x0f49db97f71c68b1e42a6d16e3de93d85dbf7d4148e3f018eb79e88554be9f75",
  "timestamp": "1794289780100",
  "price_changes": [
    {
      "asset_id": "54533043819946592547517511176940999955633860128497669742211153063842200957669",
      "price": "0.089",
      "size": "15000.0",
      "side": "BUY",
      "hash": "0xabc1",
      "best_bid": "0.089",
      "best_ask": "0.090"
    }
  ]
}
```

### Type C: Last Trade / Execution (`event_type: "last_trade_price"`)
Broadcasts the price and size of executed trades.

```json
{
  "event_type": "last_trade_price",
  "market": "0x0f49db97f71c68b1e42a6d16e3de93d85dbf7d4148e3f018eb79e88554be9f75",
  "asset_id": "54533043819946592547517511176940999955633860128497669742211153063842200957669",
  "price": "0.090",
  "size": "500.0",
  "side": "BUY",
  "timestamp": "1794289780105"
}
```

---

## 4. Encoding, Semantics & Invariants

1. **Price Encoding**:
   - Represented as string decimals between `"0.000"` and `"1.000"`.
   - Tick sizes vary by market: typically `0.01` (1 cent) or `0.001` (0.1 cent).
   - In our engine, prices are parsed directly into integer ticks via scale factor $10^4$ (4 decimal fixed point: `0.089` $\rightarrow$ `890` ticks). This completely prevents floating-point inaccuracy.
2. **Size Encoding & Semantics**:
   - Represented as string numbers (e.g., `"13558.89"`).
   - **Replacement Size (Not Delta)**: The `size` in a `price_change` event is the **new aggregate volume** at that price level.
   - **Level Deletion**: If `size == "0"` or `0.0`, the price level is depleted and must be pruned from the orderbook map.
3. **Sequence Numbers & Hashes**:
   - Polymarket does **not** provide monotonically incrementing integer sequence numbers (like Binance or Coinbase `sequence`).
   - Instead, Polymarket provides:
     - `hash`: A cryptographic state hash (e.g. SHA-1/keccak digest) representing the book state.
     - `timestamp`: Epoch millisecond timestamp string.
   - **Implication for Desync Detection**: In Phase 3, gap and desync detection must use:
     a) Timestamp monotonicity checks (detecting backwards/delayed packets).
     b) Invariant violation triggers (crossed book: $\text{best\_bid} \ge \text{best\_ask}$).
     c) Verification against the periodic REST snapshot `/book?token_id=...` hash comparison.

---

## 5. Selected Active Liquid Test Markets

Queried and verified via live Polymarket Gamma API:

| # | Market Proposition | Condition ID | Outcome Token IDs (YES / NO) | Min Tick | 24h Volume |
|---|---|---|---|---|---|
| **1** | **Will Gavin Newsom win the 2028 Democratic nomination?** | `0x0f49db97f71c68b1e42a6d16e3de93d85dbf7d4148e3f018eb79e88554be9f75` | YES: `54533043819946592547517511176940999955633860128497669742211153063842200957669`<br>NO: `87854174148074652060467921081181402357467303721471806610111179101805869578687` | `0.001` | ~$213,138 |
| **2** | **Xi Jinping out before 2027?** | `0xa467b14d51f01b957109d9cbb1d6c124fab2a089d52ed8f471d23c2812e743b7` | YES: `32338220190071351435772801779725302244575775216413325951443816017994629993401`<br>NO: `25659310674993675562345759665114759892400026242514633218387667107987341231962` | `0.001` | ~$35,160 |
| **3** | **Will Pete Buttigieg win the 2028 Democratic nomination?** | `0x4c325469d9b516ef4e6b8f73a81a12607dec075e3c2fd454f91765aaeafc4760` | YES: `91031279171981959197361710127213577102576826515163085396576756946418341946256`<br>NO: `18494065527924073750217237765784380365430319396356251245973222791140100738948` | `0.001` | ~$6,156 |
| **4** | **Will Alexandria Ocasio-Cortez win the 2028 Democratic nomination?** | `0xe6bcc2f1dd025ce5e1833190f7c60a71171c94f805df55b9ab0ded695ec93565` | YES: `107064985435494333113391038470401719113272800530429703182710416066774068907304`<br>NO: `65176072261324737856085688071627118509549293922582857186996392180609764586527` | `0.001` | ~$5,325 |

---

## 6. Open Questions & Nuances

1. **REST Snapshot Array Ordering**:
   - In `GET /book?token_id=...`, bids are returned in price ascending order (lowest to highest, best bid at end of array), while asks are returned in descending order (highest to lowest, best ask at end of array). The C++ engine's internal `std::map` sorting handles this automatically regardless of insertion order.
2. **Negative Risk (`neg_risk`) Markets**:
   - Mutually exclusive multi-outcome events (e.g. winner of presidential nomination) use negative risk mechanisms where buying NO across all others equals buying YES. For individual token CLOB books, the matching mechanics are standard binary L2 orderbooks, but the gateway can track linked outcome IDs.
3. **State Hash Exact Formula**:
   - Polymarket calculates `hash` over active book levels, but the exact hashing preimage (whether sorted canonical JSON string, binary pack, or tree root) is not publicly specified in REST documentation. In Phase 3, we verify state by polling the REST `/book` snapshot and matching level counts/BBO.
