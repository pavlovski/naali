// For conditions of distribution and use, see copyright notice in license.txt

#include "StableHeaders.h"
#include "ServiceManager.h"
#include "WorldInputModule.h"
#include "InputEvents.h"
#include "InputStateMachine.h"
#include "Framework.h"
#include "EventManager.h"

namespace Input
{
    WorldInputModule::WorldInputModule () 
        : ModuleInterfaceImpl (type_static_)
    {
    }

    WorldInputModule::~WorldInputModule ()
    {
    }

    // virtual
    void WorldInputModule::Load()
    {
    }

    // virtual
    void WorldInputModule::Unload()
    {
    }

    // virtual
    void WorldInputModule::Initialize()
    {
        Foundation::EventManagerPtr eventmgr = GetFramework()-> GetEventManager();

        event_category_id_t eid = eventmgr-> RegisterEventCategory ("Input");

        eventmgr-> RegisterEvent (eid, Input::Events::MOVE_FORWARD_PRESSED, "MoveForwardPressed");
        eventmgr-> RegisterEvent (eid, Input::Events::MOVE_FORWARD_RELEASED, "MoveForwardReleased");

        eventmgr-> RegisterEvent (eid, Input::Events::PY_RESTART, "PyRestart");
        eventmgr-> RegisterEvent (eid, Input::Events::PY_DUPLICATE_DRAG, "PyDuplicateDrag");
        eventmgr-> RegisterEvent (eid, Input::Events::PY_RUN_COMMAND, "PyRunCommand");

        eventmgr-> RegisterEvent (eid, Input::Events::PY_OBJECTEDIT_TOGGLE_MOVE, "PyObjectEditToggleMove");
        eventmgr-> RegisterEvent (eid, Input::Events::PY_OBJECTEDIT_TOGGLE_SCALE, "PyObjectEditToggleScale");
        eventmgr-> RegisterEvent (eid, Input::Events::NAALI_DELETE, "Delete");
        eventmgr-> RegisterEvent (eid, Input::Events::NAALI_UNDO, "Undo");

        eventmgr-> RegisterEvent (eid, Input::Events::NAALI_OBJECTLINK, "ObjectLink");
        eventmgr-> RegisterEvent (eid, Input::Events::NAALI_OBJECTUNLINK, "ObjectUnlink");

        eventmgr-> RegisterEvent (eid, Input::Events::KEY_PRESSED, "KeyPressed");
        eventmgr-> RegisterEvent (eid, Input::Events::KEY_RELEASED, "KeyReleased");

        eventmgr-> RegisterEvent (eid, Input::Events::INWORLD_CLICK, "InWorldClick");
        eventmgr-> RegisterEvent (eid, Input::Events::INWORLD_CLICK_REL, "InWorldClickReleased");

        eventmgr-> RegisterEvent (eid, Input::Events::LEFT_MOUSECLICK_PRESSED, "LeftMouseClickPressed");
        eventmgr-> RegisterEvent (eid, Input::Events::LEFT_MOUSECLICK_RELEASED, "LeftMouseClickReleased");
        eventmgr-> RegisterEvent (eid, Input::Events::RIGHT_MOUSECLICK_PRESSED, "RightMouseClickPressed");
        eventmgr-> RegisterEvent (eid, Input::Events::RIGHT_MOUSECLICK_RELEASED, "RightMouseClickReleased");

        eventmgr-> RegisterEvent (eid, Input::Events::INWORLD_CLICK_BUILD, "InWorldClickBuild");
        eventmgr-> RegisterEvent (eid, Input::Events::INWORLD_CLICK_BUILD_REL, "InWorldClickBuildReleased");

        eventmgr-> RegisterEvent (eid, Input::Events::MOUSEMOVE, "MouseMove");
        eventmgr-> RegisterEvent (eid, Input::Events::MOUSEDRAG, "MouseDrag");
        eventmgr-> RegisterEvent (eid, Input::Events::MOUSEDRAG_STOPPED, "MouseDragStopped");

        // Listen for QEvents generated by the UIView, and generate "Input" category events
        state_machine_ = StateMachinePtr (new WorldInputLogic (GetFramework()));

        GetFramework()-> GetServiceManager()-> 
            RegisterService (Foundation::Service::ST_Input, state_machine_);

        state_machine_-> start();
    }

    // virtual 
    void WorldInputModule::Uninitialize()
    {
        GetFramework()-> GetServiceManager()-> 
            UnregisterService (state_machine_);
    }

    // virtual 
    void WorldInputModule::Update(f64 frametime)
    {
        {
            PROFILE(WorldInputModule_Update);
            
            state_machine_-> Update (frametime);
        }
        RESETPROFILER;
    }
}
