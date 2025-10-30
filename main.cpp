#include "sierrachart.h"

SCDLLName("VH/VL Multi-Timeframe Scanner")

// ============================================================================
// TIMEFRAME SCANNER STRUCTURE
// ============================================================================
struct s_TimeframeScanner {
    enum NextPlot { PLOT_VH, PLOT_VL };
    
    NextPlot WhatToPlotNext = PLOT_VH;  // Traffic light: wat mag als volgende geplot worden
    
    // VH zoektocht (loopt ALTIJD parallel)
    bool VH_Active = false;
    float VH_PeakHigh = 0.0f;
    int VH_PeakBar = -1;
    float VH_ConfirmLevel = 0.0f;     // Low van de peak bar
    int VH_ConfirmLevelBar = -1;      // Bar waar de confirm level van is
    
    // VL zoektocht (loopt ALTIJD parallel)
    bool VL_Active = false;
    float VL_TroughLow = 0.0f;
    int VL_TroughBar = -1;
    float VL_ConfirmLevel = 0.0f;     // High van de trough bar
    int VL_ConfirmLevelBar = -1;      // Bar waar de confirm level van is
    
    int LastProcessedBar = -1;
};

// ============================================================================
// MAIN STUDY FUNCTION
// ============================================================================
SCSFExport scsf_VHVLScanner_MultiTF(SCStudyInterfaceRef sc)
{
    // ========================================================================
    // INPUTS
    // ========================================================================
    SCInputRef i_15s_Enabled = sc.Input[0];
    SCInputRef i_15s_LineColor = sc.Input[1];
    SCInputRef i_15s_SymbolColor = sc.Input[2];
    SCInputRef i_15s_SymbolSize = sc.Input[3];
    SCInputRef i_15s_SymbolOffset = sc.Input[4];
    SCInputRef i_LineWidth = sc.Input[10];
    SCInputRef i_DetailedLog = sc.Input[11];

    // ========================================================================
    // SUBGRAPHS
    // ========================================================================
    SCSubgraphRef sg_15s_VH = sc.Subgraph[0];
    SCSubgraphRef sg_15s_VL = sc.Subgraph[1];

    // ========================================================================
    // DEFAULTS
    // ========================================================================
    if (sc.SetDefaults)
    {
        sc.GraphName = "VH/VL Multi-Timeframe Scanner";
        sc.AutoLoop = 1;
        sc.GraphRegion = 0;
        sc.UpdateAlways = 1;

        i_15s_Enabled.Name = "15sec: Enabled";
        i_15s_Enabled.SetYesNo(true);
        
        i_15s_LineColor.Name = "15sec: Confirm Line Color";
        i_15s_LineColor.SetColor(RGB(0, 0, 0));
        
        i_15s_SymbolColor.Name = "15sec: Symbol Color";
        i_15s_SymbolColor.SetColor(RGB(255, 255, 255));
        
        i_15s_SymbolSize.Name = "15sec: Symbol Size";
        i_15s_SymbolSize.SetInt(8);
        
        i_15s_SymbolOffset.Name = "15sec: Symbol Offset (ticks)";
        i_15s_SymbolOffset.SetInt(3);
        
        i_LineWidth.Name = "Confirm Line Width";
        i_LineWidth.SetInt(2);
        
        i_DetailedLog.Name = "Enable Detailed Logging";
        i_DetailedLog.SetYesNo(false);

        sg_15s_VH.Name = "15s VH";
        sg_15s_VH.DrawStyle = DRAWSTYLE_POINT;  // Punt/circle
        sg_15s_VH.PrimaryColor = RGB(0, 255, 0);
        sg_15s_VH.LineWidth = 8;
        sg_15s_VH.DrawZeros = false;
        
        sg_15s_VL.Name = "15s VL";
        sg_15s_VL.DrawStyle = DRAWSTYLE_POINT;  // Punt/circle
        sg_15s_VL.PrimaryColor = RGB(255, 0, 0);
        sg_15s_VL.LineWidth = 8;
        sg_15s_VL.DrawZeros = false;
        
        return;
    }

    // ========================================================================
    // PERSISTENT DATA
    // ========================================================================
    s_TimeframeScanner* p_15s = (s_TimeframeScanner*)sc.GetPersistentPointer(1);
    
    if (p_15s == NULL) {
        p_15s = new s_TimeframeScanner();
        sc.SetPersistentPointer(1, p_15s);
    }

    if (sc.LastCallToFunction) {
        if (p_15s != NULL) {
            delete p_15s;
            sc.SetPersistentPointer(1, NULL);
        }
        return;
    }

    // ========================================================================
    // MAIN PROCESSING
    // ========================================================================
    if (!i_15s_Enabled.GetYesNo()) return;
    
    int i = sc.Index;
    if (i < 2) return;

    bool isLastBar = (i == sc.ArraySize - 1);
    
    // Bepaal of we moeten verwerken
    bool shouldProcess = false;
    int barToProcess = i;
    
    if (!isLastBar) {
        shouldProcess = true;
        barToProcess = i;
    } else {
        if (i > p_15s->LastProcessedBar && i > 0) {
            shouldProcess = true;
            barToProcess = i - 1;
        }
    }

    if (!shouldProcess) {
        // Skip - alleen visualisatie
    } else {
        // ====================================================================
        // VERWERK GESLOTEN BAR
        // ====================================================================
        p_15s->LastProcessedBar = barToProcess;
        
        float high = sc.High[barToProcess];
        float low = sc.Low[barToProcess];
        float close = sc.Close[barToProcess];
        float prev_high = sc.High[barToProcess - 1];
        float prev_low = sc.Low[barToProcess - 1];
        
        if (i_DetailedLog.GetYesNo()) {
            SCString msg;
            msg.Format("[Bar %d] Processing - NextPlot=%s, H=%.2f L=%.2f C=%.2f",
                barToProcess,
                (p_15s->WhatToPlotNext == s_TimeframeScanner::PLOT_VH ? "VH" : "VL"),
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
                            close < p_15s->VH_ConfirmLevel &&
                            bodySize >= minBodySize &&  // Body moet minimaal 2 ticks zijn
                            p_15s->WhatToPlotNext == s_TimeframeScanner::PLOT_VH);
        
        bool vl_confirmed = (p_15s->VL_Active && 
                            close > p_15s->VL_ConfirmLevel &&
                            bodySize >= minBodySize &&  // Body moet minimaal 2 ticks zijn
                            p_15s->WhatToPlotNext == s_TimeframeScanner::PLOT_VL);

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
            
            // Reset VH search - nieuwe zoektocht naar VH begint
            p_15s->VH_Active = false;
            p_15s->VH_PeakHigh = 0.0f;
            p_15s->VH_PeakBar = -1;
            p_15s->VH_ConfirmLevel = 0.0f;
            p_15s->VH_ConfirmLevelBar = -1;
            
            // BELANGRIJK: Als huidige bar al een VL trigger is, start VL search direct
            if (!p_15s->VL_Active && close < prev_low) {
                p_15s->VL_Active = true;
                p_15s->VL_TroughLow = low;
                p_15s->VL_TroughBar = barToProcess;
                p_15s->VL_ConfirmLevel = high;
                p_15s->VL_ConfirmLevelBar = barToProcess;
                
                if (i_DetailedLog.GetYesNo()) {
                    SCString msg;
                    msg.Format("  VL AUTO-STARTED after VH confirmation on same bar %d", barToProcess);
                    sc.AddMessageToLog(msg, 0);
                }
            }
        }
        
        if (vl_confirmed) {
            float plotPrice = p_15s->VL_TroughLow - (i_15s_SymbolOffset.GetInt() * sc.TickSize);
            sg_15s_VL[p_15s->VL_TroughBar] = plotPrice;
            
            if (i_DetailedLog.GetYesNo()) {
                SCString msg;
                msg.Format("*** VL PLOTTED at Bar %d (Trough=%.2f, ConfirmLvl=%.2f) ***",
                    p_15s->VL_TroughBar, p_15s->VL_TroughLow, p_15s->VL_ConfirmLevel);
                sc.AddMessageToLog(msg, 0);
            }
            
            // Switch traffic light - nu wachten op VH
            p_15s->WhatToPlotNext = s_TimeframeScanner::PLOT_VH;
            
            // Reset VL search - nieuwe zoektocht naar VL begint
            p_15s->VL_Active = false;
            p_15s->VL_TroughLow = 0.0f;
            p_15s->VL_TroughBar = -1;
            p_15s->VL_ConfirmLevel = 0.0f;
            p_15s->VL_ConfirmLevelBar = -1;
            
            // BELANGRIJK: Als huidige bar al een VH trigger is, start VH search direct
            if (!p_15s->VH_Active && close > prev_high) {
                p_15s->VH_Active = true;
                p_15s->VH_PeakHigh = high;
                p_15s->VH_PeakBar = barToProcess;
                p_15s->VH_ConfirmLevel = low;
                p_15s->VH_ConfirmLevelBar = barToProcess;
                
                if (i_DetailedLog.GetYesNo()) {
                    SCString msg;
                    msg.Format("  VH AUTO-STARTED after VL confirmation on same bar %d", barToProcess);
                    sc.AddMessageToLog(msg, 0);
                }
            }
        }

        // ====================================================================
        // STAP 2: UPDATE PARALLELLE SEARCHES
        // ====================================================================
        
        // VH Search (altijd actief)
        if (!p_15s->VH_Active) {
            // Start nieuwe VH search: close > prev_high
            if (close > prev_high) {
                p_15s->VH_Active = true;
                p_15s->VH_PeakHigh = high;
                p_15s->VH_PeakBar = barToProcess;
                p_15s->VH_ConfirmLevel = low;
                p_15s->VH_ConfirmLevelBar = barToProcess;
                
                if (i_DetailedLog.GetYesNo()) {
                    SCString msg;
                    msg.Format("  VH STARTED: Bar %d, Close(%.2f)>PrevHigh(%.2f), Peak=%.2f, ConfirmLvl=%.2f",
                        barToProcess, close, prev_high, high, low);
                    sc.AddMessageToLog(msg, 0);
                }
            }
        } else {
            // Update bestaande VH search
            if (high > p_15s->VH_PeakHigh) {
                p_15s->VH_PeakHigh = high;
                p_15s->VH_PeakBar = barToProcess;
                p_15s->VH_ConfirmLevel = low;
                p_15s->VH_ConfirmLevelBar = barToProcess;
                
                if (i_DetailedLog.GetYesNo()) {
                    SCString msg;
                    msg.Format("  VH UPDATED: Bar %d, NewPeak=%.2f, NewConfirmLvl=%.2f",
                        barToProcess, high, low);
                    sc.AddMessageToLog(msg, 0);
                }
            }
        }
        
        // VL Search (altijd actief)
        if (!p_15s->VL_Active) {
            // Start nieuwe VL search: close < prev_low
            if (close < prev_low) {
                p_15s->VL_Active = true;
                p_15s->VL_TroughLow = low;
                p_15s->VL_TroughBar = barToProcess;
                p_15s->VL_ConfirmLevel = high;
                p_15s->VL_ConfirmLevelBar = barToProcess;
                
                if (i_DetailedLog.GetYesNo()) {
                    SCString msg;
                    msg.Format("  VL STARTED: Bar %d, Close(%.2f)<PrevLow(%.2f), Trough=%.2f, ConfirmLvl=%.2f",
                        barToProcess, close, prev_low, low, high);
                    sc.AddMessageToLog(msg, 0);
                }
            }
        } else {
            // Update bestaande VL search
            if (low < p_15s->VL_TroughLow) {
                p_15s->VL_TroughLow = low;
                p_15s->VL_TroughBar = barToProcess;
                p_15s->VL_ConfirmLevel = high;
                p_15s->VL_ConfirmLevelBar = barToProcess;
                
                if (i_DetailedLog.GetYesNo()) {
                    SCString msg;
                    msg.Format("  VL UPDATED: Bar %d, NewTrough=%.2f, NewConfirmLvl=%.2f",
                        barToProcess, low, high);
                    sc.AddMessageToLog(msg, 0);
                }
            }
        }
    }

    // ========================================================================
    // STAP 3: VISUALISATIE - 1 LIJN (van "next plot" search)
    // ========================================================================
    if (isLastBar) {
        const int LINE_NUMBER = 200001;
        
        sc.DeleteACSChartDrawing(sc.ChartNumber, DRAWING_LINE, LINE_NUMBER);
        
        // Teken alleen de lijn van de search die op het punt staat te plotten
        bool shouldDrawLine = false;
        int lineBeginBar = -1;
        float lineValue = 0.0f;
        
        if (p_15s->WhatToPlotNext == s_TimeframeScanner::PLOT_VH && p_15s->VH_Active) {
            // Toon VH confirm lijn (want we wachten op VH plot)
            shouldDrawLine = true;
            lineBeginBar = p_15s->VH_ConfirmLevelBar;
            lineValue = p_15s->VH_ConfirmLevel;
        } else if (p_15s->WhatToPlotNext == s_TimeframeScanner::PLOT_VL && p_15s->VL_Active) {
            // Toon VL confirm lijn (want we wachten op VL plot)
            shouldDrawLine = true;
            lineBeginBar = p_15s->VL_ConfirmLevelBar;
            lineValue = p_15s->VL_ConfirmLevel;
        }
        
        if (shouldDrawLine) {
            int lineEndBar = p_15s->LastProcessedBar;
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
            tool.Color = i_15s_LineColor.GetColor();
            tool.LineWidth = i_LineWidth.GetInt();
            tool.LineStyle = LINESTYLE_SOLID;
            tool.ExtendLeft = false;
            tool.ExtendRight = false;
            sc.UseTool(tool);
        }
    }
}
