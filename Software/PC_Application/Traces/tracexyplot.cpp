#include "tracexyplot.h"

#include "trace.h"
#include "CustomWidgets/informationbox.h"
#include "Marker/marker.h"
#include "xyplotaxisdialog.h"
#include "Util/util.h"
#include "unit.h"
#include "preferences.h"

#include <QGridLayout>
#include <cmath>
#include <QFrame>
#include <QPainter>
#include <QDebug>
#include <QFileDialog>

using namespace std;

TraceXYPlot::TraceXYPlot(TraceModel &model, QWidget *parent)
    : TracePlot(model, parent)
{
    YAxis[0].log = false;
    YAxis[0].type = YAxisType::Disabled;
    YAxis[0].rangeDiv = 1;
    YAxis[0].rangeMax = 10;
    YAxis[0].rangeMin = 0;
    YAxis[0].autorange = false;
    YAxis[1].log = false;
    YAxis[1].type = YAxisType::Disabled;
    YAxis[1].rangeDiv = 1;
    YAxis[1].rangeMax = 10;
    YAxis[1].rangeMin = 0;
    YAxis[1].autorange = false;
    XAxis.type = XAxisType::Frequency;
    XAxis.log = false;
    XAxis.rangeDiv = 1;
    XAxis.rangeMax = 10;
    XAxis.rangeMin = 0;
    XAxis.mode = XAxisMode::UseSpan;

    // Setup default axis
    setYAxis(0, YAxisType::Magnitude, false, false, -120, 20, 10);
    setYAxis(1, YAxisType::Phase, false, false, -180, 180, 30);
    // enable autoscaling and set for full span (no information about actual span available yet)
    updateSpan(0, 6000000000);
    setXAxis(XAxisType::Frequency, XAxisMode::UseSpan, false, 0, 6000000000, 600000000);
    initializeTraceInfo();
}

void TraceXYPlot::setYAxis(int axis, TraceXYPlot::YAxisType type, bool log, bool autorange, double min, double max, double div)
{
    if(YAxis[axis].type != type) {
        // remove traces that are active but not supported with the new axis type
        bool erased = false;
        do {
            erased = false;
            for(auto t : tracesAxis[axis]) {
                if(!supported(t, type)) {
                    enableTraceAxis(t, axis, false);
                    erased = true;
                    break;
                }
            }
        } while(erased);
        YAxis[axis].type = type;
    }
    YAxis[axis].log = log;
    YAxis[axis].autorange = autorange;
    YAxis[axis].rangeMin = min;
    YAxis[axis].rangeMax = max;
    YAxis[axis].rangeDiv = div;
    traceRemovalPending = true;
    updateAxisTicks();
    updateContextMenu();
    replot();
}

void TraceXYPlot::setXAxis(XAxisType type, XAxisMode mode, bool log, double min, double max, double div)
{
    XAxis.type = type;
    XAxis.mode = mode;
    XAxis.log = log;
    XAxis.rangeMin = min;
    XAxis.rangeMax = max;
    XAxis.rangeDiv = div;
    traceRemovalPending = true;
    updateAxisTicks();
    updateContextMenu();
    replot();
}

void TraceXYPlot::enableTrace(Trace *t, bool enabled)
{
    for(int axis = 0;axis < 2;axis++) {
        enableTraceAxis(t, axis, enabled && supported(t, YAxis[axis].type));
    }
}

void TraceXYPlot::updateSpan(double min, double max)
{
    TracePlot::updateSpan(min, max);
    updateAxisTicks();
}

void TraceXYPlot::replot()
{
    if(XAxis.mode != XAxisMode::Manual || YAxis[0].autorange || YAxis[1].autorange) {
        updateAxisTicks();
    }
    TracePlot::replot();
}

nlohmann::json TraceXYPlot::toJSON()
{
    nlohmann::json j;
    nlohmann::json jX;
    jX["type"] = AxisTypeToName(XAxis.type).toStdString();
    jX["mode"] = AxisModeToName(XAxis.mode).toStdString();
    jX["log"] = XAxis.log;
    jX["min"] = XAxis.rangeMin;
    jX["max"] = XAxis.rangeMax;
    jX["div"] = XAxis.rangeDiv;
    j["XAxis"] = jX;
    for(unsigned int i=0;i<2;i++) {
        nlohmann::json jY;
        jY["type"] = AxisTypeToName(YAxis[i].type).toStdString();
        jY["log"] = YAxis[i].log;
        jY["autorange"] = YAxis[i].autorange;
        jY["min"] = YAxis[i].rangeMin;
        jY["max"] = YAxis[i].rangeMax;
        jY["div"] = YAxis[i].rangeDiv;
        nlohmann::json jtraces;
        for(auto t : tracesAxis[i]) {
            jtraces.push_back(t->toHash());
        }
        jY["traces"] = jtraces;

        if(i==0) {
            j["YPrimary"] = jY;
        } else {
            j["YSecondary"] = jY;
        }
    }
    return j;
}

void TraceXYPlot::fromJSON(nlohmann::json j)
{
    auto jX = j["XAxis"];
    // old format used enum value for type and mode, new format uses string encoding (more robust when additional enum values are added).
    // Check which format is used and parse accordingly
    XAxisType xtype;
    if(jX["type"].type() == nlohmann::json::value_t::string) {
        xtype = XAxisTypeFromName(QString::fromStdString(jX["type"]));
    } else {
        xtype = jX.value("type", XAxisType::Frequency);
    }
    XAxisMode xmode;
    if(jX["mode"].type() == nlohmann::json::value_t::string) {
        xmode = AxisModeFromName(QString::fromStdString(jX["mode"]));
    } else {
        xmode = jX.value("mode", XAxisMode::UseSpan);
    }
//    auto xlog = jX.value("log", false);
    auto xmin = jX.value("min", 0.0);
    auto xmax = jX.value("max", 6000000000.0);
    auto xdiv = jX.value("div", 600000000.0);
    auto xlog = jX.value("log", false);
    setXAxis(xtype, xmode, xlog, xmin, xmax, xdiv);
    nlohmann::json jY[2] = {j["YPrimary"], j["YSecondary"]};
    for(unsigned int i=0;i<2;i++) {
        YAxisType ytype;
        if(jY[i]["type"].type() == nlohmann::json::value_t::string) {
            ytype = YAxisTypeFromName(QString::fromStdString(jY[i]["type"]));
        } else {
            ytype = jY[i].value("type", YAxisType::Disabled);
        }
        auto yauto = jY[i].value("autorange", true);
        auto ylog = jY[i].value("log", false);
        auto ymin = jY[i].value("min", -120.0);
        auto ymax = jY[i].value("max", 20.0);
        auto ydiv = jY[i].value("div", 10.0);
        setYAxis(i, ytype, ylog, yauto, ymin, ymax, ydiv);
        for(unsigned int hash : jY[i]["traces"]) {
            // attempt to find the traces with this hash
            bool found = false;
            for(auto t : model.getTraces()) {
                if(t->toHash() == hash) {
                    enableTraceAxis(t, i, true);
                    found = true;
                    break;
                }
            }
            if(!found) {
                qWarning() << "Unable to find trace with hash" << hash;
            }
        }
    }
}

