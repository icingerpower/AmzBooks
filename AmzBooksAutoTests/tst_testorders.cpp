#include <QtTest>
#include <QCoreApplication>

// add necessary includes here

class TestOrders : public QObject
{
    Q_OBJECT

public:
    TestOrders();
    ~TestOrders();

private slots:
    void initTestCase();
    void cleanupTestCase();
    void test_case1();

};

TestOrders::TestOrders()
{

}

TestOrders::~TestOrders()
{

}

void TestOrders::initTestCase()
{

}

void TestOrders::cleanupTestCase()
{

}

void TestOrders::test_case1()
{

}

QTEST_MAIN(TestOrders)

#include "tst_testorders.moc"
