#include "QtInputService.h"

#include "ServiceManager.h"
#include "InputEvents.h"
#include "Framework.h"
#include "EventManager.h"
#include "QtInputModule.h"
//#include "RenderServiceInterface.h"
//#include "Renderer.h"

#include <QGraphicsItem>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QApplication>

#include <boost/make_shared.hpp>

QtInputService::QtInputService(Foundation::Framework *framework_)
:lastMouseX(0),
lastMouseY(0),
mouseCursorVisible(true),
//sceneMouseCapture(NoMouseCapture),
mouseFPSModeEnterX(0),
mouseFPSModeEnterY(0),
topLevelInputContext("TopLevel"),
inputCategory(0),
heldMouseButtons(0),
pressedMouseButtons(0),
releasedMouseButtons(0),
newMouseButtonsPressedQueue(0),
newMouseButtonsReleasedQueue(0),
mainView(0),
mainWindow(0),
framework(framework_)
{
    eventManager = framework_->GetEventManager();
    assert(eventManager);

    // We still need to register this for legacy reasons, but shouldn't have to.
    // The 'Input' category should be removed and replaced with 'RexInput' or something
    // similar that is world logic -centric.
    inputCategory = eventManager->RegisterEventCategory("Input");

    inputCategory = eventManager->RegisterEventCategory("SceneInput");

	eventManager->RegisterEvent(inputCategory, QtInputEvents::KeyPressed, "KeyPressed");
    eventManager->RegisterEvent(inputCategory, QtInputEvents::KeyReleased, "KeyReleased");

    eventManager->RegisterEvent(inputCategory, QtInputEvents::MousePressed, "MousePressed");
    eventManager->RegisterEvent(inputCategory, QtInputEvents::MouseReleased, "MouseReleased");
    eventManager->RegisterEvent(inputCategory, QtInputEvents::MouseClicked, "MouseClicked");
    eventManager->RegisterEvent(inputCategory, QtInputEvents::MouseDoubleClicked, "MouseDoubleClicked");
    eventManager->RegisterEvent(inputCategory, QtInputEvents::MouseMove, "MouseMove");

    mainView = framework_->GetUIView();
    assert(mainView);
//    mainView->setMouseTracking(true);
    mainView->installEventFilter(this);
//    std::cout << "Installed tracking to " << (unsigned long)mainView << std::endl;

    assert(mainView->viewport());
    mainView->viewport()->installEventFilter(this);

    // Find the top-level widget that the QGraphicsView is contained in. We need 
    // to track mouse move events from that window.
    mainWindow = mainView;
    /*
    while(mainWindow->parentWidget() && mainWindow->parentWidget()->parentWidget())
        mainWindow = mainWindow->parentWidget();

    mainWindow->setMouseTracking(true);
    mainWindow->installEventFilter(this);
    */

    while(mainWindow->parentWidget())
    {
        mainWindow = mainWindow->parentWidget();
        mainWindow->setMouseTracking(true);
        mainWindow->installEventFilter(this);
        std::cout << "Installed tracking to " << (unsigned long)mainWindow << std::endl;
    }
}

