#include "sierrachart.h"

SCDLLName("VH/VL Multi-Timeframe Scanner")

// ============================================================================
// TIMEFRAME SCANNER STRUCTURE
// ============================================================================
struct s_TimeframeScanner {
    enum NextPlot { PLOT_VH, PLOT_VL };
    enum LastPlotted { NONE, LAST_VH, LAST_VL };
    
    NextPlot WhatToPlotNext = PLOT_VH;  // Traffic light: wat mag als volgende geplot worden
    LastPlotted LastPlottedType = NONE;  // Wat hebben we als laatste geplot (extra veiligheid)
    
    // VH zoektocht (loopt ALTIJD parallel)
    bool VH_Active = false;
    float VH_PeakHigh = 0.0f;
    int VH_PeakBar = -1;              // Bar index in EIGEN timeframe
    int VH_PeakBar_15s = -1;          // Exacte 15-sec bar met hoogste high (voor multi-TF)
    float VH_ConfirmLevel = 0.0f;     // Low van de anchor bar
    int VH_ConfirmLevelBar = -1;      // Bar waar de confirm level van is
    int VH_StartBar_15s = -1;         // Start van 15-sec range voor deze search
    
    // VL zoektocht (loopt ALTIJD parallel)
    bool VL_Active = false;
    float VL_TroughLow = 0.0f;
    int VL_TroughBar = -1;            // Bar index in EIGEN timeframe
    int VL_TroughBar_15s = -1;        // Exacte 15-sec bar met laagste low (voor multi-TF)
    float VL_ConfirmLevel = 0.0f;     // High van de anchor bar
    int VL_ConfirmLevelBar = -1;      // Bar waar de confirm level van is
    int VL_StartBar_15s = -1;         // Start van 15-sec range voor deze search
    
    int LastProcessedBar = -1;
    
    // Voor hogere timeframes: tracking van laatste plots (voor range scanning)
    int LastVH_PlotBar = -1;          // Laatste VH plot (in eigen TF)
    int LastVL_PlotBar = -1;          // Laatste VL plot (in eigen TF)
    int LastVH_PlotBar_15s = -1;      // Laatste VH plot (exact 15-sec bar)
    int LastVL_PlotBar_15s = -1;      // Laatste VL plot (exact 15-sec bar)
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
    int StartBar_15s = -1;            // Eerste 15-sec bar van deze TF bar
    int EndBar_15s = -1;              // Laatste 15-sec bar van deze TF bar
    int HighBar_15s = -1;             // 15-sec bar met hoogste high in deze TF bar
    int LowBar_15s = -1;              // 15-sec bar met laagste low in deze TF bar
    bool IsComplete = false;
    
    // Vorige bar (voor prev_high/prev_low)
    float PrevHigh = 0.0f;
    float PrevLow = 0.0f;
    float PrevClose = 0.0f;
    
