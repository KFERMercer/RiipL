#include <QtTest>

#include "utils/SingleInstance.h"

class TestSingleInstance : public QObject
{
    Q_OBJECT

private slots:
    void arbitratesPrimaryAndSecondary();
    void reportsAnUnreachableInstance();
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

QTEST_MAIN(TestSingleInstance)
#include "tst_singleinstance.moc"
