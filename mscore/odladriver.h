#ifndef ODLADRIVER_H
#define ODLADRIVER_H

#include <QObject>
#include <QLocalSocket>
#include <QMap>
#include "scoreview.h"

enum selection_type_t: char
{
    NO_ELEMENT,
    SINGLE_ELEMENT,
    RANGE_ELEMENT,
};

/*!
 * \brief The ODLADriver class
 */
class ODLADriver : public QObject
{
    Q_OBJECT
public:
    enum SpeechField
    {
        NoteName =      1<<0,
        Accidental =    1<<1,
        Duration =      1<<2,
        Dots =          1<<3,
        Beat =          1<<4,
        Measure =       1<<5,
        Staff =         1<<6,
        TimeSign =       1<<7,
        Clef =          1<<8,
        KeySign =        1<<9,
        Voice =         1<<10,
        BPM =           1<<11
    };
    Q_DECLARE_FLAGS(SpeechFields, SpeechField)
    Q_FLAG(SpeechFields)
    //friend QDataStream &operator>>(QDataStream &, QMap<QString> &);

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
    bool _paused;

    quint8 getNotePitch(Ms::Element *e);
    Ms::AccidentalType getNoteAccident(Ms::Element *e);
    Ms::TDuration::DurationType getDuration(Ms::Element *e);
    quint8 getDots(Ms::Element *e);
    void getMeasureAndBeat(Ms::Element *e, int *bar, int *beat);
    Ms::ClefType getClef(Ms::Element *e);
    Ms::Fraction getTimeSig(Ms::Element *e);
    Ms::Key getKeySignature(Ms::Element *e);
    quint8 getVoice(Ms::Element *e);
    int getBPM(Ms::Element *e);
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

Q_DECLARE_OPERATORS_FOR_FLAGS(ODLADriver::SpeechFields)
#endif // ODLADRIVER_H