QGraphicsItem *QtInputService::GetVisibleItemAtCoords(int x, int y)
{
    // Silently just ignore any invalid coordinates we get. (and we do get them, it seems!)
    if (x < 0 || y < 0 || x >= mainView->width() || y >= mainView->height())
		return 0;
/*
    boost::shared_ptr<Foundation::RenderServiceInterface> renderer = 
        framework_->GetServiceManager()->GetService<Foundation::RenderServiceInterface>(Foundation::Service::ST_Renderer).lock();

	if (!renderer.get())
	{
		LogWarning("QtInputService::GetVisibleItemAtCoords: Could not find RenderServiceInterface!");
		return 0;
	}

    OgreRenderer::Renderer *ogreRenderer = dynamic_cast<OgreRenderer::Renderer *>(renderer.get());
	assert(ogreRenderer);
	if (!ogreRenderer)
		return 0;
*/
	QGraphicsItem *itemUnderMouse = 0;
    ///\bug Not sure if this function returns the items in the proper depth order! We might not get the topmost window
    /// when this loop finishes.
    QList<QGraphicsItem *> items = framework->GetUIView()->items(x, y);
    for(int i = 0; i < items.size(); ++i)
        if (items[i]->isVisible())
		{
			itemUnderMouse = items[i];
			break;
		}    

	if (!itemUnderMouse)
		return 0;

    return 0;
/* ///\todo Enable.
	// Do alpha keying: If we have clicked on a transparent part of a widget, act as if we didn't click on a widget at all.
    // This allows clicks to go through to the 3D scene from transparent parts of a widget.
	QImage &backBuffer = ogreRenderer->GetBackBuffer();
    if (x < backBuffer.width() && y < backBuffer.height() && (backBuffer.pixel(x, y) & 0xFF000000) == 0x00000000)
		itemUnderMouse = 0;
*/
	return itemUnderMouse;
}

void QtInputService::SetMouseCursorVisible(bool visible)
{
    if (mouseCursorVisible == visible)
        return;

    mouseCursorVisible = visible;
    if (mouseCursorVisible)
    {
        // We're showing the previously hidden mouse cursor. Restore the mouse cursor to the position where it
        // was when mouse was hidden.
        QApplication::restoreOverrideCursor();
        QCursor::setPos(mouseFPSModeEnterX, mouseFPSModeEnterY);
    }
    else
    {
        // Hide the mouse cursor and save up the coordinates where the mouse cursor was hidden.
        QApplication::setOverrideCursor(QCursor(Qt::BlankCursor));
        mouseFPSModeEnterX = QCursor::pos().x();
        mouseFPSModeEnterY = QCursor::pos().y();
    }
}

bool QtInputService::IsMouseCursorVisible() const
{ 
    return mouseCursorVisible;
}

bool QtInputService::IsKeyDown(Qt::Key keyCode) const
{
    return heldKeys.find(keyCode) != heldKeys.end();
}

bool QtInputService::IsKeyPressed(Qt::Key keyCode) const
{
    return std::find(pressedKeys.begin(), pressedKeys.end(), keyCode) != pressedKeys.end();
}

bool QtInputService::IsKeyReleased(Qt::Key keyCode) const
{
    return std::find(releasedKeys.begin(), releasedKeys.end(), keyCode) != releasedKeys.end();
}

bool QtInputService::IsMouseButtonDown(int mouseButton) const
{
    assert((mouseButton & (mouseButton-1)) == 0); // Must only contain a single '1' bit.

    return (heldMouseButtons & mouseButton) != 0;
}

bool QtInputService::IsMouseButtonPressed(int mouseButton) const
{
    assert((mouseButton & (mouseButton-1)) == 0); // Must only contain a single '1' bit.

    return (pressedMouseButtons & mouseButton) != 0;
}

bool QtInputService::IsMouseButtonReleased(int mouseButton) const
{
    assert((mouseButton & (mouseButton-1)) == 0); // Must only contain a single '1' bit.

    return (releasedMouseButtons & mouseButton) != 0;
}

QPoint QtInputService::MousePressedPos(int mouseButton) const
{
    return mousePressPositions.Pos(mouseButton);
}

boost::shared_ptr<InputContext> QtInputService::RegisterInputContext(const char *name, int priority)
{
    boost::shared_ptr<InputContext> inputContext = boost::make_shared<InputContext>(name);

    ///\todo Store sorted by priority.
    registeredInputContexts.push_back(boost::weak_ptr<InputContext>(inputContext));

    return inputContext;
}

