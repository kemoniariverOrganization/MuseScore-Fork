#include "odladriver.h"
#include <QDebug>
#include <QMessageBox>
#include <QAction>
#include <QFileInfo>
#include <QDir>
#include <QTimer>
#include "libmscore/score.h"
#include "libmscore/accidental.h"
#include "libmscore/clef.h"
#include "libmscore/measure.h"
#include "libmscore/chordrest.h"
#include "libmscore/tuplet.h"
#include "libmscore/chord.h"
#include "libmscore/staff.h"
#include "libmscore/keysig.h"
#include "libmscore/slur.h"
#include "libmscore/breath.h"
#include "libmscore/volta.h"
#include "libmscore/marker.h"
#include "libmscore/jump.h"
#include "libmscore/repeat.h"
#include "libmscore/hairpin.h"
#include "libmscore/ottava.h"
#include "libmscore/pedal.h"
#include "libmscore/slur.h"
#include "libmscore/textline.h"
#include "libmscore/trill.h"
#include "libmscore/tempotext.h"
#include "libmscore/fingering.h"
#include "libmscore/arpeggio.h"
#include "libmscore/glissando.h"
#include "libmscore/chordline.h"
#include "libmscore/page.h"
#include "libmscore/undo.h"
#include "fotomode.h"
#include "preferences.h"
#include "scoreview.h"
#include "scoreaccessibility.h"
#include "musescore.h"
#include "libmscore/utils.h"
#include "libmscore/instrtemplate.h"
#include "libmscore/part.h"
#include "libmscore/barline.h"
#include "seq.h"
#include "palette/palettetree.h"
#include "palette.h"


