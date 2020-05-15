/*
 *  Copyright (c) 2015 Jouni Pentikäinen <joupent@gmail.com>
 *  Copyright (c) 2020 Emmet O'Neill <emmetoneill.pdx@gmail.com>
 *  Copyright (c) 2020 Eoin O'Neill <eoinoneill1991@gmail.com>
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

#ifndef _TIMELINE_DOCKER_H_
#define _TIMELINE_DOCKER_H_

#include "kritaimage_export.h"

#include <QDockWidget>
#include <kis_mainwindow_observer.h>

#include <QScopedPointer>

class KisCanvas2;
class KisAction;

class TimelineDockerTitlebar : public QWidget
{
    Q_OBJECT

public:
    TimelineDockerTitlebar(QWidget *parent = nullptr);
    ~TimelineDockerTitlebar();

    virtual QSize sizeHint() const {return QSize(32,32);}
    virtual QSize minimumSizeHint() const {return QSize(32,32);}

    class QToolButton* btnAddKeyframe;
    class QToolButton* btnDuplicateKeyframe;
    class QToolButton* btnRemoveKeyframe;
    class KisTransportControls* transport;
    class QSpinBox *frameCounter;

private:
    const u_int SPACING_UNIT = 8;
    const u_int MAX_FRAMES = 9999;


};

class TimelineDocker : public QDockWidget, public KisMainwindowObserver
{
    Q_OBJECT
public:
    TimelineDocker();
    ~TimelineDocker() override;

    QString observerName() override { return "TimelineDocker"; }
    void setCanvas(KoCanvasBase *canvas) override;
    void unsetCanvas() override;
    void setViewManager(KisViewManager *kisview) override;

public Q_SLOTS:
    void playPause();
    void stop();
    void previousFrame();
    void nextFrame();
    void goToFrame(int frameIndex);

    void handleThemeChange();
    void updateFrameCache();

private:
    struct Private;
    const QScopedPointer<Private> m_d;
};


#endif
