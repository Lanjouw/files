#include "sierrachart.h"
#include <string>
#include <sstream>

SCDLLName("VHVLTrendIndicator_Fixed")

enum PointType { PT_NONE, PT_VH, PT_VL };

// Traffic light: bepaalt welk punt als VOLGENDE geplot mag worden
struct s_TrafficLight {
    PointType NextPlotType = PT_VH; // Start: eerste plot moet een VH zijn
    int LastProcessedBar = -1;      // Laatste bar die we volledig verwerkt hebben
};

// Onafhankelijke VH zoektocht (loopt altijd parallel)
struct s_VH_Search {
    bool IsActive = false;
    float PeakHigh = 0.0f;
    int PeakBar = 0;
    float ConfirmLevel = 0.0f;      // Low van de anker candle
    int ConfirmLevelBar = 0;        // Bar index van de anker candle
};

// Onafhankelijke VL zoektocht (loopt altijd parallel)
struct s_VL_Search {
    bool IsActive = false;
    float TroughLow = 0.0f;
    int TroughBar = 0;
    float ConfirmLevel = 0.0f;      // High van de anker candle
    int ConfirmLevelBar = 0;        // Bar index van de anker candle
};

// Constanten voor drawing management
const int LINE_NUMBER_VH = 100001;
const int LINE_NUMBER_VL = 100002;
const int TEXT_NUMBER_DEBUG = 100003;