bool TraceXYPlot::isTDRtype(TraceXYPlot::YAxisType type)
{
    switch(type) {
    case YAxisType::ImpulseReal:
    case YAxisType::ImpulseMag:
    case YAxisType::Step:
    case YAxisType::Impedance:
        return true;
    default:
        return false;
    }
}

void TraceXYPlot::axisSetupDialog()
{
    auto setup = new XYplotAxisDialog(this);
    setup->show();
}

bool TraceXYPlot::configureForTrace(Trace *t)
{
    switch(t->outputType()) {
    case Trace::DataType::Frequency:
        setXAxis(XAxisType::Frequency, XAxisMode::FitTraces, false, 0, 1, 0.1);
        setYAxis(0, YAxisType::Magnitude, false, true, 0, 1, 1.0);
        setYAxis(1, YAxisType::Phase, false, true, 0, 1, 1.0);
        break;
    case Trace::DataType::Time:
        setXAxis(XAxisType::Time, XAxisMode::FitTraces, false, 0, 1, 0.1);
        setYAxis(0, YAxisType::ImpulseMag, false, true, 0, 1, 1.0);
        setYAxis(1, YAxisType::Disabled, false, true, 0, 1, 1.0);
        break;
    case Trace::DataType::Power:
        setXAxis(XAxisType::Power, XAxisMode::FitTraces, false, 0, 1, 0.1);
        setYAxis(0, YAxisType::Magnitude, false, true, 0, 1, 1.0);
        setYAxis(1, YAxisType::Phase, false, true, 0, 1, 1.0);
        break;
    case Trace::DataType::Invalid:
        // unable to add
        return false;
    }
    traceRemovalPending = true;
    return true;
}

void TraceXYPlot::updateContextMenu()
{
    contextmenu->clear();
    auto setup = new QAction("Axis setup...", contextmenu);
    connect(setup, &QAction::triggered, this, &TraceXYPlot::axisSetupDialog);
    contextmenu->addAction(setup);

    contextmenu->addSeparator();
    auto image = new QAction("Save image...", contextmenu);
    contextmenu->addAction(image);
    connect(image, &QAction::triggered, [=]() {
        auto filename = QFileDialog::getSaveFileName(nullptr, "Save plot image", "", "PNG image files (*.png)", nullptr, QFileDialog::DontUseNativeDialog);
        if(filename.isEmpty()) {
            // aborted selection
            return;
        }
        if(filename.endsWith(".png")) {
            filename.chop(4);
        }
        filename += ".png";
        grab().save(filename);
    });

    auto createMarker = contextmenu->addAction("Add marker here");
    bool activeTraces = false;
    for(auto t : traces) {
        if(t.second) {
            activeTraces = true;
            break;
        }
    }
    if(!activeTraces) {
        createMarker->setEnabled(false);
    }
    connect(createMarker, &QAction::triggered, [=](){
        createMarkerAtPosition(contextmenuClickpoint);
    });

    for(int axis = 0;axis < 2;axis++) {
        if(YAxis[axis].type == YAxisType::Disabled) {
            continue;
        }
        if(axis == 0) {
            contextmenu->addSection("Primary Traces");
        } else {
            contextmenu->addSection("Secondary Traces");
        }
        for(auto t : traces) {
            // Skip traces that are not applicable for the selected axis type
            if(!supported(t.first, YAxis[axis].type)) {
                continue;
            }

            auto action = new QAction(t.first->name(), contextmenu);
            action->setCheckable(true);
            if(tracesAxis[axis].find(t.first) != tracesAxis[axis].end()) {
                action->setChecked(true);
            }
            connect(action, &QAction::toggled, [=](bool active) {
                enableTraceAxis(t.first, axis, active);
            });
            contextmenu->addAction(action);
        }
    }

    contextmenu->addSeparator();
    auto close = new QAction("Close", contextmenu);
    contextmenu->addAction(close);
    connect(close, &QAction::triggered, [=]() {
        markedForDeletion = true;
    });
}

bool TraceXYPlot::dropSupported(Trace *t)
{
    if(domainMatch(t) && !supported(t)) {
        // correct domain configured but Y axis do not match, prevent drop
        return false;
    }
    // either directly compatible or domain change required
    return true;
}

bool TraceXYPlot::supported(Trace *t)
{
    // potentially possible to add every kind of trace (depends on axis)
    if(supported(t, YAxis[0].type) || supported(t, YAxis[1].type)) {
        return true;
    } else {
        // no axis
        return false;
    }
}

