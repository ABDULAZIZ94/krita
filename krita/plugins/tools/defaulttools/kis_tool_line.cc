/*
 *  kis_tool_line.cc - part of Krayon
 *
 *  Copyright (c) 2000 John Califf <jwcaliff@compuzone.net>
 *  Copyright (c) 2002 Patrick Julien <freak@codepimps.org>
 *  Copyright (c) 2003 Boudewijn Rempt <boud@valdyas.org>
 *  Copyright (c) 2009 Lukáš Tvrdý <lukast.dev@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "kis_tool_line.h"

#include <QLayout>
#include <QWidget>
#include <QPainter>
#include <QPainterPath>

#include <kis_debug.h>
#include <klocale.h>

#include <kis_selection.h>
#include <kis_paint_device.h>
#include <kis_paint_information.h>
#include <kis_cursor.h>
#include <kis_painter.h>

#include "KoPointerEvent.h"
#include "kis_paintop_registry.h"
#include "kis_layer.h"
#include "KoCanvasBase.h"

#include <recorder/kis_action_recorder.h>
#include <recorder/kis_recorded_polyline_paint_action.h>
#include <recorder/kis_node_query_path.h>


#include <config-opengl.h>

#ifdef HAVE_OPENGL
#include <GL/gl.h>
#endif

#include <KoCanvasController.h>


KisToolLine::KisToolLine(KoCanvasBase * canvas)
        : KisToolPaint(canvas, KisCursor::load("tool_line_cursor.png", 6, 6)),
        m_dragging(false)
{
    setObjectName("tool_line");


    m_painter = 0;
    currentImage() = 0;
    m_startPos = QPointF(0, 0);
    m_endPos = QPointF(0, 0);
}

KisToolLine::~KisToolLine()
{
}

void KisToolLine::paint(QPainter& gc, const KoViewConverter &converter)
{
    qreal sx, sy;
    converter.zoom(&sx, &sy);
    if (m_dragging)
    {
        paintLine(gc, QRect());
    }
}


void KisToolLine::mousePressEvent(KoPointerEvent *e)
{
    if (!m_canvas || !currentImage()) return;

    QPointF pos = convertToPixelCoord(e);

    if (e->button() == Qt::LeftButton) {
        m_dragging = true;
        m_startPos = pos;
        m_endPos = pos;
    }
}

void KisToolLine::mouseMoveEvent(KoPointerEvent *e)
{
    if (m_dragging) {
        // First ensure the old temp line is deleted
        QRectF bound;
        bound.setTopLeft(m_startPos);
        bound.setBottomRight(m_endPos);
        m_canvas->updateCanvas(convertToPt(bound.normalized()));

        QPointF pos = convertToPixelCoord(e);

        if (e->modifiers() & Qt::AltModifier) {
            QPointF trans = pos - m_endPos;
            m_startPos += trans;
            m_endPos += trans;
        } else if (e->modifiers() & Qt::ShiftModifier)
            m_endPos = straightLine(pos);
        else
            m_endPos = pos;

        bound.setTopLeft(m_startPos);
        bound.setBottomRight(m_endPos);
        m_canvas->updateCanvas(convertToPt(bound.normalized()));
    }
}

void KisToolLine::mouseReleaseEvent(KoPointerEvent *e)
{
    QPointF pos = convertToPixelCoord(e);

    if (m_dragging && e->button() == Qt::LeftButton) {
        m_dragging = false;

        if (m_canvas) {

            if (m_startPos == m_endPos) {
                m_dragging = false;
                return;
            }

            if ((e->modifiers() & Qt::ShiftModifier) == Qt::ShiftModifier) {
                m_endPos = straightLine(pos);
            } else m_endPos = pos;

            KisPaintDeviceSP device;

            if (currentNode() && (device = currentNode()->paintDevice())) {
                delete m_painter;
                m_painter = new KisPainter(device, currentSelection());
                Q_CHECK_PTR(m_painter);

                m_painter->beginTransaction(i18nc("a straight drawn line", "Line"));

                m_painter->setPaintColor(currentFgColor());
                m_painter->setOpacity(m_opacity);
                m_painter->setCompositeOp(m_compositeOp);
                m_painter->setPaintOpPreset(currentPaintOpPreset(), currentImage());
                m_painter->paintLine(m_startPos, m_endPos);
                QRegion dirtyRegion = m_painter->dirtyRegion();
                device->setDirty(dirtyRegion);
                notifyModified();
// Should not be necessary anymore because of KisProjection
#if 0
                m_canvas->updateCanvas(convertToPt(dirtyRegion));
#endif

                if (image()) {
                    KisRecordedPolyLinePaintAction* linePaintAction = new KisRecordedPolyLinePaintAction(i18n("Line tool"), KisNodeQueryPath::absolutePath(currentNode()), currentPaintOpPreset(), m_painter->paintColor(), m_painter->backgroundColor(), m_painter->opacity(), false, m_compositeOp);
                    linePaintAction->addPoint(m_startPos);
                    linePaintAction->addPoint(m_endPos);
                    image()->actionRecorder()->addAction(*linePaintAction);
                }

                m_canvas->addCommand(m_painter->endTransaction());
                delete m_painter;
                m_painter = 0;
            } else {
                // Remove the last remaining line.
                // m_painter can be 0 here...!!!
                dbgPlugins << "do we ever go here";
                QRectF bound;
                bound.setTopLeft(m_startPos);
                bound.setBottomRight(m_endPos);
                m_canvas->updateCanvas(convertToPt(bound.normalized()));
            }
        }
    } else {
        KisToolPaint::mouseReleaseEvent(e);
    }
}


QPointF KisToolLine::straightLine(QPointF point)
{
    QPointF comparison = point - m_startPos;
    QPointF result;

    if (fabs(comparison.x()) > fabs(comparison.y())) {
        result.setX(point.x());
        result.setY(m_startPos.y());
    } else {
        result.setX(m_startPos.x());
        result.setY(point.y());
    }

    return result;
}


void KisToolLine::paintLine(QPainter& gc, const QRect&)
{
    QPointF viewStartPos = pixelToView(m_startPos);
    QPointF viewStartEnd = pixelToView(m_endPos);
    
#if defined(HAVE_OPENGL)
    if (m_canvas->canvasController()->isCanvasOpenGL()){
        glEnable(GL_LINE_SMOOTH);
        glEnable(GL_COLOR_LOGIC_OP);
        glLogicOp(GL_XOR);
        
        glBegin(GL_LINES);
            glColor3f(0.5,1.0,0.5);
            glVertex2f( viewStartPos.x(), viewStartPos.y() );
            glVertex2f( viewStartEnd.x(), viewStartEnd.y() );
        glEnd();

        glDisable(GL_COLOR_LOGIC_OP);
        glDisable(GL_LINE_SMOOTH);
    }else
#endif

    if (m_canvas) {
        QPainterPath path;
        path.moveTo(viewStartPos);
        path.lineTo(viewStartEnd);
        paintToolOutline(&gc,path);
/*      
        QPen old = gc.pen();
        QPen pen(Qt::SolidLine);
        gc.drawLine(viewStartPos, viewStartEnd);
        gc.setPen(old);
*/
    }
}

QString KisToolLine::quickHelp() const
{
    return i18n("Alt+Drag will move the origin of the currently displayed line around, Shift+Drag will force you to draw straight lines");
}

#include "kis_tool_line.moc"