SCSFExport scsf_VHVLTrendIndicator_Fixed(SCStudyInterfaceRef sc)
{
    SCInputRef i_ArrowOffset = sc.Input[0];
    SCInputRef i_LineColor = sc.Input[1];
    SCInputRef i_LineWidth = sc.Input[2];
    SCInputRef i_LineStyle = sc.Input[3];
    SCInputRef i_DebugMode = sc.Input[4];
    SCInputRef i_DetailedLog = sc.Input[5];

    SCSubgraphRef s_VH = sc.Subgraph[0];
    SCSubgraphRef s_VL = sc.Subgraph[1];

    if (sc.SetDefaults)
    {
        sc.GraphName = "VH/VL Trend Scanner (Fixed)";
        sc.AutoLoop = 1;
        sc.GraphRegion = 0;
        sc.UpdateAlways = 1;

        i_ArrowOffset.Name = "Arrow Offset in Ticks";
        i_ArrowOffset.SetInt(3);
        
        i_LineColor.Name = "Anchor Line Color";
        i_LineColor.SetColor(RGB(0, 0, 0));
        
        i_LineWidth.Name = "Anchor Line Width";
        i_LineWidth.SetInt(1);
        
        i_LineStyle.Name = "Anchor Line Style";
        i_LineStyle.SetCustomInputStrings("Solid;Dash;Dot;DashDot");
        i_LineStyle.SetInt(0);
        
        i_DebugMode.Name = "Enable Debug Mode";
        i_DebugMode.SetYesNo(false);
        
        i_DetailedLog.Name = "Enable Detailed Logging";
        i_DetailedLog.SetYesNo(false);

        s_VH.Name = "VH Point";
        s_VH.DrawStyle = DRAWSTYLE_ARROWUP;
        s_VH.PrimaryColor = RGB(0, 255, 0);
        s_VH.LineWidth = 3;
        s_VH.DrawZeros = false;
        
        s_VL.Name = "VL Point";
        s_VL.DrawStyle = DRAWSTYLE_ARROWDOWN;
        s_VL.PrimaryColor = RGB(255, 0, 0);
        s_VL.LineWidth = 3;
        s_VL.DrawZeros = false;
        
        return;
    }

    // Persistent data ophalen/initialiseren
    s_TrafficLight* p_TrafficLight = (s_TrafficLight*)sc.GetPersistentPointer(1);
    s_VH_Search* p_VH_Search = (s_VH_Search*)sc.GetPersistentPointer(2);
    s_VL_Search* p_VL_Search = (s_VL_Search*)sc.GetPersistentPointer(3);

    if (p_TrafficLight == NULL) {
        p_TrafficLight = new s_TrafficLight();
        sc.SetPersistentPointer(1, p_TrafficLight);
    }
    if (p_VH_Search == NULL) {
        p_VH_Search = new s_VH_Search();
        sc.SetPersistentPointer(2, p_VH_Search);
    }
    if (p_VL_Search == NULL) {
        p_VL_Search = new s_VL_Search();
        sc.SetPersistentPointer(3, p_VL_Search);
    }

    // Cleanup bij study removal
    if (sc.LastCallToFunction) {
        if (p_TrafficLight != NULL) {
            delete p_TrafficLight;
            sc.SetPersistentPointer(1, NULL);
        }
        if (p_VH_Search != NULL) {
            delete p_VH_Search;
            sc.SetPersistentPointer(2, NULL);
        }
        if (p_VL_Search != NULL) {
            delete p_VL_Search;
            sc.SetPersistentPointer(3, NULL);
        }
        return;
    }

    int i = sc.Index;
    if (i < 2) return;

    // ========================================================================
    // KRITIEK: Werk alleen met GESLOTEN bars!
    // ========================================================================
    // In realtime is bar i de "developing bar" - die is NOG NIET gesloten.
    // We mogen alleen bevestigen en updaten op basis van GESLOTEN bars.
    
    bool isLastBar = (i == sc.ArraySize - 1);
    
    // Bepaal of we een nieuwe bar moeten verwerken
    // We verwerken alleen als:
    // 1. Het NIET de laatste bar is (historische data), OF
    // 2. Het WEL de laatste bar is MAAR deze is nieuw (niet eerder verwerkt)
    bool shouldProcessNewBar = false;
    int barToProcess = i;
    
    if (!isLastBar) {
        // Historische bar - altijd verwerken
        shouldProcessNewBar = true;
        barToProcess = i;
    } else {
        // Laatste bar - check of het een NIEUWE gesloten bar is
        // Dit gebeurt wanneer een nieuwe bar is begonnen (vorige bar is nu gesloten)
        if (i > p_TrafficLight->LastProcessedBar) {
            // Nieuwe bar gedetecteerd - verwerk de VORIGE (nu gesloten) bar
            if (i > 0 && p_TrafficLight->LastProcessedBar < i - 1) {
                shouldProcessNewBar = true;
                barToProcess = i - 1;
            }
        }
    }

    // Als we geen nieuwe bar hebben om te verwerken, skip de logica (alleen visualisatie updaten)
    if (!shouldProcessNewBar) {
        // Ga direct naar visualisatie
        if (i_DetailedLog.GetYesNo()) {
            SCString logMsg;
            logMsg.Format("Bar %d: NO PROCESSING (developing bar, LastProcessedBar=%d)", i, p_TrafficLight->LastProcessedBar);
            sc.AddMessageToLog(logMsg, 0);
        }
    } else {
        // We hebben een nieuwe gesloten bar om te verwerken
        p_TrafficLight->LastProcessedBar = barToProcess;
        
        float currentClose = sc.Close[barToProcess];

        if (i_DetailedLog.GetYesNo()) {
            SCString logMsg;
            logMsg.Format("Bar %d: PROCESSING Bar %d (CLOSED) - Close=%.2f", i, barToProcess, currentClose);
            sc.AddMessageToLog(logMsg, 0);
        }

        // ====================================================================
        // STAP 1: BEVESTIGING CHECKEN (alleen op nieuwe GESLOTEN bars)
        // ====================================================================
        bool vh_isConfirmed = (p_VH_Search->IsActive && 
                              currentClose < p_VH_Search->ConfirmLevel &&
                              p_TrafficLight->NextPlotType == PT_VH);
        
        bool vl_isConfirmed = (p_VL_Search->IsActive && 
                              currentClose > p_VL_Search->ConfirmLevel &&
                              p_TrafficLight->NextPlotType == PT_VL);

        if (i_DetailedLog.GetYesNo()) {
            SCString logMsg;
            logMsg.Format("  VH Check: Active=%d, Close(%.2f) < ConfirmLvl(%.2f)=%d, NextPlot=%s => Confirmed=%d",
                p_VH_Search->IsActive, currentClose, p_VH_Search->ConfirmLevel,
                (currentClose < p_VH_Search->ConfirmLevel ? 1 : 0),
                (p_TrafficLight->NextPlotType == PT_VH ? "VH" : "VL"),
                vh_isConfirmed);
            sc.AddMessageToLog(logMsg, 0);
            
            logMsg.Format("  VL Check: Active=%d, Close(%.2f) > ConfirmLvl(%.2f)=%d, NextPlot=%s => Confirmed=%d",
                p_VL_Search->IsActive, currentClose, p_VL_Search->ConfirmLevel,
                (currentClose > p_VL_Search->ConfirmLevel ? 1 : 0),
                (p_TrafficLight->NextPlotType == PT_VH ? "VH" : "VL"),
                vl_isConfirmed);
            sc.AddMessageToLog(logMsg, 0);
        }

        // Bevestigingen verwerken
        if (vh_isConfirmed) {
            // Plot VH pijl
            float arrowPrice = p_VH_Search->PeakHigh + (i_ArrowOffset.GetInt() * sc.TickSize);
            s_VH[p_VH_Search->PeakBar] = arrowPrice;
            
            if (i_DetailedLog.GetYesNo()) {
                SCString logMsg;
                logMsg.Format("*** VH CONFIRMED & PLOTTED at Bar %d (Peak=%.2f, ConfirmLvl=%.2f) ***",
                    p_VH_Search->PeakBar, p_VH_Search->PeakHigh, p_VH_Search->ConfirmLevel);
                sc.AddMessageToLog(logMsg, 0);
            }
            
            // Wissel traffic light
            p_TrafficLight->NextPlotType = PT_VL;
            
            // Reset VH search (nieuwe zoektocht kan beginnen)
            *p_VH_Search = s_VH_Search();
        }
        
        if (vl_isConfirmed) {
            // Plot VL pijl
            float arrowPrice = p_VL_Search->TroughLow - (i_ArrowOffset.GetInt() * sc.TickSize);
            s_VL[p_VL_Search->TroughBar] = arrowPrice;
            
            if (i_DetailedLog.GetYesNo()) {
                SCString logMsg;
                logMsg.Format("*** VL CONFIRMED & PLOTTED at Bar %d (Trough=%.2f, ConfirmLvl=%.2f) ***",
                    p_VL_Search->TroughBar, p_VL_Search->TroughLow, p_VL_Search->ConfirmLevel);
                sc.AddMessageToLog(logMsg, 0);
            }
            
            // Wissel traffic light
            p_TrafficLight->NextPlotType = PT_VH;
            
            // Reset VL search (nieuwe zoektocht kan beginnen)
            *p_VL_Search = s_VL_Search();
        }

        // ====================================================================
        // STAP 2: PARALLELLE ZOEKTOCHTEN UPDATEN (alleen met GESLOTEN bars)
        // ====================================================================
        float high = sc.High[barToProcess];
        float low = sc.Low[barToProcess];
        float close = sc.Close[barToProcess];
        float prev_high = sc.High[barToProcess - 1];
        float prev_low = sc.Low[barToProcess - 1];

        // --- VH ZOEKTOCHT (altijd actief) ---
        if (!p_VH_Search->IsActive) {
            // Start nieuwe VH zoektocht: close > prev_high
            if (close > prev_high) {
                p_VH_Search->IsActive = true;
                p_VH_Search->PeakHigh = high;
                p_VH_Search->PeakBar = barToProcess;
                p_VH_Search->ConfirmLevel = low;           // Low van anker candle
                p_VH_Search->ConfirmLevelBar = barToProcess;  // Dit is de anker candle
                
                if (i_DetailedLog.GetYesNo()) {
                    SCString logMsg;
                    logMsg.Format("  VH Search STARTED: Bar %d, Close(%.2f) > PrevHigh(%.2f), Peak=%.2f, ConfirmLvl=%.2f",
                        barToProcess, close, prev_high, high, low);
                    sc.AddMessageToLog(logMsg, 0);
                }
            }
        } else {
            // Update bestaande VH zoektocht: hogere high gevonden
            if (high > p_VH_Search->PeakHigh) {
                float oldPeak = p_VH_Search->PeakHigh;
                float oldConfirm = p_VH_Search->ConfirmLevel;
                
                p_VH_Search->PeakHigh = high;
                p_VH_Search->PeakBar = barToProcess;
                p_VH_Search->ConfirmLevel = low;           // Low van nieuwe anker candle
                p_VH_Search->ConfirmLevelBar = barToProcess;  // Nieuwe anker candle
                
                if (i_DetailedLog.GetYesNo()) {
                    SCString logMsg;
                    logMsg.Format("  VH Search UPDATED: Bar %d, Peak %.2f->%.2f, ConfirmLvl %.2f->%.2f",
                        barToProcess, oldPeak, high, oldConfirm, low);
                    sc.AddMessageToLog(logMsg, 0);
                }
            }
        }

        // --- VL ZOEKTOCHT (altijd actief) ---
        if (!p_VL_Search->IsActive) {
            // Start nieuwe VL zoektocht: close < prev_low
            if (close < prev_low) {
                p_VL_Search->IsActive = true;
                p_VL_Search->TroughLow = low;
                p_VL_Search->TroughBar = barToProcess;
                p_VL_Search->ConfirmLevel = high;          // High van anker candle
                p_VL_Search->ConfirmLevelBar = barToProcess;  // Dit is de anker candle
                
                if (i_DetailedLog.GetYesNo()) {
                    SCString logMsg;
                    logMsg.Format("  VL Search STARTED: Bar %d, Close(%.2f) < PrevLow(%.2f), Trough=%.2f, ConfirmLvl=%.2f",
                        barToProcess, close, prev_low, low, high);
                    sc.AddMessageToLog(logMsg, 0);
                }
            }
        } else {
            // Update bestaande VL zoektocht: lagere low gevonden
            if (low < p_VL_Search->TroughLow) {
                float oldTrough = p_VL_Search->TroughLow;
                float oldConfirm = p_VL_Search->ConfirmLevel;
                
                p_VL_Search->TroughLow = low;
                p_VL_Search->TroughBar = barToProcess;
                p_VL_Search->ConfirmLevel = high;          // High van nieuwe anker candle
                p_VL_Search->ConfirmLevelBar = barToProcess;  // Nieuwe anker candle
                
                if (i_DetailedLog.GetYesNo()) {
                    SCString logMsg;
                    logMsg.Format("  VL Search UPDATED: Bar %d, Trough %.2f->%.2f, ConfirmLvl %.2f->%.2f",
                        barToProcess, oldTrough, low, oldConfirm, high);
                    sc.AddMessageToLog(logMsg, 0);
                }
            }
        }
    } // Einde van shouldProcessNewBar

    // ========================================================================
    // STAP 3: VISUALISATIE (alleen op laatste bar)
    // ========================================================================
    if (i == sc.ArraySize - 1) {
        // Verwijder oude drawings
        sc.DeleteACSChartDrawing(sc.ChartNumber, DRAWING_LINE, LINE_NUMBER_VH);
        sc.DeleteACSChartDrawing(sc.ChartNumber, DRAWING_LINE, LINE_NUMBER_VL);
        if (i_DebugMode.GetYesNo()) {
            sc.DeleteACSChartDrawing(sc.ChartNumber, DRAWING_TEXT, TEXT_NUMBER_DEBUG);
        }

        // BELANGRIJK: Lijn eindpunt moet LAATSTE GESLOTEN BAR zijn, niet de developing bar
        // Gebruik de laatst verwerkte bar als eindpunt
        // EN: Zorg dat de lijn minimaal 3 bars verder gaat dan de ankercandle voor zichtbaarheid
        int lineEndBar = p_TrafficLight->LastProcessedBar;

        // --- VH LIJN ---
        if (p_VH_Search->IsActive) {
            // Bereken minimaal eindpunt: ankercandle + 3 bars
            int minEndBar = p_VH_Search->ConfirmLevelBar + 3;
            int vh_EndBar = (lineEndBar > minEndBar) ? lineEndBar : minEndBar;
            
            s_UseTool VH_Line;
            VH_Line.DrawingType = DRAWING_LINE;
            VH_Line.LineNumber = LINE_NUMBER_VH;
            VH_Line.AddMethod = UTAM_ADD_OR_ADJUST;
            VH_Line.BeginIndex = p_VH_Search->ConfirmLevelBar;
            VH_Line.BeginValue = p_VH_Search->ConfirmLevel;
            VH_Line.EndIndex = vh_EndBar;  // Minimaal 3 bars verder
            VH_Line.EndValue = p_VH_Search->ConfirmLevel;
            VH_Line.Color = i_LineColor.GetColor();
            VH_Line.LineWidth = i_LineWidth.GetInt();
            VH_Line.LineStyle = (SubgraphLineStyles)i_LineStyle.GetInt();
            VH_Line.ExtendLeft = false;
            VH_Line.ExtendRight = false;
            sc.UseTool(VH_Line);
        }

        // --- VL LIJN ---
        if (p_VL_Search->IsActive) {
            // Bereken minimaal eindpunt: ankercandle + 3 bars
            int minEndBar = p_VL_Search->ConfirmLevelBar + 3;
            int vl_EndBar = (lineEndBar > minEndBar) ? lineEndBar : minEndBar;
            
            s_UseTool VL_Line;
            VL_Line.DrawingType = DRAWING_LINE;
            VL_Line.LineNumber = LINE_NUMBER_VL;
            VL_Line.AddMethod = UTAM_ADD_OR_ADJUST;
            VL_Line.BeginIndex = p_VL_Search->ConfirmLevelBar;
            VL_Line.BeginValue = p_VL_Search->ConfirmLevel;
            VL_Line.EndIndex = vl_EndBar;  // Minimaal 3 bars verder
            VL_Line.EndValue = p_VL_Search->ConfirmLevel;
            VL_Line.Color = i_LineColor.GetColor();
            VL_Line.LineWidth = i_LineWidth.GetInt();
            VL_Line.LineStyle = (SubgraphLineStyles)i_LineStyle.GetInt();
            VL_Line.ExtendLeft = false;
            VL_Line.ExtendRight = false;
            sc.UseTool(VL_Line);
        }

        // --- DEBUG TEKST ---
        if (i_DebugMode.GetYesNo()) {
            std::stringstream ss;
            ss << "=== VH/VL TREND SCANNER DEBUG ===\n";
            ss << "Next Plot: " << (p_TrafficLight->NextPlotType == PT_VH ? "VH" : "VL") << "\n\n";
            
            ss << "VH Search:\n";
            ss << "  Active: " << (p_VH_Search->IsActive ? "YES" : "NO") << "\n";
            if (p_VH_Search->IsActive) {
                ss << "  Peak High: " << sc.FormatGraphValue(p_VH_Search->PeakHigh, sc.BaseGraphValueFormat);
                ss << " @ Bar " << p_VH_Search->PeakBar << "\n";
                ss << "  Confirm Level: " << sc.FormatGraphValue(p_VH_Search->ConfirmLevel, sc.BaseGraphValueFormat);
                ss << " (Anchor Bar " << p_VH_Search->ConfirmLevelBar << ")\n";
                ss << "  Bars Active: " << (i - p_VH_Search->ConfirmLevelBar) << "\n";
            }
            
            ss << "\nVL Search:\n";
            ss << "  Active: " << (p_VL_Search->IsActive ? "YES" : "NO") << "\n";
            if (p_VL_Search->IsActive) {
                ss << "  Trough Low: " << sc.FormatGraphValue(p_VL_Search->TroughLow, sc.BaseGraphValueFormat);
                ss << " @ Bar " << p_VL_Search->TroughBar << "\n";
                ss << "  Confirm Level: " << sc.FormatGraphValue(p_VL_Search->ConfirmLevel, sc.BaseGraphValueFormat);
                ss << " (Anchor Bar " << p_VL_Search->ConfirmLevelBar << ")\n";
                ss << "  Bars Active: " << (i - p_VL_Search->ConfirmLevelBar) << "\n";
            }
            
            ss << "\nCurrent Bar Index: " << i;
            ss << "\nLast Processed Bar: " << p_TrafficLight->LastProcessedBar;
            if (p_TrafficLight->LastProcessedBar >= 0) {
                ss << "\nLast Processed Close: " << sc.FormatGraphValue(sc.Close[p_TrafficLight->LastProcessedBar], sc.BaseGraphValueFormat);
            }
            
            s_UseTool TextTool;
            TextTool.DrawingType = DRAWING_TEXT;
            TextTool.LineNumber = TEXT_NUMBER_DEBUG;
            TextTool.AddMethod = UTAM_ADD_OR_ADJUST;
            TextTool.Text = ss.str().c_str();
            TextTool.BeginIndex = sc.IndexOfFirstVisibleBar;
            TextTool.BeginValue = sc.YPixelCoordinateToGraphValue(10);
            TextTool.Color = RGB(255, 255, 255);
            TextTool.FontSize = 10;
            TextTool.TextAlignment = DT_LEFT | DT_TOP;
            sc.UseTool(TextTool);
        }
    }
}
