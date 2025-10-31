#include "sierrachart.h"

SCDLLName("VH/VL Multi-Timeframe Scanner")

// ============================================================================
// TIMEFRAME SCANNER STRUCTURE
// ============================================================================
struct s_TimeframeScanner {
    enum NextPlot { PLOT_VH, PLOT_VL };
    enum LastPlotted { NONE, LAST_VH, LAST_VL };
    
    NextPlot WhatToPlotNext = PLOT_VH;
    LastPlotted LastPlottedType = NONE;
    
    // VH zoektocht
    bool VH_Active = false;
    float VH_PeakHigh = 0.0f;
    int VH_PeakBar = -1;
    int VH_PeakBar_15s = -1;
    float VH_AnchorHigh = 0.0f;       // HIGH van huidige anker (voor nieuwe anker check)
    float VH_ConfirmLevel = 0.0f;     // LOW van huidige anker (voor bevestiging)
    int VH_ConfirmLevelBar = -1;
    int VH_ConfirmLevelBar_15s = -1;
    int VH_StartBar_15s = -1;
    
    // VL zoektocht
    bool VL_Active = false;
    float VL_TroughLow = 0.0f;
    int VL_TroughBar = -1;
    int VL_TroughBar_15s = -1;
    float VL_AnchorLow = 0.0f;        // LOW van huidige anker (voor nieuwe anker check)
    float VL_ConfirmLevel = 0.0f;     // HIGH van huidige anker (voor bevestiging)
    int VL_ConfirmLevelBar = -1;
    int VL_ConfirmLevelBar_15s = -1;
    int VL_StartBar_15s = -1;
    
    int LastProcessedBar = -1;
    
    // Voor hogere timeframes: tracking
    int LastVH_PlotBar = -1;
    int LastVL_PlotBar = -1;
    int LastVH_PlotBar_15s = -1;
    int LastVL_PlotBar_15s = -1;
    
    // Voor zigzag lijnen
    int LastPlot_15s = -1;
    float LastPlot_Price = 0.0f;
    int ZigzagLineCounter = 0;
    
    // Voor Higher TF: bar counter (om correcte barIndex te hebben)
    int TFBarCounter = 0;
};

// ============================================================================
// HIGHER TIMEFRAME BAR BUILDER
// ============================================================================
struct s_HigherTFBar {
    SCDateTime StartTime;
    float Open = 0.0f;
    float High = 0.0f;
    float Low = 0.0f;
    float Close = 0.0f;
    int StartBar_15s = -1;
    int EndBar_15s = -1;
    int HighBar_15s = -1;
    int LowBar_15s = -1;
    bool IsComplete = false;
    
    // Vorige bar (voor prev_high/prev_low)
    float PrevHigh = 0.0f;
    float PrevLow = 0.0f;
    float PrevClose = 0.0f;
    
    // NIEUW: Track de START 15s bar van de anchor bar (voor confirm level tekening)
    int AnchorStartBar_15s = -1;
    
    void Reset() {
        StartTime = 0;
        Open = High = Low = Close = 0.0f;
        StartBar_15s = EndBar_15s = -1;
        HighBar_15s = LowBar_15s = -1;
        AnchorStartBar_15s = -1;
        IsComplete = false;
    }
    
    void SaveAsPrevious() {
        PrevHigh = High;
        PrevLow = Low;
        PrevClose = Close;
    }
};

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

bool ShouldStartNewBar(SCDateTime barTime, int periodMinutes, SCDateTime lastStartTime) {
    if (lastStartTime == 0) return true;
    
    int barMinute = barTime.GetMinute();
    int barHour = barTime.GetHour();
    int lastMinute = lastStartTime.GetMinute();
    int lastHour = lastStartTime.GetHour();
    
    if (periodMinutes == 1) {
        return (barMinute != lastMinute || barHour != lastHour);
    } else if (periodMinutes == 5) {
        int barInterval = (barMinute / 5) * 5;
        int lastInterval = (lastMinute / 5) * 5;
        return (barInterval != lastInterval || barHour != lastHour);
    } else if (periodMinutes == 15) {
        int barInterval = (barMinute / 15) * 15;
        int lastInterval = (lastMinute / 15) * 15;
        return (barInterval != lastInterval || barHour != lastHour);
    }
    return false;
}

int FindExact15sBarWithHighestHigh(SCStudyInterfaceRef& sc, int startBar_15s, int endBar_15s) {
    if (startBar_15s < 0 || endBar_15s < 0 || startBar_15s > endBar_15s) return -1;
    
    float maxHigh = sc.High[startBar_15s];
    int maxBar = startBar_15s;
    
    for (int i = startBar_15s + 1; i <= endBar_15s && i < sc.ArraySize; i++) {
        if (sc.High[i] > maxHigh) {
            maxHigh = sc.High[i];
            maxBar = i;
        }
    }
    return maxBar;
}

int FindExact15sBarWithLowestLow(SCStudyInterfaceRef& sc, int startBar_15s, int endBar_15s) {
    if (startBar_15s < 0 || endBar_15s < 0 || startBar_15s > endBar_15s) return -1;
    
    float minLow = sc.Low[startBar_15s];
    int minBar = startBar_15s;
    
    for (int i = startBar_15s + 1; i <= endBar_15s && i < sc.ArraySize; i++) {
        if (sc.Low[i] < minLow) {
            minLow = sc.Low[i];
            minBar = i;
        }
    }
    return minBar;
}

void DrawZigzagLine(
    SCStudyInterfaceRef& sc,
    s_TimeframeScanner* scanner,
    int currentBar_15s,
    float currentPrice,
    int baseLineNumber,
    int color,
    int width
) {
    if (scanner->LastPlot_15s >= 0 && scanner->LastPlot_Price > 0) {
        int lineNumber = baseLineNumber + scanner->ZigzagLineCounter;
        
        s_UseTool tool;
        tool.DrawingType = DRAWING_LINE;
        tool.LineNumber = lineNumber;
        tool.AddMethod = UTAM_ADD_OR_ADJUST;
        tool.BeginIndex = scanner->LastPlot_15s;
        tool.BeginValue = scanner->LastPlot_Price;
        tool.EndIndex = currentBar_15s;
        tool.EndValue = currentPrice;
        tool.Color = color;
        tool.LineWidth = width;
        tool.LineStyle = LINESTYLE_SOLID;
        tool.ExtendLeft = false;
        tool.ExtendRight = false;
        sc.UseTool(tool);
        
        scanner->ZigzagLineCounter++;
    }
    
    scanner->LastPlot_15s = currentBar_15s;
    scanner->LastPlot_Price = currentPrice;
}

