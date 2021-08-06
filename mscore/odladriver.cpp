#include <QDebug>
#include "odladriver.h"
#include "masterpalette.h"
#include "palette.h"
#include "mscore/palette/paletteworkspace.h"
#include "musescore.h"
#include "libmscore/mscore.h"
#include "libmscore/segment.h"
#include "libmscore/tempotext.h"
#include "libmscore/measure.h"
#include "libmscore/staff.h"
#include "libmscore/utils.h"
#include "libmscore/timesig.h"
#include "libmscore/rest.h"
#include "libmscore/chord.h"
#include "libmscore/accidental.h"
#include "libmscore/keysig.h"
#include "libmscore/part.h"
#include "scoreaccessibility.h"


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
    qDebug() << "mscore pointer" << parent();
    qobject_cast<MuseScore*>(parent())->showMasterPalette(""); //Trick to load palette objects
    qDebug() << "Trick";
}

void ODLADriver::onIncomingData()
{
    if (!_currentScore || !_scoreView)
        return;
    // We can't move Musescore cast in odladriver.h in order to avoiding circular include
    auto _museScore = qobject_cast<Ms::MuseScore*>(this->parent());
    _museScore->setWindowState( (_museScore->windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    _museScore->raise();
    _museScore->activateWindow();    

    QMap<QString, QString> inMessage;
    QByteArray data = _localSocket->readAll();
    QDataStream stream(&data, QIODevice::ReadOnly);
    stream >> inMessage;
    qDebug() << "received from ODLA: " << inMessage;


    QString stateBefore = inMessage["STATE"];
    QString command = inMessage["COM"];
    int par1; bool par1Ok = false;
    int par2; bool par2Ok = false;
    par1 = inMessage["PAR1"].toInt(&par1Ok);
    par2 = inMessage["PAR2"].toInt(&par2Ok);

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
            auto measure_from = _currentScore->firstMeasureMM();

            for(int i = 1; i < from; i++)
                measure_from = measure_from->nextMeasure();

            auto measure_to = measure_from;

            //If we didn't insert an existing end measure we set last measure
            if(_scoreView->searchMeasure(to))
                for(int i = from; i < to; i++)
                   measure_to = measure_to->nextMeasure();
            else
                measure_to = _currentScore->lastMeasureMM();

            _currentScore->select(measure_from, SelectType::RANGE, 0);
            _currentScore->select(measure_to, SelectType::RANGE, _currentScore->nstaves() - 1);
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
            auto currentStaff = _currentScore->staff(_currentScore->inputState().track() / VOICES);
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
        _currentScore->setLayoutMode(LayoutMode::LINE);

    else if (command == "page-view")
        _currentScore->setLayoutMode(LayoutMode::PAGE);

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
            auto ts = new TimeSig(_currentScore);
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
    if(inMessage["SpeechFlags"] != "0")
    {
        QByteArray outData;
        QDataStream outStream(&outData, QIODevice::WriteOnly);
        outStream << speechFeedback(static_cast<SpeechFields>(inMessage["SpeechFlags"].toUInt()));
        if (_localSocket && (_localSocket->state() == QLocalSocket::ConnectedState))
        {
            _localSocket->write(outData);
            _localSocket->flush();
        }
    }
}

void ODLADriver::accBracket()
{
    _currentScore->startCmd();
    for (Element* el : _currentScore->selection().elements())
    {
        if (el->type() == ElementType::NOTE)
        {
            auto acc =  toNote(el)->accidental();
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
QMap<QString, QString> ODLADriver::speechFeedback(ODLADriver::SpeechFields flags)
{
    qDebug() << flags;
    QMap<QString, QString> retVal;
    if(_currentScore->selection().isSingle()) //if we have only an element selected
    {
        Element* e = _currentScore->selection().element();
        if(e == nullptr)
            return retVal;

        if(flags.testFlag(NoteName))
            retVal["NOT"] = getNoteName(e);

        if(flags.testFlag(DurationName))
            retVal["DUR"] = getDuration(e);

        QString measure, beat;
        getMeasureAndBeat(e,measure, beat);

        if(flags.testFlag(MeasureNumber))
            retVal["MEA"] = measure;

        if(flags.testFlag(BeatNumber))
            retVal["BEA"] = beat;

        if(flags.testFlag(StaffNumber))
            retVal["STA"] = getStaff(e);

        if(flags.testFlag(TimeSignFraction))
            retVal["TIM"] = getTimeSig(e);

        if(flags.testFlag(ClefName))
            retVal["CLE"] = getClef(e);

        if(flags.testFlag(KeySignName))
            retVal["KEY"] = getKeySignature(e);

        if(flags.testFlag(VoiceNumber))
            retVal["VOI"] = getVoice(e);

        if(flags.testFlag(BPMNumber))
            retVal["BPM"] = getBPM(e);
    }
    else if(_currentScore->selection().isRange()) //if we have more than an element selected
    {

        Segment* startSegment = _currentScore->selection().startSegment();
        if(startSegment == nullptr) return retVal;
        Segment* endSegment = _currentScore->selection().endSegment() ? _currentScore->selection().endSegment()->prev1MM() : _currentScore->lastSegment();
        if(endSegment == nullptr) return retVal;
        QString startBar, startBeat, endBar, endBeat;
        getMeasureAndBeat(startSegment, startBar, startBeat);
        getMeasureAndBeat(endSegment, endBar, endBeat);

        QStringList ret;
        ret << getStaff(startSegment)
            << startBar
            << startBeat
            << getStaff(endSegment)
            << startBar
            << startBeat;
    }
    return retVal;
}

// Get tone of note
QString ODLADriver::getNoteName(Element *e)
{
    QString retVal;
    if (e->type() == ElementType::NOTE)
        retVal = static_cast<Ms::Note*>(e)->tpcUserName(true);
    else/* if (e->type() == ElementType::REST)*/
        retVal = "rest";
    return retVal;
}

// Get duration if element is a chord or a rest
QString ODLADriver::getDuration(Ms::Element* e)
{
    QString retVal;
    if(e->parent()->type() == ElementType::CHORD)
        return static_cast<Ms::ChordRest*>(e->parent())->durationUserName();
    else if(e->type() == ElementType::REST)
        return static_cast<Ms::ChordRest*>(e)->durationUserName();
    return "";
}

// Get beat of an element in a measure
void ODLADriver::getMeasureAndBeat(Ms::Element *e, QString &measureString, QString &beatString)
{
    int bar, beat, dontCare;
    Segment* seg = static_cast<Segment*>(e->findAncestor(ElementType::SEGMENT));
    if(seg == nullptr)
        return;
    TimeSigMap* tsm = e->score()->sigmap();
    tsm->tickValues(seg->tick().ticks(), &bar, &beat, &dontCare);
    measureString = qApp->translate("Ms::ScoreAccessibility", "Measure: %1").arg(QString::number(bar + 1));
    beatString = qApp->translate("Ms::ScoreAccessibility", "Beat: %1").arg(QString::number(beat + 1));
}

// Get the clef of the context of element
QString ODLADriver::getClef(Element *e)
{
    return qApp->translate("clefTable", ClefInfo::name(_currentScore->staff(e->staffIdx())->clef(e->tick())));
}

// Get the time signature of the context of element
QString ODLADriver::getTimeSig(Element *e)
{
    auto ts = _currentScore->staff(e->staffIdx())->timeSig(e->tick())->sig();
    return QString("%1/%2").arg(ts.numerator()).arg(ts.denominator());
}

// Get the Staff Key of the element
QString ODLADriver::getKeySignature(Element *e)
{
    QString retVal;
    auto k = _currentScore->staff(e->staffIdx())->keySigEvent(e->tick());//.key();
    auto ks = new KeySig(_currentScore);
    ks->setKeySigEvent(k);
    return ks->accessibleInfo().split(": ").last();
}

// Get the voice in which rest o note is placed
QString ODLADriver::getVoice(Element *e)
{
    return QString::number(e->voice() + 1);
}

// Get the BPM setted before element
QString ODLADriver::getBPM(Element *e)
{
    return QString::number(_currentScore->tempo(e->tick()) * 60);
}

QString ODLADriver::getStaff(Element *e)
{
    QString retVal = qApp->translate("Ms::ScoreAccessibility", "Staff: %1").arg(QString::number(e->staffIdx() + 1));
    retVal += " " + e->staff()->part()->longName(e->tick());
    return retVal;
}

/*!
 * \brief ODLADriver::setCurrentScore
 * \param current
 */
void ODLADriver::setCurrentScore(MasterScore* current)
{
    _currentScore = current;
}
