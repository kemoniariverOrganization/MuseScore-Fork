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

//QDataStream& operator>>(QDataStream &stream, QMap<QString> &m) { return m;}
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
    _paused = false;

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
    mscore->showMasterPalette(""); //Trick to load palette objects
}

void ODLADriver::onIncomingData()
{
    if (!_currentScore || !_scoreView)
        return;
    // We can't move Musescore cast in odladriver.h in order to avoiding circular include
    Ms::MuseScore* _museScore = qobject_cast<Ms::MuseScore*>(this->parent());
    _museScore->setWindowState( (_museScore->windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    _museScore->raise();
    _museScore->activateWindow();    

    QMap<QString, QString> msg;
    QByteArray data = _localSocket->readAll();
    QDataStream stream(&data, QIODevice::ReadOnly);
    stream >> msg;
    qDebug() << "received from ODLA: " << msg;


    QString stateBefore = msg["STATE"];
    QString command = msg["COM"];
    int par1; bool par1Ok = false;
    int par2; bool par2Ok = false;
    par1 = msg["PAR1"].toInt(&par1Ok);
    par2 = msg["PAR2"].toInt(&par2Ok);

    QAction* playAction = getAction("play");

    if(     playAction->isChecked()
        &&( command.contains("next-chord")
        ||  command.contains("prev-chord")
        ||  command.contains("next-measure")
        ||  command.contains("prev-measure")))
        {
            command.prepend("play-");
            stateBefore = "NC";
        }

    if (stateBefore != 0)
    {
        if(stateBefore == "NORMAL")
            _scoreView->changeState(ViewState::NORMAL);
        else if(stateBefore == "NOTE_ENTRY")
            _scoreView->changeState(ViewState::NOTE_ENTRY);
        QCoreApplication::processEvents();
    }
    if (command == "play")
    {
        if(playAction->isChecked())
            _scoreView->changeState(ViewState::NOTE_ENTRY);
        else
            playAction->trigger();
        _paused = false;
    }

    else if (command == "pause")
    {
        if(playAction->isChecked() || _paused)
        {
            playAction->trigger();
            _paused = !_paused;
        }
    }
    else if (command.startsWith("palette") && _currentScore->selection().state() != SelState::NONE)
    {
        auto element = searchFromPalette(par1, par2);
        if(element)
        {
            switch(element->type())
            {
                case ElementType::MARKER:
                case ElementType::JUMP:
                case ElementType::BAR_LINE:
                case ElementType::KEYSIG:
                case ElementType::TIMESIG:// it doesn't work beacuse we insert TIMESIG as non palette (see below)
                    // We drop all this elements at the beginning of the selected element measure
                    emulateDrop(element, _currentScore->inputState().segment()->measure());
                    break;

                case ElementType::TEMPO_TEXT:
                {
                    auto tempo = static_cast<TempoText*>(element);
                    bool ok = false;
                    int bpm = command.toInt(&ok);
                    if(ok)
                    {
                        tempo->setTempo(bpm);
                        qDebug() << "setting bpm: " << bpm;
                    }
                    Palette::applyPaletteElement(tempo);
                    break;
                }

                default:
                    Palette::applyPaletteElement(element);
                    if(command.contains("bracket")) // this is due to a bad behaviour of Musescore
                        accBracket();
                    break;
            }
        }
    }

    else if (command == "insert-measures")
        _scoreView->cmdInsertMeasures(par1, ElementType::MEASURE);

    else if (command == "goto")
    {
        int target = par1 > 0 ? par1 : 1;
        target = target <= _currentScore->nmeasures() ? target : _currentScore->nmeasures();
        _currentScore->startCmd();
        _scoreView->searchMeasure(target);
        _currentScore->endCmd();
    }

    else if (command == "select-measures")
    {
        int from =  par1;
        int to =    par2;

        if(_scoreView->searchMeasure(from))
        {
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

            _currentScore->selectRange(measure_from, 0);
            _currentScore->selectRange(measure_to, _currentScore->nstaves() - 1);
            _currentScore->setUpdateAll();
            _currentScore->update();

            if (MScore::debugMode)
                qDebug() << "selecting from " << from << "to " << to;
        }
    }

    else if (command == "staff-pressed") // put note
    {
        int line = par1;
        // check chord option
        bool keepchord = (par2 & 1);
        bool slur = (par2 & 2);
        // disable slur
        if (!slur)
            _currentScore->inputState().setSlur(nullptr);

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
                _scoreView->cmd(getAction("add-slur"));
        }
    }

    else if (command == "line-view")
        _museScore->switchLayoutMode(LayoutMode::LINE);

    else if (command == "page-view")
        _museScore->switchLayoutMode(LayoutMode::PAGE);

    else if (command == "tempo")
    {
        int type = par1;
        int bpm = par2;

        // pattern, ratio, relative, followtext
        QString pattern;

        switch (type)
        {
            case 0:
                pattern = "<sym>metNoteHalfUp</sym> = %1";
                break;
            case 1:
                pattern = "<sym>metNoteQuarterUp</sym> = %1";
                break;
            case 2:
                pattern = "<sym>metNote8thUp</sym> = %1";
                break;
            case 3:
                pattern = "<sym>metNoteHalfUp</sym><sym>space</sym><sym>metAugmentationDot</sym> = %1";
                break;
            case 4:
                pattern = "<sym>metNoteQuarterUp</sym><sym>space</sym><sym>metAugmentationDot</sym> = %1";
                break;
            case 5:
                pattern = "<sym>metNote8thUp</sym><sym>space</sym><sym>metAugmentationDot</sym> = %1";
                break;
        }

        pattern = pattern.arg(bpm);

        TempoText* tempoText = static_cast<TempoText*>(Element::create(ElementType::TEMPO_TEXT, _currentScore));
        tempoText->setScore(_currentScore);
        tempoText->setTrack(_currentScore->inputTrack());
        tempoText->setParent(_currentScore->inputState().segment());
        tempoText->setXmlText(pattern);
        tempoText->setFollowText(true);

        tempoText->setTempo(bpm);

        _currentScore->startCmd();
        _currentScore->undoAddElement(tempoText);
        tempoText->undoSetTempo(bpm);
        tempoText->undoSetFollowText(true);
        _currentScore->endCmd();
    }

    else if (command == "metronome")
    {
        auto action = getAction("metronome");
        if (action)
                action->toggle();
    }

    else if (command == "time-signature")
    {
        bool pw2 = par2 && !(par2 & (par2 - 1));
        if(pw2 && par1) //check if den is power of 2
        {
            TimeSig* ts = new TimeSig(gscore);
            ts->setSig(Fraction(par1, par2), TimeSigType::NORMAL);
            emulateDrop(ts, _currentScore->inputState().segment()->measure());
            getAction("next-chord")->trigger();
            getAction("prev-chord")->trigger();
        }
    }

    else if(!command.isEmpty())
    {
        auto action = getAction(command.toUtf8());
        if(action)
            action->trigger();
        else return;
    }

    QCoreApplication::processEvents();
    collectAndSendStatus();
}

void ODLADriver::accBracket()
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

Element *ODLADriver::searchFromPalette(int paletteType, int cellIdx)
{
    Ms::MuseScore* _museScore = qobject_cast<Ms::MuseScore*>(this->parent());
    PaletteWorkspace* pw = _museScore->getPaletteWorkspace();
    auto tree = pw->masterPaletteModel()->paletteTree();
    for (auto& p : tree->palettes)
        if(int(p->type()) == paletteType)
            return cellIdx < p->ncells() ? p->cell(cellIdx)->element.get()->clone() : nullptr;
    return nullptr;
}

void ODLADriver::emulateDrop(Element *e, Element *target)
{
    EditData& dropData = _scoreView->getEditData();
    dropData.pos         = target->pagePos();
    dropData.dragOffset  = QPointF();
    dropData.dropElement = e;
    _currentScore->startCmd();
    target->drop(dropData);
    _currentScore->endCmd();
}

// Send status to Odla
void ODLADriver::collectAndSendStatus()
{
    // After execute command send an update status to ODLA
    union state_message_t status_message;
    Selection sel = _currentScore->selection();

    int len = status_message.common_fields.msgLen  = sizeof(state_message_t::common_fields_t);
    status_message.common_fields.type = NO_ELEMENT;                         // type (0 for status reply) TODO: craete protocol
    status_message.common_fields.mscoreState = _scoreView->state;// state of input
    status_message.common_fields.selectionState = sel.state();             // type of selection
    status_message.common_fields.selectedElements = sel.elements().size(); // number of elements selected

    if(sel.isSingle()) //if we have only an element selected
    {
        Element* e = sel.element();
        if(e == nullptr) return;
        int bar = 0, beat = 0, num = 0, den = 0;
        getMeasureAndBeat(e, &bar, &beat);
        auto ts = getTimeSig(e);
        if(ts.isValid())
        {
            num = ts.numerator();
            den = ts.denominator();
        }
        len = status_message.common_fields.msgLen  = sizeof(state_message_t::element_fields_t);
        status_message.common_fields.type = SINGLE_ELEMENT;
        status_message.element_fields.elementType   = e->type();                    // Byte 4: type of selection
        status_message.element_fields.notePitch     = getNotePitch(e);               // Byte 5: pitch of note selected
        status_message.element_fields.noteAccident  = getNoteAccident(e);            // Byte 6: accidents of note selected
        status_message.element_fields.duration      = getDuration(e);                // Byte 7: duration of element selected
        status_message.element_fields.dotsNum       = getDots(e);                    // Byte 8: dots of element selected
        status_message.element_fields.measureNum    = bar + 1;                     // Byte 9: measure number LSB of element selected
        status_message.element_fields.beat          = beat + 1;                    // Byte 11: beat of element selected
        status_message.element_fields.staff         = e->staffIdx() + 1;                   // Byte 12: staff of element selected
        status_message.element_fields.clef          = getClef(e);                    // Byte 13: clef of element selected
        status_message.element_fields.timeSignatureNum = num;     // Byte 14: numerator of time signature of element selected
        status_message.element_fields.timeSignatureDen = den;   // Byte 15: denominator of time signature of element selected
        status_message.element_fields.keySignature  = getKeySignature(e);            // Byte 16: keysignature of element selected
        status_message.element_fields.voiceNum      = getVoice(e);                   // Byte 17: voice of element selected
        status_message.element_fields.bpm           = getBPM(e);                       // Byte 19: bpm number LSB of element selected
    }
    else if(sel.isRange()) //if we have more than an element selected
    {
        Segment* startSegment = sel.startSegment();
        if(startSegment == nullptr) return;
        Segment* endSegment = sel.endSegment() ? sel.endSegment()->prev1MM() : _currentScore->lastSegment();
        if(endSegment == nullptr) return;
        int startBar, startBeat, endBar, endBeat;
        getMeasureAndBeat(startSegment, &startBar, &startBeat);
        getMeasureAndBeat(endSegment, &endBar, &endBeat);

        len = status_message.common_fields.msgLen    = sizeof(state_message_t::range_fields_t);
        status_message.common_fields.type = RANGE_ELEMENT;
        status_message.range_fields.firstMesaure    = startBar + 1;
        status_message.range_fields.lastMesaure     = endBar + 1;
        status_message.range_fields.firstBeat       = startBeat + 1;
        status_message.range_fields.lastBeat        = endBeat + 1;
        status_message.range_fields.firstStaff      = sel.elements().first()->staffIdx() + 1;
        status_message.range_fields.lastStaff       = sel.elements().last()->staffIdx() + 1;
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
    qDebug() << "n->tpcUserName()" << n->tpcUserName();
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

// Get beat of an element in a measure
void ODLADriver::getMeasureAndBeat(Ms::Element *e, int *bar, int *beat)
{
    Segment* seg = static_cast<Segment*>(e->findAncestor(ElementType::SEGMENT));
    if(seg == nullptr) { *bar = *beat = 0; return;}
    int dummy = 0;
    TimeSigMap* tsm = e->score()->sigmap();
    tsm->tickValues(seg->tick().ticks(), bar, beat, &dummy);
}

// Get the clef of the context of element
ClefType ODLADriver::getClef(Element *e)
{
    return _currentScore->staff(e->staffIdx())->clef(e->tick());
}

// Get the time signature of the context of element
Fraction ODLADriver::getTimeSig(Element *e)
{
    return _currentScore->staff(e->staffIdx())->timeSig(e->tick())->sig();
}

// Get the Staff Key of the element
Key ODLADriver::getKeySignature(Element *e)
{
    return _currentScore->staff(e->staffIdx())->keySigEvent(e->tick()).key();
}

// Get the voice in which rest o note is placed
quint8 ODLADriver::getVoice(Element *e)
{
    return e->voice() + 1;
}

// Get the BPM setted before element
int ODLADriver::getBPM(Element *e)
{
    return _currentScore->tempo(e->tick()) * 60;
}

/*!
 * \brief ODLADriver::setCurrentScore
 * \param current
 */
void ODLADriver::setCurrentScore(MasterScore* current)
{
    _currentScore = current;
}
} // namespace ODLA