    void Reset() {
        StartTime = 0;
        Open = High = Low = Close = 0.0f;
        StartBar_15s = EndBar_15s = -1;
        HighBar_15s = LowBar_15s = -1;
        IsComplete = false;
        // PrevHigh/PrevLow blijven behouden!
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

// Check of een 15-sec bar een nieuwe TF bar moet starten (bijv. nieuwe minuut voor 1min)
bool ShouldStartNewBar(SCDateTime barTime, int periodMinutes, SCDateTime lastStartTime) {
    if (lastStartTime == 0) return true;  // Eerste bar
    
    int barMinute = barTime.GetMinute();
    int barHour = barTime.GetHour();
    int lastMinute = lastStartTime.GetMinute();
    int lastHour = lastStartTime.GetHour();
    
    if (periodMinutes == 1) {
        // Nieuwe minuut?
        return (barMinute != lastMinute || barHour != lastHour);
    } else if (periodMinutes == 5) {
        // Nieuwe 5-min interval? (00, 05, 10, 15, ...)
        int barInterval = (barMinute / 5) * 5;
        int lastInterval = (lastMinute / 5) * 5;
        return (barInterval != lastInterval || barHour != lastHour);
    } else if (periodMinutes == 15) {
        // Nieuwe 15-min interval? (00, 15, 30, 45)
        int barInterval = (barMinute / 15) * 15;
        int lastInterval = (lastMinute / 15) * 15;
        return (barInterval != lastInterval || barHour != lastHour);
    }
    return false;
}

// Vind de exacte 15-sec bar met hoogste high in een range
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

// Vind de exacte 15-sec bar met laagste low in een range
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

// Scan een higher timeframe bar voor VH/VL
void ScanHigherTFBar(
    SCStudyInterfaceRef& sc,
    s_TimeframeScanner* scanner,
    s_HigherTFBar* tfBar,
    SCSubgraphRef& sg_VH,
    SCSubgraphRef& sg_VL,
    int symbolOffset,
    const char* tfName,
    bool detailedLog
) {
    if (!tfBar->IsComplete) return;
    
    float high = tfBar->High;
    float low = tfBar->Low;
    float open = tfBar->Open;
    float close = tfBar->Close;
    int barIndex = scanner->LastProcessedBar + 1;  // TF bar index
    
    // Gebruik de vorige TF bar data
    float prev_high = tfBar->PrevHigh;
    float prev_low = tfBar->PrevLow;
    
    float bodySize = (close > open) ? (close - open) : (open - close);
    float minBodySize = 2.0f * sc.TickSize;
    
    // Check confirmations
    bool vh_confirmed = (scanner->VH_Active && 
                        close < scanner->VH_ConfirmLevel &&
                        bodySize >= minBodySize &&
                        scanner->WhatToPlotNext == s_TimeframeScanner::PLOT_VH &&
                        scanner->LastPlottedType != s_TimeframeScanner::LAST_VH);
    
    bool vl_confirmed = (scanner->VL_Active && 
                        close > scanner->VL_ConfirmLevel &&
                        bodySize >= minBodySize &&
                        scanner->WhatToPlotNext == s_TimeframeScanner::PLOT_VL &&
                        scanner->LastPlottedType != s_TimeframeScanner::LAST_VL);
    
    // Handle VH confirmation
    if (vh_confirmed) {
        // Scan de HELE 15-sec range vanaf start van search tot nu
        int startScan = scanner->VH_StartBar_15s;
        int endScan = tfBar->EndBar_15s;
        
        int exact15sBar = FindExact15sBarWithHighestHigh(sc, startScan, endScan);
        
        if (exact15sBar >= 0) {
            float plotPrice = sc.High[exact15sBar] + (symbolOffset * sc.TickSize);
            sg_VH[exact15sBar] = plotPrice;
            
            scanner->LastPlottedType = s_TimeframeScanner::LAST_VH;
            scanner->WhatToPlotNext = s_TimeframeScanner::PLOT_VL;
            scanner->LastVH_PlotBar = barIndex;
            scanner->LastVH_PlotBar_15s = exact15sBar;
            scanner->VH_Active = false;
            scanner->VH_StartBar_15s = -1;
            
            if (detailedLog) {
                SCString msg;
                msg.Format("[%s] VH PLOTTED at 15s bar %d (scanned range %d-%d, Peak=%.2f)",
                    tfName, exact15sBar, startScan, endScan, sc.High[exact15sBar]);
                sc.AddMessageToLog(msg, 0);
                msg.Format("[%s]     Traffic light switched: Now waiting for VL", tfName);
                sc.AddMessageToLog(msg, 0);
            }
            
            // Check of VL search stale is (close > ConfirmLevel terwijl geblokkeerd)
            if (scanner->VL_Active) {
                bool wasAlreadyBroken = false;
                // We scannen niet de hele geschiedenis, alleen checken of current bar het al breekt
                if (close > scanner->VL_ConfirmLevel) {
                    wasAlreadyBroken = true;
                }
                
                if (wasAlreadyBroken) {
                    scanner->VL_Active = false;
                    scanner->VL_StartBar_15s = -1;
                    if (detailedLog) {
                        SCString msg;
                        msg.Format("[%s]     VL search was already broken (close went > confirm while blocked), RESET", tfName);
                        sc.AddMessageToLog(msg, 0);
                    }
                }
            }
        }
    }
    
    // Handle VL confirmation
    if (vl_confirmed) {
        // Scan de HELE 15-sec range vanaf start van search tot nu
        int startScan = scanner->VL_StartBar_15s;
        int endScan = tfBar->EndBar_15s;
        
        int exact15sBar = FindExact15sBarWithLowestLow(sc, startScan, endScan);
        
        if (exact15sBar >= 0) {
            float plotPrice = sc.Low[exact15sBar] - (symbolOffset * sc.TickSize);
            sg_VL[exact15sBar] = plotPrice;
            
            scanner->LastPlottedType = s_TimeframeScanner::LAST_VL;
            scanner->WhatToPlotNext = s_TimeframeScanner::PLOT_VH;
            scanner->LastVL_PlotBar = barIndex;
            scanner->LastVL_PlotBar_15s = exact15sBar;
            scanner->VL_Active = false;
            scanner->VL_StartBar_15s = -1;
            
            if (detailedLog) {
                SCString msg;
                msg.Format("[%s] VL PLOTTED at 15s bar %d (scanned range %d-%d, Trough=%.2f)",
                    tfName, exact15sBar, startScan, endScan, sc.Low[exact15sBar]);
                sc.AddMessageToLog(msg, 0);
                msg.Format("[%s]     Traffic light switched: Now waiting for VH", tfName);
                sc.AddMessageToLog(msg, 0);
            }
            
            // Check of VH search stale is (close < ConfirmLevel terwijl geblokkeerd)
            if (scanner->VH_Active) {
                bool wasAlreadyBroken = false;
                // We scannen niet de hele geschiedenis, alleen checken of current bar het al breekt
                if (close < scanner->VH_ConfirmLevel) {
                    wasAlreadyBroken = true;
                }
                
                if (wasAlreadyBroken) {
                    scanner->VH_Active = false;
                    scanner->VH_StartBar_15s = -1;
                    if (detailedLog) {
                        SCString msg;
                        msg.Format("[%s]     VH search was already broken (close went < confirm while blocked), RESET", tfName);
                        sc.AddMessageToLog(msg, 0);
                    }
                }
            }
        }
    }
    
    // Update searches
    if (!scanner->VH_Active) {
        if (close > prev_high && bodySize >= minBodySize) {
            scanner->VH_Active = true;
            scanner->VH_PeakHigh = high;
            scanner->VH_PeakBar = barIndex;
            scanner->VH_ConfirmLevel = low;
            scanner->VH_ConfirmLevelBar = barIndex;
            // Start 15-sec range vanaf laatste VL of vanaf start
            scanner->VH_StartBar_15s = (scanner->LastVL_PlotBar_15s >= 0) ? 
                                       scanner->LastVL_PlotBar_15s : tfBar->StartBar_15s;
            
            // ALTIJD loggen bij search start
            SCString msg;
            msg.Format("[%s] *** VH SEARCH STARTED *** Bar %d, Close(%.2f)>PrevHigh(%.2f), Peak=%.2f, ConfirmLvl=%.2f, NextPlot=%s",
                tfName, barIndex, close, prev_high, high, low, 
                (scanner->WhatToPlotNext == s_TimeframeScanner::PLOT_VH ? "VH" : "VL"));
            sc.AddMessageToLog(msg, 0);
        }
    } else {
        if (high > scanner->VH_PeakHigh) {
            scanner->VH_PeakHigh = high;
            scanner->VH_PeakBar = barIndex;
        }
        
        if (close > prev_high && bodySize >= minBodySize) {
            scanner->VH_ConfirmLevel = low;
            scanner->VH_ConfirmLevelBar = barIndex;
        }
    }
    
    if (!scanner->VL_Active) {
        if (close < prev_low && bodySize >= minBodySize) {
            scanner->VL_Active = true;
            scanner->VL_TroughLow = low;
            scanner->VL_TroughBar = barIndex;
            scanner->VL_ConfirmLevel = high;
            scanner->VL_ConfirmLevelBar = barIndex;
            // Start 15-sec range vanaf laatste VH of vanaf start
            scanner->VL_StartBar_15s = (scanner->LastVH_PlotBar_15s >= 0) ? 
                                       scanner->LastVH_PlotBar_15s : tfBar->StartBar_15s;
            
            // ALTIJD loggen bij search start
            SCString msg;
            msg.Format("[%s] *** VL SEARCH STARTED *** Bar %d, Close(%.2f)<PrevLow(%.2f), Trough=%.2f, ConfirmLvl=%.2f, NextPlot=%s",
                tfName, barIndex, close, prev_low, low, high,
                (scanner->WhatToPlotNext == s_TimeframeScanner::PLOT_VH ? "VH" : "VL"));
            sc.AddMessageToLog(msg, 0);
        }
    } else {
        if (low < scanner->VL_TroughLow) {
            scanner->VL_TroughLow = low;
            scanner->VL_TroughBar = barIndex;
        }
        
        if (close < prev_low && bodySize >= minBodySize) {
            scanner->VL_ConfirmLevel = high;
            scanner->VL_ConfirmLevelBar = barIndex;
        }
    }
    
    scanner->LastProcessedBar = barIndex;
}

// ============================================================================
// MAIN STUDY FUNCTION
// ============================================================================
SCSFExport scsf_VHVLScanner_MultiTF(SCStudyInterfaceRef sc)
{
    // ========================================================================
    // INPUTS
    // ========================================================================
    // 15-sec
    SCInputRef i_15s_Enabled = sc.Input[0];
    SCInputRef i_15s_SymbolColor = sc.Input[1];
    SCInputRef i_15s_SymbolSize = sc.Input[2];
    SCInputRef i_15s_SymbolOffset = sc.Input[3];
    SCInputRef i_15s_DrawStyle = sc.Input[4];
    
    // 1-min
    SCInputRef i_1m_Enabled = sc.Input[10];
    SCInputRef i_1m_SymbolColor = sc.Input[11];
    SCInputRef i_1m_SymbolSize = sc.Input[12];
    SCInputRef i_1m_SymbolOffset = sc.Input[13];
    SCInputRef i_1m_DrawStyle = sc.Input[14];
    
    // 5-min
    SCInputRef i_5m_Enabled = sc.Input[20];
    SCInputRef i_5m_SymbolColor = sc.Input[21];
    SCInputRef i_5m_SymbolSize = sc.Input[22];
    SCInputRef i_5m_SymbolOffset = sc.Input[23];
    SCInputRef i_5m_DrawStyle = sc.Input[24];
    
    // 15-min
    SCInputRef i_15m_Enabled = sc.Input[30];
    SCInputRef i_15m_SymbolColor = sc.Input[31];
    SCInputRef i_15m_SymbolSize = sc.Input[32];
    SCInputRef i_15m_SymbolOffset = sc.Input[33];
    SCInputRef i_15m_DrawStyle = sc.Input[34];
    
    // General
    SCInputRef i_LineWidth = sc.Input[50];
    SCInputRef i_DetailedLog = sc.Input[51];

    // ========================================================================
    // SUBGRAPHS
    // ========================================================================
    SCSubgraphRef sg_15s_VH = sc.Subgraph[0];
    SCSubgraphRef sg_15s_VL = sc.Subgraph[1];
    
    SCSubgraphRef sg_1m_VH = sc.Subgraph[2];
    SCSubgraphRef sg_1m_VL = sc.Subgraph[3];
    
    SCSubgraphRef sg_5m_VH = sc.Subgraph[4];
    SCSubgraphRef sg_5m_VL = sc.Subgraph[5];
    
    SCSubgraphRef sg_15m_VH = sc.Subgraph[6];
    SCSubgraphRef sg_15m_VL = sc.Subgraph[7];

    // ========================================================================
    // DEFAULTS
    // ========================================================================
    if (sc.SetDefaults)
    {
        sc.GraphName = "VH/VL Multi-Timeframe Scanner";
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
        i_15s_DrawStyle.SetInt(1);  // POINT default
        
        // 1-min Inputs
        i_1m_Enabled.Name = "1min: Enabled";
        i_1m_Enabled.SetYesNo(true);
        i_1m_SymbolColor.Name = "1min: Symbol Color";
        i_1m_SymbolColor.SetColor(RGB(255, 255, 0));  // Yellow
        i_1m_SymbolSize.Name = "1min: Symbol Size";
        i_1m_SymbolSize.SetInt(6);
        i_1m_SymbolOffset.Name = "1min: Symbol Offset (ticks)";
        i_1m_SymbolOffset.SetInt(5);
        i_1m_DrawStyle.Name = "1min: Draw Style (0=Arrow, 1=Point, 2=Star, 3=Hollow Circle)";
        i_1m_DrawStyle.SetInt(1);  // POINT default
        
        // 5-min Inputs
        i_5m_Enabled.Name = "5min: Enabled";
        i_5m_Enabled.SetYesNo(true);
        i_5m_SymbolColor.Name = "5min: Symbol Color";
        i_5m_SymbolColor.SetColor(RGB(0, 255, 255));  // Cyan
        i_5m_SymbolSize.Name = "5min: Symbol Size";
        i_5m_SymbolSize.SetInt(8);
        i_5m_SymbolOffset.Name = "5min: Symbol Offset (ticks)";
        i_5m_SymbolOffset.SetInt(7);
        i_5m_DrawStyle.Name = "5min: Draw Style (0=Arrow, 1=Point, 2=Star, 3=Hollow Circle)";
        i_5m_DrawStyle.SetInt(2);  // STAR default
        
        // 15-min Inputs
        i_15m_Enabled.Name = "15min: Enabled";
        i_15m_Enabled.SetYesNo(true);
        i_15m_SymbolColor.Name = "15min: Symbol Color";
        i_15m_SymbolColor.SetColor(RGB(255, 0, 255));  // Magenta
        i_15m_SymbolSize.Name = "15min: Symbol Size";
        i_15m_SymbolSize.SetInt(10);
        i_15m_SymbolOffset.Name = "15min: Symbol Offset (ticks)";
        i_15m_SymbolOffset.SetInt(10);
        i_15m_DrawStyle.Name = "15min: Draw Style (0=Arrow, 1=Point, 2=Star, 3=Hollow Circle)";
        i_15m_DrawStyle.SetInt(0);  // ARROW default
        
        // General Inputs
        i_LineWidth.Name = "Confirm Line Width";
        i_LineWidth.SetInt(2);
        i_DetailedLog.Name = "Enable Detailed Logging";
        i_DetailedLog.SetYesNo(false);

        // 15-sec Subgraphs
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
        
        // 1-min Subgraphs
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
        
        // 5-min Subgraphs
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
        
        // 15-min Subgraphs
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

    // ========================================================================
    // PERSISTENT DATA
    // ========================================================================
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

    // ========================================================================
    // MAIN PROCESSING
    // ========================================================================
    int i = sc.Index;
    if (i < 2) return;

    // Detect full recalculation: als we terug gaan in tijd, reset state
    if (i < p_15s->LastProcessedBar - 1) {
        // Recalculation detected - reset alle state
        *p_15s = s_TimeframeScanner();
        *p_1m = s_TimeframeScanner();
        *p_5m = s_TimeframeScanner();
        *p_15m = s_TimeframeScanner();
        p_1m_CurrentBar->Reset();
        p_5m_CurrentBar->Reset();
        p_15m_CurrentBar->Reset();
        
        if (i_DetailedLog.GetYesNo()) {
            SCString msg;
            msg.Format("RECALCULATION DETECTED at bar %d (LastProcessed was %d) - FULL RESET ALL TIMEFRAMES", i, p_15s->LastProcessedBar);
            sc.AddMessageToLog(msg, 0);
        }
    }

    // ========================================================================
    // 15-SEC TIMEFRAME PROCESSING
    // ========================================================================
    if (i_15s_Enabled.GetYesNo()) {
        // Simpele logica: verwerk bar i als die GROTER is dan LastProcessedBar
        // Voor realtime: verwerk i-1 (gesloten bar), maar sla alleen op als nieuw
        bool isLastBar = (i == sc.ArraySize - 1);
        int barToProcess = isLastBar ? (i - 1) : i;
        
        // Skip als we deze bar al verwerkt hebben
        if (barToProcess <= p_15s->LastProcessedBar) {
            // Al verwerkt - skip naar visualisatie
        } else {
        // ====================================================================
        // VERWERK NIEUWE GESLOTEN BAR
        // ====================================================================
        p_15s->LastProcessedBar = barToProcess;  // Markeer deze bar als verwerkt!
        
        float high = sc.High[barToProcess];
        float low = sc.Low[barToProcess];
        float close = sc.Close[barToProcess];
        float prev_high = sc.High[barToProcess - 1];
        float prev_low = sc.Low[barToProcess - 1];
        
        if (i_DetailedLog.GetYesNo()) {
            const char* lastPlotted = "NONE";
            if (p_15s->LastPlottedType == s_TimeframeScanner::LAST_VH) lastPlotted = "VH";
            else if (p_15s->LastPlottedType == s_TimeframeScanner::LAST_VL) lastPlotted = "VL";
            
            SCString msg;
            msg.Format("[Bar %d] Processing - NextPlot=%s, LastPlotted=%s, H=%.2f L=%.2f C=%.2f",
                barToProcess,
                (p_15s->WhatToPlotNext == s_TimeframeScanner::PLOT_VH ? "VH" : "VL"),
                lastPlotted,
                high, low, close);
            sc.AddMessageToLog(msg, 0);
        }

        // ====================================================================
        // STAP 1: CHECK BEVESTIGINGEN (voor plots)
        // ====================================================================
        float open = sc.Open[barToProcess];
        float bodySize = (close > open) ? (close - open) : (open - close);  // Absolute body size
        float minBodySize = 2.0f * sc.TickSize;
        
        bool vh_confirmed = (p_15s->VH_Active && 
                            close < p_15s->VH_ConfirmLevel &&  // Close moet ONDER niveau
                            bodySize >= minBodySize &&  // Body moet minimaal 2 ticks zijn
                            p_15s->WhatToPlotNext == s_TimeframeScanner::PLOT_VH &&
                            p_15s->LastPlottedType != s_TimeframeScanner::LAST_VH);  // GEEN dubbele VH!
        
        bool vl_confirmed = (p_15s->VL_Active && 
                            close > p_15s->VL_ConfirmLevel &&  // Close moet BOVEN niveau
                            bodySize >= minBodySize &&  // Body moet minimaal 2 ticks zijn
                            p_15s->WhatToPlotNext == s_TimeframeScanner::PLOT_VL &&
                            p_15s->LastPlottedType != s_TimeframeScanner::LAST_VL);  // GEEN dubbele VL!

        if (i_DetailedLog.GetYesNo()) {
            SCString msg;
            msg.Format("  Body: Open=%.2f Close=%.2f Size=%.2f MinReq=%.2f BodyOK=%d",
                open, close, bodySize, minBodySize, (bodySize >= minBodySize ? 1 : 0));
            sc.AddMessageToLog(msg, 0);
            
            if (p_15s->VH_Active) {
                msg.Format("  VH: Active, Peak=%.2f@%d, ConfirmLvl=%.2f, Close<Lvl=%d, BodyOK=%d, CanPlot=%d => Confirmed=%d",
                    p_15s->VH_PeakHigh, p_15s->VH_PeakBar, p_15s->VH_ConfirmLevel,
                    (close < p_15s->VH_ConfirmLevel ? 1 : 0),
                    (bodySize >= minBodySize ? 1 : 0),
                    (p_15s->WhatToPlotNext == s_TimeframeScanner::PLOT_VH ? 1 : 0),
                    vh_confirmed);
                sc.AddMessageToLog(msg, 0);
            }
            if (p_15s->VL_Active) {
                msg.Format("  VL: Active, Trough=%.2f@%d, ConfirmLvl=%.2f, Close>Lvl=%d, BodyOK=%d, CanPlot=%d => Confirmed=%d",
                    p_15s->VL_TroughLow, p_15s->VL_TroughBar, p_15s->VL_ConfirmLevel,
                    (close > p_15s->VL_ConfirmLevel ? 1 : 0),
                    (bodySize >= minBodySize ? 1 : 0),
                    (p_15s->WhatToPlotNext == s_TimeframeScanner::PLOT_VL ? 1 : 0),
                    vl_confirmed);
                sc.AddMessageToLog(msg, 0);
            }
        }

        // Verwerk bevestigingen
        if (vh_confirmed) {
            float plotPrice = p_15s->VH_PeakHigh + (i_15s_SymbolOffset.GetInt() * sc.TickSize);
            sg_15s_VH[p_15s->VH_PeakBar] = plotPrice;
            
            // Update laatste plot type
            p_15s->LastPlottedType = s_TimeframeScanner::LAST_VH;
            
            if (i_DetailedLog.GetYesNo()) {
                SCString msg;
                msg.Format("*** VH PLOTTED at Bar %d (Peak=%.2f, ConfirmLvl=%.2f) ***",
                    p_15s->VH_PeakBar, p_15s->VH_PeakHigh, p_15s->VH_ConfirmLevel);
                sc.AddMessageToLog(msg, 0);
            }
            
            // Switch traffic light - nu wachten op VL
            p_15s->WhatToPlotNext = s_TimeframeScanner::PLOT_VL;
            
            if (i_DetailedLog.GetYesNo()) {
                SCString msg;
                msg.Format("    Traffic light switched: Now waiting for VL");
                sc.AddMessageToLog(msg, 0);
            }
            
            // Reset VH search (net geplot)
            p_15s->VH_Active = false;
            p_15s->VH_PeakHigh = 0.0f;
            p_15s->VH_PeakBar = -1;
            p_15s->VH_ConfirmLevel = 0.0f;
            p_15s->VH_ConfirmLevelBar = -1;
            
            // Check: is VL search actief EN close is al boven het trough niveau?
            // Dan werkt de VL search aan een oude trough, reset
            if (p_15s->VL_Active && p_15s->VL_TroughBar >= 0) {
                // Check of we sinds de trough bar al boven de confirm level zijn gegaan
                bool vlBroken = false;
                for (int checkBar = p_15s->VL_TroughBar + 1; checkBar <= barToProcess; checkBar++) {
                    if (sc.Close[checkBar] > p_15s->VL_ConfirmLevel) {
                        vlBroken = true;
                        break;
                    }
                }
                
                if (vlBroken) {
                    if (i_DetailedLog.GetYesNo()) {
                        SCString msg;
                        msg.Format("    VL search was already broken (close went > confirm while blocked), RESET");
                        sc.AddMessageToLog(msg, 0);
                    }
                    p_15s->VL_Active = false;
                    p_15s->VL_TroughLow = 0.0f;
                    p_15s->VL_TroughBar = -1;
                    p_15s->VL_ConfirmLevel = 0.0f;
                    p_15s->VL_ConfirmLevelBar = -1;
                }
            }
        }
        
        if (vl_confirmed) {
            float plotPrice = p_15s->VL_TroughLow - (i_15s_SymbolOffset.GetInt() * sc.TickSize);
            sg_15s_VL[p_15s->VL_TroughBar] = plotPrice;
            
            // Update laatste plot type
            p_15s->LastPlottedType = s_TimeframeScanner::LAST_VL;
            
            if (i_DetailedLog.GetYesNo()) {
                SCString msg;
                msg.Format("*** VL PLOTTED at Bar %d (Trough=%.2f, ConfirmLvl=%.2f) ***",
                    p_15s->VL_TroughBar, p_15s->VL_TroughLow, p_15s->VL_ConfirmLevel);
                sc.AddMessageToLog(msg, 0);
            }
            
            // Switch traffic light - nu wachten op VH
            p_15s->WhatToPlotNext = s_TimeframeScanner::PLOT_VH;
            
            if (i_DetailedLog.GetYesNo()) {
                SCString msg;
                msg.Format("    Traffic light switched: Now waiting for VH");
                sc.AddMessageToLog(msg, 0);
            }
            
            // Reset VL search (net geplot)
            p_15s->VL_Active = false;
            p_15s->VL_TroughLow = 0.0f;
            p_15s->VL_TroughBar = -1;
            p_15s->VL_ConfirmLevel = 0.0f;
            p_15s->VL_ConfirmLevelBar = -1;
            
            // Check: is VH search actief EN close is al onder het peak niveau?
            // Dan werkt de VH search aan een oude peak, reset
            if (p_15s->VH_Active && p_15s->VH_PeakBar >= 0) {
                // Check of we sinds de peak bar al onder de confirm level zijn gegaan
                bool vhBroken = false;
                for (int checkBar = p_15s->VH_PeakBar + 1; checkBar <= barToProcess; checkBar++) {
                    if (sc.Close[checkBar] < p_15s->VH_ConfirmLevel) {
                        vhBroken = true;
                        break;
                    }
                }
                
                if (vhBroken) {
                    if (i_DetailedLog.GetYesNo()) {
                        SCString msg;
                        msg.Format("    VH search was already broken (close went < confirm while blocked), RESET");
                        sc.AddMessageToLog(msg, 0);
                    }
                    p_15s->VH_Active = false;
                    p_15s->VH_PeakHigh = 0.0f;
                    p_15s->VH_PeakBar = -1;
                    p_15s->VH_ConfirmLevel = 0.0f;
                    p_15s->VH_ConfirmLevelBar = -1;
                }
            }
        }

        // ====================================================================
        // STAP 2: UPDATE PARALLELLE SEARCHES
        // ====================================================================
        
        // VH Search (altijd actief)
        if (!p_15s->VH_Active) {
            // Start nieuwe VH search: close > prev_high EN body >= 2 ticks
            if (close > prev_high && bodySize >= minBodySize) {
                p_15s->VH_Active = true;
                p_15s->VH_PeakHigh = high;
                p_15s->VH_PeakBar = barToProcess;
                p_15s->VH_ConfirmLevel = low;
                p_15s->VH_ConfirmLevelBar = barToProcess;
                
                if (i_DetailedLog.GetYesNo()) {
                    SCString msg;
                    msg.Format("  VH STARTED: Bar %d, Close(%.2f)>PrevHigh(%.2f), Peak=%.2f, ConfirmLvl=%.2f, Body=%.2f",
                        barToProcess, close, prev_high, high, low, bodySize);
                    sc.AddMessageToLog(msg, 0);
                }
            } else if (close > prev_high && i_DetailedLog.GetYesNo()) {
                SCString msg;
                msg.Format("  VH NOT STARTED: Bar %d, Close>PrevHigh but body too small (%.2f < %.2f)",
                    barToProcess, bodySize, minBodySize);
                sc.AddMessageToLog(msg, 0);
            }
        } else {
            // Update bestaande VH search
            bool peakUpdated = false;
            bool anchorUpdated = false;
            
            // Check voor nieuwe PEAK (elke hogere high)
            if (high > p_15s->VH_PeakHigh) {
                p_15s->VH_PeakHigh = high;
                p_15s->VH_PeakBar = barToProcess;
                peakUpdated = true;
            }
            
            // Check voor nieuwe ANCHOR (close > prev_high EN body >= 2 ticks)
            if (close > prev_high && bodySize >= minBodySize) {
                p_15s->VH_ConfirmLevel = low;
                p_15s->VH_ConfirmLevelBar = barToProcess;
                anchorUpdated = true;
            }
            
            if (i_DetailedLog.GetYesNo() && (peakUpdated || anchorUpdated)) {
                SCString msg;
                if (peakUpdated && anchorUpdated) {
                    msg.Format("  VH UPDATED: Bar %d, NewPeak=%.2f, NewAnchor(ConfirmLvl=%.2f), Body=%.2f",
                        barToProcess, high, low, bodySize);
                } else if (peakUpdated) {
                    msg.Format("  VH PEAK UPDATED: Bar %d, NewPeak=%.2f (anchor unchanged, ConfirmLvl=%.2f)",
                        barToProcess, high, p_15s->VH_ConfirmLevel);
                } else if (anchorUpdated) {
                    msg.Format("  VH ANCHOR UPDATED: Bar %d, NewConfirmLvl=%.2f (peak unchanged=%.2f)",
                        barToProcess, low, p_15s->VH_PeakHigh);
                }
                sc.AddMessageToLog(msg, 0);
            }
        }
        
        // VL Search (altijd actief)
        if (!p_15s->VL_Active) {
            // Start nieuwe VL search: close < prev_low EN body >= 2 ticks
            if (close < prev_low && bodySize >= minBodySize) {
                p_15s->VL_Active = true;
                p_15s->VL_TroughLow = low;
                p_15s->VL_TroughBar = barToProcess;
                p_15s->VL_ConfirmLevel = high;
                p_15s->VL_ConfirmLevelBar = barToProcess;
                
                if (i_DetailedLog.GetYesNo()) {
                    SCString msg;
                    msg.Format("  VL STARTED: Bar %d, Close(%.2f)<PrevLow(%.2f), Trough=%.2f, ConfirmLvl=%.2f, Body=%.2f",
                        barToProcess, close, prev_low, low, high, bodySize);
                    sc.AddMessageToLog(msg, 0);
                }
            } else if (close < prev_low && i_DetailedLog.GetYesNo()) {
                SCString msg;
                msg.Format("  VL NOT STARTED: Bar %d, Close<PrevLow but body too small (%.2f < %.2f)",
                    barToProcess, bodySize, minBodySize);
                sc.AddMessageToLog(msg, 0);
            }
        } else {
            // Update bestaande VL search
            bool troughUpdated = false;
            bool anchorUpdated = false;
            
            // Check voor nieuwe TROUGH (elke lagere low)
            if (low < p_15s->VL_TroughLow) {
                p_15s->VL_TroughLow = low;
                p_15s->VL_TroughBar = barToProcess;
                troughUpdated = true;
            }
            
            // Check voor nieuwe ANCHOR (close < prev_low EN body >= 2 ticks)
            if (close < prev_low && bodySize >= minBodySize) {
                p_15s->VL_ConfirmLevel = high;
                p_15s->VL_ConfirmLevelBar = barToProcess;
                anchorUpdated = true;
            }
            
            if (i_DetailedLog.GetYesNo() && (troughUpdated || anchorUpdated)) {
                SCString msg;
                if (troughUpdated && anchorUpdated) {
                    msg.Format("  VL UPDATED: Bar %d, NewTrough=%.2f, NewAnchor(ConfirmLvl=%.2f), Body=%.2f",
                        barToProcess, low, high, bodySize);
                } else if (troughUpdated) {
                    msg.Format("  VL TROUGH UPDATED: Bar %d, NewTrough=%.2f (anchor unchanged, ConfirmLvl=%.2f)",
                        barToProcess, low, p_15s->VL_ConfirmLevel);
                } else if (anchorUpdated) {
                    msg.Format("  VL ANCHOR UPDATED: Bar %d, NewConfirmLvl=%.2f (trough unchanged=%.2f)",
                        barToProcess, high, p_15s->VL_TroughLow);
                }
                sc.AddMessageToLog(msg, 0);
            }
        }
    } // Einde processing block

    // ========================================================================
    // STAP 3: VISUALISATIE - CONFIRM LIJNEN (alle timeframes)
    // ========================================================================
    if (i == sc.ArraySize - 1) {
        // Helper functie om lijn te tekenen
        auto DrawConfirmLine = [&](int lineNumber, s_TimeframeScanner* scanner, int color) {
            sc.DeleteACSChartDrawing(sc.ChartNumber, DRAWING_LINE, lineNumber);
            
            bool shouldDrawLine = false;
            int lineBeginBar = -1;
            float lineValue = 0.0f;
            
            if (scanner->WhatToPlotNext == s_TimeframeScanner::PLOT_VH && scanner->VH_Active) {
                shouldDrawLine = true;
                lineBeginBar = scanner->VH_ConfirmLevelBar;
                lineValue = scanner->VH_ConfirmLevel;
            } else if (scanner->WhatToPlotNext == s_TimeframeScanner::PLOT_VL && scanner->VL_Active) {
                shouldDrawLine = true;
                lineBeginBar = scanner->VL_ConfirmLevelBar;
                lineValue = scanner->VL_ConfirmLevel;
            }
            
            if (shouldDrawLine && lineBeginBar >= 0) {
                int lineEndBar = scanner->LastProcessedBar;
                if (lineEndBar < lineBeginBar + 3) {
                    lineEndBar = lineBeginBar + 3;
                }
                
                s_UseTool tool;
                tool.DrawingType = DRAWING_LINE;
                tool.LineNumber = lineNumber;
                tool.AddMethod = UTAM_ADD_OR_ADJUST;
                tool.BeginIndex = lineBeginBar;
                tool.BeginValue = lineValue;
                tool.EndIndex = i;  // Altijd tot nu
                tool.EndValue = lineValue;
                tool.Color = color;
                tool.LineWidth = i_LineWidth.GetInt();
                tool.LineStyle = LINESTYLE_SOLID;
                tool.ExtendLeft = false;
                tool.ExtendRight = false;
                sc.UseTool(tool);
            }
        };
        
        // Teken lijnen voor alle enabled timeframes
        if (i_15s_Enabled.GetYesNo()) {
            DrawConfirmLine(200001, p_15s, RGB(0, 0, 0));  // Black
        }
        if (i_1m_Enabled.GetYesNo()) {
            DrawConfirmLine(200002, p_1m, i_1m_SymbolColor.GetColor());  // 1min color
        }
        if (i_5m_Enabled.GetYesNo()) {
            DrawConfirmLine(200003, p_5m, i_5m_SymbolColor.GetColor());  // 5min color
        }
        if (i_15m_Enabled.GetYesNo()) {
            DrawConfirmLine(200004, p_15m, i_15m_SymbolColor.GetColor());  // 15min color
        }
    }
    } // End 15-sec processing
    
    // ========================================================================
    // HIGHER TIMEFRAMES PROCESSING
    // ========================================================================
    
    // Build and scan 1-minute bars
    if (i_1m_Enabled.GetYesNo()) {
        SCDateTime currentBarTime = sc.BaseDateTimeIn[i];
        
        // Check if we should start a new 1min bar
        if (ShouldStartNewBar(currentBarTime, 1, p_1m_CurrentBar->StartTime)) {
            // Process the completed bar
            if (p_1m_CurrentBar->IsComplete) {
                ScanHigherTFBar(sc, p_1m, p_1m_CurrentBar, sg_1m_VH, sg_1m_VL,
                               i_1m_SymbolOffset.GetInt(), "1MIN", i_DetailedLog.GetYesNo());
                p_1m_CurrentBar->SaveAsPrevious();
            }
            
            // Start new bar
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
            p_1m_CurrentBar->IsComplete = true;  // Wordt compleet bij volgende bar
        } else {
            // Update existing bar
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
    
    // Build and scan 5-minute bars
    if (i_5m_Enabled.GetYesNo()) {
        SCDateTime currentBarTime = sc.BaseDateTimeIn[i];
        
        if (ShouldStartNewBar(currentBarTime, 5, p_5m_CurrentBar->StartTime)) {
            if (p_5m_CurrentBar->IsComplete) {
                ScanHigherTFBar(sc, p_5m, p_5m_CurrentBar, sg_5m_VH, sg_5m_VL,
                               i_5m_SymbolOffset.GetInt(), "5MIN", i_DetailedLog.GetYesNo());
                p_5m_CurrentBar->SaveAsPrevious();
            }
            
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
            p_5m_CurrentBar->IsComplete = true;
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
    
    // Build and scan 15-minute bars
    if (i_15m_Enabled.GetYesNo()) {
        SCDateTime currentBarTime = sc.BaseDateTimeIn[i];
        
        if (ShouldStartNewBar(currentBarTime, 15, p_15m_CurrentBar->StartTime)) {
            if (p_15m_CurrentBar->IsComplete) {
                ScanHigherTFBar(sc, p_15m, p_15m_CurrentBar, sg_15m_VH, sg_15m_VL,
                               i_15m_SymbolOffset.GetInt(), "15MIN", i_DetailedLog.GetYesNo());
                p_15m_CurrentBar->SaveAsPrevious();
            }
            
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
            p_15m_CurrentBar->IsComplete = true;
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