void TraceXYPlot::draw(QPainter &p)
{
    auto pref = Preferences::getInstance();

    constexpr int yAxisSpace = 55;
    constexpr int yAxisDisabledSpace = 10;
    constexpr int xAxisSpace = 30;
    auto w = p.window();
    auto pen = QPen(pref.Graphs.Color.axis, 0);
    pen.setCosmetic(true);
    p.setPen(pen);
    plotAreaLeft = YAxis[0].type == YAxisType::Disabled ? yAxisDisabledSpace : yAxisSpace;
    plotAreaWidth = w.width();
    plotAreaTop = 10;
    plotAreaBottom = w.height() - xAxisSpace;
    if(YAxis[0].type != YAxisType::Disabled) {
        plotAreaWidth -= yAxisSpace;
    } else {
        plotAreaWidth -= yAxisDisabledSpace;
    }
    if(YAxis[1].type != YAxisType::Disabled) {
        plotAreaWidth -= yAxisSpace;
    } else {
        plotAreaWidth -= yAxisDisabledSpace;
    }

    auto plotRect = QRect(plotAreaLeft, plotAreaTop, plotAreaWidth + 1, plotAreaBottom-plotAreaTop);
    p.drawRect(plotRect);

    // draw axis types
    auto font = p.font();
    font.setPixelSize(AxisLabelSize);
    p.setFont(font);
    p.drawText(QRect(0, w.height()-AxisLabelSize*1.5, w.width(), AxisLabelSize*1.5), Qt::AlignHCenter, AxisTypeToName(XAxis.type));
    for(int i=0;i<2;i++) {
        if(YAxis[i].type == YAxisType::Disabled) {
            continue;
        }
        QString labelY = AxisTypeToName(YAxis[i].type);
        p.setPen(QPen(pref.Graphs.Color.axis, 1));
        auto xStart = i == 0 ? 0 : w.width() - AxisLabelSize * 1.5;
        p.save();
        p.translate(xStart, w.height()-xAxisSpace);
        p.rotate(-90);
        p.drawText(QRect(0, 0, w.height()-xAxisSpace, AxisLabelSize*1.5), Qt::AlignHCenter, labelY);
        p.restore();
        // draw ticks
        if(YAxis[i].type != YAxisType::Disabled && YAxis[i].ticks.size() > 0) {
            // this only works for evenly distributed ticks:
            auto max = qMax(abs(YAxis[i].ticks.front()), abs(YAxis[i].ticks.back()));
            double step;
            if(YAxis[i].ticks.size() >= 2) {
                step = abs(YAxis[i].ticks[0] - YAxis[i].ticks[1]);
            } else {
                // only one tick, set arbitrary number of digits
                step = max / 1000;
            }
            int significantDigits = floor(log10(max)) - floor(log10(step)) + 1;

            for(unsigned int j = 0; j < YAxis[i].ticks.size(); j++) {
                auto yCoord = Util::Scale<double>(YAxis[i].ticks[j], YAxis[i].rangeMax, YAxis[i].rangeMin, plotAreaTop, w.height() - xAxisSpace);
                p.setPen(QPen(pref.Graphs.Color.axis, 1));
                // draw tickmark on axis
                auto tickStart = i == 0 ? plotAreaLeft : plotAreaLeft + plotAreaWidth;
                auto tickLen = i == 0 ? -2 : 2;
                p.drawLine(tickStart, yCoord, tickStart + tickLen, yCoord);
                QString unit = "";
                if(pref.Graphs.showUnits) {
                    unit = AxisUnit(YAxis[i].type);
                }
                auto tickValue = Unit::ToString(YAxis[i].ticks[j], unit, "fpnum kMG", significantDigits);
                if(i == 0) {
                    p.drawText(QRectF(0, yCoord - AxisLabelSize/2 - 2, tickStart + 2 * tickLen, AxisLabelSize), Qt::AlignRight, tickValue);
                } else {
                    p.drawText(QRectF(tickStart + 2 * tickLen + 2, yCoord - AxisLabelSize/2 - 2, yAxisSpace, AxisLabelSize), Qt::AlignLeft, tickValue);
                }

                // tick lines
                if(yCoord == plotAreaTop || yCoord == w.height() - xAxisSpace) {
                    // skip tick lines right on the plot borders
                    continue;
                }
                if(i == 0) {
                    // only draw tick lines for primary axis
                    if (pref.Graphs.Color.Ticks.Background.enabled) {
                        if (j%2)
                        {
                            int yCoordTop = Util::Scale<double>(YAxis[i].ticks[j], YAxis[i].rangeMax, YAxis[i].rangeMin, plotAreaTop, w.height() - xAxisSpace);
                            int yCoordBot = Util::Scale<double>(YAxis[i].ticks[j-1], YAxis[i].rangeMax, YAxis[i].rangeMin, plotAreaTop, w.height() - xAxisSpace);
                            if(yCoordTop > yCoordBot) {
                                auto buf = yCoordBot;
                                yCoordBot = yCoordTop;
                                yCoordTop = buf;
                            }
                            p.setBrush(pref.Graphs.Color.Ticks.Background.background);
                            p.setPen(pref.Graphs.Color.Ticks.Background.background);
                            auto rect = QRect(plotAreaLeft+1, yCoordTop+1, plotAreaWidth-2, yCoordBot-yCoordTop-2);
                            p.drawRect(rect);
                        }
                    }
                    p.setPen(QPen(pref.Graphs.Color.Ticks.divisions, 0.5, Qt::DashLine));
                    p.drawLine(plotAreaLeft, yCoord, plotAreaLeft + plotAreaWidth, yCoord);
                }
            }
        }

        // plot traces
        p.setClipRect(QRect(plotRect.x()+1, plotRect.y()+1, plotRect.width()-2, plotRect.height()-2));
        for(auto t : tracesAxis[i]) {
            if(!t->isVisible()) {
                continue;
            }
            pen = QPen(t->color(), pref.Graphs.lineWidth);
            pen.setCosmetic(true);
            if(i == 1) {
                pen.setStyle(Qt::DotLine);
            } else {
                pen.setStyle(Qt::SolidLine);
            }
            p.setPen(pen);
            auto nPoints = t->size();
            for(unsigned int j=1;j<nPoints;j++) {
                auto last = traceToCoordinate(t, j-1, YAxis[i].type);
                auto now = traceToCoordinate(t, j, YAxis[i].type);

                if(isnan(last.y()) || isnan(now.y()) || isinf(last.y()) || isinf(now.y())) {
                    continue;
                }

                // scale to plot coordinates
                auto p1 = plotValueToPixel(last, i);
                auto p2 = plotValueToPixel(now, i);
                if(!plotRect.contains(p1) && !plotRect.contains(p2)) {
                    // completely out of frame
                    continue;
                }
                // draw line
                p.drawLine(p1, p2);
            }
            if(i == 0 && nPoints > 0) {
                // only draw markers on primary YAxis and if the trace has at least one point
                auto markers = t->getMarkers();
                for(auto m : markers) {
                    double xPosition = m->getPosition();
                    if (xPosition < XAxis.rangeMin || xPosition > XAxis.rangeMax) {
                        // marker not in graph range
                        continue;
                    }
                    if(xPosition < t->minX() || xPosition > t->maxX()) {
                        // marker not in trace range
                        continue;
                    }
                    auto t = m->getTrace();
                    auto index = t->index(xPosition);
                    QPointF markerPoint;
                    if(xPosition < t->sample(index).x && index > 0) {
                        // marker is not located exactly at this point, interpolate display location
                        QPointF l0 = traceToCoordinate(t, index - 1, YAxis[i].type);
                        QPointF l1 = traceToCoordinate(t, index, YAxis[i].type);
                        auto t0 = (xPosition - t->sample(index - 1).x) / (t->sample(index).x - t->sample(index - 1).x);
                        markerPoint = l0 + (l1 - l0) * t0;
                    } else {
                        markerPoint = traceToCoordinate(t, t->index(xPosition), YAxis[i].type);
                    }
                    auto point = plotValueToPixel(markerPoint, i);
                    if(!plotRect.contains(point)) {
                        // out of screen
                        continue;
                    }
                    auto symbol = m->getSymbol();
                    point += QPoint(-symbol.width()/2, -symbol.height());
                    p.drawPixmap(point, symbol);
                }
            }
        }
        p.setClipping(false);
    }

    if(XAxis.ticks.size() >= 1) {
        // draw X ticks
        int significantDigits;
        bool displayFullFreq;
        if(XAxis.log) {
            significantDigits = 5;
            displayFullFreq = true;
        } else {
            // this only works for evenly distributed ticks:
            auto max = qMax(abs(XAxis.ticks.front()), abs(XAxis.ticks.back()));
            double step;
            if(XAxis.ticks.size() >= 2) {
                step = abs(XAxis.ticks[0] - XAxis.ticks[1]);
            } else {
                // only one tick, set arbitrary number of digits
                step = max / 1000;
            }
            significantDigits = floor(log10(max)) - floor(log10(step)) + 1;
            displayFullFreq = significantDigits <= 5;
        }
        constexpr int displayLastDigits = 4;
        QString prefixes = "fpnum kMG";
        QString unit = "";
        if(pref.Graphs.showUnits) {
            unit = AxisUnit(XAxis.type);
        }
        QString commonPrefix = QString();
        if(!displayFullFreq) {
            auto fullFreq = Unit::ToString(XAxis.ticks.front(), unit, prefixes, significantDigits);
            commonPrefix = fullFreq.at(fullFreq.size() - 1);
            auto front = fullFreq;
            front.truncate(fullFreq.size() - displayLastDigits - unit.length());
            auto back = fullFreq;
            back.remove(0, front.size());
            back.append("..");
            p.setPen(QPen(QColor("orange")));
            QRect bounding;
            p.drawText(QRect(2, plotAreaBottom + AxisLabelSize + 5, w.width(), AxisLabelSize), 0, front, &bounding);
            p.setPen(pref.Graphs.Color.axis);
            p.drawText(QRect(bounding.x() + bounding.width(), plotAreaBottom + AxisLabelSize + 5, w.width(), AxisLabelSize), 0, back);
        }

        int lastTickLabelEnd = 0;
        for(auto t : XAxis.ticks) {
            auto xCoord = Util::Scale<double>(t, XAxis.rangeMin, XAxis.rangeMax, plotAreaLeft, plotAreaLeft + plotAreaWidth, XAxis.log);
            p.setPen(QPen(pref.Graphs.Color.axis, 1));
            p.drawLine(xCoord, plotAreaBottom, xCoord, plotAreaBottom + 2);
            if(xCoord != plotAreaLeft && xCoord != plotAreaLeft + plotAreaWidth) {
                p.setPen(QPen(pref.Graphs.Color.Ticks.divisions, 0.5, Qt::DashLine));
                p.drawLine(xCoord, plotAreaTop, xCoord, plotAreaBottom);
            }
            if(xCoord - 40 <= lastTickLabelEnd) {
                // would overlap previous tick label, skip
                continue;
            }
            auto tickValue = Unit::ToString(t, unit, prefixes, significantDigits);
            p.setPen(QPen(pref.Graphs.Color.axis, 1));
            if(displayFullFreq) {
                QRect bounding;
                p.drawText(QRect(xCoord - 40, plotAreaBottom + 5, 80, AxisLabelSize), Qt::AlignHCenter, tickValue, &bounding);
                lastTickLabelEnd = bounding.x() + bounding.width();
            } else {
                // check if the same prefix was used as in the fullFreq string
                if(tickValue.at(tickValue.size() - 1) != commonPrefix) {
                    // prefix changed, we reached the next order of magnitude. Force same prefix as in fullFreq and add extra digit
                    tickValue = Unit::ToString(t, "", commonPrefix, significantDigits + 1);
                }

                tickValue.remove(0, tickValue.size() - displayLastDigits - unit.length());
                QRect bounding;
                p.drawText(QRect(xCoord - 40, plotAreaBottom + 5, 80, AxisLabelSize), Qt::AlignHCenter, tickValue, &bounding);
                lastTickLabelEnd = bounding.x() + bounding.width();
                p.setPen(QPen(QColor("orange")));
                p.drawText(QRect(0, plotAreaBottom + 5, bounding.x() - 1, AxisLabelSize), Qt::AlignRight, "..", &bounding);
            }
        }
    }

    if(dropPending) {
        p.setOpacity(0.5);
        p.setBrush(Qt::white);
        p.setPen(Qt::white);
        if((YAxis[0].type == YAxisType::Disabled || !supported(dropTrace, YAxis[0].type))
            || (YAxis[1].type == YAxisType::Disabled || !supported(dropTrace, YAxis[1].type))) {
            // only one axis enabled, show drop area over whole plot
            p.drawRect(plotRect);
            auto font = p.font();
            font.setPixelSize(20);
            p.setFont(font);
            p.setOpacity(1.0);
            p.setPen(Qt::white);
            auto text = "Drop here to add\n" + dropTrace->name() + "\nto XY-plot";
            p.drawText(plotRect, Qt::AlignCenter, text);
        } else {
            // both axis enabled, show regions
            auto leftRect = plotRect;
            leftRect.setWidth(plotRect.width() * 0.3);
            auto centerRect = plotRect;
            centerRect.setX(centerRect.x() + plotRect.width() * 0.35);
            centerRect.setWidth(plotRect.width() * 0.3);
            auto rightRect = plotRect;
            rightRect.setX(rightRect.x() + plotRect.width() * 0.7);
            rightRect.setWidth(plotRect.width() * 0.3);
            p.drawRect(leftRect);
            p.drawRect(centerRect);
            p.drawRect(rightRect);
            p.setOpacity(1.0);
            p.setPen(Qt::white);
            auto font = p.font();
            font.setPixelSize(20);
            p.setFont(font);
            p.drawText(leftRect, Qt::AlignCenter, "Drop here to add\nto primary axis");
            p.drawText(centerRect, Qt::AlignCenter, "Drop here to add\nto boths axes");
            p.drawText(rightRect, Qt::AlignCenter, "Drop here to add\nto secondary axis");
        }
    }
}

