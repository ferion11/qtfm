#include "udisks2.h"
#include <QDBusObjectPath>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusInterface>
#include <QXmlStreamReader>
#include <QDebug>
#include <QProcess>
#include <QFile>
#include <QTextStream>

QString uDisks2::getDrivePath(QString path)
{
    QDBusInterface iface(DBUS_SERVICE, path, QString("%1.Block").arg(DBUS_SERVICE), QDBusConnection::systemBus());
    if (!iface.isValid()) { return QString(); }
    QDBusObjectPath drive = iface.property("Drive").value<QDBusObjectPath>();
    return drive.path();
}

bool uDisks2::hasPartition(QString path)
{
    QDBusInterface iface(DBUS_SERVICE, path, QString("%1.Partition").arg(DBUS_SERVICE), QDBusConnection::systemBus());
    if (!iface.isValid()) { return false; }
    if (iface.property("Number").toUInt()>=1) { return true; }
    return false;
}

QString uDisks2::getFileSystem(QString path)
{
    QDBusInterface iface(DBUS_SERVICE, path, QString("%1.Block").arg(DBUS_SERVICE), QDBusConnection::systemBus());
    if (!iface.isValid()) { return QString(); }
    return iface.property("IdType").toString();
}

bool uDisks2::isRemovable(QString path)
{
    QDBusInterface iface(DBUS_SERVICE, path, QString("%1.Drive").arg(DBUS_SERVICE), QDBusConnection::systemBus());
    if (!iface.isValid()) { return false; }
    return iface.property("Removable").toBool();
}

bool uDisks2::isOptical(QString path)
{
    QDBusInterface iface(DBUS_SERVICE, path, QString("%1.Drive").arg(DBUS_SERVICE), QDBusConnection::systemBus());
    if (!iface.isValid()) { return false; }
    QStringList compat = iface.property("MediaCompatibility").toStringList();
    for (int i=0;i<compat.size();i++) {
        if (compat.at(i).startsWith("optical_")) { return true; }
    }
    return false;
}

bool uDisks2::hasMedia(QString path)
{
    QDBusInterface iface(DBUS_SERVICE, path, QString("%1.Drive").arg(DBUS_SERVICE), QDBusConnection::systemBus());
    if (!iface.isValid()) { return false; }
    return iface.property("MediaAvailable").toBool();
}

bool uDisks2::hasOpticalMedia(QString path)
{
    QDBusInterface iface(DBUS_SERVICE, path, QString("%1.Drive").arg(DBUS_SERVICE), QDBusConnection::systemBus());
    if (!iface.isValid()) { return false; }
    QString type = iface.property("Media").toString();
    if (type.startsWith("optical_")) { return true; }
    return false;
}

bool uDisks2::canEject(QString path)
{
    QDBusInterface iface(DBUS_SERVICE, path, QString("%1.Drive").arg(DBUS_SERVICE), QDBusConnection::systemBus());
    if (!iface.isValid()) { return false; }
    return iface.property("Ejectable").toBool();
}

bool uDisks2::opticalMediaIsBlank(QString path)
{
    QDBusInterface iface(DBUS_SERVICE, path, QString("%1.Drive").arg(DBUS_SERVICE), QDBusConnection::systemBus());
    if (!iface.isValid()) { return false; }
    return iface.property("OpticalBlank").toBool();
}

int uDisks2::opticalDataTracks(QString path)
{
    QDBusInterface iface(DBUS_SERVICE, path, QString("%1.Drive").arg(DBUS_SERVICE), QDBusConnection::systemBus());
    if (!iface.isValid()) { return false; }
    return iface.property("OpticalNumDataTracks").toBool();
}

int uDisks2::opticalAudioTracks(QString path)
{
    QDBusInterface iface(DBUS_SERVICE, path, QString("%1.Drive").arg(DBUS_SERVICE), QDBusConnection::systemBus());
    if (!iface.isValid()) { return false; }
    return iface.property("OpticalNumAudioTracks").toBool();
}

QString uDisks2::getMountPointOptical(QString path)
{
    QString mountpoint;
    QString device = path.split("/").takeLast();
    if (device.isEmpty()) { return mountpoint; }
    QFile mtab("/etc/mtab");
    if (!mtab.open(QIODevice::ReadOnly)) { return QString(); }
    QTextStream ts(&mtab);
    QVector<QStringList> result;
    while(!ts.atEnd()) {
        QString line = ts.readLine();
        QStringList info = line.split(" ", QString::SkipEmptyParts);
        if (info.size()>=2) {
            QString dev = info.at(0);
            QString mnt = info.at(1);
            if (dev == QString("/dev/%1").arg(device)) { mountpoint = mnt; }
        }
    }
    mtab.close();
    //qDebug() << "optical mountpoint" << mountpoint;
    return mountpoint;
}

