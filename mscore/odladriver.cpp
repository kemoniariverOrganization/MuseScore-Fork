#include "odladriver.h"
#include <QDebug>
#include <QMessageBox>
#include <QAction>
#include <QFileInfo>
#include <QDir>
#include <QTimer>
#include "libmscore/articulation.h"
#include "libmscore/fermata.h"
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
#include <masterpalette.h>
namespace ODLA {

using namespace Ms;

ODLADriver * ODLADriver::_instance;

ODLADriver* ODLADriver::instance(QObject* parent)
{
    if(_instance == nullptr)
        _instance = new ODLADriver(parent);
    return _instance;
}

ODLADriver::ODLADriver(QObject *parent) : QObject(parent)
{
    _localSocket = new QLocalSocket(this);
    _currentScore = nullptr;
    _scoreView = nullptr;
    _editingChord = false;

    _reconnectTimer = new QTimer(this);
    _reconnectTimer->start(2000);

    connect(_localSocket, &QLocalSocket::connected, this, &ODLADriver::onConnected);
    connect(_localSocket, &QLocalSocket::readyRead, this, &ODLADriver::onIncomingData);

    connect(_reconnectTimer, &QTimer::timeout, this, &ODLADriver::attemptConnection);
    connect(_localSocket, &QLocalSocket::disconnected, _reconnectTimer, static_cast<void (QTimer::*)()> (&QTimer::start));
}

void ODLADriver::attemptConnection()
{
    static int attempt = 0;
    if (_localSocket && (_localSocket->state() == QLocalSocket::UnconnectedState))
    {
        _localSocket->connectToServer("ODLA_MSCORE_SERVER", QIODevice::ReadWrite);
        qDebug() << "Connecting to server attempt " << ++attempt;
    }
}

void ODLADriver::onConnected()
{
    _reconnectTimer->stop();
    qDebug() << "Connected to ODLA server";
    Ms::MuseScore* _museScore = qobject_cast<Ms::MuseScore*>(this->parent());
    mscore->showMasterPalette(""); //Trick to load palette objects

    PaletteWorkspace* pw = _museScore->getPaletteWorkspace();
    const PaletteTree* tree = pw->masterPaletteModel()->paletteTree();
    // Add all palette elements in a QMap
    for (auto& p : tree->palettes)
        for (int i = 0; i < p->ncells(); ++i)
        {
            QString name = p->cell(i)->name;
            QString tag = p->cell(i)->tag;
            Element* element = p->cell(i)->element.get();
            if(name.count(QRegExp("%\\d+")) == 1)
            {
                int arg = 0;
                while(_paletteItemList[name.arg(arg)]) {arg++;} //already exist
                name = name.arg(arg);
            }
            _paletteItemList[name] = element;
        }
}

void ODLADriver::onIncomingData()
{
    // We can't move Musescore cast in odladriver.h in order to avoiding circular include
    Ms::MuseScore* _museScore = qobject_cast<Ms::MuseScore*>(this->parent());
    _museScore->setWindowState( (_museScore->windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    _museScore->raise();
    _museScore->activateWindow();

    bool escapeAfterCommand = false;
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
                escapeAfterCommand = true;
                msg.remove(0,1);
            }

            if (msg.startsWith("cs::"))
            {
                QString cmd = msg.split("::").last();
                _currentScore->startCmd();
                _currentScore->cmd(getAction(cmd.toStdString().data()), _scoreView->getEditData());
                _currentScore->endCmd();
            }

            else if (msg.startsWith("sv::"))
            {
                QString cmd = msg.split("::").last();
                _currentScore->startCmd();
                _scoreView->cmd(getAction(cmd.toStdString().data()));
                _currentScore->endCmd();
            }

            else if (msg.startsWith("ms::"))
            {
                QString cmd = msg.split("::").last();
                _museScore->cmd(getAction(cmd.toStdString().data()),cmd);
            }

            else if (msg.startsWith("pal::"))
            {
                QString elementDescription = msg.split("::").last();
                Element* e = _paletteItemList[elementDescription];
                if(e != nullptr)
                    Palette::applyPaletteElement(e);
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

            else if (msg.startsWith("withBrackets"))
            {
                _currentScore->startCmd();
                for (Element* el : _currentScore->selection().elements())
                {
                    if (el->type() == ElementType::NOTE)
                    {
                        Accidental* acc =  toNote(el)->accidental();
                        if (acc != nullptr)
                        {
                            _currentScore->addRefresh(acc->canvasBoundingRect());
                            acc->undoChangeProperty(Pid::ACCIDENTAL_BRACKET, int(AccidentalBracket::PARENTHESIS), PropertyFlags::NOSTYLE);
                        }
                    }
                }
                _currentScore->endCmd();
            }

            else if (msg.startsWith("LINEVIEW"))
            {
                _museScore->switchLayoutMode(LayoutMode::LINE);
            }

            else if (msg.startsWith("PAGEVIEW"))
            {
                _museScore->switchLayoutMode(LayoutMode::PAGE);
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

            }

            else if (msg.startsWith("METRONOME"))
            {
                QString txt = msg.split(" ").at(1);
                getAction("metronome")->setChecked(txt.compare("ON") == 0);
            }

    }
    if(escapeAfterCommand)
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
    Ms::Element* prev = findElementBefore(e, ElementType::TIMESIG);
    return (static_cast<TimeSig*>(prev))->sig();
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

///*!
// * \brief ODLADriver::setNoteEntryMode
// * \param enabled
// */
//void ODLADriver::setNoteEntryMode(bool enabled)
//{
//    if (enabled && !_scoreView->noteEntryMode())
//    {
//        // force note input ON
//        _currentScore->startCmd();
//        _currentScore->inputState().setNoteEntryMethod(NoteEntryMethod::STEPTIME);
//        _scoreView->cmd(getAction("note-input"));

//        // force scoreView's state machine to process state transitions
//        QCoreApplication::processEvents();

//        // set default duration if needed
//        TDuration currentDuration = _currentScore->inputState().duration();
//        if (currentDuration == TDuration::DurationType::V_ZERO ||
//                currentDuration == TDuration::DurationType::V_INVALID)
//        {
//            _currentScore->inputState().setDuration(TDuration::DurationType::V_QUARTER);
//        }
//        _currentScore->inputState().setNoteEntryMode(true);
//        _currentScore->endCmd();
//    }
//    else if (!enabled && _scoreView->noteEntryMode())
//    {
//        // note input OFF
//        _scoreView->cmd(getAction("note-input"));
//    }
//}

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

///*!
// * \brief ODLADriver::nextUntiledFileName
// * \return
// */
//QString ODLADriver::nextValidFileName(QString prefix, QString ext)
//{
//    QString newFileName("%1%2.%3");
//    newFileName = newFileName.arg(prefix,
//                                  ODLADriver::nextUntitledSuffixNumber(prefix, ext),
//                                  ext);
//    return newFileName;
//}

///*!
// * \brief ODLADriver::nextUntitledSuffix
// * \return
// */
//QString ODLADriver::nextUntitledSuffixNumber(QString untitledNamePrefix, QString ext)
//{
//    int next = 0;
//    QDir scorePath(preferences.getString(PREF_APP_PATHS_MYSCORES));

//    QString filter("*.%1");
//    filter = filter.arg(ext);
//    QStringList files = scorePath.entryList(QStringList() << filter, QDir::Files | QDir::NoSymLinks);
//    QList<int> numSuffixes;
//    for (QString fn : files)
//    {
//        if (fn.startsWith(untitledNamePrefix + "_"))
//        {
//            fn = fn.split(".").at(0);
//            QString numSuffix = fn.remove(untitledNamePrefix);
//            bool isNum = false;
//            int num = numSuffix.toInt(&isNum);
//            if (isNum)
//                numSuffixes.append(num);
//        }
//        else if (fn.startsWith(untitledNamePrefix))
//            next = 1;
//    }

//    if (!numSuffixes.isEmpty())
//    {
//        std::sort(numSuffixes.begin(), numSuffixes.end());
//        next = numSuffixes.last() + 1;
//    }

//    QString result = "";
//    if (next > 0)
//        result = QString::number(next);

//    return result;
//}


} // namespace ODLA