void TraceXYPlot::updateAxisTicks()
{
    auto createEvenlySpacedTicks = [](vector<double>& ticks, double start, double stop, double step) {
        ticks.clear();
        if(start > stop) {
            swap(start, stop);
        }
        step = abs(step);
        constexpr unsigned int maxTicks = 100;
        for(double tick = start; tick - stop < numeric_limits<double>::epsilon() && ticks.size() <= maxTicks;tick+= step) {
            ticks.push_back(tick);
        }
    };

    auto createAutomaticTicks = [](vector<double>& ticks, double start, double stop, int minDivisions) -> double {
        Q_ASSERT(stop > start);
        ticks.clear();
        double max_div_step = (stop - start) / minDivisions;
        int zeros = floor(log10(max_div_step));
        double decimals_shift = pow(10, zeros);
        max_div_step /= decimals_shift;
        if(max_div_step >= 5) {
            max_div_step = 5;
        } else if(max_div_step >= 2) {
            max_div_step = 2;
        } else {
            max_div_step = 1;
        }
        auto div_step = max_div_step * decimals_shift;
        // round min up to next multiple of div_step
        auto start_div = ceil(start / div_step) * div_step;
        for(double tick = start_div;tick <= stop;tick += div_step) {
            ticks.push_back(tick);
        }
        return div_step;
    };

    auto createLogarithmicTicks = [](vector<double>& ticks, double start, double stop, int minDivisions) {
        // enforce usable log settings
        if(start <= 0) {
            start = 1.0;
        }
        if(stop <= start) {
            stop = start + 1.0;
        }
        ticks.clear();

        auto decades = log10(stop) - log10(start);
        double max_div_decade = minDivisions / decades;
        int zeros = floor(log10(max_div_decade));
        double decimals_shift = pow(10, zeros);
        max_div_decade /= decimals_shift;
        if(max_div_decade < 2) {
            max_div_decade = 2;
        } else if(max_div_decade < 5) {
            max_div_decade = 5;
        } else {
            max_div_decade = 10;
        }
        auto step = pow(10, floor(log10(start))+1) / (max_div_decade * decimals_shift);
        // round min up to next multiple of div_step
        auto div = ceil(start / step) * step;
        if(floor(log10(div)) != floor(log10(start))) {
            // first div is already at the next decade
            step *= 10;
        }
        do {
            ticks.push_back(div);
            if(ticks.size() > 1 && div != step && floor(log10(div)) != floor(log10(div - step))) {
                // reached a new decade with this switch
                step *= 10;
                div = step;
            } else {
                div += step;
            }
        } while(div <= stop);
    };

    if(XAxis.mode == XAxisMode::Manual) {
        if(XAxis.log) {
            createLogarithmicTicks(XAxis.ticks, XAxis.rangeMin, XAxis.rangeMax, 20);
        } else {
            createEvenlySpacedTicks(XAxis.ticks, XAxis.rangeMin, XAxis.rangeMax, XAxis.rangeDiv);
        }
    } else {
        XAxis.ticks.clear();
        // automatic mode, figure out limits
        double max = std::numeric_limits<double>::lowest();
        double min = std::numeric_limits<double>::max();
        if(XAxis.mode == XAxisMode::UseSpan) {
            min = sweep_fmin;
            max = sweep_fmax;
        } else if(XAxis.mode == XAxisMode::FitTraces) {
            for(auto t : traces) {
                bool enabled = (tracesAxis[0].find(t.first) != tracesAxis[0].end()
                        || tracesAxis[1].find(t.first) != tracesAxis[1].end());
                auto trace = t.first;
                if(enabled && trace->isVisible()) {
                    if(!trace->size()) {
                        // empty trace, do not use for automatic axis calculation
                        continue;
                    }
                    // this trace is currently displayed
                    double trace_min = trace->minX();
                    double trace_max = trace->maxX();
                    if(XAxis.type == XAxisType::Distance) {
                        trace_min = trace->timeToDistance(trace_min);
                        trace_max = trace->timeToDistance(trace_max);
                    }
                    if(trace_min < min) {
                        min = trace_min;
                    }
                    if(trace_max > max) {
                        max = trace_max;
                    }
                }
            }
        }
        if(min < max) {
            // found min/max values
            XAxis.rangeMin = min;
            XAxis.rangeMax = max;
            if(XAxis.log) {
                createLogarithmicTicks(XAxis.ticks, XAxis.rangeMin, XAxis.rangeMax, 20);
            } else {
                XAxis.rangeDiv = createAutomaticTicks(XAxis.ticks, min, max, 8);
            }
        }
    }

    for(int i=0;i<2;i++) {
        if(!YAxis[i].autorange) {
            createEvenlySpacedTicks(YAxis[i].ticks, YAxis[i].rangeMin, YAxis[i].rangeMax, YAxis[i].rangeDiv);
        } else {
            // automatic mode, figure out limits
            double max = std::numeric_limits<double>::lowest();
            double min = std::numeric_limits<double>::max();
            for(auto t : tracesAxis[i]) {
                if(!t->isVisible()) {
                    continue;
                }
                unsigned int samples = t->size();
                for(unsigned int j=0;j<samples;j++) {
                    auto point = traceToCoordinate(t, j, YAxis[i].type);

                    if(point.x() < XAxis.rangeMin || point.x() > XAxis.rangeMax) {
                        // this point is not in the displayed X range, skip for auto Y range calculation
                        continue;
                    }

                    if(point.y() > max) {
                        max = point.y();
                    }
                    if(point.y() < min) {
                        min = point.y();
                    }
                }
            }
            if(max >= min) {
                auto range = max - min;
                if(range == 0.0) {
                    // this could happen if all values in a trace are identical (e.g. imported ideal touchstone files)
                    if(max == 0.0) {
                        // simply use +/-1 range
                        max = 1.0;
                        min = -1.0;
                    } else {
                        // +/-5% around value
                        max += abs(max * 0.05);
                        min -= abs(max * 0.05);
                    }
                } else {
                    // add 5% of range at both ends
                    min -= range * 0.05;
                    max += range * 0.05;
                }
            } else {
                // max/min still at default values, no valid samples are available for this axis, use default range
                max = 1.0;
                min = -1.0;
            }
            YAxis[i].rangeMin = min;
            YAxis[i].rangeMax = max;
            YAxis[i].rangeDiv = createAutomaticTicks(YAxis[i].ticks, min, max, 8);
        }
    }
}