void QtInputService::SceneReleaseAllKeys()
{
    for(InputContextList::iterator iter = registeredInputContexts.begin(); iter != registeredInputContexts.end(); ++iter)
    {
        boost::shared_ptr<InputContext> inputContext = iter->lock();
        if (inputContext)
            inputContext->ReleaseAllKeys();
    }

/*
    for(std::map<Qt::Key, KeyPressInformation>::iterator iter = heldKeys.begin(); iter != heldKeys.end(); ++iter)
    {
        // We send a very bare-bone release message here that might not reflect reality, i.e.
        // the modifiers might not match the current one, etc.
        // Therefore, you should rely on these information at the key press time, instead of the
        // release time.
		KeyEvent keyEvent;
		keyEvent.keyCode = iter->first;
		keyEvent.keyPressCount = 0;
		keyEvent.modifiers = 0;
		keyEvent.text = "";
		keyEvent.eventType = KeyEvent::KeyReleased;

        TriggerKeyEvent(keyEvent);
    }
*/
    // Now all keys are released from the inworld scene.
//    heldKeys.clear();
}

void QtInputService::SceneReleaseMouseButtons()
{
    for(int i = 1; i < MouseEvent::MaxButtonMask; i <<= 1)
        if ((heldMouseButtons & i) != 0)
        {
            // Just like with key release events, we send a very bare-bone release message here as well.
		    MouseEvent mouseEvent;
		    mouseEvent.eventType = MouseEvent::MouseReleased;
            mouseEvent.button = (MouseEvent::MouseButton)i;
            mouseEvent.x = lastMouseX;
		    mouseEvent.y = lastMouseY;
		    mouseEvent.z = 0;
		    mouseEvent.relativeX = 0;
		    mouseEvent.relativeY = 0;

		    mouseEvent.globalX = 0;
		    mouseEvent.globalY = 0;

		    mouseEvent.otherButtons = 0;

		    eventManager->SendEvent(inputCategory, QtInputEvents::MouseReleased, &mouseEvent);
        }

    // Now all the mouse buttons are released from the inworld scene.
//    heldMouseButtons = 0;
}

/// \bug Due to not being able to restrict the mouse cursor to the window client are in any cross-platform means,
/// it is possible that if the screen is resized to very small and if the mouse is moved very fast, the cursor
/// escapes the window client area and will not get recentered.
void QtInputService::RecenterMouse()
{
    QGraphicsView *view = framework->GetUIView();
    QPoint centeredCursorPosLocal = QPoint(view->size().width()/2, view->size().height()/2);
    
    lastMouseX = centeredCursorPosLocal.x();
    lastMouseY = centeredCursorPosLocal.y();
    // This call might trigger an immediate mouse move message to the window, so set the mouse coordinates above.
    QPoint centeredCursorPosGlobal = view->mapToGlobal(centeredCursorPosLocal);
    if (centeredCursorPosGlobal == QCursor::pos())
        return; // If the mouse cursor already is at the center, don't do anything.
    QCursor::setPos(centeredCursorPosGlobal);

    // Double-check that the mouse cursor did end up where we wanted it to go.
    QPoint mousePos = view->mapFromGlobal(QCursor::pos());
    lastMouseX = mousePos.x();
    lastMouseY = mousePos.y();
}

void QtInputService::PruneDeadInputContexts()
{
    InputContextList::iterator iter = registeredInputContexts.begin();

    while(iter != registeredInputContexts.end())
    {
        if (iter->expired())
            iter = registeredInputContexts.erase(iter);
        else
            ++iter;
    }
}

void QtInputService::TriggerSceneKeyReleaseEvent(InputContextList::iterator start, Qt::Key keyCode)
{
    for(; start != registeredInputContexts.end(); ++start)
    {
        boost::shared_ptr<InputContext> context = start->lock();
        if (context.get())
            context->TriggerKeyReleaseEvent(keyCode);
    }

    if (heldKeys.find(keyCode) != heldKeys.end())
    {
		KeyEvent keyEvent;
		keyEvent.keyCode = keyCode;
		keyEvent.eventType = KeyEvent::KeyReleased;

        eventManager->SendEvent(inputCategory, QtInputEvents::KeyReleased, &keyEvent);
    }
}

