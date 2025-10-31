#include "sierrachart.h"

SCDLLName("VH/VL Scanner - Single Timeframe")

// ============================================================================
// SCANNER STATE
// ============================================================================
struct s_ScannerState {
    enum NextPlot { PLOT_VH, PLOT_VL };
    enum LastPlotted { NONE, LAST_VH, LAST_VL };
    
    NextPlot WhatToPlotNext = PLOT_VH;
    LastPlotted LastPlottedType = NONE;
    
    // VH search
    bool VH_Active = false;
    float VH_PeakHigh = 0.0f;
    int VH_PeakBar = -1;
    float VH_AnchorHigh = 0.0f;       // HIGH van huidige anker candle (voor nieuwe anker check)
    float VH_ConfirmLevel = 0.0f;     // LOW van huidige anker candle (voor bevestiging)
    int VH_ConfirmLevelBar = -1;
    
    // VL search
    bool VL_Active = false;
    float VL_TroughLow = 0.0f;
    int VL_TroughBar = -1;
    float VL_AnchorLow = 0.0f;        // LOW van huidige anker candle (voor nieuwe anker check)
    float VL_ConfirmLevel = 0.0f;     // HIGH van huidige anker candle (voor bevestiging)
    int VL_ConfirmLevelBar = -1;
    
    int LastProcessedBar = -1;
    
    // Zigzag
    int LastPlot_Bar = -1;
    float LastPlot_Price = 0.0f;
    int ZigzagLineCounter = 0;
};

// ============================================================================
// HELPER: Draw zigzag line
// ============================================================================
void DrawZigzagLine(
    SCStudyInterfaceRef& sc,
    s_ScannerState* state,
    int currentBar,
    float currentPrice,
    int color,
    int width
) {
    if (state->LastPlot_Bar >= 0 && state->LastPlot_Price > 0) {
        int lineNumber = 400000 + state->ZigzagLineCounter;
        
        s_UseTool tool;
        tool.DrawingType = DRAWING_LINE;
        tool.LineNumber = lineNumber;
        tool.AddMethod = UTAM_ADD_OR_ADJUST;
        tool.BeginIndex = state->LastPlot_Bar;
        tool.BeginValue = state->LastPlot_Price;
        tool.EndIndex = currentBar;
        tool.EndValue = currentPrice;
        tool.Color = color;
        tool.LineWidth = width;
        tool.LineStyle = LINESTYLE_SOLID;
        tool.ExtendLeft = false;
        tool.ExtendRight = false;
        sc.UseTool(tool);
        
        state->ZigzagLineCounter++;
    }
    
    state->LastPlot_Bar = currentBar;
    state->LastPlot_Price = currentPrice;
}