QString uDisks2::getMountPoint(QString path)
{
    QString mountpoint;
    QDBusMessage message = QDBusMessage::createMethodCall(DBUS_SERVICE, path, DBUS_PROPERTIES, "Get");
    QList<QVariant> args;
    args << QString("%1.Filesystem").arg(DBUS_SERVICE) << "MountPoints";
    message.setArguments(args);
    QDBusMessage reply = QDBusConnection::systemBus().call(message);
    QStringList mountpoints;
    QList<QByteArray> argList;
    foreach (QVariant arg, reply.arguments()) {
        if (!arg.value<QDBusVariant>().variant().isValid()) { continue; }
        arg.value<QDBusVariant>().variant().value<QDBusArgument>() >> argList;
    }
    foreach (QByteArray point, argList) { mountpoints.append(point); }
    mountpoint = mountpoints.join("");
    //qDebug() << "mountpoint for path" << path << mountpoint;
    return mountpoint;
}

QString uDisks2::getDeviceName(QString path)
{
    QDBusInterface iface(DBUS_SERVICE, path, QString("%1.Drive").arg(DBUS_SERVICE), QDBusConnection::systemBus());
    if (!iface.isValid()) { return QString(); }
    QString name = iface.property("Vendor").toString().simplified();
    if (!name.isEmpty()) { name.append(" "); }
    name.append(iface.property("Model").toString().simplified());
    return name;
}

QString uDisks2::mountDevice(QString path)
{
    QDBusInterface filesystem(DBUS_SERVICE, path, QString("%1.Filesystem").arg(DBUS_SERVICE), QDBusConnection::systemBus());
    if (!filesystem.isValid()) { return QObject::tr("Failed D-Bus connection."); }
    QVariantMap options;
    if (getFileSystem(path) == "vfat") { options.insert("options", "flush"); }
    QDBusReply<QString> mountpoint =  filesystem.call("Mount", options);
    return mountpoint.error().message();
}

QString uDisks2::mountOptical(QString path)
{
    // something is broken somewhere in udev/udisk, whatever ...
    // So we need to handle opticals using udisks cmd
    // https://bugs.archlinux.org/task/49643
    // https://bugs.freedesktop.org/show_bug.cgi?id=52357
    QProcess proc;
    proc.start(QString("udisks --mount /dev/%1").arg(path.split("/").takeLast()));
    proc.waitForFinished();
    return QString();
}

QString uDisks2::unmountDevice(QString path)
{
    QDBusInterface filesystem(DBUS_SERVICE, path, QString("%1.Filesystem").arg(DBUS_SERVICE), QDBusConnection::systemBus());
    if (!filesystem.isValid()) { return QObject::tr("Failed D-Bus connection."); }
    QDBusMessage reply = filesystem.call("Unmount", QVariantMap());
    return reply.arguments().first().toString();
}

QString uDisks2::unmountOptical(QString path)
{
    // something is broken somewhere in udev/udisk, whatever ...
    // So we need to handle opticals using udisks cmd
    // https://bugs.archlinux.org/task/49643
    // https://bugs.freedesktop.org/show_bug.cgi?id=52357
    QProcess proc;
    proc.start(QString("udisks --unmount /dev/%1").arg(path.split("/").takeLast()));
    proc.waitForFinished();
    return QString();
}

QString uDisks2::ejectDevice(QString path)
{
    QDBusInterface filesystem(DBUS_SERVICE, path, QString("%1.Drive").arg(DBUS_SERVICE), QDBusConnection::systemBus());
    if (!filesystem.isValid()) { return QObject::tr("Failed D-Bus connection."); }
    QDBusMessage reply = filesystem.call("Eject", QVariantMap());
    return reply.arguments().first().toString();
}

QStringList uDisks2::getDevices()
{
    QStringList result;
    QDBusMessage call = QDBusMessage::createMethodCall(DBUS_SERVICE, QString("%1/block_devices").arg(DBUS_PATH), DBUS_INTROSPECTABLE, "Introspect");
    QDBusPendingReply<QString> reply = QDBusConnection::systemBus().call(call);
    QList<QDBusObjectPath> devices;
    QXmlStreamReader xml(reply.value());
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.tokenType() == QXmlStreamReader::StartElement && xml.name().toString() == "node" ) {
            QString name = xml.attributes().value("name").toString();
            if(!name.isEmpty()) { devices << QDBusObjectPath("/org/freedesktop/UDisks2/block_devices/" + name); }
        }
    }
    foreach (QDBusObjectPath device, devices) {
        result << device.path();
    }
    return result;
}