QString TraceXYPlot::AxisTypeToName(TraceXYPlot::XAxisType type)
{
    switch(type) {
    case XAxisType::Frequency: return "Frequency";
    case XAxisType::Time: return "Time";
    case XAxisType::Distance: return "Distance";
    case XAxisType::Power: return "Power";
    default: return "Unknown";
    }
}

QString TraceXYPlot::AxisModeToName(TraceXYPlot::XAxisMode mode)
{
    switch(mode) {
    case XAxisMode::Manual: return "Manual"; break;
    case XAxisMode::FitTraces: return "Fit Traces"; break;
    case XAxisMode::UseSpan: return "Use Span"; break;
    default: return "Unknown";
    }
}

TraceXYPlot::XAxisType TraceXYPlot::XAxisTypeFromName(QString name)
{
    for(unsigned int i=0;i<(int) XAxisType::Last;i++) {
        if(AxisTypeToName((XAxisType) i) == name) {
            return (XAxisType) i;
        }
    }
    // not found, use default
    return XAxisType::Frequency;
}

TraceXYPlot::YAxisType TraceXYPlot::YAxisTypeFromName(QString name)
{
    for(unsigned int i=0;i<(int) YAxisType::Last;i++) {
        if(AxisTypeToName((YAxisType) i) == name) {
            return (YAxisType) i;
        }
    }
    // not found, use default
    return YAxisType::Magnitude;
}