// ============================================================================
// SCAN HIGHER TF BAR - FIXED VERSION
// ============================================================================
void ScanHigherTFBar(
    SCStudyInterfaceRef& sc,
    s_TimeframeScanner* scanner,
    s_HigherTFBar* tfBar,
    SCSubgraphRef& sg_VH,
    SCSubgraphRef& sg_VL,
    int symbolOffset,
    const char* tfName,
    bool detailedLog,
    bool zigzagEnabled,
    int zigzagColor,
    int zigzagWidth,
    int baseLineNumber
) {
    if (!tfBar->IsComplete) return;
    
    // CORRECTIE: Gebruik TFBarCounter voor consistente tracking
    int barIndex = scanner->TFBarCounter;
    scanner->TFBarCounter++;  // Increment voor volgende bar
    
    // TF BAR data
    float high = tfBar->High;
    float low = tfBar->Low;
    float open = tfBar->Open;
    float close = tfBar->Close;
    
    // VORIGE TF BAR data
    float prev_high = tfBar->PrevHigh;
    float prev_low = tfBar->PrevLow;
    
    // Bij eerste bar: gebruik huidige bar als referentie
    if (prev_high == 0.0f && prev_low == 0.0f) {
        prev_high = high;
        prev_low = low;
    }
    
    // ====================================================================
    // CHECK CONFIRMATIONS
    // ====================================================================
    bool vh_confirmed = (scanner->VH_Active && 
                        close < scanner->VH_ConfirmLevel &&
                        scanner->WhatToPlotNext == s_TimeframeScanner::PLOT_VH &&
                        scanner->LastPlottedType != s_TimeframeScanner::LAST_VH);
    
    bool vl_confirmed = (scanner->VL_Active && 
                        close > scanner->VL_ConfirmLevel &&
                        scanner->WhatToPlotNext == s_TimeframeScanner::PLOT_VL &&
                        scanner->LastPlottedType != s_TimeframeScanner::LAST_VL);
    
    // Handle VH confirmation
    if (vh_confirmed) {
        int startScan = scanner->VH_StartBar_15s;
        int endScan = tfBar->EndBar_15s;
        
        int exact15sBar = FindExact15sBarWithHighestHigh(sc, startScan, endScan);
        
        if (exact15sBar >= 0) {
            float plotPrice = sc.High[exact15sBar] + (symbolOffset * sc.TickSize);
            sg_VH[exact15sBar] = plotPrice;
            
            if (zigzagEnabled) {
                DrawZigzagLine(sc, scanner, exact15sBar, plotPrice, baseLineNumber,
                              zigzagColor, zigzagWidth);
            }
            
            scanner->LastPlottedType = s_TimeframeScanner::LAST_VH;
            scanner->WhatToPlotNext = s_TimeframeScanner::PLOT_VL;
            scanner->LastVH_PlotBar = barIndex;
            scanner->LastVH_PlotBar_15s = exact15sBar;
            scanner->VH_Active = false;
            scanner->VH_StartBar_15s = -1;
            
            // NIEUW: Start VL search range vanaf DEZE plot (niet wachten tot VL search start)
            scanner->VL_StartBar_15s = exact15sBar;
            
            SCString msg;
            msg.Format("[%s] *** VH PLOTTED *** 15s bar %d (TF bar %d), TF Close=%.2f < ConfirmLvl=%.2f, Peak=%.2f, Scanned range %d-%d",
                tfName, exact15sBar, barIndex, close, scanner->VH_ConfirmLevel, sc.High[exact15sBar], startScan, endScan);
            sc.AddMessageToLog(msg, 0);
            
            // Check stale VL search
            if (scanner->VL_Active && close > scanner->VL_ConfirmLevel) {
                scanner->VL_Active = false;
                scanner->VL_StartBar_15s = -1;
                if (detailedLog) {
                    SCString msg2;
                    msg2.Format("[%s]     VL search was already broken, RESET", tfName);
                    sc.AddMessageToLog(msg2, 0);
                }
            }
        }
    }
    
    // Handle VL confirmation
    if (vl_confirmed) {
        int startScan = scanner->VL_StartBar_15s;
        int endScan = tfBar->EndBar_15s;
        
        int exact15sBar = FindExact15sBarWithLowestLow(sc, startScan, endScan);
        
        if (exact15sBar >= 0) {
            float plotPrice = sc.Low[exact15sBar] - (symbolOffset * sc.TickSize);
            sg_VL[exact15sBar] = plotPrice;
            
            if (zigzagEnabled) {
                DrawZigzagLine(sc, scanner, exact15sBar, plotPrice, baseLineNumber,
                              zigzagColor, zigzagWidth);
            }
            
            scanner->LastPlottedType = s_TimeframeScanner::LAST_VL;
            scanner->WhatToPlotNext = s_TimeframeScanner::PLOT_VH;
            scanner->LastVL_PlotBar = barIndex;
            scanner->LastVL_PlotBar_15s = exact15sBar;
            scanner->VL_Active = false;
            scanner->VL_StartBar_15s = -1;
            
            // NIEUW: Start VH search range vanaf DEZE plot (niet wachten tot VH search start)
            scanner->VH_StartBar_15s = exact15sBar;
            
            SCString msg;
            msg.Format("[%s] *** VL PLOTTED *** 15s bar %d (TF bar %d), TF Close=%.2f > ConfirmLvl=%.2f, Trough=%.2f, Scanned range %d-%d",
                tfName, exact15sBar, barIndex, close, scanner->VL_ConfirmLevel, sc.Low[exact15sBar], startScan, endScan);
            sc.AddMessageToLog(msg, 0);
            
            // Check stale VH search
            if (scanner->VH_Active && close < scanner->VH_ConfirmLevel) {
                scanner->VH_Active = false;
                scanner->VH_StartBar_15s = -1;
                if (detailedLog) {
                    SCString msg2;
                    msg2.Format("[%s]     VH search was already broken, RESET", tfName);
                    sc.AddMessageToLog(msg2, 0);
                }
            }
        }
    }
    
    // ====================================================================
    // UPDATE SEARCHES - FIXED VERSION
    // ====================================================================
    
    // VH Search
    if (!scanner->VH_Active) {
        // Start VH search: bodyclose > prev_high
        if (close > prev_high) {
            scanner->VH_Active = true;
            scanner->VH_PeakHigh = high;
            scanner->VH_PeakBar = barIndex;
            scanner->VH_AnchorHigh = high;        // HIGH van eerste anker
            scanner->VH_ConfirmLevel = low;       // LOW van eerste anker
            scanner->VH_ConfirmLevelBar = barIndex;
            scanner->VH_ConfirmLevelBar_15s = tfBar->LowBar_15s;
            
            // Start 15s range tracking
            if (scanner->VH_StartBar_15s < 0) {
                scanner->VH_StartBar_15s = (scanner->LastVL_PlotBar_15s >= 0) ? 
                                           scanner->LastVL_PlotBar_15s : tfBar->StartBar_15s;
            }
            
            SCString msg;
            msg.Format("[%s] *** VH SEARCH STARTED *** TFBar %d, AnchorHigh=%.2f, ConfirmLvl=%.2f (LOW), Close(%.2f)>PrevHigh(%.2f)",
                tfName, barIndex, high, low, close, prev_high);
            sc.AddMessageToLog(msg, 0);
        }
    } else {
        bool peakUpdated = false;
        bool anchorUpdated = false;
        
        // Peak: altijd bij hogere high
        if (high > scanner->VH_PeakHigh) {
            scanner->VH_PeakHigh = high;
            scanner->VH_PeakBar = barIndex;
            peakUpdated = true;
        }
        
        // ANKER update: alleen bij bodyclose > AnchorHigh!
        if (close > scanner->VH_AnchorHigh) {
            scanner->VH_AnchorHigh = high;        // HIGH van nieuwe anker
            scanner->VH_ConfirmLevel = low;       // LOW van nieuwe anker
            scanner->VH_ConfirmLevelBar = barIndex;
            scanner->VH_ConfirmLevelBar_15s = tfBar->LowBar_15s;
            anchorUpdated = true;
        }
        
        if (detailedLog && (peakUpdated || anchorUpdated)) {
            SCString msg;
            if (peakUpdated && anchorUpdated) {
                msg.Format("[%s]   VH UPDATED: TFBar %d, NewPeak=%.2f, NewAnchor(High=%.2f, ConfirmLvl=%.2f)",
                    tfName, barIndex, high, high, low);
            } else if (peakUpdated) {
                msg.Format("[%s]   VH PEAK UPDATED: TFBar %d, NewPeak=%.2f (Anchor unchanged)", tfName, barIndex, high);
            } else {
                msg.Format("[%s]   VH ANCHOR UPDATED: TFBar %d, Close(%.2f)>AnchorHigh(%.2f), NewConfirmLvl=%.2f",
                    tfName, barIndex, close, scanner->VH_AnchorHigh, low);
            }
            sc.AddMessageToLog(msg, 0);
        }
    }
    
    // VL Search
    if (!scanner->VL_Active) {
        // Start VL search: bodyclose < prev_low
        if (close < prev_low) {
            scanner->VL_Active = true;
            scanner->VL_TroughLow = low;
            scanner->VL_TroughBar = barIndex;
            scanner->VL_AnchorLow = low;          // LOW van eerste anker
            scanner->VL_ConfirmLevel = high;      // HIGH van eerste anker
            scanner->VL_ConfirmLevelBar = barIndex;
            scanner->VL_ConfirmLevelBar_15s = tfBar->HighBar_15s;
            
            // Start 15s range tracking
            if (scanner->VL_StartBar_15s < 0) {
                scanner->VL_StartBar_15s = (scanner->LastVH_PlotBar_15s >= 0) ? 
                                           scanner->LastVH_PlotBar_15s : tfBar->StartBar_15s;
            }
            
            SCString msg;
            msg.Format("[%s] *** VL SEARCH STARTED *** TFBar %d, AnchorLow=%.2f, ConfirmLvl=%.2f (HIGH), Close(%.2f)<PrevLow(%.2f)",
                tfName, barIndex, low, high, close, prev_low);
            sc.AddMessageToLog(msg, 0);
        }
    } else {
        bool troughUpdated = false;
        bool anchorUpdated = false;
        
        // Trough: altijd bij lagere low
        if (low < scanner->VL_TroughLow) {
            scanner->VL_TroughLow = low;
            scanner->VL_TroughBar = barIndex;
            troughUpdated = true;
        }
        
        // ANKER update: alleen bij bodyclose < AnchorLow!
        if (close < scanner->VL_AnchorLow) {
            scanner->VL_AnchorLow = low;          // LOW van nieuwe anker
            scanner->VL_ConfirmLevel = high;      // HIGH van nieuwe anker
            scanner->VL_ConfirmLevelBar = barIndex;
            scanner->VL_ConfirmLevelBar_15s = tfBar->HighBar_15s;
            anchorUpdated = true;
        }
        
        if (detailedLog && (troughUpdated || anchorUpdated)) {
            SCString msg;
            if (troughUpdated && anchorUpdated) {
                msg.Format("[%s]   VL UPDATED: TFBar %d, NewTrough=%.2f, NewAnchor(Low=%.2f, ConfirmLvl=%.2f)",
                    tfName, barIndex, low, low, high);
            } else if (troughUpdated) {
                msg.Format("[%s]   VL TROUGH UPDATED: TFBar %d, NewTrough=%.2f (Anchor unchanged)", tfName, barIndex, low);
            } else {
                msg.Format("[%s]   VL ANCHOR UPDATED: TFBar %d, Close(%.2f)<AnchorLow(%.2f), NewConfirmLvl=%.2f",
                    tfName, barIndex, close, scanner->VL_AnchorLow, high);
            }
            sc.AddMessageToLog(msg, 0);
        }
    }
    
    scanner->LastProcessedBar = barIndex;
}