// ============================================================================
// MAIN STUDY FUNCTION
// ============================================================================
SCSFExport scsf_VHVLScanner_SingleTF(SCStudyInterfaceRef sc)
{
    // INPUTS
    SCInputRef i_SymbolColor = sc.Input[0];
    SCInputRef i_SymbolSize = sc.Input[1];
    SCInputRef i_SymbolOffset = sc.Input[2];
    SCInputRef i_ZigzagEnabled = sc.Input[5];
    SCInputRef i_ZigzagColor = sc.Input[6];
    SCInputRef i_ZigzagWidth = sc.Input[7];
    SCInputRef i_ConfirmLineColor = sc.Input[10];
    SCInputRef i_LineWidth = sc.Input[11];
    SCInputRef i_DetailedLog = sc.Input[12];

    // SUBGRAPHS
    SCSubgraphRef sg_VH = sc.Subgraph[0];
    SCSubgraphRef sg_VL = sc.Subgraph[1];

    // DEFAULTS
    if (sc.SetDefaults)
    {
        sc.GraphName = "VH/VL Scanner (Single TF)";
        sc.AutoLoop = 1;
        sc.GraphRegion = 0;
        sc.UpdateAlways = 1;

        i_SymbolColor.Name = "Symbol Color";
        i_SymbolColor.SetColor(RGB(255, 255, 255));
        i_SymbolSize.Name = "Symbol Size";
        i_SymbolSize.SetInt(6);
        i_SymbolOffset.Name = "Symbol Offset (ticks)";
        i_SymbolOffset.SetInt(3);
        
        i_ZigzagEnabled.Name = "Zigzag Enabled";
        i_ZigzagEnabled.SetYesNo(false);
        i_ZigzagColor.Name = "Zigzag Color";
        i_ZigzagColor.SetColor(RGB(0, 255, 0));
        i_ZigzagWidth.Name = "Zigzag Width";
        i_ZigzagWidth.SetInt(2);
        
        i_ConfirmLineColor.Name = "Confirm Line Color";
        i_ConfirmLineColor.SetColor(RGB(0, 0, 0));
        i_LineWidth.Name = "Line Width";
        i_LineWidth.SetInt(2);
        i_DetailedLog.Name = "Enable Detailed Logging";
        i_DetailedLog.SetYesNo(false);

        sg_VH.Name = "VH";
        sg_VH.DrawStyle = DRAWSTYLE_POINT;
        sg_VH.PrimaryColor = RGB(0, 255, 0);
        sg_VH.LineWidth = 6;
        sg_VH.DrawZeros = false;
        
        sg_VL.Name = "VL";
        sg_VL.DrawStyle = DRAWSTYLE_POINT;
        sg_VL.PrimaryColor = RGB(255, 0, 0);
        sg_VL.LineWidth = 6;
        sg_VL.DrawZeros = false;
        
        return;
    }

    // PERSISTENT DATA
    s_ScannerState* state = (s_ScannerState*)sc.GetPersistentPointer(1);
    
    if (state == NULL) {
        state = new s_ScannerState();
        sc.SetPersistentPointer(1, state);
    }

    if (sc.LastCallToFunction) {
        if (state != NULL) {
            delete state;
            sc.SetPersistentPointer(1, NULL);
        }
        return;
    }

    int i = sc.Index;
    if (i < 2) return;

    // Detect recalculation
    if (i < state->LastProcessedBar - 1) {
        *state = s_ScannerState();
        
        for (int j = 0; j < sc.ArraySize; j++) {
            sg_VH[j] = 0;
            sg_VL[j] = 0;
        }
        
        sc.DeleteACSChartDrawing(sc.ChartNumber, DRAWING_LINE, 500001);
        
        SCString msg;
        msg.Format("RECALCULATION DETECTED - FULL RESET");
        sc.AddMessageToLog(msg, 0);
    }

    // ========================================================================
    // PROCESS BAR
    // ========================================================================
    bool isLastBar = (i == sc.ArraySize - 1);
    int barToProcess = isLastBar ? (i - 1) : i;
    
    if (barToProcess > state->LastProcessedBar) {
        state->LastProcessedBar = barToProcess;
        
        float high = sc.High[barToProcess];
        float low = sc.Low[barToProcess];
        float close = sc.Close[barToProcess];
        float open = sc.Open[barToProcess];
        float prev_high = sc.High[barToProcess - 1];
        float prev_low = sc.Low[barToProcess - 1];
        
        // ====================================================================
        // CHECK CONFIRMATIONS
        // ====================================================================
        bool vh_confirmed = (state->VH_Active && 
                            close < state->VH_ConfirmLevel &&
                            state->WhatToPlotNext == s_ScannerState::PLOT_VH &&
                            state->LastPlottedType != s_ScannerState::LAST_VH);
        
        bool vl_confirmed = (state->VL_Active && 
                            close > state->VL_ConfirmLevel &&
                            state->WhatToPlotNext == s_ScannerState::PLOT_VL &&
                            state->LastPlottedType != s_ScannerState::LAST_VL);

        // VH Confirmation
        if (vh_confirmed) {
            float plotPrice = state->VH_PeakHigh + (i_SymbolOffset.GetInt() * sc.TickSize);
            sg_VH[state->VH_PeakBar] = plotPrice;
            
            if (i_ZigzagEnabled.GetYesNo()) {
                DrawZigzagLine(sc, state, state->VH_PeakBar, plotPrice,
                              i_ZigzagColor.GetColor(), i_ZigzagWidth.GetInt());
            }
            
            state->LastPlottedType = s_ScannerState::LAST_VH;
            state->WhatToPlotNext = s_ScannerState::PLOT_VL;
            state->VH_Active = false;
            
            SCString msg;
            msg.Format("*** VH PLOTTED *** Bar %d, Close=%.2f < ConfirmLvl=%.2f, Peak=%.2f",
                state->VH_PeakBar, close, state->VH_ConfirmLevel, state->VH_PeakHigh);
            sc.AddMessageToLog(msg, 0);
            
            // Check stale VL
            if (state->VL_Active) {
                bool vlBroken = false;
                for (int checkBar = state->VL_TroughBar + 1; checkBar <= barToProcess; checkBar++) {
                    if (sc.Close[checkBar] > state->VL_ConfirmLevel) {
                        vlBroken = true;
                        break;
                    }
                }
                if (vlBroken) {
                    state->VL_Active = false;
                    if (i_DetailedLog.GetYesNo()) {
                        sc.AddMessageToLog("  VL search was stale, RESET", 0);
                    }
                }
            }
        }
        
        // VL Confirmation
        if (vl_confirmed) {
            float plotPrice = state->VL_TroughLow - (i_SymbolOffset.GetInt() * sc.TickSize);
            sg_VL[state->VL_TroughBar] = plotPrice;
            
            if (i_ZigzagEnabled.GetYesNo()) {
                DrawZigzagLine(sc, state, state->VL_TroughBar, plotPrice,
                              i_ZigzagColor.GetColor(), i_ZigzagWidth.GetInt());
            }
            
            state->LastPlottedType = s_ScannerState::LAST_VL;
            state->WhatToPlotNext = s_ScannerState::PLOT_VH;
            state->VL_Active = false;
            
            SCString msg;
            msg.Format("*** VL PLOTTED *** Bar %d, Close=%.2f > ConfirmLvl=%.2f, Trough=%.2f",
                state->VL_TroughBar, close, state->VL_ConfirmLevel, state->VL_TroughLow);
            sc.AddMessageToLog(msg, 0);
            
            // Check stale VH
            if (state->VH_Active) {
                bool vhBroken = false;
                for (int checkBar = state->VH_PeakBar + 1; checkBar <= barToProcess; checkBar++) {
                    if (sc.Close[checkBar] < state->VH_ConfirmLevel) {
                        vhBroken = true;
                        break;
                    }
                }
                if (vhBroken) {
                    state->VH_Active = false;
                    if (i_DetailedLog.GetYesNo()) {
                        sc.AddMessageToLog("  VH search was stale, RESET", 0);
                    }
                }
            }
        }

        // ====================================================================
        // UPDATE VH SEARCH
        // ====================================================================
        if (!state->VH_Active) {
            // Start VH search: bodyclose > prev_high (of gewoon high > prev voor eerste)
            if (close > prev_high) {
                state->VH_Active = true;
                state->VH_PeakHigh = high;
                state->VH_PeakBar = barToProcess;
                state->VH_AnchorHigh = high;      // HIGH van eerste anker
                state->VH_ConfirmLevel = low;     // LOW van eerste anker
                state->VH_ConfirmLevelBar = barToProcess;
                
                SCString msg;
                msg.Format("VH SEARCH STARTED at bar %d: AnchorHigh=%.2f, ConfirmLvl=%.2f (LOW), Close(%.2f)>PrevHigh(%.2f)",
                    barToProcess, high, low, close, prev_high);
                sc.AddMessageToLog(msg, 0);
            }
        } else {
            bool peakUpdated = false;
            bool anchorUpdated = false;
            
            // Peak: altijd bij hogere high
            if (high > state->VH_PeakHigh) {
                state->VH_PeakHigh = high;
                state->VH_PeakBar = barToProcess;
                peakUpdated = true;
            }
            
            // ANKER update: alleen bij bodyclose > AnchorHigh!
            if (close > state->VH_AnchorHigh) {
                state->VH_AnchorHigh = high;      // HIGH van nieuwe anker
                state->VH_ConfirmLevel = low;     // LOW van nieuwe anker
                state->VH_ConfirmLevelBar = barToProcess;
                anchorUpdated = true;
            }
            
            if (i_DetailedLog.GetYesNo() && (peakUpdated || anchorUpdated)) {
                SCString msg;
                if (peakUpdated && anchorUpdated) {
                    msg.Format("VH UPDATED at bar %d: NewPeak=%.2f, NewAnchor(High=%.2f, ConfirmLvl=%.2f)",
                        barToProcess, high, high, low);
                } else if (peakUpdated) {
                    msg.Format("VH PEAK UPDATED at bar %d: NewPeak=%.2f (Anchor unchanged)", barToProcess, high);
                } else {
                    msg.Format("VH ANCHOR UPDATED at bar %d: Close(%.2f)>AnchorHigh(%.2f), NewConfirmLvl=%.2f",
                        barToProcess, close, state->VH_AnchorHigh, low);
                }
                sc.AddMessageToLog(msg, 0);
            }
        }
        
        // ====================================================================
        // UPDATE VL SEARCH
        // ====================================================================
        if (!state->VL_Active) {
            // Start VL search: bodyclose < prev_low
            if (close < prev_low) {
                state->VL_Active = true;
                state->VL_TroughLow = low;
                state->VL_TroughBar = barToProcess;
                state->VL_AnchorLow = low;        // LOW van eerste anker
                state->VL_ConfirmLevel = high;    // HIGH van eerste anker
                state->VL_ConfirmLevelBar = barToProcess;
                
                SCString msg;
                msg.Format("VL SEARCH STARTED at bar %d: AnchorLow=%.2f, ConfirmLvl=%.2f (HIGH), Close(%.2f)<PrevLow(%.2f)",
                    barToProcess, low, high, close, prev_low);
                sc.AddMessageToLog(msg, 0);
            }
        } else {
            bool troughUpdated = false;
            bool anchorUpdated = false;
            
            // Trough: altijd bij lagere low
            if (low < state->VL_TroughLow) {
                state->VL_TroughLow = low;
                state->VL_TroughBar = barToProcess;
                troughUpdated = true;
            }
            
            // ANKER update: alleen bij bodyclose < AnchorLow!
            if (close < state->VL_AnchorLow) {
                state->VL_AnchorLow = low;        // LOW van nieuwe anker
                state->VL_ConfirmLevel = high;    // HIGH van nieuwe anker
                state->VL_ConfirmLevelBar = barToProcess;
                anchorUpdated = true;
            }
            
            if (i_DetailedLog.GetYesNo() && (troughUpdated || anchorUpdated)) {
                SCString msg;
                if (troughUpdated && anchorUpdated) {
                    msg.Format("VL UPDATED at bar %d: NewTrough=%.2f, NewAnchor(Low=%.2f, ConfirmLvl=%.2f)",
                        barToProcess, low, low, high);
                } else if (troughUpdated) {
                    msg.Format("VL TROUGH UPDATED at bar %d: NewTrough=%.2f (Anchor unchanged)", barToProcess, low);
                } else {
                    msg.Format("VL ANCHOR UPDATED at bar %d: Close(%.2f)<AnchorLow(%.2f), NewConfirmLvl=%.2f",
                        barToProcess, close, state->VL_AnchorLow, high);
                }
                sc.AddMessageToLog(msg, 0);
            }
        }
    }

    // ========================================================================
    // DRAW CONFIRM LINE
    // ========================================================================
    if (i == sc.ArraySize - 1) {
        const int LINE_NUMBER = 500001;
        sc.DeleteACSChartDrawing(sc.ChartNumber, DRAWING_LINE, LINE_NUMBER);
        
        bool shouldDrawLine = false;
        int lineBeginBar = -1;
        float lineValue = 0.0f;
        
        if (state->WhatToPlotNext == s_ScannerState::PLOT_VH && state->VH_Active) {
            shouldDrawLine = true;
            lineBeginBar = state->VH_ConfirmLevelBar;
            lineValue = state->VH_ConfirmLevel;
        } else if (state->WhatToPlotNext == s_ScannerState::PLOT_VL && state->VL_Active) {
            shouldDrawLine = true;
            lineBeginBar = state->VL_ConfirmLevelBar;
            lineValue = state->VL_ConfirmLevel;
        }
        
        if (shouldDrawLine && lineBeginBar >= 0) {
            int lineEndBar = i;
            if (lineEndBar < lineBeginBar + 3) {
                lineEndBar = lineBeginBar + 3;
            }
            
            s_UseTool tool;
            tool.DrawingType = DRAWING_LINE;
            tool.LineNumber = LINE_NUMBER;
            tool.AddMethod = UTAM_ADD_OR_ADJUST;
            tool.BeginIndex = lineBeginBar;
            tool.BeginValue = lineValue;
            tool.EndIndex = lineEndBar;
            tool.EndValue = lineValue;
            tool.Color = i_ConfirmLineColor.GetColor();
            tool.LineWidth = i_LineWidth.GetInt();
            tool.LineStyle = LINESTYLE_SOLID;
            tool.ExtendLeft = false;
            tool.ExtendRight = false;
            sc.UseTool(tool);
        }
    }
}