TraceXYPlot::XAxisMode TraceXYPlot::AxisModeFromName(QString name)
{
    for(unsigned int i=0;i<(int) XAxisMode::Last;i++) {
        if(AxisModeToName((XAxisMode) i) == name) {
            return (XAxisMode) i;
        }
    }
    // not found, use default
    return XAxisMode::UseSpan;
}

QString TraceXYPlot::AxisTypeToName(TraceXYPlot::YAxisType type)
{
    switch(type) {
    case YAxisType::Disabled: return "Disabled";
    case YAxisType::Magnitude: return "Magnitude";
    case YAxisType::Phase: return "Phase";
    case YAxisType::UnwrappedPhase: return "Unwrapped Phase";
    case YAxisType::VSWR: return "VSWR";
    case YAxisType::Real: return "Real";
    case YAxisType::Imaginary: return "Imaginary";
    case YAxisType::SeriesR: return "Resistance";
    case YAxisType::Reactance: return "Reactance";
    case YAxisType::Capacitance: return "Capacitance";
    case YAxisType::Inductance: return "Inductance";
    case YAxisType::QualityFactor: return "Quality Factor";
    case YAxisType::GroupDelay: return "Group delay";
    case YAxisType::ImpulseReal: return "Impulse Response (Real)";
    case YAxisType::ImpulseMag: return "Impulse Response (Magnitude)";
    case YAxisType::Step: return "Step Response";
    case YAxisType::Impedance: return "Impedance";
    case YAxisType::Last: return "Unknown";
    }
    return "Missing case";
}

void TraceXYPlot::enableTraceAxis(Trace *t, int axis, bool enabled)
{
    if(enabled && !supported(t, YAxis[axis].type)) {
        // unable to add trace to the requested axis
        return;
    }
    if(axis == 0) {
        TracePlot::enableTrace(t, enabled);
    }
    bool alreadyEnabled = tracesAxis[axis].find(t) != tracesAxis[axis].end();
    if(alreadyEnabled != enabled) {
        if(enabled) {
            tracesAxis[axis].insert(t);
        } else {
            tracesAxis[axis].erase(t);
            if(axis == 0) {
                disconnect(t, &Trace::markerAdded, this, &TraceXYPlot::markerAdded);
                disconnect(t, &Trace::markerRemoved, this, &TraceXYPlot::markerRemoved);
                auto tracemarkers = t->getMarkers();
                for(auto m : tracemarkers) {
                    markerRemoved(m);
                }
            }
        }

        updateContextMenu();
        replot();
    }
}

bool TraceXYPlot::domainMatch(Trace *t)
{
    switch(XAxis.type) {
    case XAxisType::Frequency:
        return t->outputType() == Trace::DataType::Frequency;
    case XAxisType::Distance:
    case XAxisType::Time:
        return t->outputType() == Trace::DataType::Time;
    case XAxisType::Power:
        return t->outputType() == Trace::DataType::Power;
    case XAxisType::Last:
        return false;
    }
    return false;
}

bool TraceXYPlot::supported(Trace *t, TraceXYPlot::YAxisType type)
{
    if(!domainMatch(t)) {
        return false;
    }

    switch(type) {
    case YAxisType::Disabled:
        return false;
    case YAxisType::VSWR:
    case YAxisType::SeriesR:
    case YAxisType::Reactance:
    case YAxisType::Capacitance:
    case YAxisType::Inductance:
    case YAxisType::QualityFactor:
        if(!t->isReflection()) {
            return false;
        }
        break;
    case YAxisType::GroupDelay:
        if(t->isReflection()) {
            return false;
        }
        break;
    default:
        break;
    }
    return true;
}

