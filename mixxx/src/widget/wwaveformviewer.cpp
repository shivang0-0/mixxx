
#include <QDebug>
#include <QtXml/QDomNode>
#include <QEvent>
#include <QDragEnterEvent>
#include <QUrl>
#include <QPainter>

#include "controlobject.h"
#include "controlobjectthreadmain.h"
#include "mixxx.h"
#include "trackinfoobject.h"
#include "waveform/widgets/waveformwidgetabstract.h"
#include "widget/wwaveformviewer.h"
#include "waveform/waveformwidgetfactory.h"

WWaveformViewer::WWaveformViewer(const char *group, QWidget * parent, Qt::WFlags f) :
    QWidget(parent) {
    m_pGroup = group;

    setAcceptDrops(true);

    m_bScratching = false;
    m_bBending = false;
    m_iMouseStart = -1;
    m_pScratchEnable = new ControlObjectThreadMain(
                ControlObject::getControl(ConfigKey(group, "scratch_position_enable")));
    m_pScratch = new ControlObjectThreadMain(
                ControlObject::getControl(ConfigKey(group, "scratch_position")));
    m_pTrackSamples = new ControlObjectThreadMain(
                ControlObject::getControl(ConfigKey(group, "track_samples")));
    m_pTrackSampleRate = new ControlObjectThreadMain(
                ControlObject::getControl(ConfigKey(group, "track_samplerate")));
    m_pRate = new ControlObjectThreadMain(
                ControlObject::getControl(ConfigKey(m_pGroup, "rate")));
    m_pRateRange = new ControlObjectThreadMain(
                ControlObject::getControl(ConfigKey(m_pGroup, "rateRange")));
    m_pRateDir = new ControlObjectThreadMain(
                ControlObject::getControl(ConfigKey(m_pGroup, "rate_dir")));

    setAttribute(Qt::WA_ForceUpdatesDisabled);
    setAttribute(Qt::WA_OpaquePaintEvent);

    m_zoomZoneWidth = 20;
    m_waveformWidget = 0;
}

WWaveformViewer::~WWaveformViewer() {
    delete m_pScratchEnable;
    delete m_pScratch;
    delete m_pTrackSamples;
    delete m_pTrackSampleRate;
    delete m_pRate;
    delete m_pRateRange;
    delete m_pRateDir;
}

void WWaveformViewer::setup(QDomNode node) {
    if (m_waveformWidget)
        m_waveformWidget->setup(node);
}

void WWaveformViewer::resizeEvent(QResizeEvent* /*event*/) {
    if (m_waveformWidget) {
        m_waveformWidget->resize(width(),height());
    }
}

void WWaveformViewer::mousePressEvent(QMouseEvent* event) {
    m_mouseAnchor = event->pos();
    m_iMouseStart = event->x();

    if(event->button() == Qt::LeftButton) {
        // If we are pitch-bending then disable and reset because the two
        // shouldn't be used at once.
        if (m_bBending) {
            emit(valueChangedRightDown(64));
            m_bBending = false;
        }
        m_bScratching = true;
        m_pScratch->slotSet(0.0f);
        m_pScratchEnable->slotSet(1.0f);
    } else if (event->button() == Qt::RightButton) {
        // If we are scratching then disable and reset because the two shouldn't
        // be used at once.
        if (m_bScratching) {
            m_pScratch->slotSet(0.0f);
            m_pScratchEnable->slotSet(0.0f);
            m_bScratching = false;
        }
        emit(valueChangedRightDown(64));
        m_bBending = true;
        
        //also reset zoom:
        if (m_waveformWidget) {
            if(WaveformWidgetFactory::instance()){
                m_waveformWidget->setZoom(WaveformWidgetFactory::instance()->getDefaultZoom());
                WaveformWidgetFactory::instance()->onZoomChange(m_waveformWidget);
            }
        }
    }

    // Set the cursor to a hand while the mouse is down.
    setCursor(Qt::ClosedHandCursor);
}

