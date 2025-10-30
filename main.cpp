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

    // Detect full recalculation: als we terug gaan in tijd, reset state
    if (i < p_15s->LastProcessedBar - 1) {
        // Recalculation detected - reset alle state
        *p_15s = s_TimeframeScanner();
        if (i_DetailedLog.GetYesNo()) {
            SCString msg;
            msg.Format("RECALCULATION DETECTED at bar %d (LastProcessed was %d) - FULL RESET", i, p_15s->LastProcessedBar);
            sc.AddMessageToLog(msg, 0);
        }
    }

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
    // STAP 3: VISUALISATIE - 1 LIJN (van "next plot" search)
    // ========================================================================
    if (i == sc.ArraySize - 1) {
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