QPointF TraceXYPlot::traceToCoordinate(Trace *t, unsigned int sample, TraceXYPlot::YAxisType type)
{
    QPointF ret = QPointF(numeric_limits<double>::quiet_NaN(), numeric_limits<double>::quiet_NaN());
    auto data = t->sample(sample);
    switch(XAxis.type) {
    case XAxisType::Distance:
        ret.setX(t->timeToDistance(data.x));
        break;
    default:
        ret.setX(data.x);
        break;
    }
    switch(type) {
    case YAxisType::Magnitude:
        ret.setY(Util::SparamTodB(data.y));
        break;
    case YAxisType::Phase:
        ret.setY(Util::SparamToDegree(data.y));
        break;
    case YAxisType::UnwrappedPhase:
        ret.setY(t->getUnwrappedPhase(sample) * 180.0 / M_PI);
        break;
    case YAxisType::VSWR:
        ret.setY(Util::SparamToVSWR(data.y));
        break;
    case YAxisType::Real:
        ret.setY(data.y.real());
        break;
    case YAxisType::Imaginary:
        ret.setY(data.y.imag());
        break;
    case YAxisType::SeriesR:
        ret.setY(Util::SparamToResistance(data.y));
        break;
    case YAxisType::Reactance:
        ret.setY(Util::SparamToImpedance(data.y).imag());
        break;
    case YAxisType::Capacitance:
        ret.setY(Util::SparamToCapacitance(data.y, data.x));
        break;
    case YAxisType::Inductance:
        ret.setY(Util::SparamToInductance(data.y, data.x));
        break;
    case YAxisType::QualityFactor:
        ret.setY(Util::SparamToQualityFactor(data.y));
        break;
    case YAxisType::GroupDelay: {
        constexpr int requiredSamples = 5;
        if(t->size() < requiredSamples) {
            // unable to calculate
            ret.setY(0.0);
            break;
        }
        // needs at least some samples before/after current sample for calculating the derivative.
        // For samples too far at either end of the trace, return group delay of "inner" trace sample instead
        if(sample < requiredSamples / 2) {
            return traceToCoordinate(t, requiredSamples / 2, type);
        } else if(sample >= t->size() - requiredSamples / 2) {
            return traceToCoordinate(t, t->size() - requiredSamples / 2 - 1, type);
        } else {
            // got enough samples at either end to calculate derivative.
            // acquire phases of the required samples
            std::vector<double> phases;
            phases.reserve(requiredSamples);
            for(unsigned int index = sample - requiredSamples / 2;index <= sample + requiredSamples / 2;index++) {
                phases.push_back(arg(t->sample(index).y));
            }
            // make sure there are no phase jumps
            Util::unwrapPhase(phases);
            // calculate linearRegression to get derivative
            double B_0, B_1;
            Util::linearRegression(phases, B_0, B_1);
            // B_1 now contains the derived phase vs. the sample. Scale by frequency to get group delay
            double freq_step = t->sample(sample).x - t->sample(sample - 1).x;
            ret.setY(-B_1 / (2.0*M_PI * freq_step));
        }
    }
        break;
    case YAxisType::ImpulseReal:
        ret.setY(real(data.y));
        break;
    case YAxisType::ImpulseMag:
        ret.setY(Util::SparamTodB(data.y));
        break;
    case YAxisType::Step:
        ret.setY(t->sample(sample, true).y.real());
        break;
    case YAxisType::Impedance: {
        double step = t->sample(sample, true).y.real();
        if(abs(step) < 1.0) {
            ret.setY(Util::SparamToImpedance(step).real());
        }
    }
        break;
    case YAxisType::Disabled:
    case YAxisType::Last:
        // no valid axis
        break;
    }
    return ret;
}

QPoint TraceXYPlot::plotValueToPixel(QPointF plotValue, int Yaxis)
{
    QPoint p;
    p.setX(Util::Scale<double>(plotValue.x(), XAxis.rangeMin, XAxis.rangeMax, plotAreaLeft, plotAreaLeft + plotAreaWidth, XAxis.log));
    p.setY(Util::Scale<double>(plotValue.y(), YAxis[Yaxis].rangeMin, YAxis[Yaxis].rangeMax, plotAreaBottom, plotAreaTop));
    return p;
}

QPointF TraceXYPlot::pixelToPlotValue(QPoint pixel, int Yaxis)
{
    QPointF p;
    p.setX(Util::Scale<double>(pixel.x(), plotAreaLeft, plotAreaLeft + plotAreaWidth, XAxis.rangeMin, XAxis.rangeMax, false, XAxis.log));
    p.setY(Util::Scale<double>(pixel.y(), plotAreaBottom, plotAreaTop, YAxis[Yaxis].rangeMin, YAxis[Yaxis].rangeMax));
    return p;
}

QPoint TraceXYPlot::markerToPixel(Marker *m)
{
    auto t = m->getTrace();
    QPointF plotPoint = traceToCoordinate(t, t->index(m->getPosition()), YAxis[0].type);
    return plotValueToPixel(plotPoint, 0);
}

