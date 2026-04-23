#include <QApplication>
#include "gui/MainWindow.h"
#include <QDebug>
#include "domain/DeviceModel.h"
#include "domain/ChannelCommandResolver.h"
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    MainWindow window;
    window.show();
    domain::DeviceModel profile;
    profile.channelCount = 8;
    profile.channelPattern.state.stateBaseHex = "0011";
    profile.channelPattern.state.stepHex = "0010";

    profile.channelPattern.current.valueBaseHex = "0012";
    profile.channelPattern.current.maximumBaseHex = "0013";
    profile.channelPattern.current.maximumLimitBaseHex = "0014";
    profile.channelPattern.current.measuredBaseHex = "0015";
    profile.channelPattern.current.protectionBaseHex = "0016";
    profile.channelPattern.current.stepHex = "0010";

    profile.channelPattern.voltage.valueBaseHex = "0017";
    profile.channelPattern.voltage.maximumBaseHex = "0018";
    profile.channelPattern.voltage.maximumLimitBaseHex = "0019";
    profile.channelPattern.voltage.measuredBaseHex = "001A";
    profile.channelPattern.voltage.protectionBaseHex = "001B";
    profile.channelPattern.voltage.stepHex = "0010";

    domain::ChannelCommandResolver resolver(profile);

    qDebug() << "Ch1 state:" << resolver.stateCode(1);
    qDebug() << "Ch2 state:" << resolver.stateCode(2);
    qDebug() << "Ch8 voltage protection:" << resolver.voltageProtectionCode(8);
    return app.exec();
}