void QtInputService::TriggerKeyEvent(KeyEvent &key)
{
    assert(key.eventType != KeyEvent::KeyEventInvalid);
    assert(key.handled == false);

    // First, we pass the key to the global top level input context, which operates above Qt widget input.
    topLevelInputContext.TriggerKeyEvent(key);
    if (key.handled)
        key.eventType = KeyEvent::KeyReleased;

    // If a widget in the QGraphicsScene has keyboard focus, don't send the keyboard message to the inworld scene (the lower contexts).
    if (mainView->scene()->focusItem())
        key.eventType = KeyEvent::KeyReleased;

    // Pass the event to all input contexts in the priority order.
    for(InputContextList::iterator iter = registeredInputContexts.begin();
        iter != registeredInputContexts.end(); ++iter)
    {
        boost::shared_ptr<InputContext> context = iter->lock();
        if (context.get())
            context->TriggerKeyEvent(key);
        if (key.handled)
        {
//            ++iter;
            key.eventType = KeyEvent::KeyReleased;
//            TriggerSceneKeyReleaseEvent(iter, key.keyCode);
//            break;
        }
    }

//    if (key.handled)
//        return;

    ///\todo Key releases might not get here if a context above it has suppressed it.

    // Finally, pass the key event to the system event tree.
    ///\todo Track which presses and releases have been passed to the event tree, and filter redundant releases.
    switch(key.eventType)
    {
    case KeyEvent::KeyPressed: 
        eventManager->SendEvent(inputCategory, QtInputEvents::KeyPressed, &key); 
        break;
    case KeyEvent::KeyReleased: 
        eventManager->SendEvent(inputCategory, QtInputEvents::KeyReleased, &key); 
        break;
// KeyDown events are not sent through the event tree. You should favor an input context for this.
//        case KeyEvent::KeyDown: eventManager->SendEvent(inputCategory, QtInputEvents::KeyDown, &keyEvent); break;
    default:
        assert(false);
        break;
    }
}

void QtInputService::TriggerMouseEvent(MouseEvent &mouse)
{
    assert(mouse.handled == false);

    // Remember where this press occurred, for tracking drag situations.
    if (mouse.eventType == MouseEvent::MousePressed)
        mousePressPositions.Set(mouse.button, mouse.x, mouse.y, mouse.origin);

    // Copy over the set of tracked mouse press positions into the event structure, so that 
    // the client can do proper drag tracking.
    mouse.mousePressPositions = mousePressPositions;

    // First, we pass the event to the global top level input context, which operates above Qt widget input.
    topLevelInputContext.TriggerMouseEvent(mouse);
    if (mouse.handled)
        return;

    // Pass the event to all input contexts in the priority order.
    for(InputContextList::iterator iter = registeredInputContexts.begin();
        iter != registeredInputContexts.end(); ++iter)
    {
        if (mouse.handled)
            break;
        boost::shared_ptr<InputContext> context = iter->lock();
        if (context.get())
            context->TriggerMouseEvent(mouse);
    }

    if (!mouse.handled)
    {
        switch(mouse.eventType)
        {
        case MouseEvent::MousePressed:
            eventManager->SendEvent(inputCategory, QtInputEvents::MousePressed, &mouse);
            break;
        case MouseEvent::MouseReleased:
            eventManager->SendEvent(inputCategory, QtInputEvents::MouseReleased, &mouse);
            break;
        case MouseEvent::MouseMove:
            eventManager->SendEvent(inputCategory, QtInputEvents::MouseMove, &mouse);
            break;
        case MouseEvent::MouseScroll:
    		eventManager->SendEvent(inputCategory, QtInputEvents::MouseScroll, &mouse);
            break;
        default:
            assert(false);
            break;
        }
    }
}

