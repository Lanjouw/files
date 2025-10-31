========================================================================
VH/VL MULTI-TIMEFRAME SCANNER V2
========================================================================

VERSION 2 - NEW FEATURES:

1. LIVE DASHBOARD
   - Real-time status display per timeframe (VH ↑ / VL ↓)
   - Shows 15-sec, 1-min, 5-min, 15-min status
   - Fully customizable position (X/Y pixels)
   - Adjustable font size, colors

2. COUNTDOWN TIMERS
   - Shows remaining time until next candle close
   - Available for 1-min, 5-min, 15-min
   - Format: [MM:SS]
   - Updates in real-time

3. ATR MULTIPLIER
   - Shows current 1-min candle size vs ATR
   - Configurable ATR period (default: 20)
   - Format: "1MIN: 2.3x ATR(20)"
   - Helps assess candle strength

========================================================================
DASHBOARD SETTINGS:

Input [60] - Dashboard: Enabled (Yes/No)
Input [61] - Dashboard: X Position (pixels from left, default: 10)
Input [62] - Dashboard: Y Position (pixels from top, default: 50)
Input [63] - Dashboard: Font Size (default: 12)
Input [64] - Dashboard: Background Color (default: Black)
Input [65] - Dashboard: Text Color (default: White)

Input [70] - ATR: Period (number of bars, default: 20)
Input [71] - ATR: Show 1-Min Candle Size Multiplier (Yes/No)

========================================================================
FILES TO SHARE:

To give the indicator to someone else:
  - Share: VHVLScanner_MultiTF_V2.cpp

They compile it in SierraChart:
  Analysis > Build Custom Studies > [Select file] > Build 64-bit

========================================================================
ALL V1 FEATURES STILL INCLUDED:

✓ Parallel VH/VL search
✓ Strict alternation (VH → VL → VH → VL)
✓ Bodyclose confirmation
✓ Multi-timeframe support (15s, 1m, 5m, 15m)
✓ Exact plot positioning
✓ Zigzag lines
✓ Confirmation lines
✓ Manual recalculation
✓ Full English localization
✓ Customizable colors, symbols, sizes

========================================================================
