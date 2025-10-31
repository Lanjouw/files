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

3. CANDLE SIZE AVERAGE (1-MIN)
   - Shows current 1-min candle size
   - Shows average size of last N bars (default: 20)
   - Shows multiplier (current vs average)
   - Format: 
     "1MIN Size: 15.25"
     "Avg(20): 12.50 (1.2x)"
   - Helps assess if current candle is larger/smaller than average

========================================================================
DASHBOARD SETTINGS:

Input [60] - Dashboard: Enabled (Yes/No)
Input [61] - Dashboard: Fixed Position (stays in corner)
             Yes = Fixed in corner (recommended)
             No = Floats with chart
Input [62] - Dashboard: Horizontal Position %
             0% = Far left, 100% = Far right
             Default: 2% (left side)
Input [63] - Dashboard: Vertical Position %
             0% = Top, 100% = Bottom
             Default: 95% (bottom)
Input [64] - Dashboard: Font Size (default: 12)
Input [65] - Dashboard: Background Color (default: Black)
Input [66] - Dashboard: Text Color (default: White)

Input [70] - 1-Min Avg: Period (number of bars, default: 20)
             Calculates average candle size of last N completed 1-min bars
Input [71] - 1-Min Avg: Show Candle Size Info (Yes/No)

CANDLE SIZE CALCULATION:
  Candle Size = High - Low
  Average = Sum of last N candle sizes / N
  Multiplier = Current candle size / Average
  
  Example:
    Current: 15.25 points
    Average of last 20: 12.50 points
    Multiplier: 1.2x (current candle is 20% larger than average)

HOW TO POSITION:
✓ FIXED MODE (Recommended - Dashboard stays in place):
  - Set "Fixed Position" = Yes
  - Bottom-Left:  H=2%,  V=95%  ← DEFAULT
  - Bottom-Right: H=98%, V=95%
  - Top-Left:     H=2%,  V=5%
  - Top-Right:    H=98%, V=5%
  - Center:       H=50%, V=50%
  
✓ FLOATING MODE (Dashboard moves with chart):
  - Set "Fixed Position" = No
  - Appears near current price action

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
