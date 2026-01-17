/********************************************************************************
** Form generated from reading UI file 'toolform.ui'
**
** Created by: Qt User Interface Compiler version 6.4.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_TOOLFORM_H
#define UI_TOOLFORM_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_ToolForm
{
public:
    QVBoxLayout *verticalLayout;
    QPushButton *groupControlBtn;
    QPushButton *fullScreenBtn;
    QSpacerItem *verticalSpacer;
    QPushButton *expandNotifyBtn;
    QPushButton *touchBtn;
    QPushButton *openScreenBtn;
    QPushButton *closeScreenBtn;
    QPushButton *powerBtn;
    QPushButton *volumeUpBtn;
    QPushButton *volumeDownBtn;
    QSlider *volumeSlider;
    QPushButton *appSwitchBtn;
    QPushButton *menuBtn;
    QPushButton *homeBtn;
    QPushButton *returnBtn;
    QPushButton *screenShotBtn;

    void setupUi(QWidget *ToolForm)
    {
        if (ToolForm->objectName().isEmpty())
            ToolForm->setObjectName("ToolForm");
        ToolForm->resize(63, 537);
        ToolForm->setStyleSheet(QString::fromUtf8(""));
        verticalLayout = new QVBoxLayout(ToolForm);
        verticalLayout->setObjectName("verticalLayout");
        verticalLayout->setContentsMargins(-1, 30, -1, -1);
        groupControlBtn = new QPushButton(ToolForm);
        groupControlBtn->setObjectName("groupControlBtn");

        verticalLayout->addWidget(groupControlBtn);

        fullScreenBtn = new QPushButton(ToolForm);
        fullScreenBtn->setObjectName("fullScreenBtn");

        verticalLayout->addWidget(fullScreenBtn);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayout->addItem(verticalSpacer);

        expandNotifyBtn = new QPushButton(ToolForm);
        expandNotifyBtn->setObjectName("expandNotifyBtn");

        verticalLayout->addWidget(expandNotifyBtn);

        touchBtn = new QPushButton(ToolForm);
        touchBtn->setObjectName("touchBtn");

        verticalLayout->addWidget(touchBtn);

        openScreenBtn = new QPushButton(ToolForm);
        openScreenBtn->setObjectName("openScreenBtn");

        verticalLayout->addWidget(openScreenBtn);

        closeScreenBtn = new QPushButton(ToolForm);
        closeScreenBtn->setObjectName("closeScreenBtn");

        verticalLayout->addWidget(closeScreenBtn);

        powerBtn = new QPushButton(ToolForm);
        powerBtn->setObjectName("powerBtn");

        verticalLayout->addWidget(powerBtn);

        volumeUpBtn = new QPushButton(ToolForm);
        volumeUpBtn->setObjectName("volumeUpBtn");

        verticalLayout->addWidget(volumeUpBtn);

        volumeDownBtn = new QPushButton(ToolForm);
        volumeDownBtn->setObjectName("volumeDownBtn");

        verticalLayout->addWidget(volumeDownBtn);

        volumeSlider = new QSlider(ToolForm);
        volumeSlider->setObjectName("volumeSlider");
        volumeSlider->setMinimum(0);
        volumeSlider->setMaximum(100);
        volumeSlider->setValue(50);
        volumeSlider->setOrientation(Qt::Horizontal);

        verticalLayout->addWidget(volumeSlider);

        appSwitchBtn = new QPushButton(ToolForm);
        appSwitchBtn->setObjectName("appSwitchBtn");

        verticalLayout->addWidget(appSwitchBtn);

        menuBtn = new QPushButton(ToolForm);
        menuBtn->setObjectName("menuBtn");

        verticalLayout->addWidget(menuBtn);

        homeBtn = new QPushButton(ToolForm);
        homeBtn->setObjectName("homeBtn");

        verticalLayout->addWidget(homeBtn);

        returnBtn = new QPushButton(ToolForm);
        returnBtn->setObjectName("returnBtn");

        verticalLayout->addWidget(returnBtn);

        screenShotBtn = new QPushButton(ToolForm);
        screenShotBtn->setObjectName("screenShotBtn");

        verticalLayout->addWidget(screenShotBtn);


        retranslateUi(ToolForm);

        QMetaObject::connectSlotsByName(ToolForm);
    } // setupUi

    void retranslateUi(QWidget *ToolForm)
    {
        ToolForm->setWindowTitle(QCoreApplication::translate("ToolForm", "Tool", nullptr));
#if QT_CONFIG(tooltip)
        groupControlBtn->setToolTip(QCoreApplication::translate("ToolForm", "group control", nullptr));
#endif // QT_CONFIG(tooltip)
        groupControlBtn->setText(QString());
#if QT_CONFIG(tooltip)
        fullScreenBtn->setToolTip(QCoreApplication::translate("ToolForm", "full screen", nullptr));
#endif // QT_CONFIG(tooltip)
        fullScreenBtn->setText(QString());
#if QT_CONFIG(tooltip)
        expandNotifyBtn->setToolTip(QCoreApplication::translate("ToolForm", "expand notify", nullptr));
#endif // QT_CONFIG(tooltip)
        expandNotifyBtn->setText(QString());
#if QT_CONFIG(tooltip)
        touchBtn->setToolTip(QCoreApplication::translate("ToolForm", "touch switch", nullptr));
#endif // QT_CONFIG(tooltip)
        touchBtn->setText(QString());
#if QT_CONFIG(tooltip)
        openScreenBtn->setToolTip(QCoreApplication::translate("ToolForm", "open screen", nullptr));
#endif // QT_CONFIG(tooltip)
        openScreenBtn->setText(QString());
#if QT_CONFIG(tooltip)
        closeScreenBtn->setToolTip(QCoreApplication::translate("ToolForm", "close screen", nullptr));
#endif // QT_CONFIG(tooltip)
        closeScreenBtn->setText(QString());
#if QT_CONFIG(tooltip)
        powerBtn->setToolTip(QCoreApplication::translate("ToolForm", "power", nullptr));
#endif // QT_CONFIG(tooltip)
        powerBtn->setText(QString());
#if QT_CONFIG(tooltip)
        volumeUpBtn->setToolTip(QCoreApplication::translate("ToolForm", "volume up", nullptr));
#endif // QT_CONFIG(tooltip)
        volumeUpBtn->setText(QString());
#if QT_CONFIG(tooltip)
        volumeDownBtn->setToolTip(QCoreApplication::translate("ToolForm", "volume down", nullptr));
#endif // QT_CONFIG(tooltip)
        volumeDownBtn->setText(QString());
#if QT_CONFIG(tooltip)
        volumeSlider->setToolTip(QCoreApplication::translate("ToolForm", "Audio Volume", nullptr));
#endif // QT_CONFIG(tooltip)
#if QT_CONFIG(tooltip)
        appSwitchBtn->setToolTip(QCoreApplication::translate("ToolForm", "app switch", nullptr));
#endif // QT_CONFIG(tooltip)
        appSwitchBtn->setText(QString());
#if QT_CONFIG(tooltip)
        menuBtn->setToolTip(QCoreApplication::translate("ToolForm", "menu", nullptr));
#endif // QT_CONFIG(tooltip)
        menuBtn->setText(QString());
#if QT_CONFIG(tooltip)
        homeBtn->setToolTip(QCoreApplication::translate("ToolForm", "home", nullptr));
#endif // QT_CONFIG(tooltip)
        homeBtn->setText(QString());
#if QT_CONFIG(tooltip)
        returnBtn->setToolTip(QCoreApplication::translate("ToolForm", "return", nullptr));
#endif // QT_CONFIG(tooltip)
        returnBtn->setText(QString());
#if QT_CONFIG(tooltip)
        screenShotBtn->setToolTip(QCoreApplication::translate("ToolForm", "screen shot", nullptr));
#endif // QT_CONFIG(tooltip)
        screenShotBtn->setText(QString());
    } // retranslateUi

};

namespace Ui {
    class ToolForm: public Ui_ToolForm {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_TOOLFORM_H
