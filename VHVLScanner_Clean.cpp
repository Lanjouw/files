#include "sierrachart.h"

SCDLLName("VH/VL Multi-Timeframe Scanner")

// ============================================================================
// TIMEFRAME SCANNER STRUCTURE
// ============================================================================
// Eén scanner per timeframe - simpel en clean
struct s_TimeframeScanner {
    enum State { LOOKING_FOR_VH, LOOKING_FOR_VL };
    
    State CurrentState = LOOKING_FOR_VH;  // Start met zoeken naar VH
    
    // VH zoektocht data
    float VH_PeakHigh = 0.0f;
    int VH_PeakBar = -1;              // Bar index in de SOURCE timeframe
    float VH_ConfirmLevel = 0.0f;     // Low van de peak bar
    
    // VL zoektocht data
    float VL_TroughLow = 0.0f;
    int VL_TroughBar = -1;            // Bar index in de SOURCE timeframe
    float VL_ConfirmLevel = 0.0f;     // High van de trough bar
    
    int LastProcessedBar = -1;        // Laatste verwerkte bar in SOURCE timeframe
    int LastPlottedBar = -1;          // Laatste geplottte bar (om duplicaten te voorkomen)
};

// ============================================================================
// MAIN STUDY FUNCTION
// ============================================================================
SCSFExport scsf_VHVLScanner_MultiTF(SCStudyInterfaceRef sc)
{
    // ========================================================================
    // INPUTS - Georganiseerd per timeframe
    // ========================================================================
    // 15 second timeframe (native chart)
    SCInputRef i_15s_Enabled = sc.Input[0];
    SCInputRef i_15s_LineColor = sc.Input[1];
    SCInputRef i_15s_SymbolColor = sc.Input[2];
    SCInputRef i_15s_SymbolSize = sc.Input[3];
    SCInputRef i_15s_SymbolOffset = sc.Input[4];
    
    // Global settings
    SCInputRef i_LineWidth = sc.Input[10];
    SCInputRef i_DetailedLog = sc.Input[11];

    // ========================================================================
    // SUBGRAPHS - Per timeframe
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
        sc.MaintainVolumeAtPriceData = 0;

        // 15 second inputs
        i_15s_Enabled.Name = "15sec: Enabled";
        i_15s_Enabled.SetYesNo(true);
        
        i_15s_LineColor.Name = "15sec: Confirm Line Color";
        i_15s_LineColor.SetColor(RGB(0, 0, 0));  // Zwart
        
        i_15s_SymbolColor.Name = "15sec: Symbol Color";
        i_15s_SymbolColor.SetColor(RGB(255, 255, 255));  // Wit
        
        i_15s_SymbolSize.Name = "15sec: Symbol Size";
        i_15s_SymbolSize.SetInt(8);
        
        i_15s_SymbolOffset.Name = "15sec: Symbol Offset (ticks)";
        i_15s_SymbolOffset.SetInt(3);
        
        // Global settings
        i_LineWidth.Name = "Confirm Line Width";
        i_LineWidth.SetInt(2);
        
        i_DetailedLog.Name = "Enable Detailed Logging";
        i_DetailedLog.SetYesNo(false);

        // 15 second subgraphs
        sg_15s_VH.Name = "15s VH";
        sg_15s_VH.DrawStyle = DRAWSTYLE_CIRCLE;  // Hollow circle
        sg_15s_VH.PrimaryColor = RGB(255, 255, 255);
        sg_15s_VH.LineWidth = 8;
        sg_15s_VH.DrawZeros = false;
        
        sg_15s_VL.Name = "15s VL";
        sg_15s_VL.DrawStyle = DRAWSTYLE_CIRCLE;  // Hollow circle
        sg_15s_VL.PrimaryColor = RGB(255, 255, 255);
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

    // Cleanup on removal
    if (sc.LastCallToFunction) {
        if (p_15s != NULL) {
            delete p_15s;
            sc.SetPersistentPointer(1, NULL);
        }
        return;
    }

    // ========================================================================
    // MAIN PROCESSING LOGIC
    // ========================================================================
    if (!i_15s_Enabled.GetYesNo()) return;
    
    int i = sc.Index;
    if (i < 2) return;

    bool isLastBar = (i == sc.ArraySize - 1);
    
    // Alleen verwerken als we een nieuwe gesloten bar hebben
    bool shouldProcess = false;
    int barToProcess = i;
    
    if (!isLastBar) {
        // Historische data
        shouldProcess = true;
        barToProcess = i;
    } else {
        // Realtime: wacht op nieuwe bar
        if (i > p_15s->LastProcessedBar && i > 0) {
            shouldProcess = true;
            barToProcess = i - 1;  // Verwerk de vorige (nu gesloten) bar
        }
    }

    if (!shouldProcess) {
        // Alleen visualisatie updaten (zie verderop)
    } else {
        // ====================================================================
        // NIEUWE GESLOTEN BAR VERWERKEN
        // ====================================================================
        p_15s->LastProcessedBar = barToProcess;
        
        float high = sc.High[barToProcess];
        float low = sc.Low[barToProcess];
        float close = sc.Close[barToProcess];
        
        if (i_DetailedLog.GetYesNo()) {
            SCString msg;
            msg.Format("[15s] Bar %d: Processing - State=%s, H=%.2f L=%.2f C=%.2f",
                barToProcess,
                (p_15s->CurrentState == s_TimeframeScanner::LOOKING_FOR_VH ? "LOOKING_FOR_VH" : "LOOKING_FOR_VL"),
                high, low, close);
            sc.AddMessageToLog(msg, 0);
        }

        // ====================================================================
        // STATE MACHINE
        // ====================================================================
        if (p_15s->CurrentState == s_TimeframeScanner::LOOKING_FOR_VH) {
            // ----------------------------------------------------------------
            // ZOEKEN NAAR VH (HOOGSTE HIGH)
            // ----------------------------------------------------------------
            
            // Update peak als deze hoger is
            if (high > p_15s->VH_PeakHigh) {
                p_15s->VH_PeakHigh = high;
                p_15s->VH_PeakBar = barToProcess;
                p_15s->VH_ConfirmLevel = low;  // Low van deze bar
                
                if (i_DetailedLog.GetYesNo()) {
                    SCString msg;
                    msg.Format("  VH Peak UPDATED: Bar %d, Peak=%.2f, ConfirmLvl=%.2f",
                        barToProcess, high, low);
                    sc.AddMessageToLog(msg, 0);
                }
            }
            
            // Check bevestiging: close < confirm level
            if (p_15s->VH_PeakBar >= 0 && close < p_15s->VH_ConfirmLevel) {
                // VH BEVESTIGD!
                float plotPrice = p_15s->VH_PeakHigh + (i_15s_SymbolOffset.GetInt() * sc.TickSize);
                sg_15s_VH[p_15s->VH_PeakBar] = plotPrice;
                p_15s->LastPlottedBar = p_15s->VH_PeakBar;
                
                if (i_DetailedLog.GetYesNo()) {
                    SCString msg;
                    msg.Format("*** VH CONFIRMED at Bar %d: Peak=%.2f, ConfirmLvl=%.2f, Close=%.2f ***",
                        p_15s->VH_PeakBar, p_15s->VH_PeakHigh, p_15s->VH_ConfirmLevel, close);
                    sc.AddMessageToLog(msg, 0);
                }
                
                // Switch state: zoek nu VL
                p_15s->CurrentState = s_TimeframeScanner::LOOKING_FOR_VL;
                p_15s->VL_TroughLow = low;  // Begin met huidige low
                p_15s->VL_TroughBar = barToProcess;
                p_15s->VL_ConfirmLevel = high;
                
                // Reset VH data
                p_15s->VH_PeakHigh = 0.0f;
                p_15s->VH_PeakBar = -1;
                p_15s->VH_ConfirmLevel = 0.0f;
            }
            
        } else {
            // ----------------------------------------------------------------
            // ZOEKEN NAAR VL (LAAGSTE LOW)
            // ----------------------------------------------------------------
            
            // Update trough als deze lager is
            if (low < p_15s->VL_TroughLow || p_15s->VL_TroughBar < 0) {
                p_15s->VL_TroughLow = low;
                p_15s->VL_TroughBar = barToProcess;
                p_15s->VL_ConfirmLevel = high;  // High van deze bar
                
                if (i_DetailedLog.GetYesNo()) {
                    SCString msg;
                    msg.Format("  VL Trough UPDATED: Bar %d, Trough=%.2f, ConfirmLvl=%.2f",
                        barToProcess, low, high);
                    sc.AddMessageToLog(msg, 0);
                }
            }
            
            // Check bevestiging: close > confirm level
            if (p_15s->VL_TroughBar >= 0 && close > p_15s->VL_ConfirmLevel) {
                // VL BEVESTIGD!
                float plotPrice = p_15s->VL_TroughLow - (i_15s_SymbolOffset.GetInt() * sc.TickSize);
                sg_15s_VL[p_15s->VL_TroughBar] = plotPrice;
                p_15s->LastPlottedBar = p_15s->VL_TroughBar;
                
                if (i_DetailedLog.GetYesNo()) {
                    SCString msg;
                    msg.Format("*** VL CONFIRMED at Bar %d: Trough=%.2f, ConfirmLvl=%.2f, Close=%.2f ***",
                        p_15s->VL_TroughBar, p_15s->VL_TroughLow, p_15s->VL_ConfirmLevel, close);
                    sc.AddMessageToLog(msg, 0);
                }
                
                // Switch state: zoek nu VH
                p_15s->CurrentState = s_TimeframeScanner::LOOKING_FOR_VH;
                p_15s->VH_PeakHigh = high;  // Begin met huidige high
                p_15s->VH_PeakBar = barToProcess;
                p_15s->VH_ConfirmLevel = low;
                
                // Reset VL data
                p_15s->VL_TroughLow = 0.0f;
                p_15s->VL_TroughBar = -1;
                p_15s->VL_ConfirmLevel = 0.0f;
            }
        }
    }

    // ========================================================================
    // VISUALISATIE - Confirm lijn (alleen op laatste bar)
    // ========================================================================
    if (isLastBar) {
        const int LINE_NUMBER = 200001;
        
        // Verwijder oude lijn
        sc.DeleteACSChartDrawing(sc.ChartNumber, DRAWING_LINE, LINE_NUMBER);
        
        // Teken nieuwe lijn als we een actieve zoektocht hebben
        bool shouldDrawLine = false;
        int lineBeginBar = -1;
        float lineValue = 0.0f;
        
        if (p_15s->CurrentState == s_TimeframeScanner::LOOKING_FOR_VH && p_15s->VH_PeakBar >= 0) {
            shouldDrawLine = true;
            lineBeginBar = p_15s->VH_PeakBar;
            lineValue = p_15s->VH_ConfirmLevel;
        } else if (p_15s->CurrentState == s_TimeframeScanner::LOOKING_FOR_VL && p_15s->VL_TroughBar >= 0) {
            shouldDrawLine = true;
            lineBeginBar = p_15s->VL_TroughBar;
            lineValue = p_15s->VL_ConfirmLevel;
        }
        
        if (shouldDrawLine) {
            int lineEndBar = p_15s->LastProcessedBar;
            if (lineEndBar < lineBeginBar + 3) {
                lineEndBar = lineBeginBar + 3;  // Minimaal 3 bars voor zichtbaarheid
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