void WWaveformViewer::mouseMoveEvent(QMouseEvent* event) {
    QPoint diff = event->pos() - m_mouseAnchor;

    // Only send signals for mouse moving if the left button is pressed
    if (m_iMouseStart != -1 && m_bScratching) {
        // Adjusts for one-to-one movement. Track sample rate in hundreds of
        // samples times two is the number of samples per pixel.  rryan
        // 4/2011
        double samplesPerPixel = m_pTrackSampleRate->get() / 100.0 * 2;

        // To take care of one one movement when zoom changes with pitch
        double rateAdjust = m_pRateDir->get() *
                math_min(0.99, m_pRate->get() * m_pRateRange->get());
        double targetPosition = -1.0 * diff.x() *
                samplesPerPixel * (1 + rateAdjust);
        //qDebug() << "Target:" << targetPosition;
        m_pScratch->slotSet(targetPosition);
    } else if (m_iMouseStart != -1 && m_bBending) {
        // start at the middle of 0-127, and emit values based on
        // how far the mouse has travelled horizontally
        double v = 64.0 + diff.x()/10.0f;
        // clamp to [0, 127]
        v = math_min(127.0, math_max(0.0, v));
        emit(valueChangedRightDown(v));
    }
}

void WWaveformViewer::mouseReleaseEvent(QMouseEvent* event){
    if (m_bScratching) {
        m_pScratchEnable->slotSet(0.0f);
        m_pScratch->slotSet(0.0f);
        m_bScratching = false;
    }
    if (m_bBending) {
        emit(valueChangedRightDown(64));
        m_bBending = false;
    }
    m_iMouseStart = -1;
    m_mouseAnchor = QPoint();

    // Set the cursor back to an arrow.
    setCursor(Qt::ArrowCursor);
}

void WWaveformViewer::wheelEvent(QWheelEvent *event) {
    if (m_waveformWidget) {
        //if (event->x() > width() - m_zoomZoneWidth) {
            if (event->delta() > 0)
                m_waveformWidget->zoomIn();
            else
                m_waveformWidget->zoomOut();

            if(WaveformWidgetFactory::instance()){
                WaveformWidgetFactory::instance()->onZoomChange(m_waveformWidget);
            }
        //}
    }
}

/** DRAG AND DROP **/

void WWaveformViewer::dragEnterEvent(QDragEnterEvent * event) {
    // Accept the enter event if the thing is a filepath.
    if (event->mimeData()->hasUrls() &&
            event->mimeData()->urls().size() > 0) {
        ControlObject *pPlayCO = ControlObject::getControl(
                    ConfigKey(m_pGroup, "play"));
        if (pPlayCO && pPlayCO->get()) {
            event->ignore();
        } else {
            event->acceptProposedAction();
        }
    }
}

void WWaveformViewer::dropEvent(QDropEvent * event) {
    if (event->mimeData()->hasUrls() &&
            event->mimeData()->urls().size() > 0) {
        QList<QUrl> urls(event->mimeData()->urls());
        QUrl url = urls.first();
        QString name = url.toLocalFile();
		//total OWEN hack: because we strip out the library prefix
		//in the view, we have to add it back here again to properly receive
		//drops
        if (!QFile(name).exists())
        {
        	if(QFile(m_sPrefix+"/"+name).exists())
        		name = m_sPrefix+"/"+name;
        }
        //If the file is on a network share, try just converting the URL to a string...
        if (name == "")
            name = url.toString();

        event->accept();
        emit(trackDropped(name, m_pGroup));
    } else {
        event->ignore();
    }
}

void WWaveformViewer::onTrackLoaded( TrackPointer track) {
    if (m_waveformWidget)
        m_waveformWidget->setTrack(track);
}

void WWaveformViewer::onTrackUnloaded( TrackPointer /*track*/) {
    if (m_waveformWidget)
        m_waveformWidget->setTrack(TrackPointer(0));
}

void WWaveformViewer::setLibraryPrefix(QString sPrefix)
{
	m_sPrefix = "";
	m_sPrefix = sPrefix;
	if (sPrefix[sPrefix.length()-1] == '/' || sPrefix[sPrefix.length()-1] == '\\')
		m_sPrefix.chop(1);
}