double TraceXYPlot::nearestTracePoint(Trace *t, QPoint pixel, double *distance)
{
    if(!tracesAxis[0].count(t)) {
        // trace not enabled
        return 0;
    }
    double closestDistance = numeric_limits<double>::max();
    double closestXpos = 0;
    unsigned int closestIndex = 0;
    auto samples = t->size();
    for(unsigned int i=0;i<samples;i++) {
        auto point = traceToCoordinate(t, i, YAxis[0].type);
        if(isnan(point.x()) || isnan(point.y())) {
            continue;
        }
        auto plotPoint = plotValueToPixel(point, 0);
        QPointF diff = plotPoint - pixel;
        auto distance = diff.x() * diff.x() + diff.y() * diff.y();
        if(distance < closestDistance) {
            closestDistance = distance;
            closestXpos = point.x();
            closestIndex = i;
        }
    }
    closestDistance = sqrt(closestDistance);
    if(closestIndex > 0) {
        auto l1 = plotValueToPixel(traceToCoordinate(t, closestIndex - 1, YAxis[0].type), 0);
        auto l2 = plotValueToPixel(traceToCoordinate(t, closestIndex, YAxis[0].type), 0);
        double ratio;
        auto distance = Util::distanceToLine(pixel, l1, l2, nullptr, &ratio);
        if(distance < closestDistance) {
            closestDistance = distance;
            closestXpos = t->sample(closestIndex-1).x + (t->sample(closestIndex).x - t->sample(closestIndex-1).x) * ratio;
        }
    }
    if(closestIndex < t->size() - 1) {
        auto l1 = plotValueToPixel(traceToCoordinate(t, closestIndex, YAxis[0].type), 0);
        auto l2 = plotValueToPixel(traceToCoordinate(t, closestIndex + 1, YAxis[0].type), 0);
        double ratio;
        auto distance = Util::distanceToLine(pixel, l1, l2, nullptr, &ratio);
        if(distance < closestDistance) {
            closestDistance = distance;
            closestXpos = t->sample(closestIndex).x + (t->sample(closestIndex+1).x - t->sample(closestIndex).x) * ratio;
        }
    }
    if(XAxis.type == XAxisType::Distance) {
        closestXpos = t->distanceToTime(closestXpos);
    }
    if(distance) {
        *distance = closestDistance;
    }
    return closestXpos;
}

bool TraceXYPlot::xCoordinateVisible(double x)
{
    if(x >= min(XAxis.rangeMin, XAxis.rangeMax) && x <= max(XAxis.rangeMax, XAxis.rangeMin)) {
        return true;
    } else {
        return false;
    }
}

void TraceXYPlot::traceDropped(Trace *t, QPoint position)
{
    if(!supported(t)) {
        // needs to switch to a different domain for the graph
        if(!InformationBox::AskQuestion("X Axis Domain Change", "You dropped a trace that is not supported with the currently selected X axis domain."
                                    " Do you want to remove all traces and change the graph to the correct domain?", true, "DomainChangeRequest")) {
            // user declined to change domain, to not add trace
            return;
        }
        if(!configureForTrace(t)) {
            // failed to configure
            return;
        }
    }
    if(YAxis[0].type == YAxisType::Disabled && YAxis[1].type == YAxisType::Disabled) {
        // no Y axis enabled, unable to drop
        return;
    }
    if(YAxis[0].type == YAxisType::Disabled) {
        // only axis 1 enabled
        enableTraceAxis(t, 1, true);
        return;
    }
    if(YAxis[1].type == YAxisType::Disabled) {
        // only axis 0 enabled
        enableTraceAxis(t, 0, true);
        return;
    }
    // both axis enabled, check drop position
    auto drop = Util::Scale<double>(position.x(), plotAreaLeft, plotAreaLeft + plotAreaWidth, 0.0, 1.0);
    if(drop < 0.66) {
        enableTraceAxis(t, 0, true);
    }
    if(drop > 0.33) {
        enableTraceAxis(t, 1, true);
    }
}

QString TraceXYPlot::mouseText(QPoint pos)
{
    QString ret;
    if(QRect(plotAreaLeft, 0, plotAreaWidth + 1, plotAreaBottom).contains(pos)) {
        // cursor within plot area
        QPointF coords[2];
        coords[0] = pixelToPlotValue(pos, 0);
        coords[1] = pixelToPlotValue(pos, 1);
        int significantDigits = floor(log10(abs(XAxis.rangeMax))) - floor(log10((abs(XAxis.rangeMax - XAxis.rangeMin)) / 1000.0)) + 1;
        ret += Unit::ToString(coords[0].x(), AxisUnit(XAxis.type), "fpnum kMG", significantDigits) + "\n";
        for(int i=0;i<2;i++) {
            if(YAxis[i].type != YAxisType::Disabled) {
                auto max = qMax(abs(YAxis[i].rangeMax), abs(YAxis[i].rangeMin));
                auto step = abs(YAxis[i].rangeMax - YAxis[i].rangeMin) / 1000.0;
                significantDigits = floor(log10(max)) - floor(log10(step)) + 1;
                ret += Unit::ToString(coords[i].y(), AxisUnit(YAxis[i].type), "fpnum kMG", significantDigits) + "\n";
            }
        }
    }
    return ret;
}

QString TraceXYPlot::AxisUnit(TraceXYPlot::YAxisType type)
{
    auto source = model.getSource();
    if(source == TraceModel::DataSource::VNA) {
        switch(type) {
        case TraceXYPlot::YAxisType::Magnitude: return "dB";
        case TraceXYPlot::YAxisType::Phase: return "°";
        case TraceXYPlot::YAxisType::UnwrappedPhase: return "°";
        case TraceXYPlot::YAxisType::VSWR: return "";
        case TraceXYPlot::YAxisType::ImpulseReal: return "";
        case TraceXYPlot::YAxisType::ImpulseMag: return "dB";
        case TraceXYPlot::YAxisType::Step: return "";
        case TraceXYPlot::YAxisType::Impedance: return "Ohm";
        case TraceXYPlot::YAxisType::GroupDelay: return "s";
        case TraceXYPlot::YAxisType::Disabled:
        case TraceXYPlot::YAxisType::Real:
        case TraceXYPlot::YAxisType::Imaginary:
        case TraceXYPlot::YAxisType::QualityFactor:
            return "";
        case TraceXYPlot::YAxisType::SeriesR: return "Ω";
        case TraceXYPlot::YAxisType::Reactance: return "Ω";
        case TraceXYPlot::YAxisType::Capacitance: return "F";
        case TraceXYPlot::YAxisType::Inductance: return "H";
        case TraceXYPlot::YAxisType::Last: return "";
        }
    } else if(source == TraceModel::DataSource::SA) {
        switch(type) {
        case TraceXYPlot::YAxisType::Magnitude: return "dBm";
        default: return "";
        }
    }
    return "";
}

QString TraceXYPlot::AxisUnit(TraceXYPlot::XAxisType type)
{
    switch(type) {
    case XAxisType::Frequency: return "Hz";
    case XAxisType::Time: return "s";
    case XAxisType::Distance: return "m";
    case XAxisType::Power: return "dBm";
    default: return "";
    }
}