namespace ODLA {

using namespace Ms;


ODLADriver::ODLADriver(QObject *parent) : QObject(parent)
{
    _localSocket = new QLocalSocket(this);
    _currentScore = nullptr;
    _scoreView = nullptr;
    _untitledPrefix = "Untitled_";
    _editingChord = false;

    QTimer *reconnectTimer = new QTimer(this);
    reconnectTimer->setInterval(2000);

    connect(_localSocket, &QLocalSocket::connected, this, &ODLADriver::onConnected);
    connect(_localSocket, &QLocalSocket::readyRead, this, &ODLADriver::onIncomingData);

    connect(reconnectTimer, &QTimer::timeout, this, &ODLADriver::init);
    connect(_localSocket, &QLocalSocket::connected, reconnectTimer, &QTimer::stop);
    connect(_localSocket, &QLocalSocket::disconnected, reconnectTimer, static_cast<void (QTimer::*)()> (&QTimer::start));
}

void ODLADriver::init()
{
    if (_localSocket != nullptr)
    {
        _localSocket->connectToServer("ODLA_MSCORE_SERVER", QIODevice::ReadWrite);

        if(!_localSocket->waitForConnected(2000))
        {
            emit _localSocket->disconnected();
        }

        qDebug() << "Connected to server";
    }
}

void ODLADriver::onConnected()
{
    qDebug() << "Connected to ODLA server";
}

void ODLADriver::onIncomingData()
{
    Ms::MuseScore* _museScore = qobject_cast<Ms::MuseScore*>(this->parent());
    _museScore->setWindowState( (_museScore->windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    _museScore->raise();
    _museScore->activateWindow();
    bool escape = false;
    while(_localSocket->canReadLine())
    {
        QString msg = QString::fromUtf8(_localSocket->readLine());
        msg = msg.trimmed();

        if (MScore::debugMode)
            qDebug() << "Command From ODLA: " << msg;

        if (_currentScore && _scoreView)
        {
            if (msg.startsWith("^"))
            {
                _museScore->changeState(ScoreState::STATE_NOTE_ENTRY);
                msg.remove(0,1);
            }
            if (msg.startsWith("!"))
            {
                escape = true;
                msg.remove(0,1);
            }

            if (msg.startsWith("cs:"))
            {
                QString cmd = msg.split(":").last();
                _currentScore->startCmd();
                _currentScore->cmd(getAction(cmd.toStdString().data()), _scoreView->getEditData());
                _currentScore->endCmd();
            }

            else if (msg.startsWith("sv:"))
            {
                QString cmd = msg.split(":").last();
                _currentScore->startCmd();
                _scoreView->cmd(getAction(cmd.toStdString().data()));
                _currentScore->endCmd();
            }

            else if (msg.startsWith("ms:"))
            {
                QString cmd = msg.split(":").last();
                _museScore->cmd(getAction(cmd.toStdString().data()),cmd);
            }

            else if (msg.startsWith("STATUS"))
            {
                ScoreAccessibility::instance()->updateAccessibilityInfo();
                QString status = QString("STATUS REPLY:%1\n").arg(ScoreAccessibility::statusBarString);
                int written = _localSocket->write(status.toUtf8(), status.length());
                if (written < 0)
                    qDebug() << "ODLA driver: could not reply to status request.";
                _localSocket->flush();
            }

            else if (msg.compare("NEWSCORE") == 0)
            {
                _museScore->cmd(getAction("file-new"), "file-new");

                // get out of note entry mode
                _currentScore->startCmd();
                _scoreView->cmd(getAction("note-input"));
                _currentScore->endCmd();
                // force scoreView's state machine to process state transitions
                QCoreApplication::processEvents();

                // get into note entry mode again
                _currentScore->startCmd();
                _currentScore->inputState().setNoteEntryMethod(NoteEntryMethod::STEPTIME);
                _scoreView->cmd(getAction("note-input"));
                _currentScore->inputState().setNoteEntryMode(true);
                _currentScore->endCmd();
                // force scoreView's state machine to process state transitions
                QCoreApplication::processEvents();
            }

            else if (msg.compare("OPEN") == 0)
            {
                _museScore->cmd(getAction("file-open"), "file-open");
            }

            else if (msg.compare("CLOSE") == 0)
            {
                _museScore->cmd(getAction("file-close"), "file-close");
            }

            else if (msg.compare("QUIT") == 0)
            {
                _museScore->cmd(getAction("quit"), "quit");
            }

            else if (msg.compare("SAVEAS") == 0)
            {
                _museScore->cmd(getAction("file-save-as"), "file-save-as");
            }

            else if (msg.compare("SAVE") == 0)
            {
                _museScore->cmd(getAction("file-save"), "file-save");
            }

            else if (msg.compare("EXPORT") == 0)
            {
                _museScore->cmd(getAction("file-export"), "file-export");

            }

            else if (msg.compare("PRINT") == 0)
            {
                _museScore->cmd(getAction("print"), "print");
            }

            else if (msg.startsWith("PAGE_FORMAT"))
            {
                _museScore->cmd(getAction("page-settings"), "page-settings");
            }

            else if (msg.compare("INSTRUMENTS") == 0)
            {
                _museScore->cmd(getAction("instruments"), "instruments");
            }

            else if (msg.compare("CONCERT_PITCH") == 0)
            {
                // toggle concert-pitch
                _currentScore->startCmd();
                bool cp = ! (_currentScore->styleB(Sid::concertPitch));
                _currentScore->style().set(Sid::concertPitch, cp);
                _currentScore->endCmd();

                getAction("concert-pitch")->setChecked(cp);
            }

            else if (msg.startsWith("INSERT_MEASURES"))
            {
                bool ok;
                int measures = msg.split(":").last().toInt(&ok);
                if(ok)
                {
                    _scoreView->cmdInsertMeasures(measures, ElementType::MEASURE);
                    QString msg("Inserted %1 measures");
                }
            }

            else if (msg.compare("PANELPLAY") == 0)
            {
                QAction* a = getAction("toggle-playpanel");
                a->toggle();
                _museScore->cmd(a, "toggle-playpanel");
            }

            else if (msg.compare("MIXER") == 0)
            {
                QAction* a = getAction("toggle-mixer");
                a->toggle();
                _museScore->cmd(a, "toggle-mixer");
            }

            else if (msg.compare("LOOP") == 0)
            {
                QAction* a = getAction("loop");
                a->toggle();
                _museScore->cmd(a, "loop");
            }

            else if (msg.compare("ADVSEL") == 0)
            {
                QAction* a = getAction("toggle-selection-window");
                a->toggle();
                _museScore->cmd(a, "toggle-selection-window");
            }

            else if (msg.compare("ZOOMIN") == 0)
            {
                _museScore->cmd(getAction("zoomin"), "zoomin");
            }

            else if (msg.compare("ZOOMOUT") == 0)
            {
                _museScore->cmd(getAction("zoomout"), "zoomout");
            }

            else if (msg.compare("TRANSPOSE") == 0)
            {
                _museScore->cmd(getAction("transpose"), "transpose");
            }

            else if (msg.compare("EXPLODE") == 0)
            {
                // force note input OFF
                setNoteEntryMode(false);
                _currentScore->cmd(getAction("explode"), _scoreView->getEditData());
            }

            else if (msg.compare("IMPLODE") == 0)
            {
                // force note input OFF
                setNoteEntryMode(false);
                _currentScore->cmd(getAction("implode"), _scoreView->getEditData());
            }

            else if (msg.compare("FILL_SLASHES") == 0)
            {
                _currentScore->cmd(getAction("slash-fill"), _scoreView->getEditData());
            }

            else if (msg.compare("SLASHES") == 0)
            {
                // force note input OFF
                setNoteEntryMode(false);
                _currentScore->cmd(getAction("slash-rhythm"), _scoreView->getEditData());
            }

            else if (msg.compare("TRANSPORT") == 0)
            {
                // force note input OFF
                setNoteEntryMode(false);
                _museScore->cmd(getAction("toggle-transport"), "toggle-transport");
            }

            else if (msg.compare("IMAGE_MODE") == 0)
            {
                // store current position in screen coordinates
                ChordRest* cr = _currentScore->inputState().cr();
                QPointF crPagePos;
                if (cr != nullptr)
                    crPagePos = cr->pagePos();

                if (!_scoreView->fotoMode())
                {
                    // force note input OFF to allow image capture
                    setNoteEntryMode(false);
                }

                // toggle image capture
                _currentScore->startCmd();
                _scoreView->cmd(getAction("fotomode"));
                _currentScore->endCmd();

                // when image capture is on, capture full page
                if (_scoreView->fotoMode())
                {
                    Page* page = _scoreView->point2page(crPagePos);
                    if (page != nullptr)
                    {
                        _scoreView->_foto->setbbox(page->tbbox().translated(page->canvasPos()));
                        _scoreView->updateGrips();

                        _scoreView->saveFotoAs(false, _scoreView->_foto->canvasBoundingRect());
                    }
                }
            }
            else if (msg.startsWith("GOTO"))
            {
                QStringList parts = msg.split(" ");
                QString target = parts.at(1);

                _currentScore->startCmd();
                if (target.compare("MEASURE") == 0)
                {
                    QString txt = parts.at(2);
                    bool ok = false;
                    int m = txt.toInt(&ok);
                    if (ok)
                    {
                        if (m <= 0)
                            m = 1;

                        _scoreView->searchMeasure(m);
                    }
                }
                else if (target.compare("START") == 0)
                {
                    _scoreView->searchMeasure(1);
                }
                else if (target.compare("END") == 0)
                {
                    int measureNumber = _currentScore->nmeasures();
                    _scoreView->searchMeasure(measureNumber);
                }
                _currentScore->endCmd();
            }

            // move cursor:
            // MOVE PREV|NEXT CHORD|MEASURE
            // MOVE UP|DOWN CHORD|STAFF
            else if (msg.startsWith("MOVE"))
            {
                QStringList parts = msg.split(" ");

                if (parts.length() >= 2)
                {
                    QString txt1 = parts.at(1);
                    QString txt2 = parts.at(2);
                    QString actionName;

                    if (txt2.compare("STAFF") == 0)
                    {
                        QString stfdir = (txt1.compare("UP") == 0) ? "above" : "below";
                        actionName = QString("select-staff-%1").arg(stfdir);
                    }
                    else
                    {
                        actionName = QString("%1-%2").arg(txt1.toLower(), txt2.toLower());
                    }

                    _currentScore->startCmd();
                    _scoreView->cmd(getAction(actionName.toStdString().data()));
                    _currentScore->endCmd();
                }
            }

            else if (msg.startsWith("SELECT:"))
            {
                QString rangeString = msg.split(":").last();
                int from =  rangeString.split("-").first().toInt();
                int to =    rangeString.split("-").last().toInt();

                //If we didn't insert an existing start measure break from command
                if(!_scoreView->searchMeasure(from))
                    break;

                Measure* measure_from = _currentScore->firstMeasureMM();

                for(int i = 1; i < from; i++)
                    measure_from = measure_from->nextMeasure();

                Measure* measure_to = measure_from;

                //If we didn't insert an existing end measure we set last measure
                if(_scoreView->searchMeasure(to))
                    for(int i = from; i < to; i++)
                       measure_to = measure_to->nextMeasure();
                else
                    measure_to = _currentScore->lastMeasureMM();

                _museScore->changeState(ScoreState::STATE_NOTE_ENTRY);

                _currentScore->deselectAll();
                _currentScore->selectRange(measure_from, 0);
                _currentScore->selectRange(measure_to, _currentScore->nstaves() - 1);
                _currentScore->setUpdateAll();
                _currentScore->update();

                if (MScore::debugMode)
                    qDebug() << "selecting from " << from << "to " << to;
            }

            // copy selection
            else if (msg.startsWith("COPY"))
            {
                if (_scoreView)
                {
                    if (_scoreView->noteEntryMode())
                    {
                        _currentScore->startCmd();
                        _currentScore->inputState().setNoteEntryMethod(NoteEntryMethod::STEPTIME);
                        _scoreView->cmd(getAction("note-input"));

                        // force scoreView's state machine to process state transitions
                        QCoreApplication::processEvents();

                        _currentScore->inputState().setNoteEntryMode(false);
                        _currentScore->endCmd();
                    }

                    _scoreView->normalCopy();
                }
            }

            // paste selection
            else if (msg.startsWith("PASTE"))
            {
                if (_scoreView)
                {
                    if (_scoreView->noteEntryMode())
                    {
                        _currentScore->startCmd();
                        _currentScore->inputState().setNoteEntryMethod(NoteEntryMethod::STEPTIME);
                        _scoreView->cmd(getAction("note-input"));

                        // force scoreView's state machine to process state transitions
                        QCoreApplication::processEvents();

                        _currentScore->inputState().setNoteEntryMode(false);
                        _currentScore->endCmd();
                    }

                    _scoreView->normalPaste();
                }
            }

            // repeat selection
            else if (msg.startsWith("REPEAT"))
            {
                if (_scoreView)
                    _scoreView->cmd(getAction("repeat-sel"));
            }

            // delete selection
            else if (msg.startsWith("DELETE"))
            {
                if (_scoreView)
                {
                    // disable note entry to allow deletion
                    TDuration currentDuration = _currentScore->inputState().duration();
                    if (_scoreView->noteEntryMode())
                    {
                        _currentScore->startCmd();
                        _scoreView->cmd(getAction("note-input"));
                        _currentScore->endCmd();
                    }

                    _currentScore->startCmd();
                    _scoreView->cmd(getAction("delete"));
                    _currentScore->endCmd();

                    // enable again
                    if (_scoreView->noteEntryMode())
                    {
                        _currentScore->startCmd();
                        _currentScore->inputState().setNoteEntryMethod(NoteEntryMethod::STEPTIME);
                        _scoreView->cmd(getAction("note-input"));
                        _currentScore->inputState().setDuration(currentDuration);
                        _currentScore->inputState().setNoteEntryMode(true);
                        _currentScore->endCmd();
                    }
                }
            }
            else if (msg.startsWith("TIME-DELETE"))
            {
                if (_scoreView)
                {
                    // disable note entry to allow deletion
                    TDuration currentDuration = _currentScore->inputState().duration();
                    if (_scoreView->noteEntryMode())
                    {
                        _currentScore->startCmd();
                        _scoreView->cmd(getAction("note-input"));
                        _currentScore->endCmd();
                    }

                    _currentScore->startCmd();
                    _scoreView->cmd(getAction("time-delete"));
                    _currentScore->endCmd();

                    // enable again
                    if (_scoreView->noteEntryMode())
                    {
                        _currentScore->startCmd();
                        _currentScore->inputState().setNoteEntryMethod(NoteEntryMethod::STEPTIME);
                        _scoreView->cmd(getAction("note-input"));
                        _currentScore->inputState().setDuration(currentDuration);
                        _currentScore->inputState().setNoteEntryMode(true);
                        _currentScore->endCmd();
                    }
                }
            }

            // undo
            else if (msg.startsWith("UNDO"))
            {
                UndoStack* optStack = _currentScore->undoStack();

                if (optStack && optStack->canUndo())
                    _currentScore->undoRedo(true, nullptr);
            }

            // redo
            else if (msg.startsWith("REDO"))
            {
                UndoStack* optStack = _currentScore->undoStack();

                if (optStack && optStack->canRedo())
                    _currentScore->undoRedo(false, nullptr);
            }

            // put note
            else if (msg.startsWith("STFLN"))
            {
                if (_currentScore->inputState().noteEntryMethod() == NoteEntryMethod::STEPTIME &&
                        _currentScore->inputState().noteEntryMode())
                {
                    QStringList parts = msg.split(" ");

                    bool ok = false;
                    int line = parts.at(1).toInt(&ok);

                    // check chord option
                    bool keepchord = false;
                    if (parts.length() > 2)
                    {
                        QString opt = parts.at(2);
                        if (opt.compare("CHORD") == 0)
                            keepchord = true;
                    }

                    // check slur option
                    bool slur = false;
                    if (parts.length() > 3)
                    {
                        QString opt = parts.at(3);
                        if (opt.compare("SLUR") == 0)
                            slur = true;
                    }

                    // disable slur
                    if (!slur)
                    {
                        _currentScore->inputState().setSlur(nullptr);
                    }

                    if (ok)
                    {
                        Segment* segment = _currentScore->inputState().segment();
                        if (segment != nullptr)
                        {
                            Fraction tick = _currentScore->inputState().tick();
                            Staff* currentStaff = _currentScore->staff(_currentScore->inputState().track() / VOICES);
                            ClefType clef = currentStaff->clef(tick);

                            _currentScore->startCmd();
                            if (_editingChord == false && keepchord &&
                                    (_currentScore->inputState().segment() !=
                                    _currentScore->inputState().lastSegment()))
                            {
                                // move to next position and start editing a new chord
                                _scoreView->cmd(getAction("next-chord"));
                            }
                            _currentScore->cmdAddPitch(relStep(line, clef), keepchord, false);
                            _currentScore->endCmd();

                            // reset flag when ODLA's Chord key is released
                            _editingChord = keepchord;

                            // add slur if not already slurring...
                            if (slur && _currentScore->inputState().slur() == nullptr)
                            {
                                _scoreView->cmd(getAction("add-slur"));
                            }
                        }
                    }
                }
            }

            // add rest with current duration
            else if (msg.startsWith("REST"))
            {
                if (_currentScore->inputState().noteEntryMethod() == NoteEntryMethod::STEPTIME &&
                        _currentScore->inputState().noteEntryMode())
                {
                    _currentScore->cmdEnterRest(_currentScore->inputState().duration());
                }
            }

            else if (msg.startsWith("DURATION"))
            {
                QStringList parts = msg.split(" ");
                QString txt1 = parts.at(1);
                int denominator = txt1.toInt();

                QString padCmd;
                switch (denominator) {
                    case 0: padCmd = QString("note-breve"); break;
                    case 1: padCmd = QString("pad-note-1"); break;
                    case 2: padCmd = QString("pad-note-2"); break;
                    case 4: padCmd = QString("pad-note-4"); break;
                    case 8: padCmd = QString("pad-note-8"); break;
                    case 16: padCmd = QString("pad-note-16"); break;
                    case 32: padCmd = QString("pad-note-32"); break;
                    case 64: padCmd = QString("pad-note-64"); break;
                    default:
                        padCmd = QString("pad-note-4"); break;
                        break;
                }

                _scoreView->cmd(getAction(padCmd.toStdString().data()));
            }

            else if (msg.startsWith("DOTS"))
            {
                QStringList parts = msg.split(" ");
                QString txt1 = parts.at(1);
                int dots = txt1.toInt();

                QString dotCmd;
                switch (dots) {
                    case 1: dotCmd = QString("pad-dot"); break;
                    case 2: dotCmd = QString("pad-dotdot"); break;
                    case 3: dotCmd = QString("pad-dot3"); break;
                }

                if (!dotCmd.isEmpty())
                {
                    _scoreView->cmd(getAction(dotCmd.toStdString().data()));
                }
            }

            else if (msg.startsWith("ACCIDENTAL"))
            {
                QStringList parts = msg.split(" ");
                QString txt1 = parts.at(1);
                AccidentalType at = AccidentalType::NONE;
                if (txt1.compare("NONE") == 0)         { at = AccidentalType::NONE; }
                else if (txt1.compare("FLAT")    == 0) { at = AccidentalType::FLAT; }
                else if (txt1.compare("SHARP")   == 0) { at = AccidentalType::SHARP; }
                else if (txt1.compare("FLAT2")   == 0) { at = AccidentalType::FLAT2;}
                else if (txt1.compare("SHARP2")  == 0) { at = AccidentalType::SHARP2; }
                else if (txt1.compare("NATURAL") == 0) { at = AccidentalType::NATURAL; }

                bool brackets = false;
                if (parts.length() > 2)
                {
                    QString txt2 = parts.at(2);
                    brackets = (txt2.compare("BRK") == 0);
                }

                _currentScore->startCmd();
                _currentScore->changeAccidental(at);
                _currentScore->endCmd();

                _currentScore->startCmd();
                if (brackets)
                {
                    for (Element* el : _currentScore->selection().elements())
                    {
                        if (el->type() == ElementType::NOTE)
                        {
                            Note* n = toNote(el);
                            Accidental* acc =  n->accidental();
                            if (acc != nullptr)
                            {
                                _currentScore->addRefresh(acc->canvasBoundingRect());
                                acc->undoChangeProperty(Pid::ACCIDENTAL_BRACKET, int(AccidentalBracket::PARENTHESIS), PropertyFlags::NOSTYLE);
                            }
                        }
                    }
                }
                _currentScore->endCmd();
            }

            else if (msg.startsWith("DYNAMICS"))
            {
                QStringList parts = msg.split(" ");

                if (parts.length() > 1)
                {
                    QString dynText = parts.at(1);

                    ChordRest* current = _currentScore->inputState().cr();
                    if (current != nullptr)
                    {
                        Dynamic* dyn = new Dynamic(_currentScore);
                        dyn->setDynamicType(dynText.toLower());
                        dyn->setPlacement(Placement::BELOW);

                        EditData& dropData = _scoreView->getEditData();
                        dropData.pos         = current->pagePos();
                        dropData.dragOffset  = QPointF();
                        dropData.dropElement = dyn;

                        _currentScore->startCmd();
                        current->drop(dropData);
                        _currentScore->endCmd();
                    }
                }
            }

            else if (msg.startsWith("FINGERING"))
            {
                bool parsed = false;
                QStringList parts = msg.split(" ");

                if (parts.length() > 1)
                {
                    Tid style = Tid::DEFAULT;
                    FrameType ft = FrameType::NO_FRAME;
                    QString txt1 = parts.at(1);
                    QString xmlText;

                    // pimac
                    if (txt1.length() == 1 && parts.count() == 2)
                    {
                        QString pimac("PIMAC");
                        if (pimac.contains(txt1))
                        {
                            xmlText = txt1.toLower();
                            parsed = true;
                        }
                    }
                    else if (parts.length() > 2)
                    {
                        // L-R fingers / strings
                        QString txt2 = parts.at(2);
                        xmlText = txt2;
                        // test number conversion
                        txt2.toInt(&parsed);

                        if (txt1.compare("RH") == 0)            { style = Tid::RH_GUITAR_FINGERING; }
                        else if (txt1.compare("LH") == 0)       { style = Tid::LH_GUITAR_FINGERING; }
                        else if (txt1.compare("STRING") == 0)   { style = Tid::STRING_NUMBER; ft = FrameType::CIRCLE; }
                        else { parsed = false; }
                    }

                    if (parsed)
                    {
                        Fingering* fin = new Fingering(_currentScore);
                        fin->setTid(style);
                        fin->setFrameType(ft);
                        fin->setXmlText(xmlText);
                        fin->setTrack(_currentScore->inputState().track() / VOICES);

                        Element* el = _currentScore->selection().element();
                        if (el != nullptr && el->isNote())
                        {
                            EditData dropData(nullptr);
                            dropData.pos          = el->pagePos();
                            dropData.dropElement  = fin;

                            _currentScore->startCmd();
                            el->drop(dropData);
                            _currentScore->endCmd();
                        }
                    }
                }
            }

            else if (msg.startsWith("ARPEGGIO"))
            {
                QStringList parts = msg.split(" ");

                if (parts.length() > 1)
                {
                    Segment* seg = _currentScore->inputState().segment();
                    ChordRest* cr = _currentScore->inputState().cr();
                    if (seg != nullptr && cr != nullptr)
                    {
                        Element* e = seg->element(_currentScore->inputState().track() + cr->voice());
                        // check a note exists at current position
                        if (e != nullptr && e->isChord())
                        {
                            bool parsed = true;
                            QString txt = parts.at(1);
                            ArpeggioType at = ArpeggioType::BRACKET;

                            if (txt.compare("WAVE") == 0)               { at = ArpeggioType::NORMAL; }
                            else if (txt.compare("WAVE_UP") == 0)       { at = ArpeggioType::UP; }
                            else if (txt.compare("WAVE_DOWN") == 0)     { at = ArpeggioType::DOWN; }
                            else if (txt.compare("SQUARE") == 0)        { at = ArpeggioType::BRACKET; }
                            else if (txt.compare("ARROW_UP") == 0)      { at = ArpeggioType::UP_STRAIGHT; }
                            else if (txt.compare("ARROW_DOWN") == 0)    { at = ArpeggioType::DOWN_STRAIGHT; }
                            else { parsed = false; }

                            if (parsed)
                            {
                                Arpeggio* arp = new Arpeggio(_currentScore);
                                arp->setArpeggioType(at);
                                arp->setTrack(_currentScore->inputState().track() / VOICES);
                                arp->setParent(_currentScore->inputState().cr());

                                _currentScore->startCmd();
                                _currentScore->undoAddElement(arp);
                                _currentScore->endCmd();
                            }
                        }
                    }
                }
            }

            else if (msg.startsWith("GLISSANDO"))
            {
                QStringList parts = msg.split(" ");

                if (parts.length() > 1)
                {
                    bool parsed = true;
                    QString txt = parts.at(1);
                    GlissandoType gt = GlissandoType::STRAIGHT;

                    if (txt.compare("LINE") == 0)       { gt = GlissandoType::STRAIGHT; }
                    else if (txt.compare("WAVE") == 0)  { gt = GlissandoType::WAVY; }
                    else { parsed = false; }

                    if (parsed)
                    {
                        Glissando* spanner = static_cast<Glissando*>(Element::create(ElementType::GLISSANDO, _currentScore));
                        spanner->setGlissandoType(gt);

                        Element* el = _currentScore->selection().element();
                        if (el != nullptr && el->isNote())
                        {
                            EditData dropData(nullptr);
                            dropData.pos          = el->pagePos();
                            dropData.dropElement  = spanner;

                            _currentScore->startCmd();
                            el->drop(dropData);
                            _currentScore->endCmd();
                        }
                    }
                 }
            }

            else if (msg.startsWith("CHORDLINE"))
            {
                if(!_currentScore->inputState().cr())
                    break;

                QStringList parts = msg.split(" ");

                if (parts.length() > 1)
                {
                    bool parsed = true;
                    QString txt = parts.at(1);
                    ChordLineType ct = ChordLineType::FALL;
                    bool straight = false;

                    if (txt.compare("FALL") == 0)           { ct = ChordLineType::FALL; }
                    else if (txt.compare("DOIT") == 0)      { ct = ChordLineType::DOIT; }
                    else if (txt.compare("PLOP") == 0)      { ct = ChordLineType::PLOP; }
                    else if (txt.compare("SCOOP") == 0)     { ct = ChordLineType::SCOOP; }
                    else if (txt.compare("OUT_DOWN") == 0)  { ct = ChordLineType::FALL; straight = true; }
                    else if (txt.compare("OUT_UP") == 0)    { ct = ChordLineType::DOIT; straight = true; }
                    else if (txt.compare("IN_ABOVE") == 0)  { ct = ChordLineType::PLOP; straight = true; }
                    else if (txt.compare("IN_BELOW") == 0)  { ct = ChordLineType::SCOOP; straight = true; }
                    else { parsed = false; }

                    if (parsed)
                    {
                        Element* el = _currentScore->selection().element();
                        if (el != nullptr && el->isNote())
                        {
                            ChordLine* chline = new ChordLine(_currentScore);
                            chline->setChordLineType(ct);
                            chline->setStraight(straight);
                            chline->setTrack(_currentScore->inputState().track() / VOICES);
                            chline->setParent(_currentScore->inputState().cr());

                            _currentScore->startCmd();
                            _currentScore->undoAddElement(chline);
                            _currentScore->endCmd();
                        }
                    }
                }
            }

            else if (msg.startsWith("BREATH"))
            {
                QStringList parts = msg.split(" ");

                if (parts.length() > 1)
                {
                    bool parsed = true;
                    QString txt = parts.at(1);
                    SymId bt = SymId::noSym;
                    qreal pause = 0;

                    if (txt.compare("COMMA") == 0) { bt = SymId::breathMarkComma; }
                    else if (txt.compare("TICK") == 0) { bt = SymId::breathMarkTick; }
                    else if (txt.compare("TRILL") == 0) { bt = SymId::breathMarkSalzedo; }
                    else if (txt.compare("UP_BOW") == 0) { bt = SymId::breathMarkUpbow; }
                    else if (txt.compare("CAESURA") == 0) { bt = SymId::caesura; pause = 2; }
                    else if (txt.compare("CAESURA_SHORT") == 0) { bt = SymId::caesuraShort; pause = 2; }
                    else if (txt.compare("CAESURA_THICK") == 0) { bt = SymId::caesuraThick; pause = 2; }
                    else { parsed = false; }

                    if (parsed)
                    {
                        Fraction tick = _currentScore->inputState().tick();
                        Measure* m = _currentScore->tick2measure(tick);
                        Segment* seg = m->undoGetSegment(SegmentType::ChordRest, tick);

                        // add breath or pause
                        Breath* b = new Breath(_currentScore);
                        b->setSymId(bt);
                        b->setPause(pause);
                        b->setTrack(_currentScore->inputState().track() / VOICES);
                        b->setParent(seg);
                        b->layout();

                        _currentScore->startCmd();
                        _currentScore->undoAddElement(b);
                        _currentScore->endCmd();
                    }
                }
            }


            else if (msg.startsWith("NOTEHEAD"))
            {
                QStringList parts = msg.split(" ");

                if (parts.length() > 1)
                {
                    bool supported = true;
                    QString txt = parts.at(1);
                    SymId nt = SymId::noSym;

                    if (txt.compare("X_CIRCLE") == 0) { nt = SymId::noteheadCircleXHalf; }
                    else if (txt.compare("TRIANGLE_UP") == 0) { nt = SymId::noteheadTriangleUpHalf; }
                    else if (txt.compare("TRIANGLE_DOWN") == 0) { nt = SymId::noteheadTriangleDownHalf; }
                    else if (txt.compare("SLASHED_FORWARD") == 0) { nt = SymId::noteheadSlashedHalf1; }
                    else if (txt.compare("MI") == 0) { nt = SymId::noteShapeDiamondWhite; }
                    else if (txt.compare("PARENTHESIS") == 0) { nt = SymId::noteheadParenthesis; }
                    else
                    {
                        supported = false;
                    }

                    if (supported)
                    {
                        _currentScore->startCmd();

                        // add parenthesis
                        if (nt == SymId::noteheadParenthesis)
                        {
                            _currentScore->cmdAddParentheses();
                        }
                        else
                        {
                            // simulate drag&drop on note elements
                            // to set the note head
                            Selection sel = _currentScore->selection();
                            if (sel.isNone())
                                  return;

                            for (Element* target : sel.elements())
                            {
                                NoteHead* notehead = new NoteHead(_currentScore);
                                notehead->setSym(nt);
                                EditData& dropData = _scoreView->getEditData();
                                dropData.pos         = target->pagePos();
                                dropData.dragOffset  = QPointF();
                                dropData.dropElement = notehead;

                                if (target->acceptDrop(dropData))
                                {
                                      Element* el = target->drop(dropData);
                                      if (el && !_scoreView->noteEntryMode())
                                            _currentScore->select(el, SelectType::SINGLE, 0);

                                      dropData.dropElement = 0;
                                }
                            }
                        }

                        _currentScore->endCmd();
                    }
                }
            }

            else if (msg.startsWith("CLEF"))
            {
                if(!_currentScore->inputState().cr())
                    break;

                QString txt = msg.split(" ").at(1);
                ClefType ct = ClefType::INVALID;
                if (txt.compare("NONE") == 0)        { ct = ClefType::INVALID; }
                else if (txt.compare("TREBLE") == 0) { ct = ClefType::G; }
                else if (txt.compare("TREBLE_8VA_ALTA") == 0) { ct = ClefType::G8_VA; }
                else if (txt.compare("TREBLE_15MA_ALTA") == 0) { ct = ClefType::G15_MA; }
                else if (txt.compare("TREBLE_8VA_BASSA") == 0) { ct = ClefType::G8_VB; }
                else if (txt.compare("TREBLE_15MA_BASSA") == 0) { ct = ClefType::G15_MB; }
                else if (txt.compare("SOPRANO") == 0) { ct = ClefType::C1; }
                else if (txt.compare("MEZZO_SOPRANO") == 0) { ct = ClefType::C2; }
                else if (txt.compare("ALTO") == 0) { ct = ClefType::C3; }
                else if (txt.compare("TENOR") == 0) { ct = ClefType::C4; }
                else if (txt.compare("BARITONE") == 0) { ct = ClefType::C5; }
                else if (txt.compare("BASS")   == 0) { ct = ClefType::F; }
                else if (txt.compare("BASS_8VA_ALTA") == 0) { ct = ClefType::F_8VA; }
                else if (txt.compare("BASS_15MA_ALTA") == 0) { ct = ClefType::F_15MA; }
                else if (txt.compare("BASS_8VA_BASSA") == 0) { ct = ClefType::F8_VB; }
                else if (txt.compare("BASS_15MA_BASSA") == 0) { ct = ClefType::F15_MB; }

                Measure* first = static_cast<Measure*>(_currentScore->measure(1));
                Measure* currentMeasure = _currentScore->inputState().segment()->measure();

                if (currentMeasure == first)
                {
                    int currentStaff = _currentScore->inputState().track() / VOICES;
                    _currentScore->startCmd();
                    _currentScore->undoChangeClef(_currentScore->staff(currentStaff), _currentScore->inputState().segment(), ct);
                    _currentScore->endCmd();
                }
                else
                {
                    // make a temporary clef, deleted by Score::cmdInsertClef(...);
                    Clef* tmpClef = new Clef(_currentScore);
                    tmpClef->setClefType(ct);

                    _currentScore->startCmd();
                    _currentScore->cmdInsertClef(tmpClef, _currentScore->inputState().cr());
                    _currentScore->endCmd();
                }
            }

            else if (msg.startsWith("LINEVIEW"))
            {
                _museScore->switchLayoutMode(LayoutMode::LINE);
            }

            else if (msg.startsWith("PAGEVIEW"))
            {
                _museScore->switchLayoutMode(LayoutMode::PAGE);
            }

            else if (msg.startsWith("BARS"))
            {
                if(!_currentScore->inputState().cr())
                    break;

                QString txt = msg.split(" ").at(1);
                BarLineType bt = BarLineType::NORMAL;
                int from = -1;
                int to = -1;

                if (txt.compare("NORMAL") == 0)             { bt = BarLineType::NORMAL; }
                else if (txt.compare("DOUBLE") == 0)        { bt = BarLineType::DOUBLE; }
                else if (txt.compare("R_START") == 0)       { bt = BarLineType::START_REPEAT; }
                else if (txt.compare("R_STOP") == 0)        { bt = BarLineType::END_REPEAT; }
                else if (txt.compare("R_START_STOP") == 0)  { bt = BarLineType::END_START_REPEAT; }
                else if (txt.compare("FINAL") == 0)         { bt = BarLineType::END; }
                else if (txt.compare("BROKEN") == 0)        { bt = BarLineType::BROKEN; }
                else if (txt.compare("DOTTED") == 0)        { bt = BarLineType::DOTTED; }
                else if (txt.compare("TICK_2_SPAN") == 0)   { bt = BarLineType::NORMAL; from = BARLINE_SPAN_TICK2_FROM; to = BARLINE_SPAN_TICK2_TO; }
                else if (txt.compare("SHORT_2_SPAN") == 0)  { bt = BarLineType::NORMAL; from = BARLINE_SPAN_SHORT2_FROM; to = BARLINE_SPAN_SHORT2_TO; }

                // simulate drag&drop on selected measures
                Selection sel = _currentScore->selection();
                if (!sel.isNone())
                {
                    _currentScore->startCmd();

                    Measure* current = _currentScore->inputState().cr()->measure();
                    Measure* last = nullptr;

                    if (sel.isRange())
                    {
                        current = sel.startSegment() ? sel.startSegment()->measure() : nullptr;
                        last = sel.endSegment() ? sel.endSegment()->measure() : nullptr;
                    }

                    while (current != last)
                    {
                        BarLine* barline = static_cast<BarLine*>(Element::create(ElementType::BAR_LINE, _currentScore));
                        barline->setBarLineType(bt);

                        if (from != -1 && to != -1)
                        {
                            barline->setSpanFrom(from);
                            barline->setSpanTo(to);
                        }

                        EditData& dropData = _scoreView->getEditData();
                        dropData.pos         = current->pagePos();
                        dropData.dragOffset  = QPointF();
                        dropData.dropElement = barline;

                        if (current->acceptDrop(dropData))
                        {
                              current->drop(dropData);
                              dropData.dropElement = 0;
                        }

                        current = sel.isRange() ? current->nextMeasureMM() : last;
                    }

                    _currentScore->endCmd();
                    _scoreView->setDropTarget(0);
                }
            }

            else if (msg.startsWith("REP_MEASURE"))
            {
                // simulate drag&drop on selected measures
                Selection sel = _currentScore->selection();
                if (sel.isNone())
                      return;

                for (Element* target : sel.elements())
                {
                    RepeatMeasure* rmeasure = new RepeatMeasure(_currentScore);
                    EditData& dropData = _scoreView->getEditData();
                    dropData.pos         = target->pagePos();
                    dropData.dragOffset  = QPointF();
                    dropData.dropElement = rmeasure;

                    if (target->acceptDrop(dropData))
                    {
                          Element* el = target->drop(dropData);
                          if (el && !_scoreView->noteEntryMode())
                                _currentScore->select(el, SelectType::SINGLE, 0);

                          dropData.dropElement = 0;
                    }
                }
                _currentScore->endCmd();
                _scoreView->setDropTarget(0);
                _currentScore->inputState().moveToNextInputPos();
            }


            else if (msg.startsWith("MARKER"))
            {
                if(!_currentScore->inputState().cr())
                    break;

                QString txt = msg.split(" ").at(1);
                Marker::Type mt = Marker::Type::USER;

                if (txt.compare("SEGNO") == 0)              { mt = Marker::Type::SEGNO; }
                else if (txt.compare("SEGNO_VAR") == 0)     { mt = Marker::Type::VARSEGNO; }
                else if (txt.compare("CODA") == 0)          { mt = Marker::Type::CODA; }
                else if (txt.compare("CODA_VAR") == 0)      { mt = Marker::Type::VARCODA; }
                else if (txt.compare("FINE") == 0)          { mt = Marker::Type::FINE; }
                else if (txt.compare("TO_CODA") == 0)       { mt = Marker::Type::TOCODA; }

                // simulate drag&drop on selected measures
                Selection sel = _currentScore->selection();
                if (!sel.isNone())
                {
                    _currentScore->startCmd();

                    Measure* current = _currentScore->inputState().cr()->measure();
                    Measure* last = nullptr;

                    if (sel.isRange())
                    {
                        current = sel.startSegment() ? sel.startSegment()->measure() : nullptr;
                        last = sel.endSegment() ? sel.endSegment()->measure() : nullptr;
                    }

                    while (current != last)
                    {
                        Marker* marker = static_cast<Marker*>(Element::create(ElementType::MARKER, _currentScore));
                        marker->setMarkerType(mt);
                        marker->styleChanged();

                        EditData& dropData = _scoreView->getEditData();
                        dropData.pos         = current->pagePos();
                        dropData.dragOffset  = QPointF();
                        dropData.dropElement = marker;

                        if (current->acceptDrop(dropData))
                        {
                              current->drop(dropData);
                              dropData.dropElement = 0;
                        }

                        current = sel.isRange() ? current->nextMeasureMM() : last;
                    }

                    _currentScore->endCmd();
                    _scoreView->setDropTarget(0);
                }
            }

            else if (msg.startsWith("JUMP"))
            {
                if(!_currentScore->inputState().cr())
                    break;

                QString txt = msg.split(" ").at(1);
                Jump::Type jt = Jump::Type::USER;

                if (txt.compare("DA_CAPO") == 0)             { jt = Jump::Type::DC; }
                else if (txt.compare("DC_AL_FINE") == 0)     { jt = Jump::Type::DC_AL_FINE; }
                else if (txt.compare("DC_AL_CODA") == 0)     { jt = Jump::Type::DC_AL_CODA; }
                else if (txt.compare("DS_AL_CODA") == 0)     { jt = Jump::Type::DS_AL_CODA; }
                else if (txt.compare("DS_AL_FINE") == 0)     { jt = Jump::Type::DS_AL_FINE; }
                else if (txt.compare("DS") == 0)             { jt = Jump::Type::DS; }

                // simulate drag&drop on selected measures
                Selection sel = _currentScore->selection();
                if (!sel.isNone())
                {
                    _currentScore->startCmd();

                    Measure* current = _currentScore->inputState().cr()->measure();
                    Measure* last = nullptr;

                    if (sel.isRange())
                    {
                        current = sel.startSegment() ? sel.startSegment()->measure() : nullptr;
                        last = sel.endSegment() ? sel.endSegment()->measure() : nullptr;
                    }

                    while (current != last)
                    {
                        Jump* jump = static_cast<Jump*>(Element::create(ElementType::JUMP, _currentScore));
                        jump->setJumpType(jt);

                        EditData& dropData = _scoreView->getEditData();
                        dropData.pos         = current->pagePos();
                        dropData.dragOffset  = QPointF();
                        dropData.dropElement = jump;

                        if (current->acceptDrop(dropData))
                        {
                              current->drop(dropData);
                              dropData.dropElement = 0;
                        }

                        current = sel.isRange() ? current->nextMeasureMM() : last;
                    }

                    _currentScore->endCmd();
                    _scoreView->setDropTarget(0);
                }
            }

            else if (msg.startsWith("VOLTA"))
            {
                QString txt = msg.split(" ").at(1);

                int volta = 0;
                bool closed = true;

                if (txt.compare("I") == 0)           { volta = 1; }
                else if (txt.compare("II") == 0)     { volta = 2; }
                else if (txt.compare("III") == 0)    { volta = 3; }
                else if (txt.compare("II_OPEN") == 0){ volta = 2; closed = false; }
                else break;

                Volta* spanner = static_cast<Volta*>(Element::create(ElementType::VOLTA, _currentScore));
                spanner->setVoltaType(closed? Volta::Type::CLOSED : Volta::Type::OPEN);
                spanner->setText(QString::number(volta) + ".");
                spanner->setEndings(QList<int>() << volta);

                _currentScore->endCmd();
                Palette::applyPaletteElement(spanner);
                _currentScore->endCmd();
            }

            else if (msg.startsWith("LINE"))
            {
                bool parsed = true;
                QString txt = msg.split(" ").at(1);
                bool slur = false;
                QString beginText;
                HookType endHook = HookType::NONE;
                bool diagonal = false;

                if (txt.compare("SLUR") == 0)      { slur = true; }
                else if (txt.compare("VII") == 0)  { beginText = "VII"; endHook = HookType::HOOK_90; }
                else if (txt.compare("LINE") == 0) { diagonal = true; }
                else { parsed = false; }

                if (parsed)
                {
                    Spanner* spanner = nullptr;
                    if (slur)
                    {
                        Slur *slur = static_cast<Slur*>(Element::create(ElementType::SLUR, _currentScore));
                        spanner = slur;
                    }
                    else
                    {
                        TextLine* line = static_cast<TextLine*>(Element::create(ElementType::TEXTLINE, _currentScore));
                        line->setBeginText(beginText);
                        line->setEndHookType(endHook);
                        line->setDiagonal(diagonal);
                        line->setLen(_currentScore->spatium() * 8);
                        spanner = line;
                    }

                    if (!addSpannerToCurrentSelection(spanner))
                        delete spanner;
                }
            }

            else if (msg.startsWith("TRILL"))
            {
                bool parsed = true;
                QString txt = msg.split(" ").at(1);
                Trill::Type tt = Trill::Type::TRILL_LINE;

                if (txt.compare("LINE") == 0)             { tt = Trill::Type::TRILL_LINE; }
                else if (txt.compare("PRALLPRALL") == 0)  { tt = Trill::Type::PRALLPRALL_LINE; }
                else { parsed = false; }

                if (parsed)
                {
                    Trill* spanner = static_cast<Trill*>(Element::create(ElementType::TRILL, _currentScore));
                    spanner->setTrillType(tt);
                    spanner->setLen(_currentScore->spatium() * 8);

                    if (!addSpannerToCurrentSelection(spanner))
                        delete spanner;
                }
            }

            else if (msg.startsWith("OTTAVA"))
            {
                bool parsed = true;
                QString txt = msg.split(" ").at(1);
                OttavaType ot = OttavaType::OTTAVA_8VA;
                Placement pl = Placement::ABOVE;

                if (txt.compare("8va_ALTA") == 0)        { ot = OttavaType::OTTAVA_8VA; }
                else if (txt.compare("8va_BASSA") == 0)  { ot = OttavaType::OTTAVA_8VB; pl = Placement::BELOW; }
                else if (txt.compare("15ma_ALTA") == 0)  { ot = OttavaType::OTTAVA_15MB; }
                else if (txt.compare("15ma_BASSA") == 0) { ot = OttavaType::OTTAVA_15MB; pl = Placement::BELOW; }
                else { parsed = false; }

                if (parsed)
                {
                    Ottava* spanner = static_cast<Ottava*>(Element::create(ElementType::OTTAVA, _currentScore));
                    spanner->setOttavaType(ot);
                    spanner->setPlacement(pl);
                    spanner->styleChanged();
                    spanner->setLen(_currentScore->spatium() * 8);

                    if (!addSpannerToCurrentSelection(spanner))
                        delete spanner;
                }
            }

            else if (msg.startsWith("PEDAL"))
            {
                bool parsed = true;
                QString txt = msg.split(" ").at(1);
                QString beginText("<sym>keyboardPedalPed</sym>");
                QString endText("<sym>keyboardPedalUp</sym>");
                QString continueText("(<sym>keyboardPedalPed</sym>)");
                HookType beginHook = HookType::HOOK_90;
                HookType endHook = HookType::HOOK_90;
                bool visibleLine = true;

                if (txt.compare("PEDAL_END90") == 0)      { endText.clear(); beginHook = HookType::NONE; }
                else if (txt.compare("PEDAL_ENDUP") == 0) { endText = "<sym>keyboardPedalUp</sym>"; visibleLine = false; }
                else if (txt.compare("PEDAL_9090") == 0)  { beginText.clear(); endText.clear(); continueText.clear(); }
                else { parsed = false; }

                if (parsed)
                {
                    Pedal* spanner = static_cast<Pedal*>(Element::create(ElementType::PEDAL, _currentScore));
                    spanner->setBeginText(beginText);
                    spanner->setEndText(endText);
                    spanner->setContinueText(continueText);
                    spanner->setBeginHookType(beginHook);
                    spanner->setEndHookType(endHook);
                    spanner->setLineVisible(visibleLine);
                    spanner->setLen(_currentScore->spatium() * 8);

                    if (!addSpannerToCurrentSelection(spanner))
                        delete spanner;
                }

            }

            else if (msg.startsWith("HAIRPIN"))
            {
                bool parsed = true;
                QString txt = msg.split(" ").at(1);
                HairpinType ht = HairpinType::INVALID;
                bool dynamic = false;

                if (txt.compare("CRESC_LINE") == 0)          { ht = HairpinType::CRESC_LINE; }
                else if (txt.compare("DIM_LINE") == 0)       { ht = HairpinType::DECRESC_LINE; }
                else if (txt.compare("CRESC_HAIRPIN") == 0)  { ht = HairpinType::CRESC_HAIRPIN; }
                else if (txt.compare("DIM_HAIRPIN") == 0)    { ht = HairpinType::DECRESC_HAIRPIN; }
                else if (txt.compare("DYNAMIC_HAIRPIN") == 0)  { ht = HairpinType::CRESC_HAIRPIN; dynamic = true; }
                else { parsed = false; }

                if (parsed)
                {
                    Hairpin* spanner = static_cast<Hairpin*>(Element::create(ElementType::HAIRPIN, _currentScore));
                    spanner->setHairpinType(ht);
                    spanner->setLen(_currentScore->spatium() * 8);

                    if (dynamic)
                    {
                        spanner->setHairpinType(HairpinType::CRESC_HAIRPIN);
                        spanner->setBeginText("<sym>dynamicMezzo</sym><sym>dynamicForte</sym>");
                        spanner->setPropertyFlags(Pid::BEGIN_TEXT, PropertyFlags::UNSTYLED);
                        spanner->setBeginTextAlign(Align::VCENTER);
                        spanner->setPropertyFlags(Pid::BEGIN_TEXT_ALIGN, PropertyFlags::UNSTYLED);
                    }

                    if (!addSpannerToCurrentSelection(spanner))
                        delete spanner;
                }
            }

            else if (msg.startsWith("TEMPO"))
            {
                _museScore->changeState(ScoreState::STATE_NOTE_ENTRY);

                QStringList parts = msg.split(" ");
                bool parsed = true;

                if (!parts.isEmpty())
                {
                    QString txt = parts.at(1);
                    float bpm = -1;

                    // pattern, ratio, relative, followtext
                    QString pattern;
                    double ratio;
                    bool relative = false;
                    bool followText = false;

                    if (txt.startsWith("TEXT_") && parts.count() > 2)
                    {
                        QString bpmText = parts.at(2);
                        bool ok = false;
                        bpm = bpmText.toInt(&ok);

                        if (ok)
                        {
                            relative = false;
                            followText = true;

                            if (txt.compare("TEXT_1") == 0)
                                { pattern = "<sym>metNoteHalfUp</sym> = %1";        ratio = bpm/30.0; }
                            else if (txt.compare("TEXT_2") == 0)
                                { pattern = "<sym>metNoteQuarterUp</sym> = %1";     ratio = bpm/60.0; }
                            else if (txt.compare("TEXT_3") == 0)
                                { pattern = "<sym>metNote8thUp</sym> = %1";         ratio = bpm/120.0; }
                            else if (txt.compare("TEXT_4") == 0)
                                { pattern = "<sym>metNoteHalfUp</sym><sym>space</sym><sym>metAugmentationDot</sym> = %1";       ratio = (bpm*1.5)/30.0; }
                            else if (txt.compare("TEXT_5") == 0)
                                { pattern = "<sym>metNoteQuarterUp</sym><sym>space</sym><sym>metAugmentationDot</sym> = %1";    ratio = (bpm*1.5)/60.0; }
                            else if (txt.compare("TEXT_6") == 0)
                                { pattern = "<sym>metNote8thUp</sym><sym>space</sym><sym>metAugmentationDot</sym> = %1";        ratio = (bpm*1.5)/120.0; }
                            else { parsed = false; }

                            pattern = pattern.arg(bpm);
                        }

                    }
                    else if (txt.startsWith("MOD_"))
                    {
                        relative = true;
                        followText = true;

                        if (txt.compare("MOD_1") == 0)
                            { pattern = "<sym>metNoteQuarterUp</sym> = <sym>metNoteQuarterUp</sym><sym>space</sym><sym>metAugmentationDot</sym>";   ratio = 3.0/2.0; }
                        else if (txt.compare("MOD_2") == 0)
                            { pattern = "<sym>metNoteQuarterUp</sym><sym>space</sym><sym>metAugmentationDot</sym> = <sym>metNoteQuarterUp</sym>";   ratio = 2.0/3.0; }
                        else if (txt.compare("MOD_3") == 0)
                            { pattern = "<sym>metNoteHalfUp</sym> = <sym>metNoteQuarterUp</sym>";   ratio = 1.0/2.0; }
                        else if (txt.compare("MOD_4") == 0)
                            { pattern = "<sym>metNoteQuarterUp</sym> = <sym>metNoteHalfUp</sym>";   ratio = 2.0/1.0; }
                        else if (txt.compare("MOD_5") == 0)
                            { pattern = "<sym>metNote8thUp</sym> = <sym>metNote8thUp</sym>";        ratio = 1.0/1.0; }
                        else if (txt.compare("MOD_6") == 0)
                            { pattern = "<sym>metNoteQuarterUp</sym> = <sym>metNoteQuarterUp</sym>";ratio = 1.0/1.0; }
                        else { parsed = false; }

                    }
                    else
                    {
                        if (txt.compare("GRAVE") == 0)                  { pattern = "Grave";            bpm = 35.0/60.0; }
                        else if (txt.compare("LARGO") == 0)             { pattern = "Largo";            bpm = 50.0/60.0; }
                        else if (txt.compare("LENTO") == 0)             { pattern = "Lento";            bpm = 52.5/60.0; }
                        else if (txt.compare("ADAGIO") == 0)            { pattern = "Adagio";           bpm = 71.0/60.0; }
                        else if (txt.compare("ANDANTE") == 0)           { pattern = "Andante";          bpm = 92.0/60.0; }
                        else if (txt.compare("ANDANTINO") == 0)         { pattern = "Andantino";        bpm = 70.0/60.0; }
                        else if (txt.compare("MODERATO") == 0)          { pattern = "Moderato";         bpm = 114.0/60.0; }
                        else if (txt.compare("ALLEGRETTO") == 0)        { pattern = "Allegretto";       bpm = 116.0/60.0; }
                        else if (txt.compare("ALLEGRO_MODERATO") == 0)  { pattern = "Allegro moderato"; bpm = 120.0/60.0; }
                        else if (txt.compare("ALLEGRO") == 0)           { pattern = "Allegro";          bpm = 144.0/60.0; }
                        else if (txt.compare("VIVACE") == 0)            { pattern = "Vivace";           bpm = 172.0/60.0; }
                        else if (txt.compare("PRESTO") == 0)            { pattern = "Presto";           bpm = 187.0/60.0; }
                        else if (txt.compare("PRESTISSIMO") == 0)       { pattern = "Prestissimo";      bpm = 200.0/60.0; }
                        else { parsed = false; }
                    }

                    if (parsed)
                    {
                        TempoText* tempoText = static_cast<TempoText*>(Element::create(ElementType::TEMPO_TEXT, _currentScore));
                        tempoText->setScore(_currentScore);
                        tempoText->setTrack(_currentScore->inputTrack());
                        tempoText->setParent(_currentScore->inputState().segment());
                        tempoText->setXmlText(pattern);
                        tempoText->setFollowText(followText);
                        if (relative)
                            tempoText->setRelative(ratio);
                        else
                            tempoText->setTempo(bpm);

                        _currentScore->startCmd();
                        _currentScore->undoAddElement(tempoText);
                        tempoText->undoSetTempo(bpm);
                        tempoText->undoSetFollowText(followText);
                        _currentScore->endCmd();
                    }
                }

            }

            else if (msg.startsWith("INTERVAL"))
            {
                QString txt = msg.split(" ").at(1);
                bool ok = false;
                int interval = txt.toInt(&ok);

                if (ok)
                {
                    QString intervalTypeCmd("interval%1");
                    intervalTypeCmd = intervalTypeCmd.arg(interval);

                    if (_scoreView)
                        _scoreView->cmd(getAction(intervalTypeCmd.toStdString().data()));
                }
            }

            else if (msg.startsWith("VOICE"))
            {
                /* HOW TO ADD AN INSTRUMENT BY NAME :(
                InstrumentTemplate* it = searchTemplate("flute");

                Part* part = nullptr;
                if (it != nullptr)
                {
                    part = new Part(_currentScore);
                    part->initFromInstrTemplate(it);
                }

                if (part != nullptr)
                {
                    int staffIdx = _currentScore->nstaves() + 1;
                    for (int i=0; i<it->nstaves(); i++)
                    {
                        Staff* staff = new Staff(_currentScore);
                        staff->setPart(part);

                        staff->init(it, it->staffTypePreset, i);
                        staff->setDefaultClefType(it->clefType(i));

                        part->staves()->push_back(staff);
                        _currentScore->staves().insert(staffIdx + i, staff);
                    }

                    _currentScore->insertPart(part, staffIdx);
                    int sidx = _currentScore->staffIdx(part);
                    int eidx = sidx + part->nstaves();
                    for (Measure* m = _currentScore->firstMeasure(); m; m = m->nextMeasure())
                    {
                        m->cmdAddStaves(sidx, eidx, true);
                    }

                    // check bar lines
                    for (int staffIdx = 0; staffIdx < _currentScore->nstaves();)
                    {
                        Staff* staff = _currentScore->staff(staffIdx);
                        int barLineSpan = staff->barLineSpan();
                        if (barLineSpan == 0)
                            staff->setBarLineSpan(1);
                        int nstaffIdx = staffIdx + barLineSpan;
                        for (int idx = staffIdx+1; idx < nstaffIdx; ++idx)
                        {
                            Staff* tStaff = _currentScore->staff(idx);
                            if (tStaff)
                                  tStaff->setBarLineSpan(0);
                        }

                        staffIdx = nstaffIdx;
                    }

                    _currentScore->setLayoutAll(true);
                    _currentScore->endCmd();
                    _currentScore->rebuildMidiMapping();
                    seq->initInstruments();
                }
                */

                QString txt = msg.split(" ").at(1);
                int voice = txt.toInt() - 1; // voice idx start from 0

                if (_scoreView)
                    _scoreView->changeVoice(voice);
            }

            else if (msg.startsWith("PLAY") || msg.startsWith("STOP"))
            {
                if (_scoreView)
                    _scoreView->cmd(getAction("play"));
            }

            else if (msg.startsWith("METRONOME"))
            {
                QString txt = msg.split(" ").at(1);
                getAction("metronome")->setChecked(txt.compare("ON") == 0);
            }

            else if (msg.startsWith("TIE"))
            {
                if (_scoreView)
                {
                    // tie when no slur is active
                    if (_currentScore->inputState().slur() == nullptr)
                        _scoreView->cmd(getAction("tie"));
                }
            }

            else if (msg.startsWith("TUPLET"))
            {
                // force scoreView's state machine to process state transitions
                QCoreApplication::processEvents();

                QStringList parts = msg.split(" ");
                QString txt1 = parts.at(1);
                QString txt2 = parts.at(2);
                int numerator = txt1.remove("NUM:").toInt();
                int denominator = txt2.remove("DEN:").toInt();

                if (denominator == 2)
                {
                    _currentScore->startCmd();
                    _scoreView->cmdTuplet(numerator);
                    _currentScore->endCmd();
                }
//                else
//                {
//                    // custom tuplet NOT WORKING
//                    _currentScore->expandVoice();
//                    _currentScore->changeCRlen(_currentScore->inputState().cr(), _currentScore->inputState().duration());
//                    ChordRest* cr = _currentScore->inputState().cr();

//                    if (cr != nullptr)
//                    {
//                        Measure* measure = cr->measure();
//                        Tuplet* tuplet = new Tuplet(_currentScore);
//                        tuplet->setTrack(cr->track());
//                        tuplet->setTick(cr->tick());
//                        tuplet->setRatio(Fraction(numerator, denominator));

//                        Fraction f1(cr->ticks());
//                        tuplet->setTicks(f1);
//                        Fraction f = f1 * Fraction(1, tuplet->ratio().denominator());
//                        f.reduce();

//                        tuplet->setBaseLen(f);
//                        tuplet->setParent(measure);

//                        _currentScore->startCmd();
//                        _scoreView->cmdCreateTuplet(cr, tuplet);
//                        _currentScore->endCmd();
//                        _scoreView->moveCursor();
//                    }
//                }
            }

            // put note
            else if (msg.startsWith("TIMESIG"))
            {
                _museScore->changeState(ScoreState::STATE_NOTE_ENTRY);

                if (_currentScore->inputState().noteEntryMethod() == NoteEntryMethod::STEPTIME &&
                        _currentScore->inputState().noteEntryMode())
                {
                    QStringList parts = msg.split(" ");

                    if (parts.length() > 2)
                    {
                        bool ok1 = false;
                        bool ok2 = false;
                        int beats = parts.at(1).toInt(&ok1);
                        int beatUnit = parts.at(2).toInt(&ok2);
                        TimeSigType tst = TimeSigType::NORMAL;

                        // special case: alla breve
                        if (beats == -2 && beatUnit == -2)
                            tst = TimeSigType::ALLA_BREVE;

                        // special case: common time
                        if (beats == -4 && beatUnit == -4)
                            tst = TimeSigType::FOUR_FOUR;

                        if (ok1 && ok2)
                        {
                            ChordRest* cr = _currentScore->inputState().cr();
                            if (cr != nullptr)
                            {
                                Measure* measure = cr->measure();

                                if (measure != nullptr)
                                {
                                    TimeSig* ts = new TimeSig(_currentScore);
                                    ts->setSig(Fraction(beats, beatUnit), tst);

                                    //Selection currentSelection = _currentScore->selection();
                                    _currentScore->startCmd();
                                    _currentScore->cmdAddTimeSig(measure, _currentScore->inputState().track() / VOICES, ts, false);
                                    _currentScore->endCmd();

                                    //_currentScore->setSelection(currentSelection);
                                }
                            }
                        }
                    }
                }
            }

            // Key signature
            else if (msg.startsWith("KEYSIG"))
            {
                QStringList parts = msg.split(" ");

                if (parts.length() > 2)
                {
                    bool ok1 = false;
                    bool ok2 = false;
                    int acc = parts.at(1).toInt(&ok1);
                    int keyopt = parts.at(2).toInt(&ok2);

                    int sign = 1; // positive for sharp
                    if (acc == 3) sign = -1; // negative for flat
                    Key k = static_cast<Key>(sign * keyopt);
                    KeySig* ks = new KeySig(_currentScore);
                    ks->setKey(k);
                    ks->setTrack(_currentScore->inputState().track());

                    //int currentStaff = _currentScore->inputState().track() / VOICES;
                    _currentScore->startCmd();
                    for (Staff* staff : _currentScore->staves()) //_currentScore->staff(currentStaff)
                        _currentScore->undoChangeKeySig(staff, _currentScore->inputState().tick(), ks->keySigEvent());
                    _currentScore->endCmd();
                }
            }

            // tremolo
            else if (msg.startsWith("TREMOLO"))
            {
                if(!_currentScore->inputState().cr())
                    break;

                if (_currentScore->inputState().cr()->type() == ElementType::CHORD)
                {
                    QStringList parts = msg.split(" ");

                    if (parts.length() > 1)
                    {
                        QString txt = msg.split(" ").at(1);
                        TremoloType tt = TremoloType::INVALID_TREMOLO;
                        if (txt.compare("STEM_8") == 0)        { tt = TremoloType::R8; }
                        else if (txt.compare("STEM_16") == 0)       { tt = TremoloType::R16; }
                        else if (txt.compare("STEM_32") == 0)       { tt = TremoloType::R32; }
                        else if (txt.compare("STEM_64") == 0)       { tt = TremoloType::R64; }
                        else if (txt.compare("BETWEEN_8") == 0)     { tt = TremoloType::C8; }
                        else if (txt.compare("BETWEEN_16") == 0)    { tt = TremoloType::C16; }
                        else if (txt.compare("BETWEEN_32") == 0)    { tt = TremoloType::C32; }
                        else if (txt.compare("BETWEEN_64")== 0)     { tt = TremoloType::C64; }

                        if (tt != TremoloType::INVALID_TREMOLO)
                        {
                            _currentScore->startCmd();

                            Selection sel = _currentScore->selection();
                            if (sel.isNone())
                                  return;

                            for (Element* target : sel.elements())
                            {
                                Tremolo* tr = new Tremolo(_currentScore);
                                tr->setParent(_currentScore->inputState().cr());
                                tr->setTremoloType(tt);

                                EditData& dropData = _scoreView->getEditData();
                                dropData.pos         = target->pagePos();
                                dropData.dragOffset  = QPointF();
                                dropData.dropElement = tr;
                                if (target->acceptDrop(dropData))
                                {
                                      Element* el = target->drop(dropData);
                                      if (el && !_scoreView->noteEntryMode())
                                            _currentScore->select(el, SelectType::SINGLE, 0);

                                      dropData.dropElement = 0;
                                }
                            }
                            _currentScore->endCmd();

                        }
                    }
                }
            }

            else if (msg.startsWith("ARTICULATION"))
            {
                QStringList parts = msg.split(" ");

                if (parts.length() > 1)
                {
                    bool supported = true;
                    QString txt = parts.at(1);
                    SymId ft = SymId::fermataAbove;
                    if (txt.compare("FERMATA_NORMAL") == 0) { ft = SymId::fermataAbove; }
                    else if (txt.compare("FERMATA_LONG") == 0) { ft = SymId::fermataLongAbove; }
                    else if (txt.compare("FERMATA_LONG_HENZE") == 0) { ft = SymId::fermataLongHenzeAbove; }
                    else if (txt.compare("ACCENT") == 0) { ft = SymId::articAccentAbove; }
                    else if (txt.compare("STACCATO") == 0) { ft = SymId::articStaccatoAbove; }
                    else if (txt.compare("STACCATISSIMO_WEDGE") == 0) { ft = SymId::articStaccatissimoWedgeAbove; }
                    else if (txt.compare("TENUTO") == 0) { ft = SymId::articTenutoAbove; }
                    else if (txt.compare("LOURE_TENUTO_STACCATO") == 0) { ft = SymId::articTenutoStaccatoAbove; } // ?
                    else if (txt.compare("MARCATO") == 0) { ft = SymId::articMarcatoAbove; }
                    else if (txt.compare("ACCENT_STACCATO") == 0) { ft = SymId::articAccentStaccatoAbove; }
                    else if (txt.compare("MARCATO_STACCATO") == 0) { ft = SymId::articMarcatoStaccatoAbove; }
                    else if (txt.compare("MARCATO_TENUTO") == 0) { ft = SymId::articMarcatoTenutoAbove; }
                    else if (txt.compare("STACCATISSIMO") == 0) { ft = SymId::articStaccatissimoAbove; }
                    else if (txt.compare("STRESS") == 0) { ft = SymId::articStressAbove; }
                    else if (txt.compare("ACCENT_TENUTO") == 0) { ft = SymId::articTenutoAccentAbove; }
                    else if (txt.compare("UNSTRESS") == 0) { ft = SymId::articUnstressAbove; }
                    else if (txt.compare("FADE_IN") == 0) { ft = SymId::guitarFadeIn; }
                    else if (txt.compare("FADE_OUT") == 0) { ft = SymId::guitarFadeOut; }
                    else if (txt.compare("MUTED") == 0) { ft = SymId::brassMuteClosed; }
                    else if (txt.compare("UP_BOW") == 0) { ft = SymId::stringsUpBow; }
                    else if (txt.compare("DOWN_BOW") == 0) { ft = SymId::stringsDownBow; }
                    else if (txt.compare("SNAP_PIZZICATO") == 0) { ft = SymId::pluckedSnapPizzicatoAbove; }
                    else {
                        supported = false;
                    }

                    if (supported)
                    {
                        _currentScore->startCmd();
                        _currentScore->addArticulation(ft);
                        _currentScore->endCmd();
                    }
                }
            }

            else if (msg.startsWith("ORNAMENT"))
            {
                QStringList parts = msg.split(" ");

                if (parts.length() > 1)
                {
                    bool supported = true;
                    QString txt = parts.at(1);
                    SymId ft = SymId::noSym;
                    if (txt.compare("INVERTED_TURN") == 0) { ft = SymId::ornamentTurnInverted; }
                    else if (txt.compare("TURN") == 0) { ft = SymId::ornamentTurn; }
                    else if (txt.compare("TRILL") == 0) { ft = SymId::ornamentTrill; }
                    else if (txt.compare("MORDENT") == 0) { ft = SymId::ornamentMordent; }
                    else if (txt.compare("INVERTED_MORDENT") == 0) { ft = SymId::ornamentMordentInverted; }
                    else if (txt.compare("TREMBLEMENT") == 0) { ft = SymId::ornamentTremblement; }
                    else if (txt.compare("PRALL_MORDENT") == 0) { ft = SymId::ornamentPrallMordent; }
                    else {
                        supported = false;
                    }

                    if (supported)
                    {
                        _currentScore->startCmd();
                        _currentScore->addArticulation(ft);
                        _currentScore->endCmd();
                    }
                }
            }

            else if (msg.startsWith("GRACE"))
            {
                QStringList parts = msg.split(" ");

                if (parts.length() > 1)
                {
                    NoteType nt = NoteType::INVALID;
                    int duration = MScore::division / 2;

                    bool supported = true;
                    QString txt = parts.at(1);
                    if (txt.compare("ACCIACCATURA") == 0) { nt = NoteType::ACCIACCATURA; }
                    else if (txt.compare("APPOGGIATURA") == 0) { nt = NoteType::APPOGGIATURA; }
                    else if (txt.compare("QUARTER") == 0) { nt = NoteType::GRACE4; duration = MScore::division; }
                    else if (txt.compare("N16TH") == 0) { nt = NoteType::GRACE16; duration = MScore::division / 4; }
                    else if (txt.compare("N32ND") == 0) { nt = NoteType::GRACE32; duration = MScore::division / 8; }
                    else if (txt.compare("N8TH_AFTER") == 0) { nt = NoteType::GRACE8_AFTER; }
                    else if (txt.compare("N16TH_AFTER") == 0) { nt = NoteType::GRACE16_AFTER; duration = MScore::division / 4; }
                    else if (txt.compare("N32ND_AFTER") == 0) { nt = NoteType::GRACE32_AFTER; duration = MScore::division / 8; }
                    else {
                        supported = false;
                    }

                    if (supported)
                    {
                        _currentScore->startCmd();
                        _currentScore->cmdAddGrace(nt, duration);
                        _currentScore->endCmd();
                    }
                }
            }

            // transposition
            else if (msg.startsWith("UPDIATONIC"))
            {
                _currentScore->startCmd();
                _currentScore->upDown(true, UpDownMode::DIATONIC);
                _currentScore->endCmd();
            }

            else if (msg.startsWith("DOWNDIATONIC"))
            {
                _currentScore->startCmd();
                _currentScore->upDown(false, UpDownMode::DIATONIC);
                _currentScore->endCmd();
            }

            else if (msg.startsWith("UPOCTAVE"))
            {
                _currentScore->startCmd();
                _currentScore->upDown(true, UpDownMode::OCTAVE);
                _currentScore->endCmd();
            }

            else if (msg.startsWith("DOWNOCTAVE"))
            {
                _currentScore->startCmd();
                _currentScore->upDown(false, UpDownMode::OCTAVE);
                _currentScore->endCmd();
            }
        }
    }
    if(escape)
    {
        _currentScore->startCmd();
        _scoreView->cmd("escape");
        _currentScore->endCmd();
    }
    collectAndSendStatus();
}

// Send status to Odla
void ODLADriver::collectAndSendStatus()
{
    // After execute command send an update status to ODLA
    union state_message_t status_message;
    Selection sel = _currentScore->selection();

    int len = status_message.common_fields.msgLen  = sizeof(state_message_t::common_fields_t);
    status_message.common_fields.type = NO_ELEMENT;                         // type (0 for status reply) TODO: craete protocol
    status_message.common_fields.mscoreState = _scoreView->mscoreState();  // state of input
    status_message.common_fields.selectionState = sel.state();             // type of selection
    status_message.common_fields.selectedElements = sel.elements().size(); // number of elements selected

    if(sel.isSingle()) //if we have only an element selected
    {
        Element* e = sel.element();

        len = status_message.common_fields.msgLen  = sizeof(state_message_t::element_fields_t);
        status_message.common_fields.type = SINGLE_ELEMENT;
        status_message.element_fields.elementType   = e->type();                    // Byte 4: type of selection
        status_message.element_fields.notePitch     = getNotePitch(e);               // Byte 5: pitch of note selected
        status_message.element_fields.noteAccident  = getNoteAccident(e);            // Byte 6: accidents of note selected
        status_message.element_fields.duration      = getDuration(e);                // Byte 7: duration of element selected
        status_message.element_fields.dotsNum       = getDots(e);                    // Byte 8: dots of element selected
        status_message.element_fields.measureNum    = getMeasureNumber(e);                     // Byte 9: measure number LSB of element selected
        status_message.element_fields.beat          = getBeat(e);                    // Byte 11: beat of element selected
        status_message.element_fields.staff         = getStaff(e);                   // Byte 12: staff of element selected
        status_message.element_fields.clef          = getClef(e);                    // Byte 13: clef of element selected
        status_message.element_fields.timeSignatureNum = getTimeSig(e).numerator();     // Byte 14: numerator of time signature of element selected
        status_message.element_fields.timeSignatureDen = getTimeSig(e).denominator();   // Byte 15: denominator of time signature of element selected
        status_message.element_fields.keySignature  = getKeySignature(e);            // Byte 16: keysignature of element selected
        status_message.element_fields.voiceNum      = getVoice(e);                   // Byte 17: voice of element selected
        status_message.element_fields.bpm           = getBPM(e);                       // Byte 19: bpm number LSB of element selected
    }
    else if(sel.isRange()) //if we have more than an element selected
    {
        Segment* startSegment = sel.startSegment();
        Segment* endSegment = sel.endSegment() ? sel.endSegment()->prev1MM() : _currentScore->lastSegment();

        len = status_message.common_fields.msgLen    = sizeof(state_message_t::range_fields_t);
        status_message.common_fields.type = RANGE_ELEMENT;
        status_message.range_fields.firstMesaure    = getMeasureNumber(startSegment);
        status_message.range_fields.lastMesaure     = getMeasureNumber(endSegment);
        status_message.range_fields.firstBeat       = getBeat(startSegment);
        status_message.range_fields.lastBeat        = getBeat(endSegment);
        status_message.range_fields.firstStaff      = getStaff(sel.elements().first());
        status_message.range_fields.lastStaff       = getStaff(sel.elements().last());
    }

    int written = _localSocket->write(QByteArray(status_message.data, len));
    _localSocket->flush();

    if (written < 0)
        qDebug() << "ODLA driver: could not reply to play status request.";
}

// Get tone of note
quint8 ODLADriver::getNotePitch(Element *e)
{
    if (e->type() != ElementType::NOTE)
        return 0xFF; //TODO: is 0xFF invalid?
    Note* n = static_cast<Ms::Note*>(e);
    return n->pitch();
}

// Get alteration of note
AccidentalType ODLADriver::getNoteAccident(Element *e)
{
    if (e->type() != ElementType::NOTE)
        return AccidentalType::NONE; //should be INVALID, but there isn't in enum
    Note* n = static_cast<Ms::Note*>(e);
    return n->accidentalType();
}

// Get duration if element is a chord or a rest
TDuration::DurationType ODLADriver::getDuration(Ms::Element* e)
{
    TDuration::DurationType returnType = TDuration::DurationType::V_INVALID;
    if(e->type() == ElementType::NOTE || e->type() == ElementType::REST)
    {
        if(e->type() == ElementType::NOTE)
        {
            Ms::Chord* n = static_cast<Ms::Chord*>(e->parent());
            returnType = n->durationType().type();
        }
        else if(e->type() == ElementType::REST)
        {
            Ms::Rest* n = static_cast<Ms::Rest*>(e);
            returnType = n->durationType().type();
        }
    }
    return returnType;
}

// Get duration if element is a chord or a rest
quint8 ODLADriver::getDots(Element *e)
{
    if(e->type() == ElementType::NOTE || e->type() == ElementType::REST)
    {
        if(e->type() == ElementType::NOTE)
        {
            Ms::Chord* n = static_cast<Ms::Chord*>(e->parent());
            return n->durationType().dots();
        }
        else if(e->type() == ElementType::REST)
        {
            Ms::Rest* n = static_cast<Ms::Rest*>(e);
            return n->durationType().dots();
        }
    }
    return -1;
}

// Get measure number of an element
int ODLADriver::getMeasureNumber(Ms::Element* e)
{
    Segment* seg;
    if(e->type() != ElementType::SEGMENT)
        seg = static_cast<Segment*>(findElementParent(e, ElementType::SEGMENT));
    else
        seg = static_cast<Segment*>(e);
    if(!seg) return 0;

    int bar = 0;
    int beat = 0;
    int ticks = 0;
    TimeSigMap* tsm = e->score()->sigmap();
    tsm->tickValues(seg->tick().ticks(), &bar, &beat, &ticks);

    return bar + 1;
}

// Get beat of an element in a measure
quint8 ODLADriver::getBeat(Ms::Element* e)
{
    Segment* seg;
    if(e->type() != ElementType::SEGMENT)
        seg = static_cast<Segment*>(findElementParent(e, ElementType::SEGMENT));
    else
        seg = static_cast<Segment*>(e);
    if(!seg) return 0;

    int bar = 0;
    int beat = 0;
    int ticks = 0;
    TimeSigMap* tsm = e->score()->sigmap();
    tsm->tickValues(seg->tick().ticks(), &bar, &beat, &ticks);

    return beat + 1;
}

// Get staff where element is placed
quint8 ODLADriver::getStaff(Element *e)
{
    return e->staffIdx() + 1;
}

// Get the clef of the context of element
ClefType ODLADriver::getClef(Element *e)
{
    Ms::Element* prev = findElementBefore(e, ElementType::CLEF, e->staffIdx());

    if(!prev)
        return ClefType::INVALID;
    else
        return (static_cast<Clef*>(prev))->clefType();
}

// Get the time signature of the context of element
Fraction ODLADriver::getTimeSig(Element *e)
{
    return e->score()->sigmap()->timesig(0).timesig();
}

// Get the Staff Key of the element
Key ODLADriver::getKeySignature(Element *e)
{
    Ms::Element* prev = findElementBefore(e, ElementType::KEYSIG);

    if(!prev)
        return Key::C; //TODO: MANAGE ERROR
    else
        return (static_cast<KeySig*>(prev))->key();
}

// Get the voice in which rest o note is placed
quint8 ODLADriver::getVoice(Element *e)
{
    return e->voice() + 1;
}

// Get the BPM setted before element
int ODLADriver::getBPM(Element *e)
{
    Ms::Element* prev = findElementBefore(e, ElementType::TEMPO_TEXT);

    if(!prev)
       return _currentScore->defaultTempo() * 60;
    else
        return (static_cast<TempoText*>(prev))->tempo() * 60;
}

// Find an element placed before el of type type in staff staffIdx (if staffIdx = -1 don't care staff)
Element *ODLADriver::findElementBefore(Element *el, ElementType type, int staffIdx)
{
    for(Page* page : _currentScore->pages())
    {
        // get all elements in page
        QListIterator<Element*> i(page->elements());
        // send iterator to the end
        i.toFront();
        // We need to start search from (almost) the same element and going backward
        if(!i.findNext(el))
            continue;

        while (i.hasPrevious())
        {
            Element *toReturn = i.previous();
            if(toReturn->type() == type)
            {
                if (staffIdx == -1 || toReturn->staffIdx() == staffIdx)
                    return toReturn;
            }
        }
    }
    return NULL;
}

Element *ODLADriver::findElementParent(Element *el, ElementType type)
{
    Element *p = el;
    while (p && p->type() != type)
        p = p->parent();
    return p;
}

/*!
 * \brief ODLADriver::setNoteEntryMode
 * \param enabled
 */
void ODLADriver::setNoteEntryMode(bool enabled)
{
    if (enabled && !_scoreView->noteEntryMode())
    {
        // force note input ON
        _currentScore->startCmd();
        _currentScore->inputState().setNoteEntryMethod(NoteEntryMethod::STEPTIME);
        _scoreView->cmd(getAction("note-input"));

        // force scoreView's state machine to process state transitions
        QCoreApplication::processEvents();

        // set default duration if needed
        TDuration currentDuration = _currentScore->inputState().duration();
        if (currentDuration == TDuration::DurationType::V_ZERO ||
                currentDuration == TDuration::DurationType::V_INVALID)
        {
            _currentScore->inputState().setDuration(TDuration::DurationType::V_QUARTER);
        }
        _currentScore->inputState().setNoteEntryMode(true);
        _currentScore->endCmd();
    }
    else if (!enabled && _scoreView->noteEntryMode())
    {
        // note input OFF
        _scoreView->cmd(getAction("note-input"));
    }
}

/*!
 * \brief ODLADriver::addSpannerToCurrentSelection
 * \param spanner
 */
bool ODLADriver::addSpannerToCurrentSelection(Spanner *spanner)
{
    bool success = false;
    if (spanner != nullptr && _currentScore != nullptr)
    {
        Selection sel = _currentScore->selection();
        if (!sel.isNone())
        {
            if (sel.isRange())
            {
                // add to whole range
                _currentScore->startCmd();
                _currentScore->cmdAddSpanner(spanner,
                                             _currentScore->inputState().track() / VOICES,
                                             sel.startSegment(), sel.endSegment());
                _currentScore->endCmd();

                success = true;
            }
            else
            {

                if(!_currentScore->inputState().cr())
                    return false;
                // drop on current measure only
                _currentScore->startCmd();
                _currentScore->cmdAddSpanner(spanner, _currentScore->inputState().cr()->pagePos());
                _currentScore->endCmd();
                success = true;
            }
        }
    }

    return success;
}

/*!
 * \brief ODLADriver::setCurrentScore
 * \param current
 */
void ODLADriver::setCurrentScore(MasterScore* current)
{
    _currentScore = current;
}

/*!
 * \brief ODLADriver::setScoreView
 * \param scoreView
 */
void ODLADriver::setScoreView(Ms::ScoreView* scoreView)
{
    _scoreView = scoreView;
}

/*!
 * \brief ODLADriver::nextUntiledFileName
 * \return
 */
QString ODLADriver::nextValidFileName(QString prefix, QString ext)
{
    QString newFileName("%1%2.%3");
    newFileName = newFileName.arg(prefix,
                                  ODLADriver::nextUntitledSuffixNumber(prefix, ext),
                                  ext);
    return newFileName;
}

/*!
 * \brief ODLADriver::nextUntitledSuffix
 * \return
 */
QString ODLADriver::nextUntitledSuffixNumber(QString untitledNamePrefix, QString ext)
{
    int next = 0;
    QDir scorePath(preferences.getString(PREF_APP_PATHS_MYSCORES));

    QString filter("*.%1");
    filter = filter.arg(ext);
    QStringList files = scorePath.entryList(QStringList() << filter, QDir::Files | QDir::NoSymLinks);
    QList<int> numSuffixes;
    for (QString fn : files)
    {
        if (fn.startsWith(untitledNamePrefix + "_"))
        {
            fn = fn.split(".").at(0);
            QString numSuffix = fn.remove(untitledNamePrefix);
            bool isNum = false;
            int num = numSuffix.toInt(&isNum);
            if (isNum)
                numSuffixes.append(num);
        }
        else if (fn.startsWith(untitledNamePrefix))
            next = 1;
    }

    if (!numSuffixes.isEmpty())
    {
        std::sort(numSuffixes.begin(), numSuffixes.end());
        next = numSuffixes.last() + 1;
    }

    QString result = "";
    if (next > 0)
        result = QString::number(next);

    return result;
}


} // namespace ODLA

