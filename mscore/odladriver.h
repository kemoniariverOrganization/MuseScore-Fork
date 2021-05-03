#ifndef ODLADRIVER_H
#define ODLADRIVER_H

#include <QObject>
#include <QLocalSocket>
#include "scoreview.h"
#include "libmscore/spanner.h"
#include "libmscore/select.h"
#include <QTreeWidgetItem>
#include <mscore/palette/palettetree.h>
#include <mscore/palette/paletteworkspace.h>

namespace ODLA {

enum command_type_t: uint8_t
{
    MS_SHORTCUT,
    PALETTE,
    INSERT_MEASURE,
    GOTO,
    SELECT,
    STAFF_PRESSED,
    LINEWVIEW,
    PAGEVIEW,
    TEMPO,
    METRNOME,
    PLAY,
    PAUSE,
    STOP,
    TIMESIGNATURE,
};

struct struct_command_t
{
    Ms::ViewState stateBefore;
    Ms::ViewState stateAfter;
    command_type_t command;
    int par1;
    int par2;
    QString string;
};

enum selection_type_t: char
{
    NO_ELEMENT,
    SINGLE_ELEMENT,
    RANGE_ELEMENT,
};

union state_message_t
{
    struct common_fields_t
    {
        quint8 msgLen;
        selection_type_t type;
        Ms::ViewState mscoreState;
        Ms::SelState selectionState;
        int selectedElements;
    } common_fields;

    struct element_fields_t
    {
        struct common_fields_t common_fields;
        Ms::ElementType elementType;
        quint8 notePitch;
        Ms::AccidentalType noteAccident;
        Ms::TDuration::DurationType duration;
        quint8 dotsNum;
        int measureNum;
        quint8 beat;
        quint8 staff;
        Ms::ClefType clef;
        quint8 timeSignatureNum;
        quint8 timeSignatureDen;
        Ms::Key keySignature;
        quint8 voiceNum;
        int bpm;
    } element_fields;

    struct range_fields_t
    {
        struct common_fields_t common_fields;
        int firstMesaure;
        int lastMesaure;
        quint8 firstBeat;
        quint8 lastBeat;
        quint8 firstStaff;
        quint8 lastStaff;
    } range_fields;

    char data[sizeof(element_fields_t)];
};

/*!
 * \brief The ODLADriver class
 */
class ODLADriver : public QObject
{
    Q_OBJECT
public:
    friend QDataStream &operator>>(QDataStream &, struct_command_t &);

    static ODLADriver* instance(QObject* parent = nullptr);
    void setScoreView(Ms::ScoreView* scoreView) {_scoreView = scoreView;};

private:
    ODLADriver(QObject *parent = nullptr);
    static ODLADriver * _instance;
    QTranslator _localTranslator;
    QLocalSocket* _localSocket;
    Ms::MasterScore* _currentScore;
    Ms::ScoreView* _scoreView;
    QTreeWidget * _palette;
    bool _editingChord;
    QMap<QString, Ms::Element*> _paletteItemList;

    quint8 getNotePitch(Ms::Element *e);
    Ms::AccidentalType getNoteAccident(Ms::Element *e);
    Ms::TDuration::DurationType getDuration(Ms::Element *e);
    quint8 getDots(Ms::Element *e);
    int getMeasureNumber(Ms::Element *e);
    quint8 getBeat(Ms::Element *e);
    quint8 getStaff(Ms::Element *e);
    Ms::ClefType getClef(Ms::Element *e);
    Ms::Fraction getTimeSig(Ms::Element *e);
    Ms::Key getKeySignature(Ms::Element *e);
    quint8 getVoice(Ms::Element *e);
    int getBPM(Ms::Element *e);
    Ms::Element* findElementBefore(Ms::Element *el, Ms::ElementType type, int staffIdx = -1);
    Ms::Element *searchFromPalette(int paletteType, int cellIdx);
    void emulateDrop(Ms::Element *e, Ms::Element *target);
    QTimer *_reconnectTimer;
    void accBracket();

public slots:
    void attemptConnection();
    void setCurrentScore(Ms::MasterScore* current);

protected slots:
    void onConnected();
    void onIncomingData();
    void collectAndSendStatus();
};

} // namespace ODLA

#endif // ODLADRIVER_H