// ============================================================================
// MAIN STUDY FUNCTION
// ============================================================================
SCSFExport scsf_VHVLScanner_MultiTF(SCStudyInterfaceRef sc)
{
    // INPUTS
    SCInputRef i_15s_Enabled = sc.Input[0];
    SCInputRef i_15s_SymbolColor = sc.Input[1];
    SCInputRef i_15s_SymbolSize = sc.Input[2];
    SCInputRef i_15s_SymbolOffset = sc.Input[3];
    SCInputRef i_15s_DrawStyle = sc.Input[4];
    SCInputRef i_15s_ZigzagEnabled = sc.Input[5];
    SCInputRef i_15s_ZigzagColor = sc.Input[6];
    SCInputRef i_15s_ZigzagWidth = sc.Input[7];
    
    SCInputRef i_1m_Enabled = sc.Input[10];
    SCInputRef i_1m_SymbolColor = sc.Input[11];
    SCInputRef i_1m_SymbolSize = sc.Input[12];
    SCInputRef i_1m_SymbolOffset = sc.Input[13];
    SCInputRef i_1m_DrawStyle = sc.Input[14];
    SCInputRef i_1m_ZigzagEnabled = sc.Input[15];
    SCInputRef i_1m_ZigzagColor = sc.Input[16];
    SCInputRef i_1m_ZigzagWidth = sc.Input[17];
    
    SCInputRef i_5m_Enabled = sc.Input[20];
    SCInputRef i_5m_SymbolColor = sc.Input[21];
    SCInputRef i_5m_SymbolSize = sc.Input[22];
    SCInputRef i_5m_SymbolOffset = sc.Input[23];
    SCInputRef i_5m_DrawStyle = sc.Input[24];
    SCInputRef i_5m_ZigzagEnabled = sc.Input[25];
    SCInputRef i_5m_ZigzagColor = sc.Input[26];
    SCInputRef i_5m_ZigzagWidth = sc.Input[27];
    
    SCInputRef i_15m_Enabled = sc.Input[30];
    SCInputRef i_15m_SymbolColor = sc.Input[31];
    SCInputRef i_15m_SymbolSize = sc.Input[32];
    SCInputRef i_15m_SymbolOffset = sc.Input[33];
    SCInputRef i_15m_DrawStyle = sc.Input[34];
    SCInputRef i_15m_ZigzagEnabled = sc.Input[35];
    SCInputRef i_15m_ZigzagColor = sc.Input[36];
    SCInputRef i_15m_ZigzagWidth = sc.Input[37];
    
    SCInputRef i_LineWidth = sc.Input[50];
    SCInputRef i_DetailedLog = sc.Input[51];
    SCInputRef i_RecalcTrigger = sc.Input[52];
    SCInputRef i_RecalcMinutes = sc.Input[53];

    // SUBGRAPHS
    SCSubgraphRef sg_15s_VH = sc.Subgraph[0];
    SCSubgraphRef sg_15s_VL = sc.Subgraph[1];
    SCSubgraphRef sg_1m_VH = sc.Subgraph[2];
    SCSubgraphRef sg_1m_VL = sc.Subgraph[3];
    SCSubgraphRef sg_5m_VH = sc.Subgraph[4];
    SCSubgraphRef sg_5m_VL = sc.Subgraph[5];
    SCSubgraphRef sg_15m_VH = sc.Subgraph[6];
    SCSubgraphRef sg_15m_VL = sc.Subgraph[7];

    // DEFAULTS
    if (sc.SetDefaults)
    {
        sc.GraphName = "VH/VL Multi-Timeframe Scanner (FIXED)";
        sc.AutoLoop = 1;
        sc.GraphRegion = 0;
        sc.UpdateAlways = 1;

        // 15-sec Inputs
        i_15s_Enabled.Name = "15sec: Enabled";
        i_15s_Enabled.SetYesNo(true);
        i_15s_SymbolColor.Name = "15sec: Symbol Color";
        i_15s_SymbolColor.SetColor(RGB(255, 255, 255));
        i_15s_SymbolSize.Name = "15sec: Symbol Size";
        i_15s_SymbolSize.SetInt(4);
        i_15s_SymbolOffset.Name = "15sec: Symbol Offset (ticks)";
        i_15s_SymbolOffset.SetInt(3);
        i_15s_DrawStyle.Name = "15sec: Draw Style (0=Arrow, 1=Point, 2=Star, 3=Hollow Circle)";
        i_15s_DrawStyle.SetInt(1);
        i_15s_ZigzagEnabled.Name = "15sec: Zigzag Enabled";
        i_15s_ZigzagEnabled.SetYesNo(false);
        i_15s_ZigzagColor.Name = "15sec: Zigzag Color";
        i_15s_ZigzagColor.SetColor(RGB(0, 0, 0));
        i_15s_ZigzagWidth.Name = "15sec: Zigzag Width";
        i_15s_ZigzagWidth.SetInt(1);
        
        // 1-min Inputs
        i_1m_Enabled.Name = "1min: Enabled";
        i_1m_Enabled.SetYesNo(true);
        i_1m_SymbolColor.Name = "1min: Symbol Color";
        i_1m_SymbolColor.SetColor(RGB(255, 255, 0));
        i_1m_SymbolSize.Name = "1min: Symbol Size";
        i_1m_SymbolSize.SetInt(6);
        i_1m_SymbolOffset.Name = "1min: Symbol Offset (ticks)";
        i_1m_SymbolOffset.SetInt(5);
        i_1m_DrawStyle.Name = "1min: Draw Style (0=Arrow, 1=Point, 2=Star, 3=Hollow Circle)";
        i_1m_DrawStyle.SetInt(1);
        i_1m_ZigzagEnabled.Name = "1min: Zigzag Enabled";
        i_1m_ZigzagEnabled.SetYesNo(false);
        i_1m_ZigzagColor.Name = "1min: Zigzag Color";
        i_1m_ZigzagColor.SetColor(RGB(0, 255, 0));
        i_1m_ZigzagWidth.Name = "1min: Zigzag Width";
        i_1m_ZigzagWidth.SetInt(2);
        
        // 5-min Inputs
        i_5m_Enabled.Name = "5min: Enabled";
        i_5m_Enabled.SetYesNo(true);
        i_5m_SymbolColor.Name = "5min: Symbol Color";
        i_5m_SymbolColor.SetColor(RGB(0, 255, 255));
        i_5m_SymbolSize.Name = "5min: Symbol Size";
        i_5m_SymbolSize.SetInt(8);
        i_5m_SymbolOffset.Name = "5min: Symbol Offset (ticks)";
        i_5m_SymbolOffset.SetInt(7);
        i_5m_DrawStyle.Name = "5min: Draw Style (0=Arrow, 1=Point, 2=Star, 3=Hollow Circle)";
        i_5m_DrawStyle.SetInt(2);
        i_5m_ZigzagEnabled.Name = "5min: Zigzag Enabled";
        i_5m_ZigzagEnabled.SetYesNo(false);
        i_5m_ZigzagColor.Name = "5min: Zigzag Color";
        i_5m_ZigzagColor.SetColor(RGB(255, 0, 0));
        i_5m_ZigzagWidth.Name = "5min: Zigzag Width";
        i_5m_ZigzagWidth.SetInt(2);
        
        // 15-min Inputs
        i_15m_Enabled.Name = "15min: Enabled";
        i_15m_Enabled.SetYesNo(true);
        i_15m_SymbolColor.Name = "15min: Symbol Color";
        i_15m_SymbolColor.SetColor(RGB(255, 0, 255));
        i_15m_SymbolSize.Name = "15min: Symbol Size";
        i_15m_SymbolSize.SetInt(10);
        i_15m_SymbolOffset.Name = "15min: Symbol Offset (ticks)";
        i_15m_SymbolOffset.SetInt(10);
        i_15m_DrawStyle.Name = "15min: Draw Style (0=Arrow, 1=Point, 2=Star, 3=Hollow Circle)";
        i_15m_DrawStyle.SetInt(0);
        i_15m_ZigzagEnabled.Name = "15min: Zigzag Enabled";
        i_15m_ZigzagEnabled.SetYesNo(false);
        i_15m_ZigzagColor.Name = "15min: Zigzag Color";
        i_15m_ZigzagColor.SetColor(RGB(255, 0, 255));
        i_15m_ZigzagWidth.Name = "15min: Zigzag Width";
        i_15m_ZigzagWidth.SetInt(3);
        
        // General Inputs
        i_LineWidth.Name = "Confirm Line Width";
        i_LineWidth.SetInt(2);
        i_DetailedLog.Name = "Enable Detailed Logging";
        i_DetailedLog.SetYesNo(false);
        i_RecalcTrigger.Name = "Trigger: Recalculate Last Period (set to Yes)";
        i_RecalcTrigger.SetYesNo(false);
        i_RecalcMinutes.Name = "Recalculate: How Many Minutes Back";
        i_RecalcMinutes.SetInt(60);

        // Subgraphs
        sg_15s_VH.Name = "15s VH";
        sg_15s_VH.DrawStyle = DRAWSTYLE_POINT;
        sg_15s_VH.PrimaryColor = RGB(255, 255, 255);
        sg_15s_VH.LineWidth = 4;
        sg_15s_VH.DrawZeros = false;
        
        sg_15s_VL.Name = "15s VL";
        sg_15s_VL.DrawStyle = DRAWSTYLE_POINT;
        sg_15s_VL.PrimaryColor = RGB(255, 255, 255);
        sg_15s_VL.LineWidth = 4;
        sg_15s_VL.DrawZeros = false;
        
        sg_1m_VH.Name = "1m VH";
        sg_1m_VH.DrawStyle = DRAWSTYLE_POINT;
        sg_1m_VH.PrimaryColor = RGB(255, 255, 0);
        sg_1m_VH.LineWidth = 6;
        sg_1m_VH.DrawZeros = false;
        
        sg_1m_VL.Name = "1m VL";
        sg_1m_VL.DrawStyle = DRAWSTYLE_POINT;
        sg_1m_VL.PrimaryColor = RGB(255, 255, 0);
        sg_1m_VL.LineWidth = 6;
        sg_1m_VL.DrawZeros = false;
        
        sg_5m_VH.Name = "5m VH";
        sg_5m_VH.DrawStyle = DRAWSTYLE_STAR;
        sg_5m_VH.PrimaryColor = RGB(0, 255, 255);
        sg_5m_VH.LineWidth = 8;
        sg_5m_VH.DrawZeros = false;
        
        sg_5m_VL.Name = "5m VL";
        sg_5m_VL.DrawStyle = DRAWSTYLE_STAR;
        sg_5m_VL.PrimaryColor = RGB(0, 255, 255);
        sg_5m_VL.LineWidth = 8;
        sg_5m_VL.DrawZeros = false;
        
        sg_15m_VH.Name = "15m VH";
        sg_15m_VH.DrawStyle = DRAWSTYLE_ARROWUP;
        sg_15m_VH.PrimaryColor = RGB(255, 0, 255);
        sg_15m_VH.LineWidth = 10;
        sg_15m_VH.DrawZeros = false;
        
        sg_15m_VL.Name = "15m VL";
        sg_15m_VL.DrawStyle = DRAWSTYLE_ARROWDOWN;
        sg_15m_VL.PrimaryColor = RGB(255, 0, 255);
        sg_15m_VL.LineWidth = 10;
        sg_15m_VL.DrawZeros = false;
        
        return;
    }

    // PERSISTENT DATA
    s_TimeframeScanner* p_15s = (s_TimeframeScanner*)sc.GetPersistentPointer(1);
    s_TimeframeScanner* p_1m = (s_TimeframeScanner*)sc.GetPersistentPointer(2);
    s_TimeframeScanner* p_5m = (s_TimeframeScanner*)sc.GetPersistentPointer(3);
    s_TimeframeScanner* p_15m = (s_TimeframeScanner*)sc.GetPersistentPointer(4);
    s_HigherTFBar* p_1m_CurrentBar = (s_HigherTFBar*)sc.GetPersistentPointer(5);
    s_HigherTFBar* p_5m_CurrentBar = (s_HigherTFBar*)sc.GetPersistentPointer(6);
    s_HigherTFBar* p_15m_CurrentBar = (s_HigherTFBar*)sc.GetPersistentPointer(7);
    
    if (p_15s == NULL) {
        p_15s = new s_TimeframeScanner();
        sc.SetPersistentPointer(1, p_15s);
    }
    if (p_1m == NULL) {
        p_1m = new s_TimeframeScanner();
        sc.SetPersistentPointer(2, p_1m);
    }
    if (p_5m == NULL) {
        p_5m = new s_TimeframeScanner();
        sc.SetPersistentPointer(3, p_5m);
    }
    if (p_15m == NULL) {
        p_15m = new s_TimeframeScanner();
        sc.SetPersistentPointer(4, p_15m);
    }
    if (p_1m_CurrentBar == NULL) {
        p_1m_CurrentBar = new s_HigherTFBar();
        sc.SetPersistentPointer(5, p_1m_CurrentBar);
    }
    if (p_5m_CurrentBar == NULL) {
        p_5m_CurrentBar = new s_HigherTFBar();
        sc.SetPersistentPointer(6, p_5m_CurrentBar);
    }
    if (p_15m_CurrentBar == NULL) {
        p_15m_CurrentBar = new s_HigherTFBar();
        sc.SetPersistentPointer(7, p_15m_CurrentBar);
    }

    if (sc.LastCallToFunction) {
        if (p_15s != NULL) {
            delete p_15s;
            sc.SetPersistentPointer(1, NULL);
        }
        if (p_1m != NULL) {
            delete p_1m;
            sc.SetPersistentPointer(2, NULL);
        }
        if (p_5m != NULL) {
            delete p_5m;
            sc.SetPersistentPointer(3, NULL);
        }
        if (p_15m != NULL) {
            delete p_15m;
            sc.SetPersistentPointer(4, NULL);
        }
        if (p_1m_CurrentBar != NULL) {
            delete p_1m_CurrentBar;
            sc.SetPersistentPointer(5, NULL);
        }
        if (p_5m_CurrentBar != NULL) {
            delete p_5m_CurrentBar;
            sc.SetPersistentPointer(6, NULL);
        }
        if (p_15m_CurrentBar != NULL) {
            delete p_15m_CurrentBar;
            sc.SetPersistentPointer(7, NULL);
        }
        return;
    }

    int i = sc.Index;
    
    // ========================================================================
    // MANUAL RECALC TRIGGER - FIXED: Alleen op laatste bar checken!
    // ========================================================================
    static bool recalcTriggered = false;
    if (i == sc.ArraySize - 1 && i_RecalcTrigger.GetYesNo() && !recalcTriggered) {
        recalcTriggered = true;  // Voorkom spam
        
        SCString debugMsg;
        debugMsg.Format("=== RECALC TRIGGER ACTIVATED === Starting manual recalc for last %d minutes...", 
                       i_RecalcMinutes.GetInt());
        sc.AddMessageToLog(debugMsg, 0);
        
        int minutesBack = i_RecalcMinutes.GetInt();
        int barsBack = minutesBack * 4;  // 4 bars per minute (15-sec)
        int startBar = i - barsBack;
        if (startBar < 2) startBar = 2;
        
        // Reset ALL state
        *p_15s = s_TimeframeScanner();
        *p_1m = s_TimeframeScanner();
        *p_5m = s_TimeframeScanner();
        *p_15m = s_TimeframeScanner();
        *p_1m_CurrentBar = s_HigherTFBar();
        *p_5m_CurrentBar = s_HigherTFBar();
        *p_15m_CurrentBar = s_HigherTFBar();
        
        // Clear plots in range
        for (int j = startBar; j <= i; j++) {
            sg_15s_VH[j] = 0;
            sg_15s_VL[j] = 0;
            sg_1m_VH[j] = 0;
            sg_1m_VL[j] = 0;
            sg_5m_VH[j] = 0;
            sg_5m_VL[j] = 0;
            sg_15m_VH[j] = 0;
            sg_15m_VL[j] = 0;
        }
        
        // Clear confirm lines
        sc.DeleteACSChartDrawing(sc.ChartNumber, DRAWING_LINE, 200001);
        sc.DeleteACSChartDrawing(sc.ChartNumber, DRAWING_LINE, 200002);
        sc.DeleteACSChartDrawing(sc.ChartNumber, DRAWING_LINE, 200003);
        sc.DeleteACSChartDrawing(sc.ChartNumber, DRAWING_LINE, 200004);
        
        // Clear zigzag lines (batch delete)
        for (int line = 0; line < 10000; line++) {
            sc.DeleteACSChartDrawing(sc.ChartNumber, DRAWING_LINE, 300000 + line);
            sc.DeleteACSChartDrawing(sc.ChartNumber, DRAWING_LINE, 310000 + line);
            sc.DeleteACSChartDrawing(sc.ChartNumber, DRAWING_LINE, 320000 + line);
            sc.DeleteACSChartDrawing(sc.ChartNumber, DRAWING_LINE, 330000 + line);
        }
        
        // Reset trigger
        i_RecalcTrigger.SetYesNo(false);
        recalcTriggered = false;  // Reset spam flag
        
        // Force full recalc
        p_15s->LastProcessedBar = startBar - 5;
        p_1m->LastProcessedBar = -1;
        p_5m->LastProcessedBar = -1;
        p_15m->LastProcessedBar = -1;
        
        debugMsg.Format("=== RECALC COMPLETE === Cleared, restarting from bar %d", startBar);
        sc.AddMessageToLog(debugMsg, 0);
        
        return;
    }

    if (i < 2) return;

    // Detect full recalculation
    if (i < p_15s->LastProcessedBar - 1) {
        *p_15s = s_TimeframeScanner();
        *p_1m = s_TimeframeScanner();
        *p_5m = s_TimeframeScanner();
        *p_15m = s_TimeframeScanner();
        *p_1m_CurrentBar = s_HigherTFBar();
        *p_5m_CurrentBar = s_HigherTFBar();
        *p_15m_CurrentBar = s_HigherTFBar();
        
        for (int j = 0; j < sc.ArraySize; j++) {
            sg_15s_VH[j] = 0;
            sg_15s_VL[j] = 0;
            sg_1m_VH[j] = 0;
            sg_1m_VL[j] = 0;
            sg_5m_VH[j] = 0;
            sg_5m_VL[j] = 0;
            sg_15m_VH[j] = 0;
            sg_15m_VL[j] = 0;
        }
        
        sc.DeleteACSChartDrawing(sc.ChartNumber, DRAWING_LINE, 200001);
        sc.DeleteACSChartDrawing(sc.ChartNumber, DRAWING_LINE, 200002);
        sc.DeleteACSChartDrawing(sc.ChartNumber, DRAWING_LINE, 200003);
        sc.DeleteACSChartDrawing(sc.ChartNumber, DRAWING_LINE, 200004);
        
        SCString msg;
        msg.Format("FULL RECALCULATION DETECTED at bar %d - COMPLETE RESET", i);
        sc.AddMessageToLog(msg, 0);
    }

    // ========================================================================
    // 15-SEC TIMEFRAME PROCESSING
    // ========================================================================
    if (i_15s_Enabled.GetYesNo()) {
        bool isLastBar = (i == sc.ArraySize - 1);
        int barToProcess = isLastBar ? (i - 1) : i;
        
        if (barToProcess > p_15s->LastProcessedBar) {
            p_15s->LastProcessedBar = barToProcess;
            
            float high = sc.High[barToProcess];
            float low = sc.Low[barToProcess];
            float close = sc.Close[barToProcess];
            float open = sc.Open[barToProcess];
            float prev_high = sc.High[barToProcess - 1];
            float prev_low = sc.Low[barToProcess - 1];
            
            // Check confirmations
            bool vh_confirmed = (p_15s->VH_Active && 
                                close < p_15s->VH_ConfirmLevel &&
                                p_15s->WhatToPlotNext == s_TimeframeScanner::PLOT_VH &&
                                p_15s->LastPlottedType != s_TimeframeScanner::LAST_VH);
            
            bool vl_confirmed = (p_15s->VL_Active && 
                                close > p_15s->VL_ConfirmLevel &&
                                p_15s->WhatToPlotNext == s_TimeframeScanner::PLOT_VL &&
                                p_15s->LastPlottedType != s_TimeframeScanner::LAST_VL);

            // Handle VH confirmation
            if (vh_confirmed) {
                float plotPrice = p_15s->VH_PeakHigh + (i_15s_SymbolOffset.GetInt() * sc.TickSize);
                sg_15s_VH[p_15s->VH_PeakBar] = plotPrice;
                
                if (i_15s_ZigzagEnabled.GetYesNo()) {
                    DrawZigzagLine(sc, p_15s, p_15s->VH_PeakBar, plotPrice, 300000,
                                  i_15s_ZigzagColor.GetColor(), i_15s_ZigzagWidth.GetInt());
                }
                
                p_15s->LastPlottedType = s_TimeframeScanner::LAST_VH;
                p_15s->WhatToPlotNext = s_TimeframeScanner::PLOT_VL;
                p_15s->VH_Active = false;
                
                // Check stale VL
                if (p_15s->VL_Active) {
                    bool vlBroken = false;
                    for (int checkBar = p_15s->VL_TroughBar + 1; checkBar <= barToProcess; checkBar++) {
                        if (sc.Close[checkBar] > p_15s->VL_ConfirmLevel) {
                            vlBroken = true;
                            break;
                        }
                    }
                    if (vlBroken) {
                        p_15s->VL_Active = false;
                    }
                }
            }
            
            // Handle VL confirmation
            if (vl_confirmed) {
                float plotPrice = p_15s->VL_TroughLow - (i_15s_SymbolOffset.GetInt() * sc.TickSize);
                sg_15s_VL[p_15s->VL_TroughBar] = plotPrice;
                
                if (i_15s_ZigzagEnabled.GetYesNo()) {
                    DrawZigzagLine(sc, p_15s, p_15s->VL_TroughBar, plotPrice, 300000,
                                  i_15s_ZigzagColor.GetColor(), i_15s_ZigzagWidth.GetInt());
                }
                
                p_15s->LastPlottedType = s_TimeframeScanner::LAST_VL;
                p_15s->WhatToPlotNext = s_TimeframeScanner::PLOT_VH;
                p_15s->VL_Active = false;
                
                // Check stale VH
                if (p_15s->VH_Active) {
                    bool vhBroken = false;
                    for (int checkBar = p_15s->VH_PeakBar + 1; checkBar <= barToProcess; checkBar++) {
                        if (sc.Close[checkBar] < p_15s->VH_ConfirmLevel) {
                            vhBroken = true;
                            break;
                        }
                    }
                    if (vhBroken) {
                        p_15s->VH_Active = false;
                    }
                }
            }

            // Update VH search
            if (!p_15s->VH_Active) {
                // Start: bodyclose > prev_high
                if (close > prev_high) {
                    p_15s->VH_Active = true;
                    p_15s->VH_PeakHigh = high;
                    p_15s->VH_PeakBar = barToProcess;
                    p_15s->VH_AnchorHigh = high;      // HIGH van eerste anker
                    p_15s->VH_ConfirmLevel = low;     // LOW van eerste anker
                    p_15s->VH_ConfirmLevelBar = barToProcess;
                    
                    if (i_DetailedLog.GetYesNo()) {
                        SCString msg;
                        msg.Format("[15s] VH SEARCH STARTED at bar %d: AnchorHigh=%.2f, ConfirmLvl=%.2f (LOW), Close(%.2f)>PrevHigh(%.2f)",
                            barToProcess, high, low, close, prev_high);
                        sc.AddMessageToLog(msg, 0);
                    }
                }
            } else {
                bool peakUpdated = false;
                bool anchorUpdated = false;
                
                // Peak: altijd bij hogere high
                if (high > p_15s->VH_PeakHigh) {
                    p_15s->VH_PeakHigh = high;
                    p_15s->VH_PeakBar = barToProcess;
                    peakUpdated = true;
                }
                
                // ANKER update: alleen bij bodyclose > AnchorHigh!
                if (close > p_15s->VH_AnchorHigh) {
                    p_15s->VH_AnchorHigh = high;      // HIGH van nieuwe anker
                    p_15s->VH_ConfirmLevel = low;     // LOW van nieuwe anker
                    p_15s->VH_ConfirmLevelBar = barToProcess;
                    anchorUpdated = true;
                }
                
                if (i_DetailedLog.GetYesNo() && (peakUpdated || anchorUpdated)) {
                    SCString msg;
                    if (peakUpdated && anchorUpdated) {
                        msg.Format("[15s] VH UPDATED at bar %d: NewPeak=%.2f, NewAnchor(High=%.2f, ConfirmLvl=%.2f)",
                            barToProcess, high, high, low);
                    } else if (peakUpdated) {
                        msg.Format("[15s] VH PEAK UPDATED at bar %d: NewPeak=%.2f (Anchor unchanged)", barToProcess, high);
                    } else {
                        msg.Format("[15s] VH ANCHOR UPDATED at bar %d: Close(%.2f)>AnchorHigh(%.2f), NewConfirmLvl=%.2f",
                            barToProcess, close, p_15s->VH_AnchorHigh, low);
                    }
                    sc.AddMessageToLog(msg, 0);
                }
            }
            
            // Update VL search
            if (!p_15s->VL_Active) {
                // Start: bodyclose < prev_low
                if (close < prev_low) {
                    p_15s->VL_Active = true;
                    p_15s->VL_TroughLow = low;
                    p_15s->VL_TroughBar = barToProcess;
                    p_15s->VL_AnchorLow = low;        // LOW van eerste anker
                    p_15s->VL_ConfirmLevel = high;    // HIGH van eerste anker
                    p_15s->VL_ConfirmLevelBar = barToProcess;
                    
                    if (i_DetailedLog.GetYesNo()) {
                        SCString msg;
                        msg.Format("[15s] VL SEARCH STARTED at bar %d: AnchorLow=%.2f, ConfirmLvl=%.2f (HIGH), Close(%.2f)<PrevLow(%.2f)",
                            barToProcess, low, high, close, prev_low);
                        sc.AddMessageToLog(msg, 0);
                    }
                }
            } else {
                bool troughUpdated = false;
                bool anchorUpdated = false;
                
                // Trough: altijd bij lagere low
                if (low < p_15s->VL_TroughLow) {
                    p_15s->VL_TroughLow = low;
                    p_15s->VL_TroughBar = barToProcess;
                    troughUpdated = true;
                }
                
                // ANKER update: alleen bij bodyclose < AnchorLow!
                if (close < p_15s->VL_AnchorLow) {
                    p_15s->VL_AnchorLow = low;        // LOW van nieuwe anker
                    p_15s->VL_ConfirmLevel = high;    // HIGH van nieuwe anker
                    p_15s->VL_ConfirmLevelBar = barToProcess;
                    anchorUpdated = true;
                }
                
                if (i_DetailedLog.GetYesNo() && (troughUpdated || anchorUpdated)) {
                    SCString msg;
                    if (troughUpdated && anchorUpdated) {
                        msg.Format("[15s] VL UPDATED at bar %d: NewTrough=%.2f, NewAnchor(Low=%.2f, ConfirmLvl=%.2f)",
                            barToProcess, low, low, high);
                    } else if (troughUpdated) {
                        msg.Format("[15s] VL TROUGH UPDATED at bar %d: NewTrough=%.2f (Anchor unchanged)", barToProcess, low);
                    } else {
                        msg.Format("[15s] VL ANCHOR UPDATED at bar %d: Close(%.2f)<AnchorLow(%.2f), NewConfirmLvl=%.2f",
                            barToProcess, close, p_15s->VL_AnchorLow, high);
                    }
                    sc.AddMessageToLog(msg, 0);
                }
            }
        }
    }

    // ========================================================================
    // CONFIRM LINES VISUALIZATION
    // ========================================================================
    if (i == sc.ArraySize - 1) {
        auto DrawConfirmLine = [&](int lineNumber, s_TimeframeScanner* scanner, int color, const char* tfName) {
            sc.DeleteACSChartDrawing(sc.ChartNumber, DRAWING_LINE, lineNumber);
            
            bool shouldDrawLine = false;
            int lineBeginBar_15s = -1;
            float lineValue = 0.0f;
            
            if (scanner->WhatToPlotNext == s_TimeframeScanner::PLOT_VH && scanner->VH_Active) {
                shouldDrawLine = true;
                lineBeginBar_15s = scanner->VH_ConfirmLevelBar_15s;
                lineValue = scanner->VH_ConfirmLevel;
            } else if (scanner->WhatToPlotNext == s_TimeframeScanner::PLOT_VL && scanner->VL_Active) {
                shouldDrawLine = true;
                lineBeginBar_15s = scanner->VL_ConfirmLevelBar_15s;
                lineValue = scanner->VL_ConfirmLevel;
            }
            
            if (shouldDrawLine && lineBeginBar_15s >= 0) {
                int lineEndBar = i;
                if (lineEndBar < lineBeginBar_15s + 3) {
                    lineEndBar = lineBeginBar_15s + 3;
                }
                
                s_UseTool tool;
                tool.DrawingType = DRAWING_LINE;
                tool.LineNumber = lineNumber;
                tool.AddMethod = UTAM_ADD_OR_ADJUST;
                tool.BeginIndex = lineBeginBar_15s;
                tool.BeginValue = lineValue;
                tool.EndIndex = lineEndBar;
                tool.EndValue = lineValue;
                tool.Color = color;
                tool.LineWidth = i_LineWidth.GetInt();
                tool.LineStyle = LINESTYLE_SOLID;
                tool.ExtendLeft = false;
                tool.ExtendRight = false;
                sc.UseTool(tool);
            }
        };
        
        // 15-sec confirm line (uses normal ConfirmLevelBar)
        if (i_15s_Enabled.GetYesNo()) {
            sc.DeleteACSChartDrawing(sc.ChartNumber, DRAWING_LINE, 200001);
            bool shouldDrawLine = false;
            int lineBeginBar = -1;
            float lineValue = 0.0f;
            
            if (p_15s->WhatToPlotNext == s_TimeframeScanner::PLOT_VH && p_15s->VH_Active) {
                shouldDrawLine = true;
                lineBeginBar = p_15s->VH_ConfirmLevelBar;
                lineValue = p_15s->VH_ConfirmLevel;
            } else if (p_15s->WhatToPlotNext == s_TimeframeScanner::PLOT_VL && p_15s->VL_Active) {
                shouldDrawLine = true;
                lineBeginBar = p_15s->VL_ConfirmLevelBar;
                lineValue = p_15s->VL_ConfirmLevel;
            }
            
            if (shouldDrawLine && lineBeginBar >= 0) {
                int lineEndBar = i;
                if (lineEndBar < lineBeginBar + 3) {
                    lineEndBar = lineBeginBar + 3;
                }
                
                s_UseTool tool;
                tool.DrawingType = DRAWING_LINE;
                tool.LineNumber = 200001;
                tool.AddMethod = UTAM_ADD_OR_ADJUST;
                tool.BeginIndex = lineBeginBar;
                tool.BeginValue = lineValue;
                tool.EndIndex = lineEndBar;
                tool.EndValue = lineValue;
                tool.Color = RGB(0, 0, 0);
                tool.LineWidth = i_LineWidth.GetInt();
                tool.LineStyle = LINESTYLE_SOLID;
                tool.ExtendLeft = false;
                tool.ExtendRight = false;
                sc.UseTool(tool);
            }
        }
        
        // Higher TF confirm lines
        if (i_1m_Enabled.GetYesNo()) {
            DrawConfirmLine(200002, p_1m, i_1m_SymbolColor.GetColor(), "1MIN");
        }
        if (i_5m_Enabled.GetYesNo()) {
            DrawConfirmLine(200003, p_5m, i_5m_SymbolColor.GetColor(), "5MIN");
        }
        if (i_15m_Enabled.GetYesNo()) {
            DrawConfirmLine(200004, p_15m, i_15m_SymbolColor.GetColor(), "15MIN");
        }
    }
    
    // ========================================================================
    // HIGHER TIMEFRAMES PROCESSING
    // ========================================================================
    
    // 1-MINUTE
    if (i_1m_Enabled.GetYesNo()) {
        SCDateTime currentBarTime = sc.BaseDateTimeIn[i];
        
        if (ShouldStartNewBar(currentBarTime, 1, p_1m_CurrentBar->StartTime)) {
            p_1m_CurrentBar->IsComplete = true;
            
            if (p_1m_CurrentBar->StartTime != 0) {
                ScanHigherTFBar(sc, p_1m, p_1m_CurrentBar, sg_1m_VH, sg_1m_VL,
                               i_1m_SymbolOffset.GetInt(), "1MIN", i_DetailedLog.GetYesNo(),
                               i_1m_ZigzagEnabled.GetYesNo(), i_1m_ZigzagColor.GetColor(),
                               i_1m_ZigzagWidth.GetInt(), 310000);
            }
            
            p_1m_CurrentBar->SaveAsPrevious();
            
            p_1m_CurrentBar->Reset();
            p_1m_CurrentBar->StartTime = currentBarTime;
            p_1m_CurrentBar->Open = sc.Open[i];
            p_1m_CurrentBar->High = sc.High[i];
            p_1m_CurrentBar->Low = sc.Low[i];
            p_1m_CurrentBar->Close = sc.Close[i];
            p_1m_CurrentBar->StartBar_15s = i;
            p_1m_CurrentBar->EndBar_15s = i;
            p_1m_CurrentBar->HighBar_15s = i;
            p_1m_CurrentBar->LowBar_15s = i;
        } else {
            if (sc.High[i] > p_1m_CurrentBar->High) {
                p_1m_CurrentBar->High = sc.High[i];
                p_1m_CurrentBar->HighBar_15s = i;
            }
            if (sc.Low[i] < p_1m_CurrentBar->Low) {
                p_1m_CurrentBar->Low = sc.Low[i];
                p_1m_CurrentBar->LowBar_15s = i;
            }
            p_1m_CurrentBar->Close = sc.Close[i];
            p_1m_CurrentBar->EndBar_15s = i;
        }
    }
    
    // 5-MINUTE
    if (i_5m_Enabled.GetYesNo()) {
        SCDateTime currentBarTime = sc.BaseDateTimeIn[i];
        
        if (ShouldStartNewBar(currentBarTime, 5, p_5m_CurrentBar->StartTime)) {
            // EEN nieuwe 5-min bar start - log dit EENMALIG
            SCString msg;
            msg.Format("[5MIN-DEBUG] NEW 5-min bar detected at 15s bar %d. Prev bar: H=%.2f L=%.2f C=%.2f, StartTime=%d",
                i, p_5m_CurrentBar->High, p_5m_CurrentBar->Low, p_5m_CurrentBar->Close,
                (p_5m_CurrentBar->StartTime != 0 ? 1 : 0));
            sc.AddMessageToLog(msg, 0);
            
            p_5m_CurrentBar->IsComplete = true;
            
            if (p_5m_CurrentBar->StartTime != 0) {
                sc.AddMessageToLog("[5MIN-DEBUG] Calling ScanHigherTFBar for previous 5-min bar...", 0);
                
                ScanHigherTFBar(sc, p_5m, p_5m_CurrentBar, sg_5m_VH, sg_5m_VL,
                               i_5m_SymbolOffset.GetInt(), "5MIN", i_DetailedLog.GetYesNo(),
                               i_5m_ZigzagEnabled.GetYesNo(), i_5m_ZigzagColor.GetColor(),
                               i_5m_ZigzagWidth.GetInt(), 320000);
                               
                sc.AddMessageToLog("[5MIN-DEBUG] ScanHigherTFBar completed", 0);
            } else {
                sc.AddMessageToLog("[5MIN-DEBUG] Skipping scan - StartTime=0 (first bar after init)", 0);
            }
            
            p_5m_CurrentBar->SaveAsPrevious();
            
            p_5m_CurrentBar->Reset();
            p_5m_CurrentBar->StartTime = currentBarTime;
            p_5m_CurrentBar->Open = sc.Open[i];
            p_5m_CurrentBar->High = sc.High[i];
            p_5m_CurrentBar->Low = sc.Low[i];
            p_5m_CurrentBar->Close = sc.Close[i];
            p_5m_CurrentBar->StartBar_15s = i;
            p_5m_CurrentBar->EndBar_15s = i;
            p_5m_CurrentBar->HighBar_15s = i;
            p_5m_CurrentBar->LowBar_15s = i;
        } else {
            if (sc.High[i] > p_5m_CurrentBar->High) {
                p_5m_CurrentBar->High = sc.High[i];
                p_5m_CurrentBar->HighBar_15s = i;
            }
            if (sc.Low[i] < p_5m_CurrentBar->Low) {
                p_5m_CurrentBar->Low = sc.Low[i];
                p_5m_CurrentBar->LowBar_15s = i;
            }
            p_5m_CurrentBar->Close = sc.Close[i];
            p_5m_CurrentBar->EndBar_15s = i;
        }
    }
    
    // 15-MINUTE
    if (i_15m_Enabled.GetYesNo()) {
        SCDateTime currentBarTime = sc.BaseDateTimeIn[i];
        
        if (ShouldStartNewBar(currentBarTime, 15, p_15m_CurrentBar->StartTime)) {
            p_15m_CurrentBar->IsComplete = true;
            
            if (p_15m_CurrentBar->StartTime != 0) {
                ScanHigherTFBar(sc, p_15m, p_15m_CurrentBar, sg_15m_VH, sg_15m_VL,
                               i_15m_SymbolOffset.GetInt(), "15MIN", i_DetailedLog.GetYesNo(),
                               i_15m_ZigzagEnabled.GetYesNo(), i_15m_ZigzagColor.GetColor(),
                               i_15m_ZigzagWidth.GetInt(), 330000);
            }
            
            p_15m_CurrentBar->SaveAsPrevious();
            
            p_15m_CurrentBar->Reset();
            p_15m_CurrentBar->StartTime = currentBarTime;
            p_15m_CurrentBar->Open = sc.Open[i];
            p_15m_CurrentBar->High = sc.High[i];
            p_15m_CurrentBar->Low = sc.Low[i];
            p_15m_CurrentBar->Close = sc.Close[i];
            p_15m_CurrentBar->StartBar_15s = i;
            p_15m_CurrentBar->EndBar_15s = i;
            p_15m_CurrentBar->HighBar_15s = i;
            p_15m_CurrentBar->LowBar_15s = i;
        } else {
            if (sc.High[i] > p_15m_CurrentBar->High) {
                p_15m_CurrentBar->High = sc.High[i];
                p_15m_CurrentBar->HighBar_15s = i;
            }
            if (sc.Low[i] < p_15m_CurrentBar->Low) {
                p_15m_CurrentBar->Low = sc.Low[i];
                p_15m_CurrentBar->LowBar_15s = i;
            }
            p_15m_CurrentBar->Close = sc.Close[i];
            p_15m_CurrentBar->EndBar_15s = i;
        }
    }
}
