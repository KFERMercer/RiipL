#include <QtTest>

#include <QDir>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "utils/SingleInstance.h"

class TestSingleInstance : public QObject
{
    Q_OBJECT

private slots:
    void arbitratesPrimaryAndSecondary();
    void reportsAnUnreachableInstance();
#ifdef Q_OS_UNIX
    void keepsItsFilesInTheRuntimeDirectory();
#endif
};

void TestSingleInstance::arbitratesPrimaryAndSecondary()
{
    {
        SingleInstance first;
        QCOMPARE(first.tryLock(), SingleInstance::Role::Primary);
        QVERIFY(first.isPrimary());

        {
            SingleInstance second;
            QCOMPARE(second.tryLock(), SingleInstance::Role::Secondary);
            QVERIFY(!second.isPrimary());

            QSignalSpy activated(&first, &SingleInstance::activationRequested);
            QVERIFY(second.notifyExistingInstance());
            QVERIFY(activated.wait(2000));
            QCOMPARE(activated.count(), 1);
        }
    }

    // A fresh launch takes over once the previous primary released the lock,
    // which also covers takeover after a crashed instance.
    SingleInstance third;
    QCOMPARE(third.tryLock(), SingleInstance::Role::Primary);
    QVERIFY(third.isPrimary());
}

// A lock held by something that does not answer is reported.
void TestSingleInstance::reportsAnUnreachableInstance()
{
    SingleInstance instance;
    QVERIFY(!instance.notifyExistingInstance());
}

#ifdef Q_OS_UNIX
void TestSingleInstance::keepsItsFilesInTheRuntimeDirectory()
{
    QTemporaryDir runtime;
    QVERIFY(runtime.isValid());
    const QByteArray previous = qgetenv("XDG_RUNTIME_DIR");
    qputenv("XDG_RUNTIME_DIR", QFile::encodeName(runtime.path()));

    SingleInstance instance;
    const SingleInstance::Role role = instance.tryLock();
    const QStringList entries = QDir(runtime.path()).entryList(QDir::Files | QDir::System);
    qputenv("XDG_RUNTIME_DIR", previous);

    QCOMPARE(role, SingleInstance::Role::Primary);
    QCOMPARE(entries.size(), 2);
}
#endif

QTEST_MAIN(TestSingleInstance)
#include "tst_singleinstance.moc"
