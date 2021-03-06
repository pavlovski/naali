// For conditions of distribution and use, see copyright notice in license.txt

#ifndef incl_UiModule_NotificationBrowserWidget_h
#define incl_UiModule_NotificationBrowserWidget_h

#include <QGraphicsProxyWidget>

#include "ui_NotificationBrowserWidget.h"

class QPropertyAnimation;

namespace CoreUi
{
    class NotificationBaseWidget;
    class NotificationLogWidget;

    class NotificationBrowserWidget : public QGraphicsProxyWidget, private Ui::NotificationBrowserWidget
    {
        
    Q_OBJECT

    public:
        NotificationBrowserWidget();

    public slots:
        void InsertNotifications(NotificationBaseWidget *notification);
        void ShowNotifications(QList<NotificationBaseWidget *> notifications);

        void ClearAllContent();

        void AnimatedShow();
        void AnimatedHide();

    protected:
        void showEvent(QShowEvent *show_event);

    private slots:
        void MoveActiveToLog(QWidget *active_widget, QString result_title, QString result);
        void LayoutCheck();
        void TabCheck();
        void AnimationsFinished();

    private:
        QWidget *internal_widget_;
        QPropertyAnimation *visibility_animation_;

        QList<NotificationLogWidget *> notification_log_widgets_;
        QList<NotificationBaseWidget *> cleanup_list_;

        QString next_bg_color_;
    };
}

#endif