bool QtInputService::eventFilter(QObject *obj, QEvent *event)
{
    switch(event->type())
    {
    case QEvent::KeyPress:
	{
        QKeyEvent *e = static_cast<QKeyEvent*>(event);

		KeyEvent keyEvent;
		keyEvent.keyCode = (Qt::Key)e->key();
        keyEvent.keyPressCount = 1;
		keyEvent.modifiers = e->modifiers();
		keyEvent.text = e->text();
		keyEvent.eventType = KeyEvent::KeyPressed;
//		keyEvent.otherHeldKeys = heldKeys; ///\todo
        keyEvent.handled = false;

        std::map<Qt::Key, KeyPressInformation>::iterator keyRecord = heldKeys.find(keyEvent.keyCode);
        if (keyRecord != heldKeys.end())
        {
            if (e->isAutoRepeat()) // If this is a repeat, track the proper keyPressCount.
                keyEvent.keyPressCount = ++keyRecord->second.keyPressCount;
        }
        else
        {
            KeyPressInformation info;
            info.keyPressCount = 1;
            info.keyState = KeyEvent::KeyPressed;
//            info.firstPressTime = now; ///\todo
            heldKeys[keyEvent.keyCode] = info;
        }

        // Queue up the press event for the polling API, independent of whether any Qt widget has keyboard focus.
        if (keyEvent.keyPressCount == 1) /// \todo The polling API does not get key repeats at all. Should it?
            newKeysPressedQueue.push_back((Qt::Key)e->key());

        TriggerKeyEvent(keyEvent);

        return keyEvent.handled; // If we got true here, need to suppress this event from going to Qt.
	}

    case QEvent::KeyRelease:
    {
        QKeyEvent *e = static_cast<QKeyEvent *>(event);
/*
        // If a widget in the QGraphicsScene has keyboard focus, send release messages for each
        // previous press message we've sent to inworld scene (i.e. release all keys from scene).
        if (mainView->scene()->focusItem())
        {
            ReleaseAllKeys();
			return false;
        }
*/
        // Our input system policy: Key releases on repeated keys are not transmitted. This means
        // that the client gets always a sequences like "press (first), press(1st repeat), press(2nd repeat), release",
        // instead of "press(first), release, press(1st repeat), release, press(2nd repeat), release".
        if (e->isAutoRepeat())
            return false;

        HeldKeysMap::iterator existingKey = heldKeys.find((Qt::Key)e->key());

		// If we received a release on an unknown key we haven't received a press for, don't pass it to the scene,
        // since we didn't pass the press to the scene either (or we've already passed the release before, so don't 
        // pass duplicate releases).
		if (existingKey == heldKeys.end())
			return false;

		KeyEvent keyEvent;
		keyEvent.keyCode = (Qt::Key)e->key();
		keyEvent.keyPressCount = existingKey->second.keyPressCount;
		keyEvent.modifiers = e->modifiers();
		keyEvent.text = e->text();
		keyEvent.eventType = KeyEvent::KeyReleased;
//		keyEvent.otherHeldKeys = heldKeys; ///\todo
        keyEvent.handled = false;

		heldKeys.erase(existingKey);

        // Queue up the release event for the polling API, independent of whether any Qt widget has keyboard focus.
        if (keyEvent.keyPressCount == 1) /// \todo The polling API does not get key repeats at all. Should it?
            newKeysReleasedQueue.push_back((Qt::Key)e->key());

        TriggerKeyEvent(keyEvent);
        
        return keyEvent.handled;//true; // Suppress this event from going forward.
    }

    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    {
        QMouseEvent *e = static_cast<QMouseEvent *>(event);
		QGraphicsItem *itemUnderMouse = GetVisibleItemAtCoords(e->x(), e->y());
/*
        // Update the flag that tracks whether the inworld scene or QGraphicsScene is grabbing mouse movement.
        if (event->type() == QEvent::MouseButtonPress)
            sceneMouseCapture = (itemUnderMouse ? QtMouseCapture : SceneMouseCapture);
        else // event type == MouseButtonRelease
            sceneMouseCapture = NoMouseCapture; 
*/
        // We always update the global polled input states, independent of whether any the mouse cursor is
        // on top of any Qt widget.
        if (event->type() == QEvent::MouseButtonPress)
        {
            heldMouseButtons |= (MouseEvent::MouseButton)e->button();
            newMouseButtonsPressedQueue |= (MouseEvent::MouseButton)e->button();
        }
        else
        {
            heldMouseButtons &= ~(MouseEvent::MouseButton)e->button();
            newMouseButtonsReleasedQueue |= (MouseEvent::MouseButton)e->button();
        }

        // If there's a visible QGraphicsItem under the mouse and mouse is not in FPS mode, 
        // the click's supposed to go there - don't send it at all to inworld scene.
		if (itemUnderMouse && mouseCursorVisible)
			return false;

        // The mouse press is going to the inworld scene - clear keyboard focus from the QGraphicsScene widget, if any had it so key events also go to inworld scene.
        if (event->type() == QEvent::MouseButtonPress)
            mainView->scene()->clearFocus();

//        QPoint mousePos = e->pos();
        QPoint mousePos = mainView->mapFromGlobal(QCursor::pos());

		MouseEvent mouseEvent;
        mouseEvent.origin = itemUnderMouse ? MouseEvent::PressOriginQtWidget : MouseEvent::PressOriginScene;
		mouseEvent.eventType = (event->type() == QEvent::MouseButtonPress) ? MouseEvent::MousePressed : MouseEvent::MouseReleased;
        mouseEvent.button = (MouseEvent::MouseButton)e->button();
/*        if (e->type() == QEvent::MouseButtonPress)
            printf("press x:%d y:%d\n", mousePos.x(), mousePos.y());
        else
            printf("release x:%d y:%d\n", mousePos.x(), mousePos.y());*/
		mouseEvent.x = mousePos.x();
		mouseEvent.y = mousePos.y();
		mouseEvent.z = 0;
		mouseEvent.relativeX = mouseEvent.x - lastMouseX;
		mouseEvent.relativeY = mouseEvent.y - lastMouseY;

        lastMouseX = mouseEvent.x;
        lastMouseY = mouseEvent.y;

		mouseEvent.globalX = e->globalX();
		mouseEvent.globalY = e->globalY();

		mouseEvent.otherButtons = e->buttons(); ///\todo Can this be trusted?

//		mouseEvent.heldKeys = heldKeys; ///\todo
        mouseEvent.handled = false;

        TriggerMouseEvent(mouseEvent);

        return mouseEvent.handled;//true;
    }

	case QEvent::MouseMove:
    {
        QMouseEvent *e = static_cast<QMouseEvent *>(event);
		QGraphicsItem *itemUnderMouse = GetVisibleItemAtCoords(e->x(), e->y());
        // If there is a graphicsItem under the mouse, don't pass the move message to the inworld scene, unless the inworld scene has captured it.
//        if (mouseCursorVisible)
//		    if ((itemUnderMouse && sceneMouseCapture != SceneMouseCapture) || sceneMouseCapture == QtMouseCapture)
//			    return false;
        if (mouseCursorVisible && itemUnderMouse)
            return false;

        MouseEvent mouseEvent;
		mouseEvent.eventType = MouseEvent::MouseMove;
		mouseEvent.button = (MouseEvent::MouseButton)e->button();

//        QPoint mousePos = e->pos();
        QPoint mousePos = mainView->mapFromGlobal(QCursor::pos());

//        printf("move x:%d, y:%d\n", mousePos.x(), mousePos.y());
        if (mouseCursorVisible)
        {
    		mouseEvent.x = mousePos.x();
	    	mouseEvent.y = mousePos.y();
        }
        else
        {
            // If mouse cursor is hidden, we're in relative "crosshair" mode. In this mode,
            // the mouse absolute coordinates are restricted to stay in the center of the screen.
            mouseEvent.x = mainView->size().width()/2;
            mouseEvent.y = mainView->size().height()/2;
        }
		mouseEvent.z = 0;
		mouseEvent.relativeX = mousePos.x() - lastMouseX;
		mouseEvent.relativeY = mousePos.y() - lastMouseY;
		
		// If there wasn't any change to the mouse relative coords in FPS mode, ignore this event.
		if (!mouseCursorVisible && mouseEvent.relativeX == 0 && mouseEvent.relativeY == 0)
		    return true;
		    
        QWidget *w = dynamic_cast<QWidget*>(obj);
        if (w)
        {
//            QPoint mousePos = w->mapFromGlobal(e->globalPos());
            lastMouseX = mousePos.x();
            lastMouseY = mousePos.y();
//            printf("stored lastmousex: %d, lastmousey: %d\n", lastMouseX, lastMouseY);
//            printf("relx: %d, rely: %d\n", mouseEvent.relativeX, mouseEvent.relativeY);
        }
        else
        {
            lastMouseX = mouseEvent.x;
            lastMouseY = mouseEvent.y;
        }

		mouseEvent.globalX = e->globalX(); // Note that these may jitter when mouse is in relative movement mode.
		mouseEvent.globalY = e->globalY();

		mouseEvent.otherButtons = e->buttons(); ///\todo Can this be trusted?

//		mouseEvent.heldKeys = heldKeys; ///\todo
        mouseEvent.handled = false;

        TriggerMouseEvent(mouseEvent);

        // In relative mouse movement mode, keep the mouse cursor hidden at screen center at all times.
        if (!mouseCursorVisible)
        {
            RecenterMouse();
            return true; // In relative mouse movement mode, the QGraphicsScene does not receive mouse movement at all.
        }

        return mouseEvent.handled;
    }

    case QEvent::Wheel:
    {
        QWheelEvent *e = static_cast<QWheelEvent *>(event);
		QGraphicsItem *itemUnderMouse = GetVisibleItemAtCoords(e->x(), e->y());
		if (itemUnderMouse)
			return false;

		MouseEvent mouseEvent;
		mouseEvent.eventType = MouseEvent::MouseScroll;
        mouseEvent.button = MouseEvent::NoButton;
		mouseEvent.otherButtons = e->buttons();
		mouseEvent.x = e->x();
		mouseEvent.y = e->y();
		mouseEvent.z = 0; // Mouse wheel does not have an absolute z position, only relative.
		mouseEvent.relativeX = 0;
		mouseEvent.relativeY = 0;
		mouseEvent.relativeZ = e->delta();
        mouseEvent.globalX = e->globalX();
        mouseEvent.globalY = e->globalY();

		mouseEvent.otherButtons = e->buttons(); ///\todo Can this be trusted?

//		mouseEvent.heldKeys = heldKeys; ///\todo
        mouseEvent.handled = false;

        TriggerMouseEvent(mouseEvent);	

        return mouseEvent.handled;
	}

    } // ~switch

    return QObject::eventFilter(obj, event);
}

void QtInputService::Update(f64 frametime)
{
    // If at any time we don't have main application window focus, release all input
    // so that keys don't get stuck when the window is reactivated. (The key release might be passed
    // to another window instead and our app keeps thinking that the key is being held down.)
    if (!QApplication::activeWindow())
    {
        SceneReleaseAllKeys();
        SceneReleaseMouseButtons();
    }

    // Move all the double-buffered input events to current events.
    pressedKeys = newKeysPressedQueue;
    newKeysPressedQueue.clear();

    releasedKeys = newKeysReleasedQueue;
    newKeysReleasedQueue.clear();

    pressedMouseButtons = newMouseButtonsPressedQueue;
    newMouseButtonsPressedQueue = 0;

    releasedMouseButtons = newMouseButtonsReleasedQueue;
    newMouseButtonsReleasedQueue = 0;

    // Update the new frame start to all input contexts.
    topLevelInputContext.UpdateFrame();

    for(InputContextList::iterator iter = registeredInputContexts.begin();
        iter != registeredInputContexts.end(); ++iter)
    {
        boost::shared_ptr<InputContext> inputContext = (*iter).lock();
        if (inputContext.get())
            inputContext->UpdateFrame();
    }